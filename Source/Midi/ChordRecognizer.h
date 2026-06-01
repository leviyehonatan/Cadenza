// ChordRecognizer
// Given a set of MIDI notes currently held in the chord zone, identify the
// chord (root pitch class + quality). Pure C++, no JUCE.
//
// Algorithm: for each pitch class P (0..11), build the interval set
// relative to P and score it against a small dictionary of chord templates.
// Highest score wins; ties broken by template-list priority.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace cadenza::midi
{
enum class ChordQuality
{
    Major,
    Minor,
    Dominant7,
    Major7,
    Minor7,
    MinorMajor7,
    Diminished,
    HalfDiminished7,
    Diminished7,
    Augmented,
    Sus2,
    Sus4,
    Power,        // root + 5th
    SingleNote,   // only 1 note held — root pitch class only
};

struct Chord
{
    int rootPitchClass = 0;  // 0=C, 1=C#, ..., 11=B
    ChordQuality quality = ChordQuality::Major;
    int bassMidi = -1;       // lowest note actually played (used for slash-chord display)

    std::string toString() const;     // e.g. "C", "Cm", "Cmaj7", "C/E"
    std::string rootName() const;     // "C", "C#", "D", ...
    std::string qualitySuffix() const;
};

// Recognise the chord from a set of MIDI notes.
// Returns nullopt if no recognisable chord (e.g. 0 notes).
std::optional<Chord> recognise(const std::vector<int>& notes);

// Parse a chord symbol string (e.g. "C", "Am", "G7", "Cmaj7", "F#m7b5", "Bb",
// "C/E") into a Chord. The inverse of Chord::toString. Extended chords (9/11/13)
// collapse to their base seventh/triad quality. Returns nullopt if the root
// can't be parsed; an empty/unknown suffix is treated as a major triad.
std::optional<Chord> parseChordSymbol(const std::string& symbol);

// Helper used internally; exposed for tests.
std::string pitchClassName(int pc);
}
