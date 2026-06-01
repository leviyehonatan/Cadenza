#include "ApplicationState.h"

#include <utility>

namespace cadenza
{
ApplicationState::ApplicationState()
    : m_channels {
          { "left", { 0, -8, false, false, "Acoustic Bass" } },
          { "right1", { 0, 0, false, false, "Grand Piano" } },
          { "right2", { 0, 14, false, false, "Warm Strings" } },
          { "right3", { 0, -20, false, false, "Synth Pad" } },
          { "melody", { 0, 0, false, false, "Tenor Sax" } },
          { "master", { 0, 0, false, false, "Stereo Out" } },
      },
      m_melodyChannels {
          { "left", true },
          { "right1", false },
          { "right2", false },
          { "right3", false },
      },
      m_chordSources {
          { "bass", true },
          { "arranger", true },
          { "memory", false },
      }
{
}

int ApplicationState::setBpm(int value) noexcept
{
    m_bpm = clamp(value, minBpm, maxBpm);
    return m_bpm;
}

int ApplicationState::setTranspose(int value) noexcept
{
    m_transpose = clamp(value, minTranspose, maxTranspose);
    return m_transpose;
}

int ApplicationState::setOctave(int value) noexcept
{
    m_octave = clamp(value, minOctave, maxOctave);
    return m_octave;
}

int ApplicationState::setCrossfade(int value) noexcept
{
    m_crossfade = clamp(value, 0, 100);
    return m_crossfade;
}

int ApplicationState::setStyleMemory(int slot) noexcept
{
    m_styleMemory = clamp(slot, 1, 4);
    return m_styleMemory;
}

void ApplicationState::setKey(std::string value)
{
    if (!value.empty())
        m_key = std::move(value);
}

void ApplicationState::setChord(std::string value)
{
    if (!value.empty())
        m_chord = std::move(value);
}

void ApplicationState::setBankMemory(std::string value)
{
    if (!value.empty())
        m_bankMemory = std::move(value);
}

bool ApplicationState::setPad(int index, bool value) noexcept
{
    if (index < 0 || index >= static_cast<int>(m_pads.size()))
        return false;

    m_pads[static_cast<std::size_t>(index)] = value;
    return true;
}

bool ApplicationState::pad(int index) const noexcept
{
    if (index < 0 || index >= static_cast<int>(m_pads.size()))
        return false;

    return m_pads[static_cast<std::size_t>(index)];
}

bool ApplicationState::setChannelVolume(const std::string& channel, int value) noexcept
{
    auto iter = m_channels.find(channel);
    if (iter == m_channels.end())
        return false;

    iter->second.volumeDb = clamp(value, minVolumeDb, maxVolumeDb);
    return true;
}

bool ApplicationState::setChannelPan(const std::string& channel, int value) noexcept
{
    auto iter = m_channels.find(channel);
    if (iter == m_channels.end())
        return false;

    iter->second.pan = clamp(value, minPan, maxPan);
    return true;
}

bool ApplicationState::setChannelSolo(const std::string& channel, bool value) noexcept
{
    auto iter = m_channels.find(channel);
    if (iter == m_channels.end())
        return false;

    iter->second.solo = value;
    return true;
}

bool ApplicationState::setChannelMute(const std::string& channel, bool value) noexcept
{
    auto iter = m_channels.find(channel);
    if (iter == m_channels.end())
        return false;

    iter->second.mute = value;
    return true;
}

const ChannelState* ApplicationState::channel(const std::string& channel) const noexcept
{
    const auto iter = m_channels.find(channel);
    if (iter == m_channels.end())
        return nullptr;

    return &iter->second;
}

bool ApplicationState::setMelodyChannelEnabled(const std::string& channel, bool value) noexcept
{
    auto iter = m_melodyChannels.find(channel);
    if (iter == m_melodyChannels.end())
        return false;

    iter->second = value;
    return true;
}

bool ApplicationState::melodyChannelEnabled(const std::string& channel) const noexcept
{
    const auto iter = m_melodyChannels.find(channel);
    return iter != m_melodyChannels.end() && iter->second;
}

bool ApplicationState::setChordSourceEnabled(const std::string& source, bool value) noexcept
{
    auto iter = m_chordSources.find(source);
    if (iter == m_chordSources.end())
        return false;

    iter->second = value;
    return true;
}

bool ApplicationState::chordSourceEnabled(const std::string& source) const noexcept
{
    const auto iter = m_chordSources.find(source);
    return iter != m_chordSources.end() && iter->second;
}

int ApplicationState::clamp(int value, int min, int max) noexcept
{
    if (value < min)
        return min;

    if (value > max)
        return max;

    return value;
}
}

