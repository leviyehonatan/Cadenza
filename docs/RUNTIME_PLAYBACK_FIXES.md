# Runtime Playback Fixes

Date: 2026-05-30

This pass implements the first playback-correctness fixes from
`docs/CADENZA_RUNTIME_AUDIT.md`. The goal was to address the main reason loaded
Yamaha `.sty` files sounded like a merged piano texture: channel and preset
state were not being applied to the synth.

## What Was Broken

- Cadenza style/UI channels are stored as MIDI channels `1..16`, but
  FluidSynth expects channels `0..15`.
- `StyleEngine` passed `Part::midiChannel` directly into `AudioEngine`, and
  `AudioEngine` passed it directly to FluidSynth.
- The parser read program changes only well enough to create a display
  instrument name. The numeric program was not stored in `Part`.
- Bank select CCs (`0` and `32`) were ignored.
- `StyleEngine` played note-on/note-off only. It never initialized each style
  channel with bank/program state before playback.
- Drum parts were absolute in the note-role model, but channel 10 was not
  guaranteed to reach FluidSynth channel 9.

## What Was Fixed

- Added a central channel mapping helper:
  - Cadenza channel `1` maps to synth channel `0`
  - Cadenza channel `10` maps to synth channel `9`
  - invalid channels are rejected
- `AudioEngine` now treats public note/program/CC APIs as Cadenza `1..16`
  channels and converts before calling the synth backend.
- `Part` now stores:
  - `bankMsb`
  - `bankLsb`
  - `program`
  - `percussion`
- `.cstyle` load/save preserves those fields when present.
- `.sty` parsing now captures bank select CC 0/32 and program changes per
  channel and carries them into generated parts.
- `StyleEngine` applies channel setup for the active section:
  - sends bank MSB as CC 0 when available
  - sends bank LSB as CC 32 when available
  - sends program change when available
  - sends drum fallback program 0 for percussion parts without a program
- `StyleEngine` logs runtime setup per active part:
  - section
  - part name
  - Cadenza channel
  - synth channel
  - bank/program
  - percussion flag
  - note count
- Percussion parts now play their source pitches directly in `StyleEngine`
  instead of going through chord transposition.

## Expected Playback Impact

Yamaha `.sty` playback should now sound less like one piano pile because style
channels are initialized before notes play. Bass, chord, pad, and drum channels
can select different GM/SoundFont presets instead of all inheriting the synth's
default preset.

Drums should also improve because Cadenza channel 10 now reaches FluidSynth
channel 9, the GM percussion channel.

## What Still Remains Missing Compared To Giglad

- no dynamic style-part mixer yet
- no UI-generated mixer channels from loaded style parts
- no mute/solo enforcement in `StyleEngine`
- no Yamaha/XG drum-kit bank policy beyond preserving source bank data
- no replay of expression, modulation, pitch bend, or other controller streams
- no arranger section state machine for intro/fill/ending transitions
- no queued bar-boundary switching
- no real VU metering
- no per-part audio buses
- no Yamaha SFF2 low/main/high phrase splitting
- no full CASM source-channel selection by muted roots/chords

## Tests Added

- channel mapping correctness
- Cadenza drum channel to synth drum channel mapping
- part preset/percussion metadata defaults
- bank/program setup-plan preservation
- drum setup-plan percussion detection
- `.sty` import preserves bank/program metadata
- `.sty` drum parts remain absolute and marked percussion

## Next Small Step

Add a runtime style-part mixer model keyed by actual loaded style parts and MIDI
channels. Then wire mute/solo and per-channel volume/pan to either MIDI CCs or
a simple playback gate in `StyleEngine`.
