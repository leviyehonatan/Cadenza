#pragma once

#include "Style.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <optional>

namespace cadenza::arranger
{
struct MidiStyleConvertOptions
{
    int barStart = 0;
    int barCount = 4;
    juce::String sectionName = "mainA";
    std::optional<int> overrideSourceRoot;
    std::optional<juce::String> overrideSourceChord;
    bool normalizeToC = true;
};

struct MidiStyleDetectedChord
{
    int root = 0;
    juce::String rootName = "C";
    juce::String chordSuffix;
    bool fallback = true;
};

struct MidiStyleImportInfo
{
    int ppq = 0;
    int tempo = 120;
    int beatsPerBar = 4;
    int beatUnit = 4;
    int totalBars = 1;
    MidiStyleDetectedChord detectedChord;
    juce::StringArray warnings;
    bool ok = false;
};

MidiStyleImportInfo inspectMidiFileForStyleImport(const juce::File& midi,
                                                  int barStart,
                                                  int barCount);

struct MidiStyleConvertResult
{
    std::unique_ptr<Style> style;
    juce::StringArray warnings;
    bool ok = false;
};

MidiStyleConvertResult convertMidiFileToNativeStyle(const juce::File& midi,
                                                    const MidiStyleConvertOptions& options);
}
