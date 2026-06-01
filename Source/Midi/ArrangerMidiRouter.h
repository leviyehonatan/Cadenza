// ArrangerMidiRouter — clean-room arranger MIDI router and chord recogniser.
//
// Adopted into Cadenza from the standalone arranger_cleanroom project.
// Pure C++17; no JUCE dependency. Lives in cadenza_core.
//
// Responsibilities:
//   * Split incoming notes between "chord zone" (below split point) and
//     "melody zone" (above), with Full-Keyboard modes routing everything
//     as chord input.
//   * Recognise chords across 18 templates including jazz extensions and
//     reduced 2-note voicings (single-finger arranger chords).
//   * Slash-chord detection (FingeredOnBass mode -> "C/E", "G7/B", etc.).
//   * Syncro Start/Stop events fired when the first chord note arrives
//     and when the last one is released.
//   * Chord Memory: keep the last detected chord while no notes are held.
//   * Per-note reference counting so duplicate note-on / overlapping
//     events don't desync the held-note tracking.
//
// Note: the namespace stays "arranger" to keep this module clearly
// identifiable as the clean-room module rather than blending it with
// Cadenza's other midi code (cadenza::midi).

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace arranger {

enum class ChordDetectionMode {
    SingleFinger,
    Fingered,
    FingeredIncomplete,
    FingeredOnBass,
    MultiFinger,
    FullKeyboard,
    FullKeyboardNoInterval,
};

enum class RouteTarget {
    ChordSide,
    MelodySide,
    Ignored,
};

enum class SyncEvent {
    Started,
    Stopped,
};

struct ArrangerState {
    std::uint8_t splitNote = 60;
    ChordDetectionMode chordMode = ChordDetectionMode::Fingered;
    bool syncroStarted = false;
    bool chordMemory = false;
    bool syncroStopOnRelease = true;
};

struct MidiMessage {
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;

    static MidiMessage noteOn(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity);
    static MidiMessage noteOff(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity = 0);
};

struct RoutedMidiMessage {
    RouteTarget target = RouteTarget::Ignored;
    MidiMessage message;
};

struct ChordRecognitionResult {
    std::uint8_t root = 0;
    std::string quality;
    std::optional<std::uint8_t> bass;
    bool inversion = false;
    std::string displayName;
};

class ArrangerMidiRouter {
public:
    using NoteCountObserver = std::function<void(std::size_t activeChordNoteCount)>;
    using SyncObserver = std::function<void(SyncEvent event)>;

    explicit ArrangerMidiRouter(ArrangerState state = {});

    const ArrangerState& state() const noexcept;
    void setSplitNote(std::uint8_t splitNote) noexcept;
    void setChordDetectionMode(ChordDetectionMode mode) noexcept;
    void setChordMemory(bool enabled) noexcept;

    void addNoteCountObserver(NoteCountObserver observer);
    void addSyncObserver(SyncObserver observer);

    void setSyncroStopOnRelease(bool enabled) noexcept;

    std::vector<RoutedMidiMessage> handle(const MidiMessage& message);

    std::size_t activeChordNoteCount() const noexcept;
    bool isChordNoteActive(std::uint8_t note) const noexcept;
    bool isMelodyNoteActive(std::uint8_t note) const noexcept;
    std::optional<ChordRecognitionResult> detectChord() const;

    void reset();

private:
    static bool isNoteMessage(const MidiMessage& message) noexcept;
    static bool isNoteOn(const MidiMessage& message) noexcept;
    static bool isNoteOff(const MidiMessage& message) noexcept;
    static std::uint8_t messageType(const MidiMessage& message) noexcept;
    static std::uint8_t clampMidi7Bit(std::uint8_t value) noexcept;
    static bool isFullKeyboardMode(ChordDetectionMode mode) noexcept;

    RouteTarget routeForNote(std::uint8_t note) const noexcept;
    void applyNoteOn(RouteTarget target, std::uint8_t note);
    void applyNoteOff(RouteTarget target, std::uint8_t note);
    void notifyNoteCountIfChanged(std::size_t previousCount);
    void maybeStartSyncro(std::size_t previousCount);
    void maybeStopSyncro(std::size_t previousCount);

    ArrangerState state_;
    std::array<unsigned, 128> chordNoteCounters_{};
    std::array<unsigned, 128> melodyNoteCounters_{};
    std::size_t activeChordNoteCount_ = 0;
    mutable std::optional<ChordRecognitionResult> currentChord_;

    std::vector<NoteCountObserver> noteCountObservers_;
    std::vector<SyncObserver> syncObservers_;
};

} // namespace arranger
