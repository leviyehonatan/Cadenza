#include "MidiChannel.h"

namespace cadenza::audio
{
std::optional<int> synthChannelFromCadenzaChannel(int channel) noexcept
{
    if (channel < 1 || channel > 16)
        return std::nullopt;
    return channel - 1;
}

bool isCadenzaDrumChannel(int channel) noexcept
{
    return channel == 10;
}

bool isSynthDrumChannel(int channel) noexcept
{
    return channel == 9;
}
}
