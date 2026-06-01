# Converting Yamaha `.sty` files to Cadenza `.cstyle`

Cadenza ships with `sty-to-cstyle.exe`, a standalone converter that
reads Yamaha-format arranger style files (or plain Standard MIDI Files)
and produces Cadenza's `.cstyle` JSON.

## What the converter does

1. Parses the Standard MIDI File portion of the input
   (`MThd` header + `MTrk` tracks).
2. Detects **section markers** — text meta-events like `Main A`,
   `Main B`, `Intro A`, `Fill In AA`, `Ending A`, etc. The mapping is
   case-insensitive and tolerates spaces, underscores, hyphens.
3. Splits notes into Cadenza sections based on those markers.
4. Groups notes by MIDI channel into Cadenza "parts".
5. Assigns each note a **chord role** with this heuristic
   (assuming the source chord is C major, which is Yamaha's
   convention):

   | Channel | Pitch class | Role |
   |---|---|---|
   | 10 (drums) | anything | `absolute` |
   | non-drum | C  (0)  | `chord-root` |
   | non-drum | E  (4)  | `chord-3` |
   | non-drum | G  (7)  | `chord-5` |
   | non-drum | Bb (10) | `chord-7` |
   | non-drum | B  (11) | `chord-7` |
   | non-drum | other   | `absolute` (passes through unchanged) |

6. Names parts by channel (`bass` on ch 2, `harmony` on ch 3, drums
   gets `drums`, others get `part-chN`).
7. Picks the GM instrument name from the most-recent program change
   on the channel.
8. Writes the result as pretty-printed JSON.

What it does NOT do (yet):

- It ignores the Yamaha-specific `CASM`/`CSEG`/`Sdec`/`Ctab` chunks
  that come after the SMF data. Those describe per-channel
  "Note Transposition Rule / Type" — adding them would give more
  accurate role assignment for melodic ornaments and colour tones.
- It picks one section per marker; some Yamaha styles have multiple
  variations (`Intro A`/`B`/`C`) that all map to `intro` in v1.
- It assumes 4/4 time. Non-4/4 styles will still convert but the bar
  count will be off.

For most user-created Yamaha styles, the v1 heuristic produces patterns
that are immediately playable. Hand-edit the `.cstyle` JSON to fix
colour tones if needed.

## Usage

```powershell
cd "C:\Users\suko5\Desktop\arranger workstation inspired by giglad"
.\build-msvc\sty-to-cstyle.exe input.sty resources\factory\styles\my-style.cstyle
```

Optional arguments:

```
--name "Pop 8 Beat"       Override the display name in the .cstyle file
--id pop-8-beat           Override the style id slug
--tempo 110               Default BPM if the source file has no tempo event
--verbose                 Print parse diagnostics to stderr
```

After conversion, the new `.cstyle` lives in
`resources/factory/styles/` and will be loadable by Cadenza. To make
Cadenza load it on startup, either:

- Rename it to `8-beat-pop.cstyle` (overwrites the bundled factory one
  that Cadenza auto-loads), or
- Send a `selectStyle` message from the web UI with `name` set to
  the new style's id or name. `MainComponent::installBridgeHooks` →
  `onSelectStyle` scans `resources/factory/styles/*.cstyle` and loads
  the first match.

## Quick start: convert a sample MIDI

Don't have a `.sty` file handy? Any plain `.mid` works:

```powershell
.\build-msvc\sty-to-cstyle.exe C:\path\to\anything.mid resources\factory\styles\test.cstyle --name "Test" --id test
```

Open the output in a text editor to see the structure. It's JSON; no
hidden binary.

## Where to find `.sty` files you actually have a right to use

- **Files you created yourself** in an arranger keyboard or via tools
  like StyleWorks. These are your own work, convert freely.
- **Community-shared user creations** on arranger keyboard forums
  (Yamaha PSR-tutorial, KeyboardForums, etc.) — many users have
  released their original style creations as free downloads. Check
  the license/credit notes on each download page.
- **Bundled "demo" styles** that ship with hardware you own may have
  EULA restrictions on redistribution. Personal use on your own
  machine usually fine; sharing the converted result is usually not.

What NOT to convert: paid commercial style packs (Yamaha StyleMagic,
DigitalSoundFactory, etc.), the proprietary library bundled with any
specific arranger software product. Those are licensed material.

## Round-trip: converter output is also a valid Cadenza .cstyle

The converter writes through the same `cadenza::arranger::saveStyleToJson`
that Cadenza itself uses for save. So the output is round-trippable —
load it back with `cadenza::arranger::loadStyleFromFile` and you get
the same in-memory `Style` you'd get from any other `.cstyle` source.
This is covered by `cadenza_style_tests` and `cadenza_sty_tests`.

## Verifying the converter

```powershell
cd build-msvc
ctest -R cadenza_sty_tests --output-on-failure
```

The test builds a synthetic 2-track MIDI file in memory (3 section
markers, 4 notes on ch 2, 1 drum hit on ch 10) and checks every
extracted detail: section split points, role assignment, drum channel
handling, marker-name normalisation.

## Hacking on the converter

All of it lives under `tools/sty-converter/`:

- `StyParser.h` / `StyParser.cpp` — the SMF parser, section marker
  table, role-assignment heuristic, GM instrument name table.
- `main.cpp` — the CLI argument parser.
- `StyParserTests.cpp` — the in-memory synthetic-MIDI test.

The first improvement most people want is **better role assignment**.
Right now any pitch class outside {0,4,7,10,11} becomes `absolute` —
which means colour tones don't follow the chord. To upgrade:

1. Parse the Yamaha `CASM` chunk after the SMF data. It contains a
   `CSEG` per section, with `Sdec` (section decoration) and `Ctab`
   (chord table) — the chord table specifies per-channel "Note
   Transposition Rule" (NTR: Root Trans, Root Fixed, Guitar) and
   "Note Transposition Type" (NTT: Bypass, Melody, Chord, Bass, …).
2. Apply NTR/NTT to map each note relative to the source chord
   declared in CASM.

That's a few hundred extra lines and the difference between "good
enough" and "exactly what the hardware would have played."
