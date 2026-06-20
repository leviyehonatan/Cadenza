#include "Audio/ChordAnalysis.h"
#include "Midi/ChordTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace cadenza::audio
{
namespace
{
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr int kMinMidi = 36;
constexpr int kMaxMidi = 84;
constexpr int kFrameSize = 8192;
constexpr int kHopSize = 4096;

int wrapPitchClass(int pc)
{
    int v = pc % 12;
    return v < 0 ? v + 12 : v;
}

double midiToFrequency(int midi)
{
    return 440.0 * std::pow(2.0, (static_cast<double>(midi) - 69.0) / 12.0);
}

double goertzelPower(const float* samples, int count, int sampleRate, double freq)
{
    if (count <= 0 || sampleRate <= 0 || freq <= 0.0)
        return 0.0;

    const double w = 2.0 * kPi * freq / static_cast<double>(sampleRate);
    const double coeff = 2.0 * std::cos(w);
    double sPrev = 0.0;
    double sPrev2 = 0.0;
    for (int i = 0; i < count; ++i) {
        const double win = (count <= 1)
            ? 1.0
            : 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(count - 1));
        const double x = static_cast<double>(samples[i]) * win;
        const double s = x + coeff * sPrev - sPrev2;
        sPrev2 = sPrev;
        sPrev = s;
    }
    return sPrev2 * sPrev2 + sPrev * sPrev - coeff * sPrev * sPrev2;
}

int semitoneShiftToC(int keyPitchClass)
{
    int shift = wrapPitchClass(0 - keyPitchClass);
    if (shift > 5)
        shift -= 12;
    return shift;
}

std::string keyName(int pc, bool minor)
{
    return cadenza::midi::pitchClassName(pc) + (minor ? " minor" : " major");
}

std::string chordName(const cadenza::midi::Chord& chord)
{
    return chord.toString();
}

std::vector<std::pair<int, int>> topPitchClasses(const std::array<double, 12>& chroma)
{
    std::vector<std::pair<int, int>> order;
    order.reserve(12);
    for (int pc = 0; pc < 12; ++pc)
        order.emplace_back(pc, static_cast<int>(pc));
    std::sort(order.begin(), order.end(), [&](const auto& a, const auto& b) {
        if (chroma[a.first] != chroma[b.first])
            return chroma[a.first] > chroma[b.first];
        return a.first < b.first;
    });
    return order;
}

std::uint16_t buildMask(const std::array<double, 12>& chroma)
{
    double maxEnergy = 0.0;
    for (double v : chroma)
        maxEnergy = std::max(maxEnergy, v);
    if (maxEnergy <= 0.0)
        return 0;

    std::uint16_t mask = 0;
    const auto order = topPitchClasses(chroma);
    for (int i = 0; i < 3 && i < static_cast<int>(order.size()); ++i)
        mask |= cadenza::midi::pcBit(order[static_cast<std::size_t>(i)].first);
    return mask;
}

int estimateBassPc(const std::vector<double>& noteEnergy)
{
    double best = 0.0;
    int bestPc = 0;
    for (int midi = kMinMidi; midi <= std::min(kMaxMidi, 55); ++midi) {
        const int idx = midi - kMinMidi;
        if (noteEnergy[static_cast<std::size_t>(idx)] > best) {
            best = noteEnergy[static_cast<std::size_t>(idx)];
            bestPc = midi % 12;
        }
    }
    if (best > 0.0)
        return bestPc;

    for (int midi = kMinMidi; midi <= kMaxMidi; ++midi) {
        const int idx = midi - kMinMidi;
        if (noteEnergy[static_cast<std::size_t>(idx)] > best) {
            best = noteEnergy[static_cast<std::size_t>(idx)];
            bestPc = midi % 12;
        }
    }
    return bestPc;
}

std::optional<cadenza::midi::Chord> analyseFrame(const float* frame,
                                                  int frameCount,
                                                  int sampleRate,
                                                  std::array<double, 12>& chromaOut,
                                                  double& confidenceOut)
{
    std::vector<double> noteEnergy(static_cast<std::size_t>(kMaxMidi - kMinMidi + 1), 0.0);
    chromaOut.fill(0.0);

    double totalEnergy = 0.0;
    for (int midi = kMinMidi; midi <= kMaxMidi; ++midi) {
        const double freq = midiToFrequency(midi);
        const double power = goertzelPower(frame, frameCount, sampleRate, freq);
        const double weight = 1.0 / (1.0 + std::abs(midi - 60) / 24.0);
        const double e = std::max(0.0, power) * weight;
        noteEnergy[static_cast<std::size_t>(midi - kMinMidi)] = e;
        chromaOut[static_cast<std::size_t>(midi % 12)] += e;
        totalEnergy += e;
    }

    if (totalEnergy <= std::numeric_limits<double>::epsilon()) {
        confidenceOut = 0.0;
        return std::nullopt;
    }

    const std::uint16_t mask = buildMask(chromaOut);
    const int bassPc = estimateBassPc(noteEnergy);
    confidenceOut = 0.0;
    for (double v : chromaOut)
        confidenceOut = std::max(confidenceOut, v);
    confidenceOut /= totalEnergy;

    if (mask == 0)
        return std::nullopt;

    if (auto match = cadenza::midi::matchChordMask(mask, bassPc, 1)) {
        cadenza::midi::Chord chord;
        chord.rootPitchClass = match->root;
        chord.quality = match->info->quality;
        chord.bassMidi = bassPc;
        return chord;
    }

    const auto order = topPitchClasses(chromaOut);
    cadenza::midi::Chord chord;
    chord.rootPitchClass = order.front().first;
    chord.quality = cadenza::midi::ChordQuality::SingleNote;
    chord.bassMidi = order.front().first;
    return chord;
}

bool isSameChord(const cadenza::midi::Chord& a, const cadenza::midi::Chord& b)
{
    return a.rootPitchClass == b.rootPitchClass && a.quality == b.quality;
}
}

AudioChordAnalysisResult analyzeChordProgression(const std::vector<float>& monoSamples,
                                                 int sampleRate,
                                                 const std::string& sourceName,
                                                 int targetKeyPitchClass)
{
    AudioChordAnalysisResult result;
    result.sourceName = sourceName;
    result.sampleRate = sampleRate;
    if (sampleRate <= 0) {
        result.ok = false;
        result.error = "invalid sample rate";
        return result;
    }
    if (monoSamples.empty()) {
        result.ok = false;
        result.error = "no audio samples";
        return result;
    }

    result.durationSeconds = static_cast<double>(monoSamples.size()) / static_cast<double>(sampleRate);
    std::array<double, 12> totalChroma { {} };
    std::vector<ChordSegment> segments;

    const int frameSize = std::min<int>(kFrameSize, static_cast<int>(monoSamples.size()));
    const int hopSize = std::max(1, std::min(kHopSize, frameSize));

    std::optional<cadenza::midi::Chord> lastChord;
    double lastStart = 0.0;
    double lastEnd = 0.0;
    double lastConfidence = 0.0;

    for (int start = 0; start < static_cast<int>(monoSamples.size()); start += hopSize) {
        const int remaining = static_cast<int>(monoSamples.size()) - start;
        const int count = std::min(frameSize, remaining);
        if (count <= 0)
            break;

        std::array<double, 12> chroma { {} };
        double confidence = 0.0;
        const auto chord = analyseFrame(monoSamples.data() + start, count, sampleRate, chroma, confidence);
        const double frameStart = static_cast<double>(start) / static_cast<double>(sampleRate);
        const double frameEnd = static_cast<double>(start + count) / static_cast<double>(sampleRate);

        for (int i = 0; i < 12; ++i)
            totalChroma[static_cast<std::size_t>(i)] += chroma[static_cast<std::size_t>(i)];

        if (!chord.has_value())
            continue;

        if (!lastChord.has_value()) {
            lastChord = chord;
            lastStart = frameStart;
            lastEnd = frameEnd;
            lastConfidence = confidence;
            continue;
        }

        if (isSameChord(*lastChord, *chord)) {
            lastEnd = frameEnd;
            lastConfidence = std::max(lastConfidence, confidence);
            continue;
        }

        segments.push_back({ lastStart, lastEnd, *lastChord, lastConfidence });
        lastChord = chord;
        lastStart = frameStart;
        lastEnd = frameEnd;
        lastConfidence = confidence;
    }

    if (lastChord.has_value())
        segments.push_back({ lastStart, lastEnd, *lastChord, lastConfidence });

    if (segments.empty()) {
        result.ok = false;
        result.error = "no chord segments detected";
        return result;
    }

    // Estimate the song centre from the detected chord roots, weighted by duration.
    std::array<double, 12> rootWeights { {} };
    std::array<double, 12> majorWeights { {} };
    std::array<double, 12> minorWeights { {} };
    auto isMinorQuality = [](cadenza::midi::ChordQuality q) {
        switch (q) {
            case cadenza::midi::ChordQuality::Minor:
            case cadenza::midi::ChordQuality::Minor7:
            case cadenza::midi::ChordQuality::MinorMajor7:
            case cadenza::midi::ChordQuality::Minor6:
            case cadenza::midi::ChordQuality::Minor9:
            case cadenza::midi::ChordQuality::Minor11:
            case cadenza::midi::ChordQuality::MinorAdd9:
            case cadenza::midi::ChordQuality::MinorMajor9:
                return true;
            default:
                return false;
        }
    };

    for (const auto& segment : segments) {
        const double weight = std::max(0.25, (segment.endSeconds - segment.startSeconds) * (0.5 + segment.confidence));
        const int root = wrapPitchClass(segment.chord.rootPitchClass);
        rootWeights[static_cast<std::size_t>(root)] += weight;
        if (isMinorQuality(segment.chord.quality))
            minorWeights[static_cast<std::size_t>(root)] += weight;
        else
            majorWeights[static_cast<std::size_t>(root)] += weight;
    }

    int bestRoot = 0;
    double bestRootWeight = -1.0;
    for (int root = 0; root < 12; ++root) {
        if (rootWeights[static_cast<std::size_t>(root)] > bestRootWeight) {
            bestRootWeight = rootWeights[static_cast<std::size_t>(root)];
            bestRoot = root;
        }
    }

    bool bestMinor = minorWeights[static_cast<std::size_t>(bestRoot)] > majorWeights[static_cast<std::size_t>(bestRoot)];
    result.detectedKeyPitchClass = bestRoot;
    result.detectedKeyMinor = bestMinor;
    result.detectedKeyName = keyName(bestRoot, bestMinor);
    result.transposeToC = semitoneShiftToC(bestRoot);
    if (targetKeyPitchClass != 0)
        result.transposeToC = semitoneShiftToC(bestRoot - targetKeyPitchClass);
    result.segments = std::move(segments);
    return result;
}

std::string formatChordAnalysisSummary(const AudioChordAnalysisResult& result)
{
    if (!result.ok)
        return result.error.empty() ? std::string("analysis failed") : result.error;

    std::ostringstream out;
    if (!result.sourceName.empty())
        out << result.sourceName << '\n';
    out << "Detected key: " << result.detectedKeyName << '\n';
    out << "Transpose to C: " << (result.transposeToC > 0 ? "+" : "") << result.transposeToC << " semitones\n";
    out << "Chords:";
    for (const auto& segment : result.segments) {
        out << '\n';
        out << "  " << segment.startSeconds << "s-" << segment.endSeconds << "s  "
            << chordName(segment.chord);
    }
    return out.str();
}
}
