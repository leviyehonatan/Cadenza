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
// The full Yamaha arranger chord vocabulary (the 34 chord types a PSR/Genos
// recognises and voices). The first block predates the full set; new types are
// appended so persisted/test values stay stable.
enum class ChordQuality
{
    Major,
    Minor,
    Dominant7,
    Major7,
    Minor7,
    MinorMajor7,
    Diminished,
    HalfDiminished7,   // m7b5
    Diminished7,
    Augmented,
    Sus2,              // Yamaha "1+2+5"
    Sus4,
    Power,             // root + 5th ("1+5")
    SingleNote,        // only 1 note held — root pitch class only ("1+8")

    // --- extended Yamaha types ---
    Major6,            // 6
    Major69,           // 6(9)
    MajorAdd9,         // add9 / Maj(9)
    Major9,            // maj7(9)
    Major7s11,         // maj7#11
    AugmentedMajor7,   // augMaj7
    Augmented7,        // 7aug / 7#5
    Minor6,            // m6
    Minor9,            // m7(9)
    Minor11,           // m7(11)
    MinorAdd9,         // m(9)
    MinorMajor9,       // mMaj7(9)
    Dominant9,         // 7(9)
    Dominant13,        // 7(13)
    Dominant7sus4,     // 7sus4
    Dominant7b5,       // 7b5
    Dominant7s11,      // 7(#11)
    Dominant7b9,       // 7(b9)
    Dominant7s9,       // 7(#9)
    Dominant7b13,      // 7(b13)
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
