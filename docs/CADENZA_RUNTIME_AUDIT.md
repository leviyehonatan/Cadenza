# Cadenza Runtime Audit

Date: 2026-05-30

Scope: architecture inspection only. No runtime code was changed for this audit.

## Executive Summary

Yamaha `.sty` files still sound like generic messy piano playback because the
runtime has a style note scheduler, but not yet a real arranger workstation
rendering pipeline.

The loader can parse notes, sections, some CASM policy, and representative GM
program names. The playback path then mostly discards the information needed to
sound like a Yamaha arranger:

- no per-channel program/bank setup is sent to the synth
- parsed part instruments are labels only
- style MIDI channels are 1-based in `Style`, but FluidSynth expects 0-based
  channels
- channel 10 drums are therefore likely sent to the wrong FluidSynth channel
- there is no style-part mixer, volume, pan, mute, solo, or per-part routing
- fills/intros/endings exist as named sections, but no arranger section state
  machine exists
- UI mixer and bank controls mostly update visual/application state, not audio
- `.sty` tracks are flattened by channel/section; original MIDI track identity,
  bank select, controllers, and phrase metadata are not preserved

The current system is best described as: **a chord-aware MIDI note looper feeding
one GM synth**, not yet a full arranger runtime.

## Why It Sounds Like Piano

The immediate cause is instrument setup.

`StyParser` reads MIDI Program Change events and uses them to choose
`Part::instrument` text, but that numeric program is not stored on `Part` and is
not sent to `AudioEngine::programChange()`. `StyleEngine::setStyle()` does not
initialize synth channels. `StyleEngine::firePatternNotesAtTick()` only sends
note-on/note-off. With FluidSynth, channels default to the SoundFont preset
state, commonly acoustic grand piano.

There is also a channel-numbering problem. `Part::midiChannel` is documented as
1..16. `StyleEngine` passes that value directly to `AudioEngine::noteOn()`, and
`FluidSynthEngine` passes it directly to `fluid_synth_noteon()`. FluidSynth uses
0..15 channels. That means Cadenza channel 10 is sent to FluidSynth channel 10,
not FluidSynth channel 9, so GM drum-channel behavior is not reliably reached.
Drum notes can therefore play as pitched piano notes.

Even when transposition is improved, if every channel uses the same default
piano preset and no mixer separation exists, a real Yamaha style collapses into
a busy piano texture.

## Subsystem Audit

### 1. FluidSynth Integration

Status: partially implemented.

Working:
- `CMakeLists.txt` conditionally finds FluidSynth.
- Current `build-msvc/CMakeCache.txt` points at vcpkg FluidSynth.
- `SynthEngine.cpp` has a `FluidSynthEngine` with note-on, note-off,
  program-change, CC, all-notes-off, and SoundFont loading.
- A SoundFont exists in `resources/sf2/` and the built resources copy.

Partially implemented:
- FluidSynth renders one stereo buffer through `fluid_synth_write_float`.
- SoundFont loading is automatic for the first `.sf2` found.

Missing:
- no explicit GM system setup
- no channel preset initialization from style data
- no bank select or drum bank setup
- no per-channel gain/pan/mute/solo model
- no error reporting to UI when FluidSynth or `.sf2` is absent

Architectural problems:
- audio API channel convention is unclear; FluidSynth wants 0-based, UI/style
  uses 1-based.
- synth calls are protected by a mutex also used during audio render, which is
  acceptable for a prototype but not ideal for a real-time arranger.

Smallest next step:
- define a single channel convention at `AudioEngine` boundary and convert
  1-based style/UI channels to 0-based FluidSynth channels internally.

### 2. MIDI Channel Routing

Status: partially implemented.

Working:
- hardware MIDI input is opened through `MidiRouter`.
- incoming notes route through chord/melody split logic.
- chord detection updates `StyleEngine`.
- style playback sends notes using each part's `midiChannel`.

Partially implemented:
- `ArrangerMidiRouter` distinguishes chord-side and melody-side notes for chord
  recognition.
- style accompaniment can be enabled/disabled through the Arranger button.

Missing:
- no persistent route table mapping style parts to synth channels
- no MIDI thru policy per part
- no separation between live keyboard parts and accompaniment parts
- no style-channel allocation/remapping layer

Architectural problems:
- raw incoming JUCE channels are 1-based, style channels are 1-based, but
  FluidSynth is 0-based. The code passes values through without a clear contract.

Smallest next step:
- add `AudioEngine` helpers that accept Cadenza 1-based channels and convert
  exactly once before calling synth backends.

### 3. GM Program Changes

Status: partially implemented in parser, not implemented in playback.

Working:
- `StyParser` parses MIDI Program Change messages.
- imported `.sty` parts get a representative GM instrument name.

Partially implemented:
- `AudioEngine` and `SynthEngine` expose `programChange()`.
- `FluidSynthEngine` calls `fluid_synth_program_change()`.

Missing:
- `Part` does not store numeric GM program.
- `StyleLoader` JSON does not persist numeric program or bank values.
- `StyleEngine` never sends program changes for style parts.
- program changes inside a style section are reduced to a label and then lost.

Architectural problems:
- `Part::instrument` is a display string, not a routable preset identity.
- the scheduler is note-only; it has no setup phase for channel state.

Smallest next step:
- add numeric `program`, `bankMsb`, and `bankLsb` fields to `Part`, populate
  them from `.sty`, and send them once when loading/changing style.

### 4. Bank Select Handling

Status: not implemented.

Working:
- `AudioEngine::controlChange()` and `FluidSynthEngine::controlChange()` exist.

Missing:
- parser ignores CC 0 and CC 32 bank select messages.
- style model has no bank fields.
- no bank select messages are sent before program changes.
- UI Bank Memories are not mapped to actual SoundFont bank/program changes.

Fake placeholder:
- `MainComponent::onBankMemoryChanged` contains only a future TODO.

Architectural problems:
- without banks, many XG/Yamaha sounds cannot be selected correctly even if
  program changes are sent.

Smallest next step:
- preserve CC 0/32 per channel in the STY parser and apply bank select before
  program change during style setup.

### 5. Drum Channel Handling

Status: partially implemented in note roles, incomplete at synth routing.

Working:
- parser assigns channel 10 notes `NoteRole::Absolute`.
- docs/tests assert drums stay absolute.

Missing:
- no explicit drum-kit program/bank setup.
- no drum-map support.
- no XG/SFX drum kit handling.
- no guarantee that Cadenza channel 10 reaches FluidSynth zero-based channel 9.

Architectural problems:
- the likely 1-based/0-based mismatch can make drums play as pitched piano.
- drum behavior is treated as note-role only, not channel/preset routing.

Smallest next step:
- fix the channel convention, then explicitly initialize Cadenza channel 10 as
  the GM drum channel before playback.

### 6. Per-Part Instruments

Status: display-only.

Working:
- `.cstyle` and `.sty` parts have `instrument` strings.
- UI has static channel labels and instrument-looking buttons.

Missing:
- no `Part` preset identity beyond text.
- no channel setup from `Part::instrument`.
- no per-section instrument changes.
- no UI reflection of actual loaded style parts/instruments.

Architectural problems:
- `instrument` looks meaningful but cannot drive a synth.
- real Yamaha styles depend on bank/program/channel setup, not just note data.

Smallest next step:
- introduce `PartPreset { bankMsb, bankLsb, program, name }` and use it as the
  runtime source of truth.

### 7. Mixer Architecture

Status: fake placeholder / application-state only.

Working:
- UI renders faders, pan knobs, mute/solo buttons, and animated meters.
- `BridgeRouter` updates `ApplicationState` for volume, pan, solo, mute.

Fake placeholder:
- JS meters are random while playing.
- mixer subtabs are explicitly documented as visual-only.
- volume/pan/solo/mute are not connected to `AudioEngine`, FluidSynth CC, or an
  audio bus mixer.

Missing:
- no per-style-part mixer channels
- no master bus gain
- no per-channel MIDI CC 7/10/11
- no audio metering from actual signal
- no solo logic that mutes other channels

Architectural problems:
- UI channels are melody-performance labels (`left`, `right1`, etc.), not the
  loaded style's accompaniment parts.

Smallest next step:
- create a `RuntimePartChannel` list from the active style and expose it to UI;
  map volume/pan/mute to MIDI CC or a simple synth-channel gate.

### 8. Style Section Architecture

Status: partially implemented.

Working:
- `Style` has sections with names, bar counts, and parts.
- `.sty` parser splits notes by section markers.
- `StyleEngine` plays one current section in a loop.

Partially implemented:
- Intro/Main/Fill/Ending names are parsed when marker text is recognized.
- style memory pads map four hard-coded slots to `mainA`, `mainB`, `intro`,
  `ending`.

Missing:
- no section metadata for Yamaha A/B/C/D variants beyond the flattened names.
- no complexity/variation model.
- no per-section CASM source-channel selection beyond current basic policy
  attachment.
- no runtime section inventory exposed to the UI.

Architectural problems:
- section handling is a loop selector, not an arranger section model.
- some Yamaha markers are collapsed, e.g. Intro A/B/C all map to `intro`.

Smallest next step:
- preserve Yamaha section IDs distinctly and expose active style sections to
  UI as data instead of four hard-coded memories.

### 9. Intro/Main/Fill/Ending Support

Status: partially implemented.

Working:
- parser recognizes common markers: main, intro, ending, fills, break.
- `StyleEngine::setSection()` can switch to a named section immediately.

Missing:
- no intro-to-main transition.
- no fill-to-next-main transition.
- no ending stop behavior.
- no variation up/down.
- no queued section changes at bar boundaries.
- no sync rules for fills or breaks.

Architectural problems:
- immediate section switching calls `allNotesOff()` from UI path and resets
  tick matching, which is musically crude.

Smallest next step:
- add a small `ArrangerSectionState` with current section, queued section, and
  transition policy applied at section/bar boundaries.

### 10. Pattern Switching / State Machine

Status: missing.

Working:
- current section loops by modulo section length.
- transport play/stop works.

Missing:
- no arranger state machine for Intro -> Main -> Fill -> Main -> Ending.
- no next-section queue.
- no chord-change transition rules.
- no retrigger handling.
- no half-bar / fade / sync-start-specific style logic.

Architectural problems:
- `StyleEngine` is a stateless pattern looper with one mutable section string.
  That is too small for real arranger behavior.

Smallest next step:
- implement queued section switching at the next bar/section boundary before
  adding Yamaha-specific transitions.

### 11. Track Muting / Solo

Status: UI/application-state only.

Working:
- bridge accepts mute/solo messages for known UI channel names.
- UI button state updates.

Missing:
- `StyleEngine` does not consult mute/solo state.
- no mapping from UI mixer channels to style parts.
- no per-part note suppression.
- no per-channel all-notes-off when muting.

Architectural problems:
- mixer channel IDs (`left`, `right1`, `melody`) do not correspond to actual
  style part channels (`drums`, `bass`, `part-ch5`, etc.).

Smallest next step:
- add a runtime style-part mixer model keyed by MIDI channel/part ID, then have
  `StyleEngine` skip muted parts and enforce solo.

### 12. Multi-Track Playback vs Merged Piano Playback

Status: partially implemented data split, incomplete playback rendering.

Working:
- imported style notes are grouped into parts by MIDI channel per section.
- `StyleEngine` iterates parts and sends notes on part channels.

Missing:
- original MIDI track identity is not preserved.
- controller events, bank select, expression, pitch bend, and articulation are
  not replayed.
- per-part channel programs are not initialized.
- no part-level audio buses or meters.

Architectural problems:
- because the synth channel state is not initialized, multiple channels can all
  sound like the same default preset. It is technically multi-channel note
  playback, but perceptually it collapses into one piano-like mass.

Smallest next step:
- keep per-channel setup events from the source file and apply them before the
  first notes of each section.

### 13. Dynamic UI Generation From Style Sections

Status: partially implemented for style list, missing for sections/parts.

Working:
- runtime resources define `onFactoryStyles`, `onStyleLoaded`, and
  `onRuntimeState`.
- factory styles are discovered from `resources/factory/styles`.
- the folder button and File -> Open Style are wired.

Partially implemented:
- the loaded style name appears in the parts panel.

Missing:
- no dynamic buttons for actual style sections.
- no UI display of loaded style parts/channels.
- no dynamic mixer generation from loaded style channels.
- style memory pads remain hard-coded to four slots.

Architectural problems:
- the UI visually resembles an arranger workstation, but the runtime data sent
  to it is only style name/id, not the section/part topology.

Smallest next step:
- extend `onStyleLoaded` payload to include sections and parts, then render
  section buttons and a style-part mixer from that payload.

## Comparison With Giglad-Like Visible Behavior

Giglad visibly behaves like a routed arranger workstation:

- styles expose dynamic section controls
- tracks remain separated as playable/mixable entities
- intros, fills, mains, and endings behave as arranger states
- mixer channels correspond to real playable tracks/routes
- instrument assignment is visible and audible
- transport behavior is arranger-aware, not just generic loop playback

Cadenza currently shows a similar-looking UI, but runtime depth is much smaller:

- section controls are hard-coded memories, not generated from loaded style data
- mixer controls are not connected to audio or style parts
- style parts do not drive synth preset setup
- bank memories are UI state only
- pattern switching is immediate loop selection, not arranger transition logic
- there is no style-part route table or mixer bus architecture

That visual/runtime mismatch is why the UI can look close while playback still
sounds unlike a workstation.

## Highest-Impact Next Implementation Steps

1. Fix channel convention at the synth boundary.
   Cadenza should use 1-based channels in UI/style data if desired, but
   `FluidSynthEngine` must receive 0-based channels.

2. Preserve and send program/bank setup.
   Store `bankMsb`, `bankLsb`, and `program` per part/channel during `.sty`
   import, then send CC 0, CC 32, and Program Change before playback.

3. Initialize drums explicitly.
   Ensure style channel 10 reaches FluidSynth channel 9 and select a drum kit.

4. Add a runtime style-part mixer model.
   Generate mixer channels from actual loaded style parts and make mute/solo
   affect `StyleEngine` note emission.

5. Add queued section switching.
   Implement a minimal arranger state machine for current/queued section and
   bar-boundary transitions before tackling full Yamaha fill/ending behavior.

## Bottom Line

The next fix for messy Yamaha playback should not be more CASM transposition
first. The largest audible gap is lower level: **Cadenza is not yet applying
Yamaha/GM channel setup to the synth.** Until bank/program/drum channel routing
and per-part mixer routing exist, even correctly parsed `.sty` note data will
continue to sound like a pile of default piano notes.
