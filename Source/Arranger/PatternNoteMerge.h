#pragma once

#include "Style.h"

#include <vector>

namespace cadenza::arranger
{
struct PatternNoteMergeResult
{
    int noteIndex = -1;
    std::vector<int> erasedIndices;
};

PatternNoteMergeResult mergePatternNote(
    std::vector<PatternNote>& notes,
    const PatternNote& incoming,
    bool percussion,
    int duplicateGridTicks = 0);
}
