#pragma once

#include "../Arranger/Style.h"

#include <vector>

namespace cadenza::ui::bar_workflow
{
struct BarSelection
{
    int anchor = -1;
    int first = -1;
    int last = -1;

    bool valid() const noexcept { return first >= 0 && last >= first; }
    int length() const noexcept { return valid() ? last - first + 1 : 0; }
    bool contains(int bar) const noexcept
    {
        return valid() && bar >= first && bar <= last;
    }
};

struct BarClipboard
{
    int lengthBars = 0;
    std::vector<cadenza::arranger::PatternNote> notes;

    bool empty() const noexcept { return lengthBars <= 0; }
};

struct MoveResult
{
    std::vector<cadenza::arranger::PatternNote> notes;
    BarSelection selection;
};

enum class EditorCommand
{
    None,
    TogglePlay,
    ToggleRecord,
    Copy,
    Paste,
    Duplicate,
    Clear,
    ClearSelection,
};

enum class ShortcutKey
{
    Space,
    R,
    C,
    V,
    D,
    DeleteKey,
    Escape,
    Other,
};

void clearSelection(BarSelection& selection) noexcept;
void selectBar(BarSelection& selection, int bar, int totalBars) noexcept;
void extendSelection(BarSelection& selection, int bar, int totalBars) noexcept;
void dragSelection(BarSelection& selection, int bar, int totalBars) noexcept;

BarClipboard copyBars(const std::vector<cadenza::arranger::PatternNote>& notes,
                      const BarSelection& selection, int barTicks);
std::vector<cadenza::arranger::PatternNote> pasteBars(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const BarClipboard& clipboard, int destinationBar,
    int totalBars, int barTicks);
std::vector<cadenza::arranger::PatternNote> duplicateBars(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const BarSelection& selection, int totalBars, int barTicks);
std::vector<cadenza::arranger::PatternNote> clearBars(
    const std::vector<cadenza::arranger::PatternNote>& notes,
    const BarSelection& selection, int barTicks);
MoveResult moveBars(const std::vector<cadenza::arranger::PatternNote>& notes,
                    const BarSelection& selection, int deltaBars,
                    int totalBars, int barTicks);

int resolvePasteBar(const BarSelection& selection, int playheadTick,
                    int totalBars, int barTicks) noexcept;
EditorCommand commandForShortcut(ShortcutKey key, bool controlDown) noexcept;
}
