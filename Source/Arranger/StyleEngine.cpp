#include "StyleEngine.h"
#include "RuntimePlayback.h"
#include "../MusicalTiming.h"

#include <algorithm>
#include <optional>

namespace cadenza::arranger
{
namespace
{
constexpr std::size_t kDefaultActiveNoteReserve = 16384;
}

StyleEngine::StyleEngine(cadenza::audio::AudioEngine& engine)
    : m_engine(engine)
{
    // Default chord (until set): C major.
    m_chord.rootPitchClass = 0;
    m_chord.quality = cadenza::midi::ChordQuality::Major;
    m_active.reserve(kDefaultActiveNoteReserve);
}

void StyleEngine::install()
{
    m_engine.setOnTick([this](int ticksAdvanced, cadenza::audio::Transport& t) {
        onTick(ticksAdvanced, t);
    });
}

void StyleEngine::setStyle(std::shared_ptr<const Style> style)
{
    const Style* preparedStyle = style.get();
    auto preparedCaches = style ? buildSectionPlaybackCaches(*style)
                                : std::vector<SectionPlaybackCache>{};

    m_panic.store(true);          // audio thread drops active notes on its next tick
    m_engine.allNotesOff();
    m_immediateSectionChanges.clear();

    if (m_engine.transport().playing()) {
        {
            std::lock_guard<std::mutex> lk(m_publishMutex);
            m_preparedStyle = preparedStyle;
            m_preparedSectionCaches = std::move(preparedCaches);
        }
        m_styleChanges.publish(std::move(style));
        return;
    }

    m_styleChanges.clear();
    std::lock_guard<std::mutex> lk(m_publishMutex);
    m_preparedStyle = preparedStyle;
    m_preparedSectionCaches = std::move(preparedCaches);
    applyStyleReplacement(std::move(style));
}

void StyleEngine::applyStyleReplacement(std::shared_ptr<const Style> style)
{
    m_style = std::move(style);
    m_currentSectionCache = nullptr;
    if (m_style) {
        installPreparedSectionCachesFor(*m_style);
        applyStyleTimingToTransport(m_engine.transport(), *m_style, false);
        m_engine.transport().reset();

        if ((m_sectionName.empty() || m_style->findSection(m_sectionName) == nullptr) && !m_style->sections.empty()) {
            m_sectionName = m_style->sections.front().name;
        }
        const auto* sec = m_style->findSection(m_sectionName);
        if (sec) {
            m_sectionLengthTicks = sec->barCount * cadenza::ticksPerBar(
                m_style->ticksPerBeat, m_style->beatsPerBar, m_style->beatUnit);
            selectSectionCache(sec);
            applySectionChannelSetup(*sec);
        }
    } else {
        m_sectionLengthTicks = 0;
        m_sectionCaches.clear();
        m_preparedSectionCaches.clear();
        m_preparedStyle = nullptr;
    }
    m_lastFiredTickInSection = -1;
    m_currentOnce = false;            // reset one-shot / queued sequencing for the new style
    m_currentReturn.clear();
    m_barsUntilReturn = 0;
    m_hasPending.store(false);
}

std::shared_ptr<const Style> StyleEngine::currentStyle() const
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    return m_style;
}

std::vector<StyleEngine::SectionPlaybackCache> StyleEngine::buildSectionPlaybackCaches(const Style& style)
{
    std::vector<SectionPlaybackCache> caches;
    caches.reserve(style.sections.size());

    for (const auto& section : style.sections) {
        SectionPlaybackCache cache;
        cache.section = &section;

        std::size_t noteCount = 0;
        std::size_t automationCount = 0;
        for (const auto& part : section.parts) {
            noteCount += part.notes.size();
            automationCount += part.automation.size();
        }

        cache.patternEvents.reserve(noteCount);
        cache.automationEvents.reserve(automationCount);
        cache.activeReserve = std::max(kDefaultActiveNoteReserve, noteCount * 2);

        std::size_t patternSequence = 0;
        std::size_t automationSequence = 0;
        for (std::size_t partIndex = 0; partIndex < section.parts.size(); ++partIndex) {
            const auto& part = section.parts[partIndex];
            cache.maxHumanizeLateTicks = std::max(
                cache.maxHumanizeLateTicks,
                humanizeProfileForPart(part, 100).maxLateTicks);

            for (const auto& note : part.notes) {
                cache.patternEvents.push_back(TimedPatternEvent{
                    note.tick, patternSequence++, partIndex, &part, &note
                });
            }

            for (const auto& ev : part.automation) {
                cache.automationEvents.push_back(TimedAutomationEvent{
                    ev.tick, automationSequence++, &part, &ev
                });
            }
        }

        std::stable_sort(cache.patternEvents.begin(), cache.patternEvents.end(),
            [](const TimedPatternEvent& a, const TimedPatternEvent& b) {
                return a.tick < b.tick;
            });
        std::stable_sort(cache.automationEvents.begin(), cache.automationEvents.end(),
            [](const TimedAutomationEvent& a, const TimedAutomationEvent& b) {
                return a.tick < b.tick;
            });

        caches.push_back(std::move(cache));
    }

    return caches;
}

void StyleEngine::installPreparedSectionCachesFor(const Style& style)
{
    if (m_preparedStyle == &style) {
        m_sectionCaches.swap(m_preparedSectionCaches);
        m_preparedStyle = nullptr;
    } else {
        m_sectionCaches = buildSectionPlaybackCaches(style);
    }

    if (!m_engine.transport().playing()) {
        std::size_t reserve = kDefaultActiveNoteReserve;
        for (const auto& cache : m_sectionCaches)
            reserve = std::max(reserve, cache.activeReserve);
        if (m_active.capacity() < reserve)
            m_active.reserve(reserve);
    }
}

void StyleEngine::selectSectionCache(const Section* section) noexcept
{
    m_currentSectionCache = nullptr;
    if (section == nullptr)
        return;

    for (const auto& cache : m_sectionCaches) {
        if (cache.section == section) {
            m_currentSectionCache = &cache;
            return;
        }
    }
}

// Caller must hold m_publishMutex. Updates the playing section + its length, runs
// channel setup, and records one-shot state for the bar-boundary handler.
void StyleEngine::switchToSection(const Style& style, const std::string& name,
                                  bool once, const std::string& returnTo)
{
    const auto* sec = style.findSection(name);
    if (!sec && !style.sections.empty())
        m_sectionName = style.sections.front().name;
    else if (sec)
        m_sectionName = name;

    const auto* picked = style.findSection(m_sectionName);
    if (picked) {
        m_sectionLengthTicks = picked->barCount * cadenza::ticksPerBar(
            style.ticksPerBeat, style.beatsPerBar, style.beatUnit);
        selectSectionCache(picked);
        applySectionChannelSetup(*picked);
    }
    m_currentOnce     = once;
    m_currentReturn   = returnTo;
    m_barsUntilReturn = (once && picked) ? std::max(1, picked->barCount) : 0;
    m_lastFiredTickInSection = -1;
}

void StyleEngine::setSection(const std::string& name, bool once, const std::string& returnTo)
{
    if (m_engine.transport().playing()) {
        m_immediateSectionChanges.publish(name, once, returnTo);
        return;
    }

    m_immediateSectionChanges.clear();
    std::lock_guard<std::mutex> lk(m_publishMutex);
    m_hasPending.store(false);   // an immediate change cancels any queued one
    m_pendingStop = false;
    if (!m_style) return;
    switchToSection(*m_style, name, once, returnTo);
}

void StyleEngine::requestSection(const std::string& name, bool once, const std::string& returnTo)
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    m_pendingSection = name;
    m_pendingOnce    = once;
    m_pendingReturn  = returnTo;
    m_pendingStop    = false;
    m_hasPending.store(true);
}

void StyleEngine::requestStopAtBarBoundary()
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    m_hasPending.store(false);
    m_pendingStop = true;
}

void StyleEngine::cancelSectionRequest()
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    m_hasPending.store(false);
    m_pendingStop = false;
}

bool StyleEngine::handleBarBoundary(const Style& style)
{
    bool changed = false, stop = false;
    std::string newName;
    {
        std::lock_guard<std::mutex> lk(m_publishMutex);
        if (m_pendingStop) {
            m_pendingStop = false;
            stop = true;
        } else if (m_hasPending.load()) {
            m_hasPending.store(false);
            switchToSection(style, m_pendingSection, m_pendingOnce, m_pendingReturn);
            newName = m_sectionName;
            changed = true;
        } else if (m_currentOnce && --m_barsUntilReturn <= 0) {
            if (m_currentReturn.empty()) { m_currentOnce = false; stop = true; }
            else { switchToSection(style, m_currentReturn, false, {}); newName = m_sectionName; changed = true; }
        }
    }
    if (changed) {
        m_active.clear();          // drop the old section's notes (audio thread)
        m_engine.allNotesOff();
        if (m_onSectionChanged) m_onSectionChanged(newName);
    }
    if (stop) {
        m_active.clear();
        m_engine.stopFromAudioThread();
        if (m_onStopRequested)
            m_onStopRequested();
    }
    return stop;
}

std::string StyleEngine::currentSection() const
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    return m_sectionName;
}

void StyleEngine::setChord(const cadenza::midi::Chord& chord)
{
    std::lock_guard<std::mutex> lk(m_chordMutex);
    if (m_chord.rootPitchClass != chord.rootPitchClass || m_chord.quality != chord.quality)
        m_chordDirty.store(true);   // ask the audio thread to re-voice held notes
    m_chord = chord;
}

void StyleEngine::setGlobalTranspose(int semitones) { m_globalTranspose.store(semitones); }
void StyleEngine::setHumanizeAmount(int amount0to100) { m_humanizeAmount.store(std::clamp(amount0to100, 0, 100)); }
void StyleEngine::setKeyTonic(int pc)               { m_keyTonic.store(pc); }
void StyleEngine::setEnabled(bool enabled)           { m_enabled.store(enabled); }

void StyleEngine::reapplyCurrentSectionChannelSetup()
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    if (!m_style || m_sectionName.empty())
        return;

    if (const auto* section = m_style->findSection(m_sectionName))
        applySectionChannelSetup(*section);
}

PlaybackDiagnosticResult StyleEngine::exportCurrentSectionDiagnostics(const std::string& outputDirectory) const
{
    std::shared_ptr<const Style> style;
    std::string sectionName;
    {
        std::lock_guard<std::mutex> lk(m_publishMutex);
        style = m_style;
        sectionName = m_sectionName;
    }

    if (!style) {
        PlaybackDiagnosticResult result;
        result.error = "no style loaded";
        return result;
    }

    cadenza::midi::Chord chord;
    {
        std::lock_guard<std::mutex> lk(m_chordMutex);
        chord = m_chord;
    }
    const TransposeContext ctx = makeStylePlaybackContext(chord, m_keyTonic.load(), m_globalTranspose.load());

    return exportPlaybackDiagnostics(*style, sectionName, ctx, outputDirectory, 4);
}

void StyleEngine::allNotesOff()
{
    // Don't touch m_active here — it's audio-thread-only. Ask the audio thread to
    // clear it on its next tick, and silence the synth now (thread-safe).
    m_panic.store(true);
    m_engine.allNotesOff();
}

void StyleEngine::onTick(int ticksAdvanced, cadenza::audio::Transport& transport)
{
    // 0) honour a panic request from the message thread (allNotesOff / setStyle):
    //    clear active notes here so the vector is only ever mutated on this thread.
    if (m_panic.exchange(false))
        m_active.clear();

    // 1) Replace the style only on the audio thread while transport is running.
    {
        std::unique_lock<std::mutex> lk(m_publishMutex, std::try_to_lock);
        if (lk.owns_lock()) {
            if (auto style = m_styleChanges.tryTake()) {
                m_active.clear();
                applyStyleReplacement(std::move(*style));
            }
        }
    }

    // 2) Apply an immediate section request published by the message thread.
    // Non-blocking takes defer by one callback if either producer is publishing.
    {
        std::unique_lock<std::mutex> lk(m_publishMutex, std::try_to_lock);
        if (lk.owns_lock()) {
            if (auto request = m_immediateSectionChanges.tryTake()) {
                m_hasPending.store(false);
                m_pendingStop = false;
                if (m_style)
                    switchToSection(*m_style, request->name, request->once, request->returnTo);
            }
        }
    }

    if (!m_enabled.load()) return;

    // 3) age active notes; release any that have expired
    advanceActiveNotes(ticksAdvanced);

    // 4) snapshot pointers (single lock acquisition)
    std::shared_ptr<const Style> style;
    bool hasSection = false;
    int sectionLength;
    {
        std::lock_guard<std::mutex> lk(m_publishMutex);
        style = m_style;
        hasSection = !m_sectionName.empty();
        sectionLength = m_sectionLengthTicks;
    }
    if (!style || !hasSection || sectionLength <= 0) return;

    // 4b) if the chord changed since last tick, re-voice sustained held notes now.
    if (m_chordDirty.exchange(false))
        revoiceActiveNotes(*style);

    // 5) determine which ticks within the section were crossed during this audio block
    const int currentTick = transport.positionTickInt();
    const int startTick   = currentTick - ticksAdvanced;

    const int barTicks = cadenza::ticksPerBar(
        style->ticksPerBeat, style->beatsPerBar, style->beatUnit);
    for (int t = startTick + 1; t <= currentTick; ++t) {
        // At each bar boundary, apply a queued section change or one-shot return
        // (sample-tight, on the audio thread). This may change m_sectionLengthTicks.
        if (barTicks > 0 && t > 0 && (t % barTicks) == 0
            && handleBarBoundary(*style))
            return;

        const int secLen = m_sectionLengthTicks;
        if (secLen <= 0) continue;
        const int tickInSection = ((t % secLen) + secLen) % secLen;
        if (tickInSection == m_lastFiredTickInSection) continue;
        // At the loop restart, return expression/bend/sustain to their default
        // so a part that faded out (or bent up) at the end of the bar doesn't
        // start the next loop stuck quiet/detuned before its first event.
        if (tickInSection == 0) {
            resetPartControllers();
            ++m_humanizeLoopCounter;   // vary the humanized feel each section pass
        }
        firePatternNotesAtTick(tickInSection);
        fireAutomationAtTick(tickInSection);
        m_lastFiredTickInSection = tickInSection;
    }
}

void StyleEngine::applySectionChannelSetup(const Section& section)
{
    const auto setups = playbackSetupsForSection(section);

    for (const auto& setup : setups) {
        // Send the style's voicing as recorded. Yamaha styles are XG-based, so an
        // XG/GS SoundFont (e.g. Timbres of Heaven) resolves the original bank+
        // program to the intended voice/drum kit; with a plain GM SoundFont
        // FluidSynth falls back to the GM voice for that program. (An earlier
        // attempt to force GM bank 0 here threw away the richer XG voices.)
        if (setup.bankMsb)
            m_engine.controlChange(setup.cadenzaChannel, 0, *setup.bankMsb);
        if (setup.bankLsb)
            m_engine.controlChange(setup.cadenzaChannel, 32, *setup.bankLsb);
        if (setup.program)
            m_engine.programChange(setup.cadenzaChannel, *setup.program);
        else if (setup.percussion)
            m_engine.programChange(setup.cadenzaChannel, 0);
        if (setup.volume)
            m_engine.controlChange(setup.cadenzaChannel, 7, *setup.volume);
        if (setup.pan)
            m_engine.controlChange(setup.cadenzaChannel, 10, *setup.pan);
        if (setup.reverb)
            m_engine.controlChange(setup.cadenzaChannel, 91, *setup.reverb);
        if (setup.chorus)
            m_engine.controlChange(setup.cadenzaChannel, 93, *setup.chorus);

        // Start every section with neutral expression/bend/sustain so a new
        // section never inherits a stuck swell or bend from the previous one.
        m_engine.controlChange(setup.cadenzaChannel, 11, 127);
        m_engine.controlChange(setup.cadenzaChannel, 1, 0);
        m_engine.controlChange(setup.cadenzaChannel, 64, 0);
        m_engine.pitchBend(setup.cadenzaChannel, 8192);
    }
}

void StyleEngine::firePatternNotesAtTick(int tickInSection)
{
    const auto* cache = m_currentSectionCache;
    if (cache == nullptr) return;

    // Snapshot chord + transpose context for consistent transposition this tick.
    // Octave is deliberately NOT applied to style parts (see makeStylePlaybackContext).
    cadenza::midi::Chord chord;
    {
        std::lock_guard<std::mutex> lk(m_chordMutex);
        chord = m_chord;
    }
    const TransposeContext ctx = makeStylePlaybackContext(chord, m_keyTonic.load(), m_globalTranspose.load());

    const int humanize = m_humanizeAmount.load();
    const int secLen   = m_sectionLengthTicks;
    const int firstPossibleTick = humanize > 0
        ? tickInSection - cache->maxHumanizeLateTicks
        : tickInSection;

    const auto first = std::lower_bound(
        cache->patternEvents.begin(), cache->patternEvents.end(), firstPossibleTick,
        [](const TimedPatternEvent& ev, int tick) { return ev.tick < tick; });
    const auto last = std::upper_bound(
        first, cache->patternEvents.end(), tickInSection,
        [](int tick, const TimedPatternEvent& ev) { return tick < ev.tick; });

    auto eventIsDue = [&](const TimedPatternEvent& ev, std::uint32_t& seedOut,
                          HumanizeProfile& profileOut) {
        if (ev.part == nullptr || ev.note == nullptr)
            return false;

        profileOut = humanizeProfileForPart(*ev.part, humanize);
        seedOut = humanizeSeed(ev.note->tick, ev.note->pitch,
                               static_cast<int>(ev.partIndex), m_humanizeLoopCounter);
        int late = humanizeLateTicks(seedOut, profileOut.maxLateTicks);
        if (secLen > 0)
            late = std::min(late, std::max(0, secLen - 1 - ev.note->tick));
        return ev.note->tick + late == tickInSection;
    };

    auto fireEvent = [&](const TimedPatternEvent& ev, std::uint32_t seed,
                         const HumanizeProfile& hp) {
        const auto& part = *ev.part;
        const auto& note = *ev.note;

        auto maybeMidi = playbackNoteForPart(part, note, ctx);
        if (!maybeMidi) return;

        const int midi = *maybeMidi;
        const int channel = playbackChannelForPart(part);
        const int velocity = humanizeVelocity(note.velocity, seed, hp.velocityJitter);
        m_engine.noteOn(channel, midi, velocity);

        // Schedule a note-off after `duration` ticks. Keep source pointers so
        // sustained notes can be re-voiced if the chord changes mid-hold.
        m_active.push_back(ActiveNote{ channel, midi,
                                       std::max(1, note.duration), velocity,
                                       &part, &note });
    };

    if (humanize <= 0) {
        for (auto it = first; it != last; ++it) {
            std::uint32_t seed = 0;
            HumanizeProfile hp;
            if (eventIsDue(*it, seed, hp))
                fireEvent(*it, seed, hp);
        }
        return;
    }

    std::size_t lastSequence = 0;
    bool haveLastSequence = false;
    for (;;) {
        const TimedPatternEvent* next = nullptr;
        std::uint32_t nextSeed = 0;
        HumanizeProfile nextProfile;

        for (auto it = first; it != last; ++it) {
            if (haveLastSequence && it->sequence <= lastSequence)
                continue;
            std::uint32_t seed = 0;
            HumanizeProfile hp;
            if (!eventIsDue(*it, seed, hp))
                continue;
            if (next == nullptr || it->sequence < next->sequence) {
                next = &*it;
                nextSeed = seed;
                nextProfile = hp;
            }
        }

        if (next == nullptr)
            break;

        fireEvent(*next, nextSeed, nextProfile);
        lastSequence = next->sequence;
        haveLastSequence = true;
    }
}

void StyleEngine::fireAutomationAtTick(int tickInSection)
{
    const auto* cache = m_currentSectionCache;
    if (cache == nullptr) return;

    const auto first = std::lower_bound(
        cache->automationEvents.begin(), cache->automationEvents.end(), tickInSection,
        [](const TimedAutomationEvent& ev, int tick) { return ev.tick < tick; });
    const auto last = std::upper_bound(
        first, cache->automationEvents.end(), tickInSection,
        [](int tick, const TimedAutomationEvent& ev) { return tick < ev.tick; });

    for (auto it = first; it != last; ++it) {
        if (it->part == nullptr || it->event == nullptr)
            continue;
        const int channel = playbackChannelForPart(*it->part);
        if (it->event->type == AutomationEvent::kPitchBend)
            m_engine.pitchBend(channel, it->event->value);
        else
            m_engine.controlChange(channel, it->event->type, it->event->value);
    }
}

void StyleEngine::resetPartControllers()
{
    std::shared_ptr<const Style> style;
    std::string sectionName;
    {
        std::lock_guard<std::mutex> lk(m_publishMutex);
        style = m_style;
        sectionName = m_sectionName;
    }
    if (!style) return;

    const Section* section = style->findSection(sectionName);
    if (!section) return;

    // Only reset channels that actually use automation, so we don't spam MIDI on
    // parts that never bend or swell.
    for (const auto& part : section->parts) {
        if (part.automation.empty()) continue;
        const int channel = playbackChannelForPart(part);
        m_engine.controlChange(channel, 11, 127);   // expression -> full
        m_engine.controlChange(channel, 1, 0);      // modulation -> off
        m_engine.controlChange(channel, 64, 0);     // sustain -> off
        m_engine.pitchBend(channel, 8192);          // pitch bend -> centre
    }
}

void StyleEngine::revoiceActiveNotes(const Style& style)
{
    if (m_active.empty()) return;

    cadenza::midi::Chord chord;
    {
        std::lock_guard<std::mutex> lk(m_chordMutex);
        chord = m_chord;
    }
    const TransposeContext ctx = makeStylePlaybackContext(chord, m_keyTonic.load(), m_globalTranspose.load());

    // Only re-articulate notes that are still sustaining (>= ~1 beat left), so we
    // shift held pads/strings without machine-gunning short percussive hits.
    const int sustainThreshold = std::max(1, style.ticksPerBeat);

    for (auto& a : m_active) {
        if (a.part == nullptr || a.src == nullptr) continue;
        if (a.part->percussion || playbackChannelForPart(*a.part) == 10) continue;  // never re-pitch drums
        if (a.src->role == NoteRole::Absolute) continue;                // fixed parts don't follow
        if (a.ticksRemaining < sustainThreshold) continue;              // leave short notes alone

        // Yamaha RTR: what this part does with notes sustaining across a chord
        // change. Stop cuts them; the ToRoot rules land on the new root; the
        // others re-articulate at the recomputed pitch. (PitchShift/Retrigger
        // both retrigger here: MIDI synths can't glide a sounding note.)
        const auto rule = a.part->yamahaPolicy
            ? a.part->yamahaPolicy->retriggerRule
            : YamahaRetriggerRule::Unknown;

        if (rule == YamahaRetriggerRule::Stop) {
            m_engine.noteOff(a.channel, a.note);
            a.ticksRemaining = 0;   // pruned by the next advanceActiveNotes
            continue;
        }

        std::optional<int> target;
        if (rule == YamahaRetriggerRule::PitchShiftToRoot
            || rule == YamahaRetriggerRule::RetriggerToRoot) {
            // Re-voice as if the source note had been a root-role note, through
            // the normal playback path — so the bass keeps its low-octave
            // anchor, policy note limits and per-part octave offsets apply, and
            // the part doesn't drift register across chord changes.
            PatternNote asRoot = *a.src;
            asRoot.role = NoteRole::ChordRoot;
            target = playbackNoteForPart(*a.part, asRoot, ctx);
        } else {
            target = playbackNoteForPart(*a.part, *a.src, ctx);
        }

        if (!target || *target == a.note) continue;

        m_engine.noteOff(a.channel, a.note);
        m_engine.noteOn(a.channel, *target, a.velocity);
        a.note = *target;
    }
}

void StyleEngine::advanceActiveNotes(int ticksAdvanced)
{
    if (ticksAdvanced <= 0 || m_active.empty()) return;
    for (auto& a : m_active) a.ticksRemaining -= ticksAdvanced;

    // Issue note-off for any expired notes, then prune.
    m_active.erase(
        std::remove_if(m_active.begin(), m_active.end(), [&](const ActiveNote& a) {
            if (a.ticksRemaining <= 0) {
                m_engine.noteOff(a.channel, a.note);
                return true;
            }
            return false;
        }),
        m_active.end());
}
}
