# Getting sound out of Cadenza

By default Cadenza builds with `NullSynthEngine` — it accepts MIDI but
produces silence (notes go to the debug log). To make audible sound you
need two things:

1. **FluidSynth** installed at compile time, so the audio engine has a
   real synth implementation linked in.
2. A **`.sf2` SoundFont** in `resources/sf2/` for FluidSynth to play
   instruments from.

There is a script that automates the whole thing.

## The fast path (recommended)

```powershell
cd "C:\Users\suko5\Desktop\arranger workstation inspired by giglad"
.\scripts\setup-sound.ps1
```

What it does:

1. Checks for `git` on PATH (required by vcpkg).
2. Clones `vcpkg` into `%USERPROFILE%\vcpkg` if you don't have it, and
   bootstraps it.
3. Runs `vcpkg install fluidsynth:x64-windows`. **This step takes
   10-20 minutes the first time** — FluidSynth has a lot of
   dependencies (glib, libsndfile, etc.) and vcpkg builds them from
   source. Coffee break.
4. Re-runs CMake with the vcpkg toolchain so `find_package(FluidSynth)`
   succeeds.
5. Rebuilds Cadenza. The configure message should change from
   `Cadenza: FluidSynth NOT found - using NullSynthEngine fallback`
   to `Cadenza: FluidSynth available - real synth enabled`.
6. Prints next steps (the SoundFont).

After the script finishes you still need a SoundFont. The script tells
you which one to download and where to put it. The recommendation:

- **GeneralUser GS** by S. Christian Collins
- ~30 MB, free, very good quality
- https://schristiancollins.com/generaluser.php
- Download the zip, extract the `.sf2`, drop it into:

  `C:\Users\suko5\Desktop\arranger workstation inspired by giglad\resources\sf2\`

The next time Cadenza launches, it will auto-discover the file (any
`.sf2` in that folder works) and load it. You'll see something like
this in the log:

  `[Cadenza] SoundFont loaded: ...\GeneralUser GS v1.471.sf2`

## Verifying it worked

```powershell
& "C:\Users\suko5\Desktop\arranger workstation inspired by giglad\build-msvc\Cadenza_artefacts\Debug\Cadenza Workstation.exe"
```

Click any key on the on-screen piano. You should hear a piano note.

If still silent:

- Check the log for `[Cadenza] SoundFont` — if it says "failed" or
  doesn't appear at all, the `.sf2` isn't being found. Verify the path
  and the file extension.
- Make sure Windows audio is routed to a working device (the JUCE
  default device manager picks whatever's set as the system default).
- The factory style auto-loads on startup; pressing Play should produce
  drums + bass + harmony immediately.

## The manual path (if the script doesn't fit your setup)

If you already have vcpkg somewhere non-standard, set `VCPKG_ROOT` to
that path before running the script, OR do these steps yourself:

```powershell
# 1. From your existing vcpkg root:
.\vcpkg install fluidsynth:x64-windows

# 2. From the Cadenza root:
del build-msvc\CMakeCache.txt
cmake -S . -B build-msvc -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>\scripts\buildsystems\vcpkg.cmake" `
    "-DVCPKG_TARGET_TRIPLET=x64-windows"
.\build.bat

# 3. Drop a .sf2 file into resources\sf2\, then launch the exe.
```

## Reverting to NullSynthEngine

If you ever want to go back to the no-FluidSynth build (smaller binary,
faster compile, silent output):

```powershell
del build-msvc\CMakeCache.txt
cmake -S . -B build-msvc -G Ninja
.\build.bat
```

Without the vcpkg toolchain, `find_package(FluidSynth)` fails and the
configure step falls back to `NullSynthEngine`. The build still works.
