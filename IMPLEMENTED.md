# Cadenza Workstation — Implemented Status

> What actually works **today** in the built application. This supersedes the
> roadmap framing in `README.md` (which was written before the C++ engine
> existed). For the chronological list of changes see `CHANGELOG.md`.

_Last updated: 2026-06-11 (v1.0.0)._

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
- **Audio output device picker** ("Audio" button) — choose the real output device
  (DAC/headphones) instead of whatever is the system default, e.g. to bypass a
  virtual "gaming" mixer that colours/flattens the sound. Choice persists
  (`audio-device.xml` next to settings) and is restored on launch.
- **Optional ASIO** (low-latency, bypasses the Windows audio stack). Off by default
  because the Steinberg ASIO SDK is proprietary and can't be bundled. To enable:
  put the SDK at `lib/asiosdk/` (so `lib/asiosdk/common/iasiodrv.h` exists) or pass
  `-DASIO_SDK_DIR=<path>`, then reconfigure — CMake prints "ASIO ENABLED" and ASIO
  appears as a device type in the Audio picker. (No native ASIO driver? Install
  ASIO4ALL.)
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
- Section switching (intro / main A–D / fill / break / ending), sample-tight at
  bar boundaries. Intros/fills are one-shots returning to the main; an Ending
  stops playback when it finishes.
- **Auto Fill-In** (toggle, on by default): pressing a Main while playing
  inserts that main's fill first, then lands on the main — Yamaha AUTO FILL IN
  behavior. Pressing the active main plays its fill.
- **Syncro Start / Syncro Stop**: play a chord in the chord zone to start the
  band; with Syncro Stop on, releasing all chord keys pauses it.
- **Tap Tempo** button.
- **Fade Out** button: the master fades to silence over ~8 s, then the
  transport stops cleanly and the level is restored.
- **One Touch Settings** (OTS 1–4) parsed from Yamaha styles, with OTS Link
  auto-recall on Main A–D.
- **Registrations** (4 slots): one-button snapshots of the whole live setup.
- Per-channel **mixer**: volume, mute, solo, and instrument (program) pick.
  Bass/drums/etc. get sensible role-based default instruments.
- **Per-part VST3 instruments**: each mixer strip's instrument menu offers
  "Load VST3 Instrument…" / "Use GM SoundFont". A channel with a loaded VST3
  instrument routes its notes (thread-safe `MidiMessageCollector`) to that plugin
  and sums its render into the mix; channels without one keep using FluidSynth.
  Zero overhead when none are loaded. The mixer fader/mute/solo control the plugin
  (effective CC7 → per-part gain), and the choice **persists per style**
  (`pluginPath` in `styleMixes`) — reloaded automatically when the style loads.
- **Per-style mixer memory**: instrument/volume/mute/solo tweaks are saved per
  style id (in settings.json under `styleMixes`) and re-applied on top of the
  style defaults whenever that style is loaded again.

### Tooling
- `style-probe` — loads a `.sty`/`.cstyle`, prints per-part source→played pitch
  and role for a given chord, **and renders the section to a `.mid`** so you can
  audition the raw arrangement in any player.
- `plugin-probe` — loads/inspects a VST3.
- `style-probe ... --casm` — dumps the parsed CASM (CSEGs, per-channel Ctab/Ctb2
  entries, decoded NTR/NTT, whether a policy attached, and raw bytes) for
  debugging why a style does or doesn't pick up Yamaha policy.
- `style-scan` — batch-parses a whole style library (recursively), classifies each
  `.sty` as OK / WARN / FAIL, separates benign notes (percussion routing) from
  real warnings (e.g. missing CASM policy -> heuristic fallback), aggregates the
  top reasons, and writes a per-file CSV report. Crash-safe: survives Unicode
  filenames and native parser faults on malformed files. Usage:
  `style-scan <dir|file> [...] [--csv report.csv]`. (On a 1,582-file library:
  ~98% parsed to a playable style; failures were non-Yamaha/Korg-format or empty.)
- `style-probe` also shows playback channel/percussion flag, shared-channel
  setup owner, and stable independent note names in range/output lines.
- `style-probe` opens with a **parse-diagnostics summary**: `Diagnostics: none`
  when the style parsed cleanly, otherwise `Diagnostics: N warning(s)` followed
  by one `- warning: ...` line per issue (e.g. `channel 9 percussion detected,
  routing to GM drum playback channel 10`). Warnings come from
  `style.parseWarnings` so unsupported/auto-corrected style features are visible
  at a glance.
- Diagnostic log at `%APPDATA%/Cadenza/cadenza.log` (fresh each launch).
- 23 unit-test suites (chord recognition, transposition, parsing, playback,
  and focused style-probe diagnostics — none / single / plural warning cases).

### Sound-quality fixes already in (CULY-ext was the test style)
- Yamaha NTR/NTT policy now guides the transposition path: RootFixed/Bypass can
  stay absolute, RootTransposition follows root movement, and chord/melody modes
  use the existing chord-tone fitting rather than one generic transposition.
- **Sub-rhythm / rhythm2 percussion routes to GM drum channel 10**, while the
  main drums part owns channel-10 bank/program setup when both share the channel.
- Chord-tone **voicing** uses nearest-tone placement (no octave scatter).
- A 7th note over a plain triad folds to the root (no accidental Am7-sounds-like-C).
- **Color/phrase notes fit the played chord's quality.** Non-chord-tone notes are
  shifted by chord root then fit to the chord quality: a 7-note scale for tonal
  qualities (Ionian/maj, Mixolydian/dom7, Dorian/min, melodic-minor/mMaj7,
  Locrian/m7b5) and the actual chord tones for symmetric qualities (dim/aug),
  so a major-key phrase takes the minor 3rd / dominant b7 / etc. instead of
  clashing. Applies in both the CASM-policy path and the fallback path; power
  and single-note chords keep simple root transposition.
- **Sub-rhythm / drum-bank parts are treated as percussion** and never pitched.
- **Pad** defaults to a tighter, faster synth-strings patch with reverb/chorus cut.
- **Bass** is anchored to one consistent low octave (E1–Eb2) so the foundation
  never jumps register or drops near-inaudible.
- **Mix for fullness/ambience.** The synth runs an explicit hall reverb + chorus
  (so the band has depth, not the dry "GM default" sound). Parts get a gentle
  stereo pan by role (bass/drums centered, comps/phrases spread L/R, pad wide).
  Reverb is **floored** per part: many styles send CC91=0 expecting the keyboard's
  global reverb, so anything below the floor (25 melodic / 12 drums) is raised —
  no bone-dry parts. Richer style reverb/pan values are kept as-is.
- **Master 3-band EQ** (low shelf 120 Hz / mid peak 900 Hz / high shelf 9 kHz) on
  the final mix, gains persisted in settings (`eqLowDb`/`eqMidDb`/`eqHighDb`,
  default +4 / 0 / +2 for low-end body + air). **Live on-screen knobs** (Low/Mid/
  High, -12..+12 dB) on the panel let the player tune by ear in real time; changes
  persist to settings immediately.
- **Analog console glue** on the master buss: the DSP from Airwindows
  *Console7Buss* (MIT) is ported in (`MasterGlue`). **Currently disabled by
  default** — in the live chain it interacted badly (broke playback), so it's kept
  for a future, properly-verified pass. Master chain order when on:
  EQ → console glue → soft limiter → optional VST3 insert.
- **Master compressor** (`MasterCompressor`, own DSP, unit-tested): stereo-linked
  feed-forward bus compressor for gentle glue/density (default -18 dB / 2:1 /
  15 ms / 200 ms / +3 dB makeup). Linked detector keeps the stereo image stable;
  output soft-clipped. Chain: EQ → compressor → (console glue, off) → limiter.
  A live **"Comp" knob** (0..100%, next to the EQ knobs) maps to threshold +
  make-up so the player dials glue by ear (0 = bypass); persists in settings
  (`compAmount`).
- **Soft limiter** (always on) rounds peaks so a hot full band never hard-clips
  into a crunchy distortion; synth gain leaves headroom for it.
- **Far more CASM policies are extracted (three fixes).** (1) Ctb2 split-range
  entries (source-note byte 21 != 0x7F, common in Intro/Ending B/C) are now decoded
  instead of dropped. (2) Binary Ctab/Ctb2 entries whose note-limit byte is 0x3D
  ('=') were misdetected as legacy ASCII and lost their policy; ASCII is now
  detected by the payload being genuinely textual. (3) A section with no policy for
  a channel now inherits that channel's real policy from a sibling section
  (preferring the same family: intro/ending/main/fill) instead of the heuristic.
  Combined, on the 1,582-file test library the styles needing the C-major heuristic
  fallback dropped from 344 to 13, and clean styles rose from 187 to 1,422 — nearly
  the whole library now plays with its real per-part Yamaha NTR/NTT behavior.

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
- **NTT chord-fitting is approximate.** Chord tones and color/phrase tones fit the
  played chord's quality (all qualities the recognizer produces, including
  dim/aug/m7b5), but this is a clean-room approximation, not the full Yamaha NTT
  tables. Slash chords and some altered tensions still won't always voice ideally.
- **Shared percussion setup is still simple.** Multiple percussion parts can now
  share GM channel 10 with the main drums setup preferred, but future drum-kit
  and keymap merging may still need more style-specific handling.
- **No style editor / no save of edited styles.** Style editing UI is a placeholder
  (`.cstyle` JSON can be edited by hand and the converter regenerates it).
- No installer (.msi/.exe setup) — distribution is a self-contained folder/zip
  produced by `scripts/package.ps1`.

(Older limitations now fixed: song mode auto-stops at chart end; per-part VST3
instruments work; Release build + package exist.)

---

## 5. Suggested next steps (priority order)

1. **Fuller NTT**: implement Yamaha NTT scale-table chord fitting for natural
   voicings across all chord qualities (slash chords, altered tensions).
2. **Drum polish**: improve style-specific drum-kit/keymap merging when multiple
   percussion parts share GM channel 10.
3. **Style editor**: edit/save `.cstyle` from the UI.
4. **Installer** (Inno Setup / NSIS) on top of the package folder.

---

## 6. Release & distribution

- `build-release/` — Ninja Release tree (same vcpkg toolchain as build-msvc).
  Configure once, then `cmake --build build-release --target Cadenza` from a
  vcvars shell.
- `scripts/package.ps1 [-Zip]` — produces `dist/Cadenza-<version>/`: the exe,
  runtime DLLs, both SoundFonts, factory content, web UI, CLI tools, and a
  user quick-start (`docs/QUICK_START.md` → package `README.md`).
  Self-contained; verified to launch from the package folder.

---

*Files of interest:* `Source/Arranger/` (StyParser, StyleEngine,
PatternTransposer, RuntimePlayback), `Source/Midi/` (MidiRouter,
ArrangerMidiRouter chord detection, LiveMelodyVoice), `Source/Audio/`
(AudioEngine, MixerModel), `Source/UI/NativePanel`, `tools/style-probe`.
