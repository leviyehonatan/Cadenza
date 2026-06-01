# Yamaha Playback Policy

This is Cadenza's first clean Yamaha playback policy model. It is intentionally
small: the importer separates Yamaha channel metadata from generic
`PatternNote` transposition, and playback consults that policy through a
policy-aware `PatternTransposer` overload. It does not rewrite `StyleEngine`.

The project reference file is currently `JJAZZLAB_YAMAHA_STYLE_STUDY.md` in the
repo root. It describes the JJazzLab/YamJJazz model; this implementation uses
that behavior as a reference only and defines Cadenza-owned types.

## Implemented

- `YamahaStyleFormat`: `Unknown`, `SFF1`, `SFF2`
- `YamahaNtr`: `RootTransposition`, `RootFixed`, `Guitar`, `Unknown`
- `YamahaNtt`: `Bypass`, `Melody`, `Chord`, minor/dorian modes, guitar modes,
  and `Unknown`
- `YamahaRetriggerRule`: decoded and stored, but not acted on yet
- `YamahaChannelPolicy`: attached to generated `.sty` parts
- binary `Ctb2` full-range policy decode for the observed `00H 7FH` middle
  range layout
- binary `Ctab` policy decode for the existing fixed offsets
- fallback policy for `.sty` parts without usable CASM metadata
- `StyleEngine` passes `Part::yamahaPolicy` into `PatternTransposer` when one
  is present

The importer applies policy only where the result is safe with the existing
Cadenza note-role model:

- drums stay absolute
- `NTT=Bypass` stays absolute
- `RootFixed + Chord` maps source chord tones to `ChordRoot`, `Chord3`,
  `Chord5`, and `Chord7`
- `RootTransposition + Melody` keeps the old heuristic because Cadenza does not
  yet have a melody-relative role
- `Bass On` marks the part as bass and makes source chord tones root-following
- unknown NTR/NTT values use the old fallback heuristic

Playback then applies these conservative runtime rules:

- `NTT=Bypass`: ignore chord transposition and play the source pitch with only
  global transpose/octave applied
- `RootFixed + Chord`: use the existing chord-role transposition path
- `RootTransposition + Melody`: shift the original source pitch by
  `destinationRoot - sourceRoot`, preserve melodic shape, and clamp to MIDI
  `0..127`
- `bassOn`: chord-root notes keep root-following bass behavior
- missing, fallback, generic CASM, or unknown policy values use the existing
  transposer behavior
- drums remain absolute because drum notes use `NoteRole::Absolute`

## Still Missing

- full Yamaha phrase fitting
- SFF2 low/main/high range splitting
- retrigger handling during chord changes
- muted root/chord source-channel selection
- Cntt override handling
- guitar-specific playback behavior
- source chord/type-aware phrase fitting beyond the simple root delta

## Why This Should Help

Before this change, every non-drum `.sty` channel was treated as if it were the
same C-major source phrase. Yamaha styles contain per-channel CASM policy, and
some channels are explicitly bypassed, melody-transposed, or bass-oriented.
Respecting those simple flags prevents obvious wrong transposition while keeping
unsupported Yamaha behavior on the old heuristic path.
