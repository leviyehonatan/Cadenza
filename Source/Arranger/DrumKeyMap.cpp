#include "DrumKeyMap.h"

// Clean-room implementation derived from the published General MIDI Level 1
// percussion key map and Yamaha XG drum/percussion specifications.

#include <algorithm>

namespace cadenza::arranger
{
namespace
{
constexpr int kNoMap = -1;
constexpr int kGmDrumLowNote = 35;
constexpr int kGmDrumHighNote = 81;

int fallbackGmDrumNote(int note) noexcept
{
    if (note >= kGmDrumLowNote && note <= kGmDrumHighNote)
        return note;
    if (note > kGmDrumHighNote)
        return 49;

    int n = note;
    while (n < kGmDrumLowNote)
        n += 12;
    return std::clamp(n, kGmDrumLowNote, 50);
}

int remapYamahaXgStandardExtension(int note) noexcept
{
    switch (note) {
        case 13: return 36; // mute surdo -> bass drum pulse
        case 14: return 47; // open surdo -> low-mid tom
        case 15: return 53; // high Q -> ride bell
        case 16: return 37; // slap -> side stick
        case 17: return 39; // scratch push -> hand clap
        case 18: return 39; // scratch pull -> hand clap
        case 19: return 39; // finger snap -> hand clap
        case 20: return 37; // click noise -> side stick
        case 21: return 37; // metronome click -> side stick
        case 22: return 53; // metronome bell -> ride bell
        case 23: return 37; // sequence click L -> side stick
        case 24: return 37; // sequence click H -> side stick
        case 25: return 38; // brush tap -> acoustic snare
        case 26: return 40; // brush swirl -> electric snare
        case 27: return 38; // brush slap -> acoustic snare
        case 28: return 40; // brush tap swirl -> electric snare
        case 29: return 38; // snare roll -> acoustic snare
        case 30: return 75; // castanet -> claves
        case 31: return 38; // snare low/soft -> acoustic snare
        case 32: return 37; // sticks -> side stick
        case 33: return 35; // bass drum low/soft -> acoustic bass drum
        case 34: return 37; // open rim shot -> side stick
        case 82: return 70; // shaker -> maracas
        case 83: return 53; // jingle bell -> ride bell / bell-like metal hit
        case 84: return 59; // bell tree -> ride cymbal 2 shimmer
        case 85: return 75; // castanets -> claves
        case 86: return 36; // mute surdo -> bass drum pulse
        case 87: return 47; // open surdo -> low-mid tom
        default: return kNoMap;
    }
}

int remapYamahaXgPopLatinNote(int note) noexcept
{
    switch (note) {
        case 13: return 35; // bass drum
        case 14: return 38; // snare
        case 15: return 44; // pedal hi-hat
        case 16: return 76; // high wood block
        case 17: return 77; // low wood block
        case 18: return 39; // hand clap
        case 20: return 75; // claves
        case 21: return 60; // high bongo
        case 22: return 61; // low bongo
        case 23: return 63; // open high conga
        case 24: return 62; // mute high conga
        case 25: return 63; // open high conga
        case 26: return 64; // low conga
        case 27: return 62; // mute high conga
        case 28: return 61; // low bongo
        case 29: return 60; // high bongo
        case 30: return 64; // low conga
        case 31: return 63; // open high conga
        case 32: return 62; // mute high conga
        case 33: return 62; // mute high conga
        case 34: return 64; // low conga
        case 35: return 60; // high bongo
        case 36: return 60; // high bongo
        case 37: return 63; // open high conga
        case 38: return 62; // mute high conga
        case 39: return 61; // low bongo
        case 40: return 64; // low conga
        case 41: return 61; // low bongo
        case 42: return 61; // low bongo
        case 43: return 62; // mute high conga
        case 44: return 63; // open high conga
        case 45: return 61; // low bongo
        case 46: return 64; // low conga
        case 47: return 45; // low tom
        case 48: return 69; // cabasa
        case 53: return 48; // high-mid tom
        case 59: return 50; // high tom
        case 60: return 56; // cowbell
        case 61: return 76; // high wood block
        case 62: return 56; // cowbell
        case 63: return 77; // low wood block
        case 64: return 73; // short guiro
        case 65: return 74; // long guiro
        case 66: return 73; // short guiro
        case 67: return 74; // long guiro
        case 68: return 54; // tambourine
        case 69: return 54; // tambourine
        case 70: return 70; // maracas
        case 71: return 69; // cabasa
        case 72: return 70; // maracas
        case 73: return 70; // maracas
        case 74: return 69; // cabasa
        case 75: return 78; // mute cuica
        case 76: return 79; // open cuica
        case 77: return 76; // high wood block
        case 78: return 77; // low wood block
        case 79: return 69; // cabasa
        case 80: return 70; // maracas
        case 81: return 80; // mute triangle
        case 82: return 81; // open triangle
        case 84: return 51; // ride cymbal
        default: return kNoMap;
    }
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
        mapped = remapYamahaXgStandardExtension(note);
    else if (sourceKeyMap == DrumSourceKeyMap::YamahaXgPopLatin)
        mapped = remapYamahaXgPopLatinNote(note);

    if (mapped == kNoMap && note >= kGmDrumLowNote && note <= kGmDrumHighNote)
        return note;

    return mapped == kNoMap ? fallbackGmDrumNote(note) : mapped;
}

int remapYamahaXgToGmDrumNote(int note) noexcept
{
    return remapDrumNoteToGm(DrumSourceKeyMap::YamahaXg, note);
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
