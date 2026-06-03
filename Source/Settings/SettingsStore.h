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

struct Settings
{
    int bpm = 120;
    int transpose = 0;
    int octave = 0;
    int melodyProgram = 0;           // GM program for the live right-hand melody voice
    std::string key = "C";
    std::string bankMemory = "Piano";
    int styleMemory = 1;
    std::string lastStyleId;
    std::string lastStylePath;
    std::string lastSongId;
    std::string lastSoundFontPath;
    std::string midiInputDevice;     // empty = system default
    int crossfade = 50;
    bool chordBassEnabled = true;
    bool chordArrangerEnabled = true;
    bool chordMemoryEnabled = false;
    bool syncroStopOnRelease = true;

    // Master 3-band EQ gains in dB. Default adds low-end body + a little air so
    // the GM mix isn't flat/thin; the player can retune these.
    int eqLowDb = 4;
    int eqMidDb = 0;
    int eqHighDb = 2;
    int compAmount = 55;             // master compressor amount 0..100 (0 = off)

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
