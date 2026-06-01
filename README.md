# Cadenza Workstation — Developer Brief

> A web-UI-driven JUCE arranger application for Windows.
> This document is the complete handoff for a developer picking up the project.

---

## 1. Project Overview

**Cadenza Workstation** is a cross-platform (Windows-first) audio arranger
application — a software equivalent of a hardware "arranger keyboard."
A musician plays chords with their left hand, the app plays a styled
backing band (drums, bass, harmony) in real time matching those chords,
and the musician plays a lead melody with their right hand.

The product is positioned in the same market segment as Giglad, Yamaha's
PSR-style "Genos" feature set in software, OneMan Band, and Vanbasco —
i.e. **solo-musician live-performance arranger**, not a multi-track DAW.

### Target user

- Home musicians / hobbyist keyboardists who want a "backing band" to play
  along to
- Live gig musicians who need a portable solo-with-band setup
- Worship / lounge / cruise-ship style players
- MIDI educators

### Differentiators (what we'll build, that competitors lack or charge for)

1. **Modern web-based UI** running inside the native shell — easier to
   restyle, easier to extend with custom panels, no QT/JUCE-Component
   maze.
2. **Open file formats** for styles and presets (JSON-based), so users
   can edit them by hand or import community content.
3. **Free or honest trial model** — no hardware fingerprinting,
   no aggressive DRM. (See §11 for license stance.)
4. **VST3 plugin host** built in — load any third-party instrument or FX.

---

## 2. Current State — What Already Exists

The repository (this folder) contains a **complete UI prototype** built
in vanilla HTML/CSS/JS:

```
arranger workstation inspired by giglad/
├── README.md                       ← you are here
├── Cadenza Workstation.html        ← main UI; loads all CSS + the JS app
├── css/
│   ├── workstation.css             ← base theme, custom properties, .btn
│   ├── arranger.css                ← Arranger-tab layout, .pill, piano, mixer
│   └── mixer-editor.css            ← Mixer + Style/Song editors + Memory Pool
├── js/
│   └── workstation.js              ← State, JuceBridge stub, CadenzaAPI
├── screenshots/                    ← visual reference for layout
└── uploads/
```

### What the UI prototype does today

- Renders all 4 tabs (Arranger, Style Editor, Song Editor, Memory Pool)
- Central state object (`State`) drives the DOM via per-section renderers
- Working interactions: tempo knob, transpose/octave/BPM scroll-wheel,
  volume faders, pan knobs, solo/mute/play/stop/record buttons, bank
  memories, pads, on-screen piano with octave-tinted keys, live VU meters
  (random animation, placeholder)
- A `JuceBridge` object stubs out the C++ ↔ JS communication. In
  development mode (no JUCE host) it `console.log`s all messages.
- A `window.CadenzaAPI` public object exposes everything programmatically
  (BPM, transpose, play/stop, volume, pan, flashNote, event subscriptions).
- Full accessibility: keyboard focus rings, `aria-pressed` on all toggles,
  `role="slider"` + ARIA values on knobs, ARIA labels on icon buttons.

### What the UI does NOT do yet

- It makes no sound (no audio engine connected)
- It accepts no MIDI input from a hardware keyboard
- It does not load/save styles, songs, presets, or banks
- It does not host plugins
- It has no real meters — the VU bars animate with `Math.random()`

The UI is the **dashboard with the gauges working**. The next phase
builds the **engine under the hood**.

---

## 3. Tech Stack — Confirmed Libraries

All open-source. Specific licensing per §11.

| Layer | Library | Purpose | Source |
|---|---|---|---|
| **Framework** | JUCE 7.x or 8.x | Cross-platform window, audio, MIDI, threading, file I/O, plugin hosting, WebView | https://github.com/juce-framework/JUCE |
| **Synth** | FluidSynth | SoundFont (.sf2) playback for the General MIDI engine | https://github.com/FluidSynth/fluidsynth |
| **Audio I/O** | libsndfile | Read/write WAV, FLAC, AIFF, OGG | https://github.com/libsndfile/libsndfile |
| **Time-stretch** | Bungee | Real-time pitch-shift + time-stretch for audio loops | https://github.com/kupix/bungee |
| **HTTP** | libcurl | Update checks, optional content downloads | https://github.com/curl/curl |
| **Crypto / TLS** | OpenSSL | HTTPS for libcurl; optional preset signing | https://github.com/openssl/openssl |
| **Plugin host** | VST3 SDK | Load third-party VST3 instruments and effects | https://github.com/steinbergmedia/vst3sdk |

### Optional (Phase 5+)

- **CHOC** — header-only utilities by JUCE's creator. Useful if we want to
  ship the WebView with embedded HTML rather than file:// loading.
  https://github.com/Tracktion/choc
- **Tracktion Engine** — reference codebase if we ever want a multi-track
  edit/record feature later. MIT-licensed.
  https://github.com/Tracktion/tracktion_engine

---

## 4. Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                         CADENZA.EXE (JUCE)                       │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │           juce::WebBrowserComponent (full window)          │  │
│  │                                                            │  │
│  │   Loads:  resources/web/Cadenza Workstation.html           │  │
│  │   ↑   ↓   (via __juce__.postMessage / evaluateJavascript)  │  │
│  └────────────────────────────────────────────────────────────┘  │
│             │                              │                     │
│             ▼                              ▲                     │
│  ┌─────────────────────────┐  ┌──────────────────────────────┐   │
│  │   BridgeRouter (C++)    │  │   ApplicationState (C++)     │   │
│  │   - parses postMessage  │  │   - mirror of JS State       │   │
│  │   - routes by 'type'    │  │   - persisted to disk        │   │
│  │   - calls handlers      │  │   - notifies BridgeRouter    │   │
│  └─────────────────────────┘  └──────────────────────────────┘   │
│             │                                                    │
│             ├───► AudioEngine ───► JUCE AudioDeviceManager       │
│             │      ├─► FluidSynth (general MIDI)                 │
│             │      ├─► VST3 PluginInstance(s)                    │
│             │      └─► Bungee (audio loop time-stretch)          │
│             │                                                    │
│             ├───► MidiRouter ───► JUCE MidiInput/Output          │
│             │      ├─► Hardware keyboard → ChordRecognizer       │
│             │      └─► ArrangerEngine → notes out                │
│             │                                                    │
│             ├───► StyleEngine (the arranger logic)               │
│             │      ├─► StylePatternPlayer (loops .style files)   │
│             │      ├─► ChordRecognizer (left-hand → chord name)  │
│             │      └─► PatternTransposer (chord → notes mapping) │
│             │                                                    │
│             ├───► AssetLoader                                    │
│             │      ├─► .sf2 (SoundFonts)                         │
│             │      ├─► .cstyle (our style format, JSON)          │
│             │      ├─► .csong (our song format, JSON)            │
│             │      └─► .cpreset (per-instrument preset)          │
│             │                                                    │
│             └───► SettingsStore (JSON in %APPDATA%)              │
└──────────────────────────────────────────────────────────────────┘
```

### Key principles

1. **The UI never directly touches audio.** The web UI sends *intent*
   (e.g. `{type: "play"}`); the C++ side does the work and reports back
   state (`{type: "playState", value: true}`).
2. **State lives in C++.** The JS `State` object is a *cache* that's
   updated when C++ sends `stateChange` events. This means the UI can
   reload without losing audio state.
3. **All audio work happens on the audio thread.** Never allocate, lock,
   or do file I/O in the JUCE `processBlock`. Inter-thread communication
   via `juce::AbstractFifo` or atomic message queues.
4. **File formats are JSON.** Easy to debug, easy to support community
   content, easy to round-trip. (Audio assets stay binary obviously.)

---

## 5. Development Roadmap

Estimate assumes one mid-level C++ developer, half-time. Adjust as needed.

### Phase 0 — Project setup *(1-2 days)*

- Install JUCE 8.x + Projucer + Visual Studio 2022 Community
- Generate the Projucer project, name it **Cadenza**
- Set output: GUI Application, Windows 10 minimum
- Add the `web/` folder (this entire repo content) as a resource directory
- Wire `juce::WebBrowserComponent` to load the local HTML on startup
- **Definition of done:** running `Cadenza.exe` opens a native window
  containing the current web UI

### Phase 1 — Bridge wiring *(3-5 days)*

- Implement `BridgeRouter` C++ class:
  - Receives postMessage JSON, parses `{ type, payload }`
  - Dispatches to typed handlers via a small message table
- Implement reverse direction: C++ → JS via `evaluateJavascript()` calling
  `window.JuceBridge.onXxx(...)` handlers (already declared in
  `js/workstation.js`)
- Add `ApplicationState` C++ class mirroring `State` from JS
- **Definition of done:** clicking play in the web UI logs to the C++
  console; calling `state.setBpm(140)` in C++ updates the web display

### Phase 2 — FluidSynth integration *(5-7 days)*

- Add FluidSynth as a dependency (vcpkg or pre-built binaries)
- Create `AudioEngine` class implementing `juce::AudioSource`
- In `processBlock`, render audio from FluidSynth into the JUCE buffer
- Load a free SoundFont (ship GeneralUser GS in `resources/sf2/`)
- Wire `noteOn`/`noteOff` from the web piano to FluidSynth
- Implement bank-memory selection (bank 0 prog 0-127 lookup)
- **Definition of done:** clicking the on-screen piano plays the
  corresponding GM instrument; bank memory pills change instruments

### Phase 3 — MIDI hardware input *(2-3 days)*

- Use `juce::MidiInput::openDevice` for the default MIDI device
- Add a settings UI for choosing the MIDI input (sub-tab in Style Editor)
- Forward incoming notes to both the synth AND `flashNote()` on the
  web piano (so the on-screen piano lights up as you play)
- Split-point logic: notes below the split → chord input (Phase 5),
  notes above → melody
- **Definition of done:** playing a hardware MIDI keyboard plays sound
  AND lights up the on-screen piano

### Phase 4 — Transport + tempo *(3-5 days)*

- Implement `Transport` class: tempo, time signature, position
- A high-resolution PPQ clock running on the audio thread
- Metronome (click track) toggleable, follows tempo
- Play / Stop / Record / Loop hooks from the web UI
- Tap-tempo support
- **Definition of done:** pressing Play starts a metronome at the
  current BPM; changing BPM in the UI changes the click immediately

### Phase 5 — Style format + Pattern Player *(2-3 weeks)*

- Design `.cstyle` JSON format. Suggested schema in §7.
- Implement `StylePatternPlayer`: loops a pattern (drums + bass + harmony
  parts), each part rendered through FluidSynth on its own channel
- Style editor tab becomes functional: list of patterns, play preview,
  edit notes
- Add "Main A / Main B / Intro / Ending / Fill" sections per style
- **Definition of done:** pressing Play with a style loaded plays a
  drum + bass loop until Stop is pressed

### Phase 6 — Chord recognition + auto-accompaniment *(3-6 weeks — the big one)*

This is the heart of an arranger. Approach:

- **ChordRecognizer**: given the set of currently-held notes in the
  chord zone, identify the root + quality (maj/min/7/m7/dim/sus/etc.).
  Standard algorithm: enumerate all rotations, score against a chord
  template dictionary, return best match. Reference algorithm: see MMA
  (Musical MIDI Accompaniment, GPL) or any of the dozens of open-source
  chord-detector projects.
- **PatternTransposer**: given a chord, transpose the bass + harmony
  parts of the current style pattern to fit. Each note in the pattern
  has a `chord-relative` role (root, 3rd, 5th, 7th, "any-chord-tone")
  that determines how it maps.
- **Voice-leading polish**: pick octaves so consecutive chord changes
  don't jump octaves wildly. Optional but makes a huge UX difference.
- **Live performance hooks**: real-time updates as the chord changes
  mid-bar, with quantize-to-beat option.
- **Definition of done:** play a C chord left-hand → drums + bass + harmony
  play in C; switch to F chord → instantly everything plays in F

### Phase 7 — Polish *(ongoing)*

- Save / Load projects (`.csong` JSON)
- Memory Pool (shared instrument cache)
- VST3 plugin hosting (load NeuralPi, Surge XT, etc. for guitar/synth parts)
- Audio loop tracks with Bungee time-stretching
- Lyrics overlay
- Multi-MIDI-output routing (for hardware sound modules)
- Bank memory keyboard shortcuts
- Installer (Inno Setup or NSIS)

### Phase 8 — Release & sustainment

- Public beta, gather feedback
- Free license model (see §11)
- Update channel via libcurl
- Community style-pack repository (GitHub-hosted)

---

## 6. Build Setup

### Prerequisites

| Tool | Version | Source |
|---|---|---|
| Visual Studio 2022 Community | 17.x | https://visualstudio.microsoft.com/ |
| JUCE Framework | 8.x | https://juce.com/download/ |
| Projucer | (bundled with JUCE) | same |
| Git for Windows | latest | https://git-scm.com/ |
| vcpkg | latest | https://github.com/microsoft/vcpkg |

### vcpkg dependencies

```bash
vcpkg install fluidsynth:x64-windows libsndfile:x64-windows curl:x64-windows openssl:x64-windows
```

Bungee will need a manual build (CMake) — instructions in their repo.

### First build

```powershell
# from project root
git clone https://github.com/juce-framework/JUCE.git lib/JUCE
.\lib\JUCE\extras\Projucer\Builds\VisualStudio2022\Projucer.exe Cadenza.jucer
# in Projucer: File → Save Project and Open in IDE → Visual Studio 2022
# in VS: select Release/x64, Build → Build Solution
```

### Current scaffold build

The repository now includes a CMake/JUCE bootstrap so Phase 0 can be built
directly from the command line while a Projucer project is still optional.

```powershell
git clone https://github.com/juce-framework/JUCE.git lib/JUCE

# Run from a Visual Studio Developer PowerShell, or through VsDevCmd.bat.
cmake -S . -B build-msvc -G Ninja
cmake --build build-msvc
ctest --test-dir build-msvc --output-on-failure
```

If JUCE's WebView2 package is missing, install it once:

```powershell
Register-PackageSource -ProviderName NuGet -Name nugetRepository -Location https://www.nuget.org/api/v2 -Force
Install-Package Microsoft.Web.WebView2 -Scope CurrentUser -RequiredVersion 1.0.3485.44 -Source nugetRepository -Force
```

Current output:

- `build-msvc/Cadenza_artefacts/Debug/Cadenza Workstation.exe`
- `build-msvc/cadenza_core_tests.exe`

### Project structure (target)

```
Cadenza/
├── Cadenza.jucer                 ← Projucer project file
├── Source/
│   ├── Main.cpp                  ← juce::JUCEApplication entry
│   ├── MainComponent.h/.cpp      ← top-level component (hosts WebView)
│   ├── BridgeRouter.h/.cpp       ← JS ↔ C++ message routing
│   ├── ApplicationState.h/.cpp   ← mirror of JS State, persisted
│   ├── Audio/
│   │   ├── AudioEngine.h/.cpp    ← juce::AudioSource implementation
│   │   ├── Transport.h/.cpp      ← clock + tempo
│   │   ├── FluidSynthVoice.h/.cpp
│   │   └── PluginHost.h/.cpp     ← VST3 hosting
│   ├── Midi/
│   │   ├── MidiRouter.h/.cpp
│   │   ├── ChordRecognizer.h/.cpp
│   │   └── SplitPoint.h/.cpp
│   ├── Arranger/
│   │   ├── StyleEngine.h/.cpp
│   │   ├── PatternPlayer.h/.cpp
│   │   ├── PatternTransposer.h/.cpp
│   │   └── VoiceLeading.h/.cpp
│   ├── Assets/
│   │   ├── StyleLoader.h/.cpp    ← .cstyle JSON
│   │   ├── SongLoader.h/.cpp     ← .csong JSON
│   │   └── PresetLoader.h/.cpp   ← .cpreset JSON
│   └── Settings/
│       └── SettingsStore.h/.cpp  ← %APPDATA%/Cadenza/settings.json
├── resources/
│   ├── web/                      ← this folder's contents go here
│   │   ├── Cadenza Workstation.html
│   │   ├── css/
│   │   └── js/
│   ├── sf2/
│   │   └── GeneralUser GS.sf2    ← bundled General MIDI soundfont
│   └── factory/
│       ├── styles/               ← shipped .cstyle files
│       └── songs/                ← shipped .csong files
├── lib/
│   └── JUCE/                     ← submodule
└── tests/                        ← unit tests for arranger logic
```

---

## 7. The JUCE Bridge Protocol

Web UI → C++ messages are JSON: `{ type, payload }`. Sent via:

```js
window.__juce__.postMessage(JSON.stringify({ type, payload }))
```

The web UI already calls this via `toJuce()` in `workstation.js`. The
following message types are already emitted by the UI:

| Type | Payload | Meaning |
|---|---|---|
| `play` | `{}` | User wants to start playback |
| `stop` | `{}` | User wants to stop |
| `bpm` | `{ value: int }` | New tempo |
| `transpose` | `{ value: int }` | New transpose (semitones) |
| `octave` | `{ value: int }` | New octave shift |
| `key` | `{ value: string }` | New song key (C, C#, …) |
| `selectStyle` | `{ index, name }` | User picked a style category |
| `selectPart` | `{ part: string }` | User clicked a part slot |
| `selectChannel` | `{ channel: string }` | Mixer tab clicked |
| `volume` | `{ channel, value: 0-100 }` | Channel fader moved |
| `pan` | `{ channel, value: -50..50 }` | Channel pan knob moved |
| `solo` / `mute` | `{ channel, value: bool }` | S/M button |
| `melodyOnOff` | `{ channel, value: bool }` | Left / R1-R3 toggle |
| `chordSource` | `{ source, value: bool }` | Bass/Arranger/Memory toggle |
| `bankMemory` | `{ name }` | Bank memory selected |
| `pad` | `{ index, value: bool }` | Pad toggled |
| `styleMemory` | `{ slot }` | Style memory slot clicked |
| `crossfade` | `{ value: 0-100 }` | Player crossfade |
| `noteOn` / `noteOff` | `{ note, velocity, piano }` | On-screen piano played |
| `record` | `{ value: bool }` | Record toggled |
| `selectStyle` | `{ index, name }` | Style category selected |

### C++ → Web

C++ pushes state changes back by calling JavaScript:

```cpp
webView.evaluateJavascript("window.JuceBridge.onBpmChanged(140);", nullptr);
```

Already-declared handlers in `workstation.js`:

| Handler | Purpose |
|---|---|
| `JuceBridge.onBpmChanged(bpm)` | Update displayed BPM |
| `JuceBridge.onPlayStateChanged(playing)` | Sync play button |
| `JuceBridge.onChordChanged(chord)` | Update chord display |
| `JuceBridge.onNoteReceived(midi)` | Flash piano key (from hardware MIDI) |

**Add more handlers as needed**; the pattern is consistent.

### Public JS API (for external scripting / tests)

`window.CadenzaAPI` exposes:

- `getState()` returns the current UI state
- `setBPM(n)`, `setTranspose(n)`, `setOctave(n)`, `setChord(s)`, `setKey(s)`
- `play()`, `stop()`
- `setVolume(channel, db)`, `setPan(channel, val)`
- `setBankMemory(name)`, `selectStyle(idx)`
- `flashNote(midi, pianoId?)`
- `onNoteOn(cb)`, `onNoteOff(cb)`, `onStateChange(cb)`

---

## 8. File Formats (Proposed)

All JSON, all human-readable. UTF-8. Files use a `.c*` extension prefix
to avoid collision with any existing format.

### `.cstyle` — a style (drum + bass + harmony pattern set)

```json
{
  "$schema": "cadenza.style.v1",
  "id": "8-beat-pop",
  "name": "8 Beat Pop",
  "tempo": 120,
  "timeSignature": [4, 4],
  "sections": {
    "intro":   { "barCount": 2, "parts": [...] },
    "mainA":   { "barCount": 4, "parts": [...] },
    "mainB":   { "barCount": 4, "parts": [...] },
    "fillAB":  { "barCount": 1, "parts": [...] },
    "ending":  { "barCount": 2, "parts": [...] }
  }
}
```

Each `part` is `{ name, channel, instrument, notes: [...] }` where each
note is `{ tick, pitch, velocity, duration, role }` and `role` is one
of `"absolute"`, `"chord-root"`, `"chord-3"`, `"chord-5"`, `"chord-7"`,
`"scale-tone"` — this is what the PatternTransposer uses to remap to
the live chord.

### `.csong` — a song (sequence of style sections with chord changes)

```json
{
  "$schema": "cadenza.song.v1",
  "id": "demo",
  "name": "Demo Song",
  "style": "8-beat-pop",
  "tempo": 120,
  "key": "C",
  "events": [
    { "bar": 1, "section": "intro" },
    { "bar": 3, "section": "mainA", "chord": "C" },
    { "bar": 7, "section": "mainA", "chord": "F" },
    { "bar": 11, "section": "mainB", "chord": "G" },
    { "bar": 15, "section": "ending", "chord": "C" }
  ]
}
```

### `.cpreset` — instrument preset (bank/program + FX chain)

```json
{
  "$schema": "cadenza.preset.v1",
  "name": "Grand Piano",
  "source": { "type": "soundfont", "file": "GeneralUser GS.sf2", "bank": 0, "program": 0 },
  "fx": [
    { "type": "reverb", "wet": 0.15 },
    { "type": "eq", "low": 0, "mid": 1, "high": 2 }
  ]
}
```

### `.cbank` — bank memory (8x2 grid mapping)

```json
{
  "$schema": "cadenza.bank.v1",
  "name": "Default",
  "slots": [
    { "row": 0, "col": 0, "preset": "grand-piano" },
    { "row": 0, "col": 1, "preset": "electric-grand" },
    ...
  ]
}
```

---

## 9. Coding Standards

- **C++**: C++20. JUCE coding style (`camelCase` methods, `PascalCase`
  types, member prefix `m_` optional). Format with `clang-format`,
  config in repo root.
- **JS**: ES2020. Vanilla, no framework (we want a minimal bundle for
  embedded WebView). Existing code is the style reference.
- **CSS**: BEM-lite naming (already in use: `.style-card`, `.mx-ch`,
  `.disp-foot`).
- **Commits**: Conventional Commits format (`feat:`, `fix:`, `refactor:`,
  `chore:`, `docs:`).
- **Branches**: trunk-based. PRs squash-merged. CI green before merge.

### Threading rules (critical)

- **Audio thread** (`AudioEngine::processBlock`): no locks, no `new`/`delete`,
  no file I/O, no `juce::String::operator+`, no logging. Communicate
  using `juce::AbstractFifo` or atomics only.
- **UI / JS thread**: anything goes, but never block waiting on audio.
- **MIDI input thread**: very short — push to a FIFO for the audio thread,
  return immediately.

---

## 10. Testing

- **Unit tests**: Catch2 or JUCE's built-in `UnitTest`. Cover:
  - `ChordRecognizer` (table of 100+ chord shapes → expected detection)
  - `PatternTransposer` (sample pattern + chord → expected note set)
  - `StyleLoader` / `SongLoader` (round-trip JSON)
  - State serialization
- **Integration tests**: scriptable via `CadenzaAPI`. Launch the app,
  use Puppeteer or Playwright to drive the UI, record audio output
  via `juce::AudioFormatWriter` to a WAV, compare to baseline.
- **Manual smoke tests**: documented checklist before each release tag.

---

## 11. Licensing & Distribution

### Library licenses (legal compliance)

| Library | License | Implication |
|---|---|---|
| JUCE 8 | GPL3 or paid commercial | We pay for the JUCE Indie tier (~$40/month) once we ship commercially. Until then, GPL3 use is fine for development. |
| FluidSynth | LGPL 2.1+ | Ship as separate DLL, do not statically link. ✓ |
| libsndfile | LGPL 2.1+ | Same as above. ✓ |
| Bungee | AGPL3 or paid commercial | Decision needed: pay for commercial license, OR replace with Rubber Band (also dual-licensed) or SoundTouch (LGPL, free as DLL). |
| libcurl | curl/MIT-style | Permissive. ✓ |
| OpenSSL | Apache 2.0 | Permissive. ✓ |
| VST3 SDK | GPL3 or Steinberg commercial | Register as Steinberg licensee (free for small devs, just paperwork). ✓ |
| NeuralPi | MIT | Bundle freely if desired. ✓ |

### Product licensing model (our stance)

The product will use a **straightforward purchase + activation key**
model, NOT hardware fingerprinting. Rationale:

- Hardware fingerprints are user-hostile (we saw this firsthand
  evaluating competitors)
- They break on legitimate use cases (laptop replacement, OS reinstall)
- They require us to run a license server, which is operational overhead
- A simple activation key + offline grace period is "good enough" and
  respects the user

Implementation: a signed JWT-style key (10-char alphanumeric) checked
against a public key embedded in the binary. No phone-home. No
fingerprint. Pirates will crack it, that's fine — we sell on quality
and ongoing updates, not on protection.

### Distribution

- Phase 8 release: direct download from product website
- Free, fully-featured **30-day trial**, single dialog, no server check
- Pricing TBD (~$60-90 one-time, comparable to mid-range arranger software)
- Future: optional subscription tier for style-pack updates

---

## 12. Reference Material

### Read for inspiration / patterns

- **MMA (Musical MIDI Accompaniment)** — http://www.mellowood.ca/mma/ —
  Python, GPL. Working open-source arranger engine. The chord-detection
  + pattern-transposition logic is conceptually identical to what we need.
- **Tracktion Engine** — https://github.com/Tracktion/tracktion_engine —
  Reference for a clean, modern JUCE audio architecture.
- **Helio Workstation** — https://github.com/helio-fm/helio-workstation —
  Smaller, more readable JUCE-based DAW.
- **Cardinal** — https://github.com/DISTRHO/Cardinal — Excellent
  example of in-app VST3 plugin hosting.

### JUCE tutorials worth following first

- "Build an Audio Plug-in" — sanity-check JUCE setup
- "Build a White Noise Generator" — first audio output
- "Handling MIDI events" — for MIDI input
- "WebBrowserComponent" docs — the bridge specifics

### Free SoundFonts we can bundle

- **GeneralUser GS** (~30 MB, top quality) —
  http://www.schristiancollins.com/generaluser.php
- **FluidR3 GM** (~140 MB, default FluidSynth set)
- **Salamander Grand Piano** (~CC license, premium piano)

---

## 13. Definition of Done (per phase, checklist style)

A phase is "done" when **all** of:

- [ ] Code compiles in Release mode without warnings on Windows x64
- [ ] All new code has unit tests where applicable
- [ ] CI is green (build + tests)
- [ ] A 30-second video clip demonstrates the new capability
- [ ] README is updated with any new architectural decisions
- [ ] No regressions in earlier phases' demos

---

## 14. Things to AVOID

Common pitfalls in audio-app development we want to head off early:

1. **Allocating in the audio callback.** Use pre-allocated buffers and
   lock-free FIFOs. The first time you put `new` in `processBlock` you'll
   get glitches that take days to debug.
2. **Locking the audio thread.** Same family of bug. `std::mutex` is
   forbidden in `processBlock`.
3. **String building in the audio path.** `juce::String` allocates.
   Pre-format messages.
4. **Frameworks for the UI bridge.** We picked vanilla JS deliberately:
   smaller bundle, no React/Vue lifecycle to wrestle inside the WebView.
   Don't reintroduce a framework "just for the dev experience."
5. **Trying to support Linux/Mac in Phase 1.** JUCE is cross-platform,
   yes, but FluidSynth + WebView quirks differ per OS. Ship Windows
   first; add platforms in Phase 8.
6. **Over-engineering the file format.** JSON, version field, backward
   compatibility tested. No protobuf, no FlatBuffers, no custom binary.
7. **Implementing your own synthesizer.** Use FluidSynth. The temptation
   to "just write a quick SoundFont parser" wastes weeks for no benefit.
8. **Building the auto-accompaniment first.** It's the hardest piece;
   it's tempting to start there because it's the "interesting" part.
   Don't. Build everything around it first so when you start that work
   you can hear results immediately.
9. **Cloning Giglad's exact UI.** Cadenza's UI is our own design,
   inspired by general arranger conventions. Don't pixel-match a
   competitor — it's both legally risky and limits our differentiation.
10. **Skipping accessibility.** Hardware-keyboard arrangers are often
    used by visually-impaired musicians (screen readers + tactile MIDI).
    Keep aria-* attributes maintained as the UI grows.

---

## 15. Contact + Handoff Notes

- This repository is a **prototype handoff**. The UI (HTML/CSS/JS) is
  complete and functional in a browser. Open `Cadenza Workstation.html`
  directly to see it.
- The UI was built by the project owner over several iterations; visual
  reference screenshots are in `screenshots/`.
- The C++ host (`Cadenza.exe`) does not yet exist — Phase 0 is to scaffold
  it.
- Companion documents elsewhere on the project owner's machine:
  - `REVERSE_ENGINEERING_NOTES.md` — analysis of competitor (Giglad)
    architecture, useful for understanding what features to prioritize.
  - `BUILD_YOUR_OWN_ARRANGER.md` — broader walkthrough of the library
    stack with realistic time estimates per milestone.

---

## 16. Quick-Reference Card

| Question | Answer |
|---|---|
| **Language?** | C++20 (host), JavaScript ES2020 (UI), CSS3 |
| **Framework?** | JUCE 8.x |
| **Build system?** | Projucer → Visual Studio 2022 |
| **Dependency manager?** | vcpkg (where possible) |
| **First milestone?** | Phase 0 — JUCE app loads the existing HTML in a native window |
| **Hardest milestone?** | Phase 6 — chord recognition + auto-accompaniment (3-6 weeks) |
| **Reasonable total scope?** | 4-9 months of half-time work to v1.0 |
| **Open-source / proprietary?** | Hybrid: open-source dependencies, proprietary product |
| **Platform target?** | Windows 10+ first, then macOS, then Linux |
| **License model?** | One-time purchase, activation key, no fingerprint |

---

*Last updated: 2026-05-29.
This document supersedes any earlier conversation notes.
For licensing-stack rationale and broader learning context, see the
companion documents on the project owner's Desktop.*
