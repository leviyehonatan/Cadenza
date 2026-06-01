# Cadenza Workstation — Project Status

> What's built, what's tested, how to run it.
> Last updated after the big build-out session.

## TL;DR

The skeleton is **done and runnable** for personal use. The JUCE host launches,
loads the web UI, accepts hardware MIDI, recognises chords, drives the
auto-accompaniment engine, persists settings between sessions, and exposes
everything through unit-tested pure-C++ libraries.

```
✓ Cadenza Workstation.exe builds and runs
✓ 8 unit-test suites pass (100%)
✓ All C++ phases scaffolded and wired
```

The only thing you need to add to make sound come out: drop a `.sf2`
SoundFont into `resources/sf2/`, OR install FluidSynth via vcpkg and
rebuild for the real synth engine. Without either, Cadenza falls back
to `NullSynthEngine` which logs notes instead of playing them.

---

## What's in the build right now

### Pure C++ core (`cadenza_core.lib`, JUCE-free, fully unit-tested)

| Component | Purpose |
|---|---|
| `Json/Json` | Minimal hand-rolled JSON parser + serializer |
| `ApplicationState` | Mirror of the JS UI state (BPM, channels, pads, etc.) |
| `BridgeRouter` | Routes JSON messages from the web UI to typed hooks |
| `Midi/ChordRecognizer` | Pitch-class-set chord detection (triads, 7ths, sus, dim, etc.) |
| `Arranger/Style` + `StyleLoader` | `.cstyle` JSON file format and in-memory model |
| `Arranger/Song` + `SongLoader` | `.csong` JSON file format and in-memory model |
| `Arranger/PatternTransposer` | Maps chord-relative pattern notes to absolute MIDI |
| `Audio/Transport` | PPQ clock; BPM, bar/beat tracking; sample-rate aware |
| `Settings/SettingsStore` | Loads/saves `%APPDATA%/Cadenza/settings.json` |

### JUCE-bound app (`Cadenza Workstation.exe`)

| Component | Purpose |
|---|---|
| `MainComponent` | Hosts the `WebBrowserComponent`; wires all bridge hooks; manages settings load/save |
| `Audio/SynthEngine` | Abstract synth interface; ships with `NullSynthEngine` and conditionally-compiled `FluidSynthEngine` |
| `Audio/Metronome` | Click track driven by Transport |
| `Audio/AudioEngine` | `juce::AudioSource`; owns synth + metronome + transport; exposes `TickCallback` |
| `Midi/MidiRouter` | `juce::MidiInputCallback`; split-point chord-zone vs melody-zone; pushes recognised chords |
| `Arranger/StyleEngine` | Tick-driven pattern playback; transposes via current chord; lock-free for audio thread |

### Tests (run via `ctest --output-on-failure` in `build-msvc/`)

| Suite | Cases | What it covers |
|---|---|---|
| `cadenza_core_tests` | 5 | BridgeRouter routing + clamp + invalid input |
| `cadenza_json_tests` | 5 | JSON primitives + objects + round-trip + errors + pretty-print |
| `cadenza_chord_tests` | 8 | Major / minor / 7ths / power / dim / aug / single / slash |
| `cadenza_style_tests` | 4 | Style JSON load + role round-trip + save/reload + malformed |
| `cadenza_transpose_tests` | 8 | Absolute pass-through; chord-root/3/5/7; scale-tone in major/minor; clipping |
| `cadenza_transport_tests` | 7 | Defaults; samples/tick math; stop/play; bar+beat rollover; BPM change; reset |
| `cadenza_settings_tests` | 3 | Defaults when missing; save/reload round-trip; corrupt file |
| `cadenza_song_tests` | 4 | Song JSON load; eventForBar precedence; save/reload; malformed |
| **Total** | **44** | |

All 8 suites pass 100% on the current Windows build (MSVC 14.50, CMake 4.3, Ninja).

---

## What works end-to-end

1. **Launch `Cadenza Workstation.exe`** → a native Windows window appears with the full web UI inside (WebView2).
2. **Click around the UI** → tabs switch, knobs turn, pads light up, BPM display updates.
3. **Play / Stop** → transport runs; metronome can be enabled.
4. **Plug in a MIDI keyboard** → the on-screen piano lights up the keys you play. Notes below the split point feed the chord recogniser. The detected chord name is pushed back to the UI in real time.
5. **Auto-accompaniment** → when a style is loaded and play is pressed, the bass + harmony parts of the pattern follow the current left-hand chord. Drums (Absolute role) pass through unchanged.
6. **Style Memory pads 1-4** → switch between `mainA`, `mainB`, `intro`, `ending` sections of the active style.
7. **Quit** → settings (BPM, transpose, key, etc.) saved to `%APPDATA%/Cadenza/settings.json`. Re-launch restores them.

What you can't do yet (or will need to flesh out):
- Hear sound without either a SoundFont in `resources/sf2/` or FluidSynth installed at compile time
- Edit styles inside the UI (loaded files work; the in-UI style editor is still a placeholder)
- Host VST3 *instruments* with per-part MIDI routing (only a single master *effect* insert is wired so far)

Newly working:
- **VST3 master effect** — File menu → Open VST3 Effect loads a `.vst3` (e.g. the
  bundled GuitarML NeuralPi amp sim) onto the arranger output via a headless
  `PluginHost` in `AudioEngine`. Verified with `tools/plugin-probe`.
- **Song mode** — tap the SONG slot to load a `.csong` chord chart; the arranger
  auto-steps its sections + chords bar-by-bar against the transport (`SongPlayer`
  + a message-thread timer in `MainComponent`). Tap SONG again, or use a Style
  Memory pad, to return to manual control.
- **Chord-following colour tones** — melodic non-chord tones now follow the chord
  by root transposition instead of freezing (the old "messy playback" bug).

---

## How to build

```powershell
# from the project root, in PowerShell:
cmake -S . -B build-msvc -G Ninja
.\build.bat
```

`build.bat` calls `vcvars64.bat` to initialise MSVC then `cmake --build`.
Build outputs:
- `build-msvc/cadenza_core.lib`
- `build-msvc/cadenza_*_tests.exe` (8 of them)
- `build-msvc/Cadenza_artefacts/Debug/Cadenza Workstation.exe`
- `build-msvc/Cadenza_artefacts/Debug/resources/` (auto-copied from the project)

## How to run

```powershell
& "build-msvc\Cadenza_artefacts\Debug\Cadenza Workstation.exe"
```

## How to test

```powershell
cd build-msvc
ctest --output-on-failure
```

---

## File layout

```
arranger workstation inspired by giglad/
├── README.md                       Developer brief (architecture, roadmap)
├── docs/
│   ├── BUILDING.md                 Quick build notes
│   └── STATUS.md                   This file
├── CMakeLists.txt                  Build config (cadenza_core + tests + Cadenza app)
├── build.bat                       MSVC env helper for ninja builds
├── .gitignore
├── Cadenza Workstation.html        Web UI (also loaded by the .exe via WebView2)
├── css/                            UI styling
├── js/                             UI behaviour (vanilla JS, IIFE)
├── resources/
│   ├── web/                        Copy of the HTML+css+js for the .exe to load
│   ├── factory/
│   │   ├── styles/8-beat-pop.cstyle
│   │   └── songs/demo.csong
│   └── sf2/                        Drop your .sf2 SoundFont here (empty by default)
├── Source/                         C++ source
│   ├── Main.cpp                    JUCE entry
│   ├── MainComponent.h/.cpp        Top-level component; wires everything
│   ├── ApplicationState.h/.cpp     Pure C++ state mirror
│   ├── BridgeRouter.h/.cpp         JSON message routing with hooks
│   ├── Json/Json.h/.cpp            Minimal JSON
│   ├── Audio/
│   │   ├── Transport.h/.cpp        PPQ clock (cadenza_core)
│   │   ├── SynthEngine.h/.cpp      Synth interface + Null + (optional) FluidSynth
│   │   ├── Metronome.h/.cpp        Click track
│   │   └── AudioEngine.h/.cpp      juce::AudioSource bringing it all together
│   ├── Midi/
│   │   ├── ChordRecognizer.h/.cpp  Pure C++ chord detection (cadenza_core)
│   │   └── MidiRouter.h/.cpp       JUCE MidiInputCallback
│   ├── Arranger/
│   │   ├── Style.h/.cpp            (cadenza_core)
│   │   ├── StyleLoader.h/.cpp      (cadenza_core)
│   │   ├── PatternTransposer.h/.cpp (cadenza_core)
│   │   ├── Song.h/.cpp             (cadenza_core)
│   │   ├── SongLoader.h/.cpp       (cadenza_core)
│   │   └── StyleEngine.h/.cpp      Audio-thread orchestration
│   └── Settings/
│       └── SettingsStore.h/.cpp    %APPDATA%/Cadenza/settings.json
├── tests/                          One .cpp per test suite (hand-rolled expect())
└── lib/JUCE/                       JUCE submodule (git-cloned separately)
```

---

## Next steps (when you feel like it)

These are bonus polish, not blockers for personal use:

1. **Get FluidSynth.** Easiest path:
   `vcpkg install fluidsynth:x64-windows`, then re-run CMake — it picks it up via `find_package(FluidSynth)` and links the real synth. The build message will switch from "NullSynthEngine fallback" to "real synth enabled".
2. **Drop a SoundFont.** Download GeneralUser GS (~30 MB) from
   schristiancollins.com/generaluser.php and put it in `resources/sf2/`.
   `MainComponent::tryLoadFactorySoundFont` will pick up any `.sf2` it finds.
3. **Wire the in-UI Style Editor.** The web UI has a Style Editor tab that's
   currently a placeholder. The C++ side already has `saveStyleToFile()` — just
   add bridge messages for "save current style" and "edit note at tick".
4. **Auto-step through a Song.** SongLoader works; just need a small SongPlayer
   that uses Transport bar-counts to look up `Song::eventForBar` and call
   `StyleEngine::setChord` + `setSection` automatically.
5. **VST3 hosting.** JUCE already includes `juce::AudioPluginFormatManager` —
   add a `PluginHost` class and wire it into AudioEngine.

Each of those is a 1-evening project.

---

## Known limitations

- **Single MIDI input.** `MidiRouter::openInput` opens one device at a time.
- **No MIDI thru / record.** Notes go to the synth but aren't recorded.
- **Chord recognition is conservative** for ambiguous voicings. For arranger
  use that's actually preferable (stable backing > over-eager re-harmonisation).
- **The "compatibility shim"** in `MainComponent.cpp` bridges JUCE 8's new
  `WebBrowserComponent::Backend::emitEvent` API to the older
  `window.__juce__.postMessage(...)` convention the JS expects. If you ever
  refactor the JS to use the new API directly, you can delete the shim.
- **Audio thread safety in StyleEngine.** Snapshot pointers are taken under a
  short mutex when changing styles/sections — fine for typical use, but a
  fully lock-free implementation would use `juce::SpinLock` + double-buffered
  state. Easy upgrade if needed.

---

*That's the whole map. Everything in this file matches what's actually
on disk and passing tests in this commit. If you change something, update
this doc so future-you doesn't have to spelunk to figure out the layout.*
