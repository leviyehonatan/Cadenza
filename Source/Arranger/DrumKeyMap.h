#pragma once

#include <optional>

namespace cadenza::arranger
{
enum class DrumSourceKeyMap
{
    GmCompatible,
    YamahaXg,
    YamahaXgPopLatin
};

struct DrumNoteRemap
{
    int originalNote = 0;
    int playbackNote = 0;
    DrumSourceKeyMap sourceKeyMap = DrumSourceKeyMap::GmCompatible;
    bool yamahaXg = false;
    bool remapped = false;
};

DrumSourceKeyMap sourceDrumKeyMap(std::optional<int> bankMsb,
                                  std::optional<int> program) noexcept;

bool isGmCompatibleDrumKeyMap(DrumSourceKeyMap keyMap) noexcept;
int remapDrumNoteToGm(DrumSourceKeyMap sourceKeyMap, int note) noexcept;
int remapYamahaXgToGmDrumNote(int note) noexcept;
DrumNoteRemap remapDrumNoteForPlayback(DrumSourceKeyMap sourceKeyMap, int note) noexcept;
}
