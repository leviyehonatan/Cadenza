#include "StylePartPianoRollGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace cadenza::ui::piano_roll
{
float tickToX(int tick, int sectionTicks, float gridLeft, float gridRight,
              float zoom, float scrollTick) noexcept
{
    const int length = std::max(1, sectionTicks);
    const float width = std::max(1.0f, gridRight - gridLeft);
    const float pixelsPerTick = (width / static_cast<float>(length))
        * std::max(0.01f, zoom);
    const int clampedTick = std::clamp(tick, 0, length);
    return gridLeft + (clampedTick - std::max(0.0f, scrollTick)) * pixelsPerTick;
}

int xToTick(float x, int sectionTicks, float gridLeft, float gridRight,
            float zoom, float scrollTick) noexcept
{
    const int length = std::max(1, sectionTicks);
    const float width = std::max(1.0f, gridRight - gridLeft);
    const float pixelsPerTick = (width / static_cast<float>(length))
        * std::max(0.01f, zoom);
    const float tick = std::max(0.0f, scrollTick) + (x - gridLeft) / pixelsPerTick;
    return std::clamp(static_cast<int>(std::lround(tick)), 0, length);
}

int wrapPlaybackTick(int tick, int sectionTicks) noexcept
{
    if (sectionTicks <= 0)
        return 0;
    return ((tick % sectionTicks) + sectionTicks) % sectionTicks;
}

float playheadX(int tick, int sectionTicks, float gridLeft, float gridRight,
                float zoom, float scrollTick) noexcept
{
    return tickToX(wrapPlaybackTick(tick, sectionTicks),
                   sectionTicks, gridLeft, gridRight, zoom, scrollTick);
}

GridLineKind classifyGridLine(int tick, int ticksPerBeat,
                              int beatsPerBar, int subdivisionTicks) noexcept
{
    const int beat = std::max(1, ticksPerBeat);
    const int bar = beat * std::max(1, beatsPerBar);
    if (tick % bar == 0)
        return GridLineKind::Bar;
    if (tick % beat == 0)
        return GridLineKind::Beat;
    (void) subdivisionTicks;
    return GridLineKind::Subdivision;
}

int measureNumberAtTick(int tick, int ticksPerBeat, int beatsPerBar) noexcept
{
    const int bar = std::max(1, ticksPerBeat) * std::max(1, beatsPerBar);
    return std::max(0, tick) / bar + 1;
}

int pitchForRow(int row, int topPitch) noexcept
{
    return std::clamp(topPitch - std::max(0, row), 0, 127);
}

std::string drumLabelForPitch(int pitch)
{
    switch (pitch) {
        case 35: return "Acoustic Kick";
        case 36: return "Kick";
        case 37: return "Side Stick";
        case 38: return "Snare";
        case 39: return "Hand Clap";
        case 40: return "Electric Snare";
        case 41: return "Low Tom";
        case 42: return "Closed Hat";
        case 43: return "Low-Mid Tom";
        case 44: return "Pedal Hat";
        case 45: return "Mid Tom";
        case 46: return "Open Hat";
        case 47: return "High-Mid Tom";
        case 48: return "High Tom";
        case 49: return "Crash";
        case 50: return "Tom 1";
        case 51: return "Ride";
        case 52: return "China";
        case 53: return "Ride Bell";
        case 54: return "Tambourine";
        case 55: return "Splash";
        case 56: return "Cowbell";
        case 57: return "Crash 2";
        case 58: return "Vibraslap";
        case 59: return "Ride 2";
        default: return "Drum " + std::to_string(pitch);
    }
}

GutterMode gutterMode(bool percussion) noexcept
{
    return percussion ? GutterMode::Drums : GutterMode::Piano;
}

int velocityAtY(float y, float laneHeight) noexcept
{
    const float height = std::max(1.0f, laneHeight);
    const float proportion = 1.0f - std::clamp(y / height, 0.0f, 1.0f);
    return std::clamp(static_cast<int>(std::lround(1.0f + proportion * 126.0f)),
                      1, 127);
}

int findNearestNoteAtTick(const std::vector<VelocityNote>& notes, int tick) noexcept
{
    if (notes.empty())
        return -1;

    int nearest = 0;
    int nearestDistance = std::abs(notes.front().tick - tick);
    for (int i = 1; i < static_cast<int>(notes.size()); ++i) {
        const int distance = std::abs(notes[i].tick - tick);
        if (distance < nearestDistance) {
            nearest = i;
            nearestDistance = distance;
        }
    }
    return nearest;
}
}
