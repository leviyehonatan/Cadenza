// LiveMelodyVoice — pure logic for the live right-hand (melody-zone) voice.
//
// Responsibilities (no JUCE, lives in cadenza_core so it is unit-tested):
//   * Apply the live Octave shift to melody-zone notes only.
//   * Remember the exact pitch played for each source note so the matching
//     note-off always releases the same pitch — even if Octave changed while
//     the note was held (otherwise notes get stuck).
//   * Sound notes on a dedicated melody MIDI channel, independent of style parts.
//   * Chord-zone notes produce NO melody sound (they only drive chord detection).
//
// Style/accompaniment playback is unaffected by this class.

#pragma once

#include "../Audio/MidiChannel.h"

#include <array>
#include <atomic>
#include <optional>
#include <string>

namespace cadenza::midi
{
// A single MIDI note event the live melody voice wants the synth to play.
struct LiveMelodyEvent
{
    int channel = cadenza::audio::kLiveMelodyChannel; // Cadenza 1-based channel
    int note = 0;        // octave-shifted MIDI note 0..127
    int velocity = 0;    // 0 for note-off
    bool isOn = false;
};

class LiveMelodyVoice
{
public:
    explicit LiveMelodyVoice(int melodyChannel = cadenza::audio::kLiveMelodyChannel) noexcept;

    void setOctave(int octaves) noexcept { m_octave.store(octaves); }
    int  octave() const noexcept { return m_octave.load(); }
    int  channel() const noexcept { return m_channel; }

    // Reassign the melody output channel (e.g. to avoid a loaded style's channels).
    // Clears held-note memory so nothing is stranded on the old channel.
    void setChannel(int channel) noexcept { m_channel = channel; reset(); }

    // Handle one incoming note. `isMelodyZone` is the router's verdict for a
    // note-ON (above the split point). Returns the event the synth should play,
    // or nullopt when the note should make no live-melody sound.
    //
    //  * melody note-on  -> shifted note-on on the melody channel (remembered)
    //  * chord/ignored note-on -> nullopt (silent; drives chord detection only)
    //  * note-off -> shifted note-off matching the remembered on, regardless of
    //    the current Octave or the note's current zone; nullopt if it was never
    //    sounded as a melody note.
    std::optional<LiveMelodyEvent> handleNote(int note, int velocity, bool isOn, bool isMelodyZone) noexcept;

    // Forget all held notes (call on device close / panic).
    void reset() noexcept;

private:
    static int clampMidi(int n) noexcept;

    int m_channel;
    std::atomic<int> m_octave { 0 };
    // For each source note 0..127, the pitch actually sounded (-1 = not held).
    std::array<int, 128> m_playedNote;
};

// Map a UI "Bank Memory" voice name to a General MIDI program number.
// Falls back to 0 (Acoustic Grand Piano) for unknown names.
int gmProgramForBankName(const std::string& name) noexcept;
}
