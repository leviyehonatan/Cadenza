// ChordTypes — the Yamaha arranger chord-type table (the "NTT tables").
//
// One row per recognisable chord type, carrying everything the rest of the
// engine needs to recognise and voice that type:
//   * chordTones / required — recognition: which pitch classes belong to the
//     chord, and which subset must actually be held (Yamaha-style fingering:
//     the 5th is usually optional, the characteristic tones are not).
//   * third / fifth / seventh — where the baked Chord3/Chord5/Chord7 note
//     roles land for this type (the chord-tone NTT mapping).
//   * scaleTones + colorFit — where non-chord (color/phrase) tones snap: the
//     chord scale of the type (e.g. 7(b9) gives phrases the b9, not the 9).
//
// Single source of truth shared by the live recogniser (ArrangerMidiRouter),
// the symbol parser/printer (ChordRecognizer) and the transposer
// (PatternTransposer).

#pragma once

#include "ChordRecognizer.h"

#include <cstdint>
#include <optional>

namespace cadenza::midi
{
// How color/phrase (non-chord-role) tones are fitted for a chord type.
enum class ColorFit : std::uint8_t
{
    Scale,        // snap to scaleTones (the type's chord scale)
    ChordTones,   // snap to chordTones (symmetric chords: dim/aug)
    Passthrough,  // leave as-is (power/single note: no harmonic info)
};

struct ChordTypeInfo
{
    ChordQuality quality;
    const char* suffix;          // canonical symbol suffix ("m7b5", "6", "")
    std::uint16_t chordTones;    // pc bitmask relative to root (bit 0 = root)
    std::uint16_t required;      // pcs that must be held to recognise the type
    std::uint16_t scaleTones;    // chord-scale pc bitmask (ColorFit::Scale)
    ColorFit colorFit;
    std::int8_t third;           // interval for Chord3-role notes
    std::int8_t fifth;           // interval for Chord5-role notes
    std::int8_t seventh;         // interval for Chord7-role notes (if hasSeventh)
    bool hasSeventh;             // false -> Chord7-role notes fold to the root
};

// All chord types in recognition-priority order (earlier rows win ties).
// Size is fixed; iterate with chordTypeCount().
const ChordTypeInfo* chordTypeTable() noexcept;
int chordTypeCount() noexcept;

// Lookup by quality. Always returns a valid row (falls back to Major).
const ChordTypeInfo& chordTypeInfo(ChordQuality quality) noexcept;

// Yamaha-style chord matching: a type matches when every held pitch class
// belongs to it (no foreign notes) and all of its required tones are held.
// Ambiguity (C6 vs Am7) resolves like a real arranger: prefer the reading
// whose root is the bass pitch class, then table order (more specific first).
// `minChordTones` filters dyad/single types in full-fingering modes.
struct ChordMatch
{
    int root = 0;                        // pitch class 0..11
    const ChordTypeInfo* info = nullptr;
};
std::optional<ChordMatch> matchChordMask(std::uint16_t playedMask,
                                         int bassPc,
                                         int minChordTones) noexcept;

constexpr std::uint16_t pcBit(int pc) noexcept
{
    return static_cast<std::uint16_t>(1u << (((pc % 12) + 12) % 12));
}

inline bool maskHas(std::uint16_t mask, int pc) noexcept
{
    return (mask & pcBit(pc)) != 0;
}
}
