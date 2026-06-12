// ArrangerMidiRouter tests — adopted from arranger_cleanroom.
// Original 32 cases, plus a new case for the split-note-itself edge.

#include "Midi/ArrangerMidiRouter.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace arranger;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void require_chord(
    const std::optional<ChordRecognitionResult>& chord,
    std::uint8_t root,
    const std::string& quality,
    std::optional<std::uint8_t> bass,
    bool inversion,
    const char* message)
{
    require(chord.has_value(), message);
    std::string rootMessage = std::string{message} + ": root matches";
    std::string qualityMessage = std::string{message} + ": quality matches";
    std::string bassMessage = std::string{message} + ": bass matches";
    std::string inversionMessage = std::string{message} + ": inversion flag matches";
    require(chord->root == root, rootMessage.c_str());
    require(chord->quality == quality, qualityMessage.c_str());
    require(chord->bass == bass, bassMessage.c_str());
    require(chord->inversion == inversion, inversionMessage.c_str());
}

void require_chord_display(
    const std::optional<ChordRecognitionResult>& chord,
    const std::string& displayName,
    const char* message)
{
    require(chord.has_value(), message);
    std::string displayMessage = std::string{message} + ": display name matches";
    require(chord->displayName == displayName, displayMessage.c_str());
}

void play_notes(ArrangerMidiRouter& router, const std::vector<std::uint8_t>& notes)
{
    for (const auto note : notes) {
        router.handle(MidiMessage::noteOn(0, note, 100));
    }
}

void release_notes(ArrangerMidiRouter& router, const std::vector<std::uint8_t>& notes)
{
    for (const auto note : notes) {
        router.handle(MidiMessage::noteOff(0, note));
    }
}

void below_split_note_routes_to_chord_side_and_starts_syncro()
{
    ArrangerMidiRouter router({60, ChordDetectionMode::Fingered, false});
    std::vector<std::size_t> counts;
    std::vector<SyncEvent> syncEvents;

    router.addNoteCountObserver([&](std::size_t count) { counts.push_back(count); });
    router.addSyncObserver([&](SyncEvent event) { syncEvents.push_back(event); });

    const auto routed = router.handle(MidiMessage::noteOn(0, 48, 100));

    require(routed.size() == 1, "one routed event is returned");
    require(routed[0].target == RouteTarget::ChordSide, "below split routes to chord side");
    require(router.isChordNoteActive(48), "below split note is tracked as chord note");
    require(router.activeChordNoteCount() == 1, "one unique chord note is active");
    require(counts == std::vector<std::size_t>{1}, "note count observer receives first count");
    require(syncEvents == std::vector<SyncEvent>{SyncEvent::Started}, "syncro-start fires on first chord note");
}

void note_on_split_point_routes_to_chord_side()
{
    // New test for the `<=` split-note convention.
    ArrangerMidiRouter router({60, ChordDetectionMode::Fingered, false});
    const auto routed = router.handle(MidiMessage::noteOn(0, 60, 100));

    require(routed[0].target == RouteTarget::ChordSide, "note ON the split point routes to chord side");
    require(router.isChordNoteActive(60), "split-point note is tracked as chord note");
    require(router.activeChordNoteCount() == 1, "split-point note increments chord note count");
}

void above_split_note_routes_to_melody_side_in_non_full_keyboard_mode()
{
    ArrangerMidiRouter router({60, ChordDetectionMode::Fingered, false});
    std::vector<std::size_t> counts;
    std::vector<SyncEvent> syncEvents;

    router.addNoteCountObserver([&](std::size_t count) { counts.push_back(count); });
    router.addSyncObserver([&](SyncEvent event) { syncEvents.push_back(event); });

    const auto routed = router.handle(MidiMessage::noteOn(0, 72, 100));

    require(routed[0].target == RouteTarget::MelodySide, "above split routes to melody side");
    require(router.isMelodyNoteActive(72), "above split note is tracked as melody note");
    require(!router.isChordNoteActive(72), "above split note is not tracked as chord note");
    require(router.activeChordNoteCount() == 0, "melody notes do not change chord note count");
    require(counts.empty(), "melody note does not notify chord note count observers");
    require(syncEvents.empty(), "melody note does not start syncro");
}

void full_keyboard_modes_route_above_split_notes_to_chord_side()
{
    for (const auto mode : {ChordDetectionMode::FullKeyboard, ChordDetectionMode::FullKeyboardNoInterval}) {
        ArrangerMidiRouter router({60, mode, false});
        const auto routed = router.handle(MidiMessage::noteOn(0, 72, 100));

        require(routed[0].target == RouteTarget::ChordSide, "full keyboard mode routes above split note to chord side");
        require(router.isChordNoteActive(72), "full keyboard above split note is tracked as chord note");
        require(router.activeChordNoteCount() == 1, "full keyboard note increments chord note count");
    }
}

void note_off_releases_chord_note_and_stops_syncro_when_empty()
{
    ArrangerMidiRouter router({60, ChordDetectionMode::Fingered, false});
    std::vector<std::size_t> counts;
    std::vector<SyncEvent> syncEvents;

    router.addNoteCountObserver([&](std::size_t count) { counts.push_back(count); });
    router.addSyncObserver([&](SyncEvent event) { syncEvents.push_back(event); });

    router.handle(MidiMessage::noteOn(0, 48, 100));
    const auto routed = router.handle(MidiMessage::noteOff(0, 48));

    require(routed[0].target == RouteTarget::ChordSide, "note-off follows chord-side route");
    require(!router.isChordNoteActive(48), "note-off releases chord note");
    require(router.activeChordNoteCount() == 0, "all chord notes are released");
    require((counts == std::vector<std::size_t>{1, 0}), "count observer receives start and reset counts");
    require((syncEvents == std::vector<SyncEvent>{SyncEvent::Started, SyncEvent::Stopped}), "syncro stop fires when final chord note is released");
}

void zero_velocity_note_on_is_treated_as_note_off()
{
    ArrangerMidiRouter router({60, ChordDetectionMode::Fingered, false});

    router.handle(MidiMessage::noteOn(0, 48, 100));
    router.handle(MidiMessage::noteOn(0, 48, 0));

    require(!router.isChordNoteActive(48), "zero velocity note-on releases the note");
    require(router.activeChordNoteCount() == 0, "zero velocity note-on updates chord note count");
    require(!router.state().syncroStarted, "zero velocity note-on can stop syncro");
}

void duplicate_note_on_uses_per_note_counter_but_not_duplicate_unique_count()
{
    ArrangerMidiRouter router({60, ChordDetectionMode::Fingered, false});
    std::vector<std::size_t> counts;
    std::vector<SyncEvent> syncEvents;

    router.addNoteCountObserver([&](std::size_t count) { counts.push_back(count); });
    router.addSyncObserver([&](SyncEvent event) { syncEvents.push_back(event); });

    router.handle(MidiMessage::noteOn(0, 48, 100));
    router.handle(MidiMessage::noteOn(0, 48, 110));
    router.handle(MidiMessage::noteOff(0, 48));

    require(router.isChordNoteActive(48), "first release leaves duplicated note active");
    require(router.activeChordNoteCount() == 1, "duplicate note-on does not duplicate unique count");
    require(counts == std::vector<std::size_t>{1}, "duplicate note-on does not notify unchanged unique count");
    require(syncEvents == std::vector<SyncEvent>{SyncEvent::Started}, "duplicate note-on does not duplicate syncro-start");

    router.handle(MidiMessage::noteOff(0, 48));

    require(!router.isChordNoteActive(48), "second release clears duplicated note");
    require((counts == std::vector<std::size_t>{1, 0}), "final release notifies reset count");
    require((syncEvents == std::vector<SyncEvent>{SyncEvent::Started, SyncEvent::Stopped}), "final release stops syncro");
}

void reset_clears_active_notes_and_stops_syncro()
{
    ArrangerMidiRouter router({60, ChordDetectionMode::FullKeyboard, false});
    std::vector<std::size_t> counts;
    std::vector<SyncEvent> syncEvents;

    router.addNoteCountObserver([&](std::size_t count) { counts.push_back(count); });
    router.addSyncObserver([&](SyncEvent event) { syncEvents.push_back(event); });

    router.handle(MidiMessage::noteOn(0, 48, 100));
    router.handle(MidiMessage::noteOn(0, 72, 100));
    router.reset();

    require(router.activeChordNoteCount() == 0, "reset clears chord note count");
    require(!router.isChordNoteActive(48), "reset clears low chord note");
    require(!router.isChordNoteActive(72), "reset clears full keyboard chord note");
    require(!router.state().syncroStarted, "reset stops syncro");
    require((counts == std::vector<std::size_t>{1, 2, 0}), "reset notifies final zero count");
    require((syncEvents == std::vector<SyncEvent>{SyncEvent::Started, SyncEvent::Stopped}), "reset emits syncro stop");
}

void detects_c_major()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 52, 55});

    require_chord(router.detectChord(), 0, "major", std::nullopt, false, "C major is detected");
}

void detects_c_minor()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 51, 55});

    require_chord(router.detectChord(), 0, "m", std::nullopt, false, "C minor is detected");
}

void detects_c7_full()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 52, 55, 58});

    require_chord(router.detectChord(), 0, "7", std::nullopt, false, "C7 is detected");
}

void detects_c7_omitted_fifth()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 52, 58});

    require_chord(router.detectChord(), 0, "7", std::nullopt, false, "C7 omitted fifth is detected");
}

void detects_cmaj7()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 52, 55, 59});

    require_chord(router.detectChord(), 0, "maj7", std::nullopt, false, "Cmaj7 is detected");
}

void detects_cdim()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 51, 54});

    require_chord(router.detectChord(), 0, "dim", std::nullopt, false, "Cdim is detected");
}

void detects_caug()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 52, 56});

    require_chord(router.detectChord(), 0, "aug", std::nullopt, false, "Caug is detected");
}

void detects_csus4_full()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 53, 55});

    require_chord(router.detectChord(), 0, "sus4", std::nullopt, false, "Csus4 is detected");
}

void detects_csus4_reduced()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 53});

    require_chord(router.detectChord(), 0, "sus4", std::nullopt, false, "reduced Csus4 is detected");
}

void detects_csus2_reduced()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 50});

    require_chord(router.detectChord(), 0, "sus2", std::nullopt, false, "reduced Csus2 is detected");
}

void detects_cm7_full()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 51, 55, 58});

    require_chord(router.detectChord(), 0, "m7", std::nullopt, false, "Cm7 is detected");
}

void detects_cm7_omitted_fifth()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 51, 58});

    require_chord(router.detectChord(), 0, "m7", std::nullopt, false, "Cm7 omitted fifth is detected");
}

void detects_c_over_e_inversion()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {52, 55, 60});

    require_chord(router.detectChord(), 0, "major", 4, true, "C/E inversion is detected");
}

void full_keyboard_above_split_detects_chord()
{
    ArrangerMidiRouter router({60, ChordDetectionMode::FullKeyboard, false});
    play_notes(router, {72, 76, 79});

    require_chord(router.detectChord(), 0, "major", std::nullopt, false, "full keyboard notes above split detect a chord");
}

void fingered_on_bass_detects_c_over_e()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::FingeredOnBass, false});
    play_notes(router, {52, 55, 60});

    const auto chord = router.detectChord();
    require_chord(chord, 0, "major", 4, true, "FingeredOnBass detects C/E");
    require_chord_display(chord, "C/E", "FingeredOnBass displays C/E");
}

void fingered_on_bass_detects_c_over_g()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::FingeredOnBass, false});
    play_notes(router, {55, 60, 64});

    const auto chord = router.detectChord();
    require_chord(chord, 0, "major", 7, true, "FingeredOnBass detects C/G");
    require_chord_display(chord, "C/G", "FingeredOnBass displays C/G");
}

void fingered_on_bass_detects_am_over_g()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::FingeredOnBass, false});
    play_notes(router, {55, 57, 60, 64});

    const auto chord = router.detectChord();
    require_chord(chord, 9, "m", 7, true, "FingeredOnBass detects Am/G");
    require_chord_display(chord, "Am/G", "FingeredOnBass displays Am/G");
}

void fingered_on_bass_detects_dm_over_f()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::FingeredOnBass, false});
    play_notes(router, {53, 57, 62});

    const auto chord = router.detectChord();
    require_chord(chord, 2, "m", 5, true, "FingeredOnBass detects Dm/F");
    require_chord_display(chord, "Dm/F", "FingeredOnBass displays Dm/F");
}

void fingered_on_bass_detects_g7_over_b()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::FingeredOnBass, false});
    play_notes(router, {47, 53, 55, 62});

    const auto chord = router.detectChord();
    require_chord(chord, 7, "7", 11, true, "FingeredOnBass detects G7/B");
    require_chord_display(chord, "G7/B", "FingeredOnBass displays G7/B");
}

void fingered_mode_still_returns_normal_inversion_info()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {52, 55, 60});

    const auto chord = router.detectChord();
    require_chord(chord, 0, "major", 4, true, "Fingered mode keeps inversion metadata");
    require_chord_display(chord, "C", "Fingered mode does not display slash chord");
}

void fingered_on_bass_mode_returns_explicit_bass_slash_chord()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::FingeredOnBass, false});
    play_notes(router, {52, 55, 60});

    const auto chord = router.detectChord();
    require_chord(chord, 0, "major", 4, true, "FingeredOnBass keeps bass metadata");
    require_chord_display(chord, "C/E", "FingeredOnBass displays slash chord");
}

void chord_memory_off_clears_chord_on_release()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false, false, true});
    play_notes(router, {48, 52, 55});
    require_chord(router.detectChord(), 0, "major", std::nullopt, false, "Chord memory off detects C");

    release_notes(router, {48, 52, 55});

    require(!router.detectChord().has_value(), "Chord memory off clears chord after all notes release");
}

void chord_memory_on_keeps_c_after_release()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false, true, true});
    play_notes(router, {48, 52, 55});
    require_chord(router.detectChord(), 0, "major", std::nullopt, false, "Chord memory on detects C");

    release_notes(router, {48, 52, 55});

    require_chord(router.detectChord(), 0, "major", std::nullopt, false, "Chord memory on keeps C after release");
    require_chord_display(router.detectChord(), "C", "Chord memory on keeps C display");
}

void chord_memory_on_replaces_c_with_g7()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false, true, true});
    play_notes(router, {48, 52, 55});
    require_chord_display(router.detectChord(), "C", "Chord memory starts with C");
    release_notes(router, {48, 52, 55});

    play_notes(router, {43, 47, 50, 53});

    require_chord(router.detectChord(), 7, "7", std::nullopt, false, "Chord memory replaces C with G7");
    require_chord_display(router.detectChord(), "G7", "Chord memory displays G7");
}

void invalid_notes_do_not_erase_remembered_chord()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false, true, true});
    play_notes(router, {48, 52, 55});
    release_notes(router, {48, 52, 55});
    require_chord_display(router.detectChord(), "C", "Remembered chord starts as C");

    // A chromatic cluster matches no chord type. (The previous notes C#-D-F#
    // now legitimately read as Dmaj7 with omitted 5th under Yamaha fingering.)
    play_notes(router, {49, 50, 51});

    require_chord(router.detectChord(), 0, "major", std::nullopt, false, "Invalid notes keep remembered C");
    require_chord_display(router.detectChord(), "C", "Invalid notes keep remembered C display");
}

void chord_detected_incrementally_completing_triad()
{
    // Matches the C-E-G acceptance test: chord is only detected when the triad is complete.
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});

    router.handle(MidiMessage::noteOn(0, 48, 100)); // C3
    require(!router.detectChord().has_value(), "single note: no chord yet");

    router.handle(MidiMessage::noteOn(0, 52, 100)); // E3
    require(!router.detectChord().has_value(), "two notes {C,E}: no chord yet (no 2-note template)");

    router.handle(MidiMessage::noteOn(0, 55, 100)); // G3
    require_chord_display(router.detectChord(), "C", "C-E-G triad: detects C major");
}

void chord_changes_from_c_to_g7_on_completion()
{
    // Matches the G-B-D-F acceptance test.
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
    play_notes(router, {48, 52, 55});
    require_chord_display(router.detectChord(), "C", "starts with C major");

    release_notes(router, {48, 52, 55});
    require(!router.detectChord().has_value(), "released: no chord");

    // G2=43, B2=47, D3=50, F3=53
    play_notes(router, {43, 47, 50, 53});
    require_chord_display(router.detectChord(), "G7", "G-B-D-F: detects G7");
}

void syncro_stop_disabled_does_not_stop_on_release()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false, false, false});
    std::vector<SyncEvent> syncEvents;
    router.addSyncObserver([&](SyncEvent ev) { syncEvents.push_back(ev); });

    play_notes(router, {48, 52, 55});
    release_notes(router, {48, 52, 55});

    require((syncEvents == std::vector<SyncEvent>{SyncEvent::Started}),
            "syncro stop disabled: only Started fires, no Stopped");
}

void syncro_stop_can_be_toggled_at_runtime()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false, false, false});
    std::vector<SyncEvent> syncEvents;
    router.addSyncObserver([&](SyncEvent ev) { syncEvents.push_back(ev); });

    play_notes(router, {48, 52, 55});
    release_notes(router, {48, 52, 55});
    require((syncEvents == std::vector<SyncEvent>{SyncEvent::Started}), "stop disabled: no stop");

    // Enable syncro stop at runtime. syncroStarted is still true (never stopped),
    // so no new Started fires — but now releasing the next chord fires Stopped.
    router.setSyncroStopOnRelease(true);
    play_notes(router, {48, 52, 55});
    release_notes(router, {48, 52, 55});
    require((syncEvents == std::vector<SyncEvent>{SyncEvent::Started, SyncEvent::Stopped}),
            "after enabling at runtime: Stopped fires on release");
}

void syncro_stop_behavior_remains_separate_from_chord_memory()
{
    ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false, true, true});
    std::vector<SyncEvent> syncEvents;
    router.addSyncObserver([&](SyncEvent event) { syncEvents.push_back(event); });

    play_notes(router, {48, 52, 55});
    release_notes(router, {48, 52, 55});

    require_chord_display(router.detectChord(), "C", "Chord memory survives syncro stop");
    require(!router.state().syncroStarted, "Syncro stop still clears syncroStarted with chord memory on");
    require((syncEvents == std::vector<SyncEvent>{SyncEvent::Started, SyncEvent::Stopped}), "Syncro stop remains independent from chord memory");
}

void releasing_a_held_triad_finger_by_finger_keeps_the_chord()
{
    // Hold Am (A2,C3,E3) below split, then lift fingers one at a time. The chord
    // must stay Am instead of downgrading to a 2-note / 1-note partial.
    ArrangerMidiRouter router({60, ChordDetectionMode::Fingered, false});

    play_notes(router, {45, 48, 52});            // A2, C3, E3
    auto full = router.detectChord();
    require(full.has_value(), "Am triad detected");
    const std::string held = full->displayName;

    release_notes(router, {48});                 // lift the 3rd -> A,E remain
    auto afterThird = router.detectChord();
    require(afterThird.has_value() && afterThird->displayName == held,
            "chord kept after releasing the 3rd (no downgrade)");

    release_notes(router, {52});                 // lift the 5th -> only A remains
    auto afterFifth = router.detectChord();
    require(afterFifth.has_value() && afterFifth->displayName == held,
            "chord kept after releasing the 5th (no downgrade)");
}

} // namespace

// Extended Yamaha chord types in the live router: voicings that previously
// matched nothing (full 9ths with the 5th, 6ths, m7b5) now detect, and the
// C6/Am7 ambiguity resolves by the bass note.
void detects_extended_yamaha_types()
{
    {
        ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
        play_notes(router, {48, 50, 52, 55, 58});   // full C9 with the 5th
        require_chord(router.detectChord(), 0, "7(9)", std::nullopt, false,
                      "full C9 voicing is detected");
    }
    {
        ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
        play_notes(router, {48, 52, 55, 57});       // C E G A, C in bass
        require_chord(router.detectChord(), 0, "6", std::nullopt, false,
                      "C6 with C bass is C6");
    }
    {
        ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
        play_notes(router, {45, 48, 52, 55});       // A C E G, A in bass
        require_chord(router.detectChord(), 9, "m7", std::nullopt, false,
                      "same notes with A bass are Am7");
    }
    {
        ArrangerMidiRouter router({72, ChordDetectionMode::Fingered, false});
        play_notes(router, {48, 51, 54, 58});       // C Eb Gb Bb
        require_chord(router.detectChord(), 0, "m7b5", std::nullopt, false,
                      "Cm7b5 is detected");
    }
}

int main()
{
    below_split_note_routes_to_chord_side_and_starts_syncro();
    note_on_split_point_routes_to_chord_side();
    above_split_note_routes_to_melody_side_in_non_full_keyboard_mode();
    full_keyboard_modes_route_above_split_notes_to_chord_side();
    note_off_releases_chord_note_and_stops_syncro_when_empty();
    zero_velocity_note_on_is_treated_as_note_off();
    duplicate_note_on_uses_per_note_counter_but_not_duplicate_unique_count();
    reset_clears_active_notes_and_stops_syncro();
    detects_c_major();
    detects_c_minor();
    detects_c7_full();
    detects_c7_omitted_fifth();
    detects_cmaj7();
    detects_cdim();
    detects_caug();
    detects_csus4_full();
    detects_csus4_reduced();
    detects_csus2_reduced();
    detects_cm7_full();
    detects_cm7_omitted_fifth();
    detects_c_over_e_inversion();
    full_keyboard_above_split_detects_chord();
    fingered_on_bass_detects_c_over_e();
    fingered_on_bass_detects_c_over_g();
    fingered_on_bass_detects_am_over_g();
    fingered_on_bass_detects_dm_over_f();
    fingered_on_bass_detects_g7_over_b();
    fingered_mode_still_returns_normal_inversion_info();
    fingered_on_bass_mode_returns_explicit_bass_slash_chord();
    chord_memory_off_clears_chord_on_release();
    chord_memory_on_keeps_c_after_release();
    chord_memory_on_replaces_c_with_g7();
    invalid_notes_do_not_erase_remembered_chord();
    syncro_stop_behavior_remains_separate_from_chord_memory();
    chord_detected_incrementally_completing_triad();
    chord_changes_from_c_to_g7_on_completion();
    syncro_stop_disabled_does_not_stop_on_release();
    syncro_stop_can_be_toggled_at_runtime();
    releasing_a_held_triad_finger_by_finger_keeps_the_chord();
    detects_extended_yamaha_types();

    std::cout << "ArrangerMidiRouter tests passed (39 cases)\n";
    return 0;
}
