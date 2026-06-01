#include "PatternTransposer.h"

#include <algorithm>
#include <cstdlib>

namespace cadenza::arranger
{
namespace
{
std::optional<int> clampMidi(int note) noexcept
{
    return std::clamp(note, 0, 127);
}

std::optional<int> midiInRange(int note) noexcept
{
    if (note < 0 || note > 127)
        return std::nullopt;
    return note;
}

int sourceRootPitchClass(const YamahaChannelPolicy& policy) noexcept
{
    if (!policy.sourceRoot)
        return 0;

    const auto& root = *policy.sourceRoot;
    if (root == "C") return 0;
    if (root == "C#") return 1;
    if (root == "D") return 2;
    if (root == "Eb") return 3;
    if (root == "E") return 4;
    if (root == "F") return 5;
    if (root == "F#") return 6;
    if (root == "G") return 7;
    if (root == "G#") return 8;
    if (root == "A") return 9;
    if (root == "Bb") return 10;
    if (root == "B") return 11;
    return 0;
}

int applyGlobalOffsets(int pitch, const TransposeContext& ctx) noexcept
{
    return pitch + ctx.globalTranspose + 12 * ctx.globalOctave;
}

// Root delta folded into the nearest octave (-5..+6 semitones) so a phrase
// follows the chord without drifting up an octave across a progression.
int foldedRootDelta(int chordRootPc, int sourceRootPc) noexcept
{
    int d = ((chordRootPc - sourceRootPc) % 12 + 12) % 12;  // 0..11
    if (d > 6) d -= 12;                                     // -5..6
    return d;
}

bool isUnknownPolicy(const YamahaChannelPolicy& policy) noexcept
{
    return policy.source == YamahaPolicySource::Fallback
        || policy.source == YamahaPolicySource::CASM
        || policy.ntr == YamahaNtr::Unknown
        || policy.ntt == YamahaNtt::Unknown;
}

bool isChordRole(NoteRole role) noexcept
{
    return role == NoteRole::ChordRoot
        || role == NoteRole::Chord3
        || role == NoteRole::Chord5
        || role == NoteRole::Chord7;
}

std::optional<int> scaleModeForNtt(YamahaNtt ntt) noexcept
{
    switch (ntt) {
        case YamahaNtt::NaturalMinor:  return 1;
        case YamahaNtt::HarmonicMinor: return 2;
        case YamahaNtt::MelodicMinor:  return 3;
        case YamahaNtt::Dorian:        return 4;
        default:                       return std::nullopt;
    }
}

// Scale to fit non-chord (color/phrase) tones to, chosen from the QUALITY of the
// chord the player is holding. This is what makes a major-key source phrase sound
// right over minor / dominant / etc. chords instead of keeping a clashing 3rd/7th.
// Returns nullopt for qualities without a clean 7-note fit (dim/aug/power) so those
// keep simple root transposition.
std::optional<int> scaleModeForChordQuality(cadenza::midi::ChordQuality quality) noexcept
{
    using Q = cadenza::midi::ChordQuality;
    switch (quality) {
        case Q::Major:
        case Q::Major7:
        case Q::Sus2:
        case Q::Sus4:        return 0;   // Ionian (major)
        case Q::Dominant7:      return 5;   // Mixolydian (major with b7)
        case Q::Minor:
        case Q::Minor7:         return 4;   // Dorian (neutral minor for phrases)
        case Q::MinorMajor7:    return 3;   // Melodic minor
        case Q::HalfDiminished7:return 6;   // Locrian
        default:                return std::nullopt;  // dim/aug/power/single
    }
}

int rootDeltaForPolicy(const TransposeContext& ctx, const YamahaChannelPolicy& policy) noexcept
{
    int delta = foldedRootDelta(ctx.chord.rootPitchClass, sourceRootPitchClass(policy));

    if (policy.chordRootUpperLimit) {
        const int limitPc = ((*policy.chordRootUpperLimit % 12) + 12) % 12;
        if (ctx.chord.rootPitchClass > limitPc)
            delta -= 12;
    }

    return delta;
}

std::optional<int> applyPolicyNoteLimits(int pitch, const YamahaChannelPolicy& policy) noexcept
{
    if (!policy.noteLowLimit && !policy.noteHighLimit)
        return clampMidi(pitch);

    int low = std::clamp(policy.noteLowLimit.value_or(0), 0, 127);
    int high = std::clamp(policy.noteHighLimit.value_or(127), 0, 127);
    if (low > high)
        std::swap(low, high);

    while (pitch < low && pitch + 12 <= 127)
        pitch += 12;
    while (pitch > high && pitch - 12 >= 0)
        pitch -= 12;

    return clampMidi(std::clamp(pitch, low, high));
}

std::optional<int> transposeWithPolicyLimits(const PatternNote& note,
                                             const TransposeContext& ctx,
                                             const YamahaChannelPolicy& policy)
{
    auto played = transposeNote(note, ctx);
    if (!played)
        return played;
    return applyPolicyNoteLimits(*played, policy);
}

std::optional<int> pitchWithPolicyLimits(int pitch,
                                         const TransposeContext& ctx,
                                         const YamahaChannelPolicy& policy) noexcept
{
    return applyPolicyNoteLimits(applyGlobalOffsets(pitch, ctx), policy);
}

// Nearest in-range (0..127) pitch whose pitch class is allowed. Ties -> lower.
int snapToAllowed(int pitch, const bool allowed[12]) noexcept
{
    int best = std::clamp(pitch, 0, 127);
    int bestDistance = 256;
    for (int candidate = 0; candidate <= 127; ++candidate) {
        if (!allowed[candidate % 12])
            continue;
        const int distance = std::abs(candidate - pitch);
        if (distance < bestDistance || (distance == bestDistance && candidate < best)) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

int snapToScale(int pitch, int rootPc, int scaleMode) noexcept
{
    bool allowed[12] = {};
    for (int degree = 0; degree < 7; ++degree)
        allowed[((rootPc + scaleSemitone(scaleMode, degree)) % 12 + 12) % 12] = true;
    return snapToAllowed(pitch, allowed);
}

std::optional<int> nearestPitchInScale(int pitch,
                                       int rootPc,
                                       int scaleMode,
                                       const YamahaChannelPolicy& policy) noexcept
{
    return applyPolicyNoteLimits(snapToScale(pitch, rootPc, scaleMode), policy);
}

// Fit a (already root-shifted) color/phrase pitch to the chord the player holds:
// a 7-note scale for tonal qualities, the actual chord tones for symmetric ones
// (dim/aug), or unchanged for qualities with no harmonic info (power/single).
int fitColorToneToChord(int pitch, int rootPc, cadenza::midi::ChordQuality quality) noexcept
{
    using Q = cadenza::midi::ChordQuality;

    if (auto mode = scaleModeForChordQuality(quality))
        return snapToScale(pitch, rootPc, *mode);

    bool allowed[12] = {};
    auto add = [&](int interval) { allowed[((rootPc + interval) % 12 + 12) % 12] = true; };
    switch (quality) {
        case Q::Diminished:  add(0); add(3); add(6); add(9); break;  // dim7 tones
        case Q::Diminished7: add(0); add(3); add(6); add(9); break;
        case Q::Augmented:   add(0); add(4); add(8);          break;
        default:             return std::clamp(pitch, 0, 127);       // power/single
    }
    return snapToAllowed(pitch, allowed);
}
}

int chordIntervalForRole(cadenza::midi::ChordQuality quality, NoteRole role) noexcept
{
    using Q = cadenza::midi::ChordQuality;

    // Default 3rds / 5ths / 7ths per quality
    int third  = 4;  // major 3rd
    int fifth  = 7;  // perfect 5th
    int seventh = 10; // dominant 7th
    bool hasSeventh = false;  // does THIS quality actually contain a 7th?

    switch (quality) {
        case Q::Major:           third = 4; fifth = 7; seventh = 11; hasSeventh = false; break;
        case Q::Minor:           third = 3; fifth = 7; seventh = 10; hasSeventh = false; break;
        case Q::Dominant7:       third = 4; fifth = 7; seventh = 10; hasSeventh = true;  break;
        case Q::Major7:          third = 4; fifth = 7; seventh = 11; hasSeventh = true;  break;
        case Q::Minor7:          third = 3; fifth = 7; seventh = 10; hasSeventh = true;  break;
        case Q::MinorMajor7:     third = 3; fifth = 7; seventh = 11; hasSeventh = true;  break;
        case Q::Diminished:      third = 3; fifth = 6; seventh = 9;  hasSeventh = false; break;
        case Q::HalfDiminished7: third = 3; fifth = 6; seventh = 10; hasSeventh = true;  break;
        case Q::Diminished7:     third = 3; fifth = 6; seventh = 9;  hasSeventh = true;  break;
        case Q::Augmented:       third = 4; fifth = 8; seventh = 11; hasSeventh = false; break;
        case Q::Sus2:            third = 2; fifth = 7; seventh = 10; hasSeventh = false; break;
        case Q::Sus4:            third = 5; fifth = 7; seventh = 10; hasSeventh = false; break;
        case Q::Power:           third = 0; fifth = 7; seventh = 0;  hasSeventh = false; break;
        case Q::SingleNote:      third = 0; fifth = 0; seventh = 0;  hasSeventh = false; break;
    }

    switch (role) {
        case NoteRole::ChordRoot: return 0;
        case NoteRole::Chord3:    return third;
        case NoteRole::Chord5:    return fifth;
        // A 7th-role note over a plain triad would add a wrong extension (e.g. the
        // pad's recorded maj7 turning Am into an Am7-sounding C chord). When the
        // current chord has no 7th, fold that note back onto the root so the part
        // reinforces the triad instead of colouring it.
        case NoteRole::Chord7:    return hasSeventh ? seventh : 0;
        default:                  return 0;
    }
}

int scaleSemitone(int scaleMode, int degree) noexcept
{
    // Major scale degrees 0..6 = 0, 2, 4, 5, 7, 9, 11
    // Natural minor      0..6 = 0, 2, 3, 5, 7, 8, 10
    // Harmonic minor     0..6 = 0, 2, 3, 5, 7, 8, 11
    // Melodic minor      0..6 = 0, 2, 3, 5, 7, 9, 11
    // Dorian             0..6 = 0, 2, 3, 5, 7, 9, 10
    static const int major[7]         = { 0, 2, 4, 5, 7, 9, 11 };
    static const int naturalMinor[7]  = { 0, 2, 3, 5, 7, 8, 10 };
    static const int harmonicMinor[7] = { 0, 2, 3, 5, 7, 8, 11 };
    static const int melodicMinor[7]  = { 0, 2, 3, 5, 7, 9, 11 };
    static const int dorian[7]        = { 0, 2, 3, 5, 7, 9, 10 };
    static const int mixolydian[7]    = { 0, 2, 4, 5, 7, 9, 10 };
    static const int locrian[7]       = { 0, 1, 3, 5, 6, 8, 10 };

    if (degree < 0 || degree > 6) return 0;
    switch (scaleMode) {
        case 1: return naturalMinor[degree];
        case 2: return harmonicMinor[degree];
        case 3: return melodicMinor[degree];
        case 4: return dorian[degree];
        case 5: return mixolydian[degree];
        case 6: return locrian[degree];
        default: return major[degree];
    }
}

std::optional<int> transposeNote(const PatternNote& note, const TransposeContext& ctx)
{
    // Absolute notes: just apply global transpose/octave.
    if (note.role == NoteRole::Absolute) {
        int n = note.pitch + ctx.globalTranspose + 12 * ctx.globalOctave;
        if (n < 0 || n > 127) return std::nullopt;
        return n;
    }

    // ScaleTone: pitch field is used only as an octave reference (octave from pitch).
    if (note.role == NoteRole::ScaleTone) {
        if (note.scaleDegree < 0 || note.scaleDegree > 6) return std::nullopt;
        int baseOctave = note.pitch / 12;
        int semitone = scaleSemitone(ctx.scaleMode, note.scaleDegree);
        int n = baseOctave * 12 + ((ctx.keyTonicPC + semitone) % 12)
              + ctx.globalTranspose + 12 * ctx.globalOctave;
        if (n < 0 || n > 127) return std::nullopt;
        return n;
    }

    // ChordColor: a non-chord source tone. Shift the phrase by the folded chord-
    // root delta (source root = C here), then fit it to the played chord's scale
    // so a major-key phrase takes the minor 3rd / dominant b7 / etc. instead of
    // clashing. Exotic qualities (dim/aug/power) skip the snap and just root-shift.
    if (note.role == NoteRole::ChordColor) {
        int delta = foldedRootDelta(ctx.chord.rootPitchClass, 0);
        int shifted = applyGlobalOffsets(note.pitch + delta, ctx);
        return fitColorToneToChord(shifted, ctx.chord.rootPitchClass, ctx.chord.quality);
    }

    // Chord roles: ChordRoot / Chord3 / Chord5 / Chord7.
    // Move the source note to the nearest chord tone (correct pitch class for the
    // chord quality) rather than snapping into the source note's own octave. This
    // preserves the recorded voicing instead of scattering notes across octaves.
    const int interval = chordIntervalForRole(ctx.chord.quality, note.role);
    const int targetPC = (ctx.chord.rootPitchClass + interval) % 12;
    const int sourcePC = ((note.pitch % 12) + 12) % 12;
    int delta = ((targetPC - sourcePC) % 12 + 12) % 12;   // 0..11
    if (delta > 6) delta -= 12;                            // nearest tone: -5..+6
    int n = note.pitch + delta + ctx.globalTranspose + 12 * ctx.globalOctave;
    if (n < 0 || n > 127) return std::nullopt;
    return n;
}

std::optional<int> transposeNote(const PatternNote& note,
                                 const TransposeContext& ctx,
                                 const YamahaChannelPolicy* policy)
{
    if (!policy || isUnknownPolicy(*policy))
        return transposeNote(note, ctx);

    // Drums and other absolute notes still use the existing absolute path.
    if (note.role == NoteRole::Absolute)
        return transposeWithPolicyLimits(note, ctx, *policy);

    if (auto scaleMode = scaleModeForNtt(policy->ntt); scaleMode && note.role == NoteRole::ScaleTone) {
        auto scaleCtx = ctx;
        scaleCtx.scaleMode = *scaleMode;
        return transposeWithPolicyLimits(note, scaleCtx, *policy);
    }

    if (policy->ntr == YamahaNtr::RootFixed && policy->ntt == YamahaNtt::Bypass)
        return pitchWithPolicyLimits(note.pitch, ctx, *policy);

    if (policy->ntt == YamahaNtt::Bypass) {
        // RootTransposition + Bypass = root shift only: no chord/scale fitting.
        return pitchWithPolicyLimits(note.pitch + rootDeltaForPolicy(ctx, *policy), ctx, *policy);
    }

    // ChordColor follows the chord by root transposition relative to this
    // channel's declared source root (e.g. a phrase recorded over G). It is then
    // fit to a scale: the CASM-declared NTT scale if any, otherwise a scale
    // derived from the QUALITY of the chord the player is holding, so phrases sit
    // in key on minor / dominant / etc. chords.
    if (note.role == NoteRole::ChordColor) {
        const int shifted = applyGlobalOffsets(note.pitch + rootDeltaForPolicy(ctx, *policy), ctx);
        // A CASM-declared NTT scale wins; otherwise fit to the played chord quality.
        if (auto nttMode = scaleModeForNtt(policy->ntt))
            return nearestPitchInScale(shifted, ctx.chord.rootPitchClass, *nttMode, *policy);
        const int fitted = fitColorToneToChord(shifted, ctx.chord.rootPitchClass, ctx.chord.quality);
        return applyPolicyNoteLimits(fitted, *policy);
    }

    if (policy->bassOn && note.role == NoteRole::ChordRoot)
        return transposeWithPolicyLimits(note, ctx, *policy);

    if (policy->ntr == YamahaNtr::RootFixed && policy->ntt == YamahaNtt::Chord)
        return transposeWithPolicyLimits(note, ctx, *policy);

    if (policy->ntr == YamahaNtr::RootTransposition && policy->ntt == YamahaNtt::Chord) {
        if (isChordRole(note.role))
            return transposeWithPolicyLimits(note, ctx, *policy);
        return pitchWithPolicyLimits(note.pitch + rootDeltaForPolicy(ctx, *policy), ctx, *policy);
    }

    if (policy->ntr == YamahaNtr::RootTransposition && policy->ntt == YamahaNtt::Melody) {
        return pitchWithPolicyLimits(note.pitch + rootDeltaForPolicy(ctx, *policy), ctx, *policy);
    }

    if (policy->ntr == YamahaNtr::Guitar) {
        // TODO: implement Yamaha guitar tables. For now, keep guitar parts safe
        // by fitting known chord tones and root-shifting color tones above.
        return transposeWithPolicyLimits(note, ctx, *policy);
    }

    return transposeWithPolicyLimits(note, ctx, *policy);
}
}
