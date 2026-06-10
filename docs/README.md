# Cadenza Workstation - Current Development Summary

_Last updated: 2026-06-10_

Cadenza is a working native Windows arranger workstation. The primary UI is the
native JUCE panel. It can load Yamaha `.sty` files, recognize left-hand chords,
transpose accompaniment in real time, play right-hand melody, switch style
sections, use FluidSynth, and route individual parts to VST3 instruments.

## Recent Changes

### Runtime and thread safety

- Section changes requested while playing are queued and applied by the audio
  thread instead of mutating playback state from the message thread.
- Style replacement while playing is also consumed by the audio thread.
- Song section changes are queued before the relevant audio-thread bar boundary.
- Non-looping songs stop at their ending boundary.
- External play, stop, and BPM commands now pass through a fixed-capacity
  transport mailbox and are consumed at the start of an audio block.
- Transport `playing` and BPM status are published through lock-free atomics for
  safe UI and StyleEngine reads.
- Audio-thread ending stops remain immediate.
- Panic/all-notes-off now reaches loaded per-part VST instruments using MIDI
  CC123 and CC120, while preserving FluidSynth panic behavior.

External transport commands may take effect up to one audio block later. This is
intentional and avoids cross-thread mutation of live play state and tempo.

### Musical timing

- Yamaha MIDI time-signature meta event `0x58` is imported.
- The first valid time signature is retained.
- Missing time signatures fall back to 4/4 with a diagnostic warning.
- Bar length now consistently uses:

  `beatsPerBar * PPQ * 4 / beatUnit`

- StyParser, StyleEngine, Transport, Metronome, and playback diagnostics share
  the same denominator-aware timing helpers.
- At PPQ 480:
  - 4/4 = 1920 ticks per bar
  - 3/4 = 1440 ticks per bar
  - 6/8 = 1440 ticks per bar

This fixes waltz and compound-meter styles previously playing with incorrect
section lengths and bar boundaries.

### Yamaha style compatibility

- PatternTransposer has improved explicit CASM `NTR=Guitar` handling using a
  root-shifted voicing anchor.
- Guitar shape and register are preserved more naturally while retaining Stroke
  and Arpeggio color-tone behavior.
- Non-guitar and metadata-free fallback behavior is unchanged.
- Existing Yamaha note limits are still applied after transposition.
- Full Yamaha guitar tables, string assignment, fret tracking, and open-string
  rules remain intentionally unsupported.

### Imported-style diagnostics

The importer now reports:

- Missing time signature and 4/4 fallback.
- Section lengths that are not exact whole bars.
- Unsupported Yamaha RTR modes, deduplicated by mode.
- Unsupported Yamaha Cntt override metadata, once per style.
- Missing or unknown NTR/NTT policies and other existing CASM issues.
- RHY2 channel 9 percussion routing using wording that matches actual playback.

Supported `RTR=PitchShift` does not produce a warning. Unsupported RTR modes
continue using Cadenza's sustained-note pitch shifting; diagnostics do not alter
playback behavior.

### Parser determinism

- Dominant instrument preset selection is deterministic when preset counts tie.
- Equal-count ties choose the latest matching preset.
- A clear highest-count preset is unchanged.

### Drum routing

- RHY1 remains the main drum kit on Cadenza channel 10.
- Yamaha RHY2 on source channel 9 remains on dedicated playback channel 9 for an
  independent mixer strip.
- Other detected percussion routes to the main drum channel.
- Existing XG-to-GM note 31 remapping remains in place.

## Verification Status

Current committed head:

`717632e fix: route transport commands through audio thread`

Latest verification:

- Native Debug target builds successfully.
- Full CTest suite passes: **30/30 tests**.
- Focused tests cover transport commands, time signatures, Yamaha parser
  diagnostics, guitar transposition, drum routing, and preset tie handling.

Build and test from a normal PowerShell session:

```powershell
.\build.bat Cadenza
ctest --test-dir build-msvc --output-on-failure -C Debug
```

Native executable:

```text
build-msvc/Cadenza_artefacts/Debug/Cadenza Workstation.exe
```

## Important Remaining Work

The recent transport change is deliberately only the first ownership slice.
Remaining risks include:

- Transport position reads racing with audio-thread position advancement.
- PPQ, time-signature, and position reset ownership during style replacement.
- Public mutable access to `AudioEngine::transport()`.
- Some blocking mutexes and heavier setup work remain in StyleEngine's audio
  callback path.
- Section playback still needs a section-local phase origin and explicit initial
  tick-zero handling.
- `.cstyle` serialization does not yet preserve every imported Yamaha policy
  field.
- Ctb2 split-range policies, full Yamaha RTR/Cntt behavior, and full Yamaha drum
  and guitar equivalence are not implemented.
- Plugins that ignore both MIDI CC123 and CC120 may still retain stuck notes.

These items should be handled incrementally. Avoid broad arranger or parser
rewrites while the current behavior remains stable.

## Recent Commits

```text
717632e fix: route transport commands through audio thread
c1fc2e7 fix: send panic to per-part VST instruments
386f661 fix: use denominator-aware bar lengths
0b58c29 fix: make dominant preset ties deterministic
b345a36 feat: warn on unsupported Yamaha Cntt overrides
bb74214 feat: warn on unsupported Yamaha RTR modes
14e334b fix: align RHY2 diagnostics with routing behavior
3624147 feat: warn on imported style timing fallbacks
6430424 feat: improve Yamaha guitar transposition behavior
56d9828 fix: preserve imported style time signatures
96ffaf3 fix: stop non-looping songs at ending
dbd3c93 fix: apply song section changes at bar boundary
e39d3e8 fix: make style replacement safe during playback
512daed fix: queue section changes safely across threads
```

For detailed historical notes and specialist investigations, see the other files
in `docs/`, especially `STATUS.md`, `CASM_NOTES.md`,
`YAMAHA_PLAYBACK_POLICY.md`, and `DRUM_PLAYBACK_AUDIT.md`.
