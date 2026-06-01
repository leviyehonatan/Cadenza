#include "Arranger/SongLoader.h"

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

using namespace cadenza::arranger;

const char* sampleSongJson = R"({
  "$schema": "cadenza.song.v1",
  "id": "demo",
  "name": "Demo Song",
  "style": "8-beat-pop",
  "tempo": 120,
  "key": "C",
  "events": [
    { "bar": 1, "section": "intro" },
    { "bar": 3, "section": "mainA", "chord": "C" },
    { "bar": 7, "section": "mainA", "chord": "F" },
    { "bar": 11, "section": "mainB", "chord": "G" },
    { "bar": 15, "section": "ending", "chord": "C" }
  ]
})";

void loadFromJsonSucceeds()
{
    auto r = loadSongFromJson(sampleSongJson);
    expect(r.ok, "load ok");
    expect(r.song.id == "demo", "id");
    expect(r.song.name == "Demo Song", "name");
    expect(r.song.styleId == "8-beat-pop", "styleId");
    expect(r.song.defaultTempo == 120, "tempo");
    expect(r.song.key == "C", "key");
    expect(r.song.events.size() == 5, "5 events");
    expect(r.song.events[2].chord == "F", "event 2 chord = F");
    expect(r.song.events[3].section == "mainB", "event 3 section = mainB");
}

void eventForBarPicksMostRecent()
{
    auto r = loadSongFromJson(sampleSongJson);
    expect(r.ok, "load ok");

    const auto* e1 = r.song.eventForBar(1);
    expect(e1 && e1->section == "intro", "bar 1 -> intro");

    const auto* e3 = r.song.eventForBar(3);
    expect(e3 && e3->section == "mainA" && e3->chord == "C", "bar 3 -> mainA/C");

    const auto* e8 = r.song.eventForBar(8);
    expect(e8 && e8->chord == "F", "bar 8 -> still F from bar 7");

    const auto* e12 = r.song.eventForBar(12);
    expect(e12 && e12->section == "mainB" && e12->chord == "G", "bar 12 -> mainB/G");

    const auto* e20 = r.song.eventForBar(20);
    expect(e20 && e20->section == "ending", "bar 20 -> ending (last event sticks)");

    const auto* e0 = r.song.eventForBar(0);
    expect(e0 == nullptr, "bar 0 -> no event yet");
}

void saveRoundTrip()
{
    auto loaded = loadSongFromJson(sampleSongJson);
    expect(loaded.ok, "initial load");

    auto json = saveSongToJson(loaded.song, false);
    auto reloaded = loadSongFromJson(json);
    expect(reloaded.ok, "reload ok");
    expect(reloaded.song.events.size() == loaded.song.events.size(), "event count round-trip");
    expect(reloaded.song.id == loaded.song.id, "id round-trip");
    expect(reloaded.song.styleId == loaded.song.styleId, "styleId round-trip");
}

void malformedFails()
{
    auto r = loadSongFromJson("{not valid}");
    expect(!r.ok, "malformed rejected");
}
}

int main()
{
    loadFromJsonSucceeds();
    eventForBarPicksMostRecent();
    saveRoundTrip();
    malformedFails();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All SongLoader tests passed\n";
    return EXIT_SUCCESS;
}
