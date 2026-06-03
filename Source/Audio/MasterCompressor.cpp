#include "MasterCompressor.h"

#include <algorithm>
#include <cmath>

namespace cadenza::audio
{
namespace
{
inline float softClip(float x) noexcept
{
    constexpr float th = 0.8f;
    if (x >  th) return  th + (1.0f - th) * std::tanh((x - th) / (1.0f - th));
    if (x < -th) return -(th + (1.0f - th) * std::tanh((-x - th) / (1.0f - th)));
    return x;
}

inline double onePoleCoeff(double timeMs, double sampleRate) noexcept
{
    const double t = std::max(0.01, timeMs) * 0.001 * sampleRate;  // time in samples
    return 1.0 - std::exp(-1.0 / t);
}
}

void MasterCompressor::prepare(double sampleRate) noexcept
{
    m_sampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    m_grDb = 0.0f;
    recomputeCoeffs();
}

void MasterCompressor::setParams(double thresholdDb, double ratio,
                                 double attackMs, double releaseMs, double makeupDb) noexcept
{
    m_thresholdDb = thresholdDb;
    m_ratio       = std::max(1.0, ratio);
    m_attackMs    = attackMs;
    m_releaseMs   = releaseMs;
    m_makeupDb    = makeupDb;
    recomputeCoeffs();
}

void MasterCompressor::setAmount(int percent) noexcept
{
    const int pct = std::clamp(percent, 0, 100);
    if (pct <= 0) { setEnabled(false); return; }
    setEnabled(true);
    const double a = pct / 100.0;
    // threshold -6 dB (light) down to -28 dB (strong); make-up 0..+5 dB.
    setParams(-6.0 - a * 22.0, 2.0, 15.0, 200.0, a * 5.0);
}

void MasterCompressor::recomputeCoeffs() noexcept
{
    m_attackCoeff  = onePoleCoeff(m_attackMs,  m_sampleRate);
    m_releaseCoeff = onePoleCoeff(m_releaseMs, m_sampleRate);
}

void MasterCompressor::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!m_enabled.load() || channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    float* l = channels[0];
    float* r = numChannels > 1 ? channels[1] : nullptr;
    if (l == nullptr) return;

    const double slope = 1.0 - 1.0 / m_ratio;   // gain-reduction slope above threshold

    for (int i = 0; i < numSamples; ++i) {
        const double sl = l[i];
        const double sr = r ? r[i] : sl;

        // Linked peak detector.
        const double peak = std::max(std::fabs(sl), std::fabs(sr));
        const double levelDb = peak > 1e-9 ? 20.0 * std::log10(peak) : -120.0;

        const double over = levelDb - m_thresholdDb;
        const double targetGrDb = over > 0.0 ? over * slope : 0.0;   // >= 0

        // Attack when GR is increasing, release when decreasing.
        const double coeff = (targetGrDb > m_grDb) ? m_attackCoeff : m_releaseCoeff;
        m_grDb += static_cast<float>((targetGrDb - m_grDb) * coeff);

        const double gain = std::pow(10.0, (m_makeupDb - m_grDb) / 20.0);

        l[i] = softClip(static_cast<float>(sl * gain));
        if (r) r[i] = softClip(static_cast<float>(sr * gain));
    }
}
}
