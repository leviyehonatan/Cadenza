# Contributing to Cadenza

Thanks for your interest! Cadenza is an open-source software arranger, and help is
very welcome — code, bug reports, test styles, and **especially music-theory
knowledge**.

## We especially need musicians

The hardest, highest-value problems in Cadenza are musical, not just technical:

- **Chord voicing / voice-leading** when a style is transposed to the chord you play.
- **Yamaha NTR/NTT transposition fidelity** — how source notes should map to the
  played chord, scale/color-tone handling, note limits.
- **Drum fills & section flow** — what makes a fill and a section transition feel right.
- **Genre feel** — accurate grooves for different styles.

If you understand arranger keyboards / music theory and can describe "this should
sound like X, not Y," that feedback is gold even without writing code. Open an issue.

## Building

See the build steps in the [README](README.md). In short (Windows, MSVC + Ninja):

```powershell
git clone https://github.com/juce-framework/JUCE.git lib/JUCE
cmake -S . -B build-msvc -G Ninja
cmake --build build-msvc
ctest --test-dir build-msvc --output-on-failure
```

Please make sure `ctest` is green before opening a PR, and add tests for logic you
change (the arranger/converter code has unit tests under `tests/`).

## Pull requests

- Keep changes focused; describe what and why.
- Match the surrounding code style (C++20, JUCE conventions).
- No new dependencies without discussion.
- Don't commit SoundFonts, Yamaha style files, build output, or API keys.

## Contributor License Agreement (please read)

Cadenza is GPLv3, and its author also offers commercial licenses (see the README).
To keep that possible, by submitting a contribution you agree that:

- you have the right to contribute the code, and
- you grant the project author (Cadenza, suko5rp2@gmail.com) the right to license
  your contribution both under the GPLv3 **and** under separate commercial terms.

You retain copyright to your contribution. If you do not agree to this, please open
an issue to discuss before contributing code. (This is the standard dual-licensing
arrangement used by many open-source projects.)
