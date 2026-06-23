#pragma once

#include <string>
#include <vector>

namespace cadenza::ui::piano_roll
{
enum class GridLineKind
{
    Subdivision,
    Beat,
    Bar,
};

enum class GutterMode
{
    Piano,
    Drums,
};

struct VelocityNote
{
    int tick = 0;
    int duration = 1;
};

float tickToX(int tick, int sectionTicks, float gridLeft, float gridRight,
              float zoom = 1.0f, float scrollTick = 0.0f) noexcept;
int xToTick(float x, int sectionTicks, float gridLeft, float gridRight,
            float zoom = 1.0f, float scrollTick = 0.0f) noexcept;

int wrapPlaybackTick(int tick, int sectionTicks) noexcept;
float playheadX(int tick, int sectionTicks, float gridLeft, float gridRight,
                float zoom = 1.0f, float scrollTick = 0.0f) noexcept;

GridLineKind classifyGridLine(int tick, int ticksPerBeat,
                              int beatsPerBar, int subdivisionTicks) noexcept;
int measureNumberAtTick(int tick, int ticksPerBeat, int beatsPerBar) noexcept;

int pitchForRow(int row, int topPitch) noexcept;
std::string drumLabelForPitch(int pitch);
GutterMode gutterMode(bool percussion) noexcept;

int velocityAtY(float y, float laneHeight) noexcept;
int findNearestNoteAtTick(const std::vector<VelocityNote>& notes, int tick) noexcept;
}
