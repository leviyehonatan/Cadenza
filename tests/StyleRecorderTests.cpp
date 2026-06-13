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

    expect(rec.clearTargetPart(), "clear removes the part");
    style = rec.snapshotStyle();
    expect(style->sections[0].parts.empty(), "part gone after clear");
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

    // Replacing with nothing removes the part.
    rec.replacePartNotes({});
    style = rec.snapshotStyle();
    expect(style->sections[0].parts.empty(), "empty edit removes the part");
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
