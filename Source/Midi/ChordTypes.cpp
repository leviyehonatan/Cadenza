#include "ChordTypes.h"

#include <bit>

namespace cadenza::midi
{
namespace
{
constexpr std::uint16_t mask(std::initializer_list<int> pcs) noexcept
{
    std::uint16_t m = 0;
    for (int pc : pcs)
        m = static_cast<std::uint16_t>(m | pcBit(pc));
    return m;
}

// Chord scales (pc masks relative to the chord root).
constexpr std::uint16_t kIonian        = mask({ 0, 2, 4, 5, 7, 9, 11 });
constexpr std::uint16_t kLydian        = mask({ 0, 2, 4, 6, 7, 9, 11 });
constexpr std::uint16_t kLydianAug     = mask({ 0, 2, 4, 6, 8, 9, 11 });
constexpr std::uint16_t kMixolydian    = mask({ 0, 2, 4, 5, 7, 9, 10 });
constexpr std::uint16_t kLydianDom     = mask({ 0, 2, 4, 6, 7, 9, 10 });
constexpr std::uint16_t kMixoB13       = mask({ 0, 2, 4, 5, 7, 8, 10 });
constexpr std::uint16_t kMixoSus       = mask({ 0, 2, 5, 7, 9, 10 });
constexpr std::uint16_t kHalfWholeDim  = mask({ 0, 1, 3, 4, 6, 7, 9, 10 });
constexpr std::uint16_t kWholeTone     = mask({ 0, 2, 4, 6, 8, 10 });
constexpr std::uint16_t kDorian        = mask({ 0, 2, 3, 5, 7, 9, 10 });
constexpr std::uint16_t kMelodicMinor  = mask({ 0, 2, 3, 5, 7, 9, 11 });
constexpr std::uint16_t kLocrian       = mask({ 0, 1, 3, 5, 6, 8, 10 });

// Recognition-priority order: more specific (more required tones) first, so a
// played set is explained by the most specific type whose required tones are
// all present and whose chord tones cover every played note.
//
//   quality            suffix       chordTones                     required                       scaleTones     colorFit                3  5  7  has7
const ChordTypeInfo kTable[] = {
    { ChordQuality::Major9,          "maj7(9)",  mask({0,2,4,7,11}),   mask({0,2,4,11}),   kIonian,       ColorFit::Scale,        4, 7, 11, true  },
    { ChordQuality::Major69,         "6(9)",     mask({0,2,4,7,9}),    mask({0,2,4,9}),    kIonian,       ColorFit::Scale,        4, 7, 9,  true  },
    { ChordQuality::MinorMajor9,     "mMaj7(9)", mask({0,2,3,7,11}),   mask({0,2,3,11}),   kMelodicMinor, ColorFit::Scale,        3, 7, 11, true  },
    { ChordQuality::Minor9,          "m7(9)",    mask({0,2,3,7,10}),   mask({0,2,3,10}),   kDorian,       ColorFit::Scale,        3, 7, 10, true  },
    { ChordQuality::Minor11,         "m7(11)",   mask({0,3,5,7,10}),   mask({0,3,5,10}),   kDorian,       ColorFit::Scale,        3, 7, 10, true  },
    { ChordQuality::Dominant9,       "7(9)",     mask({0,2,4,7,10}),   mask({0,2,4,10}),   kMixolydian,   ColorFit::Scale,        4, 7, 10, true  },
    { ChordQuality::Dominant13,      "7(13)",    mask({0,4,7,9,10}),   mask({0,4,9,10}),   kMixolydian,   ColorFit::Scale,        4, 7, 10, true  },
    { ChordQuality::Dominant7b9,     "7(b9)",    mask({0,1,4,7,10}),   mask({0,1,4,10}),   kHalfWholeDim, ColorFit::Scale,        4, 7, 10, true  },
    { ChordQuality::Dominant7s9,     "7(#9)",    mask({0,3,4,7,10}),   mask({0,3,4,10}),   kHalfWholeDim, ColorFit::Scale,        4, 7, 10, true  },
    { ChordQuality::Dominant7b13,    "7(b13)",   mask({0,4,7,8,10}),   mask({0,4,7,8,10}), kMixoB13,      ColorFit::Scale,        4, 7, 10, true  },
    { ChordQuality::Dominant7s11,    "7(#11)",   mask({0,2,4,6,7,10}), mask({0,4,6,7,10}), kLydianDom,    ColorFit::Scale,        4, 7, 10, true  },
    { ChordQuality::Major7s11,       "maj7#11",  mask({0,2,4,6,7,11}), mask({0,4,6,11}),   kLydian,       ColorFit::Scale,        4, 7, 11, true  },
    { ChordQuality::Dominant7b5,     "7b5",      mask({0,4,6,10}),     mask({0,4,6,10}),   kWholeTone,    ColorFit::Scale,        4, 6, 10, true  },
    { ChordQuality::AugmentedMajor7, "maj7aug",  mask({0,4,8,11}),     mask({0,4,8,11}),   kLydianAug,    ColorFit::Scale,        4, 8, 11, true  },
    { ChordQuality::Augmented7,      "7aug",     mask({0,4,8,10}),     mask({0,4,8,10}),   kWholeTone,    ColorFit::Scale,        4, 8, 10, true  },
    { ChordQuality::Major7,          "maj7",     mask({0,4,7,11}),     mask({0,4,11}),     kIonian,       ColorFit::Scale,        4, 7, 11, true  },
    { ChordQuality::Dominant7,       "7",        mask({0,4,7,10}),     mask({0,4,10}),     kMixolydian,   ColorFit::Scale,        4, 7, 10, true  },
    { ChordQuality::Minor7,          "m7",       mask({0,3,7,10}),     mask({0,3,10}),     kDorian,       ColorFit::Scale,        3, 7, 10, true  },
    { ChordQuality::MinorMajor7,     "mMaj7",    mask({0,3,7,11}),     mask({0,3,11}),     kMelodicMinor, ColorFit::Scale,        3, 7, 11, true  },
    { ChordQuality::HalfDiminished7, "m7b5",     mask({0,3,6,10}),     mask({0,3,6,10}),   kLocrian,      ColorFit::Scale,        3, 6, 10, true  },
    { ChordQuality::Diminished7,     "dim7",     mask({0,3,6,9}),      mask({0,3,6,9}),    0,             ColorFit::ChordTones,   3, 6, 9,  true  },
    { ChordQuality::Minor6,          "m6",       mask({0,3,7,9}),      mask({0,3,9}),      kDorian,       ColorFit::Scale,        3, 7, 9,  true  },
    { ChordQuality::Major6,          "6",        mask({0,4,7,9}),      mask({0,4,9}),      kIonian,       ColorFit::Scale,        4, 7, 9,  true  },
    { ChordQuality::Dominant7sus4,   "7sus4",    mask({0,5,7,10}),     mask({0,5,10}),     kMixoSus,      ColorFit::Scale,        5, 7, 10, true  },
    { ChordQuality::MajorAdd9,       "add9",     mask({0,2,4,7}),      mask({0,2,4}),      kIonian,       ColorFit::Scale,        4, 7, 11, false },
    { ChordQuality::MinorAdd9,       "m(9)",     mask({0,2,3,7}),      mask({0,2,3}),      kDorian,       ColorFit::Scale,        3, 7, 10, false },
    { ChordQuality::Major,           "",         mask({0,4,7}),        mask({0,4,7}),      kIonian,       ColorFit::Scale,        4, 7, 11, false },
    { ChordQuality::Minor,           "m",        mask({0,3,7}),        mask({0,3,7}),      kDorian,       ColorFit::Scale,        3, 7, 10, false },
    { ChordQuality::Diminished,      "dim",      mask({0,3,6}),        mask({0,3,6}),      mask({0,3,6,9}), ColorFit::Scale,      3, 6, 9,  false },
    { ChordQuality::Augmented,       "aug",      mask({0,4,8}),        mask({0,4,8}),      0,             ColorFit::ChordTones,   4, 8, 11, false },
    { ChordQuality::Sus4,            "sus4",     mask({0,5,7}),        mask({0,5}),        kIonian,       ColorFit::Scale,        5, 7, 10, false },
    { ChordQuality::Sus2,            "sus2",     mask({0,2,7}),        mask({0,2}),        kIonian,       ColorFit::Scale,        2, 7, 10, false },
    { ChordQuality::Power,           "5",        mask({0,7}),          mask({0,7}),        0,             ColorFit::Passthrough,  0, 7, 0,  false },
    { ChordQuality::SingleNote,      "(note)",   mask({0}),            mask({0}),          0,             ColorFit::Passthrough,  0, 0, 0,  false },
};
}

const ChordTypeInfo* chordTypeTable() noexcept { return kTable; }
int chordTypeCount() noexcept { return static_cast<int>(sizeof(kTable) / sizeof(kTable[0])); }

const ChordTypeInfo& chordTypeInfo(ChordQuality quality) noexcept
{
    for (const auto& row : kTable)
        if (row.quality == quality)
            return row;
    for (const auto& row : kTable)
        if (row.quality == ChordQuality::Major)
            return row;
    return kTable[0];   // unreachable
}

std::optional<ChordMatch> matchChordMask(std::uint16_t playedMask,
                                         int bassPc,
                                         int minChordTones) noexcept
{
    if (playedMask == 0)
        return std::nullopt;

    const int count = chordTypeCount();
    int bestScore = -1;
    int bestIndex = count;
    int bestRoot = -1;

    for (int root = 0; root < 12; ++root) {
        // Pitch classes relative to this candidate root.
        std::uint16_t rotated = 0;
        for (int pc = 0; pc < 12; ++pc) {
            if (playedMask & pcBit(pc))
                rotated = static_cast<std::uint16_t>(rotated | pcBit(pc - root + 12));
        }

        for (int i = 0; i < count; ++i) {
            const auto& row = kTable[i];
            if (std::popcount(static_cast<unsigned>(row.chordTones)) < minChordTones)
                continue;
            if ((rotated & static_cast<std::uint16_t>(~row.chordTones)) != 0)
                continue;   // a held note doesn't belong to this type
            if ((rotated & row.required) != row.required)
                continue;   // a characteristic tone is missing

            // Rank readings: a type whose tones the player covers EXACTLY beats
            // one that needs omitted tones (D-F-A is Dm before F6-no-5th), then
            // the reading rooted on the bass note wins (C-E-G-A with C in bass
            // is C6, with A in bass Am7), then table order (more specific
            // types, then the conventional reading).
            const bool exact = (rotated == row.chordTones);
            const bool onBass = (root == ((bassPc % 12) + 12) % 12);
            const int score = (exact ? 2 : 0) + (onBass ? 1 : 0);
            if (score > bestScore || (score == bestScore && i < bestIndex)) {
                bestScore = score;
                bestIndex = i;
                bestRoot = root;
            }
        }
    }

    if (bestRoot < 0)
        return std::nullopt;
    return ChordMatch{ bestRoot, &kTable[bestIndex] };
}
}
