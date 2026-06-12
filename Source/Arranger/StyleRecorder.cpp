#include "StyleRecorder.h"
#include "StyleLoader.h"
#include "../Midi/GmInstruments.h"
#include "../MusicalTiming.h"

#include <algorithm>
#include <cctype>

namespace cadenza::arranger
{
namespace
{
const RecorderPartInfo kParts[kNumRecorderParts] = {
    { RecorderPart::Drums,   "Drums",    "drums",   10, true  },
    { RecorderPart::Bass,    "Bass",     "bass",    11, false },
    { RecorderPart::Chord1,  "Chord 1",  "chord1",  12, false },
    { RecorderPart::Chord2,  "Chord 2",  "chord2",  13, false },
    { RecorderPart::Pad,     "Pad",      "pad",     14, false },
    { RecorderPart::Phrase1, "Phrase 1", "phrase1", 15, false },
    { RecorderPart::Phrase2, "Phrase 2", "phrase2", 16, false },
};

// Role of a recorded pitch against the C-major source-chord convention (the
// same convention the .sty importer bakes with — see StyParser::assignRole).
NoteRole roleForRecordedPitch(int pitch, bool percussion) noexcept
{
    if (percussion)
        return NoteRole::Absolute;
    switch (((pitch % 12) + 12) % 12) {
        case 0:  return NoteRole::ChordRoot;
        case 4:  return NoteRole::Chord3;
        case 7:  return NoteRole::Chord5;
        case 10:
        case 11: return NoteRole::Chord7;
        default: return NoteRole::ChordColor;
    }
}

// Playback policy for a self-recorded part: the same per-channel defaults the
// importer falls back to when a .sty has no CASM (StyParser::fallbackYamahaPolicy),
// so recorded styles follow chords the way the equivalent Yamaha part would.
YamahaChannelPolicy recorderPolicy(const RecorderPartInfo& info)
{
    YamahaChannelPolicy policy;
    policy.source = YamahaPolicySource::Fallback;
    policy.sourceChannel = info.midiChannel;
    policy.destinationPart = info.partName;
    policy.destinationType = info.partName;
    policy.destinationName = info.partName;
    policy.sourceRoot = std::string("C");
    policy.sourceChord = std::string("Maj7");

    switch (info.part) {
        case RecorderPart::Drums:
            policy.ntr = YamahaNtr::RootFixed;
            policy.ntt = YamahaNtt::Bypass;
            break;
        case RecorderPart::Bass:
            policy.ntr = YamahaNtr::RootTransposition;
            policy.ntt = YamahaNtt::Bypass;   // root-shift the line, keep its shape
            policy.bassOn = true;             // slash chords drive it to the named bass
            break;
        case RecorderPart::Chord1:
        case RecorderPart::Chord2:
        case RecorderPart::Pad:
            policy.ntr = YamahaNtr::RootTransposition;
            policy.ntt = YamahaNtt::Chord;    // chord tones follow the chord quality
            break;
        case RecorderPart::Phrase1:
        case RecorderPart::Phrase2:
            policy.ntr = YamahaNtr::RootTransposition;
            policy.ntt = YamahaNtt::Melody;   // melodic riff: root-shift the melody
            break;
    }
    policy.retriggerRule = YamahaRetriggerRule::PitchShift;
    return policy;
}

std::string sanitizedId(const std::string& name)
{
    std::string id;
    for (char c : name) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc)) id += static_cast<char>(std::tolower(uc));
        else if (c == ' ' || c == '-' || c == '_') id += '-';
    }
    if (id.empty()) id = "my-style";
    return id;
}
}

const RecorderPartInfo& recorderPartInfo(RecorderPart part) noexcept
{
    for (const auto& info : kParts)
        if (info.part == part)
            return info;
    return kParts[0];
}

const RecorderPartInfo& recorderPartInfo(int index) noexcept
{
    if (index < 0 || index >= kNumRecorderParts)
        return kParts[0];
    return kParts[index];
}

void StyleRecorder::startSession(const RecorderConfig& config)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_config = config;
    m_config.bars = std::max(1, m_config.bars);
    m_config.beatsPerBar = std::max(1, m_config.beatsPerBar);
    m_config.beatUnit = std::max(1, m_config.beatUnit);
    m_config.ticksPerBeat = std::max(24, m_config.ticksPerBeat);

    m_style = Style{};
    m_style.id = sanitizedId(m_config.name);
    m_style.name = m_config.name;
    m_style.defaultTempo = m_config.tempo;
    m_style.beatsPerBar = m_config.beatsPerBar;
    m_style.beatUnit = m_config.beatUnit;
    m_style.ticksPerBeat = m_config.ticksPerBeat;

    Section section;
    section.name = m_config.section;
    section.barCount = m_config.bars;
    m_style.sections.push_back(std::move(section));

    m_take.clear();
    std::fill(std::begin(m_openActive), std::end(m_openActive), false);
    m_active = true;
}

void StyleRecorder::endSession()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_active = false;
    m_take.clear();
    std::fill(std::begin(m_openActive), std::end(m_openActive), false);
}

bool StyleRecorder::sessionActive() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_active;
}

RecorderConfig StyleRecorder::config() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_config;
}

std::shared_ptr<const Style> StyleRecorder::snapshotStyle() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return std::make_shared<const Style>(m_style);
}

void StyleRecorder::setTargetPart(RecorderPart part)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_target != part) {
        m_target = part;
        m_take.clear();
        std::fill(std::begin(m_openActive), std::end(m_openActive), false);
    }
}

RecorderPart StyleRecorder::targetPart() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_target;
}

void StyleRecorder::setStyleName(const std::string& name)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (name.empty())
        return;
    m_config.name = name;
    m_style.name = name;
    m_style.id = sanitizedId(name);
}

void StyleRecorder::setQuantizeDivision(int division)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_quantizeDivision = std::max(0, division);
}

int StyleRecorder::quantizeDivision() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_quantizeDivision;
}

int StyleRecorder::sectionLengthTicks() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_config.bars
        * cadenza::ticksPerBar(m_config.ticksPerBeat, m_config.beatsPerBar, m_config.beatUnit);
}

void StyleRecorder::noteOn(int pitch, int velocity, int absoluteTick)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_active || pitch < 0 || pitch > 127)
        return;
    const int len = m_config.bars
        * cadenza::ticksPerBar(m_config.ticksPerBeat, m_config.beatsPerBar, m_config.beatUnit);
    if (len <= 0)
        return;

    m_open[pitch].startTick = ((absoluteTick % len) + len) % len;
    m_open[pitch].velocity = std::clamp(velocity, 1, 127);
    m_openActive[pitch] = true;
}

void StyleRecorder::noteOff(int pitch, int absoluteTick)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_active || pitch < 0 || pitch > 127 || !m_openActive[pitch])
        return;
    const int len = m_config.bars
        * cadenza::ticksPerBar(m_config.ticksPerBeat, m_config.beatsPerBar, m_config.beatUnit);
    if (len <= 0)
        return;

    const int end = ((absoluteTick % len) + len) % len;
    int duration = end - m_open[pitch].startTick;
    if (duration <= 0)
        duration += len;   // released after the loop wrapped
    duration = std::max(1, std::min(duration, len));

    TakeNote note;
    note.pitch = pitch;
    note.velocity = m_open[pitch].velocity;
    note.startTick = m_open[pitch].startTick;
    note.durationTicks = duration;
    m_take.push_back(note);
    m_openActive[pitch] = false;
}

bool StyleRecorder::hasPendingTake() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return !m_take.empty();
}

Part& StyleRecorder::findOrCreateTargetPart()
{
    const auto& info = recorderPartInfo(m_target);
    auto& section = m_style.sections.front();
    for (auto& part : section.parts)
        if (part.midiChannel == info.midiChannel)
            return part;

    Part part;
    part.name = info.partName;
    part.midiChannel = info.midiChannel;
    part.percussion = info.percussion;
    if (!info.percussion)
        part.program = cadenza::midi::defaultGmProgramForRole(info.partName);
    part.yamahaPolicy = recorderPolicy(info);
    section.parts.push_back(std::move(part));
    return section.parts.back();
}

bool StyleRecorder::commitTake()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_active || m_take.empty())
        return false;

    const int len = m_config.bars
        * cadenza::ticksPerBar(m_config.ticksPerBeat, m_config.beatsPerBar, m_config.beatUnit);
    const int grid = m_quantizeDivision > 0
        ? std::max(1, (m_config.ticksPerBeat * 4) / m_quantizeDivision)
        : 0;

    const auto& info = recorderPartInfo(m_target);
    auto& part = findOrCreateTargetPart();

    for (const auto& take : m_take) {
        PatternNote n;
        n.tick = take.startTick;
        if (grid > 0)
            n.tick = (((take.startTick + grid / 2) / grid) * grid) % len;
        n.duration = take.durationTicks;
        n.pitch = take.pitch;
        n.velocity = take.velocity;
        n.role = roleForRecordedPitch(take.pitch, info.percussion);
        part.notes.push_back(n);
    }
    m_take.clear();

    std::sort(part.notes.begin(), part.notes.end(),
              [](const PatternNote& a, const PatternNote& b) { return a.tick < b.tick; });
    return true;
}

void StyleRecorder::discardTake()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_take.clear();
    std::fill(std::begin(m_openActive), std::end(m_openActive), false);
}

bool StyleRecorder::clearTargetPart()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_active)
        return false;
    const auto& info = recorderPartInfo(m_target);
    auto& parts = m_style.sections.front().parts;
    const auto before = parts.size();
    parts.erase(std::remove_if(parts.begin(), parts.end(),
                               [&](const Part& p) { return p.midiChannel == info.midiChannel; }),
                parts.end());
    m_take.clear();
    return parts.size() != before;
}

bool StyleRecorder::targetPartHasNotes() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_active || m_style.sections.empty())
        return false;
    const auto& info = recorderPartInfo(m_target);
    for (const auto& part : m_style.sections.front().parts)
        if (part.midiChannel == info.midiChannel)
            return !part.notes.empty();
    return false;
}

bool StyleRecorder::save(const std::string& path) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return saveStyleToFile(m_style, path);
}
}
