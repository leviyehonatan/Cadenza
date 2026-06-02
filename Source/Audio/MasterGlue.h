// MasterGlue — analog "console" colouration on the master buss to fight the
// flat/sterile sound of a clean GM render: gentle saturation + harmonic depth.
//
// DSP ported directly from Airwindows "Console7Buss" by Chris Johnson, which is
// released under the MIT license (https://github.com/airwindows/airwindows).
// Adapted to run on Cadenza's float master buffer; a final soft clip bounds the
// console's asin output (which can exceed +/-1) so it never hard-clips.

#pragma once

#include <atomic>
#include <cstdint>

namespace cadenza::audio
{
class MasterGlue
{
public:
    void prepare(double sampleRate) noexcept;
    void setEnabled(bool enabled) noexcept { m_enabled.store(enabled); }
    bool enabled() const noexcept { return m_enabled.load(); }

    // Process up to 2 channels in place.
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

private:
    // Disabled by default: the Console7Buss makeup/dither interacts badly in the
    // live chain (broke playback in-app); kept for a future, properly-verified pass.
    std::atomic<bool> m_enabled { false };
    double m_sampleRate = 44100.0;

    // Airwindows Console7Buss state.
    double A = 1.0 / 1.03;     // "Master" trim set so makeup gain is unity (see prepare)
    double gainchase = -1.0;
    double chasespeed = 64.0;
    double biquadA[15] = {};
    double biquadB[15] = {};
    std::uint32_t fpdL = 2545674910u;
    std::uint32_t fpdR = 1066126093u;
};
}
