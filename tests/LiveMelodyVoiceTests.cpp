// LiveMelodyVoiceTests — live right-hand melody routing/instrument logic.

#include "Midi/LiveMelodyVoice.h"
#include "Audio/MidiChannel.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

using cadenza::midi::LiveMelodyVoice;
using cadenza::midi::gmProgramForBankName;
using cadenza::audio::kLiveMelodyChannel;

void melodyNoteSoundsOnDedicatedChannelShifted()
{
    LiveMelodyVoice v;
    v.setOctave(1);

    const auto on = v.handleNote(60, 100, /*isOn=*/true, /*melodyZone=*/true);
    expect(on.has_value(), "melody note-on produces an event");
    expect(on->channel == kLiveMelodyChannel, "melody plays on the dedicated melody channel");
    expect(kLiveMelodyChannel != 10, "melody channel is not the drum channel 10");
    expect(on->note == 72, "octave +1 shifts 60 -> 72");
    expect(on->velocity == 100 && on->isOn, "note-on keeps velocity, isOn=true");
}

void matchingNoteOffUsesShiftedPitch()
{
    LiveMelodyVoice v;
    v.setOctave(1);
    v.handleNote(60, 100, true, true);            // sounds 72

    const auto off = v.handleNote(60, 0, /*isOn=*/false, /*melodyZone=*/true);
    expect(off.has_value(), "melody note-off produces an event");
    expect(off->note == 72 && !off->isOn, "note-off releases the same shifted pitch (72)");
}

void octaveChangeMidHoldStillReleasesHeldPitch()
{
    LiveMelodyVoice v;
    v.setOctave(1);
    v.handleNote(64, 100, true, true);            // sounds 76
    v.setOctave(-1);                              // user changes octave while holding
    const auto off = v.handleNote(64, 0, false, true);
    expect(off.has_value() && off->note == 76, "note-off releases the originally-sounded pitch, no stuck note");
}

void chordZoneNoteMakesNoMelodySound()
{
    LiveMelodyVoice v;
    const auto on = v.handleNote(48, 100, true, /*melodyZone=*/false);
    expect(!on.has_value(), "chord-zone note-on makes no live melody sound");
    const auto off = v.handleNote(48, 0, false, false);
    expect(!off.has_value(), "chord-zone note-off makes no live melody sound");
}

void octaveDirectionsAndClamping()
{
    {
        LiveMelodyVoice v; v.setOctave(0);
        expect(v.handleNote(60, 100, true, true)->note == 60, "octave 0 keeps pitch");
    }
    {
        LiveMelodyVoice v; v.setOctave(-1);
        expect(v.handleNote(60, 100, true, true)->note == 48, "octave -1 shifts -12");
    }
    {
        LiveMelodyVoice v; v.setOctave(1);
        expect(v.handleNote(120, 100, true, true)->note == 127, "shift clamps to 127");
    }
    {
        LiveMelodyVoice v; v.setOctave(-1);
        expect(v.handleNote(5, 100, true, true)->note == 0, "shift clamps to 0");
    }
}

void octaveChangeAffectsSubsequentLiveNotes()
{
    // Mirrors the acceptance test: C5 (MIDI 72) above the split.
    LiveMelodyVoice v;

    v.setOctave(0);
    expect(v.handleNote(72, 100, true, true)->note == 72, "octave 0: C5 sounds 72");
    v.handleNote(72, 0, false, true);

    v.setOctave(1);
    expect(v.handleNote(72, 100, true, true)->note == 84, "octave +1: same key now sounds 84");
    v.handleNote(72, 0, false, true);

    v.setOctave(-1);
    expect(v.handleNote(72, 100, true, true)->note == 60, "octave -1: same key now sounds 60");
    v.handleNote(72, 0, false, true);
}

void transposeShiftsTheRightHandMelody()
{
    LiveMelodyVoice v;
    v.setTranspose(1);
    expect(v.handleNote(60, 100, true, true)->note == 61, "transpose +1: 60 sounds 61");
    v.handleNote(60, 0, false, true);

    v.setTranspose(-2);
    expect(v.handleNote(60, 100, true, true)->note == 58, "transpose -2: 60 sounds 58");
    v.handleNote(60, 0, false, true);
}

void transposeAndOctaveCombine()
{
    LiveMelodyVoice v;
    v.setOctave(1);        // +12
    v.setTranspose(2);     // +2
    expect(v.handleNote(60, 100, true, true)->note == 74, "octave +1 and transpose +2: 60 -> 74");
}

void transposedNoteOffReleasesSamePitch()
{
    LiveMelodyVoice v;
    v.setTranspose(3);
    v.handleNote(60, 100, true, true);   // sounds 63
    v.setTranspose(0);                   // change transpose while held
    const auto off = v.handleNote(60, 0, false, true);
    expect(off.has_value() && off->note == 63, "note-off releases the originally-sounded transposed pitch");
}

void resetClearsHeldNotes()
{
    LiveMelodyVoice v;
    v.setOctave(2);
    v.handleNote(60, 100, true, true);
    v.reset();
    const auto off = v.handleNote(60, 0, false, true);
    expect(!off.has_value(), "after reset, a dangling note-off produces nothing");
}

void bankNamesMapToGmPrograms()
{
    expect(gmProgramForBankName("Piano") == 0,    "Piano -> 0");
    expect(gmProgramForBankName("Rhodes") == 4,   "Rhodes -> 4");
    expect(gmProgramForBankName("Organ") == 16,   "Organ -> 16");
    expect(gmProgramForBankName("Alto Sax") == 65, "Alto Sax -> 65");
    expect(gmProgramForBankName("Trumpet") == 56, "Trumpet -> 56");
    expect(gmProgramForBankName("N. Guitar") == 24, "N. Guitar -> 24");
    expect(gmProgramForBankName("totally unknown voice") == 0, "unknown -> 0 (Acoustic Grand fallback)");
}

void dedicatedChannelIsConfigurable()
{
    LiveMelodyVoice def;
    expect(def.channel() == kLiveMelodyChannel, "default channel is kLiveMelodyChannel");

    LiveMelodyVoice custom(4);
    expect(custom.channel() == 4, "channel is configurable");
    expect(custom.handleNote(60, 90, true, true)->channel == 4, "events use the configured channel");
}
}

int main()
{
    melodyNoteSoundsOnDedicatedChannelShifted();
    matchingNoteOffUsesShiftedPitch();
    octaveChangeMidHoldStillReleasesHeldPitch();
    chordZoneNoteMakesNoMelodySound();
    octaveDirectionsAndClamping();
    octaveChangeAffectsSubsequentLiveNotes();
    transposeShiftsTheRightHandMelody();
    transposeAndOctaveCombine();
    transposedNoteOffReleasesSamePitch();
    resetClearsHeldNotes();
    bankNamesMapToGmPrograms();
    dedicatedChannelIsConfigurable();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All LiveMelodyVoice tests passed\n";
    return EXIT_SUCCESS;
}
