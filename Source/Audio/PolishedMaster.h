// PolishedMaster — an optional final-stage "polish" for the master bus, applied
// after the master volume. It makes the mix sound cleaner, a touch louder, and
// less harsh than the bare tanh soft-limiter, without changing any arranger,
// MIDI, or style behaviour. Stages, in order:
//
//   1. High-pass ~30 Hz   - clears sub-rumble/DC so it doesn't eat headroom.
//   2. Gentle air shelf   - a little +HF "polish" tilt.
//   3. Light glue comp    - low-ratio, slow; adds density (~1-2 dB GR).
//   4. Subtle stereo width - mid/side, with the low end kept mono.
//   5. Brickwall limiter  - instant-attack ceiling guard at ~-0.3 dBFS, then a
//                           hard clamp, so the output can NEVER exceed the
//                           ceiling (no clipping) while staying loud and clean.
//
// Pure C++ (no JUCE) so the DSP can be unit-tested in cadenza_core. Real-time
// safe: no allocations or locks in process().

#pragma once

#include <atomic>

namespace cadenza::audio
{
class PolishedMaster
{
public:
    // True-peak ceiling the limiter (and the final clamp) guarantee. ~-0.27 dBFS.
    static constexpr float kCeiling = 0.97f;

    void prepare(double sampleRate) noexcept;

    void setEnabled(bool enabled) noexcept { m_enabled.store(enabled); }
    bool enabled() const noexcept { return m_enabled.load(); }

    // In-place process of up to two channels. Mono (numChannels == 1) is fine.
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    // Transposed-Direct-Form-II biquad (a0 normalised to 1).
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1 = 0.0, z2 = 0.0;
        void reset() noexcept { z1 = z2 = 0.0; }
        inline double tick(double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

private:
    double m_sampleRate = 44100.0;

    Biquad m_hpf[2];      // low cleanup, per channel
    Biquad m_air[2];      // air shelf, per channel
    Biquad m_sideHpf;     // keeps bass mono: removes lows from the side signal

    // Glue compressor (linked) state.
    double m_grDb         = 0.0;
    double m_attackCoeff  = 0.0;
    double m_releaseCoeff = 0.0;

    // Brickwall limiter state.
    double m_limGain    = 1.0;
    double m_limRelease = 0.0;

    std::atomic<bool> m_enabled { true };
};
}
