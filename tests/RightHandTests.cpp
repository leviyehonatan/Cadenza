// RightHandTests — layered right-hand voices (Right 1 / Right 2 / Right 3).

#include "Midi/RightHand.h"

#include <algorithm>
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

using cadenza::midi::RightHand;
using cadenza::midi::LiveMelodyEvent;

bool hasNoteOn(const std::vector<LiveMelodyEvent>& evs, int channel, int note) {
    return std::any_of(evs.begin(), evs.end(), [&](const LiveMelodyEvent& e) {
        return e.isOn && e.channel == channel && e.note == note;
    });
}

void defaultPlaysOnlyLayer1()
{
    RightHand rh;
    rh.setLayerChannel(0, 1);
    rh.setLayerChannel(1, 2);
    rh.setLayerChannel(2, 3);

    auto on = rh.handleNote(60, 100, true, /*melodyZone=*/true);
    expect(on.size() == 1, "only Right 1 enabled by default -> 1 event");
    expect(hasNoteOn(on, 1, 60), "Right 1 sounds note 60 on channel 1");
}

void enablingLayersStacksVoices()
{
    RightHand rh;
    rh.setLayerChannel(0, 1);
    rh.setLayerChannel(1, 2);
    rh.setLayerChannel(2, 3);
    rh.setLayerEnabled(1, true);
    rh.setLayerEnabled(2, true);

    auto on = rh.handleNote(60, 100, true, true);
    expect(on.size() == 3, "all three layers sound the key");
    expect(hasNoteOn(on, 1, 60) && hasNoteOn(on, 2, 60) && hasNoteOn(on, 3, 60),
           "note 60 plays on channels 1, 2 and 3");
}

void perLayerOctaveShifts()
{
    RightHand rh;
    rh.setLayerChannel(0, 1);
    rh.setLayerChannel(1, 2);
    rh.setLayerEnabled(1, true);
    rh.setLayerOctave(1, 1);   // Right 2 up an octave

    auto on = rh.handleNote(60, 100, true, true);
    expect(hasNoteOn(on, 1, 60), "Right 1 plays 60");
    expect(hasNoteOn(on, 2, 72), "Right 2 plays 72 (one octave up)");
}

void transposeAppliesToAllLayers()
{
    RightHand rh;
    rh.setLayerChannel(0, 1);
    rh.setLayerChannel(1, 2);
    rh.setLayerEnabled(1, true);
    rh.setTranspose(2);

    auto on = rh.handleNote(60, 100, true, true);
    expect(hasNoteOn(on, 1, 62) && hasNoteOn(on, 2, 62), "transpose +2 shifts every layer");
}

void chordZoneSilentOnAllLayers()
{
    RightHand rh;
    rh.setLayerEnabled(1, true);
    auto on = rh.handleNote(48, 100, true, /*melodyZone=*/false);
    expect(on.empty(), "chord-zone note makes no right-hand sound");
}

void noteOffReleasesEvenAfterLayerDisabled()
{
    RightHand rh;
    rh.setLayerChannel(0, 1);
    rh.setLayerChannel(1, 2);
    rh.setLayerEnabled(1, true);

    auto on = rh.handleNote(64, 100, true, true);
    expect(on.size() == 2, "note-on sounds two layers");

    rh.setLayerEnabled(1, false);   // disable Right 2 while the note is held
    auto off = rh.handleNote(64, 0, false, true);
    expect(off.size() == 2, "note-off still releases BOTH layers (no stuck note)");
    expect(std::all_of(off.begin(), off.end(), [](const LiveMelodyEvent& e){ return !e.isOn; }),
           "all events are note-offs");
}

void disabledLayerDoesNotStart()
{
    RightHand rh;
    rh.setLayerChannel(0, 1);
    rh.setLayerChannel(2, 3);
    // Right 2 and 3 left disabled.
    auto on = rh.handleNote(67, 100, true, true);
    expect(on.size() == 1, "only the enabled layer starts the note");
}
}

int main()
{
    defaultPlaysOnlyLayer1();
    enablingLayersStacksVoices();
    perLayerOctaveShifts();
    transposeAppliesToAllLayers();
    chordZoneSilentOnAllLayers();
    noteOffReleasesEvenAfterLayerDisabled();
    disabledLayerDoesNotStart();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All RightHand tests passed\n";
    return EXIT_SUCCESS;
}
