#include "StyParser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace cadenza::arranger
{
namespace
{
// ============================================================
// Low-level byte reader for the Standard MIDI File parser.
// ============================================================
struct Reader
{
    const std::vector<uint8_t>& buf;
    std::size_t pos = 0;
    bool ok = true;
    std::string err;

    explicit Reader(const std::vector<uint8_t>& b) : buf(b) {}

    bool eof() const noexcept { return pos >= buf.size(); }
    std::size_t remaining() const noexcept { return buf.size() - pos; }

    void fail(const char* msg) {
        if (!ok) return;
        ok = false; err = msg;
    }

    uint8_t u8() {
        if (eof()) { fail("unexpected end of file (u8)"); return 0; }
        return buf[pos++];
    }
    uint16_t u16be() {
        uint16_t a = u8(), b = u8();
        return static_cast<uint16_t>((a << 8) | b);
    }
    uint32_t u32be() {
        uint32_t a = u8(), b = u8(), c = u8(), d = u8();
        return (a << 24) | (b << 16) | (c << 8) | d;
    }
    void skip(std::size_t n) {
        if (remaining() < n) { fail("skip past end"); pos = buf.size(); return; }
        pos += n;
    }
    bool match(const char* tag /*4 chars*/) {
        if (remaining() < 4) return false;
        for (int i = 0; i < 4; ++i)
            if (buf[pos + i] != static_cast<uint8_t>(tag[i])) return false;
        pos += 4;
        return true;
    }
    // SMF variable-length quantity
    uint32_t vlq() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            uint8_t b = u8();
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) return v;
        }
        fail("invalid VLQ");
        return v;
    }
    std::string ascii(std::size_t n) {
        std::string s;
        s.reserve(n);
        for (std::size_t i = 0; i < n; ++i) s += static_cast<char>(u8());
        return s;
    }
};

// ============================================================
// Lowercased, whitespace-collapsed compare for section names.
// ============================================================
std::string normalise(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    bool prevSpace = false;
    for (char c : s) {
        char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lc == ' ' || lc == '\t' || lc == '_' || lc == '-') {
            if (!prevSpace && !out.empty()) out += ' ';
            prevSpace = true;
        } else {
            out += lc;
            prevSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string trimAscii(const std::string& s)
{
    std::size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    std::size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string upperAscii(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string printableAscii(const std::vector<uint8_t>& bytes)
{
    std::string out;
    out.reserve(bytes.size());
    for (auto b : bytes) {
        if (b == 0) {
            out.push_back(' ');
        } else if (b >= 32 && b <= 126) {
            out.push_back(static_cast<char>(b));
        }
    }
    return trimAscii(out);
}

bool parseInt(const std::string& text, int& out)
{
    if (text.empty()) return false;
    char* end = nullptr;
    long v = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = static_cast<int>(v);
    return true;
}

uint32_t readU32be(const std::vector<uint8_t>& bytes, std::size_t pos)
{
    return (uint32_t(bytes[pos]) << 24)
         | (uint32_t(bytes[pos + 1]) << 16)
         | (uint32_t(bytes[pos + 2]) << 8)
         | uint32_t(bytes[pos + 3]);
}

std::string readTag(const std::vector<uint8_t>& bytes, std::size_t pos)
{
    return std::string(reinterpret_cast<const char*>(&bytes[pos]), 4);
}

std::vector<uint8_t> sliceBytes(const std::vector<uint8_t>& bytes,
                                std::size_t begin,
                                std::size_t end)
{
    if (begin > bytes.size()) begin = bytes.size();
    if (end > bytes.size()) end = bytes.size();
    if (end < begin) end = begin;
    return std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                                bytes.begin() + static_cast<std::ptrdiff_t>(end));
}

void addCasmLog(CasmInfo& info, bool verbose, const std::string& line)
{
    info.logLines.push_back(line);
    if (verbose)
        std::fprintf(stderr, "[StyParser] %s\n", line.c_str());
}

void addCasmWarning(CasmInfo& info, bool verbose, const std::string& warning)
{
    info.warnings.push_back(warning);
    addCasmLog(info, verbose, "CASM warning: " + warning);
}

std::string hexDump(const std::vector<uint8_t>& bytes)
{
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0) out << ' ';
        out << std::setw(2) << static_cast<int>(bytes[i]);
    }
    return out.str();
}

// ============================================================
// In-memory MIDI event collected per track for later assembly.
// All ticks are absolute (cumulative).
// ============================================================
struct RawNote
{
    uint64_t startTick = 0;
    uint64_t endTick = 0;
    uint8_t  channel = 0;       // 0..15
    uint8_t  pitch = 60;
    uint8_t  velocity = 100;
    int      programChange = -1;
    int      bankMsb = -1;
    int      bankLsb = -1;
    int      volume = -1;
    int      pan = -1;
    int      reverb = -1;
    int      chorus = -1;
};

// A timed controller / pitch-bend event with an absolute tick. Collected
// alongside notes so the engine can replay a style's expression/bend curves.
struct RawAuto
{
    uint64_t tick = 0;
    uint8_t  channel = 0;   // 0..15
    int      type = 0;      // MIDI CC number, or AutomationEvent::kPitchBend
    int      value = 0;     // CC value 0..127, or pitch-bend 0..16383
};

struct SectionMarker
{
    uint64_t tick = 0;
    std::string cadenzaName;     // e.g. "mainA"
    std::string rawText;
};

struct TrackData
{
    std::vector<RawNote> notes;
    std::vector<RawAuto> automation;   // CC1/CC11/CC64 + pitch bend, absolute ticks
    // For meta-event tempo (microseconds per quarter-note)
    std::optional<uint32_t> firstTempoMicros;
    // Per-channel program changes seen during the track (last write wins for a section).
    std::unordered_map<int, int> programByChannel;
    std::unordered_map<int, int> bankMsbByChannel;
    std::unordered_map<int, int> bankLsbByChannel;
    std::unordered_map<int, int> volumeByChannel;
    std::unordered_map<int, int> panByChannel;
    std::unordered_map<int, int> reverbByChannel;
    std::unordered_map<int, int> chorusByChannel;
};

// Parse a single MTrk chunk. trackLen is the chunk body length.
// Section markers from track 0 (the conventional location) are collected separately.
bool parseTrack(Reader& r, uint32_t trackLen,
                TrackData& out, std::vector<SectionMarker>& sectionMarkers)
{
    std::size_t end = r.pos + trackLen;
    uint64_t absoluteTick = 0;
    uint8_t runningStatus = 0;

    // Open notes: keyed by (channel<<8 | pitch) -> index into out.notes.
    std::unordered_map<int, std::size_t> open;

    while (r.ok && r.pos < end) {
        uint32_t delta = r.vlq();
        absoluteTick += delta;
        if (!r.ok) break;

        uint8_t status = r.u8();
        if (status < 0x80) {
            // Running status: this byte was actually data for the previous status.
            r.pos--;
            status = runningStatus;
        } else if (status < 0xF0) {
            runningStatus = status;
        }

        if (status >= 0x80 && status <= 0xEF) {
            uint8_t hi = status & 0xF0;
            uint8_t ch = status & 0x0F;

            switch (hi) {
                case 0x80: { // Note Off
                    uint8_t pitch = r.u8(); uint8_t /*vel*/ _v = r.u8();
                    int key = (int(ch) << 8) | pitch;
                    auto it = open.find(key);
                    if (it != open.end()) {
                        out.notes[it->second].endTick = absoluteTick;
                        open.erase(it);
                    }
                    break;
                }
                case 0x90: { // Note On (vel 0 == off)
                    uint8_t pitch = r.u8(); uint8_t vel = r.u8();
                    int key = (int(ch) << 8) | pitch;
                    if (vel == 0) {
                        auto it = open.find(key);
                        if (it != open.end()) {
                            out.notes[it->second].endTick = absoluteTick;
                            open.erase(it);
                        }
                    } else {
                        RawNote n;
                        n.startTick = absoluteTick;
                        n.endTick = absoluteTick; // updated when matching off arrives
                        n.channel = ch;
                        n.pitch = pitch;
                        n.velocity = vel;
                        auto itP = out.programByChannel.find(ch);
                        n.programChange = (itP == out.programByChannel.end()) ? -1 : itP->second;
                        auto itMsb = out.bankMsbByChannel.find(ch);
                        n.bankMsb = (itMsb == out.bankMsbByChannel.end()) ? -1 : itMsb->second;
                        auto itLsb = out.bankLsbByChannel.find(ch);
                        n.bankLsb = (itLsb == out.bankLsbByChannel.end()) ? -1 : itLsb->second;
                        auto itVol = out.volumeByChannel.find(ch);
                        n.volume = (itVol == out.volumeByChannel.end()) ? -1 : itVol->second;
                        auto itPan = out.panByChannel.find(ch);
                        n.pan = (itPan == out.panByChannel.end()) ? -1 : itPan->second;
                        auto itRev = out.reverbByChannel.find(ch);
                        n.reverb = (itRev == out.reverbByChannel.end()) ? -1 : itRev->second;
                        auto itCho = out.chorusByChannel.find(ch);
                        n.chorus = (itCho == out.chorusByChannel.end()) ? -1 : itCho->second;
                        open[key] = out.notes.size();
                        out.notes.push_back(n);
                    }
                    break;
                }
                case 0xA0: { r.u8(); r.u8(); break; } // poly aftertouch
                case 0xB0: {
                    uint8_t cc = r.u8();
                    uint8_t value = r.u8();
                    if (cc == 0)
                        out.bankMsbByChannel[ch] = value;
                    else if (cc == 32)
                        out.bankLsbByChannel[ch] = value;
                    else if (cc == 7)
                        out.volumeByChannel[ch] = value;
                    else if (cc == 10)
                        out.panByChannel[ch] = value;
                    else if (cc == 91)
                        out.reverbByChannel[ch] = value;
                    else if (cc == 93)
                        out.chorusByChannel[ch] = value;
                    else if (cc == 1 || cc == 11 || cc == 64)
                        // Musical automation: modulation, expression, sustain.
                        // Captured with their tick so swells/sustains replay live.
                        out.automation.push_back({ absoluteTick, ch, cc, value });
                    break;
                } // CC
                case 0xC0: { // Program Change
                    uint8_t prog = r.u8();
                    out.programByChannel[ch] = prog;
                    break;
                }
                case 0xD0: { r.u8(); break; } // channel aftertouch
                case 0xE0: { // pitch bend (14-bit, LSB then MSB; 8192 = centre)
                    uint8_t lsb = r.u8();
                    uint8_t msb = r.u8();
                    const int bend = (static_cast<int>(msb) << 7) | static_cast<int>(lsb);
                    out.automation.push_back({ absoluteTick, ch, AutomationEvent::kPitchBend, bend });
                    break;
                }
                default: r.fail("unknown midi voice msg"); break;
            }
        } else if (status == 0xFF) {
            // Meta event
            uint8_t metaType = r.u8();
            uint32_t metaLen = r.vlq();
            if (metaType == 0x06 /*marker*/ || metaType == 0x05 /*lyric*/ ||
                metaType == 0x01 /*text*/   || metaType == 0x03 /*track name*/) {
                std::string text = r.ascii(metaLen);
                if (auto mapped = mapSectionMarker(text); mapped) {
                    SectionMarker m;
                    m.tick = absoluteTick;
                    m.cadenzaName = *mapped;
                    m.rawText = text;
                    sectionMarkers.push_back(m);
                }
            } else if (metaType == 0x51 /*tempo*/) {
                if (metaLen == 3 && !out.firstTempoMicros) {
                    uint32_t a = r.u8(), b = r.u8(), c = r.u8();
                    out.firstTempoMicros = (a << 16) | (b << 8) | c;
                } else {
                    r.skip(metaLen);
                }
            } else if (metaType == 0x2F /*end of track*/) {
                r.pos = end; // skip to chunk end
                return r.ok;
            } else {
                r.skip(metaLen);
            }
        } else if (status == 0xF0 || status == 0xF7) {
            // SysEx
            uint32_t len = r.vlq();
            r.skip(len);
        } else {
            r.fail("unknown status byte"); break;
        }
    }

    // If there are still open notes (style file ended without matching off), close them.
    for (const auto& [key, idx] : open) {
        if (out.notes[idx].endTick <= out.notes[idx].startTick)
            out.notes[idx].endTick = out.notes[idx].startTick + 120;
    }
    return r.ok;
}

NoteRole roleFromSourceChordPitch(uint8_t pitch, int sourceRootPc = 0) noexcept
{
    int pc = (static_cast<int>(pitch) - sourceRootPc) % 12;
    if (pc < 0) pc += 12;
    switch (pc) {
        case 0:  return NoteRole::ChordRoot;  // C
        case 4:  return NoteRole::Chord3;     // E (major 3rd of source chord)
        case 7:  return NoteRole::Chord5;     // G
        case 10: return NoteRole::Chord7;     // Bb (dominant 7th)
        case 11: return NoteRole::Chord7;     // B  (major 7th)
        default: return NoteRole::ChordColor; // colour tones follow the chord by root transposition
    }
}

// Heuristic role assignment:
//  - drums (channel 10 = MIDI channel 0..15 == 9 zero-based) -> Absolute
//  - non-drum: assume source chord is C major; assign by pitch class.
NoteRole assignRole(uint8_t channelZeroBased, uint8_t pitch) noexcept
{
    if (channelZeroBased == 9) return NoteRole::Absolute;     // GM drum channel
    return roleFromSourceChordPitch(pitch);
}

NoteRole assignRole(uint8_t channelZeroBased,
                    uint8_t pitch,
                    const std::optional<YamahaChannelPolicy>& policy) noexcept
{
    if (channelZeroBased == 9)
        return NoteRole::Absolute;

    if (!policy || policy->source == YamahaPolicySource::Fallback)
        return assignRole(channelZeroBased, pitch);

    if (policy->ntt == YamahaNtt::Bypass) {
        // BYPASS notes keep their recorded interval; PatternTransposer decides
        // from NTR whether that means fixed pitch or root-only transposition.
        return NoteRole::ChordColor;
    }

    int sourceRootPc = 0;
    if (policy->sourceRoot) {
        static const std::map<std::string, int> roots = {
            { "C", 0 }, { "C#", 1 }, { "D", 2 }, { "Eb", 3 },
            { "E", 4 }, { "F", 5 }, { "F#", 6 }, { "G", 7 },
            { "G#", 8 }, { "A", 9 }, { "Bb", 10 }, { "B", 11 },
        };
        auto it = roots.find(*policy->sourceRoot);
        if (it != roots.end())
            sourceRootPc = it->second;
    }

    if (policy->bassOn)
        return roleFromSourceChordPitch(pitch, sourceRootPc);

    if (policy->ntr == YamahaNtr::RootFixed && policy->ntt == YamahaNtt::Chord)
        return roleFromSourceChordPitch(pitch, sourceRootPc);

    if (policy->ntr == YamahaNtr::RootTransposition && policy->ntt == YamahaNtt::Melody)
        return assignRole(channelZeroBased, pitch);

    return assignRole(channelZeroBased, pitch);
}

// Pick a representative MIDI program for a part = most-recently-set program on that channel
// across this section's notes. Returns -1 if no program-change was ever seen.
int dominantProgram(const std::vector<RawNote>& notes)
{
    std::unordered_map<int, int> tally;
    for (const auto& n : notes) {
        if (n.programChange >= 0) tally[n.programChange]++;
    }
    if (tally.empty()) return -1;
    auto best = std::max_element(tally.begin(), tally.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; });
    return best->first;
}

struct DominantPreset
{
    int program = -1;
    int bankMsb = -1;
    int bankLsb = -1;
    int volume = -1;
    int pan = -1;
    int reverb = -1;
    int chorus = -1;
};

DominantPreset dominantPreset(const std::vector<RawNote>& notes)
{
    DominantPreset preset;
    preset.program = dominantProgram(notes);

    for (const auto& n : notes) {
        if (preset.bankMsb < 0 && n.bankMsb >= 0)
            preset.bankMsb = n.bankMsb;
        if (preset.bankLsb < 0 && n.bankLsb >= 0)
            preset.bankLsb = n.bankLsb;
        if (preset.volume < 0 && n.volume >= 0)
            preset.volume = n.volume;
        if (preset.pan < 0 && n.pan >= 0)
            preset.pan = n.pan;
        if (preset.reverb < 0 && n.reverb >= 0)
            preset.reverb = n.reverb;
        if (preset.chorus < 0 && n.chorus >= 0)
            preset.chorus = n.chorus;
        if (preset.bankMsb >= 0 && preset.bankLsb >= 0 &&
            preset.volume >= 0 && preset.pan >= 0 &&
            preset.reverb >= 0 && preset.chorus >= 0)
            break;
    }
    return preset;
}

const char* gmInstrumentName(int program)
{
    static const char* names[128] = {
        "Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano",
        "Electric Piano 1","Electric Piano 2","Harpsichord","Clavinet",
        "Celesta","Glockenspiel","Music Box","Vibraphone","Marimba","Xylophone","Tubular Bells","Dulcimer",
        "Drawbar Organ","Percussive Organ","Rock Organ","Church Organ","Reed Organ","Accordion","Harmonica","Tango Accordion",
        "Acoustic Nylon Guitar","Acoustic Steel Guitar","Electric Jazz Guitar","Electric Clean Guitar","Electric Muted Guitar","Overdriven Guitar","Distortion Guitar","Guitar Harmonics",
        "Acoustic Bass","Electric Fingered Bass","Electric Picked Bass","Fretless Bass","Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2",
        "Violin","Viola","Cello","Contrabass","Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
        "String Ensemble 1","String Ensemble 2","Synth Strings 1","Synth Strings 2","Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit",
        "Trumpet","Trombone","Tuba","Muted Trumpet","French Horn","Brass Section","Synth Brass 1","Synth Brass 2",
        "Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax","Oboe","English Horn","Bassoon","Clarinet",
        "Piccolo","Flute","Recorder","Pan Flute","Blown Bottle","Shakuhachi","Whistle","Ocarina",
        "Square Lead","Sawtooth Lead","Calliope Lead","Chiff Lead","Charang Lead","Voice Lead","Fifths Lead","Bass + Lead",
        "New Age Pad","Warm Pad","Polysynth Pad","Choir Pad","Bowed Pad","Metallic Pad","Halo Pad","Sweep Pad",
        "Rain FX","Soundtrack FX","Crystal FX","Atmosphere FX","Brightness FX","Goblins FX","Echoes FX","Sci-Fi FX",
        "Sitar","Banjo","Shamisen","Koto","Kalimba","Bagpipe","Fiddle","Shanai",
        "Tinkle Bell","Agogo","Steel Drums","Woodblock","Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal",
        "Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet","Telephone Ring","Helicopter","Applause","Gunshot"
    };
    if (program < 0 || program > 127) return "Unknown";
    return names[program];
}

CasmCtabEntry parseAsciiCtabEntry(const std::string& record,
                                  const std::vector<uint8_t>& raw)
{
    CasmCtabEntry entry;
    entry.raw = raw;

    std::stringstream ss(record);
    std::string token;
    while (std::getline(ss, token, ';')) {
        auto eq = token.find('=');
        if (eq == std::string::npos) continue;

        std::string key = upperAscii(trimAscii(token.substr(0, eq)));
        std::string value = trimAscii(token.substr(eq + 1));
        if (value.empty()) continue;

        if (key == "CH" || key == "CHANNEL") {
            int ch = 0;
            if (parseInt(value, ch) && ch >= 1 && ch <= 16) {
                entry.channel = ch;
                entry.channelRaw = static_cast<uint8_t>(ch - 1);
            } else {
                entry.unknownFields.push_back({ token.substr(0, eq), value });
            }
        } else if (key == "NTR") {
            entry.ntr = value;
        } else if (key == "NTT") {
            entry.ntt = value;
        } else if (key == "ROOT" || key == "SOURCEROOT") {
            entry.sourceRoot = value;
        } else if (key == "CHORD" || key == "SOURCECHORD") {
            entry.sourceChord = value;
        } else if (key == "SECTION" || key == "SECTIONID") {
            entry.sectionName = value;
        } else {
            entry.unknownFields.push_back({ token.substr(0, eq), value });
        }
    }

    return entry;
}

const char* sourceRootName(uint8_t value)
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };
    return value < 12 ? names[value] : nullptr;
}

const char* sourceChordName(uint8_t value)
{
    static const char* names[] = {
        "Maj", "Maj6", "Maj7", "Maj7#11", "Maj(9)", "Maj7(9)", "Maj6(9)", "aug",
        "min", "min6", "min7", "min7b5", "min(9)", "min7(9)", "min7(11)", "minMaj7",
        "minMaj7(9)", "dim", "dim7", "7th", "7sus4", "7b5", "7(9)", "7#11",
        "7(13)", "7(b9)", "7(b13)", "7(#9)", "Maj7aug", "7aug", "1+8", "1+5",
        "sus4", "1+2+5", "cancel"
    };
    return value < (sizeof(names) / sizeof(names[0])) ? names[value] : nullptr;
}

const char* ctabNtrName(uint8_t value)
{
    switch (value) {
        case 0: return "Root Transposition";
        case 1: return "Root Fixed";
        default: return nullptr;
    }
}

const char* ctb2NtrName(uint8_t value)
{
    switch (value) {
        case 0: return "Root Transposition";
        case 1: return "Root Fixed";
        case 2: return "Guitar";
        default: return nullptr;
    }
}

const char* ctabNttName(uint8_t value)
{
    switch (value) {
        case 0: return "Bypass";
        case 1: return "Melody";
        case 2: return "Chord";
        case 3: return "Bass";
        case 4: return "Melodic Minor";
        case 5: return "Harmonic Minor";
        default: return nullptr;
    }
}

std::optional<std::string> ctb2NttName(uint8_t value)
{
    const bool bassOn = (value & 0x80) != 0;
    const uint8_t base = value & 0x7F;

    const char* name = nullptr;
    switch (base) {
        case 0: name = "Bypass"; break;
        case 1: name = "Melody"; break;
        case 2: name = "Chord"; break;
        case 3: name = "Melodic Minor"; break;
        case 4: name = "Melodic Minor 5th Var."; break;
        case 5: name = "Harmonic Minor"; break;
        case 6: name = "Harmonic Minor 5th Var."; break;
        case 7: name = "Natural Minor"; break;
        case 8: name = "Natural Minor 5th Var."; break;
        case 9: name = "Dorian"; break;
        case 10: name = "Dorian 5th Var."; break;
        default: return std::nullopt;
    }

    std::string out = name;
    if (bassOn)
        out += " (Bass On)";
    return out;
}

YamahaNtr yamahaNtrFromRaw(uint8_t value) noexcept
{
    switch (value) {
        case 0: return YamahaNtr::RootTransposition;
        case 1: return YamahaNtr::RootFixed;
        case 2: return YamahaNtr::Guitar;
        default: return YamahaNtr::Unknown;
    }
}

YamahaNtt yamahaNttFromCtb2Raw(uint8_t value, YamahaNtr ntr) noexcept
{
    const uint8_t base = value & 0x7F;
    if (ntr == YamahaNtr::Guitar) {
        switch (base) {
            case 0: return YamahaNtt::AllPurpose;
            case 1: return YamahaNtt::Stroke;
            case 2: return YamahaNtt::Arpeggio;
            default: return YamahaNtt::Unknown;
        }
    }

    // SFF GE (Ctb2) NTT table. The minor scales live at EVEN indices; the odd
    // index just above each is the same scale's "+5th" variant (differs only in
    // how the chord 5th is treated, which our scale fit already approximates).
    // Verified against Genos 2 preset styles, which use 4/6/8/10 (the older code
    // wrongly used the odd values 3/5/7/9 and left these as Unknown).
    switch (base) {
        case 0:  return YamahaNtt::Bypass;
        case 1:  return YamahaNtt::Melody;
        case 2:  return YamahaNtt::Chord;
        case 3:  return YamahaNtt::Melody;          // Bass: root-transpose the line
        case 4:  return YamahaNtt::MelodicMinor;
        case 5:  return YamahaNtt::MelodicMinor;     // +5th variant
        case 6:  return YamahaNtt::HarmonicMinor;
        case 7:  return YamahaNtt::HarmonicMinor;    // +5th variant
        case 8:  return YamahaNtt::NaturalMinor;
        case 9:  return YamahaNtt::NaturalMinor;     // +5th variant
        case 10: return YamahaNtt::Dorian;
        case 11: return YamahaNtt::Dorian;           // +5th variant
        default: return YamahaNtt::Unknown;
    }
}

YamahaNtt yamahaNttFromCtabRaw(uint8_t value, bool& bassOn) noexcept
{
    bassOn = false;
    switch (value) {
        case 0: return YamahaNtt::Bypass;
        case 1: return YamahaNtt::Melody;
        case 2: return YamahaNtt::Chord;
        case 3:
            bassOn = true;
            return YamahaNtt::Melody;
        case 4: return YamahaNtt::MelodicMinor;
        case 5: return YamahaNtt::HarmonicMinor;
        default: return YamahaNtt::Unknown;
    }
}

YamahaRetriggerRule yamahaRetriggerRuleFromRaw(uint8_t value) noexcept
{
    switch (value) {
        case 0: return YamahaRetriggerRule::Stop;
        case 1: return YamahaRetriggerRule::PitchShift;
        case 2: return YamahaRetriggerRule::PitchShiftToRoot;
        case 3: return YamahaRetriggerRule::Retrigger;
        case 4: return YamahaRetriggerRule::RetriggerToRoot;
        case 5: return YamahaRetriggerRule::NoteGenerator;
        default: return YamahaRetriggerRule::Unknown;
    }
}

YamahaNtr yamahaNtrFromText(const std::string& value)
{
    const auto text = normalise(value);
    if (text == "root trans" || text == "roottrans" || text == "root transposition")
        return YamahaNtr::RootTransposition;
    if (text == "root fixed" || text == "rootfixed")
        return YamahaNtr::RootFixed;
    if (text == "guitar")
        return YamahaNtr::Guitar;
    return YamahaNtr::Unknown;
}

YamahaNtt yamahaNttFromText(const std::string& value)
{
    const auto text = normalise(value);
    if (text == "bypass") return YamahaNtt::Bypass;
    if (text == "melody") return YamahaNtt::Melody;
    if (text == "chord") return YamahaNtt::Chord;
    if (text == "melodic minor") return YamahaNtt::MelodicMinor;
    if (text == "harmonic minor") return YamahaNtt::HarmonicMinor;
    if (text == "natural minor") return YamahaNtt::NaturalMinor;
    if (text == "dorian") return YamahaNtt::Dorian;
    if (text == "guitar all purpose" || text == "all purpose") return YamahaNtt::AllPurpose;
    if (text == "guitar stroke" || text == "stroke") return YamahaNtt::Stroke;
    if (text == "guitar arpeggio" || text == "arpeggio") return YamahaNtt::Arpeggio;
    return YamahaNtt::Unknown;
}

std::string destinationPartNameForChannel(int midiChannel)
{
    if (midiChannel == 9) return "rhythm2";
    if (midiChannel == 10) return "drums";
    if (midiChannel == 11) return "bass";
    if (midiChannel == 12) return "chord1";
    if (midiChannel == 13) return "chord2";
    if (midiChannel == 14) return "pad";
    if (midiChannel == 15) return "phrase1";
    if (midiChannel == 16) return "phrase2";
    if (midiChannel == 2) return "bass";
    if (midiChannel == 3) return "harmony";
    return "part-ch" + std::to_string(midiChannel);
}

// Standard Yamaha SFF accompaniment channel layout (MIDI channels 9..16).
// Returns nullptr for channels that are not style-accompaniment parts.
const char* yamahaStylePartName(int midiChannel) noexcept
{
    switch (midiChannel) {
        case 9:  return "rhythm2";   // sub rhythm
        case 10: return "drums";     // main rhythm (GM drum channel)
        case 11: return "bass";
        case 12: return "chord1";
        case 13: return "chord2";
        case 14: return "pad";
        case 15: return "phrase1";
        case 16: return "phrase2";
        default: return nullptr;     // 1..8 are not SFF style parts
    }
}

void fillCommonPolicyFields(YamahaChannelPolicy& policy,
                            const CasmCtabEntry& entry,
                            const std::vector<uint8_t>& payload,
                            YamahaPolicySource source)
{
    policy.source = source;
    policy.rawBytes = payload;
    if (entry.channel)
        policy.sourceChannel = *entry.channel;
    if (entry.sourceRoot)
        policy.sourceRoot = entry.sourceRoot;
    if (entry.sourceChord)
        policy.sourceChord = entry.sourceChord;

    if (policy.sourceChannel > 0) {
        policy.destinationPart = destinationPartNameForChannel(policy.sourceChannel);
        policy.destinationType = policy.destinationPart;
        policy.destinationName = policy.destinationPart;
    }
}

void attachAsciiPolicy(CasmCtabEntry& entry)
{
    if (!entry.channel)
        return;

    YamahaChannelPolicy policy;
    fillCommonPolicyFields(policy, entry, entry.raw, YamahaPolicySource::CASM);
    if (entry.ntr)
        policy.ntr = yamahaNtrFromText(*entry.ntr);
    if (entry.ntt)
        policy.ntt = yamahaNttFromText(*entry.ntt);
    entry.policy = std::move(policy);
}

CasmCtabEntry parseBinaryCtabEntry(const std::vector<uint8_t>& payload)
{
    CasmCtabEntry entry;
    entry.raw = payload;

    if (payload.size() < 22)
        return entry;

    const uint8_t sourceChannel = payload[0];
    entry.channelRaw = sourceChannel;
    if (sourceChannel <= 15)
        entry.channel = static_cast<int>(sourceChannel) + 1;

    entry.sourceRootRaw = payload[18];
    if (auto* name = sourceRootName(payload[18]))
        entry.sourceRoot = name;

    entry.sourceChordRaw = payload[19];
    if (auto* name = sourceChordName(payload[19]))
        entry.sourceChord = name;

    entry.ntrRaw = payload[20];
    if (auto* name = ctabNtrName(payload[20]))
        entry.ntr = name;

    entry.nttRaw = payload[21];
    if (auto* name = ctabNttName(payload[21]))
        entry.ntt = name;

    YamahaChannelPolicy policy;
    fillCommonPolicyFields(policy, entry, payload, YamahaPolicySource::Ctab);
    policy.rawNtr = payload[20];
    policy.ntr = yamahaNtrFromRaw(payload[20]);
    policy.rawNtt = payload[21];
    bool bassOn = false;
    policy.ntt = yamahaNttFromCtabRaw(payload[21], bassOn);
    policy.bassOn = bassOn;
    if (payload.size() > 22)
        policy.chordRootUpperLimit = payload[22];
    if (payload.size() > 23)
        policy.noteLowLimit = payload[23];
    if (payload.size() > 24)
        policy.noteHighLimit = payload[24];
    if (payload.size() > 25) {
        policy.rawRtr = payload[25];
        policy.retriggerRule = yamahaRetriggerRuleFromRaw(payload[25]);
    }
    entry.policy = std::move(policy);

    return entry;
}

void decodeCommonCtabFields(CasmCtabEntry& entry, const std::vector<uint8_t>& payload)
{
    if (payload.empty())
        return;

    const uint8_t sourceChannel = payload[0];
    entry.channelRaw = sourceChannel;
    if (sourceChannel <= 15)
        entry.channel = static_cast<int>(sourceChannel) + 1;

    if (payload.size() > 18) {
        entry.sourceRootRaw = payload[18];
        if (auto* name = sourceRootName(payload[18]))
            entry.sourceRoot = name;
    }

    if (payload.size() > 19) {
        entry.sourceChordRaw = payload[19];
        if (auto* name = sourceChordName(payload[19]))
            entry.sourceChord = name;
    }
}

CasmCtabEntry parseBinaryCtb2Entry(const std::vector<uint8_t>& payload)
{
    CasmCtabEntry entry;
    entry.raw = payload;
    decodeCommonCtabFields(entry, payload);

    if (payload.size() < 34)
        return entry;

    // Bytes 20/21 are the source-note range (low/high) for this entry: 00/7F is
    // "full range", while a split entry (e.g. Intro/Ending B/C) carries a high
    // note like 0x5F. Either way the effective NTR/NTT pair lives in the middle
    // substructure at bytes 28/29, so decode it unconditionally. (Earlier code
    // wrongly required 00/7F and dropped every split entry to the fallback.)
    if (payload[20] != 0 || payload[21] != 0x7F)
        entry.unknownFields.push_back({ "ctb2SourceRange", hexDump({ payload[20], payload[21] }) });

    entry.ntrRaw = payload[28];
    if (auto* name = ctb2NtrName(payload[28]))
        entry.ntr = name;

    entry.nttRaw = payload[29];
    if (auto name = ctb2NttName(payload[29]))
        entry.ntt = *name;

    YamahaChannelPolicy policy;
    fillCommonPolicyFields(policy, entry, payload, YamahaPolicySource::Ctb2);
    policy.rawNtr = payload[28];
    policy.ntr = yamahaNtrFromRaw(payload[28]);
    policy.rawNtt = payload[29];
    policy.ntt = yamahaNttFromCtb2Raw(payload[29], policy.ntr);
    policy.bassOn = (payload[29] & 0x80) != 0;
    policy.chordRootUpperLimit = payload[30];
    policy.noteLowLimit = payload[31];
    policy.noteHighLimit = payload[32];
    policy.rawRtr = payload[33];
    policy.retriggerRule = yamahaRetriggerRuleFromRaw(payload[33]);
    entry.policy = std::move(policy);

    return entry;
}

// Real Yamaha Ctab/Ctb2 entries are BINARY. A legacy/text "key=value" form is
// also supported, but it must be detected by being genuinely textual — not just
// by containing a '=' byte. A binary entry whose note-limit (or other) byte is
// 0x3D ('=') must NOT be mistaken for ASCII, or its policy is silently lost.
bool looksLikeAsciiCtab(const std::vector<uint8_t>& payload) noexcept
{
    bool hasEquals = false;
    for (uint8_t b : payload) {
        if (b == '=') { hasEquals = true; continue; }
        const bool printable = (b >= 0x20 && b < 0x7F) || b == 0x09 || b == 0x0A || b == 0x0D;
        if (!printable)
            return false;   // a control/high byte means this is a binary entry
    }
    return hasEquals;
}

std::vector<CasmCtabEntry> parseCtabPayload(const std::vector<uint8_t>& payload,
                                            const std::string& tag,
                                            CasmInfo& info,
                                            bool verbose)
{
    addCasmLog(info, verbose, "CASM " + tag + " hex: " + hexDump(payload));

    const auto text = printableAscii(payload);
    std::vector<CasmCtabEntry> entries;

    if (looksLikeAsciiCtab(payload)) {
        std::stringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            line = trimAscii(line);
            if (line.empty()) continue;
            auto entry = parseAsciiCtabEntry(line, payload);
            attachAsciiPolicy(entry);
            entries.push_back(std::move(entry));
        }
    }

    if (entries.empty() && !payload.empty()) {
        if (tag == "Ctb2")
            entries.push_back(parseBinaryCtb2Entry(payload));
        else
            entries.push_back(parseBinaryCtabEntry(payload));
    }

    return entries;
}

CasmCseg parseCsegPayload(const std::vector<uint8_t>& bytes,
                          std::size_t start,
                          std::size_t end,
                          CasmInfo& info,
                          bool verbose)
{
    CasmCseg cseg;
    cseg.raw = sliceBytes(bytes, start, end);

    std::size_t pos = start;
    while (pos + 8 <= end) {
        const auto tag = readTag(bytes, pos);
        const uint32_t len = readU32be(bytes, pos + 4);
        const std::size_t bodyStart = pos + 8;
        const std::size_t bodyEnd = bodyStart + len;
        cseg.childBlockTags.push_back(tag);

        if (bodyEnd > end || bodyEnd < bodyStart) {
            addCasmWarning(info, verbose, "truncated CSEG child block " + tag);
            return cseg;
        }

        auto payload = sliceBytes(bytes, bodyStart, bodyEnd);
        if (tag == "Sdec") {
            cseg.sdecRaw = payload;
            auto section = printableAscii(payload);
            if (!section.empty()) {
                cseg.sectionName = section;
                cseg.sectionId = section;
            }
        } else if (tag == "Ctab" || tag == "Ctb2") {
            auto entries = parseCtabPayload(payload, tag, info, verbose);
            for (auto& entry : entries) {
                if (!entry.sectionName && cseg.sectionName)
                    entry.sectionName = cseg.sectionName;
                if (entry.ntr || entry.ntt) {
                    std::string line = "CASM " + tag;
                    if (entry.channel) line += " ch=" + std::to_string(*entry.channel);
                    if (entry.ntr) line += " NTR=" + *entry.ntr;
                    if (entry.ntt) line += " NTT=" + *entry.ntt;
                    addCasmLog(info, verbose, line);
                }
                cseg.ctabEntries.push_back(std::move(entry));
                ++info.ctabEntryCount;
            }
        }

        pos = bodyEnd;
    }

    if (pos != end)
        addCasmWarning(info, verbose, "trailing bytes in CSEG");

    return cseg;
}

CasmInfo parseCasmAfterSmf(const std::vector<uint8_t>& bytes,
                           std::size_t smfEnd,
                           bool verbose)
{
    CasmInfo info;

    std::size_t casmOffset = std::string::npos;
    for (std::size_t pos = smfEnd; pos + 4 <= bytes.size(); ++pos) {
        if (bytes[pos] == 'C' && bytes[pos + 1] == 'A' &&
            bytes[pos + 2] == 'S' && bytes[pos + 3] == 'M') {
            casmOffset = pos;
            break;
        }
    }

    if (casmOffset == std::string::npos) {
        addCasmLog(info, verbose, "CASM not found");
        return info;
    }

    info.found = true;
    info.offset = casmOffset;
    addCasmLog(info, verbose, "CASM found at byte " + std::to_string(casmOffset));

    if (casmOffset + 8 > bytes.size()) {
        addCasmWarning(info, verbose, "truncated CASM chunk header");
        return info;
    }

    info.declaredSize = readU32be(bytes, casmOffset + 4);
    const std::size_t payloadStart = casmOffset + 8;
    std::size_t payloadEnd = payloadStart + info.declaredSize;
    if (payloadEnd > bytes.size() || payloadEnd < payloadStart) {
        addCasmWarning(info, verbose, "CASM declared size exceeds available bytes");
        payloadEnd = bytes.size();
    }
    info.parsedSize = payloadEnd - payloadStart;

    std::size_t pos = payloadStart;
    while (pos + 8 <= payloadEnd) {
        const auto tag = readTag(bytes, pos);
        const uint32_t len = readU32be(bytes, pos + 4);
        const std::size_t bodyStart = pos + 8;
        const std::size_t bodyEnd = bodyStart + len;
        info.topLevelBlockTags.push_back(tag);

        if (bodyEnd > payloadEnd || bodyEnd < bodyStart) {
            addCasmWarning(info, verbose, "truncated CASM child block " + tag);
            break;
        }

        if (tag == "CSEG")
            info.csegs.push_back(parseCsegPayload(bytes, bodyStart, bodyEnd, info, verbose));

        pos = bodyEnd;
    }

    if (pos != payloadEnd)
        addCasmWarning(info, verbose, "trailing bytes in CASM");

    addCasmLog(info, verbose, "CASM CSEG blocks: " + std::to_string(info.csegs.size()));
    addCasmLog(info, verbose, "CASM Ctab entries: " + std::to_string(info.ctabEntryCount));
    return info;
}
}

// ============================================================
// PUBLIC API
// ============================================================
std::optional<std::string> mapSectionMarker(const std::string& marker)
{
    static const std::map<std::string, std::string> table = {
        { "main a", "mainA" }, { "main b", "mainB" },
        { "main c", "mainC" }, { "main d", "mainD" },
        { "intro a", "intro" }, { "intro b", "introB" }, { "intro c", "introC" }, { "intro", "intro" },
        { "ending a", "ending" }, { "ending b", "endingB" }, { "ending c", "endingC" }, { "ending", "ending" },
        { "fill in aa", "fillAA" }, { "fill in bb", "fillBB" },
        { "fill in cc", "fillCC" }, { "fill in dd", "fillDD" },
        { "fill in ab", "fillAB" }, { "fill in ba", "fillBA" },
        { "fill in ac", "fillAC" }, { "fill in ca", "fillCA" },
        { "break", "fillBreak" },
    };
    auto key = normalise(marker);
    auto it = table.find(key);
    if (it == table.end()) return std::nullopt;
    return it->second;
}

bool csegMatchesSection(const CasmCseg& cseg, const std::string& sectionId)
{
    if (!cseg.sectionName)
        return true;

    std::stringstream ss(*cseg.sectionName);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = trimAscii(token);
        if (token.empty())
            continue;
        if (auto mapped = mapSectionMarker(token); mapped && *mapped == sectionId)
            return true;
        if (normalise(token) == normalise(sectionId))
            return true;
    }
    return false;
}

std::optional<YamahaChannelPolicy> findYamahaPolicy(const CasmInfo& casm,
                                                    const std::string& sectionId,
                                                    int midiChannel)
{
    for (const auto& cseg : casm.csegs) {
        if (!csegMatchesSection(cseg, sectionId))
            continue;
        for (const auto& entry : cseg.ctabEntries) {
            if (entry.policy && entry.policy->sourceChannel == midiChannel)
                return entry.policy;
            if (entry.policy && entry.channel && *entry.channel == midiChannel)
                return entry.policy;
        }
    }
    return std::nullopt;
}

// Coarse section family so a section with no policy can borrow from a relative:
// intro/ending/main/fill/break. Intros behave like intros (often Bypass), so we
// prefer a same-family donor before any other section.
std::string sectionFamily(const std::string& id)
{
    const std::string n = normalise(id);
    auto starts = [&](const char* p) { return n.rfind(p, 0) == 0; };
    if (starts("intro"))  return "intro";
    if (starts("ending")) return "ending";
    if (starts("fill"))   return "fill";
    if (starts("break"))  return "break";
    if (starts("main"))   return "main";
    return n;
}

bool csegHasFamily(const CasmCseg& cseg, const std::string& family)
{
    if (!cseg.sectionName)
        return false;
    std::stringstream ss(*cseg.sectionName);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token = trimAscii(token);
        if (token.empty())
            continue;
        std::string id = token;
        if (auto mapped = mapSectionMarker(token))
            id = *mapped;
        if (sectionFamily(id) == family)
            return true;
    }
    return false;
}

// When a section defines no policy for a channel, reuse that channel's REAL
// policy from another section (preferring the same family) instead of dropping
// to the generic C-major heuristic. Sets sameFamily=true if the donor matched.
std::optional<YamahaChannelPolicy> findSiblingYamahaPolicy(const CasmInfo& casm,
                                                           const std::string& sectionId,
                                                           int midiChannel,
                                                           bool& sameFamily)
{
    const std::string family = sectionFamily(sectionId);
    std::optional<YamahaChannelPolicy> anyMatch;

    for (const auto& cseg : casm.csegs) {
        const YamahaChannelPolicy* found = nullptr;
        for (const auto& entry : cseg.ctabEntries) {
            if (entry.policy
                && (entry.policy->sourceChannel == midiChannel
                    || (entry.channel && *entry.channel == midiChannel))) {
                found = &*entry.policy;
                break;
            }
        }
        if (!found)
            continue;
        if (csegHasFamily(cseg, family)) {
            sameFamily = true;
            return *found;
        }
        if (!anyMatch)
            anyMatch = *found;
    }

    sameFamily = false;
    return anyMatch;
}

YamahaChannelPolicy fallbackYamahaPolicy(int midiChannel)
{
    YamahaChannelPolicy policy;
    policy.source = YamahaPolicySource::Fallback;
    policy.sourceChannel = midiChannel;
    policy.destinationPart = destinationPartNameForChannel(midiChannel);
    policy.destinationType = policy.destinationPart;
    policy.destinationName = policy.destinationPart;

    // When a style has no CASM policy for this channel, pick the NTR/NTT that
    // matches the standard Yamaha role for the channel so the part follows chords
    // the way a real arranger would, instead of snapping every note to a chord
    // tone. NTR/NTT left Unknown (other channels) keep the generic heuristic.
    switch (midiChannel) {
        case 9:   // sub rhythm / comp
        case 12:  // chord 1
        case 13:  // chord 2
        case 14:  // pad
            policy.ntr = YamahaNtr::RootTransposition;
            policy.ntt = YamahaNtt::Chord;    // chord tones follow the chord; colour tones root-shift
            break;
        case 11:  // bass: root-shift the recorded line, preserve its shape
            policy.ntr = YamahaNtr::RootTransposition;
            policy.ntt = YamahaNtt::Bypass;
            break;
        case 15:  // phrase 1
        case 16:  // phrase 2
            policy.ntr = YamahaNtr::RootTransposition;
            policy.ntt = YamahaNtt::Melody;   // melodic riff: root-shift, keep the melody
            break;
        case 10:  // drums: never transpose
            policy.ntr = YamahaNtr::RootFixed;
            policy.ntt = YamahaNtt::Bypass;
            break;
        default:
            break;                            // leave Unknown -> generic heuristic
    }
    return policy;
}

void addParseWarning(Style& style, const std::string& warning)
{
    if (std::find(style.parseWarnings.begin(), style.parseWarnings.end(), warning) == style.parseWarnings.end())
        style.parseWarnings.push_back(warning);
}

bool isUnknownDestinationRole(const std::string& role)
{
    return role.rfind("part-ch", 0) == 0;
}

YamahaStyleFormat inferYamahaFormat(const CasmInfo& casm) noexcept
{
    bool hasCtab = false;
    for (const auto& cseg : casm.csegs) {
        for (const auto& tag : cseg.childBlockTags) {
            if (tag == "Ctb2")
                return YamahaStyleFormat::SFF2;
            if (tag == "Ctab")
                hasCtab = true;
        }
    }
    return hasCtab ? YamahaStyleFormat::SFF1 : YamahaStyleFormat::Unknown;
}

StyParseResult parseStyBytes(const std::vector<uint8_t>& bytes,
                             const StyParseOptions& options)
{
    StyParseResult result;
    Reader r(bytes);

    // ---- MThd ----
    if (!r.match("MThd")) {
        // No Standard MIDI File header. Identify common non-Yamaha formats so the
        // message is understandable instead of a cryptic "missing MThd".
        auto contains = [&bytes](const std::string& sig) {
            const std::size_t limit = bytes.size() < 256 ? bytes.size() : 256;
            if (sig.size() > limit) return false;
            for (std::size_t i = 0; i + sig.size() <= limit; ++i) {
                bool match = true;
                for (std::size_t j = 0; j < sig.size(); ++j)
                    if (bytes[i + j] != static_cast<uint8_t>(sig[j])) { match = false; break; }
                if (match) return true;
            }
            return false;
        };
        result.ok = false;
        if (bytes.empty())
            result.error = "empty file";
        else if (contains("KORF") || contains("KORG"))
            result.error = "Korg style format (not supported)";
        else
            result.error = "not a Yamaha style (no MThd header)";
        return result;
    }
    uint32_t headerLen = r.u32be();
    if (headerLen != 6) { result.ok = false; result.error = "unexpected MThd size"; return result; }
    uint16_t format = r.u16be();
    uint16_t numTracks = r.u16be();
    uint16_t division = r.u16be();
    if (format > 2) { result.ok = false; result.error = "unsupported SMF format"; return result; }

    int ticksPerQuarter = 0;
    if (division & 0x8000) {
        // SMPTE division — unusual for arranger styles. Fall back to 480.
        ticksPerQuarter = 480;
    } else {
        ticksPerQuarter = division;
        if (ticksPerQuarter <= 0) ticksPerQuarter = 480;
    }

    // ---- MTrk x numTracks ----
    std::vector<TrackData> tracks;
    std::vector<SectionMarker> markers;
    tracks.reserve(numTracks);

    for (int t = 0; t < numTracks && r.ok; ++t) {
        if (!r.match("MTrk")) {
            if (options.verbose)
                std::fprintf(stderr, "track %d: expected MTrk, stopping\n", t);
            break;
        }
        uint32_t trackLen = r.u32be();
        TrackData td;
        if (!parseTrack(r, trackLen, td, markers)) {
            result.ok = false; result.error = "track parse failed: " + r.err; return result;
        }
        tracks.push_back(std::move(td));
    }

    if (!r.ok) { result.ok = false; result.error = r.err; return result; }
    if (tracks.empty()) { result.ok = false; result.error = "no MIDI tracks"; return result; }

    result.casm = parseCasmAfterSmf(bytes, r.pos, options.verbose);

    // ---- Determine tempo ----
    int tempoBpm = options.defaultTempo;
    for (const auto& t : tracks) {
        if (t.firstTempoMicros) {
            tempoBpm = static_cast<int>(60'000'000.0 / *t.firstTempoMicros + 0.5);
            break;
        }
    }

    // ---- Assemble Style ----
    Style& style = result.style;
    style.schema = "cadenza.style.v1";
    style.id           = options.overrideId.empty()   ? std::string("imported-style") : options.overrideId;
    style.name         = options.overrideName.empty() ? std::string("Imported Style") : options.overrideName;
    style.defaultTempo = tempoBpm;
    style.beatsPerBar  = 4;
    style.beatUnit     = 4;
    style.ticksPerBeat = ticksPerQuarter;
    style.yamahaFormat = inferYamahaFormat(result.casm);

    for (const auto& warning : result.casm.warnings)
        addParseWarning(style, warning);
    if (!result.casm.found)
        addParseWarning(style, "missing CASM policy, using fallback role mapping");

    // Flatten all notes from all tracks for easier section splitting.
    struct FlatNote {
        uint64_t startTick;
        uint64_t endTick;
        uint8_t channel;
        uint8_t pitch;
        uint8_t velocity;
        int programChange;
        int bankMsb;
        int bankLsb;
        int volume;
        int pan;
        int reverb;
        int chorus;
    };
    std::vector<FlatNote> all;
    for (const auto& t : tracks)
        for (const auto& n : t.notes)
            all.push_back({ n.startTick, n.endTick, n.channel, n.pitch, n.velocity,
                            n.programChange, n.bankMsb, n.bankLsb,
                            n.volume, n.pan, n.reverb, n.chorus });

    std::sort(all.begin(), all.end(),
              [](const FlatNote& a, const FlatNote& b) { return a.startTick < b.startTick; });

    // Flatten controller/pitch-bend automation from all tracks, sorted by tick,
    // so it can be sliced into sections exactly like notes.
    std::vector<RawAuto> allAuto;
    for (const auto& t : tracks)
        for (const auto& a : t.automation)
            allAuto.push_back(a);
    std::sort(allAuto.begin(), allAuto.end(),
              [](const RawAuto& a, const RawAuto& b) { return a.tick < b.tick; });

    // If no section markers were found, treat the whole file as one mainA section.
    if (markers.empty()) {
        SectionMarker m;
        m.tick = 0;
        m.cadenzaName = "mainA";
        m.rawText = "(auto)";
        markers.push_back(m);
    }
    std::sort(markers.begin(), markers.end(),
              [](const SectionMarker& a, const SectionMarker& b) { return a.tick < b.tick; });

    // Find the overall end tick.
    uint64_t endTick = 0;
    for (const auto& n : all) endTick = std::max(endTick, n.endTick);

    // Build each section.
    for (std::size_t i = 0; i < markers.size(); ++i) {
        const auto& sm = markers[i];
        uint64_t startT = sm.tick;
        uint64_t stopT  = (i + 1 < markers.size()) ? markers[i + 1].tick : endTick;
        if (stopT <= startT) continue;

        Section section;
        section.name = sm.cadenzaName;
        uint64_t lenTicks = stopT - startT;
        section.barCount = std::max<int>(1, static_cast<int>(lenTicks / (style.beatsPerBar * style.ticksPerBeat)));

        // Group this section's notes by MIDI channel into Parts.
        std::map<int, std::vector<FlatNote>> byChannel;
        for (const auto& n : all) {
            if (n.startTick >= startT && n.startTick < stopT) {
                byChannel[n.channel].push_back(n);
            }
        }

        // Group this section's automation by MIDI channel, same window.
        std::map<int, std::vector<RawAuto>> autoByChannel;
        for (const auto& a : allAuto) {
            if (a.tick >= startT && a.tick < stopT)
                autoByChannel[a.channel].push_back(a);
        }

        // Yamaha SFF accompaniment lives on MIDI channels 9..16. Real .sty files
        // put the band there; channels 1..8 are not style parts (often duplicates
        // or non-arranger data) and just muddy playback. If this section has any
        // 9..16 part, drop the 1..8 ones. (Simple/test styles with only low
        // channels keep everything.)
        bool hasStyleChannels = false;
        for (const auto& [ch, notes] : byChannel) {
            const int midi = ch + 1;
            if (midi >= 9 && midi <= 16) { hasStyleChannels = true; break; }
        }

        for (auto& [ch, notes] : byChannel) {
            const int midiCh = ch + 1;
            if (hasStyleChannels && (midiCh < 9 || midiCh > 16))
                continue;   // skip non-SFF channels when real style channels exist

            Part part;
            part.midiChannel = ch + 1;  // SMF channels are 0-based; Cadenza uses 1-based for display.
            auto parsedPolicy = findYamahaPolicy(result.casm, section.name, part.midiChannel);
            bool inheritedPolicy = false;
            bool inheritedSameFamily = false;
            if (!parsedPolicy) {
                // No policy for this exact section: borrow the channel's real policy
                // from a sibling section before resorting to the heuristic.
                if (auto sib = findSiblingYamahaPolicy(result.casm, section.name,
                                                       part.midiChannel, inheritedSameFamily)) {
                    parsedPolicy = sib;
                    inheritedPolicy = true;
                }
            }
            part.yamahaPolicy = parsedPolicy.value_or(fallbackYamahaPolicy(part.midiChannel));
            if (!parsedPolicy) {
                addParseWarning(style,
                    "section " + section.name + " channel " + std::to_string(part.midiChannel)
                    + " missing NTR/NTT policy, using fallback role mapping");
            } else if (inheritedPolicy) {
                addParseWarning(style,
                    "section " + section.name + " channel " + std::to_string(part.midiChannel)
                    + " inherited NTR/NTT policy from a "
                    + (inheritedSameFamily ? "same-family" : "related") + " section");
            } else if (part.yamahaPolicy) {
                const auto& policy = *part.yamahaPolicy;
                if (!policy.sourceRoot || !policy.sourceChord) {
                    addParseWarning(style,
                        "section " + section.name + " channel " + std::to_string(part.midiChannel)
                        + " missing source chord root/type, using C major defaults");
                }
                if (policy.ntr == YamahaNtr::Unknown || policy.ntt == YamahaNtt::Unknown) {
                    addParseWarning(style,
                        "section " + section.name + " channel " + std::to_string(part.midiChannel)
                        + " has unknown NTR/NTT policy, using heuristic role mapping");
                }
            }

            if (part.yamahaPolicy && isUnknownDestinationRole(part.yamahaPolicy->destinationPart)) {
                addParseWarning(style,
                    "section " + section.name + " channel " + std::to_string(part.midiChannel)
                    + " destination role unknown/unmapped: " + part.yamahaPolicy->destinationPart);
            }

            auto preset = dominantPreset(
                [&]() {
                    std::vector<RawNote> tmp;
                    tmp.reserve(notes.size());
                    for (const auto& n : notes) {
                        RawNote raw;
                        raw.startTick = n.startTick;
                        raw.endTick = n.endTick;
                        raw.channel = n.channel;
                        raw.pitch = n.pitch;
                        raw.velocity = n.velocity;
                        raw.programChange = n.programChange;
                        raw.bankMsb = n.bankMsb;
                        raw.bankLsb = n.bankLsb;
                        raw.volume = n.volume;
                        raw.pan = n.pan;
                        raw.reverb = n.reverb;
                        raw.chorus = n.chorus;
                        tmp.push_back(raw);
                    }
                    return tmp;
                }()
            );
            if (preset.program >= 0)
                part.program = preset.program;
            if (preset.bankMsb >= 0)
                part.bankMsb = preset.bankMsb;
            if (preset.bankLsb >= 0)
                part.bankLsb = preset.bankLsb;
            if (preset.volume >= 0)
                part.volume = preset.volume;
            if (preset.pan >= 0)
                part.pan = preset.pan;
            if (preset.reverb >= 0)
                part.reverb = preset.reverb;
            if (preset.chorus >= 0)
                part.chorus = preset.chorus;

            // A Yamaha drum/SFX bank (MSB 126/127, or GM2 drum MSB 120) means the
            // part is percussion even when it isn't on MIDI channel 10 — e.g. the
            // RHY2 sub-rhythm on channel 9. Such parts must NOT be pitch-shifted.
            const bool drumBank = (part.bankMsb == 127 || part.bankMsb == 126 || part.bankMsb == 120);

            if (ch == 9) {   // MIDI channel 10 — GM drum channel
                part.name = "drums";
                part.instrument = "Standard Kit";
                part.percussion = true;
            } else if (const char* yn = yamahaStylePartName(midiCh)) {
                // MIDI 9/11..16 — label by the standard SFF accompaniment role.
                part.name = yn;
                part.instrument = (preset.program >= 0) ? gmInstrumentName(preset.program) : "Unknown";
            } else {
                part.name = (ch == 1) ? "bass" : (ch == 2) ? "harmony" : ("part-ch" + std::to_string(ch + 1));
                if (part.yamahaPolicy && part.yamahaPolicy->bassOn)
                    part.name = "bass";
                part.instrument = (preset.program >= 0) ? gmInstrumentName(preset.program) : "Unknown";
            }

            if (drumBank)
                part.percussion = true;

            if (part.percussion && part.midiChannel != 10) {
                addParseWarning(style,
                    "channel " + std::to_string(part.midiChannel)
                    + " percussion detected, routing to GM drum playback channel 10");
            }

            // The pad often sits an octave too high; drop it one octave at playback.
            // (The bass is instead anchored to a fixed low octave in playbackNoteForPart.)
            if (!part.percussion && part.name == "pad")
                part.octaveOffset = -1;

            // The default pad is often a slow, washy String Ensemble (prog 48) that
            // fades in late ("delay") and sounds muddy. Give it a faster, tighter
            // synth-strings patch and cut the reverb/chorus wash. (Users can still
            // pick any other instrument for this strip from the mixer.)
            if (!part.percussion && part.name == "pad") {
                part.bankMsb = 0;
                part.bankLsb = 0;
                part.program = 50;   // GM Synth Strings 1 — quicker attack than 48
                part.reverb  = 8;    // tame the long reverb tail
                part.chorus  = 0;    // remove the chorus smear
            }

            for (const auto& n : notes) {
                PatternNote pn;
                pn.tick     = static_cast<int>(n.startTick - startT);
                pn.duration = static_cast<int>(std::max<uint64_t>(1, n.endTick - n.startTick));
                pn.pitch    = n.pitch;
                pn.velocity = n.velocity;
                // Percussion parts are never pitch-shifted by the chord.
                pn.role     = part.percussion ? NoteRole::Absolute
                                              : assignRole(n.channel, n.pitch, part.yamahaPolicy);
                part.notes.push_back(pn);
            }

            // Attach this channel's expression/modulation/sustain/pitch-bend
            // automation, with ticks made relative to the section start. Drums
            // don't bend, but expression/sustain on a kit is harmless, so we
            // keep all captured events as-is.
            if (auto itA = autoByChannel.find(ch); itA != autoByChannel.end()) {
                part.automation.reserve(itA->second.size());
                for (const auto& a : itA->second) {
                    AutomationEvent ev;
                    ev.tick  = static_cast<int>(a.tick - startT);
                    ev.type  = a.type;
                    ev.value = a.value;
                    part.automation.push_back(ev);
                }
            }

            section.parts.push_back(std::move(part));
        }
        style.sections.push_back(std::move(section));
    }

    if (style.sections.empty()) {
        result.ok = false; result.error = "no usable sections produced";
    }
    return result;
}

StyParseResult parseStyFile(const std::string& path, const StyParseOptions& options)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        StyParseResult r;
        r.ok = false; r.error = "cannot open file: " + path; return r;
    }
    in.seekg(0, std::ios::end);
    auto sz = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<std::size_t>(sz));
    in.read(reinterpret_cast<char*>(bytes.data()), sz);
    if (!in) {
        StyParseResult r;
        r.ok = false; r.error = "failed to read file"; return r;
    }
    return parseStyBytes(bytes, options);
}
}
