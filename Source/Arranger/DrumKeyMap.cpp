#include "DrumKeyMap.h"

#include <algorithm>

namespace cadenza::arranger
{
namespace
{
constexpr int kNoMap = -1;

constexpr int kXgStandardLowNote = 13;
constexpr int kXgStandardHighNote = 84;

constexpr int kXgStandardToGm[] = {
    64, 64, 56, 39, 76, 77, 75, 37, 37, 56, 37, 37,
    38, 40, 38, 40, 38, 75, 38, 37, 35, 37,
    35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
    47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58,
    59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70,
    71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81,
    70, 53, 59
};

constexpr int kXgLatinLowNote = 13;
constexpr int kXgLatinHighNote = 84;

// Based on JJazzLab KeyMapXG_PopLatin plus StandardKeyMapConverter's XGLatin
// to GM destination table. Undefined source pitches keep their original note.
constexpr int kXgLatinToGm[] = {
    35, 38, 44, 76, 77, 39, kNoMap, 75, 60, 61, 63, 62,
    63, 64, 62, 61, 60, 64, 63, 62, 62, 64, 60, 60,
    63, 62, 61, 64, 61, 61, 62, 63, 61, 64, 45, 69,
    kNoMap, kNoMap, kNoMap, kNoMap, 48, kNoMap, kNoMap, kNoMap,
    kNoMap, kNoMap, 50, 56, 76, 56, 77, 73, 74, 73,
    74, 54, 54, 70, 69, 70, 70, 69, 78, 79, 76, 77,
    69, 70, 80, 81, kNoMap, 51
};

int tableLookup(int note, int lowNote, int highNote, const int* table) noexcept
{
    if (note < lowNote || note > highNote)
        return kNoMap;
    return table[note - lowNote];
}

int fallbackGmDrumNote(int note) noexcept
{
    if (note >= 35 && note <= 81)
        return note;
    if (note > 81)
        return 49;

    int n = note;
    while (n < 35)
        n += 12;
    return std::clamp(n, 35, 50);
}
}

DrumSourceKeyMap sourceDrumKeyMap(std::optional<int> bankMsb,
                                  std::optional<int> program) noexcept
{
    if (!bankMsb)
        return DrumSourceKeyMap::GmCompatible;

    if (*bankMsb == 127)
        return DrumSourceKeyMap::YamahaXg;

    if (*bankMsb == 126) {
        const int p = program.value_or(0);
        if (p >= 40 && p <= 44)
            return DrumSourceKeyMap::YamahaXgPopLatin;
        return DrumSourceKeyMap::YamahaXg;
    }

    return DrumSourceKeyMap::GmCompatible;
}

bool isGmCompatibleDrumKeyMap(DrumSourceKeyMap keyMap) noexcept
{
    return keyMap == DrumSourceKeyMap::GmCompatible;
}

int remapDrumNoteToGm(DrumSourceKeyMap sourceKeyMap, int note) noexcept
{
    if (sourceKeyMap == DrumSourceKeyMap::GmCompatible)
        return note;

    int mapped = kNoMap;
    if (sourceKeyMap == DrumSourceKeyMap::YamahaXg)
        mapped = tableLookup(note, kXgStandardLowNote, kXgStandardHighNote, kXgStandardToGm);
    else if (sourceKeyMap == DrumSourceKeyMap::YamahaXgPopLatin)
        mapped = tableLookup(note, kXgLatinLowNote, kXgLatinHighNote, kXgLatinToGm);

    return mapped == kNoMap ? fallbackGmDrumNote(note) : mapped;
}

DrumNoteRemap remapDrumNoteForPlayback(DrumSourceKeyMap sourceKeyMap, int note) noexcept
{
    DrumNoteRemap result;
    result.originalNote = note;
    result.sourceKeyMap = sourceKeyMap;
    result.yamahaXg = !isGmCompatibleDrumKeyMap(sourceKeyMap);
    result.playbackNote = remapDrumNoteToGm(sourceKeyMap, note);
    result.remapped = result.playbackNote != result.originalNote;
    return result;
}
}
