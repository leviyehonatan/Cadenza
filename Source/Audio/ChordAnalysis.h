#pragma once

#include "Midi/ChordRecognizer.h"

#include <string>
#include <vector>

namespace cadenza::audio
{
struct ChordSegment
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    cadenza::midi::Chord chord;
    double confidence = 0.0;
};

struct AudioChordAnalysisResult
{
    bool ok = true;
    std::string error;
    std::string sourceName;
    int sampleRate = 0;
    double durationSeconds = 0.0;
    std::string detectedKeyName;
    int detectedKeyPitchClass = 0;
    bool detectedKeyMinor = false;
    int transposeToC = 0;
    std::vector<ChordSegment> segments;
};

AudioChordAnalysisResult analyzeChordProgression(const std::vector<float>& monoSamples,
                                                 int sampleRate,
                                                 const std::string& sourceName = {},
                                                 int targetKeyPitchClass = 0);

std::string formatChordAnalysisSummary(const AudioChordAnalysisResult& result);
}
