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
#include "../Audio/AudioEngine.h"
#include "../Midi/ChordRecognizer.h"

#include <atomic>
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

    // Replace the currently-loaded style. Snapshot taken atomically.
    void setStyle(std::shared_ptr<const Style> style);
    std::shared_ptr<const Style> currentStyle() const;

    // Choose which section ("intro", "mainA", "mainB", "ending", ...) is playing.
    // If the named section isn't found, falls back to the first available.
    void setSection(const std::string& name);
    std::string currentSection() const;

    // Set the live chord (from MidiRouter's chord recogniser, or from the web UI directly).
    void setChord(const cadenza::midi::Chord& chord);

    // Global transpose applied on top of chord transposition.
    // NOTE: Octave is intentionally NOT handled here — it affects only live
    // right-hand melody input (see MidiRouter::setLiveOctave), never style parts.
    void setGlobalTranspose(int semitones);
    void setKeyTonic(int pitchClass);
    void setEnabled(bool enabled);
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

    void onTick(int ticksAdvanced, cadenza::audio::Transport& transport);
    void applySectionChannelSetup(const Section& section);
    void firePatternNotesAtTick(int tickInSection);
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
    std::vector<ActiveNote> m_active;

    // Chord + transpose context — atomic ints + protected struct.
    mutable std::mutex m_chordMutex;
    cadenza::midi::Chord m_chord;
    std::atomic<int> m_globalTranspose { 0 };
    std::atomic<int> m_keyTonic { 0 };
    std::atomic<bool> m_enabled { true };
    std::atomic<bool> m_chordDirty { false };
};
}
