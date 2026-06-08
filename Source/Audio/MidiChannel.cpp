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
    // Yamaha styles use two rhythm channels: MIDI 10 = RHY1 (main kit),
    // MIDI 9 = RHY2 (sub-rhythm). Both are independent drum channels so each
    // gets its own mixer strip (volume / mute / kit).
    return channel == 9 || channel == 10;
}

bool isSynthDrumChannel(int channel) noexcept
{
    // Synth (0-based): cadenza 10 -> 9 (RHY1), cadenza 9 -> 8 (RHY2).
    return channel == 8 || channel == 9;
}
}
