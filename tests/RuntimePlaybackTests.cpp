#include "Audio/MidiChannel.h"
#include "Arranger/PatternTransposer.h"
#include "Arranger/PlaybackDiagnostics.h"
#include "Arranger/RuntimePlayback.h"
#include "Arranger/Style.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
int failures = 0;

void expect(bool cond, const std::string& msg)
{
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

using namespace cadenza::arranger;

void channelMappingIsOneBasedAtCadenzaBoundary()
{
    using cadenza::audio::synthChannelFromCadenzaChannel;
    expect(synthChannelFromCadenzaChannel(1).value() == 0, "Cadenza ch 1 -> synth ch 0");
    expect(synthChannelFromCadenzaChannel(10).value() == 9, "Cadenza drum ch 10 -> synth ch 9");
    expect(cadenza::audio::isSynthDrumChannel(9), "FluidSynth ch 9 convention is drums");
    expect(!cadenza::audio::isSynthDrumChannel(10), "FluidSynth ch 10 is not the GM drum channel");
    expect(synthChannelFromCadenzaChannel(16).value() == 15, "Cadenza ch 16 -> synth ch 15");
    expect(!synthChannelFromCadenzaChannel(0).has_value(), "Cadenza ch 0 invalid");
    expect(!synthChannelFromCadenzaChannel(17).has_value(), "Cadenza ch 17 invalid");
}

void drumChannelRecognitionIsConventional()
{
    using cadenza::audio::isCadenzaDrumChannel;
    expect(isCadenzaDrumChannel(10), "Cadenza channel 10 is drums");
    expect(!isCadenzaDrumChannel(9), "Cadenza channel 9 is not drums");
    expect(!isCadenzaDrumChannel(11), "Cadenza channel 11 is not drums");
}

void partPresetMetadataDefaultsSafely()
{
    Part p;
    expect(!p.program.has_value(), "part program defaults empty");
    expect(!p.bankMsb.has_value(), "part bank MSB defaults empty");
    expect(!p.bankLsb.has_value(), "part bank LSB defaults empty");
    expect(!p.volume.has_value(), "part volume defaults empty");
    expect(!p.pan.has_value(), "part pan defaults empty");
    expect(!p.reverb.has_value(), "part reverb defaults empty");
    expect(!p.chorus.has_value(), "part chorus defaults empty");
    expect(!p.percussion, "part percussion defaults false");
}

void drumPartCanCarryPresetAndPercussionFlag()
{
    Part p;
    p.name = "drums";
    p.midiChannel = 10;
    p.percussion = true;
    p.bankMsb = 120;
    p.bankLsb = 0;
    p.program = 0;
    p.notes.push_back(PatternNote{ 0, 120, 36, 100, NoteRole::Absolute, 0 });

    expect(p.percussion, "drum part percussion flag");
    expect(p.midiChannel == 10, "drum part Cadenza channel");
    expect(p.program && *p.program == 0, "drum part program retained");
    expect(p.bankMsb && *p.bankMsb == 120, "drum part bank MSB retained");
    expect(p.notes[0].role == NoteRole::Absolute, "drum note remains absolute");
}

void playbackSetupCarriesBankProgramAndSynthChannel()
{
    Part p;
    p.name = "bass";
    p.midiChannel = 2;
    p.bankMsb = 0;
    p.bankLsb = 0;
    p.program = 32;
    p.volume = 96;
    p.pan = 64;
    p.reverb = 40;
    p.chorus = 12;
    p.notes.push_back(PatternNote{ 0, 480, 36, 90, NoteRole::ChordRoot, 0 });

    const auto setup = playbackSetupForPart(p);
    expect(setup.partName == "bass", "setup part name");
    expect(setup.cadenzaChannel == 2, "setup Cadenza channel");
    expect(setup.synthChannel && *setup.synthChannel == 1, "setup synth channel");
    expect(setup.bankMsb && *setup.bankMsb == 0, "setup bank MSB");
    expect(setup.bankLsb && *setup.bankLsb == 0, "setup bank LSB");
    expect(setup.program && *setup.program == 32, "setup program");
    expect(setup.volume && *setup.volume == 96, "setup volume CC7");
    expect(setup.pan && *setup.pan == 64, "setup pan CC10");
    expect(setup.reverb && *setup.reverb == 40, "setup reverb CC91");
    expect(setup.chorus && *setup.chorus == 12, "setup chorus CC93");
    expect(!setup.percussion, "setup non-percussion");
    expect(setup.noteCount == 1, "setup note count");
}

void melodicPartForcesGmBankZero()
{
    // A melodic part carrying Yamaha XG/GS variation banks must play on GM bank 0
    // (the program already selects the right GM instrument family).
    Part p;
    p.name = "harmony";
    p.midiChannel = 3;
    p.bankMsb = 0;
    p.bankLsb = 112;    // XG variation bank not present in a GM SoundFont
    p.program = 48;     // GM String Ensemble 1

    const auto setup = playbackSetupForPart(p);
    expect(setup.bankMsb && *setup.bankMsb == 0, "melodic bank MSB forced to GM 0");
    expect(setup.bankLsb && *setup.bankLsb == 0, "melodic bank LSB forced to GM 0");
    expect(setup.program && *setup.program == 48, "program (GM voice) preserved");
}

void drumPartKeepsItsBank()
{
    // Drums must keep their bank so the synth's percussion mapping still applies.
    Part p;
    p.name = "drums";
    p.midiChannel = 10;
    p.percussion = true;
    p.bankMsb = 127;
    p.bankLsb = 0;
    p.program = 8;      // Room kit

    const auto setup = playbackSetupForPart(p);
    expect(setup.bankMsb && *setup.bankMsb == 127, "drum bank MSB preserved");
    expect(setup.program && *setup.program == 8, "drum kit program preserved");
    expect(setup.percussion, "still flagged percussion");
}

void percussionSubRhythmRoutesToDrumChannel()
{
    Part p;
    p.name = "rhythm2";
    p.midiChannel = 9;
    p.percussion = true;
    p.bankMsb = 127;
    p.bankLsb = 0;
    p.program = 0;

    const auto setup = playbackSetupForPart(p);
    expect(setup.sourceChannel == 9, "rhythm2 setup retains source channel");
    expect(setup.cadenzaChannel == 10, "rhythm2 setup routes to Cadenza drum channel");
    expect(setup.synthChannel && *setup.synthChannel == 9, "rhythm2 setup routes to synth drum channel");
    expect(setup.percussion, "rhythm2 setup remains percussion");
}

void melodicPartDoesNotRouteToDrumChannel()
{
    Part p;
    p.name = "chord1";
    p.midiChannel = 12;
    p.percussion = false;
    p.program = 27;

    const auto setup = playbackSetupForPart(p);
    expect(setup.sourceChannel == 12, "melodic setup source channel");
    expect(setup.cadenzaChannel == 12, "melodic setup keeps playback channel");
    expect(setup.synthChannel && *setup.synthChannel == 11, "melodic setup keeps synth channel");
    expect(!setup.percussion, "melodic setup is not percussion");
}

void playbackSetupMarksDrumChannelAsPercussion()
{
    Part p;
    p.name = "drums";
    p.midiChannel = 10;
    p.notes.push_back(PatternNote{ 0, 120, 36, 100, NoteRole::Absolute, 0 });

    const auto setup = playbackSetupForPart(p);
    expect(setup.synthChannel && *setup.synthChannel == 9, "drum setup synth channel");
    expect(setup.percussion, "drum setup percussion");
    expect(setup.program && *setup.program == 0, "drum setup GM Standard Kit fallback");
}

void gmDrumNotesStayUnchanged()
{
    Part p;
    p.name = "drums";
    p.midiChannel = 10;
    p.percussion = true;
    p.bankMsb = 120;

    const auto remap = drumNoteForPlayback(p, 36);
    expect(!remap.yamahaXg, "GM drum bank is not Yamaha/XG");
    expect(!remap.remapped, "GM drum note is unchanged");
    expect(remap.playbackNote == 36, "GM kick remains 36");
}

void yamahaXgKnownProblemNoteRemaps()
{
    Part p;
    p.name = "drums";
    p.midiChannel = 10;
    p.percussion = true;
    p.bankMsb = 127;
    p.program = 0;

    const auto remap = drumNoteForPlayback(p, 31);
    expect(remap.yamahaXg, "bank MSB 127 marks Yamaha/XG drums");
    expect(remap.remapped, "known Yamaha/XG problem note remaps");
    expect(remap.originalNote == 31, "remap preserves original note");
    expect(remap.playbackNote == 37, "Yamaha/XG sticks note maps to GM side stick");
}

void unknownYamahaXgNotesStayUnchanged()
{
    Part p;
    p.name = "drums";
    p.midiChannel = 10;
    p.percussion = true;
    p.bankMsb = 127;

    const auto remap = drumNoteForPlayback(p, 36);
    expect(remap.yamahaXg, "Yamaha/XG drum detected");
    expect(!remap.remapped, "unknown Yamaha/XG note is unchanged");
    expect(remap.playbackNote == 36, "unknown Yamaha/XG note passes through");
}

void nonDrumPartsNeverRemap()
{
    Part p;
    p.name = "bass";
    p.midiChannel = 2;
    p.percussion = false;
    p.bankMsb = 127;

    const auto remap = drumNoteForPlayback(p, 31);
    expect(!remap.yamahaXg, "non-drum bank 127 does not activate drum remap");
    expect(!remap.remapped, "non-drum note is not remapped");
    expect(remap.playbackNote == 31, "non-drum note remains original");
}

void drumPlaybackBypassesChordTranspositionAndThenRemaps()
{
    Part p;
    p.name = "drums";
    p.midiChannel = 10;
    p.percussion = true;
    p.bankMsb = 127;

    PatternNote note;
    note.pitch = 31;
    note.role = NoteRole::ChordRoot;

    TransposeContext ctx;
    ctx.chord.rootPitchClass = 7;
    ctx.chord.quality = cadenza::midi::ChordQuality::Major;
    ctx.globalTranspose = 0;
    ctx.globalOctave = 0;

    const auto playback = playbackNoteForPart(p, note, ctx);
    expect(playback && *playback == 37, "drum playback bypasses chord transposition and applies drum remap");
}

void percussionSubRhythmBypassesChordTransposition()
{
    Part p;
    p.name = "rhythm2";
    p.midiChannel = 9;
    p.percussion = true;
    p.bankMsb = 127;

    PatternNote note;
    note.pitch = 42;
    note.role = NoteRole::ChordRoot;

    TransposeContext ctx;
    ctx.chord.rootPitchClass = 9;
    ctx.chord.quality = cadenza::midi::ChordQuality::Minor;

    const auto playback = playbackNoteForPart(p, note, ctx);
    expect(playback && *playback == 42, "rhythm2 percussion bypasses chord transposition");
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void playbackDiagnosticsExportCsvMidiAndSummary()
{
    Style style;
    style.name = "Diagnostic Test";
    style.id = "diagnostic-test";
    style.beatsPerBar = 4;
    style.ticksPerBeat = 120;

    Section section;
    section.name = "mainA";
    section.barCount = 1;

    Part drums;
    drums.name = "drums";
    drums.midiChannel = 10;
    drums.instrument = "XG Standard Kit";
    drums.percussion = true;
    drums.bankMsb = 127;
    drums.bankLsb = 0;
    drums.program = 0;
    drums.volume = 110;
    drums.pan = 64;
    drums.reverb = 20;
    drums.chorus = 8;
    drums.notes.push_back(PatternNote{ 0, 60, 31, 90, NoteRole::Absolute, 0 });
    drums.notes.push_back(PatternNote{ 120, 60, 36, 110, NoteRole::Absolute, 0 });

    Part bass;
    bass.name = "bass";
    bass.midiChannel = 2;
    bass.instrument = "Acoustic Bass";
    bass.program = 32;
    bass.notes.push_back(PatternNote{ 0, 120, 36, 80, NoteRole::ChordRoot, 0 });

    section.parts.push_back(std::move(drums));
    section.parts.push_back(std::move(bass));
    style.sections.push_back(std::move(section));

    TransposeContext ctx;
    ctx.chord.rootPitchClass = 7;
    ctx.chord.quality = cadenza::midi::ChordQuality::Major;

    const auto outDir = std::filesystem::temp_directory_path() / "cadenza_playback_diag_test";
    std::filesystem::remove_all(outDir);

    const auto result = exportPlaybackDiagnostics(style, "mainA", ctx, outDir.string(), 4);
    expect(result.ok, "diagnostic export succeeds");
    expect(std::filesystem::exists(outDir / "cadenza_playback_events.csv"), "diagnostic CSV exists");
    expect(std::filesystem::exists(outDir / "cadenza_playback.mid"), "diagnostic MIDI exists");
    expect(std::filesystem::exists(outDir / "playback_summary.md"), "diagnostic summary exists");
    expect(result.eventCount == 12, "one-bar section repeats over first four bars");

    const auto csv = readText(outDir / "cadenza_playback_events.csv");
    expect(csv.find("tick,channel,note,source_note") != std::string::npos, "CSV header present");
    expect(csv.find("0,10,37,31,90,60,drums,absolute,127/0/0") != std::string::npos,
           "CSV includes remapped Yamaha/XG drum note");
    expect(csv.find("true") != std::string::npos, "CSV includes transposed/remapped flags");

    const auto summary = readText(outDir / "playback_summary.md");
    expect(summary.find("section: mainA") != std::string::npos, "summary includes section");
    expect(summary.find("parts: 2") != std::string::npos, "summary includes part count");
    expect(summary.find("Drum Notes") != std::string::npos, "summary includes drum notes");

    std::filesystem::remove_all(outDir);
}

void playbackDiagnosticsRoutesPercussionEventsToDrumChannel()
{
    Style style;
    style.name = "Rhythm2 Routing Test";
    style.id = "rhythm2-routing-test";
    style.beatsPerBar = 4;
    style.ticksPerBeat = 120;

    Section section;
    section.name = "mainA";
    section.barCount = 1;

    Part rhythm2;
    rhythm2.name = "rhythm2";
    rhythm2.midiChannel = 9;
    rhythm2.instrument = "XG Percussion";
    rhythm2.percussion = true;
    rhythm2.bankMsb = 127;
    rhythm2.bankLsb = 0;
    rhythm2.program = 0;
    rhythm2.notes.push_back(PatternNote{ 0, 60, 42, 90, NoteRole::Absolute, 0 });

    Part chord1;
    chord1.name = "chord1";
    chord1.midiChannel = 12;
    chord1.instrument = "Guitar";
    chord1.program = 27;
    chord1.notes.push_back(PatternNote{ 0, 60, 64, 80, NoteRole::Chord3, 0 });

    section.parts.push_back(std::move(rhythm2));
    section.parts.push_back(std::move(chord1));
    style.sections.push_back(std::move(section));

    TransposeContext ctx;
    ctx.chord.rootPitchClass = 9;
    ctx.chord.quality = cadenza::midi::ChordQuality::Minor;

    const auto outDir = std::filesystem::temp_directory_path() / "cadenza_rhythm2_routing_test";
    std::filesystem::remove_all(outDir);

    const auto result = exportPlaybackDiagnostics(style, "mainA", ctx, outDir.string(), 1);
    expect(result.ok, "rhythm2 diagnostic export succeeds");

    const auto csv = readText(outDir / "cadenza_playback_events.csv");
    expect(csv.find("0,10,42,42,90,60,rhythm2,absolute,127/0/0") != std::string::npos,
           "rhythm2 diagnostic event routes to drum channel 10");
    expect(csv.find("0,12,60,64,80,60,chord1,chord-3,-/-/27") != std::string::npos,
           "melodic diagnostic event keeps channel 12");

    std::filesystem::remove_all(outDir);
}
}

int main()
{
    channelMappingIsOneBasedAtCadenzaBoundary();
    drumChannelRecognitionIsConventional();
    partPresetMetadataDefaultsSafely();
    drumPartCanCarryPresetAndPercussionFlag();
    playbackSetupCarriesBankProgramAndSynthChannel();
    melodicPartForcesGmBankZero();
    drumPartKeepsItsBank();
    percussionSubRhythmRoutesToDrumChannel();
    melodicPartDoesNotRouteToDrumChannel();
    playbackSetupMarksDrumChannelAsPercussion();
    gmDrumNotesStayUnchanged();
    yamahaXgKnownProblemNoteRemaps();
    unknownYamahaXgNotesStayUnchanged();
    nonDrumPartsNeverRemap();
    drumPlaybackBypassesChordTranspositionAndThenRemaps();
    percussionSubRhythmBypassesChordTransposition();
    playbackDiagnosticsExportCsvMidiAndSummary();
    playbackDiagnosticsRoutesPercussionEventsToDrumChannel();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All RuntimePlayback tests passed\n";
    return EXIT_SUCCESS;
}
