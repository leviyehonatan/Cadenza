#include "MasterEq.h"

#include <algorithm>
#include <cmath>

namespace cadenza::audio
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// Band centre/corner frequencies and Q.
constexpr double kLowFreq  = 120.0;   // low shelf
constexpr double kMidFreq  = 900.0;   // mid peak
constexpr double kMidQ     = 0.9;
constexpr double kHighFreq = 9000.0;  // high shelf
constexpr double kShelfS   = 0.7;     // shelf slope

void setLowShelf(MasterEq::Biquad& bq, double sr, double f, double gainDb) noexcept;
void setHighShelf(MasterEq::Biquad& bq, double sr, double f, double gainDb) noexcept;
void setPeak(MasterEq::Biquad& bq, double sr, double f, double q, double gainDb) noexcept;
}

void MasterEq::prepare(double sampleRate, int numChannels) noexcept
{
    m_sampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    m_numChannels = std::clamp(numChannels, 1, kMaxChannels);
    for (int c = 0; c < kMaxChannels; ++c) {
        m_low[c].reset();
        m_mid[c].reset();
        m_high[c].reset();
    }
    m_appliedLow = m_appliedMid = m_appliedHigh = 1e9f;  // force recompute
}

void MasterEq::setGains(float lowDb, float midDb, float highDb) noexcept
{
    m_lowDb.store(lowDb);
    m_midDb.store(midDb);
    m_highDb.store(highDb);
}

void MasterEq::recompute() noexcept
{
    const float lo = m_lowDb.load(), mid = m_midDb.load(), hi = m_highDb.load();
    for (int c = 0; c < m_numChannels; ++c) {
        setLowShelf (m_low[c],  m_sampleRate, kLowFreq,  lo);
        setPeak     (m_mid[c],  m_sampleRate, kMidFreq,  kMidQ, mid);
        setHighShelf(m_high[c], m_sampleRate, kHighFreq, hi);
    }
    m_appliedLow = lo; m_appliedMid = mid; m_appliedHigh = hi;
}

namespace
{
// Smooth, symmetric soft clip — transparent below ~0.8, rounds peaks above so a
// hot full-band mix never hard-clips into a "crunchy" digital distortion.
inline float softClip(float x) noexcept
{
    constexpr float th = 0.8f;
    if (x >  th) return  th + (1.0f - th) * std::tanh((x - th) / (1.0f - th));
    if (x < -th) return -(th + (1.0f - th) * std::tanh((-x - th) / (1.0f - th)));
    return x;
}
}

void MasterEq::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!m_enabled.load() || channels == nullptr || numSamples <= 0)
        return;

    const float lo = m_lowDb.load(), mid = m_midDb.load(), hi = m_highDb.load();
    if (lo != m_appliedLow || mid != m_appliedMid || hi != m_appliedHigh)
        recompute();

    const bool flatEq = std::abs(lo) < 0.01f && std::abs(mid) < 0.01f && std::abs(hi) < 0.01f;
    const int n = std::min(numChannels, m_numChannels);

    for (int c = 0; c < n; ++c) {
        float* x = channels[c];
        if (x == nullptr) continue;
        Biquad& low = m_low[c];
        Biquad& peak = m_mid[c];
        Biquad& high = m_high[c];
        for (int i = 0; i < numSamples; ++i) {
            float s = x[i];
            if (!flatEq)
                s = high.tick(peak.tick(low.tick(s)));  // 3-band EQ
            x[i] = softClip(s);                          // always-on peak safety
        }
    }
}

namespace
{
// RBJ audio-EQ cookbook biquads, normalised so a0 = 1.
void normalise(MasterEq::Biquad& bq, double b0, double b1, double b2,
               double a0, double a1, double a2) noexcept
{
    bq.b0 = b0 / a0; bq.b1 = b1 / a0; bq.b2 = b2 / a0;
    bq.a1 = a1 / a0; bq.a2 = a2 / a0;
}

void setLowShelf(MasterEq::Biquad& bq, double sr, double f, double gainDb) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * f / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / kShelfS - 1.0) + 2.0);
    const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;

    const double b0 =      A * ((A + 1.0) - (A - 1.0) * cw + twoSqrtAalpha);
    const double b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
    const double b2 =      A * ((A + 1.0) - (A - 1.0) * cw - twoSqrtAalpha);
    const double a0 =           (A + 1.0) + (A - 1.0) * cw + twoSqrtAalpha;
    const double a1 = -2.0 *    ((A - 1.0) + (A + 1.0) * cw);
    const double a2 =           (A + 1.0) + (A - 1.0) * cw - twoSqrtAalpha;
    normalise(bq, b0, b1, b2, a0, a1, a2);
}

void setHighShelf(MasterEq::Biquad& bq, double sr, double f, double gainDb) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * f / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / kShelfS - 1.0) + 2.0);
    const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;

    const double b0 =      A * ((A + 1.0) + (A - 1.0) * cw + twoSqrtAalpha);
    const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
    const double b2 =      A * ((A + 1.0) + (A - 1.0) * cw - twoSqrtAalpha);
    const double a0 =           (A + 1.0) - (A - 1.0) * cw + twoSqrtAalpha;
    const double a1 =  2.0 *    ((A - 1.0) - (A + 1.0) * cw);
    const double a2 =           (A + 1.0) - (A - 1.0) * cw - twoSqrtAalpha;
    normalise(bq, b0, b1, b2, a0, a1, a2);
}

void setPeak(MasterEq::Biquad& bq, double sr, double f, double q, double gainDb) noexcept
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * f / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);

    const double b0 = 1.0 + alpha * A;
    const double b1 = -2.0 * cw;
    const double b2 = 1.0 - alpha * A;
    const double a0 = 1.0 + alpha / A;
    const double a1 = -2.0 * cw;
    const double a2 = 1.0 - alpha / A;
    normalise(bq, b0, b1, b2, a0, a1, a2);
}
}
}
