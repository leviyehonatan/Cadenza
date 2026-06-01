# Drum Playback Audit

Date: 2026-05-30

## Summary

Cadenza's main drum routing bug is not the Cadenza-to-synth channel number anymore:
Cadenza channel 10 maps to synth channel 9. The remaining quality problems are more
likely caused by incomplete Yamaha/XG drum-kit handling, missing style mixer CCs in
older builds, and the selected SoundFont's coverage of Yamaha/XG drum banks.

This pass keeps the engine architecture unchanged and improves the existing playback
path by preserving and applying drum preset/mixer metadata.

## Current Findings

### SoundFont Quality

Status: still variable.

Cadenza can now switch SoundFonts at runtime. Drum quality still depends heavily on
whether the selected `.sf2`/`.sf3` contains useful GM/XG drum kits. A weak GM-only
SoundFont may still make Yamaha styles sound cheap even with correct routing.

Smallest next step: compare the same `.sty` with a known-good GM/XG SoundFont and
check the new drum diagnostics log for the selected bank/program.

### Drum Channel

Status: fixed/confirmed.

`Audio/MidiChannel` treats Cadenza channels as 1..16 and synth channels as 0..15:

- Cadenza channel 10 maps to synth channel 9.
- Tests assert this mapping.
- `StyleEngine` logs both `sourceCh` and `synthCh` for every drum part.

### FluidSynth Percussion Channel

Status: partially confirmed.

The FluidSynth wrapper enables `synth.drums-channel.active` and logs that synth
channel 9 is the GM drum channel. This matches the GM convention and Cadenza's
channel mapping.

Remaining risk: SoundFont-specific bank behavior can still differ. Yamaha XG styles
often use drum bank MSB 127, and not every SoundFont maps that bank the same way.

### Bank / Program

Status: improved.

The `.sty` parser preserves:

- bank select MSB: CC0
- bank select LSB: CC32
- program change

`StyleEngine` sends bank select before program change. If a drum part has no program,
runtime setup falls back to GM Standard Kit program `0`.

Real local `.STY` inspection showed Yamaha/XG-style drum metadata:

- `CULY.STY`: drum parts on channel 10, bank `127/0`, program `0`
- `CULY-ext.STY`: drum parts on channel 10, bank `127/0`, program `0`
- `C.ARDAS-RUDY1.S919.STY`: drum parts on channel 10, bank `127/0`, program `8`

### Mixer CCs

Status: fixed for the first useful set.

The parser and `.cstyle` loader now preserve:

- CC7 volume
- CC10 pan
- CC91 reverb send
- CC93 chorus send

`StyleEngine` applies these CCs during section channel setup before notes play.

Local `.STY` inspection showed these values matter. Examples:

- `CULY.STY` mainA drums: volume `120`, reverb `25`
- `CULY-ext.STY` mainA drums: volume `120`, reverb `25`
- `C.ARDAS-RUDY1.S919.STY` mainA drums: volume `127`, reverb `6`

### Drum Note Mapping

Status: first conservative compatibility entry implemented.

Cadenza now detects Yamaha/XG-looking drum parts and applies a tiny compatibility
map before sending percussion notes to the synth. Unknown notes stay unchanged.

The runtime now logs the first 20 drum note numbers per drum part and warns if any
drum notes fall outside the common GM drum range `35..81`.

Example local findings:

- `CULY.STY` fill sections include note `31`, which is outside the common GM range.
- Most common local drum notes include `36`, `40`, `42`, `44`, `46`, `47`, `48`,
  `49`, and `57`.

Current compatibility map:

- Yamaha/XG note `31` -> GM note `37` Side Stick

Remaining risk: this is not a full Yamaha-to-GM drum map yet.

## What Changed

- Added drum diagnostics logs for every percussion part:
  - source channel
  - synth channel
  - bank MSB/LSB
  - program
  - percussion flag
  - first 20 distinct note numbers
- Added common-range warning for suspicious drum notes.
- Preserved CC7, CC10, CC91, and CC93 from `.sty`.
- Preserved those CCs in `.cstyle` load/save.
- Applied those CCs during section setup.
- Kept drums absolute and untransposed.
- Added first conservative Yamaha/XG drum note remap: `31 -> 37`.
- Kept StyleEngine architecture unchanged.

## What Still Sounds Wrong If It Happens

If drums still sound cheap after this pass, the likely causes are:

1. The selected SoundFont has weak drum samples.
2. The SoundFont does not map Yamaha/XG bank `127/0` kits well.
3. The style uses Yamaha/XG drum notes not yet covered by the conservative map.
4. Cadenza does not yet parse/apply the full Yamaha/XG setup ecosystem.

## Recommended Next Step

Continue the Yamaha/XG drum policy layer:

- identify known kit program numbers
- expand the Yamaha/XG-to-GM note remap table only with real evidence
- keep the raw note if no mapping is known

This is the next likely playback-quality improvement after SoundFont selection,
channel routing, bank/program setup, and mixer CC preservation.
