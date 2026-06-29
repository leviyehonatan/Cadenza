#include "Audio/PolishedMaster.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void expect(bool cond, const std::string& msg)
{
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

using cadenza::audio::PolishedMaster;

constexpr double kSr = 48000.0;
constexpr float  kCeil = PolishedMaster::kCeiling;
constexpr double kTwoPi = 6.28318530717958647692;

// Run a stereo block through a freshly-prepared PolishedMaster and report the
// largest finite magnitude seen, plus whether anything went non-finite.
struct RunResult { float peak = 0.0f; bool anyNonFinite = false; };

RunResult run(std::vector<float> left, std::vector<float> right, int blocks = 4)
{
    PolishedMaster pm;
    pm.prepare(kSr);

    RunResult res;
    const int n = static_cast<int>(left.size());
    for (int b = 0; b < blocks; ++b) {
        // Fresh copy each block so a steady tone keeps feeding in.
        std::vector<float> l = left, r = right;
        float* chans[2] = { l.data(), r.data() };
        pm.process(chans, 2, n);
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) res.anyNonFinite = true;
            res.peak = std::max(res.peak, std::fabs(l[i]));
            res.peak = std::max(res.peak, std::fabs(r[i]));
        }
    }
    return res;
}

std::vector<float> sine(int n, double freq, double amp)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] = static_cast<float>(amp * std::sin(kTwoPi * freq * i / kSr));
    return v;
}

// ---- tests -----------------------------------------------------------------

void silenceStaysSilent()
{
    std::vector<float> z(256, 0.0f);
    auto res = run(z, z, 8);
    expect(!res.anyNonFinite, "silence: output finite");
    expect(res.peak < 1e-6f, "silence stays silent");
}

void disabledIsBypass()
{
    PolishedMaster pm;
    pm.prepare(kSr);
    pm.setEnabled(false);
    std::vector<float> l(64, 0.5f), r(64, -0.5f);
    float* chans[2] = { l.data(), r.data() };
    pm.process(chans, 2, 64);
    bool unchanged = true;
    for (int i = 0; i < 64; ++i)
        if (l[i] != 0.5f || r[i] != -0.5f) unchanged = false;
    expect(unchanged, "disabled: signal passes through untouched");
}

void neverExceedsCeiling()
{
    // A hot full-scale sine that would clip a naive chain.
    auto s = sine(480, 220.0, 1.4);
    auto res = run(s, s, 8);
    expect(!res.anyNonFinite, "loud sine: output finite");
    expect(res.peak <= kCeil + 1e-4f, "loud sine never exceeds ceiling");
}

void extremeInputStaysBounded()
{
    // Absurd input (well above full scale) must not NaN or blow the ceiling.
    std::vector<float> l(256, 0.0f), r(256, 0.0f);
    for (int i = 0; i < 256; ++i) {
        l[static_cast<size_t>(i)] = (i % 2 == 0) ?  9.0f : -9.0f;
        r[static_cast<size_t>(i)] = (i % 3 == 0) ? -7.0f :  6.0f;
    }
    auto res = run(l, r, 6);
    expect(!res.anyNonFinite, "extreme input: output finite (no NaN/inf)");
    expect(res.peak <= kCeil + 1e-4f, "extreme input never exceeds ceiling");
}

void dcInputIsCleanedAndBounded()
{
    std::vector<float> dc(512, 0.8f);
    auto res = run(dc, dc, 8);
    expect(!res.anyNonFinite, "DC: output finite");
    expect(res.peak <= kCeil + 1e-4f, "DC bounded by ceiling");
    // The high-pass should remove most of a steady DC offset over time.
    PolishedMaster pm; pm.prepare(kSr);
    std::vector<float> l(2048, 0.8f), r(2048, 0.8f);
    float* chans[2] = { l.data(), r.data() };
    for (int b = 0; b < 8; ++b) { std::vector<float> a=l, c=r; float* ch[2]={a.data(),c.data()}; pm.process(ch,2,2048);
        if (b == 7) { expect(std::fabs(a.back()) < 0.2f, "DC offset is high-passed away"); } }
}

void stereoDoesNotExplode()
{
    // Hard out-of-phase content (L = -R) is the worst case for a widener.
    auto sL = sine(480, 330.0, 0.7);
    std::vector<float> sR(sL.size());
    for (size_t i = 0; i < sL.size(); ++i) sR[i] = -sL[i];
    auto res = run(sL, sR, 8);
    expect(!res.anyNonFinite, "out-of-phase: output finite");
    expect(res.peak <= kCeil + 1e-4f, "out-of-phase width stays bounded");

    // Mono content (L == R) should also stay bounded and clean.
    auto m = sine(480, 440.0, 0.9);
    auto res2 = run(m, m, 8);
    expect(!res2.anyNonFinite, "mono: output finite");
    expect(res2.peak <= kCeil + 1e-4f, "mono stays bounded");
}

void monoChannelPathWorks()
{
    // numChannels == 1 must not read a second channel.
    PolishedMaster pm; pm.prepare(kSr);
    auto s = sine(256, 200.0, 1.2);
    bool nonFinite = false; float peak = 0.0f;
    for (int b = 0; b < 6; ++b) {
        std::vector<float> l = s;
        float* chans[1] = { l.data() };
        pm.process(chans, 1, 256);
        for (float v : l) { if (!std::isfinite(v)) nonFinite = true; peak = std::max(peak, std::fabs(v)); }
    }
    expect(!nonFinite, "mono path: output finite");
    expect(peak <= kCeil + 1e-4f, "mono path bounded by ceiling");
}
}

int main()
{
    silenceStaysSilent();
    disabledIsBypass();
    neverExceedsCeiling();
    extremeInputStaysBounded();
    dcInputIsCleanedAndBounded();
    stereoDoesNotExplode();
    monoChannelPathWorks();

    if (failures == 0)
        std::cout << "All PolishedMaster tests passed.\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
