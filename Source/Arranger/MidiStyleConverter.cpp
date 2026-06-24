#include "MidiStyleConverter.h"

#include "StyleLoader.h"
#include "../Midi/ChordTypes.h"
#include "../Midi/GmInstruments.h"
#include "../MusicalTiming.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <numeric>
#include <limits>
#include <vector>

namespace cadenza::arranger
{
namespace
{
struct RawNote
{
    int track = 0;
    int channel = 1;
    int start = 0;
    int end = 0;
    int pitch = 60;
    int velocity = 100;
};

struct RawAutomation
{
    int tick = 0;
    int channel = 1;
    int type = 0;
    int value = 0;
};

struct ChannelState
{
    int program = -1;
    std::optional<int> bankMsb;
    std::optional<int> bankLsb;
    std::optional<int> volume;
    std::optional<int> pan;
    std::optional<int> reverb;
    std::optional<int> chorus;
};

struct SourceGroup
{
    int track = 0;
    int channel = 1;
    juce::String trackName;
    ChannelState state;
    std::vector<RawNote> notes;
    std::vector<RawAutomation> automation;
};

struct ParsedMidi
{
    int ppq = 0;
    int tempo = 120;
    int beatsPerBar = 4;
    int beatUnit = 4;
    int lastTick = 0;
    int tempoTick = std::numeric_limits<int>::max();
    bool tempoSeen = false;
    int timeSignatureTick = std::numeric_limits<int>::max();
    bool timeSignatureSeen = false;
    std::vector<SourceGroup> groups;
};

struct OpenNote
{
    int start = 0;
    int velocity = 0;
};

enum class Slot
{
    Drop,
    Drums,
    Bass,
    Chord,
    Pad,
    Phrase,
};

struct SlotInfo
{
    const char* name;
    int channel;
    bool percussion;
    YamahaNtr ntr;
    YamahaNtt ntt;
    bool bassOn;
};

const SlotInfo& slotInfo(const std::string& name)
{
    static const SlotInfo infos[] = {
        { "drums", 10, true,  YamahaNtr::RootFixed,        YamahaNtt::Bypass, false },
        { "bass", 11, false, YamahaNtr::RootTransposition, YamahaNtt::Bypass, true  },
        { "chord1", 12, false, YamahaNtr::RootTransposition, YamahaNtt::Chord, false },
        { "chord2", 13, false, YamahaNtr::RootTransposition, YamahaNtt::Chord, false },
        { "pad", 14, false, YamahaNtr::RootTransposition, YamahaNtt::Chord, false },
        { "phrase1", 15, false, YamahaNtr::RootTransposition, YamahaNtt::Melody, false },
        { "phrase2", 16, false, YamahaNtr::RootTransposition, YamahaNtt::Melody, false },
    };
    for (const auto& info : infos)
        if (name == info.name)
            return info;
    return infos[0];
}

juce::String lower(juce::String s)
{
    return s.toLowerCase();
}

std::string sanitizedId(const juce::String& text)
{
    std::string id;
    for (const auto c : text.toStdString()) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc))
            id += static_cast<char>(std::tolower(uc));
        else if (c == ' ' || c == '-' || c == '_')
            id += '-';
    }
    while (id.find("--") != std::string::npos)
        id.replace(id.find("--"), 2, "-");
    while (!id.empty() && id.front() == '-') id.erase(id.begin());
    while (!id.empty() && id.back() == '-') id.pop_back();
    return id.empty() ? "imported-midi-style" : id;
}

int pc(int pitch) noexcept
{
    int v = pitch % 12;
    return v < 0 ? v + 12 : v;
}

const char* rootName(int root) noexcept
{
    static const char* names[12] = {
        "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B"
    };
    return names[pc(root)];
}

int nearestTransposeToC(int sourceRootPc) noexcept
{
    int shift = pc(-sourceRootPc);
    if (shift >= 6)
        shift -= 12;
    return shift;
}

NoteRole roleFromSourceChord(int pitch, int sourceRootPc, const std::string& sourceChord) noexcept
{
    const auto interval = pc(pitch - sourceRootPc);
    const bool majorNamed = sourceChord.rfind("maj", 0) == 0;
    const bool minorish = (!majorNamed && sourceChord.rfind("m", 0) == 0)
        || sourceChord.find("dim") != std::string::npos;
    const bool augmented = sourceChord.find("aug") != std::string::npos;
    switch (interval) {
        case 0:  return NoteRole::ChordRoot;
        case 3:  return minorish ? NoteRole::Chord3 : NoteRole::ChordColor;
        case 4:  return minorish ? NoteRole::ChordColor : NoteRole::Chord3;
        case 6:  return sourceChord.find("dim") != std::string::npos ? NoteRole::Chord5 : NoteRole::ChordColor;
        case 7:  return augmented ? NoteRole::ChordColor : NoteRole::Chord5;
        case 8:  return augmented ? NoteRole::Chord5 : NoteRole::ChordColor;
        case 9:
        case 10:
        case 11: return NoteRole::Chord7;
        default: return NoteRole::ChordColor;
    }
}

YamahaChannelPolicy makePolicy(const std::string& partName,
                               const std::string& sourceRoot,
                               const std::string& sourceChord)
{
    const auto& info = slotInfo(partName);
    YamahaChannelPolicy policy;
    policy.source = YamahaPolicySource::Fallback;
    policy.sourceChannel = info.channel;
    policy.destinationPart = partName;
    policy.destinationType = partName;
    policy.destinationName = partName;
    policy.sourceRoot = sourceRoot;
    policy.sourceChord = sourceChord;
    policy.ntr = info.ntr;
    policy.ntt = info.ntt;
    policy.bassOn = info.bassOn;
    policy.retriggerRule = YamahaRetriggerRule::PitchShift;
    return policy;
}

bool isBassProgram(int program) noexcept { return program >= 32 && program <= 39; }
bool isPianoOrganGuitar(int program) noexcept
{
    const int fam = program / 8;
    return fam == 0 || fam == 2 || fam == 3 || fam == 6;
}
bool isPadProgram(int program) noexcept
{
    const int fam = program / 8;
    return fam == 5 || fam == 6 || fam == 11;
}
bool isPhraseProgram(int program) noexcept
{
    const int fam = program / 8;
    return fam == 7 || fam == 8 || fam == 10 || fam == 3;
}

double overlapRatio(const SourceGroup& group)
{
    struct Edge { int tick; int delta; };
    std::vector<Edge> edges;
    for (const auto& n : group.notes) {
        edges.push_back({ n.start, 1 });
        edges.push_back({ n.end, -1 });
    }
    std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
        return a.tick == b.tick ? a.delta < b.delta : a.tick < b.tick;
    });
    int active = 0;
    int polyEdges = 0;
    for (const auto& e : edges) {
        active += e.delta;
        if (active > 1)
            ++polyEdges;
    }
    return edges.empty() ? 0.0 : static_cast<double>(polyEdges) / static_cast<double>(edges.size());
}

Slot classifyGroup(const SourceGroup& group)
{
    const auto name = lower(group.trackName);
    const int program = group.state.program;
    if (group.channel == 10 || name.contains("drum") || name.contains("kit") || name.contains("perc"))
        return Slot::Drums;
    if ((program >= 0 && isBassProgram(program)) || name.contains("bass"))
        return Slot::Bass;

    const double totalDur = std::accumulate(
        group.notes.begin(), group.notes.end(), 0.0,
        [](double sum, const RawNote& n) { return sum + std::max(1, n.end - n.start); });
    const double avgDur = group.notes.empty() ? 0.0 : totalDur / group.notes.size();
    const bool poly = overlapRatio(group) > 0.18;

    if ((program >= 0 && isPadProgram(program)) || name.contains("pad") || name.contains("string")) {
        if (avgDur >= 720.0 || group.notes.size() <= 12)
            return Slot::Pad;
    }
    if (program >= 0 && isPhraseProgram(program) && !poly)
        return Slot::Phrase;
    if ((program >= 0 && isPianoOrganGuitar(program)) || poly)
        return Slot::Chord;
    return Slot::Phrase;
}

int strength(const SourceGroup& group)
{
    int duration = 0;
    for (const auto& n : group.notes)
        duration += std::max(1, n.end - n.start);
    return static_cast<int>(group.notes.size() * 1000) + duration / 16;
}

void copyProgramState(Part& part, const ChannelState& state)
{
    if (state.program >= 0) {
        part.program = state.program;
        part.instrument = cadenza::midi::gmInstrumentName(state.program);
    }
    part.bankMsb = state.bankMsb;
    part.bankLsb = state.bankLsb;
    part.volume = state.volume;
    part.pan = state.pan;
    part.reverb = state.reverb;
    part.chorus = state.chorus;
}

void appendGroup(Part& part,
                 const SourceGroup& group,
                 int startTick,
                 int endTick,
                 int sourceRootPc,
                 const std::string& sourceChord,
                 int transposeSemitones)
{
    for (const auto& n : group.notes) {
        if (n.end <= startTick || n.start >= endTick)
            continue;
        PatternNote out;
        out.tick = std::max(n.start, startTick) - startTick;
        out.duration = std::max(1, std::min(n.end, endTick) - std::max(n.start, startTick));
        const int pitch = n.pitch + (part.percussion ? 0 : transposeSemitones);
        out.pitch = std::clamp(pitch, 0, 127);
        out.velocity = std::clamp(n.velocity, 1, 127);
        out.role = part.percussion ? NoteRole::Absolute
                                   : roleFromSourceChord(out.pitch, sourceRootPc, sourceChord);
        part.notes.push_back(out);
    }

    for (const auto& a : group.automation) {
        if (a.tick < startTick || a.tick >= endTick)
            continue;
        part.automation.push_back({ a.tick - startTick, a.type, a.value });
    }
}

void sortPart(Part& part)
{
    std::sort(part.notes.begin(), part.notes.end(),
              [](const PatternNote& a, const PatternNote& b) {
                  if (a.tick != b.tick) return a.tick < b.tick;
                  return a.pitch < b.pitch;
              });
    std::sort(part.automation.begin(), part.automation.end(),
              [](const AutomationEvent& a, const AutomationEvent& b) {
                  return a.tick < b.tick;
              });
}

std::map<std::pair<int, int>, SourceGroup> readGroups(const juce::MidiFile& midi,
                                                      ParsedMidi& parsed)
{
    std::map<std::pair<int, int>, SourceGroup> groups;

    for (int t = 0; t < midi.getNumTracks(); ++t) {
        const auto* seq = midi.getTrack(t);
        if (seq == nullptr)
            continue;

        juce::String trackName;
        std::array<ChannelState, 16> state;
        std::array<std::array<std::vector<OpenNote>, 128>, 16> open;

        for (int i = 0; i < seq->getNumEvents(); ++i) {
            const auto* holder = seq->getEventPointer(i);
            if (holder == nullptr)
                continue;
            const auto& msg = holder->message;
            const int tick = juce::roundToInt(msg.getTimeStamp());
            parsed.lastTick = std::max(parsed.lastTick, tick);

            if (msg.isTrackNameEvent() || msg.isTextMetaEvent()) {
                if (trackName.isEmpty())
                    trackName = msg.getTextFromTextMetaEvent();
            }
            if (msg.isTempoMetaEvent() && tick < parsed.tempoTick) {
                const auto seconds = msg.getTempoSecondsPerQuarterNote();
                if (seconds > 0.0) {
                    parsed.tempo = std::clamp(juce::roundToInt(60.0 / seconds), 20, 300);
                    parsed.tempoTick = tick;
                    parsed.tempoSeen = true;
                }
            }
            if (msg.isTimeSignatureMetaEvent() && tick < parsed.timeSignatureTick) {
                int numerator = 0;
                int denominator = 0;
                msg.getTimeSignatureInfo(numerator, denominator);
                if (numerator > 0 && denominator > 0) {
                    parsed.beatsPerBar = numerator;
                    parsed.beatUnit = denominator;
                    parsed.timeSignatureTick = tick;
                    parsed.timeSignatureSeen = true;
                }
            }

            const int channel = msg.getChannel();
            if (channel < 1 || channel > 16)
                continue;
            auto& chState = state[static_cast<std::size_t>(channel - 1)];

            if (msg.isProgramChange()) {
                chState.program = std::clamp(msg.getProgramChangeNumber() - 1, 0, 127);
                continue;
            }
            if (msg.isController()) {
                const int cc = msg.getControllerNumber();
                const int value = msg.getControllerValue();
                if (cc == 0) chState.bankMsb = value;
                else if (cc == 32) chState.bankLsb = value;
                else if (cc == 7) chState.volume = value;
                else if (cc == 10) chState.pan = value;
                else if (cc == 91) chState.reverb = value;
                else if (cc == 93) chState.chorus = value;
                if (cc == 1 || cc == 11 || cc == 64) {
                    auto& group = groups[{ t, channel }];
                    group.track = t;
                    group.channel = channel;
                    group.trackName = trackName;
                    group.automation.push_back({ tick, channel, cc, value });
                }
                continue;
            }
            if (msg.isPitchWheel()) {
                auto& group = groups[{ t, channel }];
                group.track = t;
                group.channel = channel;
                group.trackName = trackName;
                group.automation.push_back({
                    tick, channel, AutomationEvent::kPitchBend, msg.getPitchWheelValue()
                });
                continue;
            }

            const int note = msg.getNoteNumber();
            if (note < 0 || note > 127)
                continue;
            auto& stack = open[static_cast<std::size_t>(channel - 1)]
                              [static_cast<std::size_t>(note)];
            if (msg.isNoteOn()) {
                stack.push_back({ tick, static_cast<int>(msg.getVelocity()) });
            } else if (msg.isNoteOff(true)) {
                if (!stack.empty()) {
                    const auto started = stack.back();
                    stack.pop_back();
                    auto& group = groups[{ t, channel }];
                    group.track = t;
                    group.channel = channel;
                    group.trackName = trackName;
                    group.notes.push_back({
                        t, channel, started.start, std::max(started.start + 1, tick),
                        note, std::clamp(started.velocity, 1, 127)
                    });
                }
            }
        }

        for (int ch = 1; ch <= 16; ++ch) {
            auto it = groups.find({ t, ch });
            if (it != groups.end()) {
                it->second.trackName = trackName;
                it->second.state = state[static_cast<std::size_t>(ch - 1)];
            }
        }
    }
    return groups;
}

std::optional<ParsedMidi> parseMidi(const juce::File& file, juce::StringArray& warnings)
{
    juce::FileInputStream input(file);
    if (!input.openedOk()) {
        warnings.add("Could not open MIDI file");
        return std::nullopt;
    }

    juce::MidiFile midi;
    if (!midi.readFrom(input)) {
        warnings.add("Could not read MIDI file");
        return std::nullopt;
    }

    ParsedMidi parsed;
    parsed.ppq = midi.getTimeFormat();
    if (parsed.ppq <= 0) {
        warnings.add("SMPTE MIDI timing is not supported; import requires PPQ timing");
        return std::nullopt;
    }

    auto groupsByKey = readGroups(midi, parsed);
    for (auto& [key, group] : groupsByKey) {
        if (!group.notes.empty() || !group.automation.empty())
            parsed.groups.push_back(std::move(group));
    }
    return parsed;
}

struct DetectedChord
{
    int root = 0;
    std::string suffix;
    bool fallback = true;
    MidiStyleChordConfidence confidence = MidiStyleChordConfidence::Low;
    juce::String confidenceReason = "No confident chord evidence";
};

DetectedChord detectSourceChord(const std::vector<SourceGroup*>& harmonic,
                                int startTick,
                                int endTick)
{
    std::array<double, 12> weights {};
    int bassPc = -1;
    int lowestPitch = 128;
    int lowestPc = 0;
    int noteCount = 0;
    int nonBassNoteCount = 0;
    int weightedPitchClasses = 0;

    for (const auto* group : harmonic) {
        const bool bass = classifyGroup(*group) == Slot::Bass;
        for (const auto& n : group->notes) {
            if (n.end <= startTick || n.start >= endTick)
                continue;
            ++noteCount;
            if (!bass)
                ++nonBassNoteCount;
            const int p = pc(n.pitch);
            const int clippedStart = std::max(n.start, startTick);
            const int clippedEnd = std::min(n.end, endTick);
            double w = static_cast<double>(std::max(1, clippedEnd - clippedStart));
            if ((clippedStart - startTick) % 1920 == 0)
                w *= 1.75;
            if (bass) {
                w *= n.pitch < 60 ? 2.5 : 1.5;
                if (bassPc < 0 && clippedStart <= startTick + 120)
                    bassPc = p;
            }
            weights[static_cast<std::size_t>(p)] += w;
            if (n.pitch < lowestPitch) {
                lowestPitch = n.pitch;
                lowestPc = p;
            }
        }
    }

    std::uint16_t mask = 0;
    const double maxWeight = *std::max_element(weights.begin(), weights.end());
    if (maxWeight <= 0.0)
        return {};
    for (int i = 0; i < 12; ++i)
        if (weights[static_cast<std::size_t>(i)] >= maxWeight * 0.20) {
            mask = static_cast<std::uint16_t>(mask | cadenza::midi::pcBit(i));
            ++weightedPitchClasses;
        }

    const int rootHint = bassPc >= 0 ? bassPc : lowestPc;
    if (const auto match = cadenza::midi::matchChordMask(mask, rootHint, 3)) {
        DetectedChord detected { match->root, match->info != nullptr ? match->info->suffix : "", false };
        const bool hasMinorThird = (mask & cadenza::midi::pcBit(match->root + 3)) != 0;
        const bool hasMajorThird = (mask & cadenza::midi::pcBit(match->root + 4)) != 0;
        const bool hasThird = hasMinorThird || hasMajorThird;
        const bool hasFifth = (mask & cadenza::midi::pcBit(match->root + 7)) != 0
            || (mask & cadenza::midi::pcBit(match->root + 6)) != 0
            || (mask & cadenza::midi::pcBit(match->root + 8)) != 0;

        if (hasThird && hasFifth && nonBassNoteCount >= 3 && weightedPitchClasses >= 3) {
            detected.confidence = MidiStyleChordConfidence::High;
            detected.confidenceReason = "Clear triad with a third present";
        } else if (hasThird && noteCount >= 2) {
            detected.confidence = MidiStyleChordConfidence::Medium;
            detected.confidenceReason = "Matched chord, but evidence is thin";
        } else {
            detected.confidence = MidiStyleChordConfidence::Low;
            detected.confidenceReason = "Matched chord without a clear third";
        }
        return detected;
    }
    return {};
}

std::vector<SourceGroup*> groupsForSlots(ParsedMidi& parsed,
                                         Slot wanted)
{
    std::vector<SourceGroup*> result;
    for (auto& group : parsed.groups)
        if (classifyGroup(group) == wanted)
            result.push_back(&group);
    return result;
}

std::vector<SourceGroup*> harmonicGroups(ParsedMidi& parsed)
{
    std::vector<SourceGroup*> harmonic;
    auto basses = groupsForSlots(parsed, Slot::Bass);
    auto chords = groupsForSlots(parsed, Slot::Chord);
    auto pads = groupsForSlots(parsed, Slot::Pad);
    harmonic.insert(harmonic.end(), basses.begin(), basses.end());
    harmonic.insert(harmonic.end(), chords.begin(), chords.end());
    harmonic.insert(harmonic.end(), pads.begin(), pads.end());
    return harmonic;
}

bool noteOverlaps(const RawNote& note, int startTick, int endTick) noexcept
{
    return note.end > startTick && note.start < endTick;
}

int countNotesInWindow(const SourceGroup& group, int startTick, int endTick)
{
    int count = 0;
    for (const auto& note : group.notes)
        if (noteOverlaps(note, startTick, endTick))
            ++count;
    return count;
}

int countBarsWithNotes(const std::vector<SourceGroup*>& groups,
                       int startTick,
                       int barTicks,
                       int barCount)
{
    int bars = 0;
    for (int b = 0; b < barCount; ++b) {
        const int barStart = startTick + b * barTicks;
        const int barEnd = barStart + barTicks;
        bool hasNotes = false;
        for (const auto* group : groups) {
            if (countNotesInWindow(*group, barStart, barEnd) > 0) {
                hasNotes = true;
                break;
            }
        }
        if (hasNotes)
            ++bars;
    }
    return bars;
}

struct RangeScore
{
    int barStart = 0;
    int barCount = 4;
    double score = 0.0;
    bool clearlyFull = false;
};

RangeScore scoreRange(ParsedMidi& parsed, int barStart, int barCount, int barTicks)
{
    const int startTick = barStart * barTicks;
    const int endTick = startTick + barCount * barTicks;

    std::vector<SourceGroup*> drums;
    std::vector<SourceGroup*> basses;
    std::vector<SourceGroup*> chords;
    std::vector<SourceGroup*> pads;
    std::vector<SourceGroup*> phrases;
    for (auto& group : parsed.groups) {
        switch (classifyGroup(group)) {
            case Slot::Drums: drums.push_back(&group); break;
            case Slot::Bass: basses.push_back(&group); break;
            case Slot::Chord: chords.push_back(&group); break;
            case Slot::Pad: pads.push_back(&group); break;
            case Slot::Phrase: phrases.push_back(&group); break;
            case Slot::Drop: break;
        }
    }

    auto hasAny = [&](const std::vector<SourceGroup*>& groups) {
        for (const auto* group : groups)
            if (countNotesInWindow(*group, startTick, endTick) > 0)
                return true;
        return false;
    };

    std::vector<SourceGroup*> harmonyParts;
    harmonyParts.insert(harmonyParts.end(), chords.begin(), chords.end());
    harmonyParts.insert(harmonyParts.end(), pads.begin(), pads.end());

    const bool hasDrums = hasAny(drums);
    const bool hasBass = hasAny(basses);
    const int harmonyBars = countBarsWithNotes(harmonyParts, startTick, barTicks, barCount);
    const double harmonyCoverage = std::clamp(
        static_cast<double>(harmonyBars) / static_cast<double>(std::max(1, barCount)), 0.0, 1.0);
    const bool hasHarmony = harmonyCoverage > 0.0;
    const bool hasPhrase = hasAny(phrases);

    int activeSlots = 0;
    for (const bool active : { hasDrums, hasBass, hasHarmony, hasPhrase })
        if (active)
            ++activeSlots;

    std::vector<SourceGroup*> harmonic;
    harmonic.insert(harmonic.end(), basses.begin(), basses.end());
    harmonic.insert(harmonic.end(), chords.begin(), chords.end());
    harmonic.insert(harmonic.end(), pads.begin(), pads.end());
    const auto detected = detectSourceChord(harmonic, startTick, endTick);
    const auto harmonyDetected = detectSourceChord(harmonyParts, startTick, endTick);
    const bool hasThirdBearingHarmony = harmonyDetected.confidence != MidiStyleChordConfidence::Low;
    const double thirdBonus = hasThirdBearingHarmony
        ? (harmonyDetected.confidence == MidiStyleChordConfidence::High ? 20.0 : 9.0)
        : 0.0;

    std::vector<int> noteCounts;
    noteCounts.reserve(static_cast<std::size_t>(barCount));
    for (int b = 0; b < barCount; ++b) {
        const int barBegin = startTick + b * barTicks;
        const int barEnd = barBegin + barTicks;
        int count = 0;
        for (const auto* group : { &drums, &basses, &chords, &pads, &phrases })
            for (const auto* source : *group)
                count += countNotesInWindow(*source, barBegin, barEnd);
        noteCounts.push_back(count);
    }

    const double mean = noteCounts.empty() ? 0.0
        : static_cast<double>(std::accumulate(noteCounts.begin(), noteCounts.end(), 0))
            / static_cast<double>(noteCounts.size());
    double variance = 0.0;
    for (const int count : noteCounts)
        variance += (static_cast<double>(count) - mean) * (static_cast<double>(count) - mean);
    variance = noteCounts.empty() ? 0.0 : variance / static_cast<double>(noteCounts.size());
    const double cv = mean > 0.0 ? std::sqrt(variance) / mean : 2.0;
    const int minDensity = noteCounts.empty() ? 0 : *std::min_element(noteCounts.begin(), noteCounts.end());

    double densityScore = 0.0;
    if (mean < 2.0)
        densityScore -= 30.0;
    else if (mean < 5.0)
        densityScore -= 12.0;
    else
        densityScore += std::max(0.0, 18.0 * (1.0 - std::min(cv, 1.0)));
    if (mean > 90.0)
        densityScore -= std::min(25.0, (mean - 90.0) * 0.25);
    if (minDensity == 0)
        densityScore -= 18.0;

    RangeScore result;
    result.barStart = barStart;
    result.barCount = barCount;
    result.score = activeSlots * 8.0
        + (hasDrums ? 12.0 : 0.0)
        + (hasBass ? 14.0 : 0.0)
        + harmonyCoverage * 26.0
        + (hasDrums && hasBass && harmonyCoverage >= 1.0 && hasThirdBearingHarmony ? 25.0 : 0.0)
        + thirdBonus * harmonyCoverage
        + densityScore;
    result.clearlyFull = hasDrums && hasBass && harmonyCoverage >= 1.0
        && hasThirdBearingHarmony
        && detected.confidence != MidiStyleChordConfidence::Low
        && mean >= 5.0
        && cv <= 0.75;
    return result;
}

MidiStyleRecommendedRange recommendRange(ParsedMidi& parsed,
                                         int totalBars,
                                         int barTicks,
                                         int targetBarCount)
{
    MidiStyleRecommendedRange fallback;
    fallback.barStart = 0;
    fallback.barCount = std::clamp(std::max(1, targetBarCount), 1, std::max(1, totalBars));
    fallback.fallback = true;

    if (totalBars <= 1 || barTicks <= 0)
        return fallback;

    const int count = fallback.barCount;
    const int lastStart = std::max(0, totalBars - count);
    RangeScore best { 0, count, -std::numeric_limits<double>::infinity(), false };
    for (int start = 0; start <= lastStart; ++start) {
        const auto score = scoreRange(parsed, start, count, barTicks);
        if (score.clearlyFull && score.score >= 95.0)
            return { start, count, false };
        if (score.score > best.score)
            best = score;
    }

    if (best.score >= 70.0)
        return { best.barStart, best.barCount, !best.clearlyFull };
    return fallback;
}

int totalBarsFor(const ParsedMidi& parsed, int barTicks) noexcept
{
    if (barTicks <= 0)
        return 1;
    return std::max(1, (parsed.lastTick + barTicks - 1) / barTicks);
}
}

MidiStyleImportInfo inspectMidiFileForStyleImport(const juce::File& midi,
                                                  int barStart,
                                                  int barCount)
{
    MidiStyleImportInfo info;
    auto parsed = parseMidi(midi, info.warnings);
    if (!parsed)
        return info;

    const int barTicks = cadenza::ticksPerBar(
        parsed->ppq, std::max(1, parsed->beatsPerBar), std::max(1, parsed->beatUnit));
    if (barTicks <= 0) {
        info.warnings.add("Invalid MIDI time signature; could not compute bar length");
        return info;
    }

    info.ppq = parsed->ppq;
    info.tempo = parsed->tempo;
    info.beatsPerBar = parsed->beatsPerBar;
    info.beatUnit = parsed->beatUnit;
    info.totalBars = totalBarsFor(*parsed, barTicks);
    info.recommendedRange = recommendRange(*parsed, info.totalBars, barTicks, std::max(1, barCount));

    const int clampedStart = std::clamp(barStart, 0, std::max(0, info.totalBars - 1));
    const int clampedCount = std::clamp(std::max(1, barCount), 1, std::max(1, info.totalBars - clampedStart));
    const int startTick = clampedStart * barTicks;
    const int endTick = startTick + clampedCount * barTicks;

    auto detected = detectSourceChord(harmonicGroups(*parsed), startTick, endTick);
    info.detectedChord.root = detected.root;
    info.detectedChord.rootName = rootName(detected.root);
    info.detectedChord.chordSuffix = juce::String(detected.suffix);
    info.detectedChord.fallback = detected.fallback;
    info.detectedChord.confidence = detected.confidence;
    info.detectedChord.confidenceReason = detected.confidenceReason;
    if (detected.fallback)
        info.warnings.add("Could not confidently detect source chord; using C major fallback");

    info.ok = true;
    return info;
}

MidiStyleConvertResult convertMidiFileToNativeStyle(const juce::File& midi,
                                                    const MidiStyleConvertOptions& options)
{
    MidiStyleConvertResult result;
    auto parsed = parseMidi(midi, result.warnings);
    if (!parsed)
        return result;

    const int barTicks = cadenza::ticksPerBar(
        parsed->ppq, std::max(1, parsed->beatsPerBar), std::max(1, parsed->beatUnit));
    if (barTicks <= 0) {
        result.warnings.add("Invalid MIDI time signature; could not compute bar length");
        return result;
    }
    const int totalBars = totalBarsFor(*parsed, barTicks);
    const int barStart = std::clamp(options.barStart, 0, std::max(0, totalBars - 1));
    const int barCount = std::clamp(std::max(1, options.barCount), 1, std::max(1, totalBars - barStart));
    const int startTick = barStart * barTicks;
    const int endTick = startTick + barCount * barTicks;

    std::vector<SourceGroup*> drums;
    std::vector<SourceGroup*> basses;
    std::vector<SourceGroup*> chords;
    std::vector<SourceGroup*> pads;
    std::vector<SourceGroup*> phrases;

    for (auto& group : parsed->groups) {
        switch (classifyGroup(group)) {
            case Slot::Drums: drums.push_back(&group); break;
            case Slot::Bass: basses.push_back(&group); break;
            case Slot::Chord: chords.push_back(&group); break;
            case Slot::Pad: pads.push_back(&group); break;
            case Slot::Phrase: phrases.push_back(&group); break;
            case Slot::Drop: break;
        }
    }

    auto byStrength = [](const SourceGroup* a, const SourceGroup* b) {
        return strength(*a) > strength(*b);
    };
    for (auto* list : { &drums, &basses, &chords, &pads, &phrases })
        std::sort(list->begin(), list->end(), byStrength);

    std::vector<SourceGroup*> harmonic;
    harmonic.insert(harmonic.end(), basses.begin(), basses.end());
    harmonic.insert(harmonic.end(), chords.begin(), chords.end());
    harmonic.insert(harmonic.end(), pads.begin(), pads.end());

    auto detected = detectSourceChord(harmonic, startTick, endTick);
    if (options.overrideSourceRoot)
        detected.root = pc(*options.overrideSourceRoot);
    if (options.overrideSourceChord)
        detected.suffix = options.overrideSourceChord->toStdString();
    detected.fallback = !options.overrideSourceRoot && !options.overrideSourceChord && detected.fallback;
    if (detected.fallback)
        result.warnings.add("Could not confidently detect source chord; using C major fallback");
    const std::string sourceRoot = rootName(detected.root);
    const std::string sourceChord = detected.suffix;
    const int normalizedRoot = options.normalizeToC ? 0 : detected.root;
    const int transposeToNormalizedRoot = options.normalizeToC ? nearestTransposeToC(detected.root) : 0;
    const std::string normalizedSourceRoot = rootName(normalizedRoot);

    auto style = std::make_unique<Style>();
    style->schema = "cadenza.style.v1";
    style->yamahaFormat = YamahaStyleFormat::Unknown;
    style->id = sanitizedId(midi.getFileNameWithoutExtension());
    style->name = midi.getFileNameWithoutExtension().toStdString();
    style->defaultTempo = parsed->tempo;
    style->beatsPerBar = parsed->beatsPerBar;
    style->beatUnit = parsed->beatUnit;
    style->ticksPerBeat = parsed->ppq;

    Section section;
    section.name = options.sectionName.isNotEmpty() ? options.sectionName.toStdString() : "mainA";
    section.barCount = barCount;

    auto makePart = [&](const std::string& name, SourceGroup* seed) {
        Part part;
        const auto& info = slotInfo(name);
        part.name = name;
        part.midiChannel = info.channel;
        part.percussion = info.percussion;
        if (seed != nullptr)
            copyProgramState(part, seed->state);
        if (!part.percussion && !part.program)
            part.program = cadenza::midi::defaultGmProgramForRole(name);
        if (!part.percussion && part.instrument.empty() && part.program)
            part.instrument = cadenza::midi::gmInstrumentName(*part.program);
        part.yamahaPolicy = makePolicy(name, part.percussion ? sourceRoot : normalizedSourceRoot, sourceChord);
        return part;
    };

    if (!drums.empty()) {
        auto part = makePart("drums", drums.front());
        for (auto* group : drums)
            appendGroup(part, *group, startTick, endTick, detected.root, sourceChord, 0);
        sortPart(part);
        if (!part.notes.empty() || !part.automation.empty())
            section.parts.push_back(std::move(part));
    }

    if (!basses.empty()) {
        auto part = makePart("bass", basses.front());
        appendGroup(part, *basses.front(), startTick, endTick, normalizedRoot, sourceChord, transposeToNormalizedRoot);
        if (basses.size() > 1)
            result.warnings.add("Dropped extra bass MIDI tracks beyond the bass slot");
        sortPart(part);
        if (!part.notes.empty() || !part.automation.empty())
            section.parts.push_back(std::move(part));
    }

    const std::pair<const char*, std::vector<SourceGroup*>*> chordSlots[] = {
        { "chord1", &chords },
        { "chord2", &chords },
    };
    for (int i = 0; i < 2 && i < static_cast<int>(chords.size()); ++i) {
        auto part = makePart(chordSlots[i].first, chords[static_cast<std::size_t>(i)]);
        appendGroup(part, *chords[static_cast<std::size_t>(i)], startTick, endTick, normalizedRoot, sourceChord, transposeToNormalizedRoot);
        sortPart(part);
        if (!part.notes.empty() || !part.automation.empty())
            section.parts.push_back(std::move(part));
    }
    if (chords.size() > 2)
        result.warnings.add("Dropped extra chord MIDI tracks beyond chord2");

    if (!pads.empty()) {
        auto part = makePart("pad", pads.front());
        appendGroup(part, *pads.front(), startTick, endTick, normalizedRoot, sourceChord, transposeToNormalizedRoot);
        sortPart(part);
        if (!part.notes.empty() || !part.automation.empty())
            section.parts.push_back(std::move(part));
        if (pads.size() > 1)
            result.warnings.add("Dropped extra pad MIDI tracks beyond the pad slot");
    }

    for (int i = 0; i < 2 && i < static_cast<int>(phrases.size()); ++i) {
        auto part = makePart(i == 0 ? "phrase1" : "phrase2", phrases[static_cast<std::size_t>(i)]);
        appendGroup(part, *phrases[static_cast<std::size_t>(i)], startTick, endTick, normalizedRoot, sourceChord, transposeToNormalizedRoot);
        sortPart(part);
        if (!part.notes.empty() || !part.automation.empty())
            section.parts.push_back(std::move(part));
    }
    if (phrases.size() > 2)
        result.warnings.add("Dropped extra phrase MIDI tracks beyond phrase2");

    if (section.parts.empty()) {
        result.warnings.add("Selected MIDI bar range contains no mappable notes");
        return result;
    }

    style->parseWarnings.reserve(static_cast<std::size_t>(result.warnings.size()));
    for (const auto& warning : result.warnings)
        style->parseWarnings.push_back(warning.toStdString());
    style->sections.push_back(std::move(section));

    auto loaded = loadStyleFromJson(saveStyleToJson(*style));
    if (!loaded.ok) {
        result.warnings.add("Converted style failed JSON validation: " + juce::String(loaded.error));
        return result;
    }

    result.style = std::make_unique<Style>(std::move(loaded.style));
    result.ok = true;
    return result;
}
}
