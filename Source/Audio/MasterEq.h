// MasterEq — a simple, dependency-free 3-band master equaliser applied to the
// final stereo mix: low shelf + mid peak + high shelf, each with adjustable
// gain in dB. RBJ biquad cookbook coefficients. Real-time safe: gains are set
// from the message thread (atomics) and coefficients are recomputed on the
// audio thread only when they change.

#pragma once

#include <atomic>

namespace cadenza::audio
{
class MasterEq
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare(double sampleRate, int numChannels) noexcept;

    // Gains in dB (typically -12..+12). Thread-safe; takes effect next block.
    void setGains(float lowDb, float midDb, float highDb) noexcept;
    void setEnabled(bool enabled) noexcept { m_enabled.store(enabled); }

    float lowDb()  const noexcept { return m_lowDb.load(); }
    float midDb()  const noexcept { return m_midDb.load(); }
    float highDb() const noexcept { return m_highDb.load(); }

    // Process interleaved-by-channel float buffers in place.
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    // One band's biquad (public so the coefficient helpers in the .cpp can fill it).
    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        float  z1 = 0.0f, z2 = 0.0f;   // transposed direct form II state

        inline float tick(float x) noexcept
        {
            const double y = b0 * static_cast<double>(x) + z1;
            z1 = static_cast<float>(b1 * x - a1 * y) + z2;
            z2 = static_cast<float>(b2 * x - a2 * y);
            return static_cast<float>(y);
        }
        void reset() noexcept { z1 = z2 = 0.0f; }
    };

private:
    void recompute() noexcept;

    double m_sampleRate = 44100.0;
    int    m_numChannels = 2;

    std::atomic<float> m_lowDb { 0.0f };
    std::atomic<float> m_midDb { 0.0f };
    std::atomic<float> m_highDb { 0.0f };
    std::atomic<bool>  m_enabled { true };

    float m_appliedLow = 1e9f, m_appliedMid = 1e9f, m_appliedHigh = 1e9f;  // force first recompute

    Biquad m_low[kMaxChannels];
    Biquad m_mid[kMaxChannels];
    Biquad m_high[kMaxChannels];
};
}
