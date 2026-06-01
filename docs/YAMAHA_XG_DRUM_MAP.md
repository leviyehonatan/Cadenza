# Yamaha/XG Drum Compatibility Map

Date: 2026-05-30

## Scope

Cadenza now has a small Yamaha/XG-to-GM drum compatibility layer for `.sty`
playback through GM/FluidSynth SoundFonts.

This is intentionally conservative:

- only percussion parts are eligible
- Yamaha/XG detection requires drum/percussion status plus bank MSB `127` or
  parsed Yamaha channel metadata
- unknown notes are left unchanged
- chord transposition is still bypassed for drums

## Current Map

| Yamaha/XG note | GM playback note | Reason |
| --- | --- | --- |
| 31 | 37 | Local `.STY` audit found note 31 outside the common GM range. Yamaha/XG note 31 is a sticks-style hit; GM 37 is Side Stick. |

## Runtime Behavior

For each percussion note:

1. Detect whether the part looks Yamaha/XG.
2. If not Yamaha/XG, keep the note unchanged.
3. If Yamaha/XG, apply the tiny compatibility map.
4. If the note is not in the map, keep it unchanged.
5. Send the resulting note to the synth and schedule note-off using the same
   remapped note.

When a note changes, `StyleEngine` logs:

- original note
- remapped note
- part name
- bank MSB/LSB
- program

## What This Does Not Do

This is not a full Yamaha/XG drum map. It does not yet cover every XG kit,
alternate kit, SFX kit, brush kit, or percussion extension. It only fixes the
first observed incompatibility while avoiding broad guesses that could make good
styles worse.

## Next Candidates

Add more entries only when a real style and SoundFont comparison proves the
mapping. Each new entry should include:

- source `.sty` evidence
- original Yamaha/XG note
- audible problem with a GM/FluidSynth kit
- chosen GM note
- regression test
