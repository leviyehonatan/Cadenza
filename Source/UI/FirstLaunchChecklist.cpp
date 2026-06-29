#include "FirstLaunchChecklist.h"

#include <utility>

namespace cadenza::ui
{
std::vector<ChecklistItem> buildFirstLaunchChecklist(const FirstLaunchInputs& in)
{
    std::vector<ChecklistItem> items;

    // 1) Audio output (required). Needs both an open device and a real synth;
    //    the Null fallback would leave playback silent/log-only.
    {
        ChecklistItem it;
        it.label    = "Audio output";
        it.required = true;
        it.ok       = in.audioReady && in.synthAvailable;
        if (!in.audioReady)
            it.detail = "No audio device - open Audio Settings";
        else if (!in.synthAvailable)
            it.detail = "Synth unavailable - playback would be silent";
        else
            it.detail = in.audioDeviceName.empty() ? "Ready" : in.audioDeviceName;
        items.push_back(std::move(it));
    }

    // 2) Sound (SoundFont) (required).
    {
        ChecklistItem it;
        it.label    = "Sound (SoundFont)";
        it.required = true;
        it.ok       = in.soundFontLoaded;
        it.detail   = in.soundFontLoaded
                        ? (in.soundFontName.empty() ? "Loaded" : in.soundFontName)
                        : "None - choose a .sf2 SoundFont";
        items.push_back(std::move(it));
    }

    // 3) Style (required).
    {
        ChecklistItem it;
        it.label    = "Style";
        it.required = true;
        it.ok       = in.styleLoaded;
        it.detail   = in.styleLoaded
                        ? (in.styleName.empty() ? "Loaded" : in.styleName)
                        : "None - load a factory style";
        items.push_back(std::move(it));
    }

    // 4) MIDI keyboard (optional — the on-screen keyboard always works).
    {
        ChecklistItem it;
        it.label    = "MIDI keyboard";
        it.required = false;
        it.ok       = in.midiConnected;
        it.detail   = in.midiConnected
                        ? (in.midiDeviceName.empty() ? "Connected" : in.midiDeviceName)
                        : "Optional - use the on-screen keyboard below";
        items.push_back(std::move(it));
    }

    return items;
}

bool allRequiredReady(const std::vector<ChecklistItem>& items)
{
    for (const auto& it : items)
        if (it.required && !it.ok)
            return false;
    return true;
}
}
