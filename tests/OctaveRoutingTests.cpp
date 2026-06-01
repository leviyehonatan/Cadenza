// OctaveRoutingTests — proves the Octave control affects ONLY live right-hand
// melody notes, never style/accompaniment playback (drums, bass, harmony).

#include "Arranger/RuntimePlayback.h"
#include "Arranger/Style.h"
#include "Arranger/PatternTransposer.h"
#include "Midi/ChordRecognizer.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

using namespace cadenza::arranger;
using cadenza::midi::Chord;
using cadenza::midi::ChordQuality;

Chord cMajor()
{
    Chord c;
    c.rootPitchClass = 0;
    c.quality = ChordQuality::Major;
    return c;
}

PatternNote note(NoteRole role, int pitch)
{
    PatternNote n;
    n.role = role;
    n.pitch = pitch;
    return n;
}

Part melodicPart(const std::string& name, int channel)
{
    Part p;
    p.name = name;
    p.midiChannel = channel;   // 2/3 = bass/harmony, not the drum channel
    return p;
}

Part drumPart()
{
    Part p;
    p.name = "drums";
    p.midiChannel = 10;
    p.percussion = true;
    p.bankMsb = 0;             // GM kit (not Yamaha/XG) -> no note remap
    return p;
}

void styleContextNeverCarriesOctave()
{
    for (int transpose : { -5, 0, 7 }) {
        const auto ctx = makeStylePlaybackContext(cMajor(), /*key=*/0, transpose);
        expect(ctx.globalOctave == 0, "style context globalOctave is always 0");
        expect(ctx.globalTranspose == transpose, "style context keeps transpose");
    }
    // There is intentionally no octave parameter on makeStylePlaybackContext —
    // octave cannot reach style playback through this API.
}

void styleBassNoteIgnoresOctave()
{
    const auto bass = melodicPart("bass", 2);
    const auto root = note(NoteRole::ChordRoot, 36);   // C2 seed

    const int played = playbackNoteForPart(bass, root, makeStylePlaybackContext(cMajor(), 0, 0)).value();
    expect(played == 36, "bass chord-root on C = 36 (no octave applied)");

    // Prove the transposer *would* move it if octave were applied — but the style
    // path does not. (octave +2 via transposeNote would yield 36 + 24 = 60.)
    TransposeContext octaveCtx = makeStylePlaybackContext(cMajor(), 0, 0);
    octaveCtx.globalOctave = 2;   // simulate "if the bug were still here"
    const int wouldBe = transposeNote(root, octaveCtx).value();
    expect(wouldBe == 60, "sanity: +2 octaves would have produced 60");
    expect(played != wouldBe, "style bass note does NOT include the octave shift");
}

void styleHarmonyNoteIgnoresOctave()
{
    const auto harmony = melodicPart("harmony", 3);
    const auto third = note(NoteRole::Chord3, 60);     // C5 seed

    const int played = playbackNoteForPart(harmony, third, makeStylePlaybackContext(cMajor(), 0, 0)).value();
    expect(played == 64, "harmony chord-3 on C major = E5 (64), no octave applied");
}

void drumsIgnoreOctaveAndTranspose()
{
    const auto drums = drumPart();
    const auto hit = note(NoteRole::Absolute, 36);     // kick

    // Drums bypass the transpose context entirely; vary octave/transpose and the
    // played note must never move.
    for (int oct : { -3, 0, 3 }) {
        TransposeContext ctx = makeStylePlaybackContext(cMajor(), 0, /*transpose=*/oct * 2);
        ctx.globalOctave = oct;   // even if something set octave, drums must not move
        const int played = playbackNoteForPart(drums, hit, ctx).value();
        expect(played == 36, "drum kick stays 36 regardless of octave/transpose");
    }
}

void liveMelodyNoteShiftsByOctave()
{
    expect(liveMelodyNote(60, 0)  == 60, "live melody octave 0 keeps pitch");
    expect(liveMelodyNote(60, 1)  == 72, "live melody +1 octave = +12");
    expect(liveMelodyNote(60, -1) == 48, "live melody -1 octave = -12");
    expect(liveMelodyNote(60, 2)  == 84, "live melody +2 octaves = +24");

    // Clamped to the valid MIDI range.
    expect(liveMelodyNote(120, 1) == 127, "live melody clamps high");
    expect(liveMelodyNote(5, -1)  == 0,   "live melody clamps low");
}

void transposeStillAffectsStyleParts()
{
    const auto bass = melodicPart("bass", 2);
    const auto root = note(NoteRole::ChordRoot, 36);
    const auto harmony = melodicPart("harmony", 3);
    const auto third = note(NoteRole::Chord3, 60);

    const auto ctx = makeStylePlaybackContext(cMajor(), 0, /*transpose=*/3);
    expect(playbackNoteForPart(bass, root, ctx).value() == 39, "transpose +3 moves bass 36 -> 39");
    expect(playbackNoteForPart(harmony, third, ctx).value() == 67, "transpose +3 moves harmony 64 -> 67");
}
}

int main()
{
    styleContextNeverCarriesOctave();
    styleBassNoteIgnoresOctave();
    styleHarmonyNoteIgnoresOctave();
    drumsIgnoreOctaveAndTranspose();
    liveMelodyNoteShiftsByOctave();
    transposeStillAffectsStyleParts();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All OctaveRouting tests passed\n";
    return EXIT_SUCCESS;
}
