#include "Metronome.h"
#include "../MusicalTiming.h"

#include <cmath>

namespace cadenza::audio
{
void Metronome::prepare(double sampleRate)
{
    m_sampleRate = sampleRate;
    m_voiceSamplesRemaining = 0;
    m_phase = 0.0;
    m_lastBeatFiredAt = -1;
}

void Metronome::triggerClick(bool accent)
{
    m_phase = 0.0;
    m_freq = accent ? 2000.0 : 1320.0;
    m_voiceGain = accent ? 0.30f : 0.20f;
    // A short ~14 ms click reads as a "tick" rather than a pitched beep.
    m_voiceSamplesRemaining = static_cast<int>(0.014 * m_sampleRate);
}

void Metronome::renderBlock(juce::AudioBuffer<float>& buffer, Transport& transport)
{
    if (!m_enabled.load() || !transport.playing()) {
        m_voiceSamplesRemaining = 0;
        m_lastBeatFiredAt = -1;
        return;
    }

    // Detect beat boundaries within this block.
    // Strategy: ask transport for total beats; if it advanced to a new beat
    // anywhere in this block, fire a click at the start of the block.
    // (Per-sample precision can be added later.)
    const int beatTicks = cadenza::ticksPerNotatedBeat(
        transport.ticksPerBeat(), transport.beatUnit());
    if (beatTicks > 0) {
        const int totalBeats = transport.positionTickInt() / beatTicks;
        if (totalBeats != m_lastBeatFiredAt) {
            m_lastBeatFiredAt = totalBeats;
            const bool accent = (transport.beatsPerBar() > 0)
                                && (totalBeats % transport.beatsPerBar()) == 0;
            triggerClick(accent);
        }
    }

    if (m_voiceSamplesRemaining <= 0) return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const double twoPi = 2.0 * 3.14159265358979323846;
    const double phaseInc = twoPi * m_freq / m_sampleRate;

    int toRender = std::min(numSamples, m_voiceSamplesRemaining);
    for (int i = 0; i < toRender; ++i) {
        // Steep (squared) decay envelope so the click is percussive, not a beep.
        const float lin = static_cast<float>(m_voiceSamplesRemaining - i)
                          / static_cast<float>(m_voiceSamplesRemaining + 1);
        const float env = lin * lin;
        const float sample = m_voiceGain * env * static_cast<float>(std::sin(m_phase));
        for (int ch = 0; ch < numChannels; ++ch) {
            buffer.addSample(ch, i, sample);
        }
        m_phase += phaseInc;
        if (m_phase > twoPi) m_phase -= twoPi;
    }
    m_voiceSamplesRemaining -= toRender;
}
}
