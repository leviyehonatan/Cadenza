// SongPlayer — auto-steps through a Song's (bar, section, chord) events.
//
// Giglad-style "song mode": you program a chord chart with a section and chord
// per bar, press play, and the arranger walks the chart automatically — changing
// the active style section and the live chord as the transport crosses each bar.
//
// This class is pure logic (no JUCE, no audio thread). A bar-driven driver calls
// updateToBar() once per bar and applies the returned changes to a StyleEngine
// (setSection / setChord). It remembers what was last applied so it only reports
// real changes.

#pragma once

#include "Song.h"
#include "../Midi/ChordRecognizer.h"

#include <memory>
#include <string>

namespace cadenza::arranger
{
// What, if anything, should be applied when the transport reaches a given bar.
struct SongStep
{
    int bar = 1;                    // the 1-based bar this step describes
    bool sectionChanged = false;    // true when `section` should be applied
    std::string section;            // style section id ("mainA", "intro", ...)
    bool chordChanged = false;      // true when `chord` should be applied
    cadenza::midi::Chord chord;     // parsed chord to set live
    bool atEnd = false;             // true once `bar` passed the last event (non-looping)
};

class SongPlayer
{
public:
    void setSong(std::shared_ptr<const Song> song);
    std::shared_ptr<const Song> song() const { return m_song; }
    bool hasSong() const { return m_song != nullptr && !m_song->events.empty(); }

    void setLooping(bool loop) { m_loop = loop; }
    bool isLooping() const { return m_loop; }

    // Forget what was last applied so the next updateToBar re-emits the active
    // section + chord (call this on Start / when (re)loading a song).
    void reset();

    // Highest bar number referenced by any event (song length hint); 0 if none.
    int lastEventBar() const;

    // Look up the event active at `bar` (1-based) and report what changed since
    // the previous applied bar. When looping, bars past the last event wrap.
    SongStep updateToBar(int bar);

    // Report what updateToBar() would emit without advancing the applied state.
    SongStep previewToBar(int bar) const;

private:
    std::shared_ptr<const Song> m_song;
    bool m_loop = false;

    bool m_haveApplied = false;
    std::string m_appliedSection;
    std::string m_appliedChord;     // raw chord string last applied
};
}
