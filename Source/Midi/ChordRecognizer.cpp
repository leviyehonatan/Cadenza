#include "ChordRecognizer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>

namespace cadenza::midi
{
namespace
{
struct Template
{
    ChordQuality quality;
    std::vector<int> intervals;   // relative to root, pitch classes 0..11
    int requiredMatches;          // how many of these notes must be present to match
    int priority;                 // higher = preferred when scores tie
};

// Ordered so that more-specific qualities (7ths) win over less-specific (triads)
// when both are present in the held notes.
const std::vector<Template>& chordTemplates()
{
    static const std::vector<Template> t = {
        // 7th chords first (more specific)
        { ChordQuality::Major7,         { 0, 4, 7, 11 }, 4, 100 },
        { ChordQuality::Dominant7,      { 0, 4, 7, 10 }, 4, 100 },
        { ChordQuality::Minor7,         { 0, 3, 7, 10 }, 4, 100 },
        { ChordQuality::MinorMajor7,    { 0, 3, 7, 11 }, 4, 100 },
        { ChordQuality::Diminished7,    { 0, 3, 6, 9  }, 4, 100 },
        { ChordQuality::HalfDiminished7,{ 0, 3, 6, 10 }, 4, 100 },
        // Triads
        { ChordQuality::Major,          { 0, 4, 7 }, 3, 90 },
        { ChordQuality::Minor,          { 0, 3, 7 }, 3, 90 },
        { ChordQuality::Diminished,     { 0, 3, 6 }, 3, 90 },
        { ChordQuality::Augmented,      { 0, 4, 8 }, 3, 90 },
        { ChordQuality::Sus2,           { 0, 2, 7 }, 3, 85 },
        { ChordQuality::Sus4,           { 0, 5, 7 }, 3, 85 },
        // Dyads / single notes
        { ChordQuality::Power,          { 0, 7 }, 2, 50 },
        { ChordQuality::SingleNote,     { 0 },    1, 10 },
    };
    return t;
}

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
    switch (quality) {
        case ChordQuality::Major:           return "";
        case ChordQuality::Minor:           return "m";
        case ChordQuality::Dominant7:       return "7";
        case ChordQuality::Major7:          return "maj7";
        case ChordQuality::Minor7:          return "m7";
        case ChordQuality::MinorMajor7:     return "mMaj7";
        case ChordQuality::Diminished:      return "dim";
        case ChordQuality::HalfDiminished7: return "m7b5";
        case ChordQuality::Diminished7:     return "dim7";
        case ChordQuality::Augmented:       return "aug";
        case ChordQuality::Sus2:            return "sus2";
        case ChordQuality::Sus4:            return "sus4";
        case ChordQuality::Power:           return "5";
        case ChordQuality::SingleNote:      return "(note)";
    }
    return "";
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
// to a ChordQuality. Ordered longest/most-specific first.
ChordQuality qualityFromSuffix(const std::string& s)
{
    struct Entry { const char* token; ChordQuality quality; };
    static const Entry table[] = {
        // half-diminished / diminished sevenths (specific spellings first)
        { "m7b5", ChordQuality::HalfDiminished7 }, { "min7b5", ChordQuality::HalfDiminished7 },
        { "m7-5", ChordQuality::HalfDiminished7 }, { "o7", ChordQuality::Diminished7 },
        { "dim7", ChordQuality::Diminished7 },
        // minor-major sevenths
        { "mmaj7", ChordQuality::MinorMajor7 }, { "minmaj7", ChordQuality::MinorMajor7 },
        { "m(maj7)", ChordQuality::MinorMajor7 }, { "mmaj9", ChordQuality::MinorMajor7 },
        // major sevenths / sixths / extensions
        { "maj7", ChordQuality::Major7 }, { "major7", ChordQuality::Major7 },
        { "maj9", ChordQuality::Major7 }, { "maj13", ChordQuality::Major7 },
        { "ma7", ChordQuality::Major7 }, { "m7", ChordQuality::Minor7 },
        { "min7", ChordQuality::Minor7 }, { "m9", ChordQuality::Minor7 },
        { "m11", ChordQuality::Minor7 }, { "m13", ChordQuality::Minor7 },
        { "m6", ChordQuality::Minor }, { "min", ChordQuality::Minor },
        // dominant sevenths / extensions
        { "7", ChordQuality::Dominant7 }, { "9", ChordQuality::Dominant7 },
        { "11", ChordQuality::Dominant7 }, { "13", ChordQuality::Dominant7 },
        // triads / dyads
        { "dim", ChordQuality::Diminished }, { "aug", ChordQuality::Augmented },
        { "sus2", ChordQuality::Sus2 }, { "sus4", ChordQuality::Sus4 },
        { "sus", ChordQuality::Sus4 }, { "6", ChordQuality::Major },
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

    // Build the set of pitch classes present.
    std::set<int> pcs;
    int bass = notes.front();
    for (int n : notes) {
        pcs.insert(normPC(n));
        if (n < bass) bass = n;
    }

    if (pcs.size() == 1) {
        Chord c;
        c.rootPitchClass = *pcs.begin();
        c.quality = ChordQuality::SingleNote;
        c.bassMidi = bass;
        return c;
    }

    // Try each possible root pitch class. The "root" is preferably the bass
    // note's pitch class, but we search all 12 and score them.
    const auto& templates = chordTemplates();

    int bestRoot = -1;
    int bestScore = -1;
    int bestPriority = -1;
    ChordQuality bestQuality = ChordQuality::Major;

    for (int root = 0; root < 12; ++root) {
        // Build intervals present relative to this root.
        std::set<int> intervals;
        for (int pc : pcs) {
            intervals.insert((pc - root + 12) % 12);
        }

        for (const auto& tpl : templates) {
            // Count matches: how many of the template's required intervals are present.
            int matches = 0;
            for (int iv : tpl.intervals) {
                if (intervals.count(iv)) ++matches;
            }
            if (matches < tpl.requiredMatches) continue;

            // Score: match count + bonus for not including "extra" notes outside template.
            int extras = static_cast<int>(intervals.size()) - matches;
            int score = matches * 10 - extras * 2;

            // Strong preference: root coincides with the bass note pitch class.
            if (root == normPC(bass)) score += 5;

            bool better = false;
            if (score > bestScore) better = true;
            else if (score == bestScore && tpl.priority > bestPriority) better = true;

            if (better) {
                bestScore = score;
                bestPriority = tpl.priority;
                bestRoot = root;
                bestQuality = tpl.quality;
            }
        }
    }

    if (bestRoot < 0) return std::nullopt;

    Chord c;
    c.rootPitchClass = bestRoot;
    c.quality = bestQuality;
    c.bassMidi = bass;
    return c;
}
}
