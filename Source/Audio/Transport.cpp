#include "Transport.h"
#include "../MusicalTiming.h"

namespace cadenza::audio
{
void Transport::setSampleRate(double rate) noexcept
{
    if (rate > 0.0) m_sampleRate = rate;
}

void Transport::setBpm(double bpm) noexcept
{
    if (bpm > 0.0) m_bpm = bpm;
}

void Transport::setTicksPerBeat(int ppq) noexcept
{
    if (ppq > 0) m_ticksPerBeat = ppq;
}

void Transport::setTimeSignature(int beatsPerBar, int beatUnit) noexcept
{
    if (beatsPerBar > 0) m_beatsPerBar = beatsPerBar;
    if (beatUnit > 0)    m_beatUnit = beatUnit;
}

void Transport::start() noexcept { m_playing = true; }
void Transport::startFromBeginning() noexcept
{
    reset();
    start();
}

void Transport::stop()  noexcept { m_playing = false; }
void Transport::reset() noexcept
{
    m_positionTicks = 0.0;
    m_phaseAccumFrames = 0.0;
}

double Transport::samplesPerTick() const noexcept
{
    // 60s / (bpm * ppq) seconds per tick.
    // samples per tick = sampleRate * (60 / (bpm * ppq))
    if (m_bpm <= 0.0 || m_ticksPerBeat <= 0) return 0.0;
    return m_sampleRate * 60.0 / (m_bpm * static_cast<double>(m_ticksPerBeat));
}

int Transport::advance(int numFrames) noexcept
{
    if (!m_playing || numFrames <= 0) return 0;

    const double spt = samplesPerTick();
    if (spt <= 0.0) return 0;

    const double prevTicks = m_positionTicks;
    const double deltaTicks = static_cast<double>(numFrames) / spt;
    m_positionTicks += deltaTicks;

    return static_cast<int>(m_positionTicks) - static_cast<int>(prevTicks);
}

int Transport::positionBar() const noexcept
{
    const int barTicks = cadenza::ticksPerBar(m_ticksPerBeat, m_beatsPerBar, m_beatUnit);
    if (barTicks <= 0) return 0;
    return static_cast<int>(m_positionTicks) / barTicks;
}

int Transport::positionBeat() const noexcept
{
    const int beatTicks = cadenza::ticksPerNotatedBeat(m_ticksPerBeat, m_beatUnit);
    const int barTicks = cadenza::ticksPerBar(m_ticksPerBeat, m_beatsPerBar, m_beatUnit);
    if (beatTicks <= 0 || barTicks <= 0) return 0;
    return (static_cast<int>(m_positionTicks) % barTicks) / beatTicks;
}
}
