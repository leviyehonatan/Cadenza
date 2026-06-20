// ab-transpose — A/B measurement harness for note transposition fidelity.
//
// Transposes a fixed C-major source voicing to a chord progression with BOTH:
//   - Cadenza   : PatternTransposer::transposeNote (folds root delta to nearest)
//   - YamJJazz  : the reference placement extracted from
//                 PhraseUtilities.fitMelodyPhrase2ChordSymbol
//                 (Note(srcPitch + rootDelta[0..11]).getClosestPitch(destPC))
// and prints the per-note delta so real divergences are visible at a glance.
//
// Source chord is C major (the usual Yamaha source); chord roles carry the degree.

#include "Arranger/PatternTransposer.h"
#include "Midi/ChordRecognizer.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace cadenza::arranger;
using cadenza::midi::Chord;

static std::string noteName(int midi)
{
    static const char* n[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    if (midi < 0 || midi > 127) return "--";
    char buf[8];
    std::snprintf(buf, sizeof buf, "%s%d", n[midi % 12], midi / 12 - 1);
    return buf;
}

static const char* roleName(NoteRole r)
{
    switch (r) {
        case NoteRole::ChordRoot: return "root";
        case NoteRole::Chord3:    return "3rd";
        case NoteRole::Chord5:    return "5th";
        case NoteRole::Chord7:    return "7th";
        default:                  return "?";
    }
}

// YamJJazz placement: anchor = srcPitch + (destRoot - srcRoot) mod 12  (always 0..11 up),
// then snap to the nearest octave of the destination pitch class.
static int yamRef(const PatternNote& note, const Chord& chord)
{
    const int interval = chordIntervalForRole(chord.quality, note.role);
    const int destPc   = ((chord.rootPitchClass + interval) % 12 + 12) % 12;
    const int rootDelta = ((chord.rootPitchClass) % 12 + 12) % 12;   // srcRoot = C(0)
    const int anchor = note.pitch + rootDelta;
    int d = ((destPc - ((anchor % 12) + 12) % 12) % 12 + 12) % 12;
    if (d > 6) d -= 12;
    return anchor + d;
}

int main(int argc, char** argv)
{
    // A typical close C-major-7 chord-part voicing (root/3rd/5th/7th).
    const std::vector<PatternNote> src = [] {
        auto mk = [](NoteRole role, int pitch) { PatternNote n; n.role = role; n.pitch = pitch; return n; };
        return std::vector<PatternNote>{
            mk(NoteRole::ChordRoot, 60),  // C5
            mk(NoteRole::Chord3,    64),  // E5
            mk(NoteRole::Chord5,    67),  // G5
            mk(NoteRole::Chord7,    70),  // Bb5
        };
    }();

    std::vector<std::string> prog = { "C", "Am", "F", "G", "G7", "Dm7", "E", "Bb", "F#m", "Ab" };
    if (argc > 1) { prog.clear(); for (int i = 1; i < argc; ++i) prog.push_back(argv[i]); }

    std::printf("A/B transposition  (source = C major voicing; Cadenza vs YamJJazz reference)\n");
    std::printf("source:");
    for (const auto& n : src) std::printf(" %s(%s)", roleName(n.role), noteName(n.pitch).c_str());
    std::printf("\n");

    int total = 0, differ = 0, octaveDiffs = 0;
    for (const auto& cs : prog) {
        auto chord = cadenza::midi::parseChordSymbol(cs);
        if (!chord) { std::printf("  bad chord: %s\n", cs.c_str()); continue; }
        TransposeContext ctx; ctx.chord = *chord;

        std::printf("\n== %-4s (root=%d) ==\n", cs.c_str(), chord->rootPitchClass);
        for (const auto& n : src) {
            auto cad = transposeNote(n, ctx);
            const int cadPitch = cad.value_or(-1);
            const int yam = yamRef(n, *chord);
            const int delta = (cad ? cadPitch - yam : 0);
            ++total;
            const char* flag = "";
            if (cad && delta != 0) { ++differ; flag = (delta % 12 == 0) ? "  <<< OCTAVE" : "  <<< diff"; }
            if (cad && delta != 0 && delta % 12 == 0) ++octaveDiffs;
            std::printf("  %-4s src=%-4s  cadenza=%-4s(%3d)  yamjjazz=%-4s(%3d)  delta=%+d%s\n",
                        roleName(n.role), noteName(n.pitch).c_str(),
                        noteName(cadPitch).c_str(), cadPitch,
                        noteName(yam).c_str(), yam, delta, flag);
        }
    }

    std::printf("\nSUMMARY: %d notes, %d differ from YamJJazz (%d are whole-octave register diffs)\n",
                total, differ, octaveDiffs);
    return 0;
}
