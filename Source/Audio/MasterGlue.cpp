#include "MasterGlue.h"

#include <cmath>

// DSP ported from Airwindows "Console7Buss" (MIT license, (c) Chris Johnson /
// airwindows). The console saturation is an asin blend; coefficients and the
// gain-chase / dither logic are reproduced as-is, generalised to operate on
// Cadenza's in-place float buffers.

namespace cadenza::audio
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

inline float softClip(float x) noexcept
{
    constexpr float th = 0.8f;
    if (x >  th) return  th + (1.0f - th) * std::tanh((x - th) / (1.0f - th));
    if (x < -th) return -(th + (1.0f - th) * std::tanh((-x - th) / (1.0f - th)));
    return x;
}
}

void MasterGlue::prepare(double sampleRate) noexcept
{
    m_sampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    A = 1.0;
    gainchase = -1.0;
    chasespeed = 64.0;
    for (int x = 0; x < 15; ++x) { biquadA[x] = 0.0; biquadB[x] = 0.0; }
    fpdL = 2545674910u;
    fpdR = 1066126093u;
}

void MasterGlue::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    if (!m_enabled.load() || channels == nullptr || numSamples <= 0 || numChannels <= 0)
        return;

    float* in1 = channels[0];
    float* in2 = numChannels > 1 ? channels[1] : nullptr;
    if (in1 == nullptr) return;

    const double inputgain = A * 1.03;

    if (gainchase != inputgain) chasespeed *= 2.0;
    if (chasespeed > numSamples) chasespeed = numSamples;
    if (gainchase < 0.0) gainchase = inputgain;

    biquadB[0] = biquadA[0] = 20000.0 / m_sampleRate;
    biquadA[1] = 0.618033988749894848204586;
    biquadB[1] = 0.5;

    double K = std::tan(kPi * biquadA[0]); // lowpass
    double norm = 1.0 / (1.0 + K / biquadA[1] + K * K);
    biquadA[2] = K * K * norm;
    biquadA[3] = 2.0 * biquadA[2];
    biquadA[4] = biquadA[2];
    biquadA[5] = 2.0 * (K * K - 1.0) * norm;
    biquadA[6] = (1.0 - K / biquadA[1] + K * K) * norm;

    K = std::tan(kPi * biquadA[0]);
    norm = 1.0 / (1.0 + K / biquadB[1] + K * K);
    biquadB[2] = K * K * norm;
    biquadB[3] = 2.0 * biquadB[2];
    biquadB[4] = biquadB[2];
    biquadB[5] = 2.0 * (K * K - 1.0) * norm;
    biquadB[6] = (1.0 - K / biquadB[1] + K * K) * norm;

    for (int s = 0; s < numSamples; ++s)
    {
        double inputSampleL = in1[s];
        double inputSampleR = in2 ? in2[s] : 0.0;
        if (std::fabs(inputSampleL) < 1.18e-23) inputSampleL = fpdL * 1.18e-17;
        if (std::fabs(inputSampleR) < 1.18e-23) inputSampleR = fpdR * 1.18e-17;

        double outSampleL = biquadA[2]*inputSampleL+biquadA[3]*biquadA[7]+biquadA[4]*biquadA[8]-biquadA[5]*biquadA[9]-biquadA[6]*biquadA[10];
        biquadA[8] = biquadA[7]; biquadA[7] = inputSampleL; inputSampleL = outSampleL; biquadA[10] = biquadA[9]; biquadA[9] = inputSampleL; // DF1 left

        double outSampleR = biquadA[2]*inputSampleR+biquadA[3]*biquadA[11]+biquadA[4]*biquadA[12]-biquadA[5]*biquadA[13]-biquadA[6]*biquadA[14];
        biquadA[12] = biquadA[11]; biquadA[11] = inputSampleR; inputSampleR = outSampleR; biquadA[14] = biquadA[13]; biquadA[13] = inputSampleR; // DF1 right

        chasespeed *= 0.9999; chasespeed -= 0.01; if (chasespeed < 64.0) chasespeed = 64.0;
        gainchase = (((gainchase*chasespeed)+inputgain)/(chasespeed+1.0));
        if (1.0 != gainchase) { inputSampleL *= std::sqrt(gainchase); inputSampleR *= std::sqrt(gainchase); }

        if (inputSampleL >  1.0) inputSampleL =  1.0;
        if (inputSampleL < -1.0) inputSampleL = -1.0;
        inputSampleL = ((std::asin(inputSampleL*std::fabs(inputSampleL))/((std::fabs(inputSampleL) == 0.0) ?1:std::fabs(inputSampleL)))*0.618033988749894848204586)+(std::asin(inputSampleL)*0.381966011250105);
        if (inputSampleR >  1.0) inputSampleR =  1.0;
        if (inputSampleR < -1.0) inputSampleR = -1.0;
        inputSampleR = ((std::asin(inputSampleR*std::fabs(inputSampleR))/((std::fabs(inputSampleR) == 0.0) ?1:std::fabs(inputSampleR)))*0.618033988749894848204586)+(std::asin(inputSampleR)*0.381966011250105);

        outSampleL = biquadB[2]*inputSampleL+biquadB[3]*biquadB[7]+biquadB[4]*biquadB[8]-biquadB[5]*biquadB[9]-biquadB[6]*biquadB[10];
        biquadB[8] = biquadB[7]; biquadB[7] = inputSampleL; inputSampleL = outSampleL; biquadB[10] = biquadB[9]; biquadB[9] = inputSampleL; // DF1 left

        outSampleR = biquadB[2]*inputSampleR+biquadB[3]*biquadB[11]+biquadB[4]*biquadB[12]-biquadB[5]*biquadB[13]-biquadB[6]*biquadB[14];
        biquadB[12] = biquadB[11]; biquadB[11] = inputSampleR; inputSampleR = outSampleR; biquadB[14] = biquadB[13]; biquadB[13] = inputSampleR; // DF1 right

        if (1.0 != gainchase) { inputSampleL *= std::sqrt(gainchase); inputSampleR *= std::sqrt(gainchase); }

        // 32-bit stereo floating point dither (Airwindows)
        int expon; std::frexp((float)inputSampleL, &expon);
        fpdL ^= fpdL << 13; fpdL ^= fpdL >> 17; fpdL ^= fpdL << 5;
        inputSampleL += ((double(fpdL)-std::uint32_t(0x7fffffff)) * 5.5e-36l * std::pow(2.0, expon+62));
        std::frexp((float)inputSampleR, &expon);
        fpdR ^= fpdR << 13; fpdR ^= fpdR >> 17; fpdR ^= fpdR << 5;
        inputSampleR += ((double(fpdR)-std::uint32_t(0x7fffffff)) * 5.5e-36l * std::pow(2.0, expon+62));

        // Final safety: console asin can exceed +/-1, so bound it smoothly.
        in1[s] = softClip((float) inputSampleL);
        if (in2) in2[s] = softClip((float) inputSampleR);
    }
}
}
