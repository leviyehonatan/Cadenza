#pragma once

#include <optional>

namespace cadenza::audio
{
// Dedicated Cadenza (1-based) channel for live right-hand melody playback.
// Maps to FluidSynth channel 0. Chosen to avoid the GM drum channel (10) and to
// stay clear of the channels typical styles use (8-beat-pop uses 2/3/10; Yamaha
// styles use 9-16). The live melody voice owns its own program on this channel.
// NOTE: a style that happens to use channel 1 would share this program until a
// proper per-voice mixer exists.
constexpr int kLiveMelodyChannel = 1;

// Cadenza-facing APIs use conventional MIDI channel numbers 1..16.
// Synth backends such as FluidSynth use zero-based channels 0..15.
std::optional<int> synthChannelFromCadenzaChannel(int channel) noexcept;
bool isCadenzaDrumChannel(int channel) noexcept;
bool isSynthDrumChannel(int channel) noexcept;
}
