#include "Arranger/PatternTransposer.h"

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

TransposeContext ctxFor(int rootPc, ChordQuality q, int keyTonic = 0, int transpose = 0, int oct = 0, int mode = 0)
{
    TransposeContext c;
    c.chord.rootPitchClass = rootPc;
    c.chord.quality = q;
    c.keyTonicPC = keyTonic;
    c.globalTranspose = transpose;
    c.globalOctave = oct;
    c.scaleMode = mode;
    return c;
}

PatternNote noteOf(NoteRole role, int pitch, int degree = 0)
{
    PatternNote n;
    n.role = role;
    n.pitch = pitch;
    n.scaleDegree = degree;
    return n;
}

YamahaChannelPolicy policyOf(YamahaNtr ntr, YamahaNtt ntt, const std::string& sourceRoot = "C")
{
    YamahaChannelPolicy policy;
    policy.source = YamahaPolicySource::Ctb2;
    policy.sourceChannel = 2;
    policy.sourceRoot = sourceRoot;
    policy.sourceChord = "Maj";
    policy.ntr = ntr;
    policy.ntt = ntt;
    return policy;
}

void absolutePassesThrough()
{
    auto n = noteOf(NoteRole::Absolute, 60);
    auto ctx = ctxFor(0, ChordQuality::Major);
    expect(transposeNote(n, ctx).value() == 60, "absolute = 60");

    ctx.globalTranspose = 3;
    expect(transposeNote(n, ctx).value() == 63, "absolute + transpose +3");

    ctx.globalOctave = -1; ctx.globalTranspose = 0;
    expect(transposeNote(n, ctx).value() == 48, "absolute - 1 octave");
}

void chordRootFollowsChord()
{
    // Notes move to the NEAREST chord tone (within -5..+6 semitones of the source),
    // preserving voicing rather than snapping into the source note's octave.
    auto n = noteOf(NoteRole::ChordRoot, 60);  // C5

    expect(transposeNote(n, ctxFor(0, ChordQuality::Major)).value() == 60, "C root stays C5");
    expect(transposeNote(n, ctxFor(5, ChordQuality::Major)).value() == 65, "F root -> nearest F (F5, +5)");
    expect(transposeNote(n, ctxFor(7, ChordQuality::Minor)).value() == 55, "G root -> nearest G (G4, -5)");
}

void chord3rdReflectsQuality()
{
    auto n = noteOf(NoteRole::Chord3, 60);

    auto majC = transposeNote(n, ctxFor(0, ChordQuality::Major));
    auto minC = transposeNote(n, ctxFor(0, ChordQuality::Minor));
    expect(majC.value() == 64, "C major 3rd = E");
    expect(minC.value() == 63, "C minor 3rd = Eb");
}

void chord5thAndDimAug()
{
    // Nearest-tone placement from source C5 (pitch 60).
    auto n = noteOf(NoteRole::Chord5, 60);

    expect(transposeNote(n, ctxFor(0, ChordQuality::Major)).value() == 55, "C major 5th = G (nearest G4, -5)");
    expect(transposeNote(n, ctxFor(0, ChordQuality::Diminished)).value() == 66, "C dim 5th = Gb (+6)");
    expect(transposeNote(n, ctxFor(0, ChordQuality::Augmented)).value() == 56, "C aug 5th = G# (nearest, -4)");
}

void chord7thReflectsQuality()
{
    // Nearest-tone placement from source C5 (pitch 60); the 7th sits just below.
    auto n = noteOf(NoteRole::Chord7, 60);

    expect(transposeNote(n, ctxFor(0, ChordQuality::Major7)).value() == 59, "Cmaj7 -> B4 (-1)");
    expect(transposeNote(n, ctxFor(0, ChordQuality::Dominant7)).value() == 58, "C7 -> Bb4 (-2)");
    expect(transposeNote(n, ctxFor(0, ChordQuality::Minor7)).value() == 58, "Cm7 -> Bb4 (-2)");
    expect(transposeNote(n, ctxFor(0, ChordQuality::Diminished7)).value() == 57, "Cdim7 -> A4 (-3)");
}

void scaleToneRespectsKey()
{
    // Key of C major, degree 0 (tonic) at octave 5
    auto n = noteOf(NoteRole::ScaleTone, 60, 0);
    auto ctx = ctxFor(0, ChordQuality::Major, /*keyTonic=*/0);

    expect(transposeNote(n, ctx).value() == 60, "C major degree 0 = C");

    n.scaleDegree = 4;
    expect(transposeNote(n, ctx).value() == 67, "C major degree 4 = G");

    n.scaleDegree = 6;
    expect(transposeNote(n, ctx).value() == 71, "C major degree 6 = B");
}

void scaleToneRespectsMinor()
{
    auto n = noteOf(NoteRole::ScaleTone, 60, 2);  // 3rd of scale
    auto ctxMajor = ctxFor(0, ChordQuality::Major, 0, 0, 0, /*mode=*/0);
    auto ctxMinor = ctxFor(0, ChordQuality::Major, 0, 0, 0, /*mode=*/1);

    expect(transposeNote(n, ctxMajor).value() == 64, "major mode 3rd = E");
    expect(transposeNote(n, ctxMinor).value() == 63, "minor mode 3rd = Eb");
}

void rangeClipping()
{
    auto n = noteOf(NoteRole::Absolute, 60);
    auto ctx = ctxFor(0, ChordQuality::Major, 0, 0, 100);   // way over range
    expect(!transposeNote(n, ctx).has_value(), "out-of-range returns nullopt");
}

void rootFixedBypassKeepsPhrasePitch()
{
    auto n = noteOf(NoteRole::ChordRoot, 60);
    auto policy = policyOf(YamahaNtr::RootFixed, YamahaNtt::Bypass);   // sourceRoot "C"

    expect(transposeNote(n, ctxFor(0, ChordQuality::Major), &policy).value() == 60, "RF+Bypass on C: no shift");
    expect(transposeNote(n, ctxFor(5, ChordQuality::Major), &policy).value() == 60, "RF+Bypass on F: phrase pitch stays");
    expect(transposeNote(n, ctxFor(7, ChordQuality::Major), &policy).value() == 60, "RF+Bypass on G: phrase pitch stays");

    // Global transpose/octave still stack on top of the root shift.
    expect(transposeNote(n, ctxFor(5, ChordQuality::Major, 0, 2, 1), &policy).value() == 74,
           "RF+Bypass + transpose +2 + octave +1");
}

void rootTranspositionMelodyUsesRootDelta()
{
    auto n = noteOf(NoteRole::Chord3, 64);
    auto policy = policyOf(YamahaNtr::RootTransposition, YamahaNtt::Melody, "C");

    expect(transposeNote(n, ctxFor(5, ChordQuality::Minor), &policy).value() == 69, "melody C source to F shifts +5");
    expect(transposeNote(n, ctxFor(2, ChordQuality::Dominant7), &policy).value() == 66, "melody C source to D shifts +2");

    n.pitch = 126;
    expect(transposeNote(n, ctxFor(5, ChordQuality::Major), &policy).value() == 127, "melody shift clamps high");
}

void rootFixedChordPolicyKeepsChordRoles()
{
    auto n = noteOf(NoteRole::Chord3, 60);
    auto policy = policyOf(YamahaNtr::RootFixed, YamahaNtt::Chord);

    expect(transposeNote(n, ctxFor(0, ChordQuality::Major), &policy).value() == 64, "RootFixed+Chord major third");
    expect(transposeNote(n, ctxFor(0, ChordQuality::Minor), &policy).value() == 63, "RootFixed+Chord minor third");
}

void bassOnChordRootFollowsRoot()
{
    auto n = noteOf(NoteRole::ChordRoot, 48);
    auto policy = policyOf(YamahaNtr::RootTransposition, YamahaNtt::Melody);
    policy.bassOn = true;

    expect(transposeNote(n, ctxFor(5, ChordQuality::Major), &policy).value() == 53, "bass-on chord-root follows F root");
}

void unknownPolicyUsesCurrentBehavior()
{
    auto n = noteOf(NoteRole::Chord3, 60);
    auto policy = policyOf(YamahaNtr::Unknown, YamahaNtt::Unknown);

    expect(transposeNote(n, ctxFor(0, ChordQuality::Major), &policy).value() == 64, "unknown policy keeps current major third");
    expect(transposeNote(n, ctxFor(0, ChordQuality::Minor), &policy).value() == 63, "unknown policy keeps current minor third");
}

void chordColorFollowsChordByRootTransposition()
{
    // A non-chord source tone (e.g. a 9th/6th in a piano/guitar phrase) must
    // move with the chord instead of freezing. It shifts by the folded root
    // delta (-5..+6 semitones), preserving the recorded interval/voicing.
    auto n = noteOf(NoteRole::ChordColor, 62);  // D above middle C

    expect(transposeNote(n, ctxFor(0, ChordQuality::Major)).value() == 62, "ChordColor on C source stays put");
    expect(transposeNote(n, ctxFor(5, ChordQuality::Major)).value() == 67, "ChordColor to F shifts +5");
    expect(transposeNote(n, ctxFor(9, ChordQuality::Minor)).value() == 59, "ChordColor to A folds down -3");
    expect(transposeNote(n, ctxFor(7, ChordQuality::Major)).value() == 57, "ChordColor to G folds down -5");

    // global transpose stacks on top of the chord shift
    expect(transposeNote(n, ctxFor(5, ChordQuality::Major, 0, 2)).value() == 69, "ChordColor + global transpose");

    // high notes clamp rather than vanish
    auto high = noteOf(NoteRole::ChordColor, 125);
    expect(transposeNote(high, ctxFor(5, ChordQuality::Major)).value() == 127, "ChordColor clamps high");
}

void rootTranspositionBypassShiftsByRootDelta()
{
    // RootTransposition + Bypass must shift the whole phrase by the root delta.
    auto n = noteOf(NoteRole::ChordRoot, 62);
    auto policy = policyOf(YamahaNtr::RootTransposition, YamahaNtt::Bypass, "C");

    expect(transposeNote(n, ctxFor(0, ChordQuality::Major), &policy).value() == 62, "RT+Bypass on C stays");
    expect(transposeNote(n, ctxFor(2, ChordQuality::Major), &policy).value() == 64, "RT+Bypass to D shifts +2");
    expect(transposeNote(n, ctxFor(9, ChordQuality::Minor), &policy).value() == 59, "RT+Bypass to A folds -3");
}

void noteLowLimitFoldsIntoAllowedRange()
{
    auto n = noteOf(NoteRole::ChordColor, 40);
    auto policy = policyOf(YamahaNtr::RootTransposition, YamahaNtt::Bypass, "C");
    policy.noteLowLimit = 48;
    policy.noteHighLimit = 72;

    expect(transposeNote(n, ctxFor(7, ChordQuality::Major), &policy).value() == 59,
           "noteLowLimit folds low root-transposed phrase up by octaves");
}

void noteHighLimitFoldsIntoAllowedRange()
{
    auto n = noteOf(NoteRole::ChordColor, 84);
    auto policy = policyOf(YamahaNtr::RootTransposition, YamahaNtt::Bypass, "C");
    policy.noteLowLimit = 48;
    policy.noteHighLimit = 76;

    expect(transposeNote(n, ctxFor(5, ChordQuality::Major), &policy).value() == 65,
           "noteHighLimit folds high root-transposed phrase down by octaves");
}

void chordRootUpperLimitShiftsRootDeltaDown()
{
    auto n = noteOf(NoteRole::ChordColor, 60);
    auto policy = policyOf(YamahaNtr::RootTransposition, YamahaNtt::Bypass, "C");
    policy.chordRootUpperLimit = 3;

    expect(transposeNote(n, ctxFor(5, ChordQuality::Major), &policy).value() == 53,
           "root above chordRootUpperLimit shifts root delta down an octave");
}

void rootTranspositionChordPrefersChordTone()
{
    auto n = noteOf(NoteRole::Chord3, 64);
    auto policy = policyOf(YamahaNtr::RootTransposition, YamahaNtt::Chord, "C");

    expect(transposeNote(n, ctxFor(0, ChordQuality::Minor), &policy).value() == 63,
           "RT+Chord maps source third to current minor third");
}

void rootTranspositionChordWithLimitsKeepsChordToneInRange()
{
    auto n = noteOf(NoteRole::Chord3, 76);
    auto policy = policyOf(YamahaNtr::RootTransposition, YamahaNtt::Chord, "C");
    policy.noteLowLimit = 52;
    policy.noteHighLimit = 64;

    expect(transposeNote(n, ctxFor(0, ChordQuality::Minor), &policy).value() == 63,
           "RT+Chord folds fitted minor third inside note limits");
}

void scaleNttFitsScaleToneToRequestedMode()
{
    auto n = noteOf(NoteRole::ScaleTone, 60, 2);
    auto naturalMinor = policyOf(YamahaNtr::RootTransposition, YamahaNtt::NaturalMinor, "C");
    auto dorian = policyOf(YamahaNtr::RootTransposition, YamahaNtt::Dorian, "C");

    expect(transposeNote(n, ctxFor(0, ChordQuality::Minor, 0, 0, 0, 0), &naturalMinor).value() == 63,
           "NaturalMinor NTT lowers scale degree 3");

    n.scaleDegree = 5;
    expect(transposeNote(n, ctxFor(0, ChordQuality::Minor, 0, 0, 0, 0), &naturalMinor).value() == 68,
           "NaturalMinor NTT uses flat 6");
    expect(transposeNote(n, ctxFor(0, ChordQuality::Minor, 0, 0, 0, 0), &dorian).value() == 69,
           "Dorian NTT uses natural 6");
}

void scaleNttFitsColorToneToNearestScaleTone()
{
    auto n = noteOf(NoteRole::ChordColor, 64); // E over C.
    auto policy = policyOf(YamahaNtr::RootTransposition, YamahaNtt::NaturalMinor, "C");

    expect(transposeNote(n, ctxFor(0, ChordQuality::Minor), &policy).value() == 63,
           "NaturalMinor NTT pulls color tone into minor scale");
}

void chordColorPolicyUsesPolicySourceRoot()
{
    auto n = noteOf(NoteRole::ChordColor, 62);

    auto cPolicy = policyOf(YamahaNtr::RootFixed, YamahaNtt::Chord, "C");
    expect(transposeNote(n, ctxFor(2, ChordQuality::Major), &cPolicy).value() == 64,
           "ChordColor C-source to D shifts +2");

    auto gPolicy = policyOf(YamahaNtr::RootFixed, YamahaNtt::Chord, "G");
    auto g = noteOf(NoteRole::ChordColor, 67);
    expect(transposeNote(g, ctxFor(0, ChordQuality::Major), &gPolicy).value() == 72,
           "ChordColor G-source to C folds +5");

    // unknown policy falls back to the ctx-only path (source root C)
    auto unknown = policyOf(YamahaNtr::Unknown, YamahaNtt::Unknown);
    expect(transposeNote(n, ctxFor(5, ChordQuality::Major), &unknown).value() == 67,
           "ChordColor unknown policy uses ctx-only root transposition");
}

// Color/phrase notes should fit the QUALITY of the chord the player holds, not
// just root-shift. A C-major source phrase over a minor chord must take the
// minor 3rd, over a dominant chord the b7, etc. (clean-room chord-scale fit).
void colorToneFitsPlayedChordQuality()
{
    // E (the major 3rd over C) used as a color tone. No policy -> ctx-only path.
    auto major3 = noteOf(NoteRole::ChordColor, 64);

    // Over a plain major chord it stays diatonic.
    expect(transposeNote(major3, ctxFor(0, ChordQuality::Major)).value() == 64,
           "color E over C major stays E");

    // Over A minor it should bend to the minor 3rd (C), not the clashing C#.
    expect(transposeNote(major3, ctxFor(9, ChordQuality::Minor)).value() == 60,
           "color E over Am fits minor 3rd C (not C#)");

    // The major 7th (B) over a dominant chord should fit the b7.
    auto maj7 = noteOf(NoteRole::ChordColor, 71);
    expect(transposeNote(maj7, ctxFor(7, ChordQuality::Dominant7)).value() == 65,
           "color B over G7 fits the b7 (F)");
}

// Symmetric / exotic qualities also fit: half-diminished uses Locrian, while
// diminished and augmented snap to their actual chord tones (no clean 7-note scale).
void colorToneFitsExoticQualities()
{
    // C# color over C diminished -> nearest chord tone (C, since dim = C Eb Gb A).
    auto n = noteOf(NoteRole::ChordColor, 61);
    expect(transposeNote(n, ctxFor(0, ChordQuality::Diminished)).value() == 60,
           "color C# over Cdim snaps to chord tone C");

    // E color over C half-diminished -> Locrian (C Db Eb F Gb Ab Bb) -> Eb.
    auto e = noteOf(NoteRole::ChordColor, 64);
    expect(transposeNote(e, ctxFor(0, ChordQuality::HalfDiminished7)).value() == 63,
           "color E over Cm7b5 fits Locrian (Eb)");

    // C# color over C augmented -> chord tone (C E G#) -> C.
    auto cs = noteOf(NoteRole::ChordColor, 61);
    expect(transposeNote(cs, ctxFor(0, ChordQuality::Augmented)).value() == 60,
           "color C# over Caug snaps to chord tone C");
}

void colorTonePolicyChordModeFitsPlayedQuality()
{
    // A Chord-NTT part whose CASM declares no scale mode should still fit the
    // played chord's quality (the runtime path for most real styles).
    auto major3 = noteOf(NoteRole::ChordColor, 64);
    auto policy = policyOf(YamahaNtr::RootFixed, YamahaNtt::Chord, "C");

    expect(transposeNote(major3, ctxFor(9, ChordQuality::Minor), &policy).value() == 60,
           "Chord-NTT color E over Am fits minor 3rd C");
    expect(transposeNote(major3, ctxFor(0, ChordQuality::Major), &policy).value() == 64,
           "Chord-NTT color E over C major stays E");
}

void fallbackPolicyWithRolesIsHonoured()
{
    // A Fallback policy carrying explicit role-based NTR/NTT (as set by
    // fallbackYamahaPolicy for standard channels 9..16) must now be honoured
    // instead of dropping to the generic chord-snap heuristic.
    YamahaChannelPolicy bass;
    bass.source = YamahaPolicySource::Fallback;
    bass.sourceRoot = "C";
    bass.ntr = YamahaNtr::RootTransposition;
    bass.ntt = YamahaNtt::Bypass;                    // bass: root-shift the whole line

    auto root  = noteOf(NoteRole::ChordRoot,  36);   // C2
    auto fifth = noteOf(NoteRole::ChordColor, 43);   // G2, a 5th above
    // To A minor the folded root delta C->A is -3; the whole line shifts by -3,
    // preserving the recorded interval (no per-note chord snapping).
    expect(transposeNote(root,  ctxFor(9, ChordQuality::Minor), &bass).value() == 33,
           "fallback bass root C2 -> A1 (-3)");
    expect(transposeNote(fifth, ctxFor(9, ChordQuality::Minor), &bass).value() == 40,
           "fallback bass 5th also shifts -3 (interval preserved)");

    // A Fallback policy WITHOUT explicit NTR/NTT still uses the heuristic.
    YamahaChannelPolicy unknown;
    unknown.source = YamahaPolicySource::Fallback;   // ntr/ntt stay Unknown
    auto third = noteOf(NoteRole::Chord3, 60);
    expect(transposeNote(third, ctxFor(0, ChordQuality::Minor), &unknown).value() == 63,
           "fallback w/o NTR/NTT keeps the heuristic minor third");
}

void drumsStayAbsoluteWithPolicy()
{
    auto n = noteOf(NoteRole::Absolute, 36);
    auto policy = policyOf(YamahaNtr::RootFixed, YamahaNtt::Chord);
    policy.bassOn = true;

    expect(transposeNote(n, ctxFor(5, ChordQuality::Major), &policy).value() == 36, "drum absolute stays unchanged with policy");
}
}

int main()
{
    absolutePassesThrough();
    chordRootFollowsChord();
    chord3rdReflectsQuality();
    chord5thAndDimAug();
    chord7thReflectsQuality();
    scaleToneRespectsKey();
    scaleToneRespectsMinor();
    rangeClipping();
    rootFixedBypassKeepsPhrasePitch();
    rootTranspositionMelodyUsesRootDelta();
    rootFixedChordPolicyKeepsChordRoles();
    bassOnChordRootFollowsRoot();
    unknownPolicyUsesCurrentBehavior();
    chordColorFollowsChordByRootTransposition();
    rootTranspositionBypassShiftsByRootDelta();
    noteLowLimitFoldsIntoAllowedRange();
    noteHighLimitFoldsIntoAllowedRange();
    chordRootUpperLimitShiftsRootDeltaDown();
    rootTranspositionChordPrefersChordTone();
    rootTranspositionChordWithLimitsKeepsChordToneInRange();
    scaleNttFitsScaleToneToRequestedMode();
    scaleNttFitsColorToneToNearestScaleTone();
    chordColorPolicyUsesPolicySourceRoot();
    colorToneFitsPlayedChordQuality();
    colorToneFitsExoticQualities();
    colorTonePolicyChordModeFitsPlayedQuality();
    fallbackPolicyWithRolesIsHonoured();
    drumsStayAbsoluteWithPolicy();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All PatternTransposer tests passed\n";
    return EXIT_SUCCESS;
}
