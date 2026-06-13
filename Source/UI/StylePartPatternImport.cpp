#include "StylePartPatternImport.h"

#include "../Arranger/PatternNoteMerge.h"

#include <algorithm>
#include <cmath>

namespace cadenza::ui::pattern_import
{
namespace
{
using cadenza::arranger::NoteRole;
using cadenza::arranger::PatternNote;

int scaledTick(double tick, int sourcePpq, int destinationPpq)
{
    return static_cast<int>(std::llround(
        tick * static_cast<double>(destinationPpq)
        / static_cast<double>(sourcePpq)));
}

void remapSelectionAfterErase(note_workflow::NoteSelection& selection,
                              int erasedIndex)
{
    note_workflow::NoteSelection remapped;
    for (const int index : selection) {
        if (index < erasedIndex)
            remapped.insert(index);
        else if (index > erasedIndex)
            remapped.insert(index - 1);
    }
    selection = std::move(remapped);
}

PatternNote drumNote(int tick, int duration, int pitch, int velocity)
{
    return PatternNote { tick, duration, pitch, velocity,
                         NoteRole::Absolute, 0 };
}
}

ParsedPattern parseMidiPattern(const juce::MidiFile& file, int destinationPpq)
{
    ParsedPattern result;
    const int sourcePpq = file.getTimeFormat();
    if (sourcePpq <= 0 || destinationPpq <= 0) {
        result.error = "MIDI pattern must use PPQ timing.";
        return result;
    }

    const juce::MidiMessageSequence* sourceTrack = nullptr;
    for (int trackIndex = 0; trackIndex < file.getNumTracks(); ++trackIndex) {
        const auto* track = file.getTrack(trackIndex);
        if (track == nullptr)
            continue;
        for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex) {
            if (track->getEventPointer(eventIndex)->message.isNoteOn()) {
                sourceTrack = track;
                break;
            }
        }
        if (sourceTrack != nullptr)
            break;
    }

    if (sourceTrack == nullptr) {
        result.error = "The MIDI file contains no note events.";
        return result;
    }

    juce::MidiMessageSequence sequence(*sourceTrack);
    sequence.updateMatchedPairs();
    for (int eventIndex = 0; eventIndex < sequence.getNumEvents(); ++eventIndex) {
        const auto* event = sequence.getEventPointer(eventIndex);
        if (event == nullptr || !event->message.isNoteOn())
            continue;

        const int start = scaledTick(
            event->message.getTimeStamp(), sourcePpq, destinationPpq);
        int end = start + 1;
        if (event->noteOffObject != nullptr) {
            end = scaledTick(event->noteOffObject->message.getTimeStamp(),
                             sourcePpq, destinationPpq);
        }

        PatternNote note;
        note.tick = std::max(0, start);
        note.duration = std::max(1, end - start);
        note.pitch = event->message.getNoteNumber();
        note.velocity = event->message.getVelocity();
        note.role = NoteRole::Absolute;
        result.notes.push_back(note);
    }

    std::stable_sort(
        result.notes.begin(), result.notes.end(),
        [](const PatternNote& left, const PatternNote& right) {
            if (left.tick != right.tick)
                return left.tick < right.tick;
            return left.pitch < right.pitch;
        });
    return result;
}

int resolveDestinationBar(const bar_workflow::BarSelection& selection,
                          int playheadTick,
                          int totalBars,
                          int ticksPerBar)
{
    if (totalBars <= 0 || ticksPerBar <= 0)
        return 0;
    if (selection.valid())
        return std::clamp(selection.first, 0, totalBars - 1);
    if (playheadTick >= 0)
        return std::clamp(playheadTick / ticksPerBar, 0, totalBars - 1);
    return 0;
}

PatternInsertResult insertPattern(
    const std::vector<PatternNote>& existing,
    const std::vector<PatternNote>& imported,
    int destinationTick,
    int sectionTicks,
    bool percussion,
    int duplicateGridTicks)
{
    PatternInsertResult result;
    result.notes = existing;
    if (sectionTicks <= 0)
        return result;

    const int offset = std::clamp(destinationTick, 0, sectionTicks - 1);
    for (auto note : imported) {
        note.tick += offset;
        if (note.tick < 0 || note.tick >= sectionTicks)
            continue;
        note.duration = std::clamp(note.duration, 1, sectionTicks - note.tick);
        note.pitch = std::clamp(note.pitch, 0, 127);
        note.velocity = std::clamp(note.velocity, 1, 127);
        note.role = NoteRole::Absolute;

        const auto merged = cadenza::arranger::mergePatternNote(
            result.notes, note, percussion, duplicateGridTicks);
        for (const int erasedIndex : merged.erasedIndices)
            remapSelectionAfterErase(result.selection, erasedIndex);
        if (merged.noteIndex >= 0)
            result.selection.insert(merged.noteIndex);
    }
    return result;
}

std::vector<BuiltInPattern> builtInPatterns(int ticksPerBeat, int beatsPerBar)
{
    const int ppq = std::max(1, ticksPerBeat);
    const int beats = std::max(1, beatsPerBar);
    const int eighth = std::max(1, ppq / 2);
    const int hitDuration = std::max(1, ppq / 8);

    BuiltInPattern rock { "Basic Rock Drum Beat", {} };
    for (int tick = 0; tick < beats * ppq; tick += eighth)
        rock.notes.push_back(drumNote(tick, hitDuration, 42, 82));
    for (const int beat : { 0, 2 })
        if (beat < beats)
            rock.notes.push_back(drumNote(beat * ppq, hitDuration, 36, 112));
    for (const int beat : { 1, 3 })
        if (beat < beats)
            rock.notes.push_back(drumNote(beat * ppq, hitDuration, 38, 108));

    BuiltInPattern edm { "Four-on-the-floor EDM Beat", {} };
    for (int beat = 0; beat < beats; ++beat)
        edm.notes.push_back(drumNote(beat * ppq, hitDuration, 36, 116));
    for (const int beat : { 1, 3 })
        if (beat < beats)
            edm.notes.push_back(drumNote(beat * ppq, hitDuration, 39, 104));
    for (int beat = 0; beat < beats; ++beat)
        edm.notes.push_back(drumNote(
            beat * ppq + eighth, hitDuration, 42, 88));

    BuiltInPattern hipHop { "Basic Hip-Hop Beat", {} };
    hipHop.notes.push_back(drumNote(0, hitDuration, 36, 114));
    if (beats >= 3)
        hipHop.notes.push_back(drumNote(
            2 * ppq + eighth, hitDuration, 36, 106));
    for (const int beat : { 1, 3 })
        if (beat < beats)
            hipHop.notes.push_back(drumNote(beat * ppq, hitDuration, 38, 110));
    for (int tick = 0; tick < beats * ppq; tick += eighth)
        hipHop.notes.push_back(drumNote(tick, hitDuration, 42, 76));

    return { std::move(rock), std::move(edm), std::move(hipHop) };
}
}
