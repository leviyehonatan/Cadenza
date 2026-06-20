#include "Audio/ChordAnalysis.h"
#include "Midi/ChordRecognizer.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <initializer_list>
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

constexpr double kPi = 3.141592653589793238462643383279502884;

double midiToFrequency(int midi)
{
    return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
}

std::vector<float> makeChordTone(const std::initializer_list<int>& notes, double seconds,
                                 int sampleRate, double amplitude = 0.75)
{
    const int sampleCount = static_cast<int>(std::round(seconds * sampleRate));
    std::vector<float> out(static_cast<std::size_t>(sampleCount), 0.0f);

    for (int i = 0; i < sampleCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
        double sample = 0.0;
        for (int midi : notes)
            sample += std::sin(2.0 * kPi * midiToFrequency(midi) * t);
        sample /= std::max<std::size_t>(1, notes.size());
        out[static_cast<std::size_t>(i)] = static_cast<float>(sample * amplitude);
    }

    return out;
}

std::vector<float> makeTrack(const std::vector<std::pair<std::initializer_list<int>, double>>& parts,
                             int sampleRate)
{
    std::vector<float> out;
    for (const auto& [notes, seconds] : parts) {
        auto part = makeChordTone(notes, seconds, sampleRate);
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

void detectsChordsAndKey()
{
    const int sampleRate = 22050;
    auto samples = makeTrack({
        {{60, 64, 67}, 1.0},  // C
        {{65, 69, 72}, 1.0},  // F
        {{67, 71, 74}, 1.0},  // G
        {{60, 64, 67}, 1.0},  // C
    }, sampleRate);

    auto result = cadenza::audio::analyzeChordProgression(samples, sampleRate, "synthetic-c-major");

    expect(result.ok, "analysis should succeed");
    expect(result.detectedKeyName == "C major", "expected C major key");
    expect(result.transposeToC == 0, "C major should transpose to C by 0 semitones");
    expect(result.segments.size() >= 3, "should detect multiple chord segments");
    expect(result.segments[0].chord.rootPitchClass == 0,
           "first segment should be C");
    expect(result.segments[0].chord.quality == cadenza::midi::ChordQuality::Major,
           "first segment should be C major");
    expect(result.segments[1].chord.rootPitchClass == 5,
           "second segment should be F");
    expect(result.segments[2].chord.rootPitchClass == 7,
           "third segment should be G");
}

void detectsTransposeToC()
{
    const int sampleRate = 22050;
    auto samples = makeTrack({
        {{69, 73, 76}, 1.5},  // A major
        {{64, 68, 71}, 0.75}, // E major
        {{69, 73, 76}, 1.5},  // A major
    }, sampleRate);

    auto result = cadenza::audio::analyzeChordProgression(samples, sampleRate, "synthetic-a-major");

    expect(result.ok, "A major analysis should succeed");
    expect(result.detectedKeyName == "A major", "expected A major key, got " + result.detectedKeyName);
    expect(result.transposeToC == 3, "A major should transpose up 3 semitones to C, got " + std::to_string(result.transposeToC) + " from key " + result.detectedKeyName);
    expect(!result.segments.empty(), "should detect at least one segment");
    expect(result.segments.front().chord.rootPitchClass == 9,
           "first chord should be A");
}
}

int main()
{
    detectsChordsAndKey();
    detectsTransposeToC();

    if (failures != 0)
        return EXIT_FAILURE;

    std::cout << "All AudioChordAnalysis tests passed\n";
    return EXIT_SUCCESS;
}
