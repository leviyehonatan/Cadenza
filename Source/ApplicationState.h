#pragma once

#include <array>
#include <string>
#include <unordered_map>

namespace cadenza
{
struct ChannelState
{
    int volumeDb = 0;
    int pan = 0;
    bool solo = false;
    bool mute = false;
    std::string instrument;
};

class ApplicationState
{
public:
    static constexpr int minBpm = 40;
    static constexpr int maxBpm = 240;
    static constexpr int minTranspose = -12;
    static constexpr int maxTranspose = 12;
    static constexpr int minOctave = -4;
    static constexpr int maxOctave = 4;
    static constexpr int minPan = -50;
    static constexpr int maxPan = 50;
    static constexpr int minVolumeDb = -60;
    static constexpr int maxVolumeDb = 12;

    ApplicationState();

    int bpm() const noexcept { return m_bpm; }
    int transpose() const noexcept { return m_transpose; }
    int octave() const noexcept { return m_octave; }
    bool playing() const noexcept { return m_playing; }
    bool recording() const noexcept { return m_recording; }
    int crossfade() const noexcept { return m_crossfade; }
    int styleMemory() const noexcept { return m_styleMemory; }
    bool syncroStopOnRelease() const noexcept { return m_syncroStopOnRelease; }
    bool autoFillEnabled() const noexcept { return m_autoFillEnabled; }
    const std::string& key() const noexcept { return m_key; }
    const std::string& chord() const noexcept { return m_chord; }
    const std::string& bankMemory() const noexcept { return m_bankMemory; }

    int setBpm(int value) noexcept;
    int setTranspose(int value) noexcept;
    int setOctave(int value) noexcept;
    int setCrossfade(int value) noexcept;
    int setStyleMemory(int slot) noexcept;
    void setSyncroStopOnRelease(bool value) noexcept { m_syncroStopOnRelease = value; }
    void setAutoFillEnabled(bool value) noexcept { m_autoFillEnabled = value; }
    void setPlaying(bool value) noexcept { m_playing = value; }
    void setRecording(bool value) noexcept { m_recording = value; }
    void setKey(std::string value);
    void setChord(std::string value);
    void setBankMemory(std::string value);

    bool setPad(int index, bool value) noexcept;
    bool pad(int index) const noexcept;

    bool setChannelVolume(const std::string& channel, int value) noexcept;
    bool setChannelPan(const std::string& channel, int value) noexcept;
    bool setChannelSolo(const std::string& channel, bool value) noexcept;
    bool setChannelMute(const std::string& channel, bool value) noexcept;
    const ChannelState* channel(const std::string& channel) const noexcept;

    bool setMelodyChannelEnabled(const std::string& channel, bool value) noexcept;
    bool melodyChannelEnabled(const std::string& channel) const noexcept;

    bool setChordSourceEnabled(const std::string& source, bool value) noexcept;
    bool chordSourceEnabled(const std::string& source) const noexcept;

private:
    static int clamp(int value, int min, int max) noexcept;

    int m_bpm = 120;
    int m_transpose = 0;
    int m_octave = 0;
    int m_crossfade = 50;
    int m_styleMemory = 1;
    bool m_playing = false;
    bool m_recording = false;
    bool m_syncroStopOnRelease = true;
    bool m_autoFillEnabled = true;   // pressing a Main while playing fills into it
    std::string m_key = "C";
    std::string m_chord = "F";
    std::string m_bankMemory = "Piano";
    std::array<bool, 4> m_pads { false, false, false, false };
    std::unordered_map<std::string, ChannelState> m_channels;
    std::unordered_map<std::string, bool> m_melodyChannels;
    std::unordered_map<std::string, bool> m_chordSources;
};
}
