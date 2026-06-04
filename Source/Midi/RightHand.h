// RightHand — up to 3 layered live right-hand voices (Right 1 / Right 2 / Right 3).
//
// A real key in the melody zone is played by every ENABLED layer at once, so the
// player can stack sounds (e.g. piano + synth + strings) like Giglad's Right 1/2/3.
// Each layer is an independent LiveMelodyVoice on its own MIDI channel with its own
// octave; transpose is global (shared). Program (voice) and volume are applied at
// the engine level by the app, per the layer's channel.
//
// Pure (cadenza_core, no JUCE): note routing only. Returns one event per enabled
// layer. Note-off is offered to ALL layers so a note started before a layer was
// disabled still releases (no stuck notes).

#pragma once

#include "LiveMelodyVoice.h"

#include <array>
#include <vector>

namespace cadenza::midi
{
class RightHand
{
public:
    static constexpr int kNumLayers = 3;

    RightHand() noexcept;

    // ---- per-layer config (message thread) ----
    void setLayerEnabled(int layer, bool enabled) noexcept;
    bool layerEnabled(int layer) const noexcept;

    void setLayerChannel(int layer, int channel) noexcept;
    int  layerChannel(int layer) const noexcept;

    void setLayerOctave(int layer, int octaves) noexcept;
    int  layerOctave(int layer) const noexcept;

    // Global transpose applies to every layer (matches the accompaniment).
    void setTranspose(int semitones) noexcept;
    int  transpose() const noexcept { return m_transpose; }

    // ---- note handling ----
    // Returns the synth events for this note: one per enabled layer on note-on,
    // and the matching releases on note-off (for any layer that sounded it).
    std::vector<LiveMelodyEvent> handleNote(int note, int velocity, bool isOn, bool isMelodyZone);

    // Forget all held notes on every layer (device close / panic).
    void reset() noexcept;

private:
    static bool valid(int layer) noexcept { return layer >= 0 && layer < kNumLayers; }

    std::array<LiveMelodyVoice, kNumLayers> m_layers;
    std::array<bool, kNumLayers> m_enabled { true, false, false };  // Right 1 on by default
    int m_transpose = 0;
};
}
