// FirstLaunchChecklist — pure logic behind the first-launch "Quick Start"
// assistant. Turns the app's current audio / SoundFont / style / MIDI state into
// an ordered list of checklist rows the UI can render. No JUCE here so it can be
// unit-tested in cadenza_core.

#pragma once

#include <string>
#include <vector>

namespace cadenza::ui
{
// A snapshot of everything the first-launch checklist needs to know.
struct FirstLaunchInputs
{
    bool        audioReady       = false;  // an audio output device is open
    std::string audioDeviceName;
    bool        synthAvailable   = false;  // real synth, not the silent Null fallback
    bool        soundFontLoaded  = false;
    std::string soundFontName;
    bool        styleLoaded      = false;
    std::string styleName;
    bool        midiConnected    = false;  // a hardware MIDI input exists (optional)
    std::string midiDeviceName;
};

// One row in the checklist.
struct ChecklistItem
{
    std::string label;             // e.g. "Audio output"
    std::string detail;            // e.g. "Speakers (Realtek)" or a fix hint
    bool        ok       = false;  // is this item satisfied?
    bool        required = true;   // required to hear sound, or optional?
};

// Build the ordered checklist a first-time user sees: audio, sound, style
// (required) then MIDI keyboard (optional — the on-screen keyboard always works).
std::vector<ChecklistItem> buildFirstLaunchChecklist(const FirstLaunchInputs& in);

// True when every REQUIRED item is satisfied (i.e. the user will hear sound).
bool allRequiredReady(const std::vector<ChecklistItem>& items);
}
