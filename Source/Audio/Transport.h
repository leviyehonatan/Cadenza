// Transport — pure C++ PPQ (pulses per quarter) clock.
// Converts (sample-rate, BPM, sample count) into tick advances.
// Designed to be called from an audio callback; lock-free, no allocation.

#pragma once

#include <cstdint>

namespace cadenza::audio
{
class Transport
{
public:
    Transport() = default;

    void setSampleRate(double rate) noexcept;
    void setBpm(double bpm) noexcept;
    void setTicksPerBeat(int ppq) noexcept;
    void setTimeSignature(int beatsPerBar, int beatUnit) noexcept;

    double sampleRate()    const noexcept { return m_sampleRate; }
    double bpm()           const noexcept { return m_bpm; }
    int    ticksPerBeat()  const noexcept { return m_ticksPerBeat; }
    int    beatsPerBar()   const noexcept { return m_beatsPerBar; }
    int    beatUnit()      const noexcept { return m_beatUnit; }

    bool   playing()       const noexcept { return m_playing; }
    void   start() noexcept;
    void   startFromBeginning() noexcept;
    void   stop() noexcept;
    void   reset() noexcept;

    // Current tick position (continuous fractional ticks for sub-tick precision).
    double positionInTicks() const noexcept { return m_positionTicks; }
    int    positionTickInt() const noexcept { return static_cast<int>(m_positionTicks); }
    int    positionBar()     const noexcept;     // 0-based
    int    positionBeat()    const noexcept;     // 0-based within bar

    // Advance the clock by N audio frames. Returns the number of integer ticks
    // that elapsed during this call (useful for scheduling events).
    int advance(int numFrames) noexcept;

    // How many samples will pass between two consecutive ticks at the current
    // tempo and sample rate? (1 tick = 60 / (bpm * ticksPerBeat) seconds.)
    double samplesPerTick() const noexcept;

private:
    double m_sampleRate = 48000.0;
    double m_bpm = 120.0;
    int    m_ticksPerBeat = 960;
    int    m_beatsPerBar = 4;
    int    m_beatUnit = 4;
    bool   m_playing = false;
    double m_positionTicks = 0.0;
    double m_phaseAccumFrames = 0.0;
};
}
