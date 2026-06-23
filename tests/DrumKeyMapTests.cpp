#include "../Source/Arranger/DrumKeyMap.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace cadenza::arranger;

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

void expectMap(DrumSourceKeyMap keyMap, int source, int expected, const std::string& message)
{
    const int actual = remapDrumNoteToGm(keyMap, source);
    expect(actual == expected, message + " expected " + std::to_string(expected)
           + " got " + std::to_string(actual));
}

void gmStandardNotesPassThrough()
{
    const std::vector<int> notes = {
        35, 36, 37, 38, 39, 40,
        41, 43, 45, 47, 48, 50,
        42, 44, 46,
        49, 51, 53, 57, 59,
        60, 61, 62, 63, 64, 70, 75, 80, 81
    };

    for (int note : notes)
        expectMap(DrumSourceKeyMap::GmCompatible, note, note, "GM note passes through");
}

void xgStandardKeepsGmCore()
{
    expectMap(DrumSourceKeyMap::YamahaXg, 36, 36, "XG GM-core kick stays kick");
    expectMap(DrumSourceKeyMap::YamahaXg, 38, 38, "XG GM-core snare stays snare");
    expectMap(DrumSourceKeyMap::YamahaXg, 42, 42, "XG closed hat stays closed hat");
    expectMap(DrumSourceKeyMap::YamahaXg, 46, 46, "XG open hat stays open hat");
    expectMap(DrumSourceKeyMap::YamahaXg, 49, 49, "XG crash stays crash");
    expectMap(DrumSourceKeyMap::YamahaXg, 51, 51, "XG ride stays ride");
    expectMap(DrumSourceKeyMap::YamahaXg, 41, 41, "XG low floor tom stays tom");
    expectMap(DrumSourceKeyMap::YamahaXg, 45, 45, "XG low tom stays tom");
    expectMap(DrumSourceKeyMap::YamahaXg, 50, 50, "XG high tom stays tom");
}

void xgExtensionsMapToGmSounds()
{
    expectMap(DrumSourceKeyMap::YamahaXg, 31, 38, "XG soft snare maps to GM snare");
    expectMap(DrumSourceKeyMap::YamahaXg, 33, 35, "XG soft kick maps to GM kick");
    expectMap(DrumSourceKeyMap::YamahaXg, 34, 37, "XG rim shot maps to GM side stick");
    expectMap(DrumSourceKeyMap::YamahaXg, 82, 70, "XG shaker maps to GM maracas");
    expectMap(DrumSourceKeyMap::YamahaXg, 83, 53, "XG jingle bell maps to GM bell");
    expectMap(DrumSourceKeyMap::YamahaXg, 84, 59, "XG belltree maps to GM cymbal wash");
}

void xgPopLatinMapsDifferentInRangeMeanings()
{
    expectMap(DrumSourceKeyMap::YamahaXgPopLatin, 36, 60, "PopLatin high bongo does not play GM kick");
    expectMap(DrumSourceKeyMap::YamahaXgPopLatin, 42, 61, "PopLatin low bongo does not play GM closed hat");
    expectMap(DrumSourceKeyMap::YamahaXgPopLatin, 64, 73, "PopLatin short guiro maps to GM short guiro");
    expectMap(DrumSourceKeyMap::YamahaXgPopLatin, 72, 70, "PopLatin maracas maps to GM maracas");
    expectMap(DrumSourceKeyMap::YamahaXgPopLatin, 81, 80, "PopLatin mute triangle maps to GM mute triangle");
}

void noSingleSoundCollapse()
{
    const std::vector<int> sourceNotes = { 31, 33, 34, 36, 38, 42, 46, 49, 51, 82, 83, 84 };
    std::vector<int> mapped;
    for (int note : sourceNotes)
        mapped.push_back(remapDrumNoteToGm(DrumSourceKeyMap::YamahaXg, note));

    int distinct = 0;
    for (std::size_t i = 0; i < mapped.size(); ++i) {
        bool seen = false;
        for (std::size_t j = 0; j < i; ++j)
            seen = seen || mapped[i] == mapped[j];
        if (!seen)
            ++distinct;
    }

    expect(distinct >= 8, "representative XG notes map to varied GM sounds");
}

void sourceKitDetection()
{
    expect(sourceDrumKeyMap(std::optional<int>{ 120 }, std::optional<int>{ 0 }) == DrumSourceKeyMap::GmCompatible,
           "GM2/GS drum bank is GM-compatible for note layout");
    expect(sourceDrumKeyMap(std::optional<int>{ 127 }, std::optional<int>{ 0 }) == DrumSourceKeyMap::YamahaXg,
           "XG bank 127 selects XG standard map");
    expect(sourceDrumKeyMap(std::optional<int>{ 126 }, std::optional<int>{ 40 }) == DrumSourceKeyMap::YamahaXgPopLatin,
           "XG bank 126 PopLatin programs select PopLatin map");
}
}

int main()
{
    gmStandardNotesPassThrough();
    xgStandardKeepsGmCore();
    xgExtensionsMapToGmSounds();
    xgPopLatinMapsDifferentInRangeMeanings();
    noSingleSoundCollapse();
    sourceKitDetection();

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
