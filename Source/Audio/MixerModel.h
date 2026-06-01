// MixerModel — pure per-channel mixer state (volume / mute / solo) for the
// native mixer. No JUCE; lives in cadenza_core so the solo/mute logic is tested.
//
// effectiveVolume() encodes the standard console rule:
//   * a muted channel is silent;
//   * if ANY channel is soloed, only soloed channels are heard;
//   * otherwise the channel plays at its set volume.
// The host turns effectiveVolume() into a MIDI CC7 (channel volume) per channel.

#pragma once

#include <vector>

namespace cadenza::audio
{
struct MixerChannelState
{
    int  channel = 0;       // Cadenza 1-based MIDI channel
    int  volume  = 100;     // 0..127
    bool mute    = false;
    bool solo    = false;
    int  program = 0;       // GM program (instrument) for this channel, 0..127
};

class MixerModel
{
public:
    // Rebuild the strip list for the given channels, preserving volume/mute/solo
    // for channels that were already present.
    void setChannels(const std::vector<int>& channels);

    const std::vector<MixerChannelState>& channels() const noexcept { return m_channels; }
    bool has(int channel) const noexcept;

    void setVolume(int channel, int volume) noexcept;
    void setMute(int channel, bool mute) noexcept;
    void setSolo(int channel, bool solo) noexcept;
    void setProgram(int channel, int program) noexcept;

    int  volume(int channel) const noexcept;
    bool mute(int channel) const noexcept;
    bool solo(int channel) const noexcept;
    int  program(int channel) const noexcept;

    bool anySolo() const noexcept;

    // 0..127 channel volume to send, considering mute + global solo state.
    int effectiveVolume(int channel) const noexcept;

private:
    MixerChannelState*       find(int channel) noexcept;
    const MixerChannelState* find(int channel) const noexcept;

    std::vector<MixerChannelState> m_channels;
};
}
