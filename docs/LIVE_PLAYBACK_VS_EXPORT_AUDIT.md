# Live Playback vs Export Audit

Date: 2026-05-30

## Summary

The exported diagnostic MIDI can sound cleaner than live Cadenza playback because
the export path is deterministic and tick-accurate, while live playback is driven
by the audio callback, the current transport state, and live chord state.

The most important finding is that the live transport is not synchronized to the
loaded style's timing metadata. The export path uses `Style::ticksPerBeat`,
`Style::beatsPerBar`, `Style::beatUnit`, and `Style::defaultTempo`; live playback
previously kept the transport's existing PPQ, time signature, BPM, and playhead
position.

Fixed status, 2026-05-30: style load now applies `ticksPerBeat`, `beatsPerBar`,
and `beatUnit` to the runtime `Transport`, resets the transport playhead, clears
active style notes, and sends all-notes-off. The app also applies
`Style::defaultTempo` on load using a simple policy: loading a style sets the
current BPM to the style default.

For Yamaha `.sty` files this is especially important because parsed styles can use
PPQ values such as `480`, while `Transport` defaults to `960`. That makes live
playback scan style ticks at the wrong musical rate even though the exported MIDI
uses the correct PPQ.

## Compared Paths

### Live Playback Path

1. `AudioEngine::getNextAudioBlock()` renders FluidSynth for the current audio
   block.
2. The same callback then advances `Transport`.
3. `StyleEngine::onTick()` scans crossed ticks and fires matching pattern notes.
4. `StyleEngine::firePatternNotesAtTick()` calls `playbackNoteForPart()`.
5. `AudioEngine::noteOn()` maps Cadenza channel `1..16` to synth channel `0..15`.
6. `FluidSynthEngine::noteOn()` receives immediate note-on calls after the block
   has already rendered.
7. Note-offs are managed by `StyleEngine::m_active` tick counters and emitted in
   later audio callbacks.

### Export Path

1. `StyleEngine::exportCurrentSectionDiagnostics()` snapshots current style,
   section, chord, transpose, and octave.
2. `exportPlaybackDiagnostics()` iterates the first 4 bars directly from style
   data.
3. It applies the same `playbackNoteForPart()` note transform as live playback.
4. It writes exact note-on and note-off ticks to `cadenza_playback.mid`.
5. It writes exact event rows to `cadenza_playback_events.csv`.

## Transformations Shared By Both Paths

- Pattern note role handling through `playbackNoteForPart()`.
- Yamaha policy handling through `PatternTransposer`.
- Drum bypass of chord transposition.
- Yamaha/XG drum note remap for known entries such as `31 -> 37`.
- Global transpose and octave, using the context captured at the time of playback
  or export.

## Transformations And State Applied Only During Live Playback

- Audio callback block scheduling.
- Current transport position.
- Current transport PPQ, BPM, and time signature.
- Live chord changes from MIDI input while the style is playing.
- Active note bookkeeping in `StyleEngine::m_active`.
- Existing FluidSynth voice state.
- Any notes still sounding from previous play/section state until note-off or
  all-notes-off is sent.

## Findings

### 1. Live Transport PPQ Is Not Updated From Loaded Style

Status: fixed in the timing synchronization pass.

`Transport` defaults to `960` ticks per beat. `.sty` parsing stores the file's
division in `Style::ticksPerBeat`. The export path writes MIDI using
`style.ticksPerBeat`, but there is no current call from style loading to:

- `Transport::setTicksPerBeat(style.ticksPerBeat)`
- `Transport::setTimeSignature(style.beatsPerBar, style.beatUnit)`

Impact:

- A `.sty` parsed at `480` PPQ exports correctly at `480` PPQ.
- Live playback still advances at `960` PPQ.
- Style tick scanning and note durations are therefore musically mismatched.
- This can make live playback sound rushed, dense, smeared, or unlike the export.

Smallest next step:

- Done. `StyleEngine::setStyle()` applies style PPQ and time signature to the
  audio transport through the shared runtime playback helper.

### 2. Live BPM Can Differ From Export BPM

Status: fixed with a simple style-load tempo policy.

The export MIDI writes tempo from `Style::defaultTempo`. Live playback uses the
current app BPM in `ApplicationState` / `Transport`. Loading a style does not appear
to set the app BPM or transport BPM to the style default.

Impact:

- Exported MIDI may play at the style's intended tempo.
- Live Cadenza may play the same tick data at whatever BPM the UI currently holds.

Smallest next step:

- Done for now. Loading a style sets app/runtime BPM from `style.defaultTempo`.
  This intentionally overrides the previous UI tempo on style load.

### 3. Play Does Not Reset Transport Position

Status: fixed for play-from-stopped.

`AudioEngine::play()` starts the transport but does not reset it. `stop()` stops the
transport and sends all-notes-off, but it also does not reset the transport.

Impact:

- Export always starts at section tick `0`.
- Live playback can resume from the previous tick position.
- A/B comparisons can start at different musical locations.

Smallest next step:

- Done. Starting playback from a stopped transport resets to tick `0`; starting
  while already playing does not reset.

### 4. Live Note Scheduling Is Block-Quantized

Status: likely audible.

`AudioEngine::getNextAudioBlock()` renders FluidSynth first, then advances the
transport and sends note events. This means notes discovered in the callback are not
rendered into the block that just ran. They are heard on the next block.

Additionally, `StyleEngine::onTick()` loops through crossed integer ticks but sends
all events immediately during the callback. It does not provide FluidSynth with
sample offsets inside the block.

Impact:

- Exported MIDI has exact tick timing.
- Live note-ons are quantized to callback boundaries.
- At larger buffer sizes, multiple events that should be separated inside a block
  can be clustered together.
- Notes have at least one-block scheduling latency.

Smallest next step:

- Move scheduling before rendering or add sample-accurate event scheduling into the
  synth render path.

### 5. Live Note-Off Timing Is Block-Quantized

Status: likely audible.

Live note-offs are handled by decrementing `m_active.ticksRemaining` once per audio
callback by `ticksAdvanced`. Expired notes are turned off together in the callback.
The export writes note-off exactly at `tick + duration`.

Impact:

- Live notes can sustain until the next callback after their intended end.
- Multiple note-offs can collapse to the same callback boundary.
- Short drum/percussion notes can sound less tight than the exported MIDI.

Smallest next step:

- Track absolute end ticks and emit note-offs at exact crossed ticks, or use a
  sample-offset event queue.

### 6. Duplicate Active Notes Are Not Detected

Status: missing diagnostic, possible runtime issue.

`StyleEngine::m_active` stores `{channel, note, ticksRemaining}` entries. It does
not check whether the same channel/note is already active before sending another
note-on.

Impact:

- Repeated same-note hits can stack voices in FluidSynth.
- Later note-offs may not correspond one-to-one with the note-ons in a way that is
  easy to reason about.
- The export is a clean MIDI stream; MIDI players may handle overlapping same-note
  events differently than live FluidSynth calls.

Smallest next step:

- Add runtime diagnostics for duplicate active `(channel, note)` entries and
  overlapping same-note note-ons.

### 7. Missing Matching Note-On / Note-Off Detection Does Not Exist

Status: missing diagnostic.

There is no live event recorder at the `AudioEngine` or `SynthEngine` boundary.
Current logs show part setup and some drum remaps, but not a complete note-on /
note-off stream.

Impact:

- Cadenza cannot yet prove whether every live note-off matches a live note-on.
- It cannot compare the live synth stream against `cadenza_playback_events.csv`.

Smallest next step:

- Add a ring-buffer diagnostic stream at the Cadenza-to-synth boundary containing:
  `audioBlockIndex`, `transportTick`, `channel`, `note`, `velocity`, `eventType`,
  `part`, and `activeCount`.

### 8. Chord Changes Are Live, Export Is A Snapshot

Status: expected but important.

Export snapshots the current chord once and renders four bars with that chord.
Live playback reads the current chord every time `firePatternNotesAtTick()` runs.

Impact:

- Chord recognizer transitions, held-note changes, or ambiguous left-hand input can
  alter notes mid-pattern.
- Already sounding notes are not retroactively refit when the chord changes.
- New notes can use a different chord than sustained notes, causing harmonic clash.
- The exported MIDI can sound cleaner because it uses one stable chord context.

Smallest next step:

- Add a diagnostic export mode that records live chord changes and annotates note
  events with the chord used at fire time.

### 9. Section Loop Timing Is Runtime-Only

Status: possible contributor.

Live section looping is done with:

`tickInSection = transportTick % sectionLengthTicks`

Export repeats the section deterministically over the first four bars. If live
transport position is not reset, or if PPQ/BPM differs, the modulo loop will not
line up with the exported event stream.

Impact:

- Live can start in the middle of a section.
- Live can loop at a different perceived musical length if PPQ is wrong.
- Active notes from before a section change can continue unless callers explicitly
  all-notes-off.

Smallest next step:

- Add a diagnostic log on section loop boundaries:
  `transportTick`, `tickInSection`, `sectionLengthTicks`, `activeNotes`.

### 10. Runtime Note Counts Per Frame/Tick Are Not Logged

Status: missing diagnostic.

The requested runtime counters are not currently emitted. Current logs cover:

- style part setup
- drum part diagnostics
- Yamaha/XG remap events when remapped
- synth events only for `NullSynthEngine`

Missing live counters:

- note-ons per callback
- note-offs per callback
- active note count per callback
- duplicate active notes per callback
- maximum active voices per channel
- note-offs with no known active note
- live-vs-export event mismatch

## Why Export Sounds Cleaner

The export sounds cleaner because it is an idealized, deterministic rendering of
the style event list:

- exact PPQ from the style
- exact section start at tick `0`
- exact note-on ticks
- exact note-off ticks
- one fixed chord snapshot
- no audio callback block quantization
- no live chord-recognition transitions
- no prior transport position
- no existing FluidSynth voices

Live playback currently has several runtime-only sources of timing and state
variation:

- transport PPQ/time signature can now be synchronized to the loaded style
- live tempo is now reset to the style default on style load
- Play from stopped now starts at tick `0`
- notes are scheduled after rendering the current audio block
- note-offs are block-quantized
- duplicate active notes are not detected
- chord changes can mutate newly fired notes while older notes continue sounding

The PPQ/tempo/start-position differences are the highest-priority explanation for
large differences. The block scheduling and active-note behavior explain why live
playback can sound less tight even after timing metadata is synchronized.

## Required Runtime Diagnostic Instrumentation

To compare exported note stream vs live synth event stream, add a diagnostic-only
event recorder at the Cadenza-to-synth boundary.

Recommended event shape:

```text
blockIndex
sampleOffset
transportTick
sectionName
tickInSection
eventType noteOn|noteOff|cc|program
channel
note
velocity
partName
sourceNote
playbackNote
role
bankMsb
bankLsb
program
activeCountBefore
activeCountAfter
duplicateActive
missingMatchingNoteOn
```

Recommended checks:

- Compare sorted live note-ons for the first four bars against
  `diagnostics/cadenza_playback_events.csv`.
- Count note-ons per audio callback.
- Count note-offs per audio callback.
- Count active notes per channel.
- Flag duplicate active `(channel, note)` pairs.
- Flag note-offs with no active `(channel, note)`.
- Flag overlapping same-note note-ons within a short tick window.
- Log section loop boundaries.

## Recommended Fix Order

1. Done: synchronize live transport PPQ/time signature with the loaded style.
2. Done: style load sets app BPM from the style default.
3. Done: play from stopped restarts from section tick `0`.
4. Add live event recording and compare it to the exported CSV.
5. Move note scheduling before synth rendering or add sample-accurate event queues.
6. Add duplicate active-note and missing-note-off diagnostics.
7. Decide retrigger policy for overlapping same-note events.
