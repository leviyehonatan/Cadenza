# Note Transposition & Voicing — Fidelity Study

Goal: make Cadenza's accompaniment transpose/voice notes to the held chord like a
real arranger (Genos/Giglad), across all chord types and styles. This is the
core "does it sound musically right" quality of the product.

## Finding 1 — Giglad's binary is a dead end for *transposition*

The Giglad RE dump (`Desktop/New folder (4)`) cracked Giglad's input/routing/
split/chord-mode/note-bookkeeping layers, but **not** the note-transposition
engine. Its own summary states: *"Not yet proven: Yamaha-style transposition
engine."* Corroborated here: the 53k-line `Giglad_decompiled.c` has zero
mod-12 / note-table / transpose evidence.

→ For voicing specifically, reverse-engineering Giglad won't pay off. Use it only
as an *audible* A/B oracle if we ever capture its live MIDI out.

## Finding 2 — The real oracle is the Yamaha SFF NTT spec + YamJJazz

Note transposition is the documented **Yamaha SFF / CASM NTT** system (NTR =
Note Transposition Rule, NTT = Note Transposition Table). The accurate
open-source reimplementation is **YamJJazz** (which Cadenza already references via
`JJAZZLAB_YAMAHA_STYLE_STUDY.md`):

- `G:/AI_WORKSPACE/projects/JJazzLab/plugins/YamJJazz/src/main/java/org/jjazz/yamjjazz/`
- notably `CASMDataReader` (`CnttData` = CNTT note-transposition data) and `AccType`.

This is reachable, exact, and the right ground truth.

## Finding 3 — Cadenza's `PatternTransposer` is already a real NTT engine

`Source/Arranger/PatternTransposer.cpp` already implements, per
`YamahaChannelPolicy` (NTR × NTT):

- NTR: RootTransposition, RootFixed, Guitar
- NTT: Bypass, Chord, Melody, Stroke, Arpeggio, + scale modes
  (Natural/Harmonic/Melodic minor, Dorian)
- chord roles (root/3/5/7) via the `ChordTypes` per-type interval table
- ChordColor color-tone fitting to the played chord's scale/chord tones
- bassOn slash chords, note low/high limits, chordRootUpperLimit, folded root
  delta (nearest octave)

It is **not crude** — but it fits notes *algorithmically* (nearest-tone fold,
scale-snap) rather than via Yamaha's **exact per-source-note lookup tables**.
That algorithmic-vs-table difference is the suspected fidelity gap.

Known-missing (from `docs/YAMAHA_PLAYBACK_POLICY.md`, some since narrowed):
SFF2 low/main/high range splitting, Cntt override handling, full source
chord/type-aware phrase fitting beyond the simple root delta, muted root/chord
source-channel selection.

## Plan (RE → spec → A/B → fix, test-first)

1. **Extract the exact NTT tables from YamJJazz** — read `CASMDataReader`/`CnttData`
   + the YamJJazz transposition code; write down each NTT type's source-pc → target
   mapping and the chord-type handling. (This is the authoritative spec.)
2. **Gap analysis** — line up Cadenza's algorithmic `transposeNote` against those
   tables; list concrete divergences (prime suspects: Melody-NTT third-fitting,
   minor-scale variants, chord-tone octave folding, Cntt overrides, SFF2 ranges).
3. **A/B harness** — pick 2–3 representative styles + a fixed chord progression
   (C, Am, F, G, G7, Dm7, …). Render Cadenza's parts to MIDI via `style-probe` /
   `exportPlaybackDiagnostics`; compare to the YamJJazz-derived expectation
   (and, where feasible, a YamJJazz render of the same style).
4. **Fix, test-first** — encode the correct table behavior as `PatternTransposer`
   tests, close the top deltas, re-A/B. Repeat.

## STEP 1 RESULT — the exact YamJJazz NTT algorithm

NTR/NTT enums (`Ctb2ChannelSettings`): NTR = ROOT_TRANSPOSITION(0)/ROOT_FIXED(1)/
GUITAR(2). NTT byte 0..10 = BYPASS, MELODY, CHORD, MELODIC_MINOR(+_5),
HARMONIC_MINOR(+_5), NATURAL_MINOR(+_5), DORIAN(+_5); GUITAR uses byte 0/1/2 =
ALL_PURPOSE/STROKE/ARPEGGIO. RTR = STOP/PITCH_SHIFT/PITCH_SHIFT_TO_ROOT/
RETRIGGER/RETRIGGER_TO_ROOT/NOTE_GENERATOR. Ctb2 also carries chordRootUpperLimit,
note low/high limits, bassOn, and SFF2 low/main/high range splitting.

**Dispatch (`YamJJazzRhythmGenerator.fitSrcPhraseToChordSymbol`):** everything
reduces to THREE fitting primitives in `core/.../PhraseUtilities.java`:
- ROOT_FIXED + MELODY/CHORD, and all GUITAR → `fitChordPhrase2ChordSymbol`
- ROOT_TRANS + CHORD → `fitMelodyPhrase2ChordSymbol(chordMode=true)`
- ROOT_TRANS + MELODY → `fitMelodyPhrase2ChordSymbol(false)` (or `fitBass…` if bassOn)
- ROOT_TRANS + minor/dorian → **force dest chord's rendering scale**
  (Harmonic/Melodic/Aeolian/Dorian) then `fitMelodyPhrase2ChordSymbol(false)`
- ROOT_TRANS + BYPASS → pure root-delta pitch shift
- chordRootUpperLimit applied AFTER: if destRoot > limit, transpose whole phrase −12.
- `_5` variants are treated IDENTICALLY to the base → **not a real gap.**

**The actual math = DEGREE-PRESERVING mapping (not pitch-class snapping):**
1. Each source note → its **degree in the SOURCE chord type**
   (`getDegreeMostProbable(relPitchToSrcRoot)`).
2. `pSrc.getDestDegrees(ecsDest, chordMode)` maps each source degree → a
   **destination-chord degree** (handles dest chord more/less complex than source).
3. dest pitch = `Note(srcPitch + rootDelta).getClosestPitch(destChordRelPitchOfDegree)`.
- `fitChordPhrase2ChordSymbol` additionally does **voice-leading**: it tries all
  degree permutations / both parallel-chord directions and picks the inversion
  that minimises voice motion (`computeParallelChord` + `computeChordMatchingScore`).
- `fitBass…` forces the ROOT degree to the dest chord's **bass note** (slash chords).

## Gap analysis vs Cadenza `PatternTransposer`

Cadenza bakes a COARSE `NoteRole` (Absolute/ScaleTone/ChordColor/ChordRoot·3·5·7)
at import and transposes each note **independently** (nearest chord-tone fold,
scale-snap). The two concrete divergences:

1. **No voice-leading for chord parts (most audible).** YamJJazz fits a chord
   part by choosing the best inversion that minimises voice motion; Cadenza moves
   each chord-role note to its own nearest tone, which can scatter the voicing
   (the "messy" feel). → **First fix target.**
2. **Coarse degree info.** YamJJazz preserves the source note's exact degree
   (incl. 9/11/13 extensions) and remaps via `getDestDegrees`; Cadenza collapses
   non-triad tones to "color" + scale-snap, losing extended-degree precision on
   rich chords. → Second target (needs richer per-note source info at bake time).

Confirmed structural gaps (lower priority): SFF2 low/main/high range splitting,
Cntt overrides, exact RTR set.

## A/B MEASUREMENT (tools/ab-transpose) — the real #1 delta

Built `tools/ab-transpose`: transposes a fixed C-major source voicing (root/3/5/7)
to a chord progression with BOTH Cadenza `transposeNote` and the YamJJazz
placement, and diffs. Result on C/Am/F/G/G7/Dm7/E/Bb/F#m/Ab:

**40 notes → 22 differ, ALL whole-octave register diffs.** Pattern:
- root delta 0..6 above source (C,F,Dm7,E,F#m): Cadenza == YamJJazz ✓
- root delta 7..11 (Am,G,G7,Ab,Bb): Cadenza places the WHOLE part **−1 octave**
  vs YamJJazz ✗  (+ the 7th-role note over a no-7th chord folds an extra octave).

Root cause = ONE rule: Cadenza `foldedRootDelta` folds to nearest (−5..+6);
Yamaha/YamJJazz shifts UP by the mod-12 root delta (0..+11) then `getClosestPitch`.
So for ~45% of chord roots Cadenza's comp/melody sits an octave low vs a Genos.

**Nuance (design call):** fold-to-nearest is arguably *smoother* (less register
jump) but is NOT what Yamaha does — for Genos fidelity, match the up-shift. Note:
real styles' CASM note limits + chordRootUpperLimit constrain this in practice,
and Cadenza's BASS register anchor must be preserved (don't push bass up an
octave). So the fix is per-role/limit-aware, not a blanket change.

## Next concrete step (the now-measured FIX)

Align `PatternTransposer` root-delta placement with Yamaha (up 0..11 + closest
octave) for melodic/chord parts, while (a) keeping the bass register anchor,
(b) honouring note limits + chordRootUpperLimit the YamJJazz way (−12 the fitted
result if destRoot > limit). Build test-first; re-run `ab-transpose` and confirm
0 whole-octave diffs on parts without limit constraints.
