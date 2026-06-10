// StyParser — read a Yamaha .sty file (Standard MIDI File + Yamaha chunks)
// and produce a cadenza::arranger::Style.
//
// This is a first-pass converter. It parses the SMF portion (MThd + MTrk
// chunks), identifies section boundaries from text meta-events ("Main A",
// "Main B", "Intro A", "Fill In AA", "Ending A", etc.), and assigns chord
// roles using a heuristic: source chord is assumed to be C major (the
// convention for Yamaha styles), so notes on pitch classes 0/4/7/11 become
// chord-root/3/5/7, channel 10 (drums) is always "absolute", everything
// else falls back to "absolute".
//
// The CASM/CSEG Yamaha-specific chunks after the SMF data are detected and
// walked conservatively. Parsed CASM metadata is exposed for diagnostics only;
// playback role assignment still uses the C-major heuristic.

#pragma once

#include "Style.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cadenza::arranger
{
struct CasmUnknownField
{
    std::string name;
    std::string value;
};

struct CasmCtabEntry
{
    std::optional<std::string> sectionName;
    std::optional<int> channel;          // 1..16 when known
    std::optional<std::string> ntr;      // raw value/name when known
    std::optional<std::string> ntt;      // raw value/name when known
    std::optional<std::string> sourceRoot;
    std::optional<std::string> sourceChord;
    std::optional<uint8_t> channelRaw;
    std::optional<uint8_t> ntrRaw;
    std::optional<uint8_t> nttRaw;
    std::optional<uint8_t> sourceRootRaw;
    std::optional<uint8_t> sourceChordRaw;
    std::optional<YamahaChannelPolicy> policy;
    std::vector<uint8_t> raw;
    std::vector<CasmUnknownField> unknownFields;
};

struct CasmCseg
{
    std::optional<std::string> sectionName;
    std::string sectionId;
    std::vector<uint8_t> raw;
    std::vector<uint8_t> sdecRaw;
    std::vector<CasmCtabEntry> ctabEntries;
    std::vector<std::string> childBlockTags;
};

struct CasmInfo
{
    bool found = false;
    std::size_t offset = 0;
    uint32_t declaredSize = 0;
    std::size_t parsedSize = 0;
    std::size_t ctabEntryCount = 0;
    std::vector<CasmCseg> csegs;
    std::vector<std::string> topLevelBlockTags;
    std::vector<std::string> warnings;
    std::vector<std::string> logLines;
};

// MIDI channels (0-based) that Yamaha OTS setup tracks use to address the
// panel voices. PROVISIONAL — verified and corrected against the user's real
// Genos preset styles in the verification task of
// docs/superpowers/plans/2026-06-10-one-touch-settings.md. If real files
// warn about "unexpected MIDI channel", fix THESE constants, nothing else.
inline constexpr int kOtsChannelLeft   = 8;
inline constexpr int kOtsChannelRight1 = 9;
inline constexpr int kOtsChannelRight2 = 10;
inline constexpr int kOtsChannelRight3 = 11;

struct StyParseResult
{
    bool ok = true;
    std::string error;
    Style style;
    CasmInfo casm;
};

struct StyParseOptions
{
    std::string overrideId;       // if set, becomes Style::id
    std::string overrideName;     // if set, becomes Style::name
    int  defaultTempo = 120;      // used if SMF has no tempo meta-event
    bool verbose = false;
};

// Parse bytes. Source is the entire .sty (or .mid) file as a byte buffer.
StyParseResult parseStyBytes(const std::vector<uint8_t>& bytes,
                             const StyParseOptions& options = {});

// Convenience: read from a file path.
StyParseResult parseStyFile(const std::string& path,
                            const StyParseOptions& options = {});

// Mapping from a Yamaha section marker text to a Cadenza section id.
// Returns nullopt if the marker is not a recognised section heading.
//   "Main A"        -> "mainA"
//   "Main B"        -> "mainB"
//   "Main C"        -> "mainC"
//   "Main D"        -> "mainD"
//   "Intro A"       -> "intro"  (Intro B/C also map to "intro" for v1)
//   "Ending A"      -> "ending"
//   "Fill In AA"    -> "fillAA"
//   "Fill In BB"    -> "fillBB"
//   ...
// Case-insensitive.
std::optional<std::string> mapSectionMarker(const std::string& marker);
}
