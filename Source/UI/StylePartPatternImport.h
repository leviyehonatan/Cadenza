#pragma once

#include "../Arranger/Style.h"
#include "StylePartBarWorkflow.h"
#include "StylePartNoteWorkflow.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <string>
#include <vector>

namespace cadenza::ui::pattern_import
{
struct ParsedPattern
{
    std::vector<cadenza::arranger::PatternNote> notes;
    std::string error;

    bool ok() const noexcept { return error.empty(); }
};

struct PatternInsertResult
{
    std::vector<cadenza::arranger::PatternNote> notes;
    note_workflow::NoteSelection selection;
};

struct BuiltInPattern
{
    std::string name;
    std::vector<cadenza::arranger::PatternNote> notes;
};

ParsedPattern parseMidiPattern(const juce::MidiFile& file, int destinationPpq);

int resolveDestinationBar(const bar_workflow::BarSelection& selection,
                          int playheadTick,
                          int totalBars,
                          int ticksPerBar);

PatternInsertResult insertPattern(
    const std::vector<cadenza::arranger::PatternNote>& existing,
    const std::vector<cadenza::arranger::PatternNote>& imported,
    int destinationTick,
    int sectionTicks,
    bool percussion,
    int duplicateGridTicks = 0);

std::vector<BuiltInPattern> builtInPatterns(int ticksPerBeat, int beatsPerBar);
}
