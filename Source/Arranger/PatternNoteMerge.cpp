#include "PatternNoteMerge.h"

namespace cadenza::arranger
{
PatternNoteMergeResult mergePatternNote(
    std::vector<PatternNote>& notes,
    const PatternNote& incoming,
    bool percussion,
    int duplicateGridTicks)
{
    PatternNoteMergeResult result;
    if (percussion) {
        for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
            auto& existing = notes[static_cast<std::size_t>(i)];
            const bool samePosition = duplicateGridTicks > 0
                ? (existing.tick + duplicateGridTicks / 2) / duplicateGridTicks
                    == (incoming.tick + duplicateGridTicks / 2)
                        / duplicateGridTicks
                : existing.tick == incoming.tick;
            if (existing.pitch == incoming.pitch
                && samePosition) {
                existing.velocity = incoming.velocity;
                existing.duration = incoming.duration;
                result.noteIndex = i;
                return result;
            }
        }
        notes.push_back(incoming);
        result.noteIndex = static_cast<int>(notes.size()) - 1;
        return result;
    }

    const int newStart = incoming.tick;
    const int newEnd = incoming.tick + incoming.duration;
    for (int i = 0; i < static_cast<int>(notes.size());) {
        auto& existing = notes[static_cast<std::size_t>(i)];
        if (existing.pitch == incoming.pitch) {
            const int existingStart = existing.tick;
            const int existingEnd = existing.tick + existing.duration;
            if (existingStart < newEnd && newStart < existingEnd) {
                if (existingStart < newStart) {
                    existing.duration = newStart - existingStart;
                    ++i;
                    continue;
                }
                result.erasedIndices.push_back(i);
                notes.erase(notes.begin() + i);
                continue;
            }
        }
        ++i;
    }
    notes.push_back(incoming);
    result.noteIndex = static_cast<int>(notes.size()) - 1;
    return result;
}
}
