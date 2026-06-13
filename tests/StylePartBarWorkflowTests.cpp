#include "UI/StylePartBarWorkflow.h"

#include <cstdlib>
#include <iostream>
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
using namespace cadenza::ui::bar_workflow;

PatternNote note(int tick, int pitch, int duration = 240, int velocity = 100)
{
    PatternNote n;
    n.tick = tick;
    n.pitch = pitch;
    n.duration = duration;
    n.velocity = velocity;
    return n;
}

void clickAndShiftSelectionUsesAnchor()
{
    BarSelection selection;
    selectBar(selection, 2, 8);
    expect(selection.anchor == 2 && selection.first == 2 && selection.last == 2,
           "click selects one bar and sets the anchor");

    extendSelection(selection, 5, 8);
    expect(selection.first == 2 && selection.last == 5,
           "shift-click selects forward from the anchor");

    extendSelection(selection, 0, 8);
    expect(selection.first == 0 && selection.last == 2,
           "shift-click selects backward from the anchor");

    BarSelection noAnchor;
    extendSelection(noAnchor, 3, 8);
    expect(noAnchor.anchor == 3 && noAnchor.first == 3 && noAnchor.last == 3,
           "shift-click without anchor behaves as a normal click");
}

void mouseDragSelectsContiguousBars()
{
    BarSelection selection;
    selectBar(selection, 1, 8);
    dragSelection(selection, 4, 8);
    expect(selection.anchor == 1 && selection.first == 1 && selection.last == 4,
           "mouse drag selects every bar between start and current bar");
}

void copyStoresRelativeTiming()
{
    const int barTicks = 3840;
    const std::vector<PatternNote> notes {
        note(0, 36),
        note(barTicks + 120, 38, 300, 91),
        note(2 * barTicks + 240, 42),
    };
    BarSelection selection { 1, 1, 2 };
    const auto clipboard = copyBars(notes, selection, barTicks);

    expect(clipboard.lengthBars == 2 && clipboard.notes.size() == 2,
           "copy captures only notes starting in selected bars");
    expect(clipboard.notes[0].tick == 120 && clipboard.notes[1].tick == barTicks + 240,
           "copy stores ticks relative to the selected range");
    expect(clipboard.notes[0].pitch == 38 && clipboard.notes[0].velocity == 91,
           "copy preserves note properties");
}

void pasteReplacesAndClamps()
{
    const int barTicks = 3840;
    const int totalBars = 4;
    std::vector<PatternNote> notes {
        note(0, 35),
        note(barTicks + 100, 40),
        note(2 * barTicks + 200, 41),
        note(3 * barTicks + 300, 43),
    };
    BarClipboard clipboard;
    clipboard.lengthBars = 2;
    clipboard.notes = { note(120, 60, 480, 77), note(barTicks + 240, 64, 500, 88) };

    auto pasted = pasteBars(notes, clipboard, 1, totalBars, barTicks);
    expect(pasted.size() == 4, "paste replaces destination notes before inserting");
    expect(pasted[1].tick == barTicks + 120 && pasted[1].pitch == 60,
           "paste preserves first relative note timing");
    expect(pasted[2].tick == 2 * barTicks + 240 && pasted[2].pitch == 64,
           "paste preserves later relative note timing");

    auto clamped = pasteBars(notes, clipboard, 3, totalBars, barTicks);
    bool foundFirst = false;
    bool foundOverflow = false;
    for (const auto& n : clamped) {
        foundFirst = foundFirst || (n.tick == 3 * barTicks + 120 && n.pitch == 60);
        foundOverflow = foundOverflow || n.pitch == 64;
        expect(n.tick + n.duration <= totalBars * barTicks,
               "pasted note remains inside pattern length");
    }
    expect(foundFirst && !foundOverflow,
           "paste at pattern end inserts only notes that fit the remaining range");
}

void pasteFallbackUsesPlayheadOrFirstBar()
{
    BarSelection none;
    expect(resolvePasteBar(none, 2 * 3840 + 100, 4, 3840) == 2,
           "paste without selection uses playhead bar");
    expect(resolvePasteBar(none, -1, 4, 3840) == 0,
           "paste without playhead falls back to bar one");

    BarSelection selected { 3, 3, 3 };
    expect(resolvePasteBar(selected, 0, 4, 3840) == 3,
           "selected first bar overrides playhead fallback");
}

void duplicateAndClearSelectedBars()
{
    const int barTicks = 3840;
    std::vector<PatternNote> notes {
        note(100, 36),
        note(barTicks + 200, 38),
        note(2 * barTicks + 300, 42),
    };
    BarSelection selection { 0, 0, 1 };

    const auto duplicated = duplicateBars(notes, selection, 4, barTicks);
    bool haveBar3Copy = false;
    bool haveBar4Copy = false;
    for (const auto& n : duplicated) {
        haveBar3Copy = haveBar3Copy || (n.tick == 2 * barTicks + 100 && n.pitch == 36);
        haveBar4Copy = haveBar4Copy || (n.tick == 3 * barTicks + 200 && n.pitch == 38);
    }
    expect(haveBar3Copy && haveBar4Copy, "duplicate places the selected range to its right");

    const auto cleared = clearBars(notes, selection, barTicks);
    expect(cleared.size() == 1 && cleared[0].pitch == 42,
           "clear removes only notes starting in selected bars");
}

void movingBarsClampsInsidePattern()
{
    const int barTicks = 3840;
    std::vector<PatternNote> notes {
        note(barTicks + 100, 36),
        note(2 * barTicks + 200, 38),
        note(3 * barTicks + 300, 42),
    };
    BarSelection selection { 1, 1, 2 };

    const auto movedRight = moveBars(notes, selection, 10, 4, barTicks);
    expect(movedRight.selection.first == 2 && movedRight.selection.last == 3,
           "move clamps selected range at pattern end");
    expect(movedRight.notes[0].tick == 2 * barTicks + 100
               && movedRight.notes[1].tick == 3 * barTicks + 200,
           "clamped move preserves note offsets");

    const auto movedLeft = moveBars(notes, selection, -10, 4, barTicks);
    expect(movedLeft.selection.first == 0 && movedLeft.selection.last == 1,
           "move clamps selected range at pattern start");
}

void shortcutsMapToCommands()
{
    expect(commandForShortcut(ShortcutKey::Space, false) == EditorCommand::TogglePlay,
           "Space maps to Play/Stop");
    expect(commandForShortcut(ShortcutKey::R, false) == EditorCommand::ToggleRecord,
           "R maps to Record");
    expect(commandForShortcut(ShortcutKey::C, true) == EditorCommand::Copy,
           "Ctrl+C maps to Copy");
    expect(commandForShortcut(ShortcutKey::V, true) == EditorCommand::Paste,
           "Ctrl+V maps to Paste");
    expect(commandForShortcut(ShortcutKey::D, true) == EditorCommand::Duplicate,
           "Ctrl+D maps to Duplicate");
    expect(commandForShortcut(ShortcutKey::DeleteKey, false) == EditorCommand::Clear,
           "Delete maps to Clear");
    expect(commandForShortcut(ShortcutKey::Escape, false) == EditorCommand::ClearSelection,
           "Escape maps to clear selection");
}
}

int main()
{
    clickAndShiftSelectionUsesAnchor();
    mouseDragSelectsContiguousBars();
    copyStoresRelativeTiming();
    pasteReplacesAndClamps();
    pasteFallbackUsesPlayheadOrFirstBar();
    duplicateAndClearSelectedBars();
    movingBarsClampsInsidePattern();
    shortcutsMapToCommands();

    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "All Part Editor workflow tests passed\n";
    return EXIT_SUCCESS;
}
