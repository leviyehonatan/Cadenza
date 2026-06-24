#pragma once

#include "Style.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <optional>
#include <vector>

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

enum class MidiStyleChordConfidence
{
    High,
    Medium,
    Low,
};

struct MidiStyleRecommendedRange
{
    int barStart = 0;
    int barCount = 4;
    bool fallback = true;
};

struct MidiStyleDetectedChord
{
    int root = 0;
    juce::String rootName = "C";
    juce::String chordSuffix;
    bool fallback = true;
    MidiStyleChordConfidence confidence = MidiStyleChordConfidence::Low;
    juce::String confidenceReason;
};

struct MidiStyleImportInfo
{
    int ppq = 0;
    int tempo = 120;
    int beatsPerBar = 4;
    int beatUnit = 4;
    int totalBars = 1;
    MidiStyleRecommendedRange recommendedRange;
    MidiStyleDetectedChord detectedChord;
    bool rangeChangesChord = false;
    int distinctChordCount = 0;
    juce::StringArray perBarChords;
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

struct MidiStyleSectionSpec
{
    juce::String sectionId;
    int barStart = 0;
    int barCount = 4;
    juce::String overrideRoot;
    juce::String overrideQuality;
};

struct MidiStyleAutoSplitOptions
{
    int blockBars = 4;
    bool normalizeToC = true;
};

struct MidiStyleAutoSplitResult
{
    std::vector<MidiStyleSectionSpec> sections;
    juce::StringArray warnings;
    bool ok = false;
};

MidiStyleAutoSplitResult autoSplitMidiFileForStyleImport(const juce::File& midi,
                                                         const MidiStyleAutoSplitOptions& options);

MidiStyleConvertResult convertMidiFileToNativeStyleMultiSection(const juce::File& midi,
                                                                const std::vector<MidiStyleSectionSpec>& sections,
                                                                bool normalizeToC);
}
