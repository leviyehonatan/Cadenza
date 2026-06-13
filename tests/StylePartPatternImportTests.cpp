#include "Arranger/PatternNoteMerge.h"
#include "UI/StylePartPatternImport.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using cadenza::arranger::NoteRole;
using cadenza::arranger::PatternNote;
using cadenza::arranger::mergePatternNote;
using namespace cadenza::ui::pattern_import;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

PatternNote note(int tick, int duration, int pitch, int velocity = 100)
{
    return PatternNote { tick, duration, pitch, velocity,
                         NoteRole::Absolute, 0 };
}

void testPercussionDuplicateUpdatesExistingHit()
{
    std::vector<PatternNote> notes { note(0, 60, 36, 80) };

    const auto result = mergePatternNote(notes, note(0, 120, 36, 110), true);

    expect(notes.size() == 1, "percussion merge does not stack duplicate hits");
    expect(notes[0].duration == 120 && notes[0].velocity == 110,
           "percussion merge updates duration and velocity");
    expect(result.noteIndex == 0 && result.erasedIndices.empty(),
           "percussion merge reports the updated note index");
}

void testMelodicOverlapUsesRecorderCleanupRule()
{
    std::vector<PatternNote> notes {
        note(0, 480, 60),
        note(600, 240, 60),
        note(0, 240, 64)
    };

    const auto result = mergePatternNote(notes, note(240, 600, 60, 95), false);

    expect(notes.size() == 3, "melodic merge replaces fully covered note");
    expect(notes[0].tick == 0 && notes[0].duration == 240,
           "melodic merge trims an earlier same-pitch overlap");
    expect(notes[1].pitch == 64, "melodic merge preserves unrelated notes");
    expect(notes[2].tick == 240 && notes[2].duration == 600,
           "melodic merge appends the incoming note");
    expect(result.noteIndex == 2 && result.erasedIndices == std::vector<int> { 1 },
           "melodic merge reports erased and inserted indices");
}

void addMidiNote(juce::MidiMessageSequence& track, int tick, int duration,
                 int pitch, int velocity = 100)
{
    auto on = juce::MidiMessage::noteOn(
        1, pitch, static_cast<juce::uint8>(velocity));
    on.setTimeStamp(tick);
    track.addEvent(on);
    auto off = juce::MidiMessage::noteOff(1, pitch);
    off.setTimeStamp(tick + duration);
    track.addEvent(off);
}

void testOneTrackMidiImportAndPpqScaling()
{
    juce::MidiMessageSequence track;
    addMidiNote(track, 0, 120, 36, 110);
    addMidiNote(track, 480, 240, 38, 90);
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    file.addTrack(track);

    const auto parsed = parseMidiPattern(file, 960);

    expect(parsed.ok() && parsed.notes.size() == 2,
           "one-track MIDI imports note events");
    expect(parsed.notes[0].tick == 0 && parsed.notes[0].duration == 240,
           "MIDI duration scales into editor PPQ");
    expect(parsed.notes[1].tick == 960 && parsed.notes[1].duration == 480,
           "MIDI timing scales into editor PPQ");
    expect(parsed.notes[1].pitch == 38 && parsed.notes[1].velocity == 90,
           "MIDI pitch and velocity are preserved");
}

void testMultipleTracksChooseFirstNoteTrack()
{
    juce::MidiMessageSequence metadata;
    auto tempo = juce::MidiMessage::tempoMetaEvent(500000);
    tempo.setTimeStamp(0);
    metadata.addEvent(tempo);
    juce::MidiMessageSequence firstNotes;
    addMidiNote(firstNotes, 120, 120, 60);
    juce::MidiMessageSequence laterNotes;
    addMidiNote(laterNotes, 0, 120, 72);

    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    file.addTrack(metadata);
    file.addTrack(firstNotes);
    file.addTrack(laterNotes);

    const auto parsed = parseMidiPattern(file, 480);

    expect(parsed.ok() && parsed.notes.size() == 1,
           "multiple-track import uses one note-bearing track");
    expect(parsed.notes[0].pitch == 60 && parsed.notes[0].tick == 120,
           "first note-bearing track wins");
}

void testDestinationPriority()
{
    constexpr int barTicks = 3840;
    const int playhead = 3 * barTicks + 20;
    expect(resolveDestinationBar({ 2, 2, 4 }, playhead, 8, barTicks) == 2,
           "selected bar has first destination priority");
    expect(resolveDestinationBar({}, playhead, 8, barTicks) == 3,
           "playhead bar has second destination priority");
    expect(resolveDestinationBar({}, -1, 8, barTicks) == 0,
           "bar one is the destination fallback");
}

void testSelectedBarOffsetsImportedPattern()
{
    constexpr int barTicks = 3840;
    const int destinationBar = resolveDestinationBar(
        { 2, 2, 2 }, 0, 4, barTicks);
    const auto result = insertPattern(
        {}, { note(120, 240, 60) },
        destinationBar * barTicks, 4 * barTicks, false);

    expect(result.notes.size() == 1
               && result.notes[0].tick == 2 * barTicks + 120,
           "selected bar offsets imported pattern timing");
}

void testImportOverdubsAndSelectsImportedNotes()
{
    const std::vector<PatternNote> existing {
        note(100, 120, 50, 70),
        note(1920, 60, 36, 60)
    };
    const std::vector<PatternNote> imported {
        note(0, 120, 36, 115),
        note(480, 120, 38, 100)
    };

    const auto result = insertPattern(existing, imported, 1920, 7680, true);

    expect(result.notes.size() == 3,
           "drum duplicate import updates rather than stacks");
    expect(result.notes[0].tick == 100 && result.notes[0].pitch == 50
               && result.notes[0].velocity == 70,
           "import leaves unrelated existing notes untouched");
    expect(result.notes[1].tick == 1920 && result.notes[1].pitch == 36
               && result.notes[1].velocity == 115
               && result.notes[1].duration == 120,
           "same drum pitch and tick is replaced by imported hit");
    expect(result.notes[2].tick == 2400 && result.notes[2].pitch == 38,
           "imported timing is offset to destination");
    expect(result.selection
               == cadenza::ui::note_workflow::NoteSelection({ 1, 2 }),
           "updated and appended imported notes become selected");
}

void testImportClampsAtPatternEnd()
{
    const std::vector<PatternNote> imported {
        note(0, 100, 60),
        note(100, 100, 64)
    };

    const auto result = insertPattern({}, imported, 3800, 3840, false);

    expect(result.notes.size() == 1,
           "notes starting beyond pattern end are discarded");
    expect(result.notes[0].tick == 3800 && result.notes[0].duration == 40,
           "note crossing pattern end is trimmed");
    expect(result.selection
               == cadenza::ui::note_workflow::NoteSelection { 0 },
           "remaining clamped import is selected");
}

void testDrumDuplicateUsesQuantizedGridPosition()
{
    const std::vector<PatternNote> existing {
        note(1930, 60, 36, 70)
    };
    const std::vector<PatternNote> imported {
        note(0, 120, 36, 118)
    };

    const auto result = insertPattern(
        existing, imported, 1920, 7680, true, 240);

    expect(result.notes.size() == 1,
           "drum hits in the same quantized cell do not stack");
    expect(result.notes[0].velocity == 118
               && result.notes[0].duration == 120,
           "quantized drum duplicate is updated by import");
    expect(result.selection
               == cadenza::ui::note_workflow::NoteSelection { 0 },
           "updated quantized duplicate becomes selected");
}

const BuiltInPattern* findPreset(
    const std::vector<BuiltInPattern>& patterns,
    const std::string& name)
{
    const auto it = std::find_if(
        patterns.begin(), patterns.end(),
        [&name](const BuiltInPattern& pattern) { return pattern.name == name; });
    return it == patterns.end() ? nullptr : &*it;
}

bool containsNote(const BuiltInPattern& pattern, int tick, int pitch)
{
    return std::any_of(
        pattern.notes.begin(), pattern.notes.end(),
        [tick, pitch](const PatternNote& value) {
            return value.tick == tick && value.pitch == pitch;
        });
}

void testBuiltInPatternsContainOriginalFoundationGrooves()
{
    constexpr int ppq = 960;
    const auto patterns = builtInPatterns(ppq, 4);

    expect(patterns.size() == 3, "three built-in patterns are available");
    const auto* rock = findPreset(patterns, "Basic Rock Drum Beat");
    expect(rock != nullptr, "basic rock pattern is named");
    expect(findPreset(patterns, "Four-on-the-floor EDM Beat") != nullptr,
           "EDM pattern is named");
    expect(findPreset(patterns, "Basic Hip-Hop Beat") != nullptr,
           "hip-hop pattern is named");
    expect(containsNote(*rock, 0, 36) && containsNote(*rock, 2 * ppq, 36),
           "rock pattern has kick on beats one and three");
    expect(containsNote(*rock, ppq, 38) && containsNote(*rock, 3 * ppq, 38),
           "rock pattern has snare on beats two and four");
    for (int eighth = 0; eighth < 8; ++eighth)
        expect(containsNote(*rock, eighth * (ppq / 2), 42),
               "rock pattern has closed-hat eighth notes");
}
}

int main()
{
    testPercussionDuplicateUpdatesExistingHit();
    testMelodicOverlapUsesRecorderCleanupRule();
    testOneTrackMidiImportAndPpqScaling();
    testMultipleTracksChooseFirstNoteTrack();
    testDestinationPriority();
    testSelectedBarOffsetsImportedPattern();
    testImportOverdubsAndSelectsImportedNotes();
    testImportClampsAtPatternEnd();
    testDrumDuplicateUsesQuantizedGridPosition();
    testBuiltInPatternsContainOriginalFoundationGrooves();
    std::cout << "All MIDI pattern import tests passed\n";
    return 0;
}
