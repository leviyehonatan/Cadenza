#include "PlaybackDiagnostics.h"

#include "RuntimePlayback.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

namespace cadenza::arranger
{
namespace
{
struct DiagnosticEvent
{
    int tick = 0;
    int channel = 1;
    int sourceNote = 60;
    int playbackNote = 60;
    int velocity = 100;
    int length = 1;
    std::string partName;
    std::string role;
    std::string bankProgram;
    std::string ntr;
    std::string ntt;
    bool transposed = false;
    bool remapped = false;
    bool percussion = false;
};

std::string csvEscape(const std::string& value)
{
    bool quote = false;
    for (char c : value)
        quote = quote || c == ',' || c == '"' || c == '\n' || c == '\r';
    if (!quote)
        return value;

    std::string out = "\"";
    for (char c : value) {
        if (c == '"')
            out += "\"\"";
        else
            out += c;
    }
    out += '"';
    return out;
}

std::string optionalInt(const std::optional<int>& value)
{
    return value ? std::to_string(*value) : std::string("-");
}

std::string bankProgram(const Part& part)
{
    return optionalInt(part.bankMsb) + "/" + optionalInt(part.bankLsb) + "/" + optionalInt(part.program);
}

std::string roleName(NoteRole role)
{
    switch (role) {
        case NoteRole::Absolute: return "absolute";
        case NoteRole::ChordRoot: return "chord-root";
        case NoteRole::Chord3: return "chord-3";
        case NoteRole::Chord5: return "chord-5";
        case NoteRole::Chord7: return "chord-7";
        case NoteRole::ChordColor: return "chord-color";
        case NoteRole::ScaleTone: return "scale-tone";
    }
    return "absolute";
}

std::string ntrName(const std::optional<YamahaChannelPolicy>& policy)
{
    if (!policy)
        return "-";
    switch (policy->ntr) {
        case YamahaNtr::RootTransposition: return "RootTransposition";
        case YamahaNtr::RootFixed: return "RootFixed";
        case YamahaNtr::Guitar: return "Guitar";
        case YamahaNtr::Unknown: return "Unknown";
    }
    return "Unknown";
}

std::string nttName(const std::optional<YamahaChannelPolicy>& policy)
{
    if (!policy)
        return "-";
    switch (policy->ntt) {
        case YamahaNtt::Bypass: return "Bypass";
        case YamahaNtt::Melody: return "Melody";
        case YamahaNtt::Chord: return "Chord";
        case YamahaNtt::MelodicMinor: return "MelodicMinor";
        case YamahaNtt::HarmonicMinor: return "HarmonicMinor";
        case YamahaNtt::NaturalMinor: return "NaturalMinor";
        case YamahaNtt::Dorian: return "Dorian";
        case YamahaNtt::AllPurpose: return "AllPurpose";
        case YamahaNtt::Stroke: return "Stroke";
        case YamahaNtt::Arpeggio: return "Arpeggio";
        case YamahaNtt::Unknown: return "Unknown";
    }
    return "Unknown";
}

void pushU8(std::vector<uint8_t>& out, uint8_t value) { out.push_back(value); }
void pushU16(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}
void pushU32(std::vector<uint8_t>& out, uint32_t value)
{
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}
void pushTag(std::vector<uint8_t>& out, const char* tag)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(tag[i]));
}
void pushVlq(std::vector<uint8_t>& out, uint32_t value)
{
    uint8_t bytes[5];
    int count = 0;
    bytes[count++] = static_cast<uint8_t>(value & 0x7F);
    value >>= 7;
    while (value != 0) {
        bytes[count++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
        value >>= 7;
    }
    for (int i = count - 1; i >= 0; --i)
        out.push_back(bytes[i]);
}

struct MidiEvent
{
    int tick = 0;
    int order = 0;
    std::vector<uint8_t> bytes;
};

void addCc(std::vector<MidiEvent>& midi, int tick, int channel, int controller, int value)
{
    midi.push_back({ tick, 0, {
        static_cast<uint8_t>(0xB0 | ((channel - 1) & 0x0F)),
        static_cast<uint8_t>(controller & 0x7F),
        static_cast<uint8_t>(value & 0x7F),
    } });
}

void addProgram(std::vector<MidiEvent>& midi, int tick, int channel, int program)
{
    midi.push_back({ tick, 1, {
        static_cast<uint8_t>(0xC0 | ((channel - 1) & 0x0F)),
        static_cast<uint8_t>(program & 0x7F),
    } });
}

bool writeMidi(const std::filesystem::path& path,
               const Style& style,
               const Section& section,
               const std::vector<DiagnosticEvent>& events)
{
    std::vector<MidiEvent> midi;
    for (const auto& part : section.parts) {
        const auto setup = playbackSetupForPart(part);
        if (setup.bankMsb) addCc(midi, 0, setup.cadenzaChannel, 0, *setup.bankMsb);
        if (setup.bankLsb) addCc(midi, 0, setup.cadenzaChannel, 32, *setup.bankLsb);
        if (setup.program) addProgram(midi, 0, setup.cadenzaChannel, *setup.program);
        if (setup.volume) addCc(midi, 0, setup.cadenzaChannel, 7, *setup.volume);
        if (setup.pan) addCc(midi, 0, setup.cadenzaChannel, 10, *setup.pan);
        if (setup.reverb) addCc(midi, 0, setup.cadenzaChannel, 91, *setup.reverb);
        if (setup.chorus) addCc(midi, 0, setup.cadenzaChannel, 93, *setup.chorus);
    }

    for (const auto& ev : events) {
        const auto ch = static_cast<uint8_t>((ev.channel - 1) & 0x0F);
        midi.push_back({ ev.tick, 2, {
            static_cast<uint8_t>(0x90 | ch),
            static_cast<uint8_t>(ev.playbackNote & 0x7F),
            static_cast<uint8_t>(ev.velocity & 0x7F),
        } });
        midi.push_back({ ev.tick + std::max(1, ev.length), 3, {
            static_cast<uint8_t>(0x80 | ch),
            static_cast<uint8_t>(ev.playbackNote & 0x7F),
            64,
        } });
    }

    std::sort(midi.begin(), midi.end(), [](const MidiEvent& a, const MidiEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return a.order < b.order;
    });

    std::vector<uint8_t> track;
    pushVlq(track, 0);
    pushU8(track, 0xFF); pushU8(track, 0x03);
    const std::string name = "Cadenza Diagnostic " + section.name;
    pushVlq(track, static_cast<uint32_t>(name.size()));
    for (char c : name) pushU8(track, static_cast<uint8_t>(c));
    pushVlq(track, 0);
    pushU8(track, 0xFF); pushU8(track, 0x51); pushU8(track, 3);
    const int bpm = std::clamp(style.defaultTempo, 20, 300);
    const int microsPerQuarter = 60000000 / bpm;
    pushU8(track, static_cast<uint8_t>((microsPerQuarter >> 16) & 0xFF));
    pushU8(track, static_cast<uint8_t>((microsPerQuarter >> 8) & 0xFF));
    pushU8(track, static_cast<uint8_t>(microsPerQuarter & 0xFF));
    pushVlq(track, 0);
    pushU8(track, 0xFF); pushU8(track, 0x58); pushU8(track, 4);
    pushU8(track, static_cast<uint8_t>(std::clamp(style.beatsPerBar, 1, 32)));
    int denominatorPower = 0;
    for (int denom = 1; denom < std::max(1, style.beatUnit) && denominatorPower < 7; denom <<= 1)
        ++denominatorPower;
    pushU8(track, static_cast<uint8_t>(std::clamp(denominatorPower, 0, 7)));
    pushU8(track, 24);
    pushU8(track, 8);

    int lastTick = 0;
    for (const auto& event : midi) {
        pushVlq(track, static_cast<uint32_t>(std::max(0, event.tick - lastTick)));
        track.insert(track.end(), event.bytes.begin(), event.bytes.end());
        lastTick = event.tick;
    }
    pushVlq(track, 0);
    pushU8(track, 0xFF); pushU8(track, 0x2F); pushU8(track, 0);

    std::vector<uint8_t> file;
    pushTag(file, "MThd");
    pushU32(file, 6);
    pushU16(file, 0);
    pushU16(file, 1);
    pushU16(file, static_cast<uint16_t>(std::clamp(style.ticksPerBeat, 1, 32767)));
    pushTag(file, "MTrk");
    pushU32(file, static_cast<uint32_t>(track.size()));
    file.insert(file.end(), track.begin(), track.end());

    std::ofstream out(path, std::ios::binary);
    if (!out.good())
        return false;
    out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    return out.good();
}

bool writeCsv(const std::filesystem::path& path, const std::vector<DiagnosticEvent>& events)
{
    std::ofstream out(path);
    if (!out.good())
        return false;

    out << "tick,channel,note,source_note,velocity,note_length,part_name,role,bank_program,ntr,ntt,transposed,remapped\n";
    for (const auto& ev : events) {
        out << ev.tick << ','
            << ev.channel << ','
            << ev.playbackNote << ','
            << ev.sourceNote << ','
            << ev.velocity << ','
            << ev.length << ','
            << csvEscape(ev.partName) << ','
            << ev.role << ','
            << csvEscape(ev.bankProgram) << ','
            << ev.ntr << ','
            << ev.ntt << ','
            << (ev.transposed ? "true" : "false") << ','
            << (ev.remapped ? "true" : "false") << '\n';
    }
    return out.good();
}

std::vector<std::string> suspiciousIssues(const Section& section,
                                          const std::vector<DiagnosticEvent>& events)
{
    std::vector<std::string> issues;
    std::set<int> channels;
    std::set<int> programs;
    std::set<int> velocities;
    int pianoPrograms = 0;
    int missingPrograms = 0;
    int missingCc = 0;
    int drumOutsideGm = 0;
    int notesOutsideExpected = 0;

    for (const auto& part : section.parts) {
        channels.insert(part.midiChannel);
        if (part.program) {
            programs.insert(*part.program);
            if (!part.percussion && *part.program >= 0 && *part.program <= 7)
                ++pianoPrograms;
        } else {
            ++missingPrograms;
        }

        if (!part.volume || !part.pan || !part.reverb || !part.chorus)
            ++missingCc;
    }

    for (const auto& ev : events) {
        velocities.insert(ev.velocity);
        if (ev.percussion && (ev.sourceNote < 35 || ev.sourceNote > 81))
            ++drumOutsideGm;
        if (ev.playbackNote < 0 || ev.playbackNote > 127 || (!ev.percussion && (ev.playbackNote < 12 || ev.playbackNote > 108)))
            ++notesOutsideExpected;
    }

    if (section.parts.size() > 1 && channels.size() == 1)
        issues.push_back("all parts use the same MIDI channel");
    if (section.parts.size() > 1 && programs.size() == 1 && missingPrograms == 0)
        issues.push_back("all parts use the same program");
    if (pianoPrograms >= 3 || (pianoPrograms > 0 && pianoPrograms * 2 >= static_cast<int>(section.parts.size())))
        issues.push_back("too many non-drum parts use piano programs");
    if (drumOutsideGm > 0)
        issues.push_back("drum source notes outside common GM range 35..81: " + std::to_string(drumOutsideGm));
    if (notesOutsideExpected > 0)
        issues.push_back("notes outside expected range: " + std::to_string(notesOutsideExpected));
    if (missingPrograms > 0)
        issues.push_back("parts missing program changes: " + std::to_string(missingPrograms));
    if (missingCc > 0)
        issues.push_back("parts missing at least one of CC7/10/91/93: " + std::to_string(missingCc));
    if (events.size() > 1 && velocities.size() <= 1)
        issues.push_back("no velocity variation");
    if (issues.empty())
        issues.push_back("none");

    return issues;
}

bool writeSummary(const std::filesystem::path& path,
                  const Style& style,
                  const Section& section,
                  const std::vector<DiagnosticEvent>& events,
                  const std::vector<std::string>& issues)
{
    std::ofstream out(path);
    if (!out.good())
        return false;

    std::set<int> channels;
    std::map<std::string, int> instruments;
    std::set<int> drumNotes;
    for (const auto& part : section.parts) {
        channels.insert(part.midiChannel);
        instruments[part.instrument.empty() ? "(unknown)" : part.instrument]++;
    }
    for (const auto& ev : events)
        if (ev.percussion)
            drumNotes.insert(ev.sourceNote);

    out << "# Playback Diagnostic Summary\n\n";
    out << "- style: " << style.name << "\n";
    out << "- section: " << section.name << "\n";
    out << "- exported bars: 4\n";
    out << "- parts: " << section.parts.size() << "\n";
    out << "- note events: " << events.size() << "\n";
    out << "- channels used:";
    for (int ch : channels) out << ' ' << ch;
    out << "\n\n";

    out << "## Instruments\n\n";
    for (const auto& [name, count] : instruments)
        out << "- " << name << ": " << count << "\n";

    out << "\n## Drum Notes\n\n";
    if (drumNotes.empty()) {
        out << "- none\n";
    } else {
        out << "-";
        for (int note : drumNotes) out << ' ' << note;
        out << "\n";
    }

    out << "\n## Suspicious Issues\n\n";
    for (const auto& issue : issues)
        out << "- " << issue << "\n";

    return out.good();
}
}

PlaybackDiagnosticResult exportPlaybackDiagnostics(const Style& style,
                                                   const std::string& sectionName,
                                                   const TransposeContext& context,
                                                   const std::string& outputDirectory,
                                                   int bars)
{
    PlaybackDiagnosticResult result;
    const Section* section = style.findSection(sectionName);
    if (!section && !style.sections.empty())
        section = &style.sections.front();
    if (!section) {
        result.error = "no section available";
        return result;
    }

    const int exportBars = std::max(1, bars);
    const int horizonTicks = exportBars * style.beatsPerBar * style.ticksPerBeat;
    const int sectionTicks = std::max(1, section->barCount * style.beatsPerBar * style.ticksPerBeat);

    std::vector<DiagnosticEvent> events;
    for (const auto& part : section->parts) {
        for (int offset = 0; offset < horizonTicks; offset += sectionTicks) {
            for (const auto& note : part.notes) {
                const int tick = offset + note.tick;
                if (tick < 0 || tick >= horizonTicks)
                    continue;

                const auto playback = playbackNoteForPart(part, note, context);
                if (!playback)
                    continue;

                const bool percussion = part.percussion || part.midiChannel == 10;
                const auto drum = percussion ? drumNoteForPlayback(part, note.pitch) : DrumNoteRemap{};
                DiagnosticEvent ev;
                ev.tick = tick;
                ev.channel = part.midiChannel;
                ev.sourceNote = note.pitch;
                ev.playbackNote = *playback;
                ev.velocity = note.velocity;
                ev.length = std::max(1, note.duration);
                ev.partName = part.name;
                ev.role = roleName(note.role);
                ev.bankProgram = bankProgram(part);
                ev.ntr = ntrName(part.yamahaPolicy);
                ev.ntt = nttName(part.yamahaPolicy);
                ev.transposed = !percussion && ev.playbackNote != ev.sourceNote;
                ev.remapped = percussion && drum.remapped;
                ev.percussion = percussion;
                events.push_back(std::move(ev));
            }
        }
    }

    std::sort(events.begin(), events.end(), [](const DiagnosticEvent& a, const DiagnosticEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.channel != b.channel) return a.channel < b.channel;
        return a.playbackNote < b.playbackNote;
    });

    const std::filesystem::path outDir(outputDirectory);
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        result.error = "failed to create diagnostics directory: " + ec.message();
        return result;
    }

    const auto csvPath = outDir / "cadenza_playback_events.csv";
    const auto midiPath = outDir / "cadenza_playback.mid";
    const auto summaryPath = outDir / "playback_summary.md";

    if (!writeCsv(csvPath, events)) {
        result.error = "failed to write CSV";
        return result;
    }
    if (!writeMidi(midiPath, style, *section, events)) {
        result.error = "failed to write MIDI";
        return result;
    }
    const auto issues = suspiciousIssues(*section, events);
    if (!writeSummary(summaryPath, style, *section, events, issues)) {
        result.error = "failed to write summary";
        return result;
    }

    result.ok = true;
    result.csvPath = csvPath.string();
    result.midiPath = midiPath.string();
    result.summaryPath = summaryPath.string();
    result.eventCount = static_cast<int>(events.size());
    return result;
}
}
