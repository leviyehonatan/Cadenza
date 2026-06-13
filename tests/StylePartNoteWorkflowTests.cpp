#include "UI/StylePartNoteWorkflow.h"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

using cadenza::arranger::PatternNote;
using namespace cadenza::ui::note_workflow;

PatternNote note(int tick, int pitch, int duration = 240, int velocity = 100)
{
    PatternNote n;
    n.tick = tick;
    n.pitch = pitch;
    n.duration = duration;
    n.velocity = velocity;
    return n;
}

void selectionClickAndToggle()
{
    NoteSelection selection;
    selectOnly(selection, 2);
    expect(selection == NoteSelection { 2 }, "left click selects one note");
    toggle(selection, 4);
    expect(selection == NoteSelection({ 2, 4 }), "Ctrl-click adds a note");
    toggle(selection, 2);
    expect(selection == NoteSelection { 4 }, "Ctrl-click removes a selected note");
}

void rectangleSelectsIntersectingNotes()
{
    const std::vector<NoteBounds> bounds {
        { 0, 0.0f, 0.0f, 20.0f, 10.0f },
        { 1, 30.0f, 20.0f, 20.0f, 10.0f },
        { 2, 70.0f, 70.0f, 20.0f, 10.0f },
    };
    const auto selection = selectIntersecting(bounds, { 10.0f, 5.0f, 45.0f, 30.0f });
    expect(selection == NoteSelection({ 0, 1 }),
           "selection rectangle includes all intersecting notes");
}

void groupMoveClampsAsOneUnit()
{
    const std::vector<PatternNote> notes {
        note(0, 36),
        note(480, 40),
        note(960, 44),
    };
    const NoteSelection selection { 0, 1 };
    const auto moved = moveSelected(notes, selection, -1000, -50, 1920);
    expect(moved[0].tick == 0 && moved[1].tick == 480,
           "group move clamps at pattern start without changing spacing");
    expect(moved[0].pitch == 0 && moved[1].pitch == 4,
           "group pitch movement clamps at MIDI zero");
}

void groupResizeClampsDurations()
{
    const std::vector<PatternNote> notes {
        note(0, 60, 480),
        note(1600, 64, 500),
    };
    const NoteSelection selection { 0, 1 };
    const auto resized = resizeSelected(notes, selection, 500, 240, 1920);
    expect(resized[0].duration == 980, "group resize applies the same duration delta");
    expect(resized[1].duration == 320, "resize clamps at the pattern end");

    const auto minimum = resizeSelected(notes, selection, -1000, 240, 1920);
    expect(minimum[0].duration == 240 && minimum[1].duration == 240,
           "resize clamps every selected note to one grid cell");
}

void duplicatePreparationCopiesSelection()
{
    const std::vector<PatternNote> notes {
        note(0, 60),
        note(480, 64),
        note(960, 67),
    };
    const auto duplicated = duplicateSelected(notes, NoteSelection { 0, 2 });
    expect(duplicated.notes.size() == 5, "duplicate adds copies of selected notes");
    expect(duplicated.selection == NoteSelection({ 3, 4 }),
           "newly duplicated notes become the selection");
    expect(duplicated.notes[3].tick == 0 && duplicated.notes[4].tick == 960,
           "duplicates initially match the originals");
}

void clipboardPreservesRelativeTimingAndPitch()
{
    const std::vector<PatternNote> notes {
        note(240, 64, 300, 91),
        note(720, 67, 480, 82),
        note(1200, 72),
    };
    const auto clipboard = copySelected(notes, NoteSelection { 0, 1 });
    expect(clipboard.baseTick == 240 && clipboard.basePitch == 64,
           "clipboard records selection origin");
    expect(clipboard.notes[0].tick == 0 && clipboard.notes[1].tick == 480,
           "clipboard stores relative timing");
    expect(clipboard.notes[0].pitch == 0 && clipboard.notes[1].pitch == 3,
           "clipboard stores relative pitch");
    expect(clipboard.spanTicks == 960, "clipboard stores selected time span");
}

void pasteUsesCursorOrPlayheadAndClamps()
{
    NoteClipboard clipboard;
    clipboard.basePitch = 60;
    clipboard.spanTicks = 720;
    clipboard.notes = { note(0, 0, 480), note(480, 4, 480) };

    const auto atCursor = pasteNotes({}, clipboard, 1000, 70, 1920);
    expect(atCursor.notes[0].tick == 1000 && atCursor.notes[0].pitch == 70,
           "paste starts at the cursor tick and pitch");
    expect(atCursor.notes[1].tick == 1480 && atCursor.notes[1].pitch == 74,
           "paste preserves relative note offsets");
    expect(atCursor.notes[1].duration == 440, "paste trims at pattern end");

    const auto atPlayhead = pasteNotes({}, clipboard, 240, -1, 1920);
    expect(atPlayhead.notes[0].tick == 240 && atPlayhead.notes[0].pitch == 60,
           "paste falls back to playhead and original base pitch");
}

void duplicateRightAndDelete()
{
    const std::vector<PatternNote> notes {
        note(0, 60, 240),
        note(480, 64, 240),
        note(1200, 67, 240),
    };
    const auto duplicated = duplicateToRight(notes, NoteSelection { 0, 1 }, 1920);
    expect(duplicated.notes.size() == 5, "duplicate-right creates selected copies");
    expect(duplicated.notes[3].tick == 720 && duplicated.notes[4].tick == 1200,
           "duplicate-right offsets by selection span");

    const auto deleted = deleteSelected(notes, NoteSelection { 0, 2 });
    expect(deleted.size() == 1 && deleted[0].pitch == 64,
           "delete removes only selected notes");
}
}

int main()
{
    selectionClickAndToggle();
    rectangleSelectsIntersectingNotes();
    groupMoveClampsAsOneUnit();
    groupResizeClampsDurations();
    duplicatePreparationCopiesSelection();
    clipboardPreservesRelativeTimingAndPitch();
    pasteUsesCursorOrPlayheadAndClamps();
    duplicateRightAndDelete();

    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "All note workflow tests passed\n";
    return EXIT_SUCCESS;
}
