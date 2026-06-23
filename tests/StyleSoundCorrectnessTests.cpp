#include "Arranger/PatternTransposer.h"
#include "Arranger/RuntimePlayback.h"
#include "Arranger/StyParser.h"
#include "Arranger/Style.h"

#include <cstdint>
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

using namespace cadenza::arranger;
using cadenza::midi::ChordQuality;

TransposeContext ctxFor(int rootPc, ChordQuality quality, int transpose = 0, int octave = 0)
{
    TransposeContext ctx;
    ctx.chord.rootPitchClass = rootPc;
    ctx.chord.quality = quality;
    ctx.globalTranspose = transpose;
    ctx.globalOctave = octave;
    return ctx;
}

YamahaChannelPolicy policyOf(YamahaPolicySource source,
                             YamahaNtr ntr,
                             YamahaNtt ntt,
                             const std::string& sourceRoot)
{
    YamahaChannelPolicy policy;
    policy.source = source;
    policy.ntr = ntr;
    policy.ntt = ntt;
    policy.sourceRoot = sourceRoot;
    policy.sourceChord = "Maj";
    return policy;
}

void pushU16(std::vector<uint8_t>& b, uint16_t v)
{
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

void pushU32(std::vector<uint8_t>& b, uint32_t v)
{
    b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

void pushTag(std::vector<uint8_t>& b, const char* tag)
{
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<uint8_t>(tag[i]));
}

void pushVLQ(std::vector<uint8_t>& b, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = static_cast<uint8_t>(v & 0x7F);
    v >>= 7;
    while (v) {
        buf[n++] = static_cast<uint8_t>((v & 0x7F) | 0x80);
        v >>= 7;
    }
    for (int i = n - 1; i >= 0; --i)
        b.push_back(buf[i]);
}

void appendBlock(std::vector<uint8_t>& b, const char* tag, const std::vector<uint8_t>& payload)
{
    pushTag(b, tag);
    pushU32(b, static_cast<uint32_t>(payload.size()));
    b.insert(b.end(), payload.begin(), payload.end());
}

void appendTrack(std::vector<uint8_t>& smf, const std::vector<uint8_t>& track)
{
    appendBlock(smf, "MTrk", track);
}

void pushMarker(std::vector<uint8_t>& trk, uint32_t delta, const std::string& text)
{
    pushVLQ(trk, delta);
    trk.push_back(0xFF);
    trk.push_back(0x06);
    pushVLQ(trk, static_cast<uint32_t>(text.size()));
    trk.insert(trk.end(), text.begin(), text.end());
}

void pushNoteOn(std::vector<uint8_t>& trk, uint32_t delta, uint8_t channel, uint8_t pitch)
{
    pushVLQ(trk, delta);
    trk.push_back(static_cast<uint8_t>(0x90 | channel));
    trk.push_back(pitch);
    trk.push_back(100);
}

void pushNoteOff(std::vector<uint8_t>& trk, uint32_t delta, uint8_t channel, uint8_t pitch)
{
    pushVLQ(trk, delta);
    trk.push_back(static_cast<uint8_t>(0x80 | channel));
    trk.push_back(pitch);
    trk.push_back(0);
}

void pushEndOfTrack(std::vector<uint8_t>& trk, uint32_t delta = 0)
{
    pushVLQ(trk, delta);
    trk.push_back(0xFF);
    trk.push_back(0x2F);
    trk.push_back(0);
}

std::vector<uint8_t> makeSourceRootStyle()
{
    std::vector<uint8_t> smf;
    pushTag(smf, "MThd");
    pushU32(smf, 6);
    pushU16(smf, 1);
    pushU16(smf, 2);
    pushU16(smf, 480);

    std::vector<uint8_t> markers;
    pushMarker(markers, 0, "Main A");
    pushEndOfTrack(markers, 960);
    appendTrack(smf, markers);

    std::vector<uint8_t> notes;
    pushNoteOn(notes, 0, 11, 67);
    pushNoteOff(notes, 240, 11, 67);
    pushEndOfTrack(notes);
    appendTrack(smf, notes);

    std::vector<uint8_t> cseg;
    std::vector<uint8_t> sdec = { 'M', 'a', 'i', 'n', ' ', 'A' };
    appendBlock(cseg, "Sdec", sdec);

    const std::string ctabText = "CH=12;NTR=Root Transposition;NTT=Melody;ROOT=G;CHORD=Maj";
    std::vector<uint8_t> ctab(ctabText.begin(), ctabText.end());
    appendBlock(cseg, "Ctab", ctab);

    std::vector<uint8_t> casm;
    appendBlock(casm, "CSEG", cseg);
    appendBlock(smf, "CASM", casm);
    return smf;
}

void percussionPlaybackPitchIsInvariant()
{
    Part drums;
    drums.name = "drums";
    drums.midiChannel = 10;
    drums.percussion = true;
    drums.bankMsb = 120;
    drums.octaveOffset = -2;
    drums.yamahaPolicy = policyOf(YamahaPolicySource::CASM,
                                  YamahaNtr::RootTransposition,
                                  YamahaNtt::Chord,
                                  "G");
    drums.yamahaPolicy->noteLowLimit = 60;
    drums.yamahaPolicy->noteHighLimit = 72;

    PatternNote note;
    note.pitch = 49;
    note.role = NoteRole::ChordRoot;

    const auto c = playbackNoteForPart(drums, note, ctxFor(0, ChordQuality::Major));
    const auto fSharp = playbackNoteForPart(drums, note, ctxFor(6, ChordQuality::Diminished, 7, 2));

    expect(c && *c == 49, "GM drum note passes through unchanged");
    expect(fSharp && *fSharp == 49, "drum playback ignores chord, transpose, limits, windows, and octave offset");

    note.pitch = 90;
    const auto high = drumNoteForPlayback(drums, note.pitch);
    expect(!high.yamahaXg, "GM-compatible drum bank is not treated as XG");
    expect(high.playbackNote == 90, "uncertain GM-compatible drum note passes through unchanged");
}

void casmPolicyWithKnownNtrNttIsHonored()
{
    auto policy = policyOf(YamahaPolicySource::CASM,
                           YamahaNtr::RootTransposition,
                           YamahaNtt::Bypass,
                           "G");

    PatternNote note;
    note.pitch = 67;
    note.role = NoteRole::ChordColor;

    const auto played = transposeNote(note, ctxFor(0, ChordQuality::Major), &policy);
    expect(played && *played == 72, "CASM RootTransposition/Bypass honors source root G");
}

void sourceRootAssignsChordRolesRelativeToPolicyRoot()
{
    const auto result = parseStyBytes(makeSourceRootStyle());
    expect(result.ok, "source-root style parses");
    if (!result.ok) {
        std::cerr << result.error << '\n';
        return;
    }

    const auto* mainA = result.style.findSection("mainA");
    expect(mainA != nullptr, "source-root style has mainA");
    const Part* part = nullptr;
    if (mainA) {
        for (const auto& p : mainA->parts)
            if (p.midiChannel == 12)
                part = &p;
    }

    expect(part != nullptr, "source-root style has channel 12 part");
    if (part) {
        expect(!part->notes.empty(), "source-root part has notes");
        expect(part->notes.front().role == NoteRole::ChordRoot,
               "source root G note is classified as ChordRoot, not C-based Chord5");
    }
}
}

int main()
{
    percussionPlaybackPitchIsInvariant();
    casmPolicyWithKnownNtrNttIsHonored();
    sourceRootAssignsChordRolesRelativeToPolicyRoot();

    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
