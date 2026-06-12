#include "ChordRecognizer.h"
#include "ChordTypes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>

namespace cadenza::midi
{
namespace
{
int normPC(int n)
{
    int pc = n % 12;
    return pc < 0 ? pc + 12 : pc;
}
}

std::string pitchClassName(int pc)
{
    static const char* names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    if (pc < 0 || pc > 11) return "?";
    return names[pc];
}

std::string Chord::rootName() const { return pitchClassName(rootPitchClass); }

std::string Chord::qualitySuffix() const
{
    return chordTypeInfo(quality).suffix;
}

std::string Chord::toString() const
{
    std::string out = rootName() + qualitySuffix();
    if (bassMidi >= 0) {
        int bassPC = normPC(bassMidi);
        if (bassPC != rootPitchClass) {
            out += "/";
            out += pitchClassName(bassPC);
        }
    }
    return out;
}

namespace
{
// Root letter -> pitch class (natural).
std::optional<int> letterPitchClass(char c)
{
    switch (std::toupper(static_cast<unsigned char>(c))) {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
        default:  return std::nullopt;
    }
}

// Map a chord-symbol suffix (already lower-cased, accidentals/root stripped)
// to a ChordQuality. Ordered longest/most-specific first. Covers the full
// Yamaha chord-type set plus common alternate spellings.
ChordQuality qualityFromSuffix(const std::string& s)
{
    struct Entry { const char* token; ChordQuality quality; };
    static const Entry table[] = {
        // minor-major sevenths / ninths (specific spellings first)
        { "mmaj7(9)", ChordQuality::MinorMajor9 }, { "mmaj9", ChordQuality::MinorMajor9 },
        { "minmaj9", ChordQuality::MinorMajor9 },
        { "mmaj7", ChordQuality::MinorMajor7 }, { "minmaj7", ChordQuality::MinorMajor7 },
        { "m(maj7)", ChordQuality::MinorMajor7 },
        // half-diminished / diminished sevenths
        { "m7b5", ChordQuality::HalfDiminished7 }, { "min7b5", ChordQuality::HalfDiminished7 },
        { "m7-5", ChordQuality::HalfDiminished7 }, { "o7", ChordQuality::Diminished7 },
        { "dim7", ChordQuality::Diminished7 },
        // major sevenths / extensions
        { "maj7#11", ChordQuality::Major7s11 }, { "maj7(#11)", ChordQuality::Major7s11 },
        { "maj7aug", ChordQuality::AugmentedMajor7 }, { "maj7#5", ChordQuality::AugmentedMajor7 },
        { "augmaj7", ChordQuality::AugmentedMajor7 },
        { "maj7(9)", ChordQuality::Major9 }, { "maj9", ChordQuality::Major9 },
        { "maj13", ChordQuality::Major9 },
        { "maj7", ChordQuality::Major7 }, { "major7", ChordQuality::Major7 },
        { "ma7", ChordQuality::Major7 },
        // major sixths / add9
        { "6(9)", ChordQuality::Major69 }, { "69", ChordQuality::Major69 },
        { "6/9", ChordQuality::Major69 },
        { "add9", ChordQuality::MajorAdd9 }, { "(9)", ChordQuality::MajorAdd9 },
        { "6", ChordQuality::Major6 },
        // minor sevenths / extensions
        { "m7(9)", ChordQuality::Minor9 }, { "m9", ChordQuality::Minor9 },
        { "m7(11)", ChordQuality::Minor11 }, { "m11", ChordQuality::Minor11 },
        { "m13", ChordQuality::Minor9 },
        { "m7", ChordQuality::Minor7 }, { "min7", ChordQuality::Minor7 },
        { "m(9)", ChordQuality::MinorAdd9 }, { "madd9", ChordQuality::MinorAdd9 },
        { "m6", ChordQuality::Minor6 }, { "min", ChordQuality::Minor },
        // dominant sevenths / extensions / alterations
        { "7sus4", ChordQuality::Dominant7sus4 }, { "7sus", ChordQuality::Dominant7sus4 },
        { "7(9)", ChordQuality::Dominant9 }, { "9", ChordQuality::Dominant9 },
        { "7(13)", ChordQuality::Dominant13 }, { "13", ChordQuality::Dominant13 },
        { "11", ChordQuality::Dominant9 },
        { "7(b9)", ChordQuality::Dominant7b9 }, { "7b9", ChordQuality::Dominant7b9 },
        { "7(#9)", ChordQuality::Dominant7s9 }, { "7#9", ChordQuality::Dominant7s9 },
        { "7(b13)", ChordQuality::Dominant7b13 }, { "7b13", ChordQuality::Dominant7b13 },
        { "7(#11)", ChordQuality::Dominant7s11 }, { "7#11", ChordQuality::Dominant7s11 },
        { "7b5", ChordQuality::Dominant7b5 }, { "7-5", ChordQuality::Dominant7b5 },
        { "7aug", ChordQuality::Augmented7 }, { "aug7", ChordQuality::Augmented7 },
        { "7#5", ChordQuality::Augmented7 }, { "7+5", ChordQuality::Augmented7 },
        { "7", ChordQuality::Dominant7 },
        // triads / dyads
        { "dim", ChordQuality::Diminished }, { "aug", ChordQuality::Augmented },
        { "sus2", ChordQuality::Sus2 }, { "sus4", ChordQuality::Sus4 },
        { "sus", ChordQuality::Sus4 },
        { "5", ChordQuality::Power }, { "m", ChordQuality::Minor },
        { "-", ChordQuality::Minor }, { "+", ChordQuality::Augmented },
    };
    for (const auto& e : table) {
        if (s == e.token) return e.quality;
    }
    if (s.empty()) return ChordQuality::Major;
    // Unrecognised suffix: best-effort by leading character.
    if (s[0] == 'm' || s[0] == '-') return ChordQuality::Minor;
    return ChordQuality::Major;
}
}

std::optional<Chord> parseChordSymbol(const std::string& symbol)
{
    // Trim leading/trailing whitespace.
    std::size_t a = 0, b = symbol.size();
    while (a < b && std::isspace(static_cast<unsigned char>(symbol[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(symbol[b - 1]))) --b;
    if (a >= b) return std::nullopt;
    std::string s = symbol.substr(a, b - a);

    // Optional slash bass: "C/E".
    std::optional<int> bassPc;
    if (auto slash = s.find('/'); slash != std::string::npos) {
        const std::string bassPart = s.substr(slash + 1);
        s = s.substr(0, slash);
        if (!bassPart.empty()) {
            if (auto pc = letterPitchClass(bassPart[0])) {
                int v = *pc;
                for (std::size_t i = 1; i < bassPart.size(); ++i) {
                    if (bassPart[i] == '#') ++v;
                    else if (bassPart[i] == 'b' || bassPart[i] == 'B') --v;
                    else break;
                }
                bassPc = ((v % 12) + 12) % 12;
            }
        }
    }

    if (s.empty()) return std::nullopt;

    auto rootPc = letterPitchClass(s[0]);
    if (!rootPc) return std::nullopt;
    int root = *rootPc;

    // Accidentals immediately after the root letter.
    std::size_t i = 1;
    while (i < s.size() && (s[i] == '#' || s[i] == 'b')) {
        if (s[i] == '#') ++root;
        else             --root;
        ++i;
    }
    root = ((root % 12) + 12) % 12;

    // Remaining text is the quality suffix.
    std::string suffix = s.substr(i);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    Chord c;
    c.rootPitchClass = root;
    c.quality = qualityFromSuffix(suffix);
    c.bassMidi = bassPc ? *bassPc : -1;
    return c;
}

std::optional<Chord> recognise(const std::vector<int>& notes)
{
    if (notes.empty()) return std::nullopt;

    // Build the pitch-class mask and find the bass (lowest) note.
    std::uint16_t played = 0;
    int bass = notes.front();
    for (int n : notes) {
        played = static_cast<std::uint16_t>(played | pcBit(normPC(n)));
        if (n < bass) bass = n;
    }

    const auto match = matchChordMask(played, normPC(bass), 1);
    if (!match.has_value()) return std::nullopt;

    Chord c;
    c.rootPitchClass = match->root;
    c.quality = match->info->quality;
    c.bassMidi = bass;
    return c;
}
}
