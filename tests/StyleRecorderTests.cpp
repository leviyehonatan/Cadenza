#include "Arranger/StyleRecorder.h"
#include "Arranger/StyleLoader.h"

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

using namespace cadenza::arranger;

RecorderConfig defaultConfig()
{
    RecorderConfig cfg;
    cfg.name = "Test Groove";
    cfg.tempo = 100;
    cfg.bars = 2;
    cfg.ticksPerBeat = 960;        // bar = 3840 ticks, section = 7680
    return cfg;
}

void sessionBuildsPlayableSkeleton()
{
    StyleRecorder rec;
    expect(!rec.sessionActive(), "no session before start");

    rec.startSession(defaultConfig());
    expect(rec.sessionActive(), "session active after start");
    expect(rec.sectionLengthTicks() == 7680, "2 bars of 4/4 at 960 ppq = 7680 ticks");

    auto style = rec.snapshotStyle();
    expect(style->name == "Test Groove", "style carries the session name");
    expect(style->id == "test-groove", "style id is sanitized");
    expect(style->defaultTempo == 100, "style carries the tempo");
    expect(style->sections.size() == 1 && style->sections[0].name == "mainA",
           "one mainA section");
    expect(style->sections[0].barCount == 2, "section bar count");
    expect(style->sections[0].parts.empty(), "skeleton has no parts yet");
}

void bassTakeBakesRolesAndPolicy()
{
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Bass);

    rec.noteOn(36, 100, 0);      rec.noteOff(36, 900);     // C2 -> root
    rec.noteOn(43, 90, 1920);    rec.noteOff(43, 2400);    // G2 -> 5th
    rec.noteOn(38, 80, 3840);    rec.noteOff(38, 4300);    // D2 -> color

    expect(rec.hasPendingTake(), "take pending after notes");
    expect(rec.commitTake(), "commit succeeds");
    expect(!rec.hasPendingTake(), "take cleared after commit");

    auto style = rec.snapshotStyle();
    const auto& parts = style->sections[0].parts;
    expect(parts.size() == 1, "one part created");
    const auto& bass = parts[0];
    expect(bass.name == "bass" && bass.midiChannel == 11, "bass part on channel 11");
    expect(!bass.percussion, "bass is melodic");
    expect(bass.program && *bass.program == 33, "bass gets the role-default program");
    expect(bass.yamahaPolicy.has_value(), "bass has a playback policy");
    expect(bass.yamahaPolicy->ntr == YamahaNtr::RootTransposition
               && bass.yamahaPolicy->ntt == YamahaNtt::Bypass,
           "bass policy is RootTransposition+Bypass");
    expect(bass.yamahaPolicy->bassOn, "bass policy has bassOn for slash chords");

    expect(bass.notes.size() == 3, "three notes committed");
    expect(bass.notes[0].role == NoteRole::ChordRoot, "C bakes as root");
    expect(bass.notes[1].role == NoteRole::Chord5, "G bakes as 5th");
    expect(bass.notes[2].role == NoteRole::ChordColor, "D bakes as color");
    expect(bass.notes[0].duration == 900, "duration preserved");
}

void drumsAreAbsoluteAndPercussive()
{
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Drums);

    rec.noteOn(36, 110, 5);   rec.noteOff(36, 100);   // kick
    rec.noteOn(42, 70, 480);  rec.noteOff(42, 520);   // hat
    rec.commitTake();

    auto style = rec.snapshotStyle();
    const auto& drums = style->sections[0].parts[0];
    expect(drums.percussion && drums.midiChannel == 10, "drums on channel 10, percussive");
    expect(drums.notes.size() == 2, "two drum hits");
    expect(drums.notes[0].role == NoteRole::Absolute
               && drums.notes[1].role == NoteRole::Absolute,
           "drum hits are absolute");
}

void quantizeSnapsStartsKeepsDurations()
{
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Chord1);
    rec.setQuantizeDivision(16);   // grid = 960*4/16 = 240 ticks

    rec.noteOn(60, 100, 100);   rec.noteOff(60, 600);    // -> snaps to 0
    rec.noteOn(64, 100, 1390);  rec.noteOff(64, 1800);   // -> snaps to 1440
    rec.commitTake();

    auto style = rec.snapshotStyle();
    const auto& notes = style->sections[0].parts[0].notes;
    expect(notes[0].tick == 0, "100 snaps to 0 on a 240 grid");
    expect(notes[0].duration == 500, "duration is not quantized");
    expect(notes[1].tick == 1440, "1390 snaps to 1440");
}

void loopWrapGivesWrapAwareDuration()
{
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Pad);
    rec.setQuantizeDivision(0);

    // Held across the loop seam: on at tick 7600, off at (absolute) 7780 of the
    // NEXT pass = 100 in-section.
    rec.noteOn(67, 100, 7600);
    rec.noteOff(67, 7680 + 100);
    rec.commitTake();

    auto style = rec.snapshotStyle();
    const auto& note = style->sections[0].parts[0].notes[0];
    expect(note.tick == 7600, "wrap note keeps its start");
    expect(note.duration == 180, "wrap duration spans the seam");
}

void overdubMergesAndClearRemoves()
{
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Drums);

    rec.noteOn(36, 110, 0);  rec.noteOff(36, 100);
    rec.commitTake();
    rec.noteOn(38, 100, 960); rec.noteOff(38, 1060);   // second pass overdub
    rec.commitTake();

    auto style = rec.snapshotStyle();
    expect(style->sections[0].parts[0].notes.size() == 2, "overdub merges takes");
    expect(rec.targetPartHasNotes(), "target part reports notes");

    expect(rec.clearTargetPart(), "clear reports it had data");
    style = rec.snapshotStyle();
    expect(!style->sections[0].parts.empty(), "part slot preserved after clear");
    expect(style->sections[0].parts[0].notes.empty(), "notes cleared from part");
    expect(!rec.targetPartHasNotes(), "no notes after clear");
}

void savedStyleRoundTrips()
{
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Bass);
    rec.noteOn(36, 100, 0); rec.noteOff(36, 900);
    rec.commitTake();

    const std::string path = "test-recorded-style.cstyle";
    expect(rec.save(path), "save writes the file");

    auto loaded = loadStyleFromFile(path);
    expect(loaded.ok, "saved style loads back");
    expect(loaded.style.name == "Test Groove", "name round-trips");
    expect(loaded.style.sections.size() == 1
               && loaded.style.sections[0].parts.size() == 1,
           "structure round-trips");
    const auto& bass = loaded.style.sections[0].parts[0];
    expect(bass.notes.size() == 1 && bass.notes[0].role == NoteRole::ChordRoot,
           "roles round-trip");
    expect(bass.yamahaPolicy && bass.yamahaPolicy->bassOn, "policy round-trips");
    std::remove(path.c_str());
}

void savedCadenzaStyleReloadsAsEditableSession()
{
    StyleRecorder original;
    original.startSession(defaultConfig());
    original.setTargetPart(RecorderPart::Bass);
    original.noteOn(36, 100, 0);
    original.noteOff(36, 480);
    original.commitTake();

    const std::string path = "test-editable-recorded-style.cstyle";
    expect(original.save(path), "editable style fixture saves");
    const auto loaded = loadStyleFromFile(path);
    expect(loaded.ok && isEditableCadenzaStyle(loaded.style),
           "native cstyle is recognized as recorder-editable");

    StyleRecorder reopened;
    expect(reopened.loadSession(loaded.style),
           "saved cstyle rehydrates an editable recorder session");
    expect(reopened.sessionActive(),
           "reloaded editable style enables recorder controls");
    expect(reopened.targetPart() == RecorderPart::Bass
               && reopened.targetPartNotes().size() == 1,
           "reloaded editable style can reopen its part in the editor");
    std::remove(path.c_str());
}

void importedYamahaStyleIsNotRecorderEditable()
{
    Style imported;
    imported.schema = "cadenza.style.v1";
    imported.yamahaFormat = YamahaStyleFormat::SFF2;
    imported.sections.push_back(Section { "mainA", 4, {} });

    StyleRecorder recorder;
    expect(!isEditableCadenzaStyle(imported),
           "Yamaha-derived cstyle remains read-only in Style Recorder");
    expect(!recorder.loadSession(imported) && !recorder.sessionActive(),
           "read-only imported style does not activate recorder controls");
}

void makeStyleEditableUnlocksYamahaStyle()
{
    // A synthetic imported Yamaha style: two drum channels (9 sub + 10 main) and
    // seven melodic channels (2..8) across two sections. After makeStyleEditable
    // the drums merge onto ch10, melodic parts map to ch11..16, and the 7th
    // melodic channel is dropped (only 6 melodic slots exist).
    auto buildSection = [](const std::string& name) {
        Section sec;
        sec.name = name;
        sec.barCount = 2;
        auto addPart = [&](const std::string& pn, int ch, bool perc, int pitch) {
            Part p;
            p.name = pn;
            p.midiChannel = ch;
            p.percussion = perc;
            p.instrument = pn;
            PatternNote n;
            n.tick = 0; n.duration = 240; n.pitch = pitch; n.velocity = 100;
            n.role = perc ? NoteRole::Absolute : NoteRole::ChordRoot;
            p.notes.push_back(n);
            sec.parts.push_back(std::move(p));
        };
        addPart("rhythm-sub",  9, true, 36);
        addPart("rhythm-main", 10, true, 38);
        addPart("bass",        2, false, 36);
        addPart("chord1",      3, false, 60);
        addPart("chord2",      4, false, 64);
        addPart("pad",         5, false, 67);
        addPart("phrase1",     6, false, 72);
        addPart("phrase2",     7, false, 74);
        addPart("extra",       8, false, 76);   // 7th melodic source -> dropped
        return sec;
    };

    Style imported;
    imported.schema = "cadenza.style.v1";
    imported.yamahaFormat = YamahaStyleFormat::SFF2;
    imported.name = "Yamaha Test";
    imported.sections.push_back(buildSection("mainA"));
    imported.sections.push_back(buildSection("mainB"));

    expect(!isEditableCadenzaStyle(imported), "starts read-only (Yamaha-flagged)");

    std::vector<std::string> dropped;
    makeStyleEditable(imported, &dropped);

    expect(isEditableCadenzaStyle(imported), "becomes recorder-editable after convert");
    expect(imported.yamahaFormat == YamahaStyleFormat::Unknown, "Yamaha flag is cleared");
    expect(dropped.size() == 1, "one extra melodic part dropped (7 melodic, 6 slots)");

    for (const auto& sec : imported.sections) {
        expect(sec.parts.size() == 7, "section normalized to 7 parts (drums + 6 melodic)");
        const Part* drums = nullptr;
        bool channelsOk = true;
        for (const auto& p : sec.parts) {
            if (p.midiChannel < 10 || p.midiChannel > 16) channelsOk = false;
            if (p.midiChannel == 10) drums = &p;
        }
        expect(channelsOk, "all parts mapped onto channels 10..16");
        expect(drums != nullptr && drums->percussion, "drums land on ch10, percussive");
        expect(drums != nullptr && drums->notes.size() == 2,
               "the two drum channels merge into one part");
    }

    StyleRecorder rec;
    expect(rec.loadSession(imported, "mainA") && rec.sessionActive(),
           "converted style opens an editable recorder session");

    const std::string path = "test-made-editable.cstyle";
    expect(rec.save(path), "converted style saves as .cstyle");
    const auto loaded = loadStyleFromFile(path);
    expect(loaded.ok && isEditableCadenzaStyle(loaded.style)
               && loaded.style.sections.size() == 2,
           "saved converted style reloads as editable, both sections intact");
    std::remove(path.c_str());
}

void setEditSectionSwitchesSectionAndBars()
{
    // Two sections with different bar counts and a distinguishing bass note each.
    auto makeSec = [](const std::string& name, int bars, int bassTick) {
        Section s; s.name = name; s.barCount = bars;
        Part bass; bass.name = "bass"; bass.midiChannel = 11; bass.percussion = false;
        bass.notes.push_back(PatternNote{ bassTick, 240, 36, 100, NoteRole::ChordRoot, 0 });
        s.parts.push_back(std::move(bass));
        return s;
    };

    Style style;
    style.schema = "cadenza.style.v1";
    style.name = "Switcher";
    style.sections.push_back(makeSec("mainA", 2, 0));
    style.sections.push_back(makeSec("mainB", 4, 960));

    StyleRecorder rec;
    expect(rec.loadSession(style, "mainA"), "loads editable multi-section style");
    rec.setTargetPart(RecorderPart::Bass);
    expect(rec.config().bars == 2, "mainA is 2 bars");
    expect(rec.targetPartNotes().size() == 1 && rec.targetPartNotes()[0].tick == 0,
           "mainA bass note sits at tick 0");

    expect(rec.setEditSection("mainB"), "switches to mainB");
    expect(rec.config().bars == 4, "mainB bar count is applied");
    expect(rec.sectionLengthTicks() == 4 * 3840, "section length follows the new bar count");
    expect(rec.targetPartNotes().size() == 1 && rec.targetPartNotes()[0].tick == 960,
           "now editing mainB's own content (note at tick 960)");

    expect(!rec.setEditSection("does-not-exist"), "unknown section is rejected");
}

void barLengthChangesResizeSectionAndNotes()
{
    StyleRecorder recorder;
    auto cfg = defaultConfig();
    cfg.bars = 4;
    recorder.startSession(cfg);
    recorder.setQuantizeDivision(0);
    recorder.setTargetPart(RecorderPart::Bass);
    recorder.replacePartNotes({
        PatternNote { 100, 200, 36, 90 },
        PatternNote { 3800, 200, 40, 90 },
        PatternNote { 4000, 200, 43, 90 },
    });

    expect(recorder.setBarCount(1), "one-bar selection updates active session");
    expect(recorder.config().bars == 1
               && recorder.snapshotStyle()->sections[0].barCount == 1
               && recorder.sectionLengthTicks() == 3840,
           "one-bar selection creates a one-bar part");
    auto notes = recorder.targetPartNotes();
    expect(notes.size() == 2, "notes starting after shorter end are removed");
    expect(notes[1].tick == 3800 && notes[1].duration == 40,
           "note crossing shorter end is trimmed");

    expect(recorder.setBarCount(2), "two-bar selection updates active session");
    expect(recorder.config().bars == 2
               && recorder.snapshotStyle()->sections[0].barCount == 2
               && recorder.sectionLengthTicks() == 7680,
           "two-bar selection creates a two-bar part");

    expect(recorder.setBarCount(4), "four-bar selection updates active session");
    expect(recorder.config().bars == 4
               && recorder.snapshotStyle()->sections[0].barCount == 4
               && recorder.sectionLengthTicks() == 15360,
           "four-bar selection creates a four-bar part");
}

void changedBarLengthSurvivesSaveReload()
{
    StyleRecorder recorder;
    recorder.startSession(defaultConfig());
    recorder.setBarCount(1);

    const std::string path = "test-one-bar-recorded-style.cstyle";
    expect(recorder.save(path), "resized style saves");
    const auto loaded = loadStyleFromFile(path);
    expect(loaded.ok && loaded.style.sections.size() == 1
               && loaded.style.sections[0].barCount == 1,
           "save and reload preserve selected bar count");

    StyleRecorder reopened;
    expect(reopened.loadSession(loaded.style)
               && reopened.config().bars == 1
               && reopened.sectionLengthTicks() == 3840,
           "reloaded recorder restores selected bar count");
    std::remove(path.c_str());
}

void replacePartNotesRebakesRoles()
{
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Bass);
    rec.noteOn(36, 100, 0); rec.noteOff(36, 900);   // C2 root
    rec.commitTake();

    // Piano-roll edit: move the C to an E (3rd) and add a B (7th).
    auto notes = rec.targetPartNotes();
    expect(notes.size() == 1, "one note before the edit");
    notes[0].pitch = 40;                            // E2
    PatternNote added;
    added.tick = 1920; added.duration = 480; added.pitch = 47; added.velocity = 90;
    notes.push_back(added);
    rec.replacePartNotes(notes);

    auto style = rec.snapshotStyle();
    const auto& bass = style->sections[0].parts[0];
    expect(bass.notes.size() == 2, "edited part has two notes");
    expect(bass.notes[0].role == NoteRole::Chord3, "moved note re-bakes as 3rd");
    expect(bass.notes[1].role == NoteRole::Chord7, "added note bakes as 7th");

    // Replacing with nothing clears the notes but preserves the part slot + metadata.
    rec.replacePartNotes({});
    style = rec.snapshotStyle();
    expect(!style->sections[0].parts.empty(), "part slot preserved on empty edit");
    expect(style->sections[0].parts[0].notes.empty(), "empty edit clears notes");
}

// --- loop recording: wraparound + merge / dedup / overlap rules ---

void loopRecordingWrapsAroundSection()
{
    // Section is 7680 ticks (2 bars). A hit played during the SECOND loop pass
    // folds into the section instead of landing past its end.
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Drums);

    rec.noteOn(38, 100, 7680 + 250);    // 250 into the next pass
    rec.noteOff(38, 7680 + 400);
    rec.commitTake();

    auto style = rec.snapshotStyle();
    const auto& notes = style->sections[0].parts[0].notes;
    expect(notes.size() == 1, "wrapped hit recorded as one note");
    expect(notes[0].tick >= 0 && notes[0].tick < 7680, "wrapped tick folds into the section");
    expect(notes[0].tick == 240, "250 in-section snaps to the 1/16 grid (240)");
}

void drumKickRecordedTwiceDoesNotDuplicate()
{
    // A kick on the same grid cell across two passes updates in place (newest
    // velocity wins) rather than stacking a duplicate.
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Drums);

    rec.noteOn(36, 100, 10); rec.noteOff(36, 120);     // pass 1: snaps to 0
    rec.commitTake();
    rec.noteOn(36, 70, 7680 + 15); rec.noteOff(36, 7680 + 120);  // pass 2: also snaps to 0
    rec.commitTake();

    auto style = rec.snapshotStyle();
    const auto& notes = style->sections[0].parts[0].notes;
    expect(notes.size() == 1, "same kick on the same grid cell is not duplicated");
    expect(notes[0].tick == 0, "kick stays on grid position 0");
    expect(notes[0].velocity == 70, "velocity updates from the newest hit");
}

void drumKickThenHatCreatesTwoNotes()
{
    // Different pitches on the same grid cell are two distinct hits.
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Drums);

    rec.noteOn(36, 100, 0);  rec.noteOff(36, 100);     // kick
    rec.noteOn(42, 80, 5);   rec.noteOff(42, 90);      // hat, same cell
    rec.commitTake();

    auto style = rec.snapshotStyle();
    const auto& notes = style->sections[0].parts[0].notes;
    expect(notes.size() == 2, "kick + hi-hat on the same cell make two notes");
    const bool haveKick = notes[0].pitch == 36 || notes[1].pitch == 36;
    const bool haveHat  = notes[0].pitch == 42 || notes[1].pitch == 42;
    expect(haveKick && haveHat, "both the kick and the hat are kept");
}

void melodicOverlapSamePitchIsCleaned()
{
    // Two same-pitch melodic notes that overlap: the earlier note is trimmed to
    // end where the new one starts, so they never sound on top of each other.
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Bass);

    rec.noteOn(36, 100, 0);   rec.noteOff(36, 2000);   // long C2 (0..2000)
    rec.noteOn(36, 90, 500);  rec.noteOff(36, 900);    // C2 again, snaps to 480
    rec.commitTake();

    auto style = rec.snapshotStyle();
    const auto& notes = style->sections[0].parts[0].notes;
    expect(notes.size() == 2, "overlapping same-pitch notes are kept as two");
    expect(notes[0].pitch == 36 && notes[1].pitch == 36, "both notes are the same pitch");
    expect(notes[0].duration == 480, "earlier note trimmed to where the new one begins");
    expect(notes[0].tick + notes[0].duration <= notes[1].tick, "no remaining overlap");
}

void quantizeSnapsRecordedTicks()
{
    // 1/16 grid at 960 PPQ = 240 ticks per cell.
    StyleRecorder rec;
    rec.startSession(defaultConfig());
    rec.setTargetPart(RecorderPart::Chord1);
    rec.setQuantizeDivision(16);

    rec.noteOn(60, 100, 100);  rec.noteOff(60, 600);    // 100 -> 0
    rec.noteOn(64, 100, 1390); rec.noteOff(64, 1700);   // 1390 -> 1440
    rec.commitTake();

    auto style = rec.snapshotStyle();
    const auto& notes = style->sections[0].parts[0].notes;
    expect(notes[0].tick == 0, "100 snaps to 0 on the 240 grid");
    expect(notes[1].tick == 1440, "1390 snaps to 1440 on the 240 grid");
    expect(notes[0].duration == 500, "note length is preserved, not quantized");
}

void partInfoTableIsConsistent()
{
    for (int i = 0; i < kNumRecorderParts; ++i) {
        const auto& info = recorderPartInfo(i);
        expect(info.midiChannel >= 10 && info.midiChannel <= 16,
               "part channels are the SFF range");
        expect(&recorderPartInfo(info.part) == &info, "enum lookup matches index lookup");
    }
}
}

int main()
{
    sessionBuildsPlayableSkeleton();
    bassTakeBakesRolesAndPolicy();
    drumsAreAbsoluteAndPercussive();
    quantizeSnapsStartsKeepsDurations();
    loopWrapGivesWrapAwareDuration();
    overdubMergesAndClearRemoves();
    savedStyleRoundTrips();
    savedCadenzaStyleReloadsAsEditableSession();
    importedYamahaStyleIsNotRecorderEditable();
    makeStyleEditableUnlocksYamahaStyle();
    setEditSectionSwitchesSectionAndBars();
    barLengthChangesResizeSectionAndNotes();
    changedBarLengthSurvivesSaveReload();
    replacePartNotesRebakesRoles();
    loopRecordingWrapsAroundSection();
    drumKickRecordedTwiceDoesNotDuplicate();
    drumKickThenHatCreatesTwoNotes();
    melodicOverlapSamePitchIsCleaned();
    quantizeSnapsRecordedTicks();
    partInfoTableIsConsistent();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All StyleRecorder tests passed\n";
    return EXIT_SUCCESS;
}
