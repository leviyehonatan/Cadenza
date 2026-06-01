# Cadenza Workstation — Implemented Status

> What actually works **today** in the built application. This supersedes the
> roadmap framing in `README.md` (which was written before the C++ engine
> existed). For the chronological list of changes see `CHANGELOG.md`.

_Last updated: 2026-06-01._

---

## 1. Summary

Cadenza is now a **working software arranger keyboard**, not just a UI mockup.
You can load a Yamaha `.sty` style, play chords with your left hand on a MIDI
keyboard (or the on-screen keyboard), and a styled backing band (drums, bass,
chord/comp, pad, sub-rhythm) plays in real time and follows your chords. You
play melody with your right hand on a dedicated voice.

There are two UIs:
- **Native JUCE control panel** — the primary live surface (transport, tempo,
  style, chord display, transpose/octave, mixer strips with per-channel
  instrument pickers, pads, on-screen keyboard).
- **WebView UI** — secondary, can be toggled.

---

## 2. What works

### Audio + MIDI
- FluidSynth SoundFont (`.sf2`) playback. Configurable SoundFont path.
- Real-time audio engine with a PPQ transport clock; events fire before render.
- **MIDI input**: opens all connected devices and hot-plugs new ones (rescans ~2s).
- On-screen keyboard injects notes through the same route as hardware.
- **Split point** (default C4 / MIDI 60): below = chord zone, above = melody.

### Arranger engine
- Parses Yamaha playback policy fields into explicit NTR/NTT enums, including
  SFF1/Ctab NTT byte 3 as Melody + bassOn. `bassOn`, `noteLowLimit`,
  `noteHighLimit`, and `chordRootUpperLimit` are preserved and covered by
  focused tests.
- Loads Yamaha **`.sty`** (SFF) directly, and our `.cstyle` JSON format.
- Parses CASM/Ctab/Ctb2 policy, channels 9–16 mapped to the standard SFF
  accompaniment parts (sub-rhythm, drums, bass, chord1/2, pad, phrase1/2).
- **Chord recognition** from held left-hand notes (18 templates; multiple modes).
  Stable on finger-by-finger release (won't downgrade a held chord).
- **Chord following / transposition** per part by baked note roles
  (root / 3rd / 5th / 7th / color / scale / absolute).
- **Held notes re-voice instantly on chord change** (pads/strings shift with you,
  no waiting for the loop).
- **Live melody voice** on its own channel with octave control.
- Section switching (intro / main A–D / fill / break / ending).
- Per-channel **mixer**: volume, mute, solo, and instrument (program) pick.
  Bass/drums/etc. get sensible role-based default instruments.

### Tooling
- `style-probe` — loads a `.sty`/`.cstyle`, prints per-part source→played pitch
  and role for a given chord, **and renders the section to a `.mid`** so you can
  audition the raw arrangement in any player.
- `plugin-probe` — loads/inspects a VST3.
- `style-probe` also shows playback channel/percussion flag, shared-channel
  setup owner, and stable independent note names in range/output lines.
- Diagnostic log at `%APPDATA%/Cadenza/cadenza.log` (fresh each launch).
- 20 unit-test suites (chord recognition, transposition, parsing, playback,
  and focused style-probe diagnostics).

### Sound-quality fixes already in (CULY-ext was the test style)
- Yamaha NTR/NTT policy now guides the transposition path: RootFixed/Bypass can
  stay absolute, RootTransposition follows root movement, and chord/melody modes
  use the existing chord-tone fitting rather than one generic transposition.
- **Sub-rhythm / rhythm2 percussion routes to GM drum channel 10**, while the
  main drums part owns channel-10 bank/program setup when both share the channel.
- Chord-tone **voicing** uses nearest-tone placement (no octave scatter).
- A 7th note over a plain triad folds to the root (no accidental Am7-sounds-like-C).
- **Sub-rhythm / drum-bank parts are treated as percussion** and never pitched.
- **Pad** defaults to a tighter, faster synth-strings patch with reverb/chorus cut.
- **Bass** is anchored to one consistent low octave (E1–Eb2) so the foundation
  never jumps register or drops near-inaudible.

---

## 3. Build & run (Windows)

Requires Visual Studio 2022/Build Tools + the JUCE/WebView2 setup described in
`README.md` §6.

```powershell
# from the project folder, in a shell with the MSVC environment:
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul && cmake --build build-msvc'
ctest --test-dir build-msvc --output-on-failure
```

- App: `build-msvc\Cadenza_artefacts\Debug\Cadenza Workstation.exe`
- Stop a running instance before rebuilding (`Stop-Process -Name "Cadenza Workstation"`),
  otherwise the linker can't overwrite the exe.

Diagnose a style without launching the app:

```powershell
build-msvc\style-probe.exe "YOUR-STYLE.STY" Am mainA      # prints + renders probe_out\cadenza_playback.mid
```

---

## 4. Known limitations / not done yet

This is an honest list of what will still feel rough — especially relevant
because **most arbitrary `.sty` files will not sound perfect yet**:

- **Style coverage is partial.** The engine handles common SFF layouts, but with
  thousands of community styles many will mis-parse, play wrong instruments, or
  voice oddly. There is no per-style validation/repair pass.
- **NTT chord-fitting is simplified.** We use root transposition + nearest
  chord-tone remapping, not full Yamaha NTT scale tables. Complex chords
  (alterations, slash chords, exotic qualities) won't always voice ideally.
- **Shared percussion setup is still simple.** Multiple percussion parts can now
  share GM channel 10 with the main drums setup preferred, but future drum-kit
  and keymap merging may still need more style-specific handling.
- **Instrument/octave tweaks are global defaults**, not persisted per style.
  (The bass low-octave anchor and pad patch are applied at parse time.)
- **No style editor / no save of edited styles.** Style editing UI is a placeholder.
- **VST3 hosting is master-effect only** — no per-part VST instruments with MIDI
  routing yet.
- **Song mode** doesn't auto-stop at an ending; chord/section hold past chart end.
- Build artifacts are Debug; no installer.

---

## 5. Suggested next steps (priority order)

1. **Style robustness**: a parse-time validator + log of unsupported features,
   and a fallback when CASM policy is missing/garbled.
2. **Fuller NTT**: implement Yamaha NTT scale-table chord fitting for natural
   voicings across all chord qualities.
3. **Drum polish**: improve style-specific drum-kit/keymap merging when multiple
   percussion parts share GM channel 10.
4. **Per-style persistence**: save instrument/octave/mixer choices per style.
5. **Song mode**: honor ending sections and stop.
6. **Per-part VST instruments**; then installer + Release build.

---

*Files of interest:* `Source/Arranger/` (StyParser, StyleEngine,
PatternTransposer, RuntimePlayback), `Source/Midi/` (MidiRouter,
ArrangerMidiRouter chord detection, LiveMelodyVoice), `Source/Audio/`
(AudioEngine, MixerModel), `Source/UI/NativePanel`, `tools/style-probe`.
