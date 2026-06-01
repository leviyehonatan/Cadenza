// Song data model — a sequence of (bar, section, chord) events using one style.
// Loaded from a `.csong` JSON file.

#pragma once

#include <string>
#include <vector>

namespace cadenza::arranger
{
struct SongEvent
{
    int bar = 1;                 // 1-based bar number
    std::string section;         // section name from the style
    std::string chord;           // chord name (e.g. "C", "Am", "G7"); empty = keep current
};

struct Song
{
    std::string schema = "cadenza.song.v1";
    std::string id;
    std::string name;
    std::string styleId;
    int defaultTempo = 120;
    std::string key = "C";
    std::vector<SongEvent> events;

    // Returns the event that should be active at the start of the given bar (1-based).
    // The most-recent event with bar <= requested bar wins.
    const SongEvent* eventForBar(int bar) const noexcept;
};
}
