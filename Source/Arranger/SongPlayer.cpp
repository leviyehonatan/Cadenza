#include "SongPlayer.h"

namespace cadenza::arranger
{
void SongPlayer::setSong(std::shared_ptr<const Song> song)
{
    m_song = std::move(song);
    reset();
}

void SongPlayer::reset()
{
    m_haveApplied = false;
    m_appliedSection.clear();
    m_appliedChord.clear();
}

int SongPlayer::lastEventBar() const
{
    if (!m_song) return 0;
    int last = 0;
    for (const auto& e : m_song->events)
        if (e.bar > last) last = e.bar;
    return last;
}

SongStep SongPlayer::updateToBar(int bar)
{
    SongStep step;
    step.bar = bar;

    if (!hasSong())
        return step;

    const int lastBar = lastEventBar();

    // When looping, wrap bars past the chart back to the top (each event spans
    // at least one bar, so a 1-based modulo over the last event bar is a safe
    // approximation of "restart the chart").
    int effectiveBar = bar;
    if (m_loop && lastBar > 0 && bar > lastBar)
        effectiveBar = ((bar - 1) % lastBar) + 1;
    else
        step.atEnd = (bar > lastBar);

    const SongEvent* e = m_song->eventForBar(effectiveBar);
    if (e == nullptr)
        return step;   // before the first event — nothing to apply yet

    // Section change: only when the event names a (different) section.
    if (!e->section.empty() && (!m_haveApplied || e->section != m_appliedSection)) {
        step.sectionChanged = true;
        step.section = e->section;
        m_appliedSection = e->section;
    }

    // Chord change: only when the event names a (different) chord that parses.
    if (!e->chord.empty() && (!m_haveApplied || e->chord != m_appliedChord)) {
        if (auto parsed = cadenza::midi::parseChordSymbol(e->chord)) {
            step.chordChanged = true;
            step.chord = *parsed;
            m_appliedChord = e->chord;
        }
    }

    m_haveApplied = true;
    return step;
}

SongStep SongPlayer::previewToBar(int bar) const
{
    auto preview = *this;
    return preview.updateToBar(bar);
}

bool SongPlayer::shouldStopAtBar(int bar) const
{
    return previewToBar(bar).atEnd;
}
}
