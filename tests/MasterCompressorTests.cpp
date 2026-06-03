#include "Audio/MasterCompressor.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
int failures = 0;
void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

using cadenza::audio::MasterCompressor;

// Run a steady sine of the given amplitude through the compressor for enough
// blocks to reach steady state; return the peak output of the final block.
double steadyPeak(double amp)
{
    const int N = 512;
    const double sr = 48000.0;
    MasterCompressor comp;
    comp.prepare(sr);
    comp.setParams(-18.0, 2.0, 15.0, 200.0, 3.0);

    double phase = 0.0, peak = 0.0;
    for (int blk = 0; blk < 80; ++blk) {
        std::vector<float> L(N), R(N);
        for (int i = 0; i < N; ++i) {
            const float s = static_cast<float>(amp * std::sin(phase));
            phase += 2.0 * 3.14159265 * 220.0 / sr;
            L[i] = s; R[i] = s;
        }
        float* ch[2] = { L.data(), R.data() };
        comp.process(ch, 2, N);
        if (blk == 79)
            for (float v : L) peak = std::max(peak, static_cast<double>(std::fabs(v)));
    }
    return peak;
}

void quietGetsMakeupNotCompressed()
{
    // 0.03 peak (-30 dB) is well below the -18 dB threshold -> only +3 dB makeup.
    const double out = steadyPeak(0.03);
    const double expected = 0.03 * std::pow(10.0, 3.0 / 20.0);  // ~0.0424
    expect(std::fabs(out - expected) < 0.004, "quiet signal gets makeup only (no compression)");
}

void loudSignalIsCompressed()
{
    // A loud signal must be gained DOWN relative to a quiet one (compression).
    const double quietRatio = steadyPeak(0.05) / 0.05;
    const double loudRatio  = steadyPeak(0.6)  / 0.6;
    expect(loudRatio < quietRatio * 0.85, "loud signal compressed relative to quiet");
    // And the loud output must be tamed below the input peak after gain reduction.
    expect(steadyPeak(0.6) < 0.6, "loud peak reduced below input");
}

void outputStaysBoundedAndFinite()
{
    const int N = 256;
    MasterCompressor comp; comp.prepare(48000.0);
    std::vector<float> L(N), R(N);
    for (int i = 0; i < N; ++i) { L[i] = 0.98f; R[i] = -0.98f; }  // hot DC-ish
    float* ch[2] = { L.data(), R.data() };
    for (int b = 0; b < 50; ++b) comp.process(ch, 2, N);
    bool ok = true;
    for (float v : L) if (std::isnan(v) || std::isinf(v) || std::fabs(v) > 1.0f) ok = false;
    expect(ok, "output finite and bounded to +/-1");
}

void stereoGainIsLinked()
{
    // Different L/R amplitudes must receive the SAME gain (no image shift).
    const int N = 256;
    MasterCompressor comp; comp.prepare(48000.0);
    comp.setParams(-18.0, 4.0, 15.0, 200.0, 0.0);
    double phase = 0.0;
    std::vector<float> L(N), R(N);
    double lastL = 0, lastR = 0, inL = 0, inR = 0;
    for (int b = 0; b < 60; ++b) {
        for (int i = 0; i < N; ++i) {
            inL = 0.6 * std::sin(phase);
            inR = 0.3 * std::sin(phase);
            phase += 2.0 * 3.14159265 * 220.0 / 48000.0;
            L[i] = (float) inL; R[i] = (float) inR;
        }
        float* ch[2] = { L.data(), R.data() };
        comp.process(ch, 2, N);
        lastL = L[N - 1]; lastR = R[N - 1];
    }
    // gainL == gainR => out/in equal for both channels (where in != 0).
    if (std::fabs(inL) > 1e-4 && std::fabs(inR) > 1e-4) {
        const double gL = lastL / inL, gR = lastR / inR;
        expect(std::fabs(gL - gR) < 1e-3, "L and R receive identical (linked) gain");
    }
}

void disabledIsPassThrough()
{
    const int N = 64;
    MasterCompressor comp; comp.prepare(48000.0); comp.setEnabled(false);
    std::vector<float> L(N), R(N);
    for (int i = 0; i < N; ++i) { L[i] = 0.5f; R[i] = -0.4f; }
    float* ch[2] = { L.data(), R.data() };
    comp.process(ch, 2, N);
    expect(L[0] == 0.5f && R[0] == -0.4f, "disabled compressor passes audio through unchanged");
}
}

int main()
{
    quietGetsMakeupNotCompressed();
    loudSignalIsCompressed();
    outputStaysBoundedAndFinite();
    stereoGainIsLinked();
    disabledIsPassThrough();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All MasterCompressor tests passed\n";
    return EXIT_SUCCESS;
}
