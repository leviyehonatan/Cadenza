#include "StylePartNoteWorkflow.h"

#include <algorithm>
#include <limits>

namespace cadenza::ui::note_workflow
{
namespace
{
bool intersects(const Rectangle& a, const NoteBounds& b) noexcept
{
    return a.x < b.x + b.width && b.x < a.x + a.width
        && a.y < b.y + b.height && b.y < a.y + a.height;
}

bool validIndex(int index, int size) noexcept
{
    return index >= 0 && index < size;
}
}

void selectOnly(NoteSelection& selection, int index)
{
    selection.clear();
    if (index >= 0)
        selection.insert(index);
}

void toggle(NoteSelection& selection, int index)
{
    if (index < 0)
        return;
    const auto it = selection.find(index);
    if (it == selection.end())
        selection.insert(index);
    else
        selection.erase(it);
}

NoteSelection selectIntersecting(const std::vector<NoteBounds>& bounds,
                                 Rectangle selectionRectangle)
{
    NoteSelection selection;
    for (const auto& boundsForNote : bounds)
        if (intersects(selectionRectangle, boundsForNote))
            selection.insert(boundsForNote.index);
    return selection;
}

std::vector<int> selectedIndices(const NoteSelection& selection, int noteCount)
{
    std::vector<int> indices;
    indices.reserve(selection.size());
    for (const int index : selection)
        if (validIndex(index, noteCount))
            indices.push_back(index);
    return indices;
}

std::vector<cadenza::arranger::PatternNote> moveSelected(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection, int tickDelta, int pitchDelta,
    int sectionTicks)
{
    auto result = notes;
    if (selection.empty() || sectionTicks <= 0)
        return result;

    int minimumTick = sectionTicks;
    int maximumEnd = 0;
    int minimumPitch = 127;
    int maximumPitch = 0;
    for (const int index : selection) {
        if (!validIndex(index, (int) notes.size()))
            continue;
        minimumTick = std::min(minimumTick, notes[index].tick);
        maximumEnd = std::max(maximumEnd, notes[index].tick + notes[index].duration);
        minimumPitch = std::min(minimumPitch, notes[index].pitch);
        maximumPitch = std::max(maximumPitch, notes[index].pitch);
    }

    const int clampedTickDelta = std::clamp(
        tickDelta, -minimumTick, sectionTicks - maximumEnd);
    const int clampedPitchDelta = std::clamp(
        pitchDelta, -minimumPitch, 127 - maximumPitch);
    for (const int index : selection) {
        if (!validIndex(index, (int) result.size()))
            continue;
        result[index].tick += clampedTickDelta;
        result[index].pitch += clampedPitchDelta;
    }
    return result;
}

void moveSelectedInPlace(
    std::vector<cadenza::arranger::PatternNote>& notes,
    const std::vector<cadenza::arranger::PatternNote>& startNotes,
    const std::vector<int>& selection, int tickDelta, int pitchDelta,
    int sectionTicks)
{
    if (selection.empty() || sectionTicks <= 0)
        return;

    int minimumTick = sectionTicks;
    int maximumEnd = 0;
    int minimumPitch = 127;
    int maximumPitch = 0;
    for (const int index : selection) {
        if (!validIndex(index, (int) startNotes.size())
            || !validIndex(index, (int) notes.size()))
            continue;
        minimumTick = std::min(minimumTick, startNotes[index].tick);
        maximumEnd = std::max(maximumEnd,
                              startNotes[index].tick + startNotes[index].duration);
        minimumPitch = std::min(minimumPitch, startNotes[index].pitch);
        maximumPitch = std::max(maximumPitch, startNotes[index].pitch);
    }

    const int clampedTickDelta = std::clamp(
        tickDelta, -minimumTick, sectionTicks - maximumEnd);
    const int clampedPitchDelta = std::clamp(
        pitchDelta, -minimumPitch, 127 - maximumPitch);
    for (const int index : selection) {
        if (!validIndex(index, (int) startNotes.size())
            || !validIndex(index, (int) notes.size()))
            continue;
        notes[index] = startNotes[index];
        notes[index].tick += clampedTickDelta;
        notes[index].pitch += clampedPitchDelta;
    }
}

std::vector<cadenza::arranger::PatternNote> resizeSelected(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection, int durationDelta, int minimumDuration,
    int sectionTicks)
{
    auto result = notes;
    const int minimum = std::max(1, minimumDuration);
    for (const int index : selection) {
        if (!validIndex(index, (int) result.size()))
            continue;
        const int maximum = std::max(minimum, sectionTicks - result[index].tick);
        result[index].duration = std::clamp(
            result[index].duration + durationDelta, minimum, maximum);
    }
    return result;
}

void resizeSelectedInPlace(
    std::vector<cadenza::arranger::PatternNote>& notes,
    const std::vector<cadenza::arranger::PatternNote>& startNotes,
    const std::vector<int>& selection, int durationDelta, int minimumDuration,
    int sectionTicks)
{
    const int minimum = std::max(1, minimumDuration);
    for (const int index : selection) {
        if (!validIndex(index, (int) startNotes.size())
            || !validIndex(index, (int) notes.size()))
            continue;
        notes[index] = startNotes[index];
        const int maximum = std::max(minimum, sectionTicks - notes[index].tick);
        notes[index].duration = std::clamp(
            notes[index].duration + durationDelta, minimum, maximum);
    }
}

void resizeSelectedLeftInPlace(
    std::vector<cadenza::arranger::PatternNote>& notes,
    const std::vector<cadenza::arranger::PatternNote>& startNotes,
    const std::vector<int>& selection, int startDelta, int minimumDuration,
    int sectionTicks)
{
    if (selection.empty() || sectionTicks <= 0)
        return;

    const int minimum = std::max(1, minimumDuration);
    int lowerDelta = -sectionTicks;
    int upperDelta = sectionTicks;
    for (const int index : selection) {
        if (!validIndex(index, (int) startNotes.size())
            || !validIndex(index, (int) notes.size()))
            continue;
        const auto& note = startNotes[index];
        lowerDelta = std::max(lowerDelta, -note.tick);
        upperDelta = std::min(upperDelta, note.duration - minimum);
    }

    if (lowerDelta > upperDelta)
        lowerDelta = upperDelta = 0;

    const int clampedDelta = std::clamp(startDelta, lowerDelta, upperDelta);
    for (const int index : selection) {
        if (!validIndex(index, (int) startNotes.size())
            || !validIndex(index, (int) notes.size()))
            continue;
        notes[index] = startNotes[index];
        const int end = notes[index].tick + notes[index].duration;
        notes[index].tick = std::clamp(notes[index].tick + clampedDelta,
                                       0, std::max(0, end - minimum));
        notes[index].duration = std::max(minimum, end - notes[index].tick);
    }
}

NoteEditResult duplicateSelected(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection)
{
    NoteEditResult result;
    result.notes = notes;
    for (const int index : selection) {
        if (!validIndex(index, (int) notes.size()))
            continue;
        result.selection.insert((int) result.notes.size());
        result.notes.push_back(notes[index]);
    }
    return result;
}

NoteClipboard copySelected(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection)
{
    NoteClipboard clipboard;
    if (selection.empty())
        return clipboard;

    int maximumEnd = 0;
    clipboard.baseTick = std::numeric_limits<int>::max();
    clipboard.basePitch = 127;
    for (const int index : selection) {
        if (!validIndex(index, (int) notes.size()))
            continue;
        clipboard.baseTick = std::min(clipboard.baseTick, notes[index].tick);
        clipboard.basePitch = std::min(clipboard.basePitch, notes[index].pitch);
        maximumEnd = std::max(maximumEnd, notes[index].tick + notes[index].duration);
    }
    if (clipboard.baseTick == std::numeric_limits<int>::max())
        return {};

    clipboard.spanTicks = std::max(1, maximumEnd - clipboard.baseTick);
    for (const int index : selection) {
        if (!validIndex(index, (int) notes.size()))
            continue;
        auto note = notes[index];
        note.tick -= clipboard.baseTick;
        note.pitch -= clipboard.basePitch;
        clipboard.notes.push_back(note);
    }
    return clipboard;
}

NoteEditResult pasteNotes(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteClipboard& clipboard, int destinationTick, int destinationPitch,
    int sectionTicks)
{
    NoteEditResult result;
    result.notes = notes;
    if (clipboard.empty() || sectionTicks <= 0)
        return result;

    int minRelativePitch = 127;
    int maxRelativePitch = 0;
    for (const auto& note : clipboard.notes) {
        minRelativePitch = std::min(minRelativePitch, note.pitch);
        maxRelativePitch = std::max(maxRelativePitch, note.pitch);
    }
    const int baseTick = std::clamp(destinationTick, 0, sectionTicks - 1);
    const int requestedPitch = destinationPitch >= 0
        ? destinationPitch : clipboard.basePitch;
    const int basePitch = std::clamp(
        requestedPitch, -minRelativePitch, 127 - maxRelativePitch);

    for (auto note : clipboard.notes) {
        note.tick += baseTick;
        if (note.tick >= sectionTicks)
            continue;
        note.pitch += basePitch;
        note.duration = std::clamp(note.duration, 1, sectionTicks - note.tick);
        result.selection.insert((int) result.notes.size());
        result.notes.push_back(note);
    }
    return result;
}

NoteEditResult duplicateToRight(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection, int sectionTicks)
{
    const auto clipboard = copySelected(notes, selection);
    return pasteNotes(notes, clipboard,
                      clipboard.baseTick + clipboard.spanTicks,
                      clipboard.basePitch, sectionTicks);
}

std::vector<cadenza::arranger::PatternNote> deleteSelected(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection)
{
    std::vector<cadenza::arranger::PatternNote> result;
    result.reserve(notes.size());
    for (int i = 0; i < (int) notes.size(); ++i)
        if (!selection.contains(i))
            result.push_back(notes[i]);
    return result;
}
}
