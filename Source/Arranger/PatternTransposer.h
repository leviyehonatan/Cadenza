// PatternTransposer — maps a PatternNote (chord-relative role) to an absolute MIDI note,
// given the live chord context.
//
// The "pitch" field of a PatternNote is interpreted as:
//   - Absolute  : returned unchanged (drums, FX)
//   - ChordRoot/Chord3/Chord5/Chord7 : pitch is used only for octave reference;
//     the actual pitch class comes from the chord. Octave is preserved from pitch.
//   - ScaleTone : scaleDegree (0..6) selects a scale tone in the current key

#pragma once

#include "Style.h"
#include "../Midi/ChordRecognizer.h"

#include <optional>

namespace cadenza::arranger
{
struct TransposeContext
{
    cadenza::midi::Chord chord;   // current chord
    int keyTonicPC = 0;           // 0..11; root of the song key for scale degrees
    int globalTranspose = 0;      // semitones added on top (user transpose)
    int globalOctave = 0;         // octaves added on top (user octave)
    // Optional scale modifier: 0 = major, 1 = minor (natural).
    // Could be extended to modes later.
    int scaleMode = 0;
};

// Returns the absolute MIDI note number to play, or nullopt if the role cannot
// be resolved (e.g. ScaleTone with degree out of 0..6).
std::optional<int> transposeNote(const PatternNote& note, const TransposeContext& ctx);
std::optional<int> transposeNote(const PatternNote& note,
                                 const TransposeContext& ctx,
                                 const YamahaChannelPolicy* policy);

// Returns intervals (semitones from root) that define each chord quality.
// Exposed for tests and StyleEngine helpers.
int chordIntervalForRole(cadenza::midi::ChordQuality quality, NoteRole role) noexcept;

// Returns the semitone offset from key tonic for the given diatonic degree (0..6).
// For modes other than 0 (major) and 1 (minor), falls back to major.
int scaleSemitone(int scaleMode, int degree) noexcept;
}
