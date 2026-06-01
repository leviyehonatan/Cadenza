// SettingsStore — persistent application state on disk.
// Pure C++; uses cadenza::json. Stores BPM, key, last style, last preset etc.

#pragma once

#include <optional>
#include <string>

namespace cadenza::settings
{
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
