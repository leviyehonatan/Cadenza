// StyleLoader — JSON ⇄ Style
// Uses cadenza::json (no JUCE).

#pragma once

#include "Style.h"

#include <string>

namespace cadenza::arranger
{
struct LoadResult
{
    bool ok = true;
    std::string error;
    Style style;
};

LoadResult loadStyleFromJson(const std::string& json);
std::string saveStyleToJson(const Style& style, bool pretty = true);

// File helpers (use only C++ standard library; can be called from cadenza_core_tests).
LoadResult loadStyleFromFile(const std::string& path);
LoadResult loadStyleFromStyFile(const std::string& path);
bool       saveStyleToFile(const Style& style, const std::string& path, bool pretty = true);

// Convert role enum ⇄ string (exposed for tests).
const char* roleToString(NoteRole role) noexcept;
NoteRole    roleFromString(const std::string& s) noexcept;
}
