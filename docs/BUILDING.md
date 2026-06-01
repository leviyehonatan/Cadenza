# Building Cadenza

This repository now has a native-host scaffold around the existing web UI.

## Current Targets

- `cadenza_core`: standard C++20 state and bridge-routing library.
- `cadenza_core_tests`: unit tests for the pure C++ bridge/state behavior.
- `Cadenza`: JUCE GUI app target, enabled only when `lib/JUCE` exists.

## Setup

```powershell
git clone https://github.com/juce-framework/JUCE.git lib/JUCE
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

If no C++ compiler is available on `PATH`, run these commands from a Visual Studio 2022 Developer PowerShell.

## Runtime Assets

The build copies `resources/` beside `Cadenza.exe`. During development the app also looks for `resources/web/Cadenza Workstation.html` from the current working directory.

