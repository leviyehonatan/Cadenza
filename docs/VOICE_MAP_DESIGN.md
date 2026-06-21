# Voice Map — "a voice is a VST instrument + preset" (Giglad-style voicing)

Goal: let a style's GM program numbers resolve to high-quality VST3 voices (a
sampler + a saved preset) instead of the GM SoundFont, the way Giglad maps each
voice to a `maidi` sampler preset (see `GIGLAD_ARCHITECTURE.md`). This closes the
realism gap using Cadenza's existing per-part VST3 hosting.

## Data model (implemented: `cadenza::audio::VoiceMap`, pure core + tested)

```
VoiceMapEntry { pluginPath; presetState (optional); gain (optional CC7) }
VoiceMap:
  forProgram(gmProgram 0..127) -> exact program, else family (program/8), else null
  forDrums()                   -> drum-channel instrument, or null
```

### File format (`resources/voicemap.json`, optional)
```json
{
  "drums":    { "plugin": "C:/.../MT-PowerDrumKit.vst3" },
  "programs": {
    "0":  { "plugin": "C:/.../sforzando.vst3", "state": "<vst3 state>" },
    "33": { "plugin": "C:/.../Bass.vst3", "gain": 100 }
  },
  "families": {
    "3": { "plugin": "C:/.../Guitar.vst3" },   // GM 24..31
    "5": { "plugin": "C:/.../Strings.vst3" }    // GM 40..47
  }
}
```
- `programs` = exact GM program (wins). `families` = 0..15, each covers 8 GM
  programs (a low-effort way to cover whole instrument groups).
- Entries without `plugin` are ignored; missing file → app falls back to GM.

## Resolution order (per part, on style load)
```
part is drums?  -> VoiceMap.forDrums()
else            -> VoiceMap.forProgram(part.program)   // exact, then family
if an entry exists and its plugin loads:
      load it on the part's channel (apply preset state + gain)
else: keep the current GM-SoundFont voice (today's behaviour)
```

## Wiring plan (NOT yet wired — needs a careful, reviewed pass)
The core type is ready. Hooking it into playback touches the live audio path, so
it should be done deliberately (we had audio regressions before):

1. `AudioEngine`: add `loadPartInstrument(channel, path, stateBlob)` overload that
   restores the VST3 state after load (extend `PluginHost` to `setStateBlob`).
2. `MainComponent`: own a `VoiceMap`, load `resources/voicemap.json` at startup.
3. In `updateNativePanelStyle` / `applyStyleMix`, for each part with **no
   per-style plugin override**, consult the VoiceMap and load the mapped VST.
   Per-style explicit choices (existing `pluginPath`) still win.
4. Reconcile (reuse the existing idempotent `loadPartInstrument`) so switching
   styles doesn't rebuild unchanged plugins.
5. Settings toggle: "Use voice map" on/off so the user can A/B against GM.

### Why this reaches Giglad quality
Giglad = one sampler VST + a curated library + a preset per voice. With a VoiceMap
pointing at a good sampler (Decent Sampler / sforzando / Kontakt) and a sample
library, Cadenza does the same thing: every style automatically plays great voices,
with per-style overrides and the GM SoundFont as the safety net.

## Status (wired 2026-06-21)
- [x] `VoiceMap` core type + JSON format + unit tests (`cadenza_voicemap_tests`).
- [x] `VoiceMap` in-memory setters + `toJson()` (for the capture flow).
- [x] `PluginHost` state-blob get/set (`getStateBlob`/`setStateBlob`).
- [x] `AudioEngine` load-with-state (base64 VST3 state) + `capturePartInstrumentState`.
- [x] `MainComponent` wiring: loads `%APPDATA%/Cadenza/voicemap.json`, applies on
      style load to channels without a per-style override (gated by `useProVoices`,
      GM SoundFont fallback).
- [x] Curation UX: "Set as default voice..." in each part's instrument menu →
      captures the loaded sforzando+SFZ state and writes `voicemap.json`
      (Drums / Bass=family 4 / Piano=family 0).
- [x] Settings toggle "Use pro voices (SFZ)" in the MIDI Settings panel (live A/B
      against the GM SoundFont).
- [ ] Full visual voice-map editor (future; the capture flow + JSON suffice).
- [ ] More voices (guitar/strings/brass/organ) + the VCSL all-in-one library (future).
