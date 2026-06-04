#include "RightHand.h"

namespace cadenza::midi
{
RightHand::RightHand() noexcept
{
    // Give each layer a distinct default channel so they don't collide before the
    // app assigns free channels around the loaded style.
    for (int i = 0; i < kNumLayers; ++i)
        m_layers[static_cast<std::size_t>(i)].setChannel(cadenza::audio::kLiveMelodyChannel + i);
}

void RightHand::setLayerEnabled(int layer, bool enabled) noexcept
{
    if (valid(layer))
        m_enabled[static_cast<std::size_t>(layer)] = enabled;
}

bool RightHand::layerEnabled(int layer) const noexcept
{
    return valid(layer) && m_enabled[static_cast<std::size_t>(layer)];
}

void RightHand::setLayerChannel(int layer, int channel) noexcept
{
    if (valid(layer))
        m_layers[static_cast<std::size_t>(layer)].setChannel(channel);
}

int RightHand::layerChannel(int layer) const noexcept
{
    return valid(layer) ? m_layers[static_cast<std::size_t>(layer)].channel() : 0;
}

void RightHand::setLayerOctave(int layer, int octaves) noexcept
{
    if (valid(layer))
        m_layers[static_cast<std::size_t>(layer)].setOctave(octaves);
}

int RightHand::layerOctave(int layer) const noexcept
{
    return valid(layer) ? m_layers[static_cast<std::size_t>(layer)].octave() : 0;
}

void RightHand::setTranspose(int semitones) noexcept
{
    m_transpose = semitones;
    for (auto& v : m_layers)
        v.setTranspose(semitones);
}

std::vector<LiveMelodyEvent> RightHand::handleNote(int note, int velocity, bool isOn, bool isMelodyZone)
{
    std::vector<LiveMelodyEvent> events;
    for (int i = 0; i < kNumLayers; ++i) {
        // Disabled layers don't START notes, but note-offs are still offered to
        // every layer so a note held when a layer was switched off still releases.
        if (isOn && !m_enabled[static_cast<std::size_t>(i)])
            continue;
        if (auto ev = m_layers[static_cast<std::size_t>(i)].handleNote(note, velocity, isOn, isMelodyZone))
            events.push_back(*ev);
    }
    return events;
}

void RightHand::reset() noexcept
{
    for (auto& v : m_layers)
        v.reset();
}
}
