#include "MixerModel.h"

namespace cadenza::audio
{
namespace
{
int clamp127(int v) noexcept { return v < 0 ? 0 : (v > 127 ? 127 : v); }
}

void MixerModel::setChannels(const std::vector<int>& channels)
{
    std::vector<MixerChannelState> rebuilt;
    rebuilt.reserve(channels.size());
    for (int ch : channels) {
        if (const auto* existing = find(ch))
            rebuilt.push_back(*existing);          // keep volume/mute/solo/program
        else
            rebuilt.push_back(MixerChannelState{ ch, 100, false, false, 0 });
    }
    m_channels = std::move(rebuilt);
}

MixerChannelState* MixerModel::find(int channel) noexcept
{
    for (auto& c : m_channels)
        if (c.channel == channel) return &c;
    return nullptr;
}

const MixerChannelState* MixerModel::find(int channel) const noexcept
{
    for (const auto& c : m_channels)
        if (c.channel == channel) return &c;
    return nullptr;
}

bool MixerModel::has(int channel) const noexcept { return find(channel) != nullptr; }

void MixerModel::setVolume(int channel, int volume) noexcept
{
    if (auto* c = find(channel)) c->volume = clamp127(volume);
}

void MixerModel::setMute(int channel, bool mute) noexcept
{
    if (auto* c = find(channel)) c->mute = mute;
}

void MixerModel::setSolo(int channel, bool solo) noexcept
{
    if (auto* c = find(channel)) c->solo = solo;
}

void MixerModel::setProgram(int channel, int program) noexcept
{
    if (auto* c = find(channel)) c->program = (program < 0 ? 0 : (program > 127 ? 127 : program));
}

int MixerModel::program(int channel) const noexcept
{
    const auto* c = find(channel);
    return c ? c->program : 0;
}

int MixerModel::volume(int channel) const noexcept
{
    const auto* c = find(channel);
    return c ? c->volume : 0;
}

bool MixerModel::mute(int channel) const noexcept
{
    const auto* c = find(channel);
    return c && c->mute;
}

bool MixerModel::solo(int channel) const noexcept
{
    const auto* c = find(channel);
    return c && c->solo;
}

bool MixerModel::anySolo() const noexcept
{
    for (const auto& c : m_channels)
        if (c.solo) return true;
    return false;
}

int MixerModel::effectiveVolume(int channel) const noexcept
{
    const auto* c = find(channel);
    if (c == nullptr) return 0;
    if (c->mute) return 0;
    if (anySolo() && !c->solo) return 0;
    return c->volume;
}
}
