// OtsRecall — pure mapping from a style's One Touch Setting onto the three
// live right-hand layers (Right 1/2/3). Kept JUCE-free so it is unit-testable;
// MainComponent applies the returned targets through the normal layer path.

#pragma once

#include "Style.h"

#include <array>

namespace cadenza::arranger
{
// What recalling one OTS slot should do to one right-hand layer. program and
// volume are applied only when their set* flag is true; otherwise the layer
// keeps its current value (an OTS may specify a voice only partially).
struct OtsLayerTarget
{
    bool enabled = false;
    bool setProgram = false;
    int  program = 0;
    bool setVolume = false;
    int  volume = 0;
};

inline std::array<OtsLayerTarget, 3> otsRecallTargets(const OtsSetting& setting)
{
    std::array<OtsLayerTarget, 3> out {};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto& voice = setting.layers[i];
        out[i].enabled = voice.present;
        if (voice.present && voice.program >= 0) {
            out[i].setProgram = true;
            out[i].program = voice.program;
        }
        if (voice.present && voice.volume >= 0) {
            out[i].setVolume = true;
            out[i].volume = voice.volume;
        }
    }
    return out;
}
}
