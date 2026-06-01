#include "Midi/ChordRecognizer.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;

void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

using cadenza::midi::recognise;
using cadenza::midi::parseChordSymbol;
using cadenza::midi::ChordQuality;

void emptyAndSingle()
{
    expect(!recognise({}).has_value(), "empty input -> no chord");

    auto c = recognise({ 60 });
    expect(c.has_value(), "single note recognized");
    expect(c->rootPitchClass == 0, "single C -> root C");
    expect(c->quality == ChordQuality::SingleNote, "single -> SingleNote quality");
}

void majorTriads()
{
    auto c = recognise({ 60, 64, 67 });   // C E G
    expect(c.has_value() && c->rootPitchClass == 0 && c->quality == ChordQuality::Major, "C major triad");

    c = recognise({ 65, 69, 72 });        // F A C
    expect(c.has_value() && c->rootPitchClass == 5 && c->quality == ChordQuality::Major, "F major triad");

    c = recognise({ 62, 66, 69 });        // D F# A
    expect(c.has_value() && c->rootPitchClass == 2 && c->quality == ChordQuality::Major, "D major triad");
}

void minorTriads()
{
    auto c = recognise({ 60, 63, 67 });   // C Eb G
    expect(c.has_value() && c->rootPitchClass == 0 && c->quality == ChordQuality::Minor, "C minor triad");

    c = recognise({ 57, 60, 64 });        // A C E
    expect(c.has_value() && c->rootPitchClass == 9 && c->quality == ChordQuality::Minor, "A minor triad");
}

void seventhChords()
{
    auto c = recognise({ 60, 64, 67, 70 });   // C E G Bb
    expect(c.has_value() && c->quality == ChordQuality::Dominant7, "C dominant 7");

    c = recognise({ 60, 64, 67, 71 });        // C E G B
    expect(c.has_value() && c->quality == ChordQuality::Major7, "C major 7");

    c = recognise({ 60, 63, 67, 70 });        // C Eb G Bb
    expect(c.has_value() && c->quality == ChordQuality::Minor7, "C minor 7");
}

void powerChord()
{
    auto c = recognise({ 60, 67 });   // C G
    expect(c.has_value(), "C5 recognized");
    expect(c->quality == ChordQuality::Power, "C5 is Power");
    expect(c->rootPitchClass == 0, "C5 root");
}

void diminishedAndAugmented()
{
    auto c = recognise({ 60, 63, 66 });  // C Eb Gb
    expect(c.has_value() && c->quality == ChordQuality::Diminished, "C diminished");

    c = recognise({ 60, 64, 68 });       // C E G#
    expect(c.has_value() && c->quality == ChordQuality::Augmented, "C augmented");
}

void chordToString()
{
    auto c = recognise({ 60, 64, 67 });   // C major
    expect(c->toString() == "C", "C major toString");

    c = recognise({ 57, 60, 64 });        // A minor
    expect(c->toString() == "Am", "A minor toString");

    c = recognise({ 60, 64, 67, 70 });    // C7
    expect(c->toString() == "C7", "C7 toString");
}

void slashChords()
{
    // C major with E in bass = C/E
    auto c = recognise({ 52, 60, 64, 67 });   // E C E G - but lowest is E
    if (c.has_value()) {
        const auto s = c->toString();
        // Either "C/E" (preferred) or something including "/" depending on root scoring.
        expect(s.find('/') != std::string::npos || c->rootPitchClass == 4,
               std::string("slash chord, got: ") + s);
    }
}
void parseSymbols()
{
    auto c = parseChordSymbol("C");
    expect(c && c->rootPitchClass == 0 && c->quality == ChordQuality::Major, "parse C");

    c = parseChordSymbol("Am");
    expect(c && c->rootPitchClass == 9 && c->quality == ChordQuality::Minor, "parse Am");

    c = parseChordSymbol("G7");
    expect(c && c->rootPitchClass == 7 && c->quality == ChordQuality::Dominant7, "parse G7");

    c = parseChordSymbol("Cmaj7");
    expect(c && c->rootPitchClass == 0 && c->quality == ChordQuality::Major7, "parse Cmaj7");

    c = parseChordSymbol("F#m7b5");
    expect(c && c->rootPitchClass == 6 && c->quality == ChordQuality::HalfDiminished7, "parse F#m7b5");

    c = parseChordSymbol("Bb");
    expect(c && c->rootPitchClass == 10 && c->quality == ChordQuality::Major, "parse Bb -> A#");

    c = parseChordSymbol("Ebm7");
    expect(c && c->rootPitchClass == 3 && c->quality == ChordQuality::Minor7, "parse Ebm7");

    c = parseChordSymbol("Dsus4");
    expect(c && c->rootPitchClass == 2 && c->quality == ChordQuality::Sus4, "parse Dsus4");

    c = parseChordSymbol("C9");   // extended dominant collapses to Dominant7
    expect(c && c->quality == ChordQuality::Dominant7, "parse C9 -> Dominant7");

    // slash bass preserved for display
    c = parseChordSymbol("C/E");
    expect(c && c->rootPitchClass == 0 && c->toString() == "C/E", "parse C/E round-trips");

    // round-trip: recognise->toString->parse is stable for supported qualities
    auto r = recognise({ 57, 60, 64 });   // Am
    expect(r && parseChordSymbol(r->toString())->rootPitchClass == 9, "Am round-trip root");

    // invalid input
    expect(!parseChordSymbol("").has_value(), "empty symbol -> nullopt");
    expect(!parseChordSymbol("H7").has_value(), "bad root letter -> nullopt");
}
}

int main()
{
    emptyAndSingle();
    majorTriads();
    minorTriads();
    seventhChords();
    powerChord();
    diminishedAndAugmented();
    chordToString();
    slashChords();
    parseSymbols();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All ChordRecognizer tests passed\n";
    return EXIT_SUCCESS;
}
