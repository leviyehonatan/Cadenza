#include "PolishedMaster.h"

#include <algorithm>
#include <cmath>

namespace cadenza::audio
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// --- Tuning (deliberately subtle) -------------------------------------------
constexpr double kHpfFreq   = 30.0;    // sub/DC cleanup
constexpr double kHpfQ      = 0.7071;  // Butterworth
constexpr double kAirFreq   = 12000.0; // air shelf corner
constexpr double kAirGainDb = 1.5;     // gentle HF tilt
constexpr double kSideHpf   = 120.0;   // bass stays mono below this
constexpr double kWidth     = 1.18;    // side-signal scale (>1 = a touch wider)

constexpr double kCompThreshDb = -14.0;
constexpr double kCompRatio    = 2.0;
constexpr double kCompAttackMs = 20.0;
constexpr double kCompRelMs    = 160.0;
constexpr double kCompMakeupDb = 1.0;

constexpr double kPreGainDb    = 4.5;   // push into the limiter for loudness
constexpr double kLimRelMs     = 80.0;

double onePole(double timeMs, double sr) noexcept
{
    const double t = std::max(0.01, timeMs) * 0.001 * sr;   // samples
    return 1.0 - std::exp(-1.0 / t);
}

void setHighpass(PolishedMaster::Biquad& bq, double sr, double f, double q) noexcept
{
    const double w0 = 2.0 * kPi * f / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * q);
    const double a0 = 1.0 + alpha;
    bq.b0 = ((1.0 + cw) / 2.0) / a0;
    bq.b1 = (-(1.0 + cw))      / a0;
    bq.b2 = ((1.0 + cw) / 2.0) / a0;
    bq.a1 = (-2.0 * cw)        / a0;
    bq.a2 = (1.0 - alpha)      / a0;
}

void setHighShelf(PolishedMaster::Biquad& bq, double sr, double f, double gainDb) noexcept
{
    constexpr double S = 0.7;
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * kPi * f / sr;
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
    const double twoSqrtAalpha = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * cw + twoSqrtAalpha;
    bq.b0 =      A * ((A + 1.0) + (A - 1.0) * cw + twoSqrtAalpha) / a0;
    bq.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw)               / a0;
    bq.b2 =      A * ((A + 1.0) + (A - 1.0) * cw - twoSqrtAalpha) / a0;
    bq.a1 =  2.0 *    ((A - 1.0) - (A + 1.0) * cw)               / a0;
    bq.a2 =          ((A + 1.0) - (A - 1.0) * cw - twoSqrtAalpha) / a0;
}
}

void PolishedMaster::prepare(double sampleRate) noexcept
{
    m_sampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    for (int c = 0; c < 2; ++c) {
        setHighpass (m_hpf[c], m_sampleRate, kHpfFreq, kHpfQ);
        setHighShelf(m_air[c], m_sampleRate, kAirFreq, kAirGainDb);
        m_hpf[c].reset();
        m_air[c].reset();
    }
    setHighpass(m_sideHpf, m_sampleRate, kSideHpf, kHpfQ);
    m_sideHpf.reset();

    m_grDb         = 0.0;
    m_attackCoeff  = onePole(kCompAttackMs, m_sampleRate);
    m_releaseCoeff = onePole(kCompRelMs,    m_sampleRate);
    m_limGain      = 1.0;
    m_limRelease   = onePole(kLimRelMs, m_sampleRate);
}

void PolishedMaster::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!m_enabled.load() || channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    float* L = channels[0];
    float* R = numChannels > 1 ? channels[1] : nullptr;
    if (L == nullptr) return;

    const double slope    = 1.0 - 1.0 / kCompRatio;
    const double preGain  = std::pow(10.0, kPreGainDb / 20.0);
    const double ceiling  = static_cast<double>(kCeiling);

    for (int i = 0; i < numSamples; ++i)
    {
        double l = L[i];
        double r = R ? R[i] : l;

        // 1) low cleanup
        l = m_hpf[0].tick(l);
        r = R ? m_hpf[1].tick(r) : l;

        // 2) air shelf
        l = m_air[0].tick(l);
        r = R ? m_air[1].tick(r) : l;

        // 3) light glue compression (linked peak detector)
        const double peak = std::max(std::fabs(l), std::fabs(r));
        const double levelDb = peak > 1e-9 ? 20.0 * std::log10(peak) : -120.0;
        const double over = levelDb - kCompThreshDb;
        const double targetGr = over > 0.0 ? over * slope : 0.0;
        const double cCoeff = (targetGr > m_grDb) ? m_attackCoeff : m_releaseCoeff;
        m_grDb += (targetGr - m_grDb) * cCoeff;
        const double g = std::pow(10.0, (kCompMakeupDb - m_grDb) / 20.0);
        l *= g;
        r *= g;

        // 4) subtle stereo width, low end kept mono
        double mid  = 0.5 * (l + r);
        double side = 0.5 * (l - r);
        side = m_sideHpf.tick(side) * kWidth;
        l = mid + side;
        r = mid - side;

        // 5) push into the limiter
        l *= preGain;
        r *= preGain;

        // 6) brickwall limiter: instant attack, smooth release. Guarantees the
        //    post-gain peak never exceeds the ceiling.
        const double pk = std::max(std::fabs(l), std::fabs(r));
        const double desired = pk > ceiling ? ceiling / pk : 1.0;
        if (desired < m_limGain) m_limGain = desired;                 // attack now
        else m_limGain += (1.0 - m_limGain) * m_limRelease;           // release
        l *= m_limGain;
        r *= m_limGain;

        // 7) hard clamp — belt-and-suspenders so output is always <= ceiling
        l = std::clamp(l, -ceiling, ceiling);
        r = std::clamp(r, -ceiling, ceiling);

        L[i] = static_cast<float>(l);
        if (R) R[i] = static_cast<float>(r);
    }
}
}
