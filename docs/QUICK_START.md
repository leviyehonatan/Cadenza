# Cadenza Workstation — Quick Start

Cadenza is a software arranger keyboard for Windows. Play chords with your
left hand and a full backing band (drums, bass, comping, pads, phrases)
follows you in real time, just like a Yamaha/Korg/Ketron arranger keyboard.

## Requirements

- Windows 10/11, 64-bit.
- An audio output (any soundcard; ASIO optional for lowest latency).
- A MIDI keyboard is recommended — but the on-screen keyboard works too.
- Microsoft Edge WebView2 Runtime (already present on Windows 11; on older
  systems download it free from Microsoft if the Web UI doesn't open).

## First run

1. Unzip the folder anywhere (e.g. `C:\Cadenza`) and run
   **`Cadenza Workstation.exe`**.
2. The bundled XG SoundFont loads automatically — you should be able to play
   the on-screen keyboard right away.
3. Click **Open Style** and pick a Yamaha style file (`.sty`, e.g. from your
   keyboard or a style collection), or the bundled demo style.
4. Plug in a MIDI keyboard any time — it is detected automatically.

## Playing (like a real arranger)

- **Split point:** keys below the marker (drag it!) are the *chord zone*;
  keys above play your melody (Right 1/2/3 voices).
- **Play** (or **Syncro Start**): just play a chord in the chord zone and the
  band starts with you.
- **Sections:** Intro, Main A–D, Break, Ending buttons. With **Auto Fill** on,
  pressing another Main inserts its fill-in first — exactly like the
  AUTO FILL IN button on a Yamaha.
- **Syncro Stop:** lift your left hand and the band pauses; touch a chord and
  it comes back in.
- **Tap:** tap the tempo with the Tap button.
- **Fade:** fades the whole band out over a few seconds and stops — a clean
  song ending without an arranged Ending section.
- **OTS 1–4:** One Touch Settings — recall the style's suggested right-hand
  voices. **OTS Link** switches them automatically with Main A–D.
- **Registrations 1–4:** store/recall your complete panel setup (style, tempo,
  voices, split, EQ...) with one button.

## Mixer & sound

- Every accompaniment part has a strip: volume, mute, solo, and an instrument
  picker (GM voices, drum kits, or **a VST3 instrument per part**).
- Master EQ (Low/Mid/High), Comp, Master, Drums, Reverb knobs shape the mix.
- **Audio** button: pick your real output device (or ASIO driver).
- **Open SoundFont**: swap in any `.sf2` (XG-compatible ones sound best with
  Yamaha styles).

## Song mode

Load a `.csong` chord chart (bar → section + chord) and Cadenza plays the
arrangement for you, switching sections and chords automatically and stopping
at the end of the chart.

## Tools (in `tools\`)

- `style-probe.exe MyStyle.sty Am mainA` — inspect how a style parses and
  render the section to MIDI.
- `style-scan.exe <folder> --csv report.csv` — batch-check a style library.
- `sty-to-cstyle.exe` — convert `.sty` to Cadenza's editable `.cstyle` JSON.

## Where settings live

`%APPDATA%\Cadenza\` — settings.json (your setup, registrations, per-style
mixes), audio-device.xml, and cadenza.log (diagnostics for bug reports).
