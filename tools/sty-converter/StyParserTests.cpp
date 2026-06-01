// StyParser tests — builds a minimal Standard MIDI File in memory,
// feeds it to the parser, and verifies the resulting Style.

#include "Arranger/StyParser.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;
void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

// ---------- SMF byte builders ----------
void pushU8(std::vector<uint8_t>& b, uint8_t v) { b.push_back(v); }
void pushU16(std::vector<uint8_t>& b, uint16_t v) { b.push_back(v >> 8); b.push_back(v & 0xFF); }
void pushU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back((v >> 24) & 0xFF); b.push_back((v >> 16) & 0xFF);
    b.push_back((v >> 8) & 0xFF);  b.push_back(v & 0xFF);
}
void pushTag(std::vector<uint8_t>& b, const char* t) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(t[i])); }
void pushVLQ(std::vector<uint8_t>& b, uint32_t v) {
    uint8_t buf[5]; int n = 0;
    buf[n++] = v & 0x7F;
    v >>= 7;
    while (v) { buf[n++] = (v & 0x7F) | 0x80; v >>= 7; }
    for (int i = n - 1; i >= 0; --i) b.push_back(buf[i]);
}

// Append a Marker meta event with the given text.
void pushMarker(std::vector<uint8_t>& trk, uint32_t delta, const std::string& text)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0xFF); pushU8(trk, 0x06); pushVLQ(trk, static_cast<uint32_t>(text.size()));
    for (char c : text) trk.push_back(static_cast<uint8_t>(c));
}

// Append a tempo meta event (microseconds per quarter-note).
void pushTempo(std::vector<uint8_t>& trk, uint32_t delta, uint32_t micros)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0xFF); pushU8(trk, 0x51); pushVLQ(trk, 3);
    pushU8(trk, (micros >> 16) & 0xFF);
    pushU8(trk, (micros >> 8) & 0xFF);
    pushU8(trk, micros & 0xFF);
}

// Append a Note On.
void pushNoteOn(std::vector<uint8_t>& trk, uint32_t delta, uint8_t channel, uint8_t pitch, uint8_t vel)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0x90 | (channel & 0x0F));
    pushU8(trk, pitch);
    pushU8(trk, vel);
}
void pushNoteOff(std::vector<uint8_t>& trk, uint32_t delta, uint8_t channel, uint8_t pitch)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0x80 | (channel & 0x0F));
    pushU8(trk, pitch);
    pushU8(trk, 64);
}
void pushProgramChange(std::vector<uint8_t>& trk, uint32_t delta, uint8_t channel, uint8_t program)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0xC0 | (channel & 0x0F));
    pushU8(trk, program);
}
void pushEndOfTrack(std::vector<uint8_t>& trk, uint32_t delta = 0)
{
    pushVLQ(trk, delta);
    pushU8(trk, 0xFF); pushU8(trk, 0x2F); pushVLQ(trk, 0);
}

// Wrap a track body with MTrk header + length.
void appendTrack(std::vector<uint8_t>& smf, const std::vector<uint8_t>& trk)
{
    pushTag(smf, "MTrk");
    pushU32(smf, static_cast<uint32_t>(trk.size()));
    for (auto b : trk) smf.push_back(b);
}

void appendChunk(std::vector<uint8_t>& out, const char* tag, const std::vector<uint8_t>& payload)
{
    pushTag(out, tag);
    pushU32(out, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

std::vector<uint8_t> makeSampleSmf()
{
    // SMF format 1, 2 tracks, 480 ticks/quarter
    std::vector<uint8_t> smf;
    pushTag(smf, "MThd"); pushU32(smf, 6);
    pushU16(smf, 1);      // format
    pushU16(smf, 2);      // ntracks
    pushU16(smf, 480);    // ticks per quarter

    // Track 0: tempo + section markers
    std::vector<uint8_t> t0;
    pushTempo(t0, 0, 500000);                 // 120 BPM
    pushMarker(t0, 0, "Main A");              // tick 0
    pushMarker(t0, 1920, "Main B");           // 1 bar later
    pushMarker(t0, 1920, "Ending A");         // another bar later
    pushEndOfTrack(t0, 1920);
    appendTrack(smf, t0);

    // Track 1: 4 notes on channel 2 (bass-ish), 4 drum hits on channel 10
    std::vector<uint8_t> t1;
    pushProgramChange(t1, 0, 1, 32);          // Acoustic Bass on channel 2 (zero-based 1)
    // Notes in mainA: C(60), E(64), G(67), Bb(70), 1 quarter apart each
    pushNoteOn (t1, 0,    1, 60, 100);
    pushNoteOff(t1, 480,  1, 60);
    pushNoteOn (t1, 0,    1, 64, 100);
    pushNoteOff(t1, 480,  1, 64);
    pushNoteOn (t1, 0,    1, 67, 100);
    pushNoteOff(t1, 480,  1, 67);
    pushNoteOn (t1, 0,    1, 70, 100);
    pushNoteOff(t1, 480,  1, 70);
    // mainB starts at tick 1920. One drum hit on channel 10 (zero-based 9):
    pushNoteOn (t1, 0,    9, 36, 110);
    pushNoteOff(t1, 240,  9, 36);
    // Ending A at tick 3840:
    pushNoteOn (t1, 1680, 1, 60, 100);
    pushNoteOff(t1, 480,  1, 60);
    pushEndOfTrack(t1, 0);
    appendTrack(smf, t1);

    return smf;
}

std::vector<uint8_t> makeSingleSectionSmf(uint8_t channel)
{
    std::vector<uint8_t> smf;
    pushTag(smf, "MThd"); pushU32(smf, 6);
    pushU16(smf, 1);
    pushU16(smf, 2);
    pushU16(smf, 480);

    std::vector<uint8_t> t0;
    pushTempo(t0, 0, 500000);
    pushMarker(t0, 0, "Main A");
    pushEndOfTrack(t0, 1920);
    appendTrack(smf, t0);

    std::vector<uint8_t> t1;
    pushProgramChange(t1, 0, channel, channel == 9 ? 0 : 48);
    pushNoteOn (t1, 0,   channel, channel == 9 ? 36 : 60, 100);
    pushNoteOff(t1, 480, channel, channel == 9 ? 36 : 60);
    pushNoteOn (t1, 0,   channel, 64, 100);
    pushNoteOff(t1, 480, channel, 64);
    pushNoteOn (t1, 0,   channel, 67, 100);
    pushNoteOff(t1, 480, channel, 67);
    pushNoteOn (t1, 0,   channel, 70, 100);
    pushNoteOff(t1, 480, channel, 70);
    pushEndOfTrack(t1, 0);
    appendTrack(smf, t1);

    return smf;
}

std::vector<uint8_t> makeSampleStyWithCasm()
{
    auto sty = makeSampleSmf();

    std::vector<uint8_t> sdec;
    const std::string section = "Main A";
    sdec.insert(sdec.end(), section.begin(), section.end());

    std::vector<uint8_t> ctab;
    const std::string fields = "CH=2;NTR=RootTrans;NTT=Chord;ROOT=C;CHORD=Maj;UNKNOWN=keep";
    ctab.insert(ctab.end(), fields.begin(), fields.end());

    std::vector<uint8_t> cseg;
    appendChunk(cseg, "Sdec", sdec);
    appendChunk(cseg, "Ctab", ctab);

    std::vector<uint8_t> casm;
    appendChunk(casm, "CSEG", cseg);
    appendChunk(sty, "CASM", casm);
    return sty;
}

std::vector<uint8_t> makeSampleStyWithBinaryCtab()
{
    auto sty = makeSampleSmf();

    std::vector<uint8_t> sdec;
    const std::string section = "Main A";
    sdec.insert(sdec.end(), section.begin(), section.end());

    std::vector<uint8_t> ctab(26, 0);
    ctab[0] = 1; // source channel 2, zero-based in Yamaha Ctab
    const std::string name = "Bass";
    for (std::size_t i = 0; i < 8; ++i)
        ctab[1 + i] = i < name.size() ? static_cast<uint8_t>(name[i]) : static_cast<uint8_t>(' ');
    ctab[9] = 10;  // destination channel 11 / bass, not decoded by this step
    ctab[10] = 0;  // editable, not decoded by this step
    ctab[18] = 7;  // source root G
    ctab[19] = 10; // source chord min7
    ctab[20] = 1;  // NTR Root Fixed
    ctab[21] = 3;  // NTT Bass
    ctab[22] = 11; // high key, not decoded by this step
    ctab[23] = 24; // low note, not decoded by this step
    ctab[24] = 84; // high note, not decoded by this step
    ctab[25] = 2;  // RTR, not decoded by this step

    std::vector<uint8_t> cseg;
    appendChunk(cseg, "Sdec", sdec);
    appendChunk(cseg, "Ctab", ctab);

    std::vector<uint8_t> casm;
    appendChunk(casm, "CSEG", cseg);
    appendChunk(sty, "CASM", casm);
    return sty;
}

std::vector<uint8_t> makeSampleStyWithBinaryCtb2()
{
    auto sty = makeSampleSmf();

    std::vector<uint8_t> sdec;
    const std::string section = "Main A";
    sdec.insert(sdec.end(), section.begin(), section.end());

    std::vector<uint8_t> ctb2(47, 0);
    ctb2[0] = 4; // source channel 5, zero-based in Yamaha Ctb2
    const std::string name = "StrRoot";
    for (std::size_t i = 0; i < 8; ++i)
        ctb2[1 + i] = i < name.size() ? static_cast<uint8_t>(name[i]) : static_cast<uint8_t>(' ');
    ctb2[9] = 12;  // destination channel, not decoded by this step
    ctb2[18] = 0;  // source root C
    ctb2[19] = 19; // source chord 7th
    ctb2[20] = 0;  // no low split, middle substructure covers from bottom
    ctb2[21] = 0x7F; // no high split, middle substructure covers through top
    ctb2[28] = 0;  // middle NTR Root Transposition
    ctb2[29] = 0x81; // middle NTT Melody with bass-on bit set
    ctb2[30] = 8;  // high key, not decoded by this step
    ctb2[31] = 28; // low note, not decoded by this step
    ctb2[32] = 127; // high note, not decoded by this step
    ctb2[33] = 2;  // RTR, not decoded by this step

    std::vector<uint8_t> cseg;
    appendChunk(cseg, "Sdec", sdec);
    appendChunk(cseg, "Ctb2", ctb2);

    std::vector<uint8_t> casm;
    appendChunk(casm, "CSEG", cseg);
    appendChunk(sty, "CASM", casm);
    return sty;
}

std::vector<uint8_t> makeSingleSectionStyWithCtb2(uint8_t channel,
                                                  uint8_t ntr,
                                                  uint8_t ntt,
                                                  uint8_t sourceRoot = 0,
                                                  uint8_t sourceChord = 19,
                                                  uint8_t rtr = 1,
                                                  uint8_t chordRootUpperLimit = 11,
                                                  uint8_t noteLowLimit = 24,
                                                  uint8_t noteHighLimit = 108)
{
    auto sty = makeSingleSectionSmf(channel);

    std::vector<uint8_t> sdec;
    const std::string section = "Main A";
    sdec.insert(sdec.end(), section.begin(), section.end());

    std::vector<uint8_t> ctb2(47, 0);
    ctb2[0] = channel;
    const std::string name = "Policy";
    for (std::size_t i = 0; i < 8; ++i)
        ctb2[1 + i] = i < name.size() ? static_cast<uint8_t>(name[i]) : static_cast<uint8_t>(' ');
    ctb2[9] = channel;
    ctb2[18] = sourceRoot;
    ctb2[19] = sourceChord;
    ctb2[20] = 0;
    ctb2[21] = 0x7F;
    ctb2[28] = ntr;
    ctb2[29] = ntt;
    ctb2[30] = chordRootUpperLimit;
    ctb2[31] = noteLowLimit;
    ctb2[32] = noteHighLimit;
    ctb2[33] = rtr;

    std::vector<uint8_t> cseg;
    appendChunk(cseg, "Sdec", sdec);
    appendChunk(cseg, "Ctb2", ctb2);

    std::vector<uint8_t> casm;
    appendChunk(casm, "CSEG", cseg);
    appendChunk(sty, "CASM", casm);
    return sty;
}

void parsesHeaderAndProducesStyle()
{
    auto smf = makeSampleSmf();
    auto r = cadenza::arranger::parseStyBytes(smf);
    expect(r.ok, "parses OK");
    if (!r.ok) return;
    expect(r.style.ticksPerBeat == 480, "ppq 480");
    expect(r.style.defaultTempo == 120, "tempo 120");
    expect(!r.casm.found, "plain SMF has no CASM");
}

void sectionsAreSplitByMarkers()
{
    auto smf = makeSampleSmf();
    auto r = cadenza::arranger::parseStyBytes(smf);
    expect(r.ok, "parses OK");
    if (!r.ok) return;

    expect(r.style.sections.size() == 3, "3 sections");
    bool hasMainA = false, hasMainB = false, hasEnding = false;
    for (const auto& s : r.style.sections) {
        if (s.name == "mainA")  hasMainA  = true;
        if (s.name == "mainB")  hasMainB  = true;
        if (s.name == "ending") hasEnding = true;
    }
    expect(hasMainA && hasMainB && hasEnding, "mainA, mainB, ending all present");
}

void mainARolesAreAssignedHeuristically()
{
    auto smf = makeSampleSmf();
    auto r = cadenza::arranger::parseStyBytes(smf);
    expect(r.ok, "parses OK");
    if (!r.ok) return;

    const cadenza::arranger::Section* mainA = nullptr;
    for (const auto& s : r.style.sections)
        if (s.name == "mainA") mainA = &s;
    expect(mainA != nullptr, "mainA section found");
    if (!mainA) return;

    // We expect one part on channel 2 (bass-like) with notes 60/64/67/70.
    const cadenza::arranger::Part* bass = nullptr;
    for (const auto& p : mainA->parts) if (p.midiChannel == 2) bass = &p;
    expect(bass != nullptr, "bass part found");
    if (!bass) return;

    expect(bass->notes.size() == 4, "4 notes in main A bass part");
    using cadenza::arranger::NoteRole;
    expect(bass->notes[0].role == NoteRole::ChordRoot, "C -> chord-root");
    expect(bass->notes[1].role == NoteRole::Chord3,    "E -> chord-3");
    expect(bass->notes[2].role == NoteRole::Chord5,    "G -> chord-5");
    expect(bass->notes[3].role == NoteRole::Chord7,    "Bb -> chord-7");
    expect(bass->instrument == "Acoustic Bass",         "GM 32 -> Acoustic Bass");
}

void drumsAreAbsolute()
{
    auto smf = makeSampleSmf();
    auto r = cadenza::arranger::parseStyBytes(smf);
    expect(r.ok, "parses OK");
    if (!r.ok) return;

    const cadenza::arranger::Section* mainB = nullptr;
    for (const auto& s : r.style.sections)
        if (s.name == "mainB") mainB = &s;
    expect(mainB != nullptr, "mainB section found");
    if (!mainB) return;

    const cadenza::arranger::Part* drums = nullptr;
    for (const auto& p : mainB->parts) if (p.midiChannel == 10) drums = &p;
    expect(drums != nullptr, "drum part on channel 10");
    if (!drums) return;
    expect(drums->name == "drums", "named drums");
    expect(drums->notes.size() == 1, "1 drum note in mainB");
    using cadenza::arranger::NoteRole;
    expect(drums->notes[0].role == NoteRole::Absolute, "drum note is absolute");
}

void markerMappingIsCaseInsensitive()
{
    using namespace cadenza::arranger;
    expect(mapSectionMarker("Main A").value() == "mainA",     "Main A");
    expect(mapSectionMarker("main a").value() == "mainA",     "main a");
    expect(mapSectionMarker("MAIN_A").value() == "mainA",     "MAIN_A");
    expect(mapSectionMarker("Intro B").value() == "introB",   "Intro B -> introB");
    expect(mapSectionMarker("Fill In AA").value() == "fillAA","Fill In AA");
    expect(!mapSectionMarker("Random text").has_value(),      "non-marker rejected");
}

void casmChunkIsDetectedAndParsed()
{
    auto sty = makeSampleStyWithCasm();
    cadenza::arranger::StyParseOptions options;
    options.verbose = true;
    auto r = cadenza::arranger::parseStyBytes(sty, options);
    expect(r.ok, "CASM sty parses OK");
    if (!r.ok) return;

    expect(r.casm.found, "CASM found");
    expect(r.casm.csegs.size() == 1, "one CSEG parsed");
    expect(r.casm.ctabEntryCount == 1, "one Ctab entry parsed");
    if (!r.casm.csegs.empty()) {
        const auto& cseg = r.casm.csegs[0];
        expect(cseg.sectionName && *cseg.sectionName == "Main A", "Sdec section name parsed");
        expect(cseg.ctabEntries.size() == 1, "CSEG has one Ctab entry");
        if (!cseg.ctabEntries.empty()) {
            const auto& entry = cseg.ctabEntries[0];
            expect(entry.channel && *entry.channel == 2, "Ctab channel parsed");
            expect(entry.ntr && *entry.ntr == "RootTrans", "Ctab NTR parsed");
            expect(entry.ntt && *entry.ntt == "Chord", "Ctab NTT parsed");
            expect(entry.sourceRoot && *entry.sourceRoot == "C", "Ctab source root parsed");
            expect(entry.sourceChord && *entry.sourceChord == "Maj", "Ctab source chord parsed");
            expect(!entry.unknownFields.empty(), "unknown Ctab fields retained");
            expect(!entry.raw.empty(), "raw Ctab data retained");
        }
    }
}

void binaryCtabDecodesKnownFields()
{
    auto sty = makeSampleStyWithBinaryCtab();
    cadenza::arranger::StyParseOptions options;
    options.verbose = true;
    auto r = cadenza::arranger::parseStyBytes(sty, options);
    expect(r.ok, "binary CASM sty parses OK");
    if (!r.ok) return;

    expect(r.casm.found, "binary CASM found");
    expect(r.casm.ctabEntryCount == 1, "binary Ctab entry counted");
    expect(!r.casm.csegs.empty(), "binary CSEG exists");
    if (r.casm.csegs.empty()) return;
    expect(!r.casm.csegs[0].ctabEntries.empty(), "binary Ctab entry exists");
    if (r.casm.csegs[0].ctabEntries.empty()) return;

    const auto& entry = r.casm.csegs[0].ctabEntries[0];
    expect(entry.channel && *entry.channel == 2, "binary source channel decoded");
    expect(entry.channelRaw && *entry.channelRaw == 1, "binary source channel raw retained");
    expect(entry.sourceRoot && *entry.sourceRoot == "G", "binary source root decoded");
    expect(entry.sourceRootRaw && *entry.sourceRootRaw == 7, "binary source root raw retained");
    expect(entry.sourceChord && *entry.sourceChord == "min7", "binary source chord decoded");
    expect(entry.sourceChordRaw && *entry.sourceChordRaw == 10, "binary source chord raw retained");
    expect(entry.ntr && *entry.ntr == "Root Fixed", "binary NTR decoded");
    expect(entry.ntrRaw && *entry.ntrRaw == 1, "binary NTR raw retained");
    expect(entry.ntt && *entry.ntt == "Bass", "binary NTT decoded");
    expect(entry.nttRaw && *entry.nttRaw == 3, "binary NTT raw retained");
    expect(entry.raw.size() == 26, "binary Ctab raw payload retained");

    bool hasHexDump = false;
    for (const auto& line : r.casm.logLines)
        if (line.find("CASM Ctab hex:") != std::string::npos)
            hasHexDump = true;
    expect(hasHexDump, "verbose binary Ctab hex dump logged");
}

void sff1CtabNttByte3MapsToMelodyBassOn()
{
    auto sty = makeSampleStyWithBinaryCtab();
    auto r = cadenza::arranger::parseStyBytes(sty);
    expect(r.ok, "SFF1 Ctab NTT=3 sty parses OK");
    if (!r.ok) return;

    expect(!r.casm.csegs.empty(), "SFF1 Ctab CSEG exists");
    if (r.casm.csegs.empty()) return;
    expect(!r.casm.csegs[0].ctabEntries.empty(), "SFF1 Ctab entry exists");
    if (r.casm.csegs[0].ctabEntries.empty()) return;

    const auto& entry = r.casm.csegs[0].ctabEntries[0];
    expect(entry.policy.has_value(), "SFF1 Ctab policy attached");
    if (!entry.policy) return;

    using cadenza::arranger::YamahaNtt;
    using cadenza::arranger::YamahaPolicySource;
    const auto& policy = *entry.policy;
    expect(policy.source == YamahaPolicySource::Ctab, "SFF1 Ctab policy source");
    expect(policy.rawNtt && *policy.rawNtt == 3, "SFF1 Ctab raw NTT byte 3 retained");
    expect(policy.ntt == YamahaNtt::Melody, "SFF1 Ctab NTT byte 3 maps to Melody");
    expect(policy.bassOn, "SFF1 Ctab NTT byte 3 sets bassOn");
}

void binaryCtb2DecodesKnownFields()
{
    auto sty = makeSampleStyWithBinaryCtb2();
    cadenza::arranger::StyParseOptions options;
    options.verbose = true;
    auto r = cadenza::arranger::parseStyBytes(sty, options);
    expect(r.ok, "binary Ctb2 sty parses OK");
    if (!r.ok) return;

    expect(r.casm.found, "binary Ctb2 CASM found");
    expect(r.casm.ctabEntryCount == 1, "binary Ctb2 entry counted");
    expect(!r.casm.csegs.empty(), "binary Ctb2 CSEG exists");
    if (r.casm.csegs.empty()) return;
    expect(!r.casm.csegs[0].ctabEntries.empty(), "binary Ctb2 entry exists");
    if (r.casm.csegs[0].ctabEntries.empty()) return;

    const auto& entry = r.casm.csegs[0].ctabEntries[0];
    expect(entry.channel && *entry.channel == 5, "Ctb2 source channel decoded");
    expect(entry.channelRaw && *entry.channelRaw == 4, "Ctb2 source channel raw retained");
    expect(entry.sourceRoot && *entry.sourceRoot == "C", "Ctb2 source root decoded");
    expect(entry.sourceChord && *entry.sourceChord == "7th", "Ctb2 source chord decoded");
    expect(entry.ntr && *entry.ntr == "Root Transposition", "Ctb2 NTR decoded from middle substructure");
    expect(entry.ntrRaw && *entry.ntrRaw == 0, "Ctb2 NTR raw retained");
    expect(entry.ntt && *entry.ntt == "Melody (Bass On)", "Ctb2 NTT decoded from middle substructure");
    expect(entry.nttRaw && *entry.nttRaw == 0x81, "Ctb2 NTT raw retained");
    expect(entry.raw.size() == 47, "Ctb2 raw payload retained");

    bool hasHexDump = false;
    for (const auto& line : r.casm.logLines)
        if (line.find("CASM Ctb2 hex:") != std::string::npos)
            hasHexDump = true;
    expect(hasHexDump, "verbose Ctb2 hex dump logged");
}

void ctb2PolicyIsDecodedAndAttachedToPart()
{
    auto sty = makeSingleSectionStyWithCtb2(4, 0, 0x81);
    auto r = cadenza::arranger::parseStyBytes(sty);
    expect(r.ok, "policy Ctb2 sty parses OK");
    if (!r.ok) return;

    const auto* mainA = r.style.findSection("mainA");
    expect(mainA != nullptr, "policy mainA found");
    if (!mainA) return;

    const cadenza::arranger::Part* part = nullptr;
    for (const auto& p : mainA->parts)
        if (p.midiChannel == 5)
            part = &p;
    expect(part != nullptr, "policy part found");
    if (!part) return;

    expect(part->yamahaPolicy.has_value(), "Yamaha policy attached");
    if (!part->yamahaPolicy) return;

    using cadenza::arranger::YamahaNtr;
    using cadenza::arranger::YamahaNtt;
    using cadenza::arranger::YamahaPolicySource;
    using cadenza::arranger::YamahaRetriggerRule;
    const auto& policy = *part->yamahaPolicy;
    expect(policy.source == YamahaPolicySource::Ctb2, "policy source is Ctb2");
    expect(policy.sourceChannel == 5, "policy source channel");
    expect(policy.ntr == YamahaNtr::RootTransposition, "policy NTR decoded");
    expect(policy.ntt == YamahaNtt::Melody, "policy NTT decoded");
    expect(policy.bassOn, "policy bass-on decoded");
    expect(policy.rawNtr && *policy.rawNtr == 0, "policy raw NTR retained");
    expect(policy.rawNtt && *policy.rawNtt == 0x81, "policy raw NTT retained");
    expect(policy.rawRtr && *policy.rawRtr == 1, "policy raw RTR retained");
    expect(policy.retriggerRule == YamahaRetriggerRule::PitchShift, "policy RTR decoded");
    expect(policy.chordRootUpperLimit && *policy.chordRootUpperLimit == 11, "policy chord root upper limit decoded");
    expect(policy.noteLowLimit && *policy.noteLowLimit == 24, "policy note low limit decoded");
    expect(policy.noteHighLimit && *policy.noteHighLimit == 108, "policy note high limit decoded");
}

void ctb2PolicyPreservesPlaybackLimits()
{
    auto sty = makeSingleSectionStyWithCtb2(
        4,
        0,
        0x81,
        0,
        19,
        1,
        3,
        52,
        76);
    auto r = cadenza::arranger::parseStyBytes(sty);
    expect(r.ok, "limit policy Ctb2 sty parses OK");
    if (!r.ok) return;

    const auto* mainA = r.style.findSection("mainA");
    expect(mainA != nullptr, "limit policy mainA found");
    if (!mainA) return;

    const cadenza::arranger::Part* part = nullptr;
    for (const auto& p : mainA->parts)
        if (p.midiChannel == 5)
            part = &p;
    expect(part != nullptr, "limit policy part found");
    if (!part) return;

    expect(part->yamahaPolicy.has_value(), "limit policy attached");
    if (!part->yamahaPolicy) return;

    const auto& policy = *part->yamahaPolicy;
    expect(policy.chordRootUpperLimit && *policy.chordRootUpperLimit == 3,
           "custom chord root upper limit decoded");
    expect(policy.noteLowLimit && *policy.noteLowLimit == 52,
           "custom note low limit decoded");
    expect(policy.noteHighLimit && *policy.noteHighLimit == 76,
           "custom note high limit decoded");
}

void bypassPolicyFollowsByRootTransposition()
{
    // BYPASS = "root shift only" -> melodic notes follow the chord root (ChordColor),
    // they do NOT freeze. (Channel 2 here is a melodic part, not drums.)
    auto sty = makeSingleSectionStyWithCtb2(1, 1, 0);
    auto r = cadenza::arranger::parseStyBytes(sty);
    expect(r.ok, "bypass policy sty parses OK");
    if (!r.ok) return;

    const auto* mainA = r.style.findSection("mainA");
    expect(mainA != nullptr, "bypass mainA found");
    if (!mainA) return;

    const cadenza::arranger::Part* part = nullptr;
    for (const auto& p : mainA->parts)
        if (p.midiChannel == 2)
            part = &p;
    expect(part != nullptr, "bypass part found");
    if (!part) return;

    for (const auto& note : part->notes)
        expect(note.role == cadenza::arranger::NoteRole::ChordColor, "bypass note follows chord (ChordColor)");
}

void rootFixedChordPolicyAssignsChordRoles()
{
    auto sty = makeSingleSectionStyWithCtb2(1, 1, 2);
    auto r = cadenza::arranger::parseStyBytes(sty);
    expect(r.ok, "RootFixed+Chord policy sty parses OK");
    if (!r.ok) return;

    const auto* mainA = r.style.findSection("mainA");
    expect(mainA != nullptr, "RootFixed+Chord mainA found");
    if (!mainA) return;

    const cadenza::arranger::Part* part = nullptr;
    for (const auto& p : mainA->parts)
        if (p.midiChannel == 2)
            part = &p;
    expect(part != nullptr, "RootFixed+Chord part found");
    if (!part) return;

    using cadenza::arranger::NoteRole;
    expect(part->notes[0].role == NoteRole::ChordRoot, "RootFixed+Chord C -> root");
    expect(part->notes[1].role == NoteRole::Chord3, "RootFixed+Chord E -> third");
    expect(part->notes[2].role == NoteRole::Chord5, "RootFixed+Chord G -> fifth");
    expect(part->notes[3].role == NoteRole::Chord7, "RootFixed+Chord Bb -> seventh");
}

void bassOnPolicyMarksPartAsBassAndRootFollowing()
{
    auto sty = makeSingleSectionStyWithCtb2(4, 0, 0x81);
    auto r = cadenza::arranger::parseStyBytes(sty);
    expect(r.ok, "bass-on policy sty parses OK");
    if (!r.ok) return;

    const auto* mainA = r.style.findSection("mainA");
    expect(mainA != nullptr, "bass-on mainA found");
    if (!mainA) return;

    const cadenza::arranger::Part* part = nullptr;
    for (const auto& p : mainA->parts)
        if (p.midiChannel == 5)
            part = &p;
    expect(part != nullptr, "bass-on part found");
    if (!part) return;

    expect(part->name == "bass", "bass-on part named bass");
    expect(part->yamahaPolicy && part->yamahaPolicy->bassOn, "bass-on policy retained");
    expect(part->notes[0].role == cadenza::arranger::NoteRole::ChordRoot, "bass-on C follows chord root");
}

void unknownPolicyFallsBackToHeuristic()
{
    auto sty = makeSingleSectionStyWithCtb2(1, 99, 99);
    auto r = cadenza::arranger::parseStyBytes(sty);
    expect(r.ok, "unknown policy sty parses OK");
    if (!r.ok) return;

    const auto* mainA = r.style.findSection("mainA");
    expect(mainA != nullptr, "unknown policy mainA found");
    if (!mainA) return;

    const cadenza::arranger::Part* part = nullptr;
    for (const auto& p : mainA->parts)
        if (p.midiChannel == 2)
            part = &p;
    expect(part != nullptr, "unknown policy part found");
    if (!part) return;

    using cadenza::arranger::NoteRole;
    using cadenza::arranger::YamahaNtr;
    using cadenza::arranger::YamahaNtt;
    expect(part->yamahaPolicy && part->yamahaPolicy->ntr == YamahaNtr::Unknown, "unknown NTR retained as Unknown");
    expect(part->yamahaPolicy && part->yamahaPolicy->ntt == YamahaNtt::Unknown, "unknown NTT retained as Unknown");
    expect(part->notes[0].role == NoteRole::ChordRoot, "unknown C falls back to root heuristic");
    expect(part->notes[1].role == NoteRole::Chord3, "unknown E falls back to third heuristic");
}

void drumsRemainAbsoluteEvenWithPolicy()
{
    auto sty = makeSingleSectionStyWithCtb2(9, 1, 2);
    auto r = cadenza::arranger::parseStyBytes(sty);
    expect(r.ok, "drum policy sty parses OK");
    if (!r.ok) return;

    const auto* mainA = r.style.findSection("mainA");
    expect(mainA != nullptr, "drum policy mainA found");
    if (!mainA) return;

    const cadenza::arranger::Part* part = nullptr;
    for (const auto& p : mainA->parts)
        if (p.midiChannel == 10)
            part = &p;
    expect(part != nullptr, "drum policy part found");
    if (!part) return;

    for (const auto& note : part->notes)
        expect(note.role == cadenza::arranger::NoteRole::Absolute, "drum policy note stays absolute");
}

void malformedCasmDoesNotCrash()
{
    auto sty = makeSampleSmf();
    pushTag(sty, "CASM");
    pushU32(sty, 1000); // deliberately longer than remaining bytes
    sty.push_back(0x01);
    sty.push_back(0x02);

    auto r = cadenza::arranger::parseStyBytes(sty);
    expect(r.ok, "malformed CASM does not fail style parse");
    expect(r.casm.found, "malformed CASM still detected");
    expect(!r.casm.warnings.empty(), "malformed CASM warning recorded");
}

bool hasStyleWarning(const cadenza::arranger::Style& style, const std::string& text)
{
    for (const auto& warning : style.parseWarnings)
        if (warning.find(text) != std::string::npos)
            return true;
    return false;
}

void missingCasmReportsParseWarning()
{
    auto smf = makeSampleSmf();
    auto r = cadenza::arranger::parseStyBytes(smf);
    expect(r.ok, "missing CASM style parses OK");
    if (!r.ok) return;

    expect(hasStyleWarning(r.style, "missing CASM policy"), "missing CASM warning stored on style");
    expect(hasStyleWarning(r.style, "channel 2 missing NTR/NTT"), "fallback channel warning stored on style");
}

void completePolicyDoesNotReportParseWarnings()
{
    auto sty = makeSingleSectionStyWithCtb2(11, 0, 2);
    auto r = cadenza::arranger::parseStyBytes(sty);
    expect(r.ok, "complete policy style parses OK");
    if (!r.ok) return;

    expect(r.style.parseWarnings.empty(), "complete CASM policy has no parse warnings");
}

void unmappedPolicyChannelReportsWarning()
{
    auto sty = makeSingleSectionStyWithCtb2(6, 0, 2);
    auto r = cadenza::arranger::parseStyBytes(sty);
    expect(r.ok, "unmapped channel policy style parses OK");
    if (!r.ok) return;

    expect(hasStyleWarning(r.style, "destination role unknown"), "unmapped destination role warning stored on style");
}
}

int main()
{
    markerMappingIsCaseInsensitive();
    parsesHeaderAndProducesStyle();
    sectionsAreSplitByMarkers();
    mainARolesAreAssignedHeuristically();
    drumsAreAbsolute();
    casmChunkIsDetectedAndParsed();
    binaryCtabDecodesKnownFields();
    sff1CtabNttByte3MapsToMelodyBassOn();
    binaryCtb2DecodesKnownFields();
    ctb2PolicyIsDecodedAndAttachedToPart();
    ctb2PolicyPreservesPlaybackLimits();
    bypassPolicyFollowsByRootTransposition();
    rootFixedChordPolicyAssignsChordRoles();
    bassOnPolicyMarksPartAsBassAndRootFollowing();
    unknownPolicyFallsBackToHeuristic();
    drumsRemainAbsoluteEvenWithPolicy();
    malformedCasmDoesNotCrash();
    missingCasmReportsParseWarning();
    completePolicyDoesNotReportParseWarnings();
    unmappedPolicyChannelReportsWarning();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All StyParser tests passed\n";
    return EXIT_SUCCESS;
}
