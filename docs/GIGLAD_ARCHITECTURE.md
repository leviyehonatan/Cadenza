# Giglad Architecture — Reverse-Engineering Notes (for Cadenza)

Summary of how Giglad (the commercial arranger that inspired Cadenza) is built,
derived from inspecting the files it ships and prior static analysis. No DRM was
bypassed; this records **architecture and tech stack** to inform Cadenza's design.
It does **not** copy Giglad code or sample data.

Source material: `Desktop/New folder (4)` (bundled DLLs/VSTs, `Dynamix Audio`
program files, and the `*_analysis` / `*.md` RE writeups).

---

## 1. Tech stack (what Giglad bundles)

| Component | Role |
|---|---|
| **`maidi.vst3`** | Giglad's own **VST3 sampler** — plays every instrument voice |
| `libfluidsynth-3.dll` + `libinstpatch-2.dll` | SoundFont/DLS/sample loading backend used under the sampler |
| `libsndfile-1.dll` | audio file I/O (sample loading) |
| `bungee.dll` | time-stretch (tempo change without pitch shift) |
| `NeuralPi.vst3` | guitar amp/cab modeler for guitar voices |
| `*.scl` (Scala) | microtuning / historical temperaments supported by `maidi` |
| glib/gobject, libsndfile, zlib, yaml | sampler/runtime deps + config |
| libcurl, libcrypto | networking (API server / licensing / updates) |

Key point: Giglad uses **FluidSynth + libinstpatch** as a backend — the *same*
synth family Cadenza uses — but the audible quality does **not** come from a GM
SoundFont (see §2).

---

## 2. Voicing / sound engine (the important part)

Giglad's voices are **VST3 sampler presets**, not General MIDI. Each instrument is
a program file (`.pjson`) that instantiates the `maidi` sampler with a saved VST3
state blob. Example (`Dynamix Audio/Programs/.../(0) Grand Piano.pjson`):

```json
{
  "program": {
    "instrument": { "format": "VST3", "name": "maidi", "state": "<VST3 state blob>" },
    "sends": [ { "name": "Reverb 1", "gain": 0.126 } ],
    "pre_effects": "null", "post_effects": "null"
  }
}
```

- Voices are organized GM-style by family:
  `(0) Piano … (3) Guitar (4) Bass (5) Strings … (16) Acoustic Drums (17) Electro Drums`.
- Quality comes from a **curated sample library ("Dynamix Audio")** played by the
  sampler — not GM.
- Each voice carries **effect sends** (e.g. reverb) and optional pre/post effects;
  guitars route through **NeuralPi**.

**Takeaway for Cadenza:** "a voice = a VST instrument + a preset state + a reverb
send." This is exactly Cadenza's per-part VST3 hosting. The gap vs Giglad is the
*sample library*, not the engine. See `VOICE_MAP_DESIGN.md`.

---

## 3. Arranger / style engine

- **Centralized `ArrangerState`** with an observer/property architecture. Confirmed
  fields: `tempo`, `tempo_locked`, `split_note_number`, `transposition`, `octave`,
  `chords_arranger`, `chords_bass`, `chords_memory`, `syncro_start`, `syncro_stop`,
  `fade_in`, `fade_out`, `playing`. State changes notify the engine/UI.
- **Split routing:** `note < splitNote → chord side; else → melody side`. Full-
  Keyboard modes also feed above-split notes to chord detection.
- **Chord-detection modes (9):** Single Finger, Fingered, Fingered Incomplete,
  Fingered On Bass, Multi Finger, Full Keyboard, Full Keyboard (No Interval),
  Fingered On Bass (Nazarian), Single Finger (Patterns).
- **Chord recognition = "ChordSeeker":** held notes → normalized **interval
  vector** → hashed into **buckets keyed by interval-count** → matched against a
  chord-definition template tree. Pure template matching.
- Style sections carry per-section timing/precision; transport supports
  syncro start/stop, fades, tempo lock, and arranger-state recall/persistence.
- The arranger is partly driven through an internal **JSON/API** layer (`gapi`),
  i.e. state keys are addressed by string id.

---

## 4. How Cadenza compares

| Aspect | Giglad | Cadenza |
|---|---|---|
| Synth backend | FluidSynth + libinstpatch (under `maidi`) | FluidSynth ✅ |
| Arranger state | ArrangerState + observers | ArrangerState ✅ |
| Split / chord modes | split + 9 modes | split + fingered modes ✅ (fewer modes) |
| Chord engine | interval-vector template buckets | pitch-class/quality recognizer ✅ |
| Per-part instruments | `maidi` sampler preset per voice | per-part VST3 hosting ✅ |
| **Sound source** | **curated sample library** | **GM SoundFont (Timbres) + optional VSTs** ⬅ the gap |

**Conclusion:** Cadenza already matches Giglad architecturally. The realism gap is
the **sound library**. The path to Giglad-class sound is the `maidi` model: map
each voice to a sampler VST + preset. That design is specified in
`VOICE_MAP_DESIGN.md` and backed by the `cadenza::audio::VoiceMap` core type.
