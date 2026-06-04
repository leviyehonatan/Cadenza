// MidiRouter — JUCE adapter around arranger::ArrangerMidiRouter.
//
// Owns a juce::MidiInput and forwards every incoming juce::MidiMessage to
// the pure-C++ ArrangerMidiRouter, which handles:
//   * chord-zone vs melody-zone split (with Full-Keyboard modes)
//   * 18-template chord recognition + slash chords
//   * Syncro Start/Stop events (first chord note in / last chord note out)
//   * Chord Memory
//   * Per-note reference counting (handles duplicate note-on events)
//
// This wrapper only adapts JUCE I/O. All MIDI/arranger logic lives in
// cadenza_core where it's fully unit-tested.

#pragma once

#include "ChordRecognizer.h"
#include "ArrangerMidiRouter.h"
#include "LiveMelodyVoice.h"
#include "RightHand.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace cadenza::midi
{
// Snapshot fired on every incoming MIDI message — used for the debug panel.
// Captures the raw message + the router's interpretation of it.
struct MidiDebugEvent
{
    std::string deviceName;
    int status = 0;        // raw status byte (0x80..0xFF), 0 if message had no bytes
    int note = -1;         // MIDI note number, -1 if not a note message
    int velocity = -1;     // 0..127 for note-on/off, -1 otherwise
    std::string route;     // "chord", "melody", "ignored"
    std::string chordName; // current detected chord display name; "" if none
    std::string sync;      // "start" if syncro just started, "stop" if just stopped, "none" otherwise
};

class MidiRouter final : public juce::MidiInputCallback
{
public:
    using NoteCallback  = std::function<void(int channel, int note, int velocity, bool isNoteOn)>;
    using ChordCallback = std::function<void(const std::optional<Chord>& chord, const std::string& displayName)>;
    using SyncCallback  = std::function<void(bool started)>;  // true = started, false = stopped
    using DebugCallback = std::function<void(const MidiDebugEvent&)>;

    MidiRouter();
    ~MidiRouter() override;

    bool openInput(const juce::String& deviceName);
    void closeInputs();
    juce::StringArray availableInputs() const;

    // Open every available MIDI input device that isn't already open (and log it).
    // Safe to call repeatedly — used at startup and periodically for hot-plug so a
    // keyboard plugged in after launch is picked up. Returns the number of inputs
    // now open.
    int refreshInputs();

    void setSplitPoint(int midiNote) noexcept;
    int  splitPoint() const noexcept;

    // Live Octave control: shifts the primary right-hand voice (Right 1) by N
    // octaves. Does not affect chord detection or style/accompaniment playback.
    void setLiveOctave(int octaves) noexcept { m_rightHand.setLayerOctave(0, octaves); }
    int  liveOctave() const noexcept { return m_rightHand.layerOctave(0); }

    // Global transpose: shifts every right-hand layer by N semitones, matching the
    // accompaniment transpose. Chord detection is unaffected.
    void setLiveTranspose(int semitones) noexcept { m_rightHand.setTranspose(semitones); }
    int  liveTranspose() const noexcept { return m_rightHand.transpose(); }

    // Dedicated Cadenza channel the primary right-hand voice (Right 1) plays on.
    int  melodyChannel() const noexcept { return m_rightHand.layerChannel(0); }
    void setMelodyChannel(int channel) noexcept { m_rightHand.setLayerChannel(0, channel); }

    // Right 1 / Right 2 / Right 3 layered voices (layer 0..2). Right 1 is on by
    // default; enabling a layer stacks its voice on top of the right hand.
    static constexpr int kNumRightLayers = RightHand::kNumLayers;
    void setRightLayerEnabled(int layer, bool on) noexcept { m_rightHand.setLayerEnabled(layer, on); }
    bool rightLayerEnabled(int layer) const noexcept { return m_rightHand.layerEnabled(layer); }
    void setRightLayerChannel(int layer, int ch) noexcept { m_rightHand.setLayerChannel(layer, ch); }
    int  rightLayerChannel(int layer) const noexcept { return m_rightHand.layerChannel(layer); }
    void setRightLayerOctave(int layer, int oct) noexcept { m_rightHand.setLayerOctave(layer, oct); }
    int  rightLayerOctave(int layer) const noexcept { return m_rightHand.layerOctave(layer); }

    // Play an on-screen / virtual-keyboard note through the live melody voice so
    // it gets the same Octave shift, dedicated channel and matched note-off as a
    // hardware key. Notes at/above the split point are treated as melody; this
    // does NOT feed chord detection. Returns the synth event to play, or nullopt
    // (e.g. a below-split note, which the caller may sound directly). Thread-safe.
    std::vector<LiveMelodyEvent> handleVirtualMelodyNote(int note, int velocity, bool isOn);

    // Inject a note from a native on-screen keyboard, running the SAME path as a
    // hardware key: split routing, chord detection (below split), and the live
    // melody voice (above split). Fires the note + chord callbacks. Thread-safe.
    void injectNote(int note, int velocity, bool isOn);

    // Pass-through to the underlying arranger router.
    void setChordDetectionMode(arranger::ChordDetectionMode mode) noexcept;
    void setChordMemory(bool enabled) noexcept;
    void setSyncroStopOnRelease(bool enabled) noexcept;

    // Returns the display name of the currently-detected chord, or "" if none.
    // Thread-safe; acquires the internal publish mutex.
    std::string currentChordDisplayName() const;

    void setNoteCallback(NoteCallback cb)   { m_onNote  = std::move(cb); }
    void setChordCallback(ChordCallback cb) { m_onChord = std::move(cb); }
    void setSyncCallback(SyncCallback cb)   { m_onSync  = std::move(cb); }
    void setDebugCallback(DebugCallback cb) { m_onDebug = std::move(cb); }

    // juce::MidiInputCallback
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& msg) override;

private:
    // Translate arranger::ChordRecognitionResult into the cadenza::midi::Chord
    // struct already consumed by StyleEngine.
    static std::optional<Chord> toCadenzaChord(const std::optional<arranger::ChordRecognitionResult>& result);
    static ChordQuality toCadenzaQuality(const std::string& s);

    juce::OwnedArray<juce::MidiInput> m_inputs;
    juce::StringArray m_openIdentifiers;       // identifiers of devices already opened
    juce::String m_lastDeviceSignature;        // to log the device list only when it changes

    arranger::ArrangerMidiRouter m_router;
    mutable std::mutex m_publishMutex;

    std::string  m_lastChordName;
    RightHand    m_rightHand;
    NoteCallback  m_onNote;
    ChordCallback m_onChord;
    SyncCallback  m_onSync;
    DebugCallback m_onDebug;
};
}
