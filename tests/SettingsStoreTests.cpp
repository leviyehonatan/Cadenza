#include "Settings/SettingsStore.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

std::string tempPath(const char* name)
{
    auto p = std::filesystem::temp_directory_path() / name;
    return p.string();
}

void defaultsWhenMissing()
{
    using namespace cadenza::settings;
    const auto path = tempPath("cadenza_settings_test_missing.json");
    std::filesystem::remove(path);

    SettingsStore s(path);
    bool loaded = s.load();
    expect(!loaded, "missing file -> load returns false");
    expect(s.state().bpm == 120, "defaults preserved when file missing");
    expect(s.state().key == "C", "default key C");
}

void saveAndReload()
{
    using namespace cadenza::settings;
    const auto path = tempPath("cadenza_settings_test_roundtrip.json");
    std::filesystem::remove(path);

    {
        SettingsStore s(path);
        s.state().bpm = 144;
        s.state().key = "G";
        s.state().bankMemory = "Rhodes";
        s.state().lastStyleId = "8-beat-pop";
        s.state().lastStylePath = "C:/styles/test.sty";
        s.state().lastSoundFontPath = "C:/soundfonts/test.sf2";
        s.state().chordBassEnabled = false;
        s.state().chordArrangerEnabled = false;
        s.state().chordMemoryEnabled = true;
        s.state().syncroStopOnRelease = false;
        s.state().eqLowDb = 6;
        s.state().eqMidDb = -2;
        s.state().eqHighDb = 3;
        s.state().styleMixes["8-beat-pop"] = {
            { /*channel*/ 12, /*program*/ 27, /*volume*/ 90, /*mute*/ false, /*solo*/ true },
            { /*channel*/ 14, /*program*/ 48, /*volume*/ -1, /*mute*/ true,  /*solo*/ false },
        };
        expect(s.save(), "save returns true");
    }

    {
        SettingsStore s(path);
        expect(s.load(), "reload returns true");
        expect(s.state().bpm == 144, "bpm round-trip");
        expect(s.state().key == "G", "key round-trip");
        expect(s.state().bankMemory == "Rhodes", "bankMemory round-trip");
        expect(s.state().lastStyleId == "8-beat-pop", "lastStyleId round-trip");
        expect(s.state().lastStylePath == "C:/styles/test.sty", "lastStylePath round-trip");
        expect(s.state().lastSoundFontPath == "C:/soundfonts/test.sf2", "lastSoundFontPath round-trip");
        expect(!s.state().chordBassEnabled, "chordBassEnabled round-trip");
        expect(!s.state().chordArrangerEnabled, "chordArrangerEnabled round-trip");
        expect(s.state().chordMemoryEnabled, "chordMemoryEnabled round-trip");
        expect(!s.state().syncroStopOnRelease, "syncroStopOnRelease round-trip");
        expect(s.state().eqLowDb == 6 && s.state().eqMidDb == -2 && s.state().eqHighDb == 3,
               "EQ gains round-trip");

        const auto it = s.state().styleMixes.find("8-beat-pop");
        expect(it != s.state().styleMixes.end(), "styleMixes has the style");
        if (it != s.state().styleMixes.end()) {
            expect(it->second.size() == 2, "styleMixes channel count round-trip");
            expect(it->second[0].channel == 12 && it->second[0].program == 27
                   && it->second[0].volume == 90 && it->second[0].solo,
                   "styleMixes channel 12 round-trip");
            expect(it->second[1].channel == 14 && it->second[1].mute
                   && it->second[1].volume == -1,
                   "styleMixes channel 14 round-trip (mute + unset volume)");
        }
    }

    std::filesystem::remove(path);
}

void corruptFileFailsCleanly()
{
    using namespace cadenza::settings;
    const auto path = tempPath("cadenza_settings_test_corrupt.json");
    {
        FILE* f = std::fopen(path.c_str(), "w");
        if (f) { std::fprintf(f, "not json"); std::fclose(f); }
    }

    SettingsStore s(path);
    expect(!s.load(), "corrupt file load returns false");
    expect(s.state().bpm == 120, "defaults preserved after corrupt load");

    std::filesystem::remove(path);
}

void otsLinkRoundTrips()
{
    const auto path = tempPath("cadenza_settings_test_otslink.json");
    {
        cadenza::settings::SettingsStore store(path);
        expect(!store.state().otsLinkEnabled, "ots link defaults to off");
        store.state().otsLinkEnabled = true;
        expect(store.save(), "otslink settings save");
    }
    {
        cadenza::settings::SettingsStore store(path);
        expect(store.load(), "otslink settings load");
        expect(store.state().otsLinkEnabled, "otsLinkEnabled survives a round trip");
    }
    std::filesystem::remove(path);
}
}

int main()
{
    defaultsWhenMissing();
    saveAndReload();
    corruptFileFailsCleanly();
    otsLinkRoundTrips();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All SettingsStore tests passed\n";
    return EXIT_SUCCESS;
}
