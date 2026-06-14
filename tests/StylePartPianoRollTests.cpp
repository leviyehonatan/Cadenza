#include "UI/StylePartPianoRollGeometry.h"
#include "Arranger/StyleRecorder.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool closeTo(float actual, float expected)
{
    return std::abs(actual - expected) < 0.001f;
}

using namespace cadenza::ui::piano_roll;

void tickAndPixelConversionsRoundTrip()
{
    expect(closeTo(tickToX(1920, 7680, 100.0f, 900.0f), 300.0f),
           "quarter-loop tick maps to quarter-loop x");
    expect(xToTick(300.0f, 7680, 100.0f, 900.0f) == 1920,
           "x converts back to the original tick");
    expect(xToTick(50.0f, 7680, 100.0f, 900.0f) == 0,
           "x before the grid clamps to tick zero");
}

void playheadWrapsInsideLoop()
{
    expect(wrapPlaybackTick(7780, 7680) == 100,
           "playhead wraps positive ticks inside the loop");
    expect(wrapPlaybackTick(-20, 7680) == 7660,
           "playhead wraps negative ticks inside the loop");
    expect(closeTo(playheadX(7780, 7680, 100.0f, 900.0f), 110.416664f),
           "wrapped playhead x uses the loop-relative tick");
}

void gridClassifiesMeasuresBeatsAndSubdivisions()
{
    expect(classifyGridLine(3840, 960, 4, 240) == GridLineKind::Bar,
           "bar boundary is a strong grid line");
    expect(classifyGridLine(960, 960, 4, 240) == GridLineKind::Beat,
           "beat boundary is a medium grid line");
    expect(classifyGridLine(240, 960, 4, 240) == GridLineKind::Subdivision,
           "snap boundary is a light grid line");
    expect(measureNumberAtTick(0, 960, 4) == 1
               && measureNumberAtTick(3840, 960, 4) == 2,
           "measure numbers are one-based");
}

void percussionAndMelodicGuttersRemainDistinct()
{
    expect(pitchForRow(0, 60) == 60 && pitchForRow(4, 60) == 56,
           "visible rows map downward from the top pitch");
    expect(drumLabelForPitch(36) == "Kick", "GM kick row has a readable label");
    expect(drumLabelForPitch(38) == "Snare", "GM snare row has a readable label");
    expect(gutterMode(true) == GutterMode::Drums,
           "percussion parts use drum labels");
    expect(gutterMode(false) == GutterMode::Piano,
           "melodic parts retain piano keys");
}

void velocityLaneMapsHeightAndFindsNearestNote()
{
    expect(velocityAtY(0.0f, 64.0f) == 127,
           "top of velocity lane is maximum velocity");
    expect(velocityAtY(64.0f, 64.0f) == 1,
           "bottom of velocity lane is minimum velocity");

    const std::vector<VelocityNote> notes {
        { 0, 240 },
        { 480, 240 },
        { 960, 480 },
    };
    expect(findNearestNoteAtTick(notes, 500) == 1,
           "velocity hit selects the nearest note start");
    expect(findNearestNoteAtTick({}, 500) == -1,
           "velocity hit reports no note for an empty part");
}

void recorderBarLengthUpdatesPianoRollGeometry()
{
    cadenza::arranger::StyleRecorder recorder;
    cadenza::arranger::RecorderConfig config;
    config.bars = 4;
    recorder.startSession(config);

    recorder.setBarCount(1);
    expect(closeTo(tickToX(3840, recorder.sectionLengthTicks(),
                          100.0f, 900.0f), 900.0f),
           "one-bar recorder length fills one-bar piano-roll geometry");

    recorder.setBarCount(2);
    expect(closeTo(tickToX(3840, recorder.sectionLengthTicks(),
                          100.0f, 900.0f), 500.0f),
           "two-bar recorder length maps bar two to piano-roll midpoint");
}
}

int main()
{
    tickAndPixelConversionsRoundTrip();
    playheadWrapsInsideLoop();
    gridClassifiesMeasuresBeatsAndSubdivisions();
    percussionAndMelodicGuttersRemainDistinct();
    velocityLaneMapsHeightAndFindsNearestNote();
    recorderBarLengthUpdatesPianoRollGeometry();

    if (failures != 0)
        return EXIT_FAILURE;
    std::cout << "All Part Editor tests passed\n";
    return EXIT_SUCCESS;
}
