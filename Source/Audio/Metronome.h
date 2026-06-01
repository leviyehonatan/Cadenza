// Metronome — generates a click on every beat (accented on bar 1).
// Driven by Transport ticks. Audio-thread-safe.

#pragma once

#include "Transport.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

namespace cadenza::audio
{
class Metronome
{
public:
    void prepare(double sampleRate);
    void setEnabled(bool enabled) noexcept { m_enabled.store(enabled); }
    bool isEnabled() const noexcept { return m_enabled.load(); }

    // Called once per audio block. Reads the transport to decide when to fire.
    // Adds clicks to the buffer (does NOT clear it).
    void renderBlock(juce::AudioBuffer<float>& buffer, Transport& transport);

private:
    double m_sampleRate = 48000.0;
    std::atomic<bool> m_enabled { false };

    // Click voice — tiny envelope on a sine.
    int    m_voiceSamplesRemaining = 0;
    double m_phase = 0.0;
    double m_freq = 1000.0;
    float  m_voiceGain = 0.0f;

    // Track which beat number we last fired on, so we don't fire twice per beat.
    int m_lastBeatFiredAt = -1;

    void triggerClick(bool accent);
};
}
