// SongLoader — JSON <-> Song.
// Pure C++; uses cadenza::json.

#pragma once

#include "Song.h"

#include <string>

namespace cadenza::arranger
{
struct SongLoadResult
{
    bool ok = true;
    std::string error;
    Song song;
};

SongLoadResult loadSongFromJson(const std::string& json);
std::string    saveSongToJson(const Song& song, bool pretty = true);

SongLoadResult loadSongFromFile(const std::string& path);
bool           saveSongToFile(const Song& song, const std::string& path, bool pretty = true);
}
