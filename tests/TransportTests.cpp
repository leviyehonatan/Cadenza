#include "Audio/Transport.h"
#include "Arranger/RuntimePlayback.h"
#include "Arranger/Style.h"

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

using cadenza::audio::Transport;

void defaultsAreSensible()
{
    Transport t;
    expect(t.bpm() == 120.0, "default bpm 120");
    expect(t.ticksPerBeat() == 960, "default ppq 960");
    expect(t.beatsPerBar() == 4, "default 4/4 numerator");
    expect(!t.playing(), "starts stopped");
    expect(t.positionInTicks() == 0.0, "starts at 0");
}

void samplesPerTickAt120BpmAnd48kHz()
{
    Transport t;
    t.setSampleRate(48000.0);
    t.setBpm(120.0);
    t.setTicksPerBeat(960);

    // 60 / (120 * 960) = 5.208333e-4 s/tick.
    // At 48kHz: 25 samples/tick.
    expect(t.samplesPerTick() > 24.99 && t.samplesPerTick() < 25.01, "120 BPM 48k -> 25 samp/tick");
}

void stoppedDoesNotAdvance()
{
    Transport t;
    t.setSampleRate(48000.0);
    t.setBpm(120.0);

    expect(t.advance(48000) == 0, "stopped advance returns 0");
    expect(t.positionInTicks() == 0.0, "stopped position stays 0");
}

void playingAdvancesByExpectedTicks()
{
    Transport t;
    t.setSampleRate(48000.0);
    t.setBpm(120.0);
    t.setTicksPerBeat(960);
    t.start();

    // 1 second at 120 BPM = 2 beats = 1920 ticks.
    int delta = t.advance(48000);
    expect(delta > 1918 && delta < 1922, "1 second -> ~1920 ticks");
    expect(t.positionBar() == 0, "still in bar 0");
    expect(t.positionBeat() == 2, "now at beat 2");
}

void barAndBeatRollOverCorrectly()
{
    Transport t;
    t.setSampleRate(48000.0);
    t.setBpm(120.0);
    t.setTicksPerBeat(960);
    t.start();

    // Advance 2 seconds = 4 beats = 1 full bar (4/4).
    t.advance(96000);
    expect(t.positionBar() == 1, "after 4 beats, bar 1");
    expect(t.positionBeat() == 0, "beat 0 of bar 1");
}

void changingBpmTakesEffectImmediately()
{
    Transport t;
    t.setSampleRate(48000.0);
    t.setBpm(120.0);
    t.start();

    t.advance(48000);  // 1s at 120 BPM
    double posA = t.positionInTicks();

    t.setBpm(240.0);    // double tempo
    t.advance(48000);  // another 1s
    double posB = t.positionInTicks();

    // The second second should advance twice as many ticks as the first.
    double firstSec = posA;
    double secondSec = posB - posA;
    expect(secondSec > 1.9 * firstSec && secondSec < 2.1 * firstSec,
           "tempo doubling doubles tick rate");
}

void resetClears()
{
    Transport t;
    t.setSampleRate(48000.0);
    t.setBpm(120.0);
    t.start();
    t.advance(48000);
    expect(t.positionInTicks() > 0, "advanced");
    t.reset();
    expect(t.positionInTicks() == 0.0, "reset to 0");
}

void startFromBeginningResetsStoppedTransport()
{
    Transport t;
    t.setSampleRate(48000.0);
    t.setBpm(120.0);
    t.start();
    t.advance(48000);
    expect(t.positionInTicks() > 0.0, "transport advanced before stop");

    t.stop();
    t.startFromBeginning();
    expect(t.playing(), "start from beginning leaves transport playing");
    expect(t.positionInTicks() == 0.0, "start from beginning resets to tick 0");
}

void styleTimingAppliesToTransport()
{
    cadenza::arranger::Style style;
    style.ticksPerBeat = 480;
    style.beatsPerBar = 3;
    style.beatUnit = 8;
    style.defaultTempo = 92;

    Transport t;
    expect(t.ticksPerBeat() == 960, "test starts with default 960 PPQ");

    cadenza::arranger::applyStyleTimingToTransport(t, style);
    expect(t.ticksPerBeat() == 480, "style PPQ applies to transport");
    expect(t.beatsPerBar() == 3, "style beatsPerBar applies to transport");
    expect(t.beatUnit() == 8, "style beatUnit applies to transport");
    expect(t.bpm() == 92.0, "style default tempo applies to transport");
}
}

int main()
{
    defaultsAreSensible();
    samplesPerTickAt120BpmAnd48kHz();
    stoppedDoesNotAdvance();
    playingAdvancesByExpectedTicks();
    barAndBeatRollOverCorrectly();
    changingBpmTakesEffectImmediately();
    resetClears();
    startFromBeginningResetsStoppedTransport();
    styleTimingAppliesToTransport();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All Transport tests passed\n";
    return EXIT_SUCCESS;
}
