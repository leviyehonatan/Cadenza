// VoiceMapTests — voice -> VST3 instrument/preset mapping (Giglad-style voicing).

#include "Audio/VoiceMap.h"

#include <cstdlib>
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

using cadenza::audio::VoiceMap;

void emptyByDefault()
{
    VoiceMap m;
    expect(m.empty(), "fresh map is empty");
    expect(m.forProgram(0) == nullptr, "no entry for program 0");
    expect(m.forDrums() == nullptr, "no drum entry");
}

void exactProgramLookup()
{
    VoiceMap m;
    expect(m.loadFromJson(R"({ "programs": { "0": { "plugin": "Piano.vst3", "gain": 100 } } })"),
           "parse program map");
    const auto* e = m.forProgram(0);
    expect(e != nullptr, "program 0 maps");
    expect(e && e->pluginPath == "Piano.vst3", "program 0 plugin path");
    expect(e && e->gain == 100, "program 0 gain parsed");
    expect(m.forProgram(1) == nullptr, "program 1 unmapped");
}

void familyFallback()
{
    VoiceMap m;
    // Family 3 = GM programs 24..31 (guitars).
    expect(m.loadFromJson(R"({ "families": { "3": { "plugin": "Guitar.vst3" } } })"),
           "parse family map");
    const auto* e = m.forProgram(27);   // 27 / 8 = 3
    expect(e != nullptr && e->pluginPath == "Guitar.vst3", "program 27 falls back to family 3");
    expect(m.forProgram(0) == nullptr, "program 0 not in family 3");
}

void exactBeatsFamily()
{
    VoiceMap m;
    expect(m.loadFromJson(R"({
        "families": { "0": { "plugin": "Fam.vst3" } },
        "programs": { "2": { "plugin": "Exact.vst3" } }
    })"), "parse mixed map");
    const auto* e2 = m.forProgram(2);
    expect(e2 && e2->pluginPath == "Exact.vst3", "exact program wins over family");
    const auto* e1 = m.forProgram(1);   // family 0 fallback
    expect(e1 && e1->pluginPath == "Fam.vst3", "program 1 uses family 0 fallback");
}

void drumsLookup()
{
    VoiceMap m;
    expect(m.loadFromJson(R"({ "drums": { "plugin": "Kit.vst3", "state": "abc" } })"),
           "parse drums");
    const auto* d = m.forDrums();
    expect(d != nullptr && d->pluginPath == "Kit.vst3", "drum plugin path");
    expect(d && d->presetState == "abc", "drum preset state parsed");
    expect(d && d->gain == -1, "drum gain defaults to -1");
}

void ignoresEntriesWithoutPlugin()
{
    VoiceMap m;
    expect(m.loadFromJson(R"({ "programs": { "0": { "gain": 50 } } })"), "parse");
    expect(m.forProgram(0) == nullptr, "entry without plugin is ignored");
    expect(m.empty(), "map stays empty when no usable entries");
}

void outOfRangeIgnored()
{
    VoiceMap m;
    m.loadFromJson(R"({ "programs": { "200": { "plugin": "X.vst3" }, "-1": { "plugin": "Y.vst3" } } })");
    expect(m.empty(), "out-of-range program keys are dropped");
}

void malformedJsonReturnsFalseAndClears()
{
    VoiceMap m;
    m.loadFromJson(R"({ "programs": { "0": { "plugin": "Piano.vst3" } } })");
    expect(!m.empty(), "map populated first");
    expect(!m.loadFromJson("{ not valid json"), "malformed JSON returns false");
    expect(m.empty(), "map cleared after a failed load");
}

void reloadReplaces()
{
    VoiceMap m;
    m.loadFromJson(R"({ "programs": { "0": { "plugin": "A.vst3" } } })");
    m.loadFromJson(R"({ "programs": { "1": { "plugin": "B.vst3" } } })");
    expect(m.forProgram(0) == nullptr, "old entry gone after reload");
    const auto* e = m.forProgram(1);
    expect(e && e->pluginPath == "B.vst3", "new entry present after reload");
}

void setAndSerializeRoundTrips()
{
    VoiceMap m;
    cadenza::audio::VoiceMapEntry drums; drums.pluginPath = "sforzando.vst3"; drums.presetState = "BLOB1"; drums.gain = 100;
    cadenza::audio::VoiceMapEntry bass;  bass.pluginPath  = "sforzando.vst3"; bass.presetState  = "BLOB2";
    m.setDrums(drums);
    m.setFamily(4, bass);
    expect(!m.empty(), "map non-empty after setters");

    const std::string json = m.toJson();
    VoiceMap m2;
    expect(m2.loadFromJson(json), "serialized json re-parses");
    expect(m2.forDrums() && m2.forDrums()->pluginPath == "sforzando.vst3", "drums round-trip plugin");
    expect(m2.forDrums() && m2.forDrums()->presetState == "BLOB1", "drums round-trip state");
    expect(m2.forDrums() && m2.forDrums()->gain == 100, "drums round-trip gain");
    expect(m2.forProgram(33) && m2.forProgram(33)->presetState == "BLOB2", "bass family round-trip (prog 33 -> family 4)");
}
}

int main()
{
    emptyByDefault();
    setAndSerializeRoundTrips();
    exactProgramLookup();
    familyFallback();
    exactBeatsFamily();
    drumsLookup();
    ignoresEntriesWithoutPlugin();
    outOfRangeIgnored();
    malformedJsonReturnsFalseAndClears();
    reloadReplaces();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All VoiceMap tests passed\n";
    return EXIT_SUCCESS;
}
