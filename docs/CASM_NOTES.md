# CASM Parsing Notes

CASM support now has two layers. The parser still builds the playable `Style`
from the Standard MIDI File portion, but it also attaches a lightweight
`YamahaChannelPolicy` to generated `.sty` parts when Ctab/Ctb2 metadata is
available. `StyleEngine` playback scheduling is unchanged; the policy is used
only for conservative note-role assignment during import.

## What is detected

After the declared SMF `MTrk` chunks are parsed, `StyParser` scans the remaining
bytes for a `CASM` chunk. When found, it records:

- CASM byte offset
- declared CASM payload size
- parsed payload size
- top-level child block tags
- warning messages for truncated or malformed block boundaries

The parser does not fail the style import when CASM is malformed. CASM warnings
are stored in `StyParseResult::casm.warnings`.

## Parsed block structure

The current parser safely walks this chunk shape:

```text
CASM
  CSEG
    Sdec
    Ctab
```

For each `CSEG`, it records:

- raw `CSEG` payload
- child block tags
- `Sdec` raw bytes
- printable `Sdec` text as a provisional section name/id when available
- `Ctab` entries

## Parsed Ctab fields

The parser supports three conservative `Ctab`/`Ctb2` cases.

The first is labelled ASCII data, used by early synthetic tests:

- `CH` or `CHANNEL`
- `NTR`
- `NTT`
- `ROOT` or `SOURCEROOT`
- `CHORD` or `SOURCECHORD`
- `SECTION` or `SECTIONID`

Unknown labelled fields are retained in `CasmCtabEntry::unknownFields`. The raw
`Ctab` payload is always retained in `CasmCtabEntry::raw`.

The second is the old Yamaha binary `Ctab` layout documented by Peter Wierzba
and Michael P. Bedesem in "Style Files - Introduction and Details" v2.1. Only
these byte offsets are decoded:

| Byte | Field | Stored as |
| --- | --- | --- |
| 0 | source MIDI channel, zero-based `00H..0FH` | `channelRaw`, `channel` |
| 18 | source root `00H..0BH` | `sourceRootRaw`, `sourceRoot` |
| 19 | source chord type `00H..22H` | `sourceChordRaw`, `sourceChord` |
| 20 | NTR `00H..01H` | `ntrRaw`, `ntr` |
| 21 | NTT `00H..05H` | `nttRaw`, `ntt` |

Known names currently mapped:

- Source roots: `C`, `C#`, `D`, `Eb`, `E`, `F`, `F#`, `G`, `G#`, `A`, `Bb`, `B`
- NTR: `Root Transposition`, `Root Fixed`
- NTT: `Bypass`, `Melody`, `Chord`, `Bass`, `Melodic Minor`, `Harmonic Minor`
- Source chord types: the documented `Maj` through `cancel` range `00H..22H`

All other bytes are intentionally left uninterpreted and remain available in
the raw payload. If a binary payload is shorter than the supported fixed
offsets, the parser keeps a raw entry without assigning field meanings.

The third is the newer Yamaha binary `Ctb2` layout. The first/common part uses
the same source channel/root/chord offsets as `Ctab`, so those are decoded from
the same bytes. For NTR/NTT, only one observed and documented pattern is decoded:

| Byte | Field | Stored as |
| --- | --- | --- |
| 0 | source MIDI channel, zero-based `00H..0FH` | `channelRaw`, `channel` |
| 18 | source root `00H..0BH` | `sourceRootRaw`, `sourceRoot` |
| 19 | source chord type `00H..22H` | `sourceChordRaw`, `sourceChord` |
| 20 | lowest middle-note boundary | used only when `00H` |
| 21 | highest middle-note boundary | used only when `7FH` |
| 28 | middle-range NTR | `ntrRaw`, `ntr` |
| 29 | middle-range NTT plus bass-on bit | `nttRaw`, `ntt` |

The local real styles inspected in this pass all used `Ctb2` with bytes
20/21 set to `00H 7FH`, meaning the middle-note substructure covers the full
range. In that case bytes 28/29 are treated as the effective NTR/NTT pair.
If bytes 20/21 are not `00H 7FH`, the parser keeps the raw data and records
the split boundary as unknown rather than guessing which range applies.

Known `Ctb2` NTT names currently mapped:

- `00H` / `80H`: `Bypass`
- `01H` / `81H`: `Melody`
- `02H` / `82H`: `Chord`
- `03H` / `83H`: `Melodic Minor`
- `04H` / `84H`: `Melodic Minor 5th Var.`
- `05H` / `85H`: `Harmonic Minor`
- `06H` / `86H`: `Harmonic Minor 5th Var.`
- `07H` / `87H`: `Natural Minor`
- `08H` / `88H`: `Natural Minor 5th Var.`
- `09H` / `89H`: `Dorian`
- `0AH` / `8AH`: `Dorian 5th Var.`

Values with bit 7 set are labelled with ` (Bass On)`.

## Verbose logging

When `StyParseOptions::verbose` is enabled, the parser logs:

- CASM found / not found
- CASM warnings
- number of `CSEG` blocks
- number of `Ctab` entries
- hex dump of every `Ctab` payload
- any discovered NTR/NTT values

The same log lines are stored in `StyParseResult::casm.logLines`.

## Playback policy use

The current importer uses policy only in safe cases:

- channel 10/drums remain absolute
- `NTT=Bypass` remains absolute
- `Root Fixed + Chord` maps source chord tones to existing Cadenza chord roles
- `Root Transposition + Melody` keeps the previous fallback behavior because
  Cadenza has no separate melody-role model yet
- `Bass On` marks the generated part as bass and uses root-following chord roles
- unknown policy values fall back to the old C-major heuristic

All generated `.sty` parts receive a `YamahaChannelPolicy`. If CASM has no
usable channel entry for that part, its policy source is `Fallback`.

## Still unknown

The following are intentionally not implemented yet:

- `Ctb2` split-range selection beyond the observed full-range `00H 7FH` case
- full Yamaha phrase fitting
- SFF2 low/main/high range playback
- retrigger behavior during chord changes
- source-channel selection by muted roots/chords
- source chord/root application inside `PatternTransposer`
- Section-to-CASM matching beyond readable `Sdec` text
- Any playback changes in `StyleEngine`

Real `.sty` files inspected on 2026-05-30:

- `C.ARDAS-RUDY1.S919.STY`: CASM found, 13 `CSEG` blocks, 92 `Ctb2` entries
- `CULY.STY`: CASM found, 15 `CSEG` blocks, 108 `Ctb2` entries
- `CULY-ext.STY`: CASM found, 15 `CSEG` blocks, 108 `Ctb2` entries

All three used `Ctb2` rather than old `Ctab` records.

## Next step

Add real Yamaha `.sty` fixtures with known CASM settings, compare their `Ctab`
hex dumps to the decoded metadata, and then implement full phrase fitting in a
dedicated Yamaha playback layer instead of expanding the generic
`PatternTransposer` heuristics.
