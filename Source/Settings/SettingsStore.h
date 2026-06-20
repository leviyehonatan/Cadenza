// SettingsStore — persistent application state on disk.
// Pure C++; uses cadenza::json. Stores BPM, key, last style, last preset etc.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace cadenza::settings
{
// One mixer strip's saved state for a style. -1 means "keep the style default".
struct StyleChannelMix
{
    int  channel = 0;
    int  program = -1;
    int  volume  = -1;
    bool mute    = false;
    bool solo    = false;
    std::string pluginPath;          // VST3 instrument for this channel ("" = GM SoundFont)
};

// One of the three layered right-hand voices (Right 1 / Right 2 / Right 3).
struct RightLayer
{
    bool enabled = false;   // does this layer sound?
    int  program = 0;       // GM program 0..127 (used when pluginPath is empty)
    int  volume  = 100;     // 0..127
    int  octave  = 0;       // octave shift, e.g. -2..+2
    std::string pluginPath; // VST3 instrument for this layer ("" = GM voice)
};

// A registration: a one-button snapshot of the live performance setup, recalled
// instantly. Empty (used == false) slots are shown but do nothing on recall.
struct Registration
{
    bool used = false;
    std::string name;            // optional label
    std::string styleId;         // style to (re)load on recall ("" = leave current)
    std::string stylePath;       // file path fallback when the id can't be resolved
    int bpm = 120;
    int transpose = 0;
    int octave = 0;
    int splitNote = 60;
    int eqLowDb = 0, eqMidDb = 0, eqHighDb = 0, compAmount = 0;
    std::string bankMemory;      // Right 1 voice name
    bool chordArrangerEnabled = true;
    bool chordMemoryEnabled = false;
    bool chordBassEnabled = false;
    bool syncroStopOnRelease = true;
    bool autoFillEnabled = true;
    RightLayer rightLayers[3];
};

struct Settings
{
    int bpm = 120;
    int transpose = 0;
    int octave = 0;
    int melodyProgram = 0;           // GM program for Right 1 (mirrors rightLayers[0].program)

    // The three layered right-hand voices. Right 1 is on by default and tracks
    // melodyProgram / the bank-memory buttons; Right 2 and 3 are off until enabled.
    RightLayer rightLayers[3] = { { true, 0, 100, 0 }, { false, 0, 100, 0 }, { false, 0, 100, 0 } };

    // One-button performance snapshots (registrations).
    static constexpr int kNumRegistrations = 4;
    Registration registrations[kNumRegistrations];
    std::string key = "C";
    std::string bankMemory = "Piano";
    int styleMemory = 1;
    std::string lastStyleId;
    std::string lastStylePath;
    std::string lastSongId;
    std::string lastSoundFontPath;
    std::string midiInputDevice;     // empty = auto (main keyboard port, skip aux ports)
    std::string midiChordMode = "fingered";  // fingered|single|multi|onbass|full
    int crossfade = 50;
    bool chordBassEnabled = true;
    bool chordArrangerEnabled = true;
    bool chordMemoryEnabled = false;
    bool syncroStopOnRelease = true;
    bool autoFillEnabled = true;     // pressing a Main while playing fills into it
    bool otsLinkEnabled = false;     // auto-recall OTS 1-4 when Main A-D starts

    // Master 3-band EQ gains in dB. Default adds low-end body + a little air so
    // the GM mix isn't flat/thin; the player can retune these.
    int eqLowDb = 4;
    int eqMidDb = 0;
    int eqHighDb = 2;
    int compAmount = 55;             // master compressor amount 0..100 (0 = off)
    int masterVolume = 100;          // master output volume 0..127 (100 = unity)
    int reverbLevel = 80;            // master reverb amount 0..100
    int splitNote = 60;              // keyboard split: notes < this drive chords, >= play melody
    int humanizeAmount = 35;         // 0..100 accompaniment velocity/timing variation (0 = off)

    // MIDI control mappings: trigger id (see MidiControlMap) -> command string
    // (section id or "play"). Lets hardware buttons drive the arranger.
    std::map<int, std::string> midiControlMap;

    // Per-style mixer overrides keyed by style id. Applied on top of the style's
    // own defaults when a style is (re)loaded, so the player's instrument/volume/
    // mute/solo tweaks persist per style.
    std::map<std::string, std::vector<StyleChannelMix>> styleMixes;
};

class SettingsStore
{
public:
    explicit SettingsStore(std::string path);

    // Loads from disk. Returns true on success. If file missing, Settings stays at defaults.
    bool load();

    // Saves to disk. Returns true on success.
    bool save() const;

    Settings&       state()       noexcept { return m_state; }
    const Settings& state() const noexcept { return m_state; }
    const std::string& path() const noexcept { return m_path; }

private:
    std::string m_path;
    Settings    m_state;
};
}
