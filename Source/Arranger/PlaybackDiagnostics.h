#pragma once

#include "PatternTransposer.h"
#include "Style.h"

#include <string>
#include <vector>

namespace cadenza::arranger
{
struct PlaybackDiagnosticResult
{
    bool ok = false;
    std::string error;
    std::string csvPath;
    std::string midiPath;
    std::string summaryPath;
    int eventCount = 0;
};

PlaybackDiagnosticResult exportPlaybackDiagnostics(const Style& style,
                                                   const std::string& sectionName,
                                                   const TransposeContext& context,
                                                   const std::string& outputDirectory,
                                                   int bars = 4);
}
