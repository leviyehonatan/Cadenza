// StyleEngine — orchestrates style pattern playback.
//
// Lifecycle:
//   1) AudioEngine drives Transport on the audio thread.
//   2) StyleEngine::onTick is called from AudioEngine's TickCallback.
//   3) Each tick we look for any pattern notes whose tick == (transport tick) mod (section length in ticks).
//      Pattern notes are transposed by PatternTransposer using the live chord,
//      then sent to AudioEngine::noteOn / scheduled note-offs.
//   4) When the section's bar count is reached, loop back to tick 0 (or advance to queued next section).
//
// Thread-safety: setStyle/setSection/setChord may be called from the message thread.
// They publish to atomic snapshots that the audio thread reads.

#pragma once

#include "Style.h"
#include "PatternTransposer.h"
#include "PlaybackDiagnostics.h"
#include "SectionChangeQueue.h"
#include "StyleChangeQueue.h"
#include "../Audio/AudioEngine.h"
#include "../Midi/ChordRecognizer.h"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cadenza::arranger
{
class StyleEngine
{
public:
    explicit StyleEngine(cadenza::audio::AudioEngine& engine);

    // Replace the currently-loaded style. While stopped this applies immediately;
    // while playing it is handed to the audio thread for the next callback.
    void setStyle(std::shared_ptr<const Style> style);
    std::shared_ptr<const Style> currentStyle() const;

    // Choose which section is playing now. While stopped this applies immediately;
    // while playing it is handed to the audio thread for the next callback.
    // `once`=true makes it a one-shot (intro/fill/ending): after its bars elapse
    // the engine switches to `returnTo` (empty -> request stop).
    void setSection(const std::string& name, bool once = false, const std::string& returnTo = {});
    // Queue a section to switch to exactly at the next bar boundary (sample-tight,
    // applied on the audio thread). `once`/`returnTo` as above. Used while playing.
    void requestSection(const std::string& name, bool once, const std::string& returnTo);
    void requestStopAtBarBoundary();
    void cancelSectionRequest();
    std::string currentSection() const;

    // Fired (on the audio thread) when the playing section changes, and when a
    // one-shot ending finishes and playback should stop. Marshal to the UI thread.
    using SectionChangedCallback = std::function<void(const std::string&)>;
    using StopRequestedCallback  = std::function<void()>;
    void setSectionChangedCallback(SectionChangedCallback cb) { m_onSectionChanged = std::move(cb); }
    void setStopRequestedCallback(StopRequestedCallback cb)   { m_onStopRequested  = std::move(cb); }

    // Set the live chord (from MidiRouter's chord recogniser, or from the web UI directly).
    void setChord(const cadenza::midi::Chord& chord);

    // Global transpose applied on top of chord transposition.
    // NOTE: Octave is intentionally NOT handled here — it affects only live
    // right-hand melody input (see MidiRouter::setLiveOctave), never style parts.
    void setGlobalTranspose(int semitones);
    void setKeyTonic(int pitchClass);
    void setEnabled(bool enabled);

    // Humanization amount 0..100 (0 = exact grid/velocities, the original
    // behavior). Adds subtle per-note velocity + late-timing variation so the
    // accompaniment feels played rather than sequenced.
    void setHumanizeAmount(int amount0to100);
    void reapplyCurrentSectionChannelSetup();
    PlaybackDiagnosticResult exportCurrentSectionDiagnostics(const std::string& outputDirectory) const;

    // Connect to the AudioEngine's tick callback. Call after AudioEngine has been constructed.
    void install();

    // Stop all currently-sounding pattern notes (called on Stop or section change).
    void allNotesOff();

private:
    struct ActiveNote {
        int channel;
        int note;            // currently-sounding (played) pitch; updated on re-voice
        int ticksRemaining;
        int velocity = 100;
        const Part* part = nullptr;        // source part (lives in m_style)
        const PatternNote* src = nullptr;  // source note (lives in m_style)
    };

    struct TimedPatternEvent {
        int tick = 0;
        std::size_t sequence = 0;
        std::size_t partIndex = 0;
        const Part* part = nullptr;
        const PatternNote* note = nullptr;
    };

    struct TimedAutomationEvent {
        int tick = 0;
        std::size_t sequence = 0;
        const Part* part = nullptr;
        const AutomationEvent* event = nullptr;
    };

    struct SectionPlaybackCache {
        const Section* section = nullptr;
        std::vector<TimedPatternEvent> patternEvents;
        std::vector<TimedAutomationEvent> automationEvents;
        int maxHumanizeLateTicks = 0;
        std::size_t activeReserve = 0;
    };

    void onTick(int ticksAdvanced, cadenza::audio::Transport& transport);
    // Caller must hold m_publishMutex. Called only while stopped or on audio thread.
    void applyStyleReplacement(std::shared_ptr<const Style> style);
    bool handleBarBoundary(const Style& style);   // audio thread: apply queued/one-shot section changes
    void switchToSection(const Style& style, const std::string& name, bool once, const std::string& returnTo);
    void installPreparedSectionCachesFor(const Style& style);
    void selectSectionCache(const Section* section) noexcept;
    static std::vector<SectionPlaybackCache> buildSectionPlaybackCaches(const Style& style);
    void applySectionChannelSetup(const Section& section);
    void firePatternNotesAtTick(int tickInSection);
    void fireAutomationAtTick(int tickInSection);   // CC/pitch-bend events due this tick
    void resetPartControllers();                     // expression/bend/sustain back to default
    void advanceActiveNotes(int ticksAdvanced);
    // Re-pitch sustained chord-following notes when the live chord changes, so
    // held parts (e.g. pads) follow immediately instead of waiting for the loop.
    void revoiceActiveNotes(const Style& style);

    cadenza::audio::AudioEngine& m_engine;

    // Snapshots — written by the message thread, read by audio thread.
    mutable std::mutex m_publishMutex;
    std::shared_ptr<const Style> m_style;
    std::string m_sectionName;

    // Audio-thread-only state.
    int m_lastFiredTickInSection = -1;
    int m_sectionLengthTicks = 0;
    int m_humanizeLoopCounter = 0;   // bumped each section loop; varies the feel
    std::vector<ActiveNote> m_active;
    std::vector<SectionPlaybackCache> m_sectionCaches;
    std::vector<SectionPlaybackCache> m_preparedSectionCaches;
    const Style* m_preparedStyle = nullptr;
    const SectionPlaybackCache* m_currentSectionCache = nullptr;

    // One-shot / quantized section sequencing.
    bool        m_currentOnce = false;       // current section is a one-shot (audio thread)
    std::string m_currentReturn;             // where to go when the one-shot ends (audio thread)
    int         m_barsUntilReturn = 0;       // bars left before the one-shot returns (audio thread)
    std::string m_pendingSection;            // queued section (guarded by m_publishMutex)
    bool        m_pendingOnce = false;       // (guarded by m_publishMutex)
    std::string m_pendingReturn;             // (guarded by m_publishMutex)
    bool        m_pendingStop = false;       // stop at next bar boundary (guarded by m_publishMutex)
    std::atomic<bool> m_hasPending { false };
    SectionChangeQueue m_immediateSectionChanges;
    StyleChangeQueue m_styleChanges;
    SectionChangedCallback m_onSectionChanged;
    StopRequestedCallback  m_onStopRequested;

    // Chord + transpose context — atomic ints + protected struct.
    mutable std::mutex m_chordMutex;
    cadenza::midi::Chord m_chord;
    std::atomic<int> m_globalTranspose { 0 };
    std::atomic<int> m_keyTonic { 0 };
    std::atomic<int> m_humanizeAmount { 0 };   // 0..100; 0 = off (original behavior)
    std::atomic<bool> m_enabled { true };
    std::atomic<bool> m_chordDirty { false };
    // Set by the message thread (allNotesOff / setStyle) to ask the audio thread to
    // drop all active notes. m_active is mutated ONLY on the audio thread, so the
    // message thread never races the vector (which caused a crash on fast section
    // switches / fills).
    std::atomic<bool> m_panic { false };
};
}
