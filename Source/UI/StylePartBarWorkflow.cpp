#include "StylePartBarWorkflow.h"

#include <algorithm>

namespace cadenza::ui::bar_workflow
{
namespace
{
int clampBar(int bar, int totalBars) noexcept
{
    return std::clamp(bar, 0, std::max(0, totalBars - 1));
}

void sortNotes(std::vector<cadenza::arranger::PatternNote>& notes)
{
    std::stable_sort(notes.begin(), notes.end(),
                     [](const auto& a, const auto& b) {
                         if (a.tick != b.tick)
                             return a.tick < b.tick;
                         return a.pitch < b.pitch;
                     });
}
}

void clearSelection(BarSelection& selection) noexcept
{
    selection = {};
}

void selectBar(BarSelection& selection, int bar, int totalBars) noexcept
{
    const int selected = clampBar(bar, totalBars);
    selection.anchor = selected;
    selection.first = selected;
    selection.last = selected;
}

void extendSelection(BarSelection& selection, int bar, int totalBars) noexcept
{
    if (selection.anchor < 0) {
        selectBar(selection, bar, totalBars);
        return;
    }
    const int selected = clampBar(bar, totalBars);
    selection.first = std::min(selection.anchor, selected);
    selection.last = std::max(selection.anchor, selected);
}

void dragSelection(BarSelection& selection, int bar, int totalBars) noexcept
{
    extendSelection(selection, bar, totalBars);
}

BarClipboard copyBars(const std::vector<cadenza::arranger::PatternNote>& notes,
                      const BarSelection& selection, int barTicks)
{
    BarClipboard clipboard;
    if (!selection.valid() || barTicks <= 0)
        return clipboard;

    clipboard.lengthBars = selection.length();
    const int start = selection.first * barTicks;
    const int end = (selection.last + 1) * barTicks;
    for (auto note : notes) {
        if (note.tick >= start && note.tick < end) {
            note.tick -= start;
            clipboard.notes.push_back(note);
        }
    }
    sortNotes(clipboard.notes);
    return clipboard;
}

std::vector<cadenza::arranger::PatternNote> pasteBars(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const BarClipboard& clipboard, int destinationBar,
    int totalBars, int barTicks)
{
    if (clipboard.empty() || totalBars <= 0 || barTicks <= 0)
        return notes;

    const int destination = clampBar(destinationBar, totalBars);
    const int destinationBars = std::min(clipboard.lengthBars, totalBars - destination);
    const int destinationStart = destination * barTicks;
    const int destinationEnd = destinationStart + destinationBars * barTicks;
    const int patternEnd = totalBars * barTicks;

    std::vector<cadenza::arranger::PatternNote> result;
    result.reserve(notes.size() + clipboard.notes.size());
    for (const auto& note : notes) {
        if (note.tick < destinationStart || note.tick >= destinationEnd)
            result.push_back(note);
    }

    const int availableTicks = destinationBars * barTicks;
    for (auto note : clipboard.notes) {
        if (note.tick < 0 || note.tick >= availableTicks)
            continue;
        note.tick += destinationStart;
        note.duration = std::clamp(note.duration, 1, patternEnd - note.tick);
        result.push_back(note);
    }
    sortNotes(result);
    return result;
}

std::vector<cadenza::arranger::PatternNote> duplicateBars(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const BarSelection& selection, int totalBars, int barTicks)
{
    if (!selection.valid() || selection.last + 1 >= totalBars)
        return notes;
    return pasteBars(notes, copyBars(notes, selection, barTicks),
                     selection.last + 1, totalBars, barTicks);
}

std::vector<cadenza::arranger::PatternNote> clearBars(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const BarSelection& selection, int barTicks)
{
    if (!selection.valid() || barTicks <= 0)
        return notes;
    const int start = selection.first * barTicks;
    const int end = (selection.last + 1) * barTicks;
    std::vector<cadenza::arranger::PatternNote> result;
    result.reserve(notes.size());
    for (const auto& note : notes)
        if (note.tick < start || note.tick >= end)
            result.push_back(note);
    return result;
}

MoveResult moveBars(const std::vector<cadenza::arranger::PatternNote>& notes,
                    const BarSelection& selection, int deltaBars,
                    int totalBars, int barTicks)
{
    MoveResult result { notes, selection };
    if (!selection.valid() || totalBars <= 0 || barTicks <= 0)
        return result;

    const int clampedDelta = std::clamp(
        deltaBars, -selection.first, totalBars - 1 - selection.last);
    if (clampedDelta == 0)
        return result;

    const int start = selection.first * barTicks;
    const int end = (selection.last + 1) * barTicks;
    const int tickDelta = clampedDelta * barTicks;
    const int patternEnd = totalBars * barTicks;
    for (auto& note : result.notes) {
        if (note.tick >= start && note.tick < end) {
            note.tick += tickDelta;
            note.duration = std::clamp(note.duration, 1, patternEnd - note.tick);
        }
    }
    sortNotes(result.notes);
    result.selection.anchor += clampedDelta;
    result.selection.first += clampedDelta;
    result.selection.last += clampedDelta;
    return result;
}

int resolvePasteBar(const BarSelection& selection, int playheadTick,
                    int totalBars, int barTicks) noexcept
{
    if (selection.valid())
        return clampBar(selection.first, totalBars);
    if (playheadTick >= 0 && barTicks > 0)
        return clampBar(playheadTick / barTicks, totalBars);
    return 0;
}

EditorCommand commandForShortcut(ShortcutKey key, bool controlDown) noexcept
{
    if (controlDown) {
        if (key == ShortcutKey::C) return EditorCommand::Copy;
        if (key == ShortcutKey::V) return EditorCommand::Paste;
        if (key == ShortcutKey::D) return EditorCommand::Duplicate;
        return EditorCommand::None;
    }

    switch (key) {
        case ShortcutKey::Space:     return EditorCommand::TogglePlay;
        case ShortcutKey::R:         return EditorCommand::ToggleRecord;
        case ShortcutKey::DeleteKey: return EditorCommand::Clear;
        case ShortcutKey::Escape:    return EditorCommand::ClearSelection;
        default:                     return EditorCommand::None;
    }
}
}
