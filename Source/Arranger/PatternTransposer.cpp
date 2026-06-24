#include "PatternTransposer.h"
#include "../Midi/ChordTypes.h"

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
    // A Fallback policy is honoured only when it carries an explicit role-based
    // NTR/NTT (set by fallbackYamahaPolicy for standard channels 9..16); without
    // those it still drops to the generic heuristic via the Unknown checks below.
    return policy.ntr == YamahaNtr::Unknown
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

// True when the chord type fits color tones to a chord scale (the full NTT
// behavior); symmetric (dim/aug) and empty (power/single) types do not.
bool hasChordScale(cadenza::midi::ChordQuality quality) noexcept
{
    return cadenza::midi::chordTypeInfo(quality).colorFit == cadenza::midi::ColorFit::Scale;
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

// Nearest pitch whose pitch class is in `mask` (pc bits relative to rootPc).
int snapToMask(int pitch, int rootPc, std::uint16_t mask) noexcept
{
    bool allowed[12] = {};
    for (int pc = 0; pc < 12; ++pc)
        if (mask & cadenza::midi::pcBit(pc))
            allowed[((rootPc + pc) % 12 + 12) % 12] = true;
    return snapToAllowed(pitch, allowed);
}

// Snap a color tone to the chord scale of the played chord type (the NTT
// chord-scale tables: e.g. 7(b9) offers phrases the b9, Lydian types the #11).
int snapToChordScale(int pitch, int rootPc, cadenza::midi::ChordQuality quality) noexcept
{
    return snapToMask(pitch, rootPc, cadenza::midi::chordTypeInfo(quality).scaleTones);
}

std::optional<int> nearestPitchInScale(int pitch,
                                       int rootPc,
                                       int scaleMode,
                                       const YamahaChannelPolicy& policy) noexcept
{
    return applyPolicyNoteLimits(snapToScale(pitch, rootPc, scaleMode), policy);
}

// Fit a (already root-shifted) color/phrase pitch to the chord the player holds:
// the chord scale of the played type (full NTT tables), the actual chord tones
// for symmetric types (dim7/aug), or unchanged when there is no harmonic info
// (power/single note).
int fitColorToneToChord(int pitch, int rootPc, cadenza::midi::ChordQuality quality) noexcept
{
    const auto& info = cadenza::midi::chordTypeInfo(quality);
    switch (info.colorFit) {
        case cadenza::midi::ColorFit::Scale:      return snapToMask(pitch, rootPc, info.scaleTones);
        case cadenza::midi::ColorFit::ChordTones: return snapToMask(pitch, rootPc, info.chordTones);
        case cadenza::midi::ColorFit::Passthrough: break;
    }
    return std::clamp(pitch, 0, 127);
}
}

int chordIntervalForRole(cadenza::midi::ChordQuality quality, NoteRole role) noexcept
{
    // The per-type 3rd/5th/7th intervals come from the chord-type table
    // (ChordTypes), which covers the full Yamaha set: a 6th chord maps the
    // 7th-role note to its 6th, a 7b5 chord bends the 5th-role note to b5, etc.
    const auto& info = cadenza::midi::chordTypeInfo(quality);

    switch (role) {
        case NoteRole::ChordRoot: return 0;
        case NoteRole::Chord3:    return info.third;
        case NoteRole::Chord5:    return info.fifth;
        // A 7th-role note over a plain triad would add a wrong extension (e.g. the
        // pad's recorded maj7 turning Am into an Am7-sounding C chord). When the
        // current chord has no 7th, fold that note back onto the root so the part
        // reinforces the triad instead of colouring it.
        case NoteRole::Chord7:    return info.hasSeventh ? info.seventh : 0;
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

int fitToChordTones(int pitch, int rootPc, cadenza::midi::ChordQuality quality) noexcept
{
    const auto& info = cadenza::midi::chordTypeInfo(quality);

    // Guitar-stroke shapes always include the type's 7th (a stroked C major
    // shape keeps its recorded maj7 colour), and chords with no harmonic body
    // (power/single) snap to root+5th.
    std::uint16_t mask = info.chordTones;
    if (info.seventh > 0)
        mask = static_cast<std::uint16_t>(mask | cadenza::midi::pcBit(info.seventh));
    if (mask == cadenza::midi::pcBit(0))
        mask = static_cast<std::uint16_t>(mask | cadenza::midi::pcBit(7));

    return snapToMask(pitch, rootPc, mask);
}

std::optional<int> fitChordRoleFromAnchor(int anchor,
                                          NoteRole role,
                                          const TransposeContext& ctx,
                                          const YamahaChannelPolicy& policy) noexcept
{
    const int interval = chordIntervalForRole(ctx.chord.quality, role);
    const int targetPc = (ctx.chord.rootPitchClass + interval) % 12;
    const int anchorPc = ((anchor % 12) + 12) % 12;
    int chordToneDelta = ((targetPc - anchorPc) % 12 + 12) % 12;
    if (chordToneDelta > 6)
        chordToneDelta -= 12;
    return pitchWithPolicyLimits(anchor + chordToneDelta, ctx, policy);
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

    const int shifted = applyGlobalOffsets(note.pitch + rootDeltaForPolicy(ctx, *policy), ctx);

    // ChordColor follows the chord by root transposition relative to this
    // channel's declared source root (e.g. a phrase recorded over G). It is then
    // fit according to the declared NTT family. Guitar NTTs are a little more
    // specific: AllPurpose keeps the scale/chord fit, Stroke prefers chord-tone
    // shapes, and Arpeggio keeps a flowing scale-like line.
    if (note.role == NoteRole::ChordColor) {
        if (policy->ntr == YamahaNtr::Guitar) {
            if (policy->ntt == YamahaNtt::Stroke)
                return applyPolicyNoteLimits(fitToChordTones(shifted, ctx.chord.rootPitchClass, ctx.chord.quality), *policy);

            if (policy->ntt == YamahaNtt::Arpeggio) {
                if (hasChordScale(ctx.chord.quality))
                    return applyPolicyNoteLimits(snapToChordScale(shifted, ctx.chord.rootPitchClass, ctx.chord.quality), *policy);
                return applyPolicyNoteLimits(fitColorToneToChord(shifted, ctx.chord.rootPitchClass, ctx.chord.quality), *policy);
            }

            const int fitted = fitColorToneToChord(shifted, ctx.chord.rootPitchClass, ctx.chord.quality);
            return applyPolicyNoteLimits(fitted, *policy);
        }

        // Non-guitar NTTs: a CASM-declared NTT scale wins; otherwise fit to the
        // played chord quality.
        if (auto nttMode = scaleModeForNtt(policy->ntt))
            return nearestPitchInScale(shifted, ctx.chord.rootPitchClass, *nttMode, *policy);
        const int fitted = fitColorToneToChord(shifted, ctx.chord.rootPitchClass, ctx.chord.quality);
        return applyPolicyNoteLimits(fitted, *policy);
    }

    if (policy->ntr == YamahaNtr::Guitar
        && policy->source != YamahaPolicySource::Fallback
        && isChordRole(note.role)) {
        // Anchor the target chord tone to the source shape after its root move.
        // This avoids octave inversions when the root delta and a quality change
        // (for example C major -> G minor) cross the nearest-tone fold boundary.
        const int anchor = note.pitch + rootDeltaForPolicy(ctx, *policy);
        return fitChordRoleFromAnchor(anchor, note.role, ctx, *policy);
    }

    if (policy->bassOn && note.role == NoteRole::ChordRoot) {
        // Slash chords: when the player names a bass note (FingeredOnBass,
        // e.g. C/E), the bass-enabled part plays that note instead of the
        // chord root — the Yamaha "on bass" behavior.
        const int bassPc = ctx.chord.bassMidi >= 0 ? ((ctx.chord.bassMidi % 12) + 12) % 12 : -1;
        if (bassPc >= 0 && bassPc != ctx.chord.rootPitchClass) {
            const int sourcePc = ((note.pitch % 12) + 12) % 12;
            int delta = ((bassPc - sourcePc) % 12 + 12) % 12;
            if (delta > 6) delta -= 12;
            return pitchWithPolicyLimits(note.pitch + delta, ctx, *policy);
        }
        return transposeWithPolicyLimits(note, ctx, *policy);
    }

    if (policy->ntr == YamahaNtr::RootFixed && policy->ntt == YamahaNtt::Chord)
        return transposeWithPolicyLimits(note, ctx, *policy);

    if (policy->ntr == YamahaNtr::RootTransposition && policy->ntt == YamahaNtt::Chord) {
        if (isChordRole(note.role))
            return transposeWithPolicyLimits(note, ctx, *policy);
        return pitchWithPolicyLimits(note.pitch + rootDeltaForPolicy(ctx, *policy), ctx, *policy);
    }

    if (policy->ntr == YamahaNtr::RootTransposition && policy->ntt == YamahaNtt::Melody) {
        if (policy->source == YamahaPolicySource::Fallback && isChordRole(note.role)) {
            const int anchor = note.pitch + rootDeltaForPolicy(ctx, *policy);
            return fitChordRoleFromAnchor(anchor, note.role, ctx, *policy);
        }
        return pitchWithPolicyLimits(note.pitch + rootDeltaForPolicy(ctx, *policy), ctx, *policy);
    }

    if (policy->ntr == YamahaNtr::Guitar) {
        if (policy->ntt == YamahaNtt::Stroke)
            return transposeWithPolicyLimits(note, ctx, *policy);

        if (policy->ntt == YamahaNtt::Arpeggio) {
            if (isChordRole(note.role))
                return transposeWithPolicyLimits(note, ctx, *policy);
            if (hasChordScale(ctx.chord.quality))
                return applyPolicyNoteLimits(snapToChordScale(shifted, ctx.chord.rootPitchClass, ctx.chord.quality), *policy);
            return transposeWithPolicyLimits(note, ctx, *policy);
        }

        // AllPurpose and unknown guitar tables fall back to the safer fitted path.
        return transposeWithPolicyLimits(note, ctx, *policy);
    }

    return transposeWithPolicyLimits(note, ctx, *policy);
}
}
