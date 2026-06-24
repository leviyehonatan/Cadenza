#include "Arranger/MidiStyleConverter.h"
#include "Arranger/StyleLoader.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
using cadenza::arranger::NoteRole;
using cadenza::arranger::Part;
using cadenza::arranger::Style;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void addNote(juce::MidiMessageSequence& track, int channel, int tick, int duration,
             int pitch, int velocity = 100)
{
    auto on = juce::MidiMessage::noteOn(channel, pitch, static_cast<juce::uint8>(velocity));
    on.setTimeStamp(tick);
    track.addEvent(on);
    auto off = juce::MidiMessage::noteOff(channel, pitch);
    off.setTimeStamp(tick + duration);
    track.addEvent(off);
}

void addProgram(juce::MidiMessageSequence& track, int channel, int program)
{
    auto msg = juce::MidiMessage::programChange(channel, program + 1);
    msg.setTimeStamp(0);
    track.addEvent(msg);
}

juce::File writeMidi(const juce::String& name, juce::MidiFile& midi)
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile(name, ".mid");
    juce::FileOutputStream out(file);
    expect(out.openedOk(), "temporary MIDI file opens for writing");
    midi.writeTo(out);
    out.flush();
    return file;
}

const Part* findPart(const Style& style, const std::string& name)
{
    if (style.sections.empty())
        return nullptr;
    for (const auto& part : style.sections.front().parts)
        if (part.name == name)
            return &part;
    return nullptr;
}

const Part* findPart(const cadenza::arranger::Section& section, const std::string& name)
{
    for (const auto& part : section.parts)
        if (part.name == name)
            return &part;
    return nullptr;
}

void makeCmajorFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;

    juce::MidiMessageSequence meta;
    auto tempo = juce::MidiMessage::tempoMetaEvent(500000);
    tempo.setTimeStamp(0);
    meta.addEvent(tempo);
    auto ts = juce::MidiMessage::timeSignatureMetaEvent(4, 4);
    ts.setTimeStamp(0);
    meta.addEvent(ts);

    juce::MidiMessageSequence drums;
    for (int b = 0; b < 4; ++b) {
        addNote(drums, 10, b * bar, 120, 36, 110);
        addNote(drums, 10, b * bar + ppq, 120, 38, 96);
    }

    juce::MidiMessageSequence bass;
    addProgram(bass, 2, 33);
    for (int b = 0; b < 4; ++b)
        addNote(bass, 2, b * bar, ppq * 4, 36, 104);

    juce::MidiMessageSequence piano;
    addProgram(piano, 3, 0);
    for (int b = 0; b < 4; ++b) {
        addNote(piano, 3, b * bar, ppq * 4, 60, 88);
        addNote(piano, 3, b * bar, ppq * 4, 64, 82);
        addNote(piano, 3, b * bar, ppq * 4, 67, 80);
    }

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(meta);
    midi.addTrack(drums);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void makeAminorFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;
    juce::MidiMessageSequence bass;
    addProgram(bass, 2, 34);
    addNote(bass, 2, 0, bar * 4, 45, 100);

    juce::MidiMessageSequence piano;
    addProgram(piano, 3, 0);
    addNote(piano, 3, 0, bar * 4, 57, 88);
    addNote(piano, 3, 0, bar * 4, 60, 82);
    addNote(piano, 3, 0, bar * 4, 64, 80);

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void makeFsharpMinorFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;

    juce::MidiMessageSequence drums;
    addNote(drums, 10, 0, 120, 36, 110);
    addNote(drums, 10, ppq, 120, 38, 96);

    juce::MidiMessageSequence bass;
    addProgram(bass, 2, 34);
    addNote(bass, 2, 0, bar * 4, 42, 100);

    juce::MidiMessageSequence piano;
    addProgram(piano, 3, 0);
    addNote(piano, 3, 0, bar * 4, 66, 88);
    addNote(piano, 3, 0, bar * 4, 69, 82);
    addNote(piano, 3, 0, bar * 4, 73, 80);

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(drums);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void makeTwoSectionFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;

    juce::MidiMessageSequence meta;
    auto tempo = juce::MidiMessage::tempoMetaEvent(500000);
    tempo.setTimeStamp(0);
    meta.addEvent(tempo);
    auto ts = juce::MidiMessage::timeSignatureMetaEvent(4, 4);
    ts.setTimeStamp(0);
    meta.addEvent(ts);

    juce::MidiMessageSequence drums;
    for (int b = 0; b < 8; ++b) {
        addNote(drums, 10, b * bar, 120, 36, 110);
        addNote(drums, 10, b * bar + ppq, 120, 38, 96);
    }

    juce::MidiMessageSequence bass;
    addProgram(bass, 2, 33);
    for (int b = 0; b < 4; ++b)
        addNote(bass, 2, b * bar, ppq * 4, 36, 104);
    for (int b = 4; b < 8; ++b)
        addNote(bass, 2, b * bar, ppq * 4, 47, 104);

    juce::MidiMessageSequence piano;
    addProgram(piano, 3, 0);
    for (int b = 0; b < 4; ++b) {
        addNote(piano, 3, b * bar, ppq * 4, 60, 88);
        addNote(piano, 3, b * bar, ppq * 4, 64, 82);
        addNote(piano, 3, b * bar, ppq * 4, 67, 80);
    }
    for (int b = 4; b < 8; ++b) {
        addNote(piano, 3, b * bar, ppq * 4, 59, 88);
        addNote(piano, 3, b * bar, ppq * 4, 63, 82);
        addNote(piano, 3, b * bar, ppq * 4, 66, 80);
    }

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(meta);
    midi.addTrack(drums);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void makeSparseIntroThenFullBandFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;

    juce::MidiMessageSequence drums;
    for (int b = 0; b < 8; ++b) {
        addNote(drums, 10, b * bar, 120, 36, 110);
        addNote(drums, 10, b * bar + ppq, 120, 38, 96);
    }

    juce::MidiMessageSequence bass;
    addProgram(bass, 2, 33);
    for (int b = 0; b < 8; ++b)
        addNote(bass, 2, b * bar, ppq * 4, 36, 104);

    juce::MidiMessageSequence piano;
    addProgram(piano, 3, 0);
    for (int b = 4; b < 8; ++b) {
        addNote(piano, 3, b * bar, ppq * 4, 60, 88);
        addNote(piano, 3, b * bar, ppq * 4, 64, 82);
        addNote(piano, 3, b * bar, ppq * 4, 67, 80);
    }

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(drums);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void makeBassDrumsOnlyFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;

    juce::MidiMessageSequence drums;
    for (int b = 0; b < 4; ++b)
        addNote(drums, 10, b * bar, 120, 36, 110);

    juce::MidiMessageSequence bass;
    addProgram(bass, 2, 33);
    for (int b = 0; b < 4; ++b)
        addNote(bass, 2, b * bar, ppq * 4, 36, 104);

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(drums);
    midi.addTrack(bass);
}

void makeGPowerShapeFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;

    juce::MidiMessageSequence bass;
    addProgram(bass, 2, 33);
    addNote(bass, 2, 0, bar * 4, 43, 104);

    juce::MidiMessageSequence piano;
    addProgram(piano, 3, 0);
    addNote(piano, 3, 0, bar * 4, 55, 88);
    addNote(piano, 3, 0, bar * 4, 62, 82);

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void addFullBandBars(juce::MidiMessageSequence& drums,
                     juce::MidiMessageSequence& bass,
                     juce::MidiMessageSequence& piano,
                     int firstBar,
                     int barCount,
                     int root,
                     int third,
                     int fifth)
{
    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;
    for (int b = firstBar; b < firstBar + barCount; ++b) {
        addNote(drums, 10, b * bar, 120, 36, 110);
        addNote(drums, 10, b * bar + ppq, 120, 38, 96);
        addNote(bass, 2, b * bar, ppq * 4, root - 24, 104);
        addNote(piano, 3, b * bar, ppq * 4, root, 88);
        addNote(piano, 3, b * bar, ppq * 4, third, 82);
        addNote(piano, 3, b * bar, ppq * 4, fifth, 80);
    }
}

void makeAutoSparseIntroRepeatedMainFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;

    juce::MidiMessageSequence drums;
    juce::MidiMessageSequence bass;
    juce::MidiMessageSequence piano;
    addProgram(bass, 2, 33);
    addProgram(piano, 3, 0);

    for (int b = 0; b < 4; ++b) {
        addNote(drums, 10, b * bar, 120, 36, 110);
        addNote(bass, 2, b * bar, ppq * 4, 36, 104);
    }
    addFullBandBars(drums, bass, piano, 4, 8, 60, 64, 67);

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(drums);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void makeAutoTwoDistinctRepeatedMainsFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;

    juce::MidiMessageSequence drums;
    juce::MidiMessageSequence bass;
    juce::MidiMessageSequence piano;
    addProgram(bass, 2, 33);
    addProgram(piano, 3, 0);

    addFullBandBars(drums, bass, piano, 0, 8, 60, 64, 67);
    addFullBandBars(drums, bass, piano, 8, 8, 65, 69, 72);

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(drums);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void makeTwoBarChordChangeFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;

    juce::MidiMessageSequence drums;
    juce::MidiMessageSequence bass;
    juce::MidiMessageSequence piano;
    addProgram(bass, 2, 33);
    addProgram(piano, 3, 0);

    addFullBandBars(drums, bass, piano, 0, 2, 60, 64, 67);
    addFullBandBars(drums, bass, piano, 2, 2, 65, 69, 72);

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(drums);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void makeAutoTwoBarChordChangesFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;

    juce::MidiMessageSequence drums;
    juce::MidiMessageSequence bass;
    juce::MidiMessageSequence piano;
    addProgram(bass, 2, 33);
    addProgram(piano, 3, 0);

    addFullBandBars(drums, bass, piano, 0, 2, 60, 64, 67);
    addFullBandBars(drums, bass, piano, 2, 2, 65, 69, 72);
    addFullBandBars(drums, bass, piano, 4, 2, 60, 64, 67);
    addFullBandBars(drums, bass, piano, 6, 2, 65, 69, 72);

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(drums);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void makeAutoNoIntroFixture(juce::MidiFile& midi)
{
    constexpr int ppq = 480;

    juce::MidiMessageSequence drums;
    juce::MidiMessageSequence bass;
    juce::MidiMessageSequence piano;
    addProgram(bass, 2, 33);
    addProgram(piano, 3, 0);
    addFullBandBars(drums, bass, piano, 0, 8, 60, 64, 67);

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(drums);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void addHeavyFillBar(juce::MidiMessageSequence& drums, int barIndex)
{
    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;
    const int start = barIndex * bar;
    addNote(drums, 10, start, 80, 36, 116);
    addNote(drums, 10, start + ppq / 2, 80, 41, 104);
    addNote(drums, 10, start + ppq, 80, 45, 108);
    addNote(drums, 10, start + ppq + ppq / 2, 80, 47, 108);
    addNote(drums, 10, start + ppq * 2, 80, 48, 112);
    addNote(drums, 10, start + ppq * 2 + ppq / 2, 80, 50, 112);
    addNote(drums, 10, start + ppq * 3, 120, 49, 118);
    addNote(drums, 10, start + ppq * 3 + ppq / 2, 80, 57, 116);
}

void makeAutoFillBeforeMainFixture(juce::MidiFile& midi, bool heavyFill)
{
    constexpr int ppq = 480;

    juce::MidiMessageSequence drums;
    juce::MidiMessageSequence bass;
    juce::MidiMessageSequence piano;
    addProgram(bass, 2, 33);
    addProgram(piano, 3, 0);

    addFullBandBars(drums, bass, piano, 0, 3, 65, 69, 72);
    if (heavyFill)
        addHeavyFillBar(drums, 3);
    else
        addFullBandBars(drums, bass, piano, 3, 1, 65, 69, 72);
    addFullBandBars(drums, bass, piano, 4, 8, 60, 64, 67);

    midi.setTicksPerQuarterNote(ppq);
    midi.addTrack(drums);
    midi.addTrack(bass);
    midi.addTrack(piano);
}

void testCmajorMapsPartsAndRoundTrips()
{
    juce::MidiFile midi;
    makeCmajorFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-cmaj", midi);

    auto result = cadenza::arranger::convertMidiFileToNativeStyle(file, {});
    expect(result.ok && result.style != nullptr, "C major fixture converts");
    expect(result.style->defaultTempo == 120, "tempo meta is imported");
    expect(result.style->beatsPerBar == 4 && result.style->beatUnit == 4,
           "time signature meta is imported");
    expect(result.style->ticksPerBeat == 480, "PPQ is preserved");
    expect(result.style->sections.size() == 1
               && result.style->sections.front().name == "mainA"
               && result.style->sections.front().barCount == 4,
           "one mainA section is emitted");

    const auto* drums = findPart(*result.style, "drums");
    const auto* bass = findPart(*result.style, "bass");
    const auto* chord1 = findPart(*result.style, "chord1");
    expect(drums != nullptr && drums->midiChannel == 10, "drums map to channel 10");
    expect(bass != nullptr && bass->midiChannel == 11, "bass maps to channel 11");
    expect(chord1 != nullptr && chord1->midiChannel == 12, "piano maps to chord1 channel 12");
    expect(!drums->notes.empty() && drums->notes.front().role == NoteRole::Absolute
               && drums->notes.front().pitch == 36,
           "drums stay absolute with original pitch");
    expect(bass->yamahaPolicy && bass->yamahaPolicy->sourceRoot.value_or("") == "C"
               && bass->yamahaPolicy->sourceChord.value_or("") == "",
           "C major source chord is stored on bass policy");
    expect(chord1->notes.size() >= 3
               && chord1->notes[0].role == NoteRole::ChordRoot
               && chord1->notes[1].role == NoteRole::Chord3
               && chord1->notes[2].role == NoteRole::Chord5,
           "chord tones get source-root-relative roles");

    const auto json = cadenza::arranger::saveStyleToJson(*result.style);
    const auto loaded = cadenza::arranger::loadStyleFromJson(json);
    expect(loaded.ok && loaded.style.findSection("mainA") != nullptr,
           "converted style round-trips through JSON");
}

void testAminorSourceChord()
{
    juce::MidiFile midi;
    makeAminorFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-amin", midi);
    cadenza::arranger::MidiStyleConvertOptions options;
    options.normalizeToC = false;
    auto result = cadenza::arranger::convertMidiFileToNativeStyle(file, options);
    expect(result.ok && result.style != nullptr, "A minor fixture converts");
    const auto* bass = findPart(*result.style, "bass");
    expect(bass != nullptr && bass->yamahaPolicy
               && bass->yamahaPolicy->sourceRoot.value_or("") == "A"
               && bass->yamahaPolicy->sourceChord.value_or("") == "m",
           "A minor source chord is detected");
    const auto* chord1 = findPart(*result.style, "chord1");
    expect(chord1 != nullptr && chord1->notes.size() >= 3
               && chord1->notes[0].role == NoteRole::ChordRoot
               && chord1->notes[1].role == NoteRole::Chord3
               && chord1->notes[2].role == NoteRole::Chord5,
           "minor chord tones are role-classified against A root");
}

void testNormalizeFsharpMinorToC()
{
    juce::MidiFile midi;
    makeFsharpMinorFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-fsharp-min-normalize", midi);

    cadenza::arranger::MidiStyleConvertOptions originalOptions;
    originalOptions.normalizeToC = false;
    auto original = cadenza::arranger::convertMidiFileToNativeStyle(file, originalOptions);
    expect(original.ok && original.style != nullptr, "F# minor fixture converts without normalization");
    const auto* originalBass = findPart(*original.style, "bass");
    const auto* originalChord1 = findPart(*original.style, "chord1");
    expect(originalBass != nullptr && originalBass->yamahaPolicy
               && originalBass->yamahaPolicy->sourceRoot.value_or("") == "F#"
               && originalBass->yamahaPolicy->sourceChord.value_or("") == "m",
           "normalizeToC=false leaves detected F# minor source chord");
    expect(originalChord1 != nullptr && originalChord1->notes.size() >= 3,
           "unnormalized F# minor chord part exists");

    cadenza::arranger::MidiStyleConvertOptions normalizedOptions;
    normalizedOptions.normalizeToC = true;
    auto normalized = cadenza::arranger::convertMidiFileToNativeStyle(file, normalizedOptions);
    expect(normalized.ok && normalized.style != nullptr, "F# minor fixture converts with normalization");
    const auto* drums = findPart(*normalized.style, "drums");
    const auto* bass = findPart(*normalized.style, "bass");
    const auto* chord1 = findPart(*normalized.style, "chord1");
    expect(drums != nullptr && !drums->notes.empty()
               && drums->notes[0].role == NoteRole::Absolute
               && drums->notes[0].pitch == 36,
           "normalized import leaves drum pitches absolute");
    expect(bass != nullptr && bass->yamahaPolicy
               && bass->yamahaPolicy->sourceRoot.value_or("") == "C"
               && bass->yamahaPolicy->sourceChord.value_or("") == "m",
           "normalized bass policy stores C minor");
    expect(chord1 != nullptr && chord1->yamahaPolicy
               && chord1->yamahaPolicy->sourceRoot.value_or("") == "C"
               && chord1->yamahaPolicy->sourceChord.value_or("") == "m",
           "normalized chord policy stores C minor");
    expect(chord1 != nullptr && chord1->notes.size() >= 3
               && chord1->notes[0].pitch == 60
               && chord1->notes[1].pitch == 63
               && chord1->notes[2].pitch == 67,
           "F# minor chord tones transpose down to C minor");
    expect(chord1->notes[0].role == originalChord1->notes[0].role
               && chord1->notes[1].role == originalChord1->notes[1].role
               && chord1->notes[2].role == originalChord1->notes[2].role,
           "normalization preserves chord note roles");
}

void testMultiSectionImportNormalizesEachSectionAndRoundTrips()
{
    juce::MidiFile midi;
    makeTwoSectionFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-two-section", midi);

    std::vector<cadenza::arranger::MidiStyleSectionSpec> sections;
    sections.push_back({ "mainB", 4, 4, "B", "maj" });
    sections.push_back({ "mainA", 0, 4, "C", "maj" });

    auto result = cadenza::arranger::convertMidiFileToNativeStyleMultiSection(file, sections, true);
    expect(result.ok && result.style != nullptr, "two-section fixture converts");
    expect(result.style->sections.size() == 2, "two sections are emitted");
    const auto* mainA = result.style->findSection("mainA");
    const auto* mainB = result.style->findSection("mainB");
    expect(mainA != nullptr && mainB != nullptr, "mainA and mainB sections are present");
    expect(result.style->sections[0].name == "mainA" && result.style->sections[1].name == "mainB",
           "sections are ordered by arranger slot");
    expect(mainA->barCount == 4 && mainB->barCount == 4, "each section keeps its own bar count");

    const auto* mainADrums = findPart(*mainA, "drums");
    const auto* mainABass = findPart(*mainA, "bass");
    const auto* mainAChord = findPart(*mainA, "chord1");
    const auto* mainBDrums = findPart(*mainB, "drums");
    const auto* mainBBass = findPart(*mainB, "bass");
    const auto* mainBChord = findPart(*mainB, "chord1");
    expect(mainADrums != nullptr && mainBDrums != nullptr
               && mainADrums->midiChannel == mainBDrums->midiChannel,
           "drum parts align to the native channel layout");
    expect(mainABass != nullptr && mainBBass != nullptr
               && mainABass->midiChannel == 11 && mainBBass->midiChannel == 11,
           "bass parts align to channel 11 in both sections");
    expect(mainAChord != nullptr && mainBChord != nullptr
               && mainAChord->midiChannel == 12 && mainBChord->midiChannel == 12,
           "chord parts align to channel 12 in both sections");
    expect(mainAChord->notes.size() >= 3
               && mainAChord->notes[0].role == NoteRole::ChordRoot
               && mainAChord->notes[1].role == NoteRole::Chord3
               && mainAChord->notes[2].role == NoteRole::Chord5,
           "mainA chord roles are assigned");
    expect(mainBChord->notes.size() >= 3
               && mainBChord->notes[0].role == NoteRole::ChordRoot
               && mainBChord->notes[1].role == NoteRole::Chord3
               && mainBChord->notes[2].role == NoteRole::Chord5,
           "mainB chord roles are assigned");
    expect(mainABass->yamahaPolicy && mainABass->yamahaPolicy->sourceRoot.value_or("") == "C",
           "mainA normalized policy references C");
    expect(mainBBass->yamahaPolicy && mainBBass->yamahaPolicy->sourceRoot.value_or("") == "C",
           "mainB normalized policy references C");
    expect(mainADrums->yamahaPolicy && mainADrums->yamahaPolicy->sourceRoot.value_or("") == "C"
               && mainBDrums->yamahaPolicy && mainBDrums->yamahaPolicy->sourceRoot.value_or("") == "C",
           "normalized drum policies also reference C while pitches stay absolute");
    expect(mainBChord->notes[0].pitch == 60
               && mainBChord->notes[1].pitch == 64
               && mainBChord->notes[2].pitch == 67,
           "mainB B major tones transpose up to C major");

    const auto json = cadenza::arranger::saveStyleToJson(*result.style);
    const auto loaded = cadenza::arranger::loadStyleFromJson(json);
    expect(loaded.ok && loaded.style.findSection("mainA") != nullptr
               && loaded.style.findSection("mainB") != nullptr,
           "multi-section style round-trips through JSON");
}

void testOverrideSourceChord()
{
    juce::MidiFile midi;
    makeAminorFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-override", midi);

    cadenza::arranger::MidiStyleConvertOptions options;
    options.overrideSourceRoot = 0;
    options.overrideSourceChord = "";

    auto result = cadenza::arranger::convertMidiFileToNativeStyle(file, options);
    expect(result.ok && result.style != nullptr, "override fixture converts");
    const auto* bass = findPart(*result.style, "bass");
    expect(bass != nullptr && bass->yamahaPolicy
               && bass->yamahaPolicy->sourceRoot.value_or("") == "C"
               && bass->yamahaPolicy->sourceChord.value_or("x") == "",
           "source chord override is stored on policy");
    const auto* chord1 = findPart(*result.style, "chord1");
    expect(chord1 != nullptr && !chord1->notes.empty()
               && chord1->notes.front().role == NoteRole::Chord7,
           "override source chord is used for role assignment");
}

void testInspectReadsEarliestTempo()
{
    constexpr int ppq = 480;
    constexpr int bar = ppq * 4;
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(ppq);

    juce::MidiMessageSequence laterMeta;
    auto laterTempo = juce::MidiMessage::tempoMetaEvent(500000);
    laterTempo.setTimeStamp(bar);
    laterMeta.addEvent(laterTempo);
    midi.addTrack(laterMeta);

    juce::MidiMessageSequence notes;
    auto firstTempo = juce::MidiMessage::tempoMetaEvent(512821);
    firstTempo.setTimeStamp(0);
    notes.addEvent(firstTempo);
    auto ts = juce::MidiMessage::timeSignatureMetaEvent(4, 4);
    ts.setTimeStamp(0);
    notes.addEvent(ts);
    addProgram(notes, 3, 0);
    for (int b = 0; b < 6; ++b) {
        addNote(notes, 3, b * bar, ppq * 4, 66, 88);
        addNote(notes, 3, b * bar, ppq * 4, 69, 82);
        addNote(notes, 3, b * bar, ppq * 4, 73, 80);
    }
    midi.addTrack(notes);

    const auto file = writeMidi("cadenza-midi-style-tempo-inspect", midi);
    auto info = cadenza::arranger::inspectMidiFileForStyleImport(file, 0, 4);
    expect(info.ok, "inspect succeeds");
    expect(info.tempo == 117, "earliest MIDI tempo meta is imported");
    expect(info.beatsPerBar == 4 && info.beatUnit == 4, "inspect reports time signature");
    expect(info.totalBars == 6, "inspect reports total available bars");

    auto converted = cadenza::arranger::convertMidiFileToNativeStyle(file, {});
    expect(converted.ok && converted.style != nullptr
               && converted.style->defaultTempo == 117,
           "conversion uses earliest MIDI tempo meta");
}

void testSmpteRejects()
{
    juce::MidiFile midi;
    midi.setSmpteTimeFormat(25, 40);
    juce::MidiMessageSequence track;
    addNote(track, 1, 0, 40, 60);
    midi.addTrack(track);
    const auto file = writeMidi("cadenza-midi-style-smpte", midi);
    auto result = cadenza::arranger::convertMidiFileToNativeStyle(file, {});
    expect(!result.ok && result.style == nullptr
               && result.warnings.joinIntoString(" ").containsIgnoreCase("SMPTE"),
           "SMPTE MIDI is rejected with warning");
}

void testFallbackWarning()
{
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence track;
    addProgram(track, 3, 0);
    addNote(track, 3, 0, 480, 60);
    addNote(track, 3, 0, 480, 61);
    addNote(track, 3, 0, 480, 66);
    midi.addTrack(track);
    const auto file = writeMidi("cadenza-midi-style-fallback", midi);
    auto result = cadenza::arranger::convertMidiFileToNativeStyle(file, {});
    expect(result.ok && result.style != nullptr, "fallback fixture still converts");
    expect(result.warnings.joinIntoString(" ").containsIgnoreCase("C major"),
           "unmatched source chord falls back with warning");
    const auto* chord1 = findPart(*result.style, "chord1");
    expect(chord1 != nullptr && chord1->yamahaPolicy
               && chord1->yamahaPolicy->sourceRoot.value_or("") == "C"
               && chord1->yamahaPolicy->sourceChord.value_or("") == "",
           "fallback policy stores C major");
}

void testLowConfidenceSourceChordFallsBackUnlessOverridden()
{
    juce::MidiFile midi;
    makeGPowerShapeFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-low-confidence-power", midi);

    auto info = cadenza::arranger::inspectMidiFileForStyleImport(file, 0, 4);
    expect(info.ok, "power-shape fixture inspects");
    expect(info.detectedChord.confidence == cadenza::arranger::MidiStyleChordConfidence::Low,
           "no-third power shape reports low source-chord confidence");

    cadenza::arranger::MidiStyleConvertOptions options;
    options.normalizeToC = false;
    auto result = cadenza::arranger::convertMidiFileToNativeStyle(file, options);
    expect(result.ok && result.style != nullptr, "low-confidence power-shape fixture converts");
    expect(result.warnings.joinIntoString(" ").containsIgnoreCase("C major fallback"),
           "low-confidence source chord warns and falls back to C major");
    const auto* bass = findPart(*result.style, "bass");
    expect(bass != nullptr && bass->yamahaPolicy
               && bass->yamahaPolicy->sourceRoot.value_or("") == "C"
               && bass->yamahaPolicy->sourceChord.value_or("") == "",
           "low-confidence source chord is not silently stored as G power");

    options.overrideSourceRoot = 7;
    auto overridden = cadenza::arranger::convertMidiFileToNativeStyle(file, options);
    expect(overridden.ok && overridden.style != nullptr, "low-confidence fixture converts with root override");
    const auto* overriddenBass = findPart(*overridden.style, "bass");
    expect(overriddenBass != nullptr && overriddenBass->yamahaPolicy
               && overriddenBass->yamahaPolicy->sourceRoot.value_or("") == "G",
           "valid override wins even when detected confidence is low");
}

void testInspectAutoRangeSkipsSparseIntro()
{
    juce::MidiFile midi;
    makeSparseIntroThenFullBandFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-auto-range", midi);

    auto info = cadenza::arranger::inspectMidiFileForStyleImport(file, 0, 4);
    expect(info.ok, "auto-range fixture inspects");
    expect(info.recommendedRange.barStart == 4,
           "auto-range starts in the full-band region instead of bar 1");
    expect(info.recommendedRange.barCount == 4, "auto-range keeps requested four-bar length");
}

void testInspectReportsChordChangingRange()
{
    juce::MidiFile midi;
    makeTwoBarChordChangeFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-chord-changing-range", midi);

    auto info = cadenza::arranger::inspectMidiFileForStyleImport(file, 0, 4);
    expect(info.ok, "chord-changing range inspects");
    expect(info.rangeChangesChord, "four-bar range with two clear chords reports a chord change");
    expect(info.distinctChordCount >= 2, "chord-changing range reports multiple distinct chords");
}

void testInspectReportsSingleChordRange()
{
    juce::MidiFile midi;
    makeCmajorFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-single-chord-range", midi);

    auto info = cadenza::arranger::inspectMidiFileForStyleImport(file, 0, 4);
    expect(info.ok, "single-chord range inspects");
    expect(!info.rangeChangesChord, "four-bar range on one clear chord does not report a change");
    expect(info.distinctChordCount == 1, "single-chord range reports one distinct chord");
}

void testAutoSplitFindsMainAfterSparseIntro()
{
    juce::MidiFile midi;
    makeAutoSparseIntroRepeatedMainFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-auto-split-intro-main", midi);

    auto result = cadenza::arranger::autoSplitMidiFileForStyleImport(file, {});
    expect(result.ok, "auto-split succeeds for sparse intro plus repeated main");
    const auto mainA = std::find_if(result.sections.begin(), result.sections.end(), [](const auto& spec) {
        return spec.sectionId == "mainA";
    });
    expect(mainA != result.sections.end(), "auto-split emits mainA");
    expect(mainA->barStart >= 4, "auto-split mainA starts in the full-band region instead of bar 1");
    const auto intro = std::find_if(result.sections.begin(), result.sections.end(), [](const auto& spec) {
        return spec.sectionId == "intro";
    });
    expect(intro != result.sections.end() && intro->barStart == 0,
           "auto-split keeps the obvious sparse intro");
    expect(result.warnings.joinIntoString(" ").containsIgnoreCase("Main A: found at bars"),
           "auto-split reports the original mainA timeline location");
}

void testAutoSplitFindsDistinctRepeatedMainB()
{
    juce::MidiFile midi;
    makeAutoTwoDistinctRepeatedMainsFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-auto-split-mainb", midi);

    auto result = cadenza::arranger::autoSplitMidiFileForStyleImport(file, {});
    expect(result.ok, "auto-split succeeds for two repeated full-band regions");
    const auto mainA = std::find_if(result.sections.begin(), result.sections.end(), [](const auto& spec) {
        return spec.sectionId == "mainA";
    });
    const auto mainB = std::find_if(result.sections.begin(), result.sections.end(), [](const auto& spec) {
        return spec.sectionId == "mainB";
    });
    expect(mainA != result.sections.end() && mainB != result.sections.end(),
           "auto-split emits mainA and mainB");
    expect(mainA->barStart != mainB->barStart, "mainB comes from a distinct timeline block");
}

void testAutoSplitKeepsSectionsChordStable()
{
    juce::MidiFile midi;
    makeAutoTwoBarChordChangesFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-auto-split-chord-stable", midi);

    auto result = cadenza::arranger::autoSplitMidiFileForStyleImport(file, {});
    expect(result.ok, "auto-split succeeds when chords change every two bars");
    expect(!result.sections.empty(), "auto-split emits sections for two-bar chord runs");

    for (const auto& spec : result.sections) {
        auto info = cadenza::arranger::inspectMidiFileForStyleImport(file, spec.barStart, spec.barCount);
        expect(info.ok, "auto-split emitted section inspects");
        expect(!info.rangeChangesChord, "auto-split emitted section stays on one chord");
        expect(spec.barCount <= 2, "auto-split emitted section is capped to the two-bar chord run");
    }
}

void testAutoSplitDoesNotInventIntro()
{
    juce::MidiFile midi;
    makeAutoNoIntroFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-auto-split-no-intro", midi);

    auto result = cadenza::arranger::autoSplitMidiFileForStyleImport(file, {});
    expect(result.ok, "auto-split succeeds for repeated full-band song");
    const auto intro = std::find_if(result.sections.begin(), result.sections.end(), [](const auto& spec) {
        return spec.sectionId == "intro";
    });
    expect(intro == result.sections.end(), "auto-split does not invent an intro");
}

void testAutoSplitExtractsHeavyPreMainFill()
{
    juce::MidiFile midi;
    makeAutoFillBeforeMainFixture(midi, true);
    const auto file = writeMidi("cadenza-midi-style-auto-split-heavy-fill", midi);

    auto split = cadenza::arranger::autoSplitMidiFileForStyleImport(file, {});
    const auto fill = std::find_if(split.sections.begin(), split.sections.end(), [](const auto& spec) {
        return spec.sectionId == "fillAA";
    });
    expect(split.ok, "auto-split succeeds for repeated main with a heavy pre-main fill");
    expect(fill != split.sections.end(), "auto-split emits fillAA for a heavy pre-main transition bar");
    expect(fill->barStart == 3 && fill->barCount == 1, "fillAA comes from the one bar before mainA");

    auto converted = cadenza::arranger::convertMidiFileToNativeStyleMultiSection(file, split.sections, true);
    expect(converted.ok && converted.style != nullptr, "auto-split fill specs convert");
    const auto* fillSection = converted.style->findSection("fillAA");
    expect(fillSection != nullptr && fillSection->barCount == 1, "converted fillAA is a one-bar section");
    const auto* drums = fillSection != nullptr ? findPart(*fillSection, "drums") : nullptr;
    expect(drums != nullptr && !drums->notes.empty()
               && std::all_of(drums->notes.begin(), drums->notes.end(), [](const auto& note) {
                   return note.role == NoteRole::Absolute;
               }),
           "fill drum notes stay absolute");

    const auto json = cadenza::arranger::saveStyleToJson(*converted.style);
    const auto loaded = cadenza::arranger::loadStyleFromJson(json);
    expect(loaded.ok && loaded.style.findSection("fillAA") != nullptr,
           "fill section round-trips through JSON");
}

void testAutoSplitDoesNotExtractSteadyPreMainGrooveAsFill()
{
    juce::MidiFile midi;
    makeAutoFillBeforeMainFixture(midi, false);
    const auto file = writeMidi("cadenza-midi-style-auto-split-no-false-fill", midi);

    auto result = cadenza::arranger::autoSplitMidiFileForStyleImport(file, {});
    expect(result.ok, "auto-split succeeds for repeated main with steady pre-main groove");
    const auto fill = std::find_if(result.sections.begin(), result.sections.end(), [](const auto& spec) {
        return spec.sectionId.startsWith("fill");
    });
    expect(fill == result.sections.end(), "auto-split does not treat a steady groove bar as a fill");
}

void testAutoSplitSectionsConvertAndRoundTrip()
{
    juce::MidiFile midi;
    makeAutoTwoDistinctRepeatedMainsFixture(midi);
    const auto file = writeMidi("cadenza-midi-style-auto-split-roundtrip", midi);

    auto split = cadenza::arranger::autoSplitMidiFileForStyleImport(file, {});
    expect(split.ok && split.sections.size() >= 2, "auto-split returns multiple specs for round-trip");
    auto converted = cadenza::arranger::convertMidiFileToNativeStyleMultiSection(file, split.sections, true);
    expect(converted.ok && converted.style != nullptr, "auto-split specs convert through multi-section import");
    const auto json = cadenza::arranger::saveStyleToJson(*converted.style);
    const auto loaded = cadenza::arranger::loadStyleFromJson(json);
    expect(loaded.ok && loaded.style.findSection("mainA") != nullptr
               && loaded.style.findSection("mainB") != nullptr,
           "auto-split converted style round-trips through JSON");
}

void testInspectChordConfidence()
{
    juce::MidiFile clearMinor;
    makeFsharpMinorFixture(clearMinor);
    const auto clearFile = writeMidi("cadenza-midi-style-confidence-high", clearMinor);
    auto clearInfo = cadenza::arranger::inspectMidiFileForStyleImport(clearFile, 0, 4);
    expect(clearInfo.ok, "clear chord fixture inspects");
    expect(clearInfo.detectedChord.confidence == cadenza::arranger::MidiStyleChordConfidence::High,
           "clear minor triad with a third reports high confidence");

    juce::MidiFile sparse;
    makeBassDrumsOnlyFixture(sparse);
    const auto sparseFile = writeMidi("cadenza-midi-style-confidence-low", sparse);
    auto sparseInfo = cadenza::arranger::inspectMidiFileForStyleImport(sparseFile, 0, 4);
    expect(sparseInfo.ok, "bass and drums fixture inspects");
    expect(sparseInfo.detectedChord.confidence == cadenza::arranger::MidiStyleChordConfidence::Low,
           "bass and drums only reports low confidence");

    juce::MidiFile fallback;
    fallback.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence track;
    addProgram(track, 3, 0);
    addNote(track, 3, 0, 480, 60);
    addNote(track, 3, 0, 480, 61);
    addNote(track, 3, 0, 480, 66);
    fallback.addTrack(track);
    const auto fallbackFile = writeMidi("cadenza-midi-style-confidence-fallback-low", fallback);
    auto fallbackInfo = cadenza::arranger::inspectMidiFileForStyleImport(fallbackFile, 0, 4);
    expect(fallbackInfo.ok, "fallback fixture inspects");
    expect(fallbackInfo.detectedChord.confidence == cadenza::arranger::MidiStyleChordConfidence::Low,
           "fallback-to-C reports low confidence");
}
}

int main()
{
    testCmajorMapsPartsAndRoundTrips();
    testAminorSourceChord();
    testNormalizeFsharpMinorToC();
    testMultiSectionImportNormalizesEachSectionAndRoundTrips();
    testOverrideSourceChord();
    testInspectReadsEarliestTempo();
    testSmpteRejects();
    testFallbackWarning();
    testLowConfidenceSourceChordFallsBackUnlessOverridden();
    testInspectAutoRangeSkipsSparseIntro();
    testInspectReportsChordChangingRange();
    testInspectReportsSingleChordRange();
    testAutoSplitFindsMainAfterSparseIntro();
    testAutoSplitFindsDistinctRepeatedMainB();
    testAutoSplitKeepsSectionsChordStable();
    testAutoSplitDoesNotInventIntro();
    testAutoSplitExtractsHeavyPreMainFill();
    testAutoSplitDoesNotExtractSteadyPreMainGrooveAsFill();
    testAutoSplitSectionsConvertAndRoundTrip();
    testInspectChordConfidence();
    std::cout << "All MIDI style converter tests passed\n";
    return 0;
}
