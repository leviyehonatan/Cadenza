# Cadenza Workstation — Changelog & Current State

> A plain-language record of what was added, what works today, and what
> is still placeholder. Read this when you come back to the project and
> need to remember where things stand.
>
> Complements `README.md` (the forward-looking developer brief) and
> `docs/STATUS.md` (the technical build-state reference).

---

## What you have today, in one sentence

A real, native Windows arranger keyboard application: it opens, plays
sound, listens to a hardware MIDI keyboard, recognises chords, drives
auto-accompaniment in time with your chord changes, remembers your
settings between sessions, and ships with its own Yamaha `.sty` file
converter.

---

## Timeline of what got built

### Custom FL-style piano roll (replaced the vendored one)

The vendored Sjhunt93 grid had no visible/clickable keyboard and a confusing
control panel, so it was removed entirely and replaced with a self-contained
`StylePartPianoRoll` (no external deps):

- A real piano keyboard down the left edge (white/black keys, octave labels),
  click a key to audition the pitch.
- Draw a note by clicking empty grid (drag to set its length); drag a note's
  body to move it (pitch + time); drag its right edge to resize; right-click
  to delete. Mouse wheel scrolls the visible pitch range.
- Everything snaps to a selectable grid (1/4…1/32, or Off), defaulting to
  1/16; a bar/beat ruler and an orange playback marker track the loop.
- Opens on a useful register automatically (GM percussion for drums, the
  playing range for melodic parts). Edits write straight back into the style
  so they're heard on the next loop pass.

`Source/UI/PianoRoll/` (the vendored MIT sources) and its wrapper are gone;
`StylePartEditorWindow` now hosts the new roll with a small grid-snap toolbar.

### Piano-roll part editor — fix recorded notes visually

A visual editor for Style Recorder parts, so you can correct what you played
without re-recording. New **Edit…** button on the recorder row opens a
piano-roll window showing the selected part:

- Drag notes to move/resize, double-click to add, Delete to remove, with the
  usual marquee-select and multi-edit. Notes audition on the part's voice as
  you edit them.
- Every edit writes straight back into the in-progress style and re-bakes
  note roles from the new pitches (move a note to an E and it becomes the
  chord 3rd), so the change is audible on the very next loop pass.
- A playback marker sweeps the grid in time with the looping section.
- Switching the target part (or committing/clearing a take) refreshes the
  open editor automatically.

The grid is vendored from **Sjhunt93/Piano-Roll-Editor** (MIT, JUCE-based) in
`Source/UI/PianoRoll/` — its IGME-specific hooks are already compiled out via
`LIB_VERSION`, and the only local change is swapping the Projucer `JuceHeader.h`
include for the CMake module include. `StylePartEditorWindow` wraps it and
bridges the 480-PPQ grid domain to Cadenza's style ticks. `StyleRecorder`
gained `replacePartNotes` / `targetPartNotes` for the round-trip (covered by
a new test). 32 suites pass.

### Style Recorder — record your own styles (no Yamaha file needed)

The first step away from .sty dependence: styles can now be CREATED inside
Cadenza. New "Style Recorder" row on the panel:

- **New** starts a session: an empty looping section (1/2/4/8 bars) at the
  current tempo/time signature, metronome on. The whole keyboard switches to
  the selected part (no split) so you can play it naturally.
- **Part picker** chooses what you're recording — Drums, Bass, Chord 1/2,
  Pad, Phrase 1/2 — each on its standard SFF channel with the same playback
  policy a Yamaha part would have (bass root-shifts and answers slash
  chords; chords fit the chord quality; phrases follow the root).
- **Record** loops the section and captures what you play, looper-style
  (each pass overdubs). Clicking Record again commits the take: note starts
  quantize to a 1/16 grid, durations are kept, and roles are baked against
  the C-major source convention (root/3rd/5th/7th/color; drums absolute) —
  so the recorded style follows live chords exactly like an imported one.
- **Clear Part** wipes a part; **Save…** writes a `.cstyle` (named after the
  file) and makes it the current style; **Exit** returns to the previous
  style.

Engine plumbing: `StyleRecorder` (pure C++, cadenza_core, 8 new test cases
covering role baking, quantize, loop-wrap durations, overdub merge and save
round-trip) + a MIDI "capture mode" in MidiRouter that bypasses
split/chord/melody routing while recording. 32 test suites pass.

For a future piano-roll editing view, candidate: Sjhunt93/Piano-Roll-Editor
on GitHub (MIT, JUCE-based).

### Full Yamaha chord vocabulary + NTT voicing tables + RTR retrigger rules

The "play any chord, voice it like a Yamaha" pass. One shared table
(`Source/Midi/ChordTypes`) now defines all 34 Yamaha chord types — chord
tones, required fingering tones, per-type chord scale, and 3rd/5th/7th role
intervals — and drives everything:

- **Recognition** (live router + symbol parser): subset matching with Yamaha
  fingering rules replaces the old exact-match 18-template list. Optional
  5ths, reduced voicings, full 9th/13th voicings, m7b5, 6ths, add9s and the
  altered dominants all detect. Ambiguities resolve like a real arranger:
  exact-coverage readings beat omitted-tone readings (D-F-A is Dm, not
  F6-no-5th), then the bass note decides (C-E-G-A = C6 with C bass, Am7 with
  A bass). Extended qualities now reach the engine instead of collapsing to
  plain triads/7ths (`MidiRouter::toCadenzaQuality`).
- **Voicing** (`PatternTransposer`): chord-role notes take the type's own
  intervals (6th chords map 7th-role notes to the 6th, 7b5 bends the 5th,
  7sus4 moves the 3rd to the 4th); color/phrase tones snap to the type's
  chord scale (7(b9) phrases take the b9, half-whole diminished; maj7#11
  takes Lydian; 7(b13) Mixolydian b13; 7aug whole-tone). FingeredOnBass
  slash chords (C/E) drive bass-enabled (`bassOn`) parts to the named bass
  note; other modes keep inversions on the root.
- **RTR retrigger rules** (`StyleEngine::revoiceActiveNotes`): sustaining
  notes across a chord change now follow each part's CASM rule — Stop cuts,
  PitchShiftToRoot/RetriggerToRoot land on the new root, the rest
  re-articulate at the recomputed pitch. This was flagged "unsupported" on
  ~790 of 800 Genos2 styles; only NoteGenerator still falls back.
- **Section length rounding** (`StyParser`): the last section's length (which
  legitimately ends at the final note, not a bar line) rounds to the NEAREST
  bar instead of truncating, and only a genuinely odd length (> 1/8 bar off)
  warns. An ending whose last note stops at 3.7 bars is now 4 bars, not 3.

Verified on the full user library (891 files: Genos2 presets, PSR-S950
Balkan, two Dance packs): 100% playable, 880 fully clean, 0 parse failures,
0 heuristic fallbacks. 31 test suites pass, with new coverage for extended
recognition, altered-dominant scales, slash bass, and RTR warnings.

### v1.0.0 — Auto Fill-In, Fade Out, Release build + distributable package

The "finished product" pass. Three things landed:

- **Auto Fill-In** (new "Auto Fill" toggle, on by default, persisted +
  captured in registrations): pressing a Main button while the band plays
  inserts that main's own fill (`mainB` → `fillBB`) for one pattern and then
  lands on the main — the classic Yamaha AUTO FILL IN behavior. Pressing the
  active main again just plays its fill. Falls back to a plain quantized
  switch when the style has no matching fill. Song mode is unaffected (it
  drives the engine directly).
- **Fade Out** (new "Fade" button next to Play): ramps the master to silence
  over ~8 s on the audio thread (per-sample multiplicative ramp to −60 dB),
  then stops the transport sample-tight and restores the gain; the UI play
  state syncs via the existing 20 Hz timer. Pressing Play during a fade
  cancels it. A clean way to end songs that have no Ending section.
- **Release build + package**: `build-release/` is a Ninja Release tree
  (same vcpkg toolchain); `scripts/package.ps1` collects the exe, runtime
  DLLs, resources (both SoundFonts), the CLI tools, and a user quick-start
  (`docs/QUICK_START.md`) into `dist/Cadenza-<version>/` (~450 MB,
  self-contained — verified to launch from the package folder), with `-Zip`
  for a shippable archive. Project version bumped to **1.0.0**.

Also: settings round-trip test covers the new `autoFillEnabled` flag.
31/31 test suites pass. (Syncro Start/Stop and Tap Tempo, listed as missing
in older docs, were already implemented — docs corrected.)

### Chord voicing fix — nearest-tone placement (no octave scatter)

Chord parts followed the chord but their voicings scattered: each note's pitch
class was snapped into *its own* source octave, so e.g. a 5th could land an octave
below the root/3rd. `PatternTransposer` now moves each chord-tone note to the
**nearest** target tone (within -5..+6 semitones of the source), preserving the
recorded voicing. Updated the transposer tests that encoded the old octave-snap.
Also: `style-probe` can now **render a style section to a MIDI file**
(`exportPlaybackDiagnostics`) for offline listening / A-B against other arrangers.
18 suites pass.

### BYPASS parts now follow the chord (were frozen)

Some melodic style parts didn't follow chords — they were baked `Absolute`
(frozen) because their CASM policy was `NTT=BYPASS` with `NTR=ROOT_FIXED`. But per
the Yamaha SFF spec, **BYPASS means "root shift only"** — the phrase follows the
chord root, it does not freeze. Fixed both the importer (`StyParser::assignRole`:
BYPASS → `ChordColor` for melodic parts) and the live transposer
(`PatternTransposer`: BYPASS → root-transposition for any NTR). Drums (channel 10)
stay `Absolute` as before. Verified on a real style: a frozen `rhythm2` part now
transposes with the chord. Updated the two tests that encoded the old freeze
behavior; 18 suites pass.

### Chord stability on release — no more downgrade to partial chords

Holding e.g. Am and lifting fingers one at a time made the arranger flip to a
weaker partial (power/sus/single) as the note set shrank. Fix in
`ArrangerMidiRouter::detectChord`: a NEW chord now requires enough distinct pitch
classes for the current mode (Single/Multi-finger=1, FingeredIncomplete=2,
Fingered/FingeredOnBass/FullKeyboard=3); if a chord is already held and the set
drops below that, the current chord is kept instead of downgrading. Building a
chord up from nothing is unaffected (no held chord to keep). New test
`releasing_a_held_triad_finger_by_finger_keeps_the_chord`; 18 suites pass.

### MIDI hot-plug + open-all inputs + style tempo display sync

- MIDI keyboard wasn't detected: the app opened only the *first* device once at
  startup, so a keyboard plugged in later (or not first in the list) never opened.
  Now `MidiRouter::refreshInputs()` opens **every** available input and logs the
  device list; an always-on 20 Hz timer in `MainComponent` rescans ~every 2 s for
  **hot-plug**. Confirmed: "Oxygen Pro 49" (all 4 ports) now open.
- Style tempo: `loadAndApplyStyleFile` already set the engine BPM to the style's
  tempo; now the native panel's BPM display is synced too (in
  `updateNativePanelStyle`), so the shown tempo matches the loaded style.
- The song-mode stepping moved onto the same always-on timer (was started/stopped
  per song-mode toggle). 18 suites pass.

### Per-channel instrument picker + role defaults (JJazzLab-style)

Studied JJazzLab's instrument model (`JJAZZLAB_MIXER_INSTRUMENT_STUDY.md`):
RhythmVoice.Type → InstrumentFamily → default GM program. Implemented the same in
Cadenza's native mixer:

- `Source/Midi/GmInstruments` (cadenza_core, tested): `gmInstrumentName(0..127)`,
  `gmFamilyName(0..15)`, and `defaultGmProgramForRole` (bass→33 Fingered Bass,
  chord1→26 Jazz Guitar, chord2→0 Piano, pad→48 Strings, phrase1/2→61 Brass).
- `MixerModel` gained a per-channel `program` (preserved on rebuild; tested).
- Each native mixer strip now has an **instrument button** opening a GM picker
  grouped by family (the drum strip offers GM **drum kits**: Standard/Room/Power/
  Electronic/808/Jazz/Brush/Orchestra). Picking sends a program-change to that
  channel and re-asserts via `applyMixerState` (so section changes don't reset it).
- On style load each strip is **auto-seeded** with the style's own program, or — if
  the part has none — its **role default** (so bass gets a bass, pad gets strings,
  etc. automatically). 18 test suites pass.

### .sty importer cleanup — Yamaha SFF channel layout (fixes "messy")

Built `tools/style-probe` (prints what each part actually plays: source pitch →
played pitch + role) to diagnose messy playback. It proved the transposition is
correct at the home chord (identity at C) — the mess was the **importer**:

- A real `.sty` was producing 9 parts including spurious **MIDI ch 1–2** duplicate
  "bass" parts (Yamaha accompaniment only lives on **MIDI channels 9–16**). Fix:
  per section, if any 9–16 style part exists, drop the 1–8 parts. Simple/test
  styles with only low channels are unaffected (so the parser test-suite still
  passes).
- Parts are now labelled by the standard SFF role:
  9=rhythm2, 10=drums, 11=bass, 12=chord1, 13=chord2, 14=pad, 15=phrase1,
  16=phrase2 (was "four bass parts").
- **Intro/Ending A/B/C** previously all collapsed to one id, so picking "Intro"
  played the tiny first one. They now get distinct ids (intro/introB/introC,
  ending/endingB/endingC) with matching native section buttons.
- Verified on real styles: `mainA` 9→7 clean parts; sections de-duplicated.
  17 suites pass.

### Instrument fix from Giglad's voice table — GM bank for melodic parts

Studied a reverse-engineering dump of the real Giglad.exe (analysis docs + its
`msb_lsb_pgm.dat` voice table). Key factual insight from that table: it is
`MSB,LSB,PGM,Category,BankType,VoiceName`, and the **PGM column is the General
MIDI program number** — Yamaha XG voices are GM-program-aligned, with MSB/LSB only
selecting *variations* of that GM program.

That explained wrong instruments from `.sty` files: parts send Yamaha variation
bank-selects (bank LSB 112/117/19…) that a GM SoundFont (GeneralUser GS) doesn't
contain, so FluidSynth loaded the wrong/last preset.

- Fix (in the tested `playbackSetupForPart`): melodic parts now force **GM bank 0**
  and rely on the program number (the correct GM instrument family); drum parts
  keep their bank so percussion mapping still works. Verified in the log: real
  `.sty` melodic parts now report `bankMsb=0 bankLsb=0`.
- Also gave the factory `8-beat-pop.cstyle` real GM programs (bass 32, harmony 0)
  — it previously had none, so bass defaulted to piano.
- No Giglad proprietary data was copied into Cadenza; only the factual XG↔GM
  insight was used. `tests/RuntimePlaybackTests.cpp` gains GM-bank/drum-bank cases;
  17 suites pass.

### Diagnosis: file logging + live-melody/style channel-collision fix

Added a `juce::FileLogger` (Main.cpp) writing a fresh `%APPDATA%/Cadenza/cadenza.log`
each launch, plus an audio-device status line in `AudioEngine::startAudioDevice`
(name / open / sampleRate / buffer / output channels). This made the engine's real
behaviour readable after the fact.

The log immediately exposed a real bug: the live-melody voice was hard-pinned to
**Cadenza channel 1**, but a loaded Yamaha style (`C.ARDAS`) uses channel 1 for its
**bass** part. `applyMelodyProgram()` then overwrote channel 1 with the melody
program (Trumpet 56), so the style's bass played as a trumpet and the right hand
collided with the bass.

- Fix: the live-melody channel is now **chosen dynamically** to avoid the loaded
  style's channels (lowest free channel in 1–16, never the drum channel 10). On
  the `C.ARDAS` style it moves to channel 2. `LiveMelodyVoice::setChannel` +
  `MidiRouter::setMelodyChannel`; `applyMelodyProgram` / the mixer "Melody" strip
  now use the chosen channel. Audio path itself was healthy (FluidSynth +
  GeneralUser GS, device open at 96 kHz).
- 17 suites still pass.

### Native arranger — phase 2: mixer + pads

- **Mixer.** A pure, unit-tested `cadenza::audio::MixerModel` (volume/mute/solo
  with proper solo-override logic + `effectiveVolume`) drives a row of native
  vertical strips — one for live **Melody** plus one per distinct style channel
  (Drums/Bass/Harmony/…), labelled from the style's parts. Each strip has a
  volume fader and M/S buttons. Volume/mute/solo send MIDI **CC7** per channel via
  `AudioEngine::controlChange(effectiveVolume)`. Strips seed from the style's own
  part volumes on load, and the mixer is **re-asserted after each section change**
  (the style re-sends its channel volumes on section setup, so the mixer wins).
- **Pads.** Four native trigger pads fire one-shot GM percussion hits
  (crash / hand-clap / tambourine / cowbell) on the drum channel.
- Layout: mixer band sits above the on-screen keyboard; pads row above the
  section buttons. `tests/MixerModelTests.cpp` covers mute, solo overrides,
  multi-solo, clamping, and state-preserving rebuilds. 17 suites pass.

### Native arranger — phase 1: full window, tempo, on-screen keyboard

Expanded the native panel into the primary full-window arranger; the WebView now
starts hidden and is toggled with a "Web UI" button (it keeps loading in the
background so the toggle is instant and bridge mirroring still works).

- Added **Tempo −/+** with a BPM readout → `AudioEngine::setBpm` directly.
- Added a native **on-screen keyboard** (`juce::MidiKeyboardComponent`) across the
  bottom. Its notes go through the new `MidiRouter::injectNote`, which runs the
  SAME path as a hardware key: split routing, chord detection below the split, and
  the live melody voice above it (octave + dedicated channel). So you can set
  chords and play melody natively, no web piano needed.
- `MidiRouter::injectNote` is thread-safe and shares the chord/melody state with
  the hardware path; `m_lastChordName` updates are now guarded on both paths.
- Layout: native panel fills the window when the web UI is hidden; when shown they
  split (native left, web right).
- Still 16 suites pass; app builds and launches. (Mixer + pads are the next phase.)

### Native control panel — core live controls no longer need the WebView

Added a native JUCE control surface (`Source/UI/NativePanel`) docked on the left
of the window; the WebView stays visible on the right but is now secondary. Every
native control calls the C++ engines **directly** — no JS bridge — eliminating
bridge latency/bugs for performance controls.

- Controls: Play/Stop, Open Style, Open SoundFont, style-name label, large live
  chord display, Transpose −/+, Octave −/+, and toggles for Arranger, Chord
  Memory, Syncro Stop, and Chord-on-Bass (Fingered/FingeredOnBass).
- **Section buttons are generated from the loaded style** via the new pure
  `cadenza::arranger::sectionButtonsForStyle` (Intro / Main A–D / Fills / Break /
  Ending, in order, only those present). Pressing one calls
  `StyleEngine::setSection` directly and highlights the active section.
- The panel is the source of truth: its callbacks run `m_audio` / `m_midi` /
  `m_styleEngine` directly (Octave → `MidiRouter::setLiveOctave` live-melody only;
  Transpose → `StyleEngine::setGlobalTranspose`; sections, arranger on/off, chord
  memory, syncro stop, fingered-on-bass). It mirrors values back to the WebView
  for display, and updates live on style-load, chord changes, and play state
  (the last two marshalled from the MIDI thread to the UI thread).
- Pure controller logic is unit-tested: `tests/SectionButtonsTests.cpp`
  (ordering, filtering, labels, unknown-section handling). 16 suites pass; app
  builds and launches with the panel.

### Octave UI wiring fix — on-screen piano now obeys Octave

The backend `LiveMelodyVoice` was correct and the hardware-MIDI path applied
Octave, but pressing Octave +/- in the app didn't change the audible right-hand
pitch. Root cause: the **on-screen piano** sends its notes via the bridge
`noteOn`/`noteOff` messages straight to `AudioEngine`, **bypassing `MidiRouter`
and the live melody voice entirely** — so the Octave shift (and the melody
channel/instrument) never applied to UI-played notes. (The Octave→`setLiveOctave`
hook itself was wired correctly.)

- Added `MidiRouter::handleVirtualMelodyNote(note, vel, isOn)` — a thread-safe
  method that runs an on-screen/virtual note through the same `LiveMelodyVoice`
  (above-split = melody: Octave shift, dedicated channel, matched note-off)
  **without** touching chord detection. Below-split notes return nullopt and are
  sounded directly, unshifted.
- `MainComponent`'s `onNoteOn`/`onNoteOff` bridge hooks now go through that method,
  so the on-screen piano behaves like a hardware key for Octave + instrument.
- The shared voice's per-note state is now updated under `MidiRouter`'s publish
  mutex (hardware MIDI thread + UI message thread both serialise through it).
- Diagnostics logs added per request: UI octave-button click (JS console),
  `onOctaveChanged → setLiveOctave` (+ read-back), and UI melody note orig/shifted/
  channel/octave.
- Tests: explicit BridgeRouter octave-routes-absolute-value test; a
  `LiveMelodyVoice` test modelling the acceptance scenario (C5 sounds 72 → 84 → 60
  as Octave goes 0 → +1 → −1). 15 suites pass.

### Live right-hand melody — dedicated voice, instrument & working Octave

The right-hand live melody now behaves like a real arranger voice instead of a
single robotic piano shared with everything.

- New pure `cadenza::midi::LiveMelodyVoice` (cadenza_core, unit-tested):
  - melody-zone (above split) notes sound on a **dedicated melody channel**
    (`cadenza::audio::kLiveMelodyChannel` = Cadenza ch 1, away from drum ch 10);
  - the live **Octave** shift is applied to the sounded note (clamped 0..127);
  - it **remembers the exact pitch played per source note**, so the matching
    note-off releases that same pitch even if Octave changed mid-hold (fixes the
    stuck-note bug where note-off used the original pitch);
  - **chord-zone (left-hand) notes make no melody sound** — they only drive chord
    detection.
- `MidiRouter` now routes each note first, then plays only melody notes through
  the voice, and logs `orig/shifted/channel/octave` per live note.
- **Instrument picker wired:** the "Bank Memory" voice grid (16 voices) was a
  disabled placeholder; its buttons now set the live melody GM program via
  `gmProgramForBankName` → `programChange` on the melody channel. Persisted as
  `melodyProgram` in settings; defaults to Acoustic Grand Piano. The program is
  re-asserted on startup and after loading a style (in case the style used ch 1).
- Octave still does NOT affect style parts or chord detection (unchanged from the
  previous fix). New `tests/LiveMelodyVoiceTests.cpp` (8 cases). 15 suites pass.

### Octave routing fix — Octave no longer moves the style

The Octave up/down control used to be wired into `StyleEngine` (via
`setGlobalOctave` → the playback `TransposeContext`), so changing octave shifted
the whole accompaniment — drums excepted, but bass/harmony jumped. Correct
arranger behaviour: Octave is a **live right-hand** feature only.

- `StyleEngine` no longer has any octave concept (`setGlobalOctave`/`m_globalOctave`
  removed). Its playback context is now built by the new pure helper
  `makeStylePlaybackContext(chord, key, transpose)` which forces `globalOctave = 0`.
  Transpose is unchanged.
- `MidiRouter` gained `setLiveOctave(n)`; it routes each incoming note first, then
  shifts **only melody-zone (right-hand)** notes by `n` octaves (clamped) before
  sounding them. Chord-zone notes and the chord recogniser still see the original
  pitch, so octave never affects chord detection or accompaniment.
- `MainComponent::onOctaveChanged` now calls `m_midi.setLiveOctave(...)` instead of
  the style engine.
- New `tests/OctaveRoutingTests.cpp` (6 cases): style context never carries octave;
  bass/harmony/drums are unmoved by octave; live melody note shifts by octave
  (with clamping); transpose still moves style parts. 14 suites pass.

### VST3 hosting — master insert effect

Cadenza can now host a VST3 plugin as a master insert effect on the arranger
output (e.g. a GuitarML **NeuralPi** amp/cab sim over the whole mix).

- Uses JUCE's bundled `juce_audio_processors_headless` module (VST3 SDK
  included) with `JUCE_PLUGINHOST_VST3=1` — hosting only, no plugin editor UI.
- `Source/Audio/PluginHost` — thread-safe wrapper around
  `AudioPluginFormatManager` + `VST3PluginFormatHeadless`: load/prepare/clear on
  the message thread, lock-free `process()` on the audio thread (skips a block
  rather than blocking while a load/clear is mid-flight).
- `AudioEngine` runs the mixed output buffer through the loaded plugin as step 4
  of `getNextAudioBlock`; exposes `loadMasterEffect`/`clearMasterEffect`/
  `masterEffectName`.
- UI: File menu → **Open VST3 Effect** / **Clear VST3 Effect**; an "FX:" status
  field in the header shows the loaded plugin. New bridge messages
  `openPluginFile` / `clearPlugin` (router cases + hooks + tests).
- `tools/plugin-probe` — a headless CLI that loads a `.vst3` and pushes a block
  through it. **Verified on the bundled `NeuralPi.vst3`**: enumerates as a
  GuitarML "Fx" (2 in / 2 out), instantiates, and produces non-silent output
  from an impulse. (`maidi.vst3` shipped as a bare DLL without a discoverable
  VST3 factory, so it doesn't enumerate — a packaging issue with that file, not
  the host; NeuralPi proves the path works.)
- 12 test suites pass; app builds and launches with the host wired in.

### Song mode — auto-stepping chord charts (Giglad-style)

You can now load a `.csong` chord chart and have the arranger walk it
automatically: as the transport crosses each bar, the active style section
and the live chord change to match the chart, exactly like a hardware
arranger's song/chord-sequencer mode.

- `parseChordSymbol()` (in `ChordRecognizer`): turns chord-chart text
  ("Am", "G7", "Cmaj7", "F#m7b5", "Bb", "C/E") into a `Chord`. Inverse of
  `Chord::toString`; extended chords collapse to their base quality.
- `SongPlayer` (pure `cadenza_core`): given a `Song` + a 1-based bar, reports
  what section/chord to apply, suppressing duplicate changes, with optional
  looping. Fully unit-tested (8 cases).
- Wired into the app: `MainComponent` runs a 30 Hz message-thread timer that
  reads the transport bar and drives `SongPlayer` → `StyleEngine::setSection`
  / `setChord`. Tapping the **SONG** slot opens a `.csong` chooser, loads the
  chart (and its referenced style), and turns song mode on; tapping again (or
  using a Style-Memory pad for manual section control) turns it off. New
  bridge messages `openSongFile` / `songMode` (router cases + hooks + tests).
- 12 test suites pass; the app launches and runs without regressions.

### Style-playback fidelity fix (chord-following colour tones)

The `.sty` importer used to tag every melodic source note that wasn't a
plain chord tone (root/3rd/5th/7th) as `absolute`, and `PatternTransposer`
never moves `absolute` notes. The result: 6ths, 9ths, suspensions and
passing tones in guitar/piano/string phrases stayed frozen at their
recorded pitch while the chord changed — the "messy playback" symptom.

- Added `NoteRole::ChordColor`: a non-chord source tone that follows the
  chord by **root transposition** (shift by the folded chord-root delta,
  −5..+6 semitones), preserving the recorded voicing/interval.
- `StyParser` now emits `ChordColor` for melodic colour tones instead of
  `absolute` (drums stay `absolute`).
- Honoured Yamaha NTR for `BYPASS`: `ROOT_TRANSPOSITION + BYPASS` now
  shifts by the root delta (per JJazzLab/YamJJazz semantics); only
  `ROOT_FIXED + BYPASS` stays frozen.
- Verified on the real `CULY.STY`: chord-following notes rose 82 → 891 and
  frozen melodic notes dropped 1135 → 326 (the remainder are the
  legitimately-fixed Root-Fixed+Bypass parts); 0 parts left with stranded
  colour tones. All 11 test suites pass (4 new transposer cases).

### Before this session

- An HTML/CSS/JS prototype of the UI (`Cadenza Workstation.html`,
  `css/*`, `js/workstation.js`). It rendered all four tabs and had
  working knobs/faders/buttons, but did nothing real — every
  interaction just `console.log`-ed via a stub bridge.
- The previous session had scaffolded a basic JUCE host
  (`MainComponent`, `BridgeRouter`, `ApplicationState`) so the .exe
  opened and routed a few message types, but there was no audio, no
  MIDI, no chord detection, no style engine, no persistence.

### Added this session — the C++ engine

These all live in `cadenza_core` (pure C++20, no JUCE dependency, fully
unit-tested):

| New file | What it does |
|---|---|
| `Source/Json/Json.{h,cpp}` | Hand-rolled minimal JSON parser + serializer. No external dependency. |
| `Source/Midi/ChordRecognizer.{h,cpp}` | Pitch-class-set chord detection. Recognises maj / min / 7 / maj7 / m7 / dim / dim7 / m7b5 / aug / sus2 / sus4 / power / single notes. Slash-chord aware via bass note. |
| `Source/Arranger/Style.{h,cpp}` | Style data model (sections, parts, pattern notes with chord-relative roles). |
| `Source/Arranger/StyleLoader.{h,cpp}` | `.cstyle` JSON load + save (round-trippable). |
| `Source/Arranger/PatternTransposer.{h,cpp}` | Maps each `PatternNote` to an absolute MIDI note given the live chord, key, transpose, and octave. |
| `Source/Audio/Transport.{h,cpp}` | Pure-C++ PPQ clock. Sample-rate aware. Bar/beat rollover. Used by audio thread. |
| `Source/Settings/SettingsStore.{h,cpp}` | Reads/writes a JSON settings file in `%APPDATA%/Cadenza/`. |
| `Source/Arranger/Song.{h,cpp}` | Song data model (events: bar + section + chord). |
| `Source/Arranger/SongLoader.{h,cpp}` | `.csong` JSON load + save. |

### Added this session — the JUCE-bound layer

These pull in JUCE / OS APIs and live next to `Main.cpp`:

| New file | What it does |
|---|---|
| `Source/Audio/SynthEngine.{h,cpp}` | Abstract synth interface + two implementations: `NullSynthEngine` (logs notes, silent) and `FluidSynthEngine` (real audio via FluidSynth). Conditional compile via `CADENZA_HAVE_FLUIDSYNTH`. |
| `Source/Audio/Metronome.{h,cpp}` | Click track. Sine-wave envelope, accented on bar 1. Audio-thread-safe. |
| `Source/Audio/AudioEngine.{h,cpp}` | Top-level `juce::AudioSource`. Owns synth + transport + metronome. Drives the audio device. Exposes a tick-callback for the style engine. |
| `Source/Midi/MidiRouter.{h,cpp}` | `juce::MidiInputCallback`. Opens a hardware MIDI device. Splits notes between chord zone (below C4) and melody zone. Feeds chord-zone notes to `ChordRecognizer`. Emits chord-change events to both the web UI and the style engine. |
| `Source/Arranger/StyleEngine.{h,cpp}` | Orchestrates style playback. Subscribes to the audio engine's tick callback. On every tick, fires any pattern notes whose tick matches the current position within the section, transposing them via the current chord. Loops the section when its bar count is reached. Handles section switching and chord changes lock-free. |

### Modified this session — what was there before

- `Source/BridgeRouter.{h,cpp}` — added a `BridgeHooks` struct with
  `std::function` callbacks for every message type the web UI can emit.
  When a message arrives, the router updates `ApplicationState` *and*
  fires the matching hook so the JUCE app can route the message into
  audio / MIDI / style code.
- `Source/MainComponent.{h,cpp}` — instantiates `AudioEngine`,
  `MidiRouter`, `StyleEngine`, and `SettingsStore`. Wires hooks
  bidirectionally. Auto-loads `resources/factory/styles/8-beat-pop.cstyle`
  on startup. Auto-discovers any `.sf2` in `resources/sf2/` and loads
  it into the synth. Saves settings on quit.
- `CMakeLists.txt` — added all the new source files. Added 7 new test
  targets. Added optional `find_package(FluidSynth)` with a clean
  fallback when not available. Added the `sty-to-cstyle` tool target.

### Added this session — tests (all hand-rolled, no external test framework)

| Test suite | Cases | Time |
|---|---|---|
| `cadenza_core_tests` | 5 | BridgeRouter routing + clamping + invalid input |
| `cadenza_json_tests` | 5 | JSON parse/serialize round-trip, escapes, errors, pretty |
| `cadenza_chord_tests` | 8 | Triads, 7ths, power, dim, aug, single notes, slash chords |
| `cadenza_style_tests` | 4 | Load valid JSON, role string round-trip, save & reload, reject malformed |
| `cadenza_transpose_tests` | 8 | All 5 role types in multiple keys; major/minor modes; out-of-range clipping |
| `cadenza_transport_tests` | 7 | PPQ math; stopped vs playing; bar/beat rollover; BPM change; reset |
| `cadenza_settings_tests` | 3 | Defaults on missing file; full round-trip; corrupt file handled |
| `cadenza_song_tests` | 4 | Load valid, `eventForBar` precedence rule, round-trip, malformed |
| `cadenza_sty_tests` | 6 | In-memory synthetic SMF parse: marker mapping, section splitting, role heuristic, drum-channel handling |
| **Total** | **44 cases across 9 suites** | All pass in ~1.5 sec |

### Added this session — the Yamaha `.sty` converter

A standalone command-line tool that reads any Yamaha `.sty` file (or a
plain `.mid` file) and produces a Cadenza `.cstyle`:

```
tools/sty-converter/
├── StyParser.h
├── StyParser.cpp        ← parses MThd + MTrk chunks, detects section markers,
│                          groups notes by channel, applies role heuristic
├── main.cpp             ← CLI: sty-to-cstyle in.sty out.cstyle [--name ...]
└── StyParserTests.cpp   ← synthetic-MIDI test (no external file dependency)
```

The compiled binary lives at `build-msvc/sty-to-cstyle.exe`.

### Added this session — scripts and docs

| File | Purpose |
|---|---|
| `build.bat` | Initialises MSVC environment (`vcvars64.bat`) then calls `cmake --build`. Solves the "cl.exe not on PATH" problem in plain PowerShell. |
| `scripts/setup-sound.ps1` | Automates the full FluidSynth path: clones vcpkg if missing, installs fluidsynth, reconfigures CMake with the vcpkg toolchain, rebuilds Cadenza, tells you where to put a SoundFont. |
| `docs/STATUS.md` | Project status reference. File layout, build/test commands, known limitations. |
| `docs/SOUND_SETUP.md` | Step-by-step guide for getting audio working. Manual fallback if the script doesn't fit your setup. |
| `docs/STY_CONVERSION.md` | How to use the `.sty` converter, including the role-assignment heuristic and what is/isn't legally OK to convert. |
| `CHANGELOG.md` | This file. |

### Factory content shipped with the build

- `resources/factory/styles/8-beat-pop.cstyle` — a small 1-bar drums +
  bass + harmony pattern in 4/4 with `mainA`, `mainB`, `intro`, `ending`
  sections. Deliberately bare-bones; replaceable.
- `resources/factory/songs/demo.csong` — example song with 4 chord
  changes (C / F / G / C) showing the `.csong` event format.

---

## What works end-to-end *right now*

Confirmed by you in this session, in order:

1. ✅ The `.exe` builds clean (25.9 MB).
2. ✅ All 9 test suites pass (44 cases, ~1.5 sec total).
3. ✅ The .exe launches and shows the full web UI inside a native window.
4. ✅ FluidSynth is linked and the SoundFont loads.
5. ✅ Clicking the on-screen piano produces audible sound (piano timbre).
6. ✅ Pressing Play starts the factory style — drums + bass + harmony loop.
7. ✅ Settings persist between sessions (`%APPDATA%/Cadenza/settings.json`).

Confirmed by the code (not by you yet — you can try these):

8. ✅ Hardware MIDI keyboard input: notes light up the on-screen piano
   AND play through the synth.
9. ✅ Chord recognition: hold a chord with your left hand → the detected
   chord name appears in the title-bar status.
10. ✅ Auto-accompaniment chord-following: the bass + harmony in the
    looped pattern transpose live to follow whatever chord you hold.
11. ✅ Section switching: clicking the Style Memory pads 1–4 in the UI
    switches between mainA / mainB / intro / ending.
12. ✅ The `.sty` converter accepts any Yamaha `.sty` or plain `.mid`
    file and produces a valid `.cstyle`.

---

## What is still placeholder / not yet wired

These are not bugs; they're deliberately deferred to keep this build
focused.

- **The factory style is intentionally minimal** — it's a 1-bar 8-beat
  with the simplest possible drum/bass/harmony. It exists to prove the
  pipeline works, not to sound impressive. Replace
  `resources/factory/styles/8-beat-pop.cstyle` (or convert any `.sty`
  via the converter) to get a richer sound.
- **No VST3 plugin hosting yet** — JUCE provides the `AudioPluginFormatManager`,
  and the architecture allows for it, but no `PluginHost` class is implemented.
- **Style Editor tab is visual-only** — you can navigate it, but the
  edit canvas is a placeholder. Saving styles from the UI requires
  wiring `cadenza::arranger::saveStyleToFile` to a bridge message.
- **No auto-stepping through `.csong` files** — `SongLoader` parses the
  format correctly and `Song::eventForBar` returns the right event for
  any bar, but no `SongPlayer` class reads those events on each new bar
  to drive `StyleEngine::setChord` / `setSection` automatically. A
  ~30-line addition.
- **No MIDI device picker in the UI** — `MidiRouter::availableInputs()`
  returns the list of attached devices; the UI just opens the default
  one. A dropdown could be added in the Style Editor tab.
- **VU meters animate with `Math.random()`** — they're decorative until
  real audio levels are fed from C++ back through the bridge.
- **Yamaha CASM / CSEG chunks ignored** — the `.sty` converter parses
  only the Standard MIDI File portion and applies a "source chord is C
  major" heuristic. For most user-created styles this works; for
  hardware-bundled styles with complex per-channel transposition rules,
  the converter gives a reasonable starting point that you can tweak
  by hand.

---

## How to do everything you need

### Build (after any code change)

```powershell
cd "C:\Users\suko5\Desktop\arranger workstation inspired by giglad"
.\build.bat
```

### Run the app

```powershell
& "C:\Users\suko5\Desktop\arranger workstation inspired by giglad\build-msvc\Cadenza_artefacts\Debug\Cadenza Workstation.exe"
```

### Run the tests

```powershell
cd build-msvc
ctest --output-on-failure
```

### Convert a `.sty` (or `.mid`) into a Cadenza style

```powershell
.\build-msvc\sty-to-cstyle.exe `
    "C:\path\to\some.sty" `
    "resources\factory\styles\my-new-style.cstyle" `
    --name "My New Style" `
    --id my-new-style
```

Then either rebuild (`.\build.bat`) so the file is auto-copied next to
the `.exe`, or manually drop it into
`build-msvc\Cadenza_artefacts\Debug\resources\factory\styles\`.

### Reset the trial-rollback... wait, wrong project. Move on.

---

## Known operational tips

1. **After adding a SoundFont, you must rebuild OR manually copy.** The
   CMake `POST_BUILD` step copies `resources/` next to the `.exe` only
   when the target is being built. Dropping a `.sf2` into
   `resources/sf2/` does nothing on its own — either run `.\build.bat`
   (a few seconds since nothing recompiles) or drop the file directly
   into `build-msvc\Cadenza_artefacts\Debug\resources\sf2\`.
2. **The compatibility shim in `MainComponent.cpp`** adapts JUCE 8's
   `__JUCE__.backend.emitEvent(...)` API to the older
   `window.__juce__.postMessage(...)` API the JS uses. If you ever
   modernise the JS, you can delete the shim.
3. **FluidSynth detection happens at CMake configure time.** If you
   install FluidSynth *after* configuring, you need to wipe
   `build-msvc/CMakeCache.txt` and re-run cmake configure with the
   vcpkg toolchain flag. `scripts/setup-sound.ps1` does this for you.
4. **Settings live in `%APPDATA%/Cadenza/settings.json`.** Delete that
   file if you want to reset to defaults.
5. **Logs go to JUCE's logger.** On Windows that's `OutputDebugString`,
   visible in DbgView or in the Visual Studio Output window during a
   debugger session. There is no log file by default.

---

## What "done" means for this project

This was never going to be a polished commercial release in one session
— but for **personal use**, the engine is now complete enough that you
can:

- Play and hear backing patterns in real time
- Control them live with a MIDI keyboard
- Add new styles by converting `.sty` files
- Customise everything (the UI is HTML/CSS/JS, the engine is C++, the
  formats are JSON)

The remaining items in the "placeholder" list above are all enhancements
on top of a working foundation. None of them block daily use.

---

*Last updated: end of the big build-out session.
For the forward-looking architecture/spec, see `README.md`.
For the up-to-date build-status reference, see `docs/STATUS.md`.*
