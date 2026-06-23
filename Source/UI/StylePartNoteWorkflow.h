#pragma once

#include "../Arranger/Style.h"

#include <set>
#include <vector>

namespace cadenza::ui::note_workflow
{
using NoteSelection = std::set<int>;

struct Rectangle
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct NoteBounds
{
    int index = -1;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct NoteClipboard
{
    int baseTick = 0;
    int basePitch = 60;
    int spanTicks = 0;
    std::vector<cadenza::arranger::PatternNote> notes;

    bool empty() const noexcept { return notes.empty(); }
};

struct NoteEditResult
{
    std::vector<cadenza::arranger::PatternNote> notes;
    NoteSelection selection;
};

void selectOnly(NoteSelection& selection, int index);
void toggle(NoteSelection& selection, int index);
NoteSelection selectIntersecting(const std::vector<NoteBounds>& bounds,
                                 Rectangle selectionRectangle);
std::vector<int> selectedIndices(const NoteSelection& selection, int noteCount);

std::vector<cadenza::arranger::PatternNote> moveSelected(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection, int tickDelta, int pitchDelta,
    int sectionTicks);
std::vector<cadenza::arranger::PatternNote> resizeSelected(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection, int durationDelta, int minimumDuration,
    int sectionTicks);
void moveSelectedInPlace(
    std::vector<cadenza::arranger::PatternNote>& notes,
    const std::vector<cadenza::arranger::PatternNote>& startNotes,
    const std::vector<int>& selection, int tickDelta, int pitchDelta,
    int sectionTicks);
void resizeSelectedInPlace(
    std::vector<cadenza::arranger::PatternNote>& notes,
    const std::vector<cadenza::arranger::PatternNote>& startNotes,
    const std::vector<int>& selection, int durationDelta, int minimumDuration,
    int sectionTicks);
void resizeSelectedLeftInPlace(
    std::vector<cadenza::arranger::PatternNote>& notes,
    const std::vector<cadenza::arranger::PatternNote>& startNotes,
    const std::vector<int>& selection, int startDelta, int minimumDuration,
    int sectionTicks);
NoteEditResult duplicateSelected(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection);

NoteClipboard copySelected(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection);
NoteEditResult pasteNotes(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteClipboard& clipboard, int destinationTick, int destinationPitch,
    int sectionTicks);
NoteEditResult duplicateToRight(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection, int sectionTicks);
std::vector<cadenza::arranger::PatternNote> deleteSelected(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const NoteSelection& selection);
}
