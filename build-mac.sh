#!/bin/bash
set -e

if ! xcode-select -p &>/dev/null; then
  echo "ERROR: Xcode Command Line Tools not installed. Run: xcode-select --install"
  exit 1
fi

if ! pkg-config --exists fluidsynth 2>/dev/null && ! pkg-config --exists fluid-synth 2>/dev/null; then
  if command -v brew &>/dev/null; then
    echo "FluidSynth not found. Installing via Homebrew..."
    brew install fluid-synth
  else
    echo "WARNING: FluidSynth not found and Homebrew is not installed."
    echo "The app will build but with NullSynthEngine (no sound)."
    echo "Install FluidSynth manually or install Homebrew from https://brew.sh"
  fi
fi

if [ ! -d "lib/JUCE" ]; then
  echo "Cloning JUCE 8.0.13..."
  git clone --depth 1 --branch 8.0.13 https://github.com/juce-framework/JUCE.git lib/JUCE
fi

echo "Configuring..."
cmake -S . -B build-mac -G Ninja -DCADENZA_BUILD_TESTS=ON

echo "Building..."
cmake --build build-mac --parallel

echo "Running core tests..."
ctest --test-dir build-mac --output-on-failure -R "^cadenza_(core|json|chord|style|transpose|transport|audioblock|runtime|drum|octave|livemelody|righthand|midimap|sections|section_flow|section_queue|style_queue|transport_command|mixer|compressor|voicemap|gm|settings|song|songplayer|arranger_router|ots|recorder|part_editor|part_workflow|note_workflow|audio_chord|first_launch|polished_master|ai_style|midi_style|pattern_import|style_sound)"

echo "ALL CORE TESTS PASSED"
