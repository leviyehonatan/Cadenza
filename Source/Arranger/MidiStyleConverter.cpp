#include "MidiStyleConverter.h"

#include "StyleLoader.h"
#include "../Midi/ChordTypes.h"
#include "../Midi/GmInstruments.h"
#include "../MusicalTiming.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
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

struct PerBarChord
{
    int barStart = 0;
    DetectedChord chord;
};

struct ChordStability
{
    std::vector<PerBarChord> bars;
    int distinctChordCount = 0;
    bool changesChord = false;
};

bool isClearChord(const DetectedChord& chord) noexcept
{
    return !chord.fallback && chord.confidence != MidiStyleChordConfidence::Low;
}

bool sameClearChord(const DetectedChord& a, const DetectedChord& b) noexcept
{
    return pc(a.root - b.root) == 0 && a.suffix == b.suffix;
}

std::vector<SourceGroup*> harmonicGroups(ParsedMidi& parsed);

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

ChordStability detectChordStability(ParsedMidi& parsed,
                                    int barStart,
                                    int barCount,
                                    int barTicks)
{
    ChordStability result;
    if (barTicks <= 0 || barCount <= 0)
        return result;

    auto harmonic = harmonicGroups(parsed);
    std::vector<DetectedChord> distinct;
    for (int b = 0; b < barCount; ++b) {
        const int currentBar = barStart + b;
        const int startTick = currentBar * barTicks;
        const int endTick = startTick + barTicks;
        auto detected = detectSourceChord(harmonic, startTick, endTick);
        result.bars.push_back({ currentBar, detected });

        if (!isClearChord(detected))
            continue;

        const auto duplicate = std::find_if(distinct.begin(), distinct.end(), [&](const auto& existing) {
            return sameClearChord(existing, detected);
        });
        if (duplicate == distinct.end())
            distinct.push_back(std::move(detected));
    }

    result.distinctChordCount = static_cast<int>(distinct.size());
    result.changesChord = result.distinctChordCount > 1;
    return result;
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

struct BlockFingerprint
{
    int barStart = 0;
    int barCount = 4;
    std::uint8_t activeMask = 0;
    std::array<int, 5> noteCounts {};
    std::array<double, 5> densities {};
    std::array<double, 12> pitchClasses {};
    DetectedChord chord;
    RangeScore score;
    bool hasAnyNotes = false;
    bool fullBand = false;
};

int activeCount(std::uint8_t mask) noexcept
{
    int count = 0;
    while (mask != 0) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

juce::String qualityLabelForSuffix(const std::string& suffix)
{
    if (suffix == "m") return "min";
    if (suffix.empty()) return "maj";
    return juce::String(suffix);
}

BlockFingerprint fingerprintBlock(ParsedMidi& parsed, int barStart, int barCount, int barTicks)
{
    BlockFingerprint fp;
    fp.barStart = barStart;
    fp.barCount = barCount;
    fp.score = scoreRange(parsed, barStart, barCount, barTicks);

    const int startTick = barStart * barTicks;
    const int endTick = startTick + barCount * barTicks;
    std::vector<SourceGroup*> harmonic;
    std::vector<SourceGroup*> harmonyParts;
    int harmonyBars = 0;

    for (auto& group : parsed.groups) {
        const auto slot = classifyGroup(group);
        int slotIndex = -1;
        switch (slot) {
            case Slot::Drums: slotIndex = 0; break;
            case Slot::Bass: slotIndex = 1; harmonic.push_back(&group); break;
            case Slot::Chord: slotIndex = 2; harmonic.push_back(&group); harmonyParts.push_back(&group); break;
            case Slot::Pad: slotIndex = 3; harmonic.push_back(&group); harmonyParts.push_back(&group); break;
            case Slot::Phrase: slotIndex = 4; break;
            case Slot::Drop: break;
        }
        if (slotIndex < 0)
            continue;

        int count = 0;
        for (const auto& note : group.notes) {
            if (!noteOverlaps(note, startTick, endTick))
                continue;
            ++count;
            fp.hasAnyNotes = true;
            if (slot != Slot::Drums) {
                const int clippedStart = std::max(note.start, startTick);
                const int clippedEnd = std::min(note.end, endTick);
                fp.pitchClasses[static_cast<std::size_t>(pc(note.pitch))] +=
                    static_cast<double>(std::max(1, clippedEnd - clippedStart));
            }
        }
        fp.noteCounts[static_cast<std::size_t>(slotIndex)] += count;
    }

    for (int i = 0; i < 5; ++i) {
        if (fp.noteCounts[static_cast<std::size_t>(i)] > 0)
            fp.activeMask = static_cast<std::uint8_t>(fp.activeMask | (1u << i));
        fp.densities[static_cast<std::size_t>(i)] =
            static_cast<double>(fp.noteCounts[static_cast<std::size_t>(i)])
            / static_cast<double>(std::max(1, barCount));
    }

    const double pcSum = std::accumulate(fp.pitchClasses.begin(), fp.pitchClasses.end(), 0.0);
    if (pcSum > 0.0)
        for (auto& value : fp.pitchClasses)
            value /= pcSum;

    harmonyBars = countBarsWithNotes(harmonyParts, startTick, barTicks, barCount);
    const bool hasDrums = (fp.activeMask & (1u << 0)) != 0;
    const bool hasBass = (fp.activeMask & (1u << 1)) != 0;
    const bool hasHarmony = harmonyBars >= std::max(1, barCount);
    fp.fullBand = hasDrums && hasBass && hasHarmony && fp.score.clearlyFull;
    fp.chord = detectSourceChord(harmonic, startTick, endTick);
    return fp;
}

double normalizedDensityDistance(double a, double b)
{
    const double denom = std::max({ 1.0, std::abs(a), std::abs(b) });
    return std::abs(a - b) / denom;
}

double fingerprintDistance(const BlockFingerprint& a, const BlockFingerprint& b)
{
    const auto activeDelta = static_cast<unsigned int>(a.activeMask ^ b.activeMask);
    double activeDistance = 0.0;
    for (int i = 0; i < 5; ++i)
        if ((activeDelta & (1u << i)) != 0)
            activeDistance += (i == 0 || i == 1 || i == 2 || i == 3) ? 0.28 : 0.16;

    double densityDistance = 0.0;
    for (int i = 0; i < 5; ++i)
        densityDistance += normalizedDensityDistance(a.densities[static_cast<std::size_t>(i)],
                                                     b.densities[static_cast<std::size_t>(i)]);
    densityDistance /= 5.0;

    double pitchDistance = 0.0;
    for (int i = 0; i < 12; ++i)
        pitchDistance += std::abs(a.pitchClasses[static_cast<std::size_t>(i)]
                                  - b.pitchClasses[static_cast<std::size_t>(i)]);
    pitchDistance *= 0.5;

    double chordDistance = 0.0;
    if (!a.chord.fallback && !b.chord.fallback) {
        if (pc(a.chord.root - b.chord.root) != 0)
            chordDistance += 0.35;
        if (a.chord.suffix != b.chord.suffix)
            chordDistance += 0.20;
    } else if (a.chord.fallback != b.chord.fallback) {
        chordDistance += 0.15;
    }

    return activeDistance * 2.2 + densityDistance * 1.4 + pitchDistance + chordDistance;
}

struct BlockGroup
{
    std::vector<int> memberIndexes;
    double averageScore = 0.0;
    int firstBar = 0;
};

std::vector<BlockGroup> groupSimilarBlocks(const std::vector<BlockFingerprint>& blocks)
{
    std::vector<BlockGroup> groups;
    constexpr double similarThreshold = 0.58;
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        int bestGroup = -1;
        double bestDistance = std::numeric_limits<double>::infinity();
        for (int g = 0; g < static_cast<int>(groups.size()); ++g) {
            const auto& rep = blocks[static_cast<std::size_t>(groups[static_cast<std::size_t>(g)].memberIndexes.front())];
            const double distance = fingerprintDistance(blocks[static_cast<std::size_t>(i)], rep);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestGroup = g;
            }
        }
        if (bestGroup >= 0 && bestDistance <= similarThreshold) {
            groups[static_cast<std::size_t>(bestGroup)].memberIndexes.push_back(i);
        } else {
            BlockGroup group;
            group.memberIndexes.push_back(i);
            group.firstBar = blocks[static_cast<std::size_t>(i)].barStart;
            groups.push_back(std::move(group));
        }
    }

    for (auto& group : groups) {
        double total = 0.0;
        for (const int index : group.memberIndexes)
            total += blocks[static_cast<std::size_t>(index)].score.score;
        group.averageScore = group.memberIndexes.empty()
            ? 0.0
            : total / static_cast<double>(group.memberIndexes.size());
        group.firstBar = blocks[static_cast<std::size_t>(group.memberIndexes.front())].barStart;
    }
    return groups;
}

bool groupIsRepeatedFullBand(const BlockGroup& group,
                             const std::vector<BlockFingerprint>& blocks,
                             int preferredBlockBars)
{
    if (group.memberIndexes.size() < 2)
        return false;
    int full = 0;
    for (const int index : group.memberIndexes) {
        const auto& block = blocks[static_cast<std::size_t>(index)];
        if (block.fullBand && block.barCount > 0 && block.barCount <= preferredBlockBars)
            ++full;
    }
    return full >= 2;
}

int chordStableRunLength(ParsedMidi& parsed,
                         int barStart,
                         int maxBars,
                         int totalBars,
                         int barTicks)
{
    if (barTicks <= 0 || maxBars <= 1 || barStart >= totalBars)
        return 1;

    auto harmonic = harmonicGroups(parsed);
    DetectedChord reference;
    bool hasReference = false;
    int count = 0;
    for (; count < maxBars && barStart + count < totalBars; ++count) {
        const int currentBar = barStart + count;
        const auto detected = detectSourceChord(harmonic, currentBar * barTicks, (currentBar + 1) * barTicks);
        if (isClearChord(detected)) {
            if (hasReference && !sameClearChord(reference, detected))
                break;
            if (!hasReference) {
                reference = detected;
                hasReference = true;
            }
        }
    }

    return std::max(1, count);
}

MidiStyleSectionSpec specForBlock(const juce::String& sectionId, const BlockFingerprint& block)
{
    MidiStyleSectionSpec spec;
    spec.sectionId = sectionId;
    spec.barStart = block.barStart;
    spec.barCount = block.barCount;
    if (block.chord.confidence != MidiStyleChordConfidence::Low && !block.chord.fallback) {
        spec.overrideRoot = rootName(block.chord.root);
        spec.overrideQuality = qualityLabelForSuffix(block.chord.suffix);
    }
    return spec;
}

juce::String sectionLabel(const juce::String& id)
{
    if (id == "intro") return "Intro";
    if (id == "mainB") return "Main B";
    if (id == "mainC") return "Main C";
    if (id == "mainD") return "Main D";
    if (id == "ending") return "Ending";
    return "Main A";
}

void addFoundAtWarning(juce::StringArray& warnings, const MidiStyleSectionSpec& spec)
{
    const int start = spec.barStart + 1;
    const int end = spec.barStart + std::max(1, spec.barCount);
    warnings.add(sectionLabel(spec.sectionId) + ": found at bars "
                 + juce::String(start) + "-" + juce::String(end));
}

bool sameRange(const BlockFingerprint& block, const MidiStyleSectionSpec& spec) noexcept
{
    return block.barStart == spec.barStart && block.barCount == spec.barCount;
}

int totalBarsFor(const ParsedMidi& parsed, int barTicks) noexcept
{
    if (barTicks <= 0)
        return 1;
    return std::max(1, (parsed.lastTick + barTicks - 1) / barTicks);
}

int sectionOrder(const juce::String& id) noexcept
{
    const auto s = id.trim().toStdString();
    if (s == "intro") return 0;
    if (s == "mainA") return 1;
    if (s == "mainB") return 2;
    if (s == "mainC") return 3;
    if (s == "mainD") return 4;
    if (s == "ending") return 5;
    return 100;
}

std::optional<int> parseRootName(const juce::String& root)
{
    const auto text = root.trim().toLowerCase();
    if (text.isEmpty())
        return std::nullopt;
    if (text == "c") return 0;
    if (text == "c#" || text == "db") return 1;
    if (text == "d") return 2;
    if (text == "d#" || text == "eb") return 3;
    if (text == "e") return 4;
    if (text == "f") return 5;
    if (text == "f#" || text == "gb") return 6;
    if (text == "g") return 7;
    if (text == "g#" || text == "ab") return 8;
    if (text == "a") return 9;
    if (text == "a#" || text == "bb") return 10;
    if (text == "b") return 11;
    return std::nullopt;
}

std::optional<juce::String> qualityOverrideSuffix(const juce::String& quality)
{
    const auto q = quality.trim().toLowerCase();
    if (q.isEmpty())
        return std::nullopt;
    if (q == "maj" || q == "major")
        return juce::String();
    if (q == "min" || q == "minor")
        return "m";
    return quality.trim();
}

struct BuiltSection
{
    Section section;
    juce::StringArray warnings;
    bool ok = false;
};

BuiltSection buildSectionFromRange(ParsedMidi& parsed,
                                   int barTicks,
                                   int totalBars,
                                   const MidiStyleSectionSpec& spec,
                                   bool normalizeToC)
{
    BuiltSection built;
    const int barStart = std::clamp(spec.barStart, 0, std::max(0, totalBars - 1));
    const int barCount = std::clamp(std::max(1, spec.barCount), 1, std::max(1, totalBars - barStart));
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
    const auto overrideRoot = parseRootName(spec.overrideRoot);
    const auto overrideQuality = qualityOverrideSuffix(spec.overrideQuality);
    if (overrideRoot)
        detected.root = pc(*overrideRoot);
    if (overrideQuality)
        detected.suffix = overrideQuality->toStdString();
    const bool hasValidOverride = overrideRoot || overrideQuality;
    if (!hasValidOverride && detected.confidence == MidiStyleChordConfidence::Low) {
        detected.root = 0;
        detected.suffix.clear();
        detected.fallback = true;
    } else {
        detected.fallback = !hasValidOverride && detected.fallback;
    }
    if (detected.fallback)
        built.warnings.add("Could not confidently detect source chord for "
                           + (spec.sectionId.isNotEmpty() ? spec.sectionId : "mainA")
                           + "; using C major fallback");
    if (spec.overrideRoot.isNotEmpty() && !overrideRoot)
        built.warnings.add("Ignored unknown root override for "
                           + (spec.sectionId.isNotEmpty() ? spec.sectionId : "mainA")
                           + ": " + spec.overrideRoot);

    const std::string sourceRoot = rootName(detected.root);
    const std::string sourceChord = detected.suffix;
    const int normalizedRoot = normalizeToC ? 0 : detected.root;
    const int transposeToNormalizedRoot = normalizeToC ? nearestTransposeToC(detected.root) : 0;
    const std::string normalizedSourceRoot = rootName(normalizedRoot);

    Section section;
    section.name = spec.sectionId.isNotEmpty() ? spec.sectionId.toStdString() : "mainA";
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
        part.yamahaPolicy = makePolicy(name, normalizedSourceRoot, sourceChord);
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
            built.warnings.add("Dropped extra bass MIDI tracks beyond the bass slot in " + juce::String(section.name));
        sortPart(part);
        if (!part.notes.empty() || !part.automation.empty())
            section.parts.push_back(std::move(part));
    }

    for (int i = 0; i < 2 && i < static_cast<int>(chords.size()); ++i) {
        auto part = makePart(i == 0 ? "chord1" : "chord2", chords[static_cast<std::size_t>(i)]);
        appendGroup(part, *chords[static_cast<std::size_t>(i)], startTick, endTick, normalizedRoot, sourceChord, transposeToNormalizedRoot);
        sortPart(part);
        if (!part.notes.empty() || !part.automation.empty())
            section.parts.push_back(std::move(part));
    }
    if (chords.size() > 2)
        built.warnings.add("Dropped extra chord MIDI tracks beyond chord2 in " + juce::String(section.name));

    if (!pads.empty()) {
        auto part = makePart("pad", pads.front());
        appendGroup(part, *pads.front(), startTick, endTick, normalizedRoot, sourceChord, transposeToNormalizedRoot);
        sortPart(part);
        if (!part.notes.empty() || !part.automation.empty())
            section.parts.push_back(std::move(part));
        if (pads.size() > 1)
            built.warnings.add("Dropped extra pad MIDI tracks beyond the pad slot in " + juce::String(section.name));
    }

    for (int i = 0; i < 2 && i < static_cast<int>(phrases.size()); ++i) {
        auto part = makePart(i == 0 ? "phrase1" : "phrase2", phrases[static_cast<std::size_t>(i)]);
        appendGroup(part, *phrases[static_cast<std::size_t>(i)], startTick, endTick, normalizedRoot, sourceChord, transposeToNormalizedRoot);
        sortPart(part);
        if (!part.notes.empty() || !part.automation.empty())
            section.parts.push_back(std::move(part));
    }
    if (phrases.size() > 2)
        built.warnings.add("Dropped extra phrase MIDI tracks beyond phrase2 in " + juce::String(section.name));

    if (section.parts.empty()) {
        built.warnings.add("Selected MIDI bar range for " + juce::String(section.name) + " contains no mappable notes");
        return built;
    }

    built.section = std::move(section);
    built.ok = true;
    return built;
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

    const auto stability = detectChordStability(*parsed, clampedStart, clampedCount, barTicks);
    info.rangeChangesChord = stability.changesChord;
    info.distinctChordCount = stability.distinctChordCount;
    for (const auto& bar : stability.bars) {
        if (isClearChord(bar.chord)) {
            info.perBarChords.add("Bar " + juce::String(bar.barStart + 1) + ": "
                                  + juce::String(rootName(bar.chord.root)) + " "
                                  + qualityLabelForSuffix(bar.chord.suffix));
        } else {
            info.perBarChords.add("Bar " + juce::String(bar.barStart + 1) + ": unclear");
        }
    }

    info.ok = true;
    return info;
}

MidiStyleAutoSplitResult autoSplitMidiFileForStyleImport(const juce::File& midi,
                                                         const MidiStyleAutoSplitOptions& options)
{
    MidiStyleAutoSplitResult result;
    (void) options.normalizeToC;
    auto parsed = parseMidi(midi, result.warnings);
    if (!parsed) {
        if (result.warnings.isEmpty())
            result.warnings.add("Could not parse MIDI file");
        return result;
    }

    const int barTicks = cadenza::ticksPerBar(
        parsed->ppq, std::max(1, parsed->beatsPerBar), std::max(1, parsed->beatUnit));
    if (barTicks <= 0) {
        result.warnings.add("Invalid MIDI time signature; could not compute bar length");
        return result;
    }

    const int totalBars = totalBarsFor(*parsed, barTicks);
    int blockBars = std::clamp(std::max(1, options.blockBars), 1, std::max(1, totalBars));
    if (totalBars < blockBars * 2 && totalBars >= 2)
        blockBars = 2;
    blockBars = std::min(blockBars, 4);

    std::vector<BlockFingerprint> blocks;
    for (int start = 0; start < totalBars;) {
        const int count = chordStableRunLength(*parsed, start, std::min(blockBars, totalBars - start),
                                               totalBars, barTicks);
        if (count <= 0)
            break;
        blocks.push_back(fingerprintBlock(*parsed, start, count, barTicks));
        start += count;
    }

    if (blocks.empty()) {
        result.warnings.add("MIDI file has no usable bar ranges for auto-split");
        return result;
    }

    auto groups = groupSimilarBlocks(blocks);
    std::vector<int> mainGroupIndexes;
    for (int i = 0; i < static_cast<int>(groups.size()); ++i)
        if (groupIsRepeatedFullBand(groups[static_cast<std::size_t>(i)], blocks, blockBars))
            mainGroupIndexes.push_back(i);

    std::stable_sort(mainGroupIndexes.begin(), mainGroupIndexes.end(), [&](int a, int b) {
        const auto& ga = groups[static_cast<std::size_t>(a)];
        const auto& gb = groups[static_cast<std::size_t>(b)];
        if (ga.memberIndexes.size() != gb.memberIndexes.size())
            return ga.memberIndexes.size() > gb.memberIndexes.size();
        if (std::abs(ga.averageScore - gb.averageScore) > 0.001)
            return ga.averageScore > gb.averageScore;
        return ga.firstBar < gb.firstBar;
    });

    if (mainGroupIndexes.empty()) {
        result.warnings.add("Auto-split could not find a repeated full-band Main section");
        result.ok = false;
        return result;
    } else {
        std::vector<int> selectedGroupIndexes;
        const char* mainIds[] = { "mainA", "mainB", "mainC", "mainD" };
        for (const int groupIndex : mainGroupIndexes) {
            const auto& group = groups[static_cast<std::size_t>(groupIndex)];
            const auto& representative = blocks[static_cast<std::size_t>(group.memberIndexes.front())];
            bool distinct = true;
            for (const int selected : selectedGroupIndexes) {
                const auto& selectedGroup = groups[static_cast<std::size_t>(selected)];
                const auto& selectedRep = blocks[static_cast<std::size_t>(selectedGroup.memberIndexes.front())];
                if (fingerprintDistance(representative, selectedRep) < 0.72) {
                    distinct = false;
                    break;
                }
            }
            if (!distinct)
                continue;

            selectedGroupIndexes.push_back(groupIndex);
            auto spec = specForBlock(mainIds[result.sections.size()], representative);
            addFoundAtWarning(result.warnings, spec);
            result.sections.push_back(std::move(spec));
            if (result.sections.size() >= 4)
                break;
        }
    }

    if (result.sections.empty()) {
        result.warnings.add("Auto-split did not select any mappable style sections");
        return result;
    }

    int firstMainStart = totalBars;
    double firstMainScore = 0.0;
    std::vector<BlockFingerprint> mainBlocks;
    for (const auto& spec : result.sections) {
        if (!spec.sectionId.startsWith("main"))
            continue;
        firstMainStart = std::min(firstMainStart, spec.barStart);
        auto it = std::find_if(blocks.begin(), blocks.end(), [&](const auto& block) {
            return sameRange(block, spec);
        });
        if (it != blocks.end()) {
            firstMainScore = std::max(firstMainScore, it->score.score);
            mainBlocks.push_back(*it);
        }
    }

    auto introIt = std::find_if(blocks.begin(), blocks.end(), [&](const auto& block) {
        return block.barStart < firstMainStart
            && block.hasAnyNotes
            && !block.fullBand
            && (activeCount(block.activeMask) < 3 || block.score.score <= firstMainScore - 20.0);
    });
    if (introIt != blocks.end()) {
        auto spec = specForBlock("intro", *introIt);
        addFoundAtWarning(result.warnings, spec);
        result.sections.push_back(std::move(spec));
    }

    const auto& finalBlock = blocks.back();
    const bool alreadySelected = std::any_of(result.sections.begin(), result.sections.end(), [&](const auto& spec) {
        return sameRange(finalBlock, spec);
    });
    if (!alreadySelected && finalBlock.hasAnyNotes) {
        double nearestMainDistance = std::numeric_limits<double>::infinity();
        for (const auto& mainBlock : mainBlocks)
            nearestMainDistance = std::min(nearestMainDistance, fingerprintDistance(finalBlock, mainBlock));
        const bool sparseOrCadential = !finalBlock.fullBand && activeCount(finalBlock.activeMask) <= 2;
        const bool differsFromMains = nearestMainDistance > 0.78 && finalBlock.score.score <= firstMainScore - 12.0;
        if (sparseOrCadential || differsFromMains) {
            auto spec = specForBlock("ending", finalBlock);
            addFoundAtWarning(result.warnings, spec);
            result.sections.push_back(std::move(spec));
        }
    }

    std::stable_sort(result.sections.begin(), result.sections.end(), [](const auto& a, const auto& b) {
        const int ao = sectionOrder(a.sectionId);
        const int bo = sectionOrder(b.sectionId);
        return ao == bo ? a.sectionId.compare(b.sectionId) < 0 : ao < bo;
    });

    result.ok = !result.sections.empty();
    return result;
}

MidiStyleConvertResult convertMidiFileToNativeStyle(const juce::File& midi,
                                                    const MidiStyleConvertOptions& options)
{
    MidiStyleSectionSpec spec;
    spec.sectionId = options.sectionName.isNotEmpty() ? options.sectionName : "mainA";
    spec.barStart = options.barStart;
    spec.barCount = options.barCount;
    if (options.overrideSourceRoot)
        spec.overrideRoot = rootName(*options.overrideSourceRoot);
    if (options.overrideSourceChord)
        spec.overrideQuality = options.overrideSourceChord->isEmpty()
            ? "maj"
            : (*options.overrideSourceChord == "m" ? "min" : *options.overrideSourceChord);
    return convertMidiFileToNativeStyleMultiSection(midi, { spec }, options.normalizeToC);
}

MidiStyleConvertResult convertMidiFileToNativeStyleMultiSection(const juce::File& midi,
                                                                const std::vector<MidiStyleSectionSpec>& sections,
                                                                bool normalizeToC)
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

    auto style = std::make_unique<Style>();
    style->schema = "cadenza.style.v1";
    style->yamahaFormat = YamahaStyleFormat::Unknown;
    style->id = sanitizedId(midi.getFileNameWithoutExtension());
    style->name = midi.getFileNameWithoutExtension().toStdString();
    style->defaultTempo = parsed->tempo;
    style->beatsPerBar = parsed->beatsPerBar;
    style->beatUnit = parsed->beatUnit;
    style->ticksPerBeat = parsed->ppq;

    auto requested = sections;
    if (requested.empty())
        requested.push_back({ "mainA", 0, 4, {}, {} });
    std::stable_sort(requested.begin(), requested.end(), [](const auto& a, const auto& b) {
        const int ao = sectionOrder(a.sectionId);
        const int bo = sectionOrder(b.sectionId);
        return ao == bo ? a.sectionId.compare(b.sectionId) < 0 : ao < bo;
    });

    for (const auto& spec : requested) {
        auto built = buildSectionFromRange(*parsed, barTicks, totalBars, spec, normalizeToC);
        result.warnings.addArray(built.warnings);
        if (!built.ok)
            continue;
        style->sections.push_back(std::move(built.section));
    }

    if (style->sections.empty()) {
        if (result.warnings.isEmpty())
            result.warnings.add("Selected MIDI bar ranges contain no mappable notes");
        return result;
    }

    style->parseWarnings.reserve(static_cast<std::size_t>(result.warnings.size()));
    for (const auto& warning : result.warnings)
        style->parseWarnings.push_back(warning.toStdString());

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
