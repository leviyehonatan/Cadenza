// MasterCompressor — a simple, stereo-linked feed-forward master-bus compressor
// for gentle "glue"/density on the final mix. Dependency-free (operates on raw
// float buffers) so it's unit-testable. Detector is peak, linked across channels
// (same gain to L and R, so the stereo image never shifts). A soft clip on the
// output bounds peaks after make-up gain.

#pragma once

#include <atomic>

namespace cadenza::audio
{
class MasterCompressor
{
public:
    void prepare(double sampleRate) noexcept;

    // Gentle glue defaults are baked in; this just toggles the whole stage.
    void setEnabled(bool enabled) noexcept { m_enabled.store(enabled); }
    bool enabled() const noexcept { return m_enabled.load(); }

    // Optional tuning (dB / ratio / ms). Safe to call before prepare.
    void setParams(double thresholdDb, double ratio,
                   double attackMs, double releaseMs, double makeupDb) noexcept;

    // Single "amount" control (0..100). 0 = bypass; higher = lower threshold +
    // more make-up = more glue/density. Maps to sensible threshold/makeup.
    void setAmount(int percent) noexcept;

    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    // Current gain reduction in dB (>=0), for metering/tests.
    float gainReductionDb() const noexcept { return m_grDb; }

private:
    void recomputeCoeffs() noexcept;

    std::atomic<bool> m_enabled { true };

    double m_sampleRate = 44100.0;
    double m_thresholdDb = -18.0;
    double m_ratio       = 2.0;
    double m_attackMs    = 15.0;
    double m_releaseMs   = 200.0;
    double m_makeupDb    = 3.0;

    double m_attackCoeff  = 0.0;   // one-pole smoothing toward target GR
    double m_releaseCoeff = 0.0;

    float m_grDb = 0.0f;           // current gain reduction in dB (audio-thread state)
};
}
