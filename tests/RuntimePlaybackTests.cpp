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
#include <vector>

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
    expect(isCadenzaDrumChannel(10), "Cadenza channel 10 (RHY1) is drums");
    expect(isCadenzaDrumChannel(9), "Cadenza channel 9 (RHY2) is drums");
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

void mixDefaultsFillInWhenStyleOmitsThem()
{
    // A melodic comp part with no mix info should get a wide-ish pan + ambience.
    Part chord1;
    chord1.name = "chord1";
    chord1.midiChannel = 12;
    chord1.program = 27;
    chord1.notes.push_back(PatternNote{ 0, 480, 60, 90, NoteRole::ChordRoot, 0 });

    const auto s1 = playbackSetupForPart(chord1);
    expect(s1.volume && *s1.volume == 100, "default volume 100");
    expect(s1.pan && *s1.pan == 48, "chord1 panned slightly left");
    expect(s1.reverb && *s1.reverb == 25, "melodic reverb floor 25 when unset");
    expect(s1.chorus && *s1.chorus == 16, "chord part default chorus 16");

    // Drums centered, lighter reverb floor, no chorus.
    Part drums;
    drums.name = "drums";
    drums.midiChannel = 10;
    drums.percussion = true;
    drums.notes.push_back(PatternNote{ 0, 60, 36, 100, NoteRole::Absolute, 0 });

    const auto s2 = playbackSetupForPart(drums);
    expect(s2.pan && *s2.pan == 64, "drums centered");
    expect(s2.reverb && *s2.reverb == 12, "drums reverb floor 12");
    expect(s2.chorus && *s2.chorus == 0, "drums no chorus");

    // A style that sends reverb=0 is floored up (no bone-dry parts); pan is kept.
    Part dryPart;
    dryPart.name = "chord2";
    dryPart.midiChannel = 13;
    dryPart.pan = 100;
    dryPart.reverb = 0;
    const auto s3 = playbackSetupForPart(dryPart);
    expect(s3.pan && *s3.pan == 100, "explicit pan preserved");
    expect(s3.reverb && *s3.reverb == 25, "explicit reverb=0 raised to floor");

    // A richer explicit reverb above the floor is preserved.
    Part wetPart;
    wetPart.name = "pad";
    wetPart.midiChannel = 14;
    wetPart.reverb = 55;
    const auto s4 = playbackSetupForPart(wetPart);
    expect(s4.reverb && *s4.reverb == 55, "explicit reverb above floor preserved");
}

void melodicPartPreservesVariationBankAndDefaultsMissingBank()
{
    // A melodic part with a Yamaha XG/GS variation bank should preserve that
    // bank so XG-capable SoundFonts can use the richer preset.
    Part p;
    p.name = "harmony";
    p.midiChannel = 3;
    p.bankMsb = 0;
    p.bankLsb = 112;    // XG variation bank not present in a GM SoundFont
    p.program = 48;     // GM String Ensemble 1

    const auto setup = playbackSetupForPart(p);
    expect(setup.bankMsb && *setup.bankMsb == 0, "melodic bank MSB preserved");
    expect(setup.bankLsb && *setup.bankLsb == 112, "melodic bank LSB preserved");
    expect(setup.program && *setup.program == 48, "program (GM voice) preserved");

    // When the style omits bank select entirely, we still start from GM bank 0/0
    // rather than inheriting stale bank state from an earlier channel.
    Part q;
    q.name = "pad";
    q.midiChannel = 4;
    q.program = 50;

    const auto setup2 = playbackSetupForPart(q);
    expect(setup2.bankMsb && *setup2.bankMsb == 0, "melodic missing bank MSB defaults to GM 0");
    expect(setup2.bankLsb && *setup2.bankLsb == 0, "melodic missing bank LSB defaults to GM 0");
    expect(setup2.program && *setup2.program == 50, "melodic program preserved when bank defaults");
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
    expect(setup.cadenzaChannel == 9, "rhythm2 setup keeps its own drum channel (RHY2)");
    expect(setup.synthChannel && *setup.synthChannel == 8, "rhythm2 setup routes to synth drum channel 8");
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

void drumAndRhythm2GetSeparateChannels()
{
    Section section;
    section.name = "mainA";

    Part rhythm2;
    rhythm2.name = "rhythm2";
    rhythm2.midiChannel = 9;
    rhythm2.percussion = true;
    rhythm2.bankMsb = 127;
    rhythm2.bankLsb = 0;
    rhythm2.program = 0;

    Part drums;
    drums.name = "drums";
    drums.midiChannel = 10;
    drums.percussion = true;
    drums.bankMsb = 120;
    drums.bankLsb = 0;
    drums.program = 8;
    drums.volume = 110;

    Part chord1;
    chord1.name = "chord1";
    chord1.midiChannel = 12;
    chord1.program = 27;

    section.parts.push_back(std::move(rhythm2));
    section.parts.push_back(std::move(drums));
    section.parts.push_back(std::move(chord1));

    const auto setups = playbackSetupsForSection(section);
    const PartPlaybackSetup* drumSetup = nullptr;
    const PartPlaybackSetup* rhythm2Setup = nullptr;
    const PartPlaybackSetup* chordSetup = nullptr;
    for (const auto& setup : setups) {
        if (setup.cadenzaChannel == 10)
            drumSetup = &setup;
        if (setup.cadenzaChannel == 9)
            rhythm2Setup = &setup;
        if (setup.cadenzaChannel == 12)
            chordSetup = &setup;
    }

    // RHY1 and RHY2 are now independent drum channels, each with its own kit.
    expect(drumSetup != nullptr, "main drum channel setup exists");
    expect(drumSetup && drumSetup->partName == "drums", "main drums setup on channel 10");
    expect(drumSetup && drumSetup->program && *drumSetup->program == 8, "main drums kit program kept");
    expect(drumSetup && drumSetup->bankMsb && *drumSetup->bankMsb == 120, "main drums bank kept");
    expect(rhythm2Setup != nullptr, "rhythm2 has its own setup on channel 9");
    expect(rhythm2Setup && rhythm2Setup->partName == "rhythm2", "rhythm2 setup on channel 9");
    expect(rhythm2Setup && rhythm2Setup->percussion, "rhythm2 setup is percussion");
    expect(chordSetup != nullptr, "melodic setup still exists on its own channel");
    expect(chordSetup && chordSetup->partName == "chord1", "melodic setup is preserved");
}

void drumChannelSetupUsesRhythm2WhenAlone()
{
    Section section;
    section.name = "mainA";

    Part rhythm2;
    rhythm2.name = "rhythm2";
    rhythm2.midiChannel = 9;
    rhythm2.percussion = true;
    rhythm2.bankMsb = 127;
    rhythm2.bankLsb = 0;
    rhythm2.program = 0;

    section.parts.push_back(std::move(rhythm2));

    const auto setups = playbackSetupsForSection(section);
    expect(setups.size() == 1, "rhythm2-alone section has one setup");
    expect(setups[0].partName == "rhythm2", "rhythm2 setup is used when alone");
    expect(setups[0].sourceChannel == 9, "rhythm2 source channel retained");
    expect(setups[0].cadenzaChannel == 9, "rhythm2 keeps its own drum channel");
    expect(setups[0].program && *setups[0].program == 0, "rhythm2 kit program retained");
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

void registerFenceLeavesInRangeNotesUntouched()
{
    Part p;
    p.name = "harmony";
    p.midiChannel = 3;

    PatternNote note;
    note.pitch = 60;                 // C4 — squarely inside the harmony window
    note.role = NoteRole::ChordRoot;

    TransposeContext ctx;
    ctx.chord.rootPitchClass = 0;    // C major
    ctx.chord.quality = cadenza::midi::ChordQuality::Major;

    const auto playback = playbackNoteForPart(p, note, ctx);
    expect(playback && *playback == 60, "in-range harmony note is not moved by the register fence");
}

void registerFenceFoldsHighNoteDownIntoWindow()
{
    Part p;
    p.name = "harmony";
    p.midiChannel = 3;

    PatternNote note;
    note.pitch = 96;                 // C7 — above the harmony window [48,84]
    note.role = NoteRole::ChordRoot;

    TransposeContext ctx;
    ctx.chord.rootPitchClass = 0;    // C major (root stays pitch class 0)
    ctx.chord.quality = cadenza::midi::ChordQuality::Major;

    const auto playback = playbackNoteForPart(p, note, ctx);
    expect(playback && *playback == 84, "above-window harmony note folds down one octave into [48,84]");
}

void registerFenceFoldsLowNoteUpIntoWindow()
{
    Part p;
    p.name = "pad";
    p.midiChannel = 4;

    PatternNote note;
    note.pitch = 24;                 // C1 — below the pad window [48,84]
    note.role = NoteRole::ChordRoot;

    TransposeContext ctx;
    ctx.chord.rootPitchClass = 0;    // C major
    ctx.chord.quality = cadenza::midi::ChordQuality::Major;

    const auto playback = playbackNoteForPart(p, note, ctx);
    expect(playback && *playback == 48, "below-window pad note folds up into [48,84]");
}

void humanizeOffIsExactlyOriginal()
{
    Part drums; drums.name = "drums"; drums.percussion = true;
    const auto prof = humanizeProfileForPart(drums, 0);
    expect(prof.velocityJitter == 0 && prof.maxLateTicks == 0, "amount 0 yields a zero humanize profile");

    const std::uint32_t seed = humanizeSeed(0, 36, 0, 0);
    expect(humanizeVelocity(100, seed, 0) == 100, "zero jitter leaves velocity unchanged");
    expect(humanizeLateTicks(seed, 0) == 0, "zero late-ticks yields no timing offset");
}

void humanizeVelocityStaysInRangeAndIsDeterministic()
{
    Part comp; comp.name = "harmony";
    const auto prof = humanizeProfileForPart(comp, 100);
    expect(prof.velocityJitter > 0, "full amount gives a non-zero velocity jitter");

    bool inRange = true, deterministic = true;
    for (int s = 0; s < 500; ++s) {
        const std::uint32_t seed = humanizeSeed(s * 17, 60, 2, s);
        const int v1 = humanizeVelocity(80, seed, prof.velocityJitter);
        const int v2 = humanizeVelocity(80, seed, prof.velocityJitter);
        if (v1 < 1 || v1 > 127 || v1 < 80 - prof.velocityJitter || v1 > 80 + prof.velocityJitter) inRange = false;
        if (v1 != v2) deterministic = false;
    }
    expect(inRange, "humanized velocity stays within [base-jitter, base+jitter] and [1,127]");
    expect(deterministic, "same seed always yields the same humanized velocity");
}

void humanizeVelocityClampsExtremes()
{
    const std::uint32_t seed = humanizeSeed(1, 2, 3, 4);
    expect(humanizeVelocity(125, seed, 20) <= 127, "humanized velocity never exceeds 127");
    expect(humanizeVelocity(3,   seed, 20) >= 1,   "humanized velocity never drops below 1");
}

void humanizeLateTicksStayWithinBound()
{
    bool ok = true;
    for (int s = 0; s < 500; ++s) {
        const int late = humanizeLateTicks(humanizeSeed(s, s * 3, 1, s), 8);
        if (late < 0 || late > 8) ok = false;
    }
    expect(ok, "late offset stays within [0, maxLateTicks]");
}

void humanizeProfileScalesAndDiffersByRole()
{
    Part drums; drums.name = "drums"; drums.percussion = true;
    Part bass;  bass.name  = "bass";
    const auto d = humanizeProfileForPart(drums, 100);
    const auto b = humanizeProfileForPart(bass, 100);
    expect(d.velocityJitter > b.velocityJitter, "drums get more velocity life than bass");

    const auto half = humanizeProfileForPart(drums, 50);
    expect(half.velocityJitter < d.velocityJitter && half.velocityJitter > 0, "amount scales the profile down");
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::vector<unsigned char> readBinary(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

int countMidiBytes(const std::vector<unsigned char>& bytes, std::initializer_list<unsigned char> pattern)
{
    const std::vector<unsigned char> needle(pattern);
    if (needle.empty() || bytes.size() < needle.size())
        return 0;

    int count = 0;
    for (std::size_t i = 0; i + needle.size() <= bytes.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < needle.size(); ++j)
            match = match && bytes[i + j] == needle[j];
        if (match)
            ++count;
    }
    return count;
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

void playbackDiagnosticsMidiSetupPrefersMainDrumsOnSharedChannel()
{
    Style style;
    style.name = "Shared Drum Setup Test";
    style.id = "shared-drum-setup-test";
    style.beatsPerBar = 4;
    style.ticksPerBeat = 120;

    Section section;
    section.name = "mainA";
    section.barCount = 1;

    Part rhythm2;
    rhythm2.name = "rhythm2";
    rhythm2.midiChannel = 9;
    rhythm2.percussion = true;
    rhythm2.bankMsb = 127;
    rhythm2.bankLsb = 0;
    rhythm2.program = 0;
    rhythm2.notes.push_back(PatternNote{ 0, 60, 42, 90, NoteRole::Absolute, 0 });

    Part drums;
    drums.name = "drums";
    drums.midiChannel = 10;
    drums.percussion = true;
    drums.bankMsb = 120;
    drums.bankLsb = 0;
    drums.program = 8;
    drums.notes.push_back(PatternNote{ 60, 60, 36, 100, NoteRole::Absolute, 0 });

    section.parts.push_back(std::move(rhythm2));
    section.parts.push_back(std::move(drums));
    style.sections.push_back(std::move(section));

    TransposeContext ctx;
    ctx.chord.rootPitchClass = 0;
    ctx.chord.quality = cadenza::midi::ChordQuality::Major;

    const auto outDir = std::filesystem::temp_directory_path() / "cadenza_shared_drum_setup_test";
    std::filesystem::remove_all(outDir);

    const auto result = exportPlaybackDiagnostics(style, "mainA", ctx, outDir.string(), 1);
    expect(result.ok, "shared drum setup diagnostic export succeeds");

    const auto midi = readBinary(outDir / "cadenza_playback.mid");
    expect(countMidiBytes(midi, { 0xB9, 0x00, 120 }) == 1, "MIDI channel 10 bank MSB uses main drums");
    expect(countMidiBytes(midi, { 0xC9, 8 }) == 1, "MIDI channel 10 program uses main drums kit");
    expect(countMidiBytes(midi, { 0xB9, 0x00, 127 }) == 0, "rhythm2 bank does not override channel 10 setup");
    expect(countMidiBytes(midi, { 0xC9, 0 }) == 0, "rhythm2 program does not override channel 10 setup");

    const auto csv = readText(outDir / "cadenza_playback_events.csv");
    expect(csv.find("0,9,42,42,90,60,rhythm2") != std::string::npos,
           "rhythm2 emits notes on its own drum channel 9");
    expect(csv.find("60,10,36,36,100,60,drums") != std::string::npos,
           "main drums still emits notes on channel 10");

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
    rhythm2.percussion = false; // channel identity alone must classify RHY2 as percussion.
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
    expect(csv.find("0,9,42,42,90,60,rhythm2,absolute,127/0/0") != std::string::npos,
           "rhythm2 diagnostic event routes to its own drum channel 9");
    const auto summary = readText(outDir / "playback_summary.md");
    expect(summary.find("- 42") != std::string::npos,
           "rhythm2 diagnostic summary classifies channel 9 as percussion");
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
    mixDefaultsFillInWhenStyleOmitsThem();
    melodicPartPreservesVariationBankAndDefaultsMissingBank();
    drumPartKeepsItsBank();
    percussionSubRhythmRoutesToDrumChannel();
    melodicPartDoesNotRouteToDrumChannel();
    drumAndRhythm2GetSeparateChannels();
    drumChannelSetupUsesRhythm2WhenAlone();
    playbackSetupMarksDrumChannelAsPercussion();
    gmDrumNotesStayUnchanged();
    yamahaXgKnownProblemNoteRemaps();
    unknownYamahaXgNotesStayUnchanged();
    nonDrumPartsNeverRemap();
    drumPlaybackBypassesChordTranspositionAndThenRemaps();
    percussionSubRhythmBypassesChordTransposition();
    registerFenceLeavesInRangeNotesUntouched();
    registerFenceFoldsHighNoteDownIntoWindow();
    registerFenceFoldsLowNoteUpIntoWindow();
    humanizeOffIsExactlyOriginal();
    humanizeVelocityStaysInRangeAndIsDeterministic();
    humanizeVelocityClampsExtremes();
    humanizeLateTicksStayWithinBound();
    humanizeProfileScalesAndDiffersByRole();
    playbackDiagnosticsExportCsvMidiAndSummary();
    playbackDiagnosticsMidiSetupPrefersMainDrumsOnSharedChannel();
    playbackDiagnosticsRoutesPercussionEventsToDrumChannel();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All RuntimePlayback tests passed\n";
    return EXIT_SUCCESS;
}
