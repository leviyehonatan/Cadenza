#include "StyleEngine.h"
#include "RuntimePlayback.h"

#include <algorithm>
#include <optional>
#include <sstream>

namespace cadenza::arranger
{
namespace
{
juce::String optionalIntText(const std::optional<int>& value)
{
    return value ? juce::String(*value) : juce::String("-");
}

juce::String firstDrumNoteText(const Part& part, int limit = 20)
{
    std::vector<int> notes;
    notes.reserve(static_cast<std::size_t>(std::min(limit, static_cast<int>(part.notes.size()))));

    for (const auto& note : part.notes) {
        if (std::find(notes.begin(), notes.end(), note.pitch) == notes.end())
            notes.push_back(note.pitch);
        if (static_cast<int>(notes.size()) >= limit)
            break;
    }

    std::ostringstream out;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (i > 0) out << ',';
        out << notes[i];
    }
    return notes.empty() ? juce::String("-") : juce::String(out.str());
}

bool drumNotesLookSuspicious(const Part& part)
{
    if (part.notes.empty())
        return false;

    int outsideCommonGmRange = 0;
    for (const auto& note : part.notes) {
        if (note.pitch < 35 || note.pitch > 81)
            ++outsideCommonGmRange;
    }

    return outsideCommonGmRange > 0;
}
}

StyleEngine::StyleEngine(cadenza::audio::AudioEngine& engine)
    : m_engine(engine)
{
    // Default chord (until set): C major.
    m_chord.rootPitchClass = 0;
    m_chord.quality = cadenza::midi::ChordQuality::Major;
}

void StyleEngine::install()
{
    m_engine.setOnTick([this](int ticksAdvanced, cadenza::audio::Transport& t) {
        onTick(ticksAdvanced, t);
    });
}

void StyleEngine::setStyle(std::shared_ptr<const Style> style)
{
    m_active.clear();
    m_engine.allNotesOff();

    std::lock_guard<std::mutex> lk(m_publishMutex);
    m_style = std::move(style);
    if (m_style) {
        applyStyleTimingToTransport(m_engine.transport(), *m_style, false);
        m_engine.transport().reset();

        if ((m_sectionName.empty() || m_style->findSection(m_sectionName) == nullptr) && !m_style->sections.empty()) {
            m_sectionName = m_style->sections.front().name;
        }
        const auto* sec = m_style->findSection(m_sectionName);
        if (sec) {
            m_sectionLengthTicks = sec->barCount * m_style->beatsPerBar * m_style->ticksPerBeat;
            applySectionChannelSetup(*sec);
        }
    } else {
        m_sectionLengthTicks = 0;
    }
    m_lastFiredTickInSection = -1;
}

std::shared_ptr<const Style> StyleEngine::currentStyle() const
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    return m_style;
}

void StyleEngine::setSection(const std::string& name)
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    if (!m_style) return;
    const auto* sec = m_style->findSection(name);
    if (!sec && !m_style->sections.empty()) {
        // Fallback
        m_sectionName = m_style->sections.front().name;
    } else if (sec) {
        m_sectionName = name;
    }
    const auto* picked = m_style->findSection(m_sectionName);
    if (picked) {
        m_sectionLengthTicks = picked->barCount * m_style->beatsPerBar * m_style->ticksPerBeat;
        applySectionChannelSetup(*picked);
    }
    m_lastFiredTickInSection = -1;
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
    m_active.clear();
    m_engine.allNotesOff();
}

void StyleEngine::onTick(int ticksAdvanced, cadenza::audio::Transport& transport)
{
    if (!m_enabled.load()) return;

    // 1) age active notes; release any that have expired
    advanceActiveNotes(ticksAdvanced);

    // 2) snapshot pointers (single lock acquisition)
    std::shared_ptr<const Style> style;
    std::string sectionName;
    int sectionLength;
    {
        std::lock_guard<std::mutex> lk(m_publishMutex);
        style = m_style;
        sectionName = m_sectionName;
        sectionLength = m_sectionLengthTicks;
    }
    if (!style || sectionName.empty() || sectionLength <= 0) return;

    // 2b) if the chord changed since last tick, re-voice sustained held notes now.
    if (m_chordDirty.exchange(false))
        revoiceActiveNotes(*style);

    // 3) determine which ticks within the section were crossed during this audio block
    const int currentTick = transport.positionTickInt();
    const int startTick   = currentTick - ticksAdvanced;

    for (int t = startTick + 1; t <= currentTick; ++t) {
        const int tickInSection = ((t % sectionLength) + sectionLength) % sectionLength;
        if (tickInSection == m_lastFiredTickInSection) continue;
        firePatternNotesAtTick(tickInSection);
        m_lastFiredTickInSection = tickInSection;
    }
}

void StyleEngine::applySectionChannelSetup(const Section& section)
{
    const auto setups = playbackSetupsForSection(section);

    for (const auto& setup : setups) {
        const Part* part = nullptr;
        for (const auto& candidate : section.parts) {
            if (candidate.midiChannel == setup.sourceChannel && candidate.name == setup.partName) {
                part = &candidate;
                break;
            }
        }

        // Translate Yamaha voicing to what a GM SoundFont can actually play.
        // Yamaha styles select voices/kits via banks a GM SoundFont doesn't have
        // (e.g. drum bank 127, MegaVoice/Super Articulation banks), which makes
        // FluidSynth fail the preset lookup -> wrong or missing sounds.
        if (setup.percussion) {
            // The drum channel is already a percussion channel (FluidSynth
            // synth.drums-channel.active). Never send the Yamaha drum bank (127)
            // or it leaves the GM percussion bank. Keep a standard GM kit unless
            // the program is already a real GM kit slot.
            int kit = setup.program.value_or(0);
            switch (kit) {
                case 0: case 8: case 16: case 24: case 25:
                case 32: case 40: case 48: break;            // a real GM kit slot
                default: kit = 0;                            // unknown Yamaha kit -> Standard
            }
            m_engine.programChange(setup.cadenzaChannel, kit);
        } else {
            // Melodic: a GM SoundFont only has bank 0, and Yamaha voice banks
            // won't resolve. Force GM bank 0 so the program (which follows GM
            // order) lands on the closest GM instrument.
            m_engine.controlChange(setup.cadenzaChannel, 0, 0);    // bank MSB 0
            m_engine.controlChange(setup.cadenzaChannel, 32, 0);   // bank LSB 0
            m_engine.programChange(setup.cadenzaChannel, setup.program.value_or(0));
        }
        if (setup.volume)
            m_engine.controlChange(setup.cadenzaChannel, 7, *setup.volume);
        if (setup.pan)
            m_engine.controlChange(setup.cadenzaChannel, 10, *setup.pan);
        if (setup.reverb)
            m_engine.controlChange(setup.cadenzaChannel, 91, *setup.reverb);
        if (setup.chorus)
            m_engine.controlChange(setup.cadenzaChannel, 93, *setup.chorus);

        juce::Logger::writeToLog(
            juce::String("[Cadenza] Style part setup section=") + juce::String(section.name)
            + " part=" + juce::String(setup.partName)
            + " sourceCh=" + juce::String(setup.sourceChannel)
            + " playbackCh=" + juce::String(setup.cadenzaChannel)
            + " synthCh=" + (setup.synthChannel ? juce::String(*setup.synthChannel) : juce::String("invalid"))
            + " bankMsb=" + optionalIntText(setup.bankMsb)
            + " bankLsb=" + optionalIntText(setup.bankLsb)
            + " program=" + optionalIntText(setup.program)
            + " volume=" + optionalIntText(setup.volume)
            + " pan=" + optionalIntText(setup.pan)
            + " reverb=" + optionalIntText(setup.reverb)
            + " chorus=" + optionalIntText(setup.chorus)
            + " percussion=" + juce::String(setup.percussion ? "true" : "false")
            + " notes=" + juce::String(setup.noteCount));

        if (setup.percussion) {
            juce::Logger::writeToLog(
                juce::String("[Cadenza] Drum part diagnostics section=") + juce::String(section.name)
                + " part=" + juce::String(setup.partName)
                + " sourceCh=" + juce::String(setup.sourceChannel)
                + " playbackCh=" + juce::String(setup.cadenzaChannel)
                + " synthCh=" + (setup.synthChannel ? juce::String(*setup.synthChannel) : juce::String("invalid"))
                + " bankMsb=" + optionalIntText(setup.bankMsb)
                + " bankLsb=" + optionalIntText(setup.bankLsb)
                + " program=" + optionalIntText(setup.program)
                + " percussion=" + juce::String(setup.percussion ? "true" : "false")
                + " firstNotes=" + (part != nullptr ? firstDrumNoteText(*part) : juce::String("-")));

            if (part != nullptr && drumNotesLookSuspicious(*part)) {
                juce::Logger::writeToLog(
                    juce::String("[Cadenza] WARNING: Drum notes include pitches outside common GM drum range 35..81; ")
                    + "section=" + juce::String(section.name)
                    + " part=" + juce::String(setup.partName)
                    + " sourceCh=" + juce::String(setup.sourceChannel)
                    + " playbackCh=" + juce::String(setup.cadenzaChannel)
                    + " firstNotes=" + firstDrumNoteText(*part));
            }
        }
    }
}

void StyleEngine::firePatternNotesAtTick(int tickInSection)
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

    // Snapshot chord + transpose context for consistent transposition this tick.
    // Octave is deliberately NOT applied to style parts (see makeStylePlaybackContext).
    cadenza::midi::Chord chord;
    {
        std::lock_guard<std::mutex> lk(m_chordMutex);
        chord = m_chord;
    }
    const TransposeContext ctx = makeStylePlaybackContext(chord, m_keyTonic.load(), m_globalTranspose.load());

    for (const auto& part : section->parts) {
        for (const auto& note : part.notes) {
            if (note.tick != tickInSection) continue;

            auto maybeMidi = playbackNoteForPart(part, note, ctx);
            if (!maybeMidi) continue;

            const int midi = *maybeMidi;
            const int channel = playbackChannelForPart(part);
            const bool percussion = part.percussion || channel == 10;
            if (percussion) {
                const auto remap = drumNoteForPlayback(part, note.pitch);
                if (remap.remapped) {
                    juce::Logger::writeToLog(
                        juce::String("[Cadenza] Yamaha/XG drum note remap part=") + juce::String(part.name)
                        + " original=" + juce::String(remap.originalNote)
                        + " remapped=" + juce::String(remap.playbackNote)
                        + " bankMsb=" + optionalIntText(part.bankMsb)
                        + " bankLsb=" + optionalIntText(part.bankLsb)
                        + " program=" + optionalIntText(part.program));
                }
            }

            m_engine.noteOn(channel, midi, note.velocity);

            // Schedule a note-off after `duration` ticks. Keep source pointers so
            // sustained notes can be re-voiced if the chord changes mid-hold.
            m_active.push_back(ActiveNote{ channel, midi,
                                           std::max(1, note.duration), note.velocity,
                                           &part, &note });
        }
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

        const auto maybe = playbackNoteForPart(*a.part, *a.src, ctx);
        if (!maybe || *maybe == a.note) continue;

        m_engine.noteOff(a.channel, a.note);
        m_engine.noteOn(a.channel, *maybe, a.velocity);
        a.note = *maybe;
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
