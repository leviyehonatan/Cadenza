#pragma once

#include <cstdint>
#include <limits>

namespace cadenza
{
constexpr int ticksPerNotatedBeat(int ticksPerQuarter, int beatUnit) noexcept
{
    if (ticksPerQuarter <= 0 || beatUnit <= 0)
        return 0;

    const auto ticks = static_cast<std::int64_t>(ticksPerQuarter) * 4 / beatUnit;
    if (ticks <= 0 || ticks > std::numeric_limits<int>::max())
        return 0;
    return static_cast<int>(ticks);
}

constexpr int ticksPerBar(int ticksPerQuarter, int beatsPerBar, int beatUnit) noexcept
{
    if (ticksPerQuarter <= 0 || beatsPerBar <= 0 || beatUnit <= 0)
        return 0;

    const auto ticks = static_cast<std::int64_t>(ticksPerQuarter) * beatsPerBar * 4 / beatUnit;
    if (ticks <= 0 || ticks > std::numeric_limits<int>::max())
        return 0;
    return static_cast<int>(ticks);
}
}
