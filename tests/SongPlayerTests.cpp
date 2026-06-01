#include "Arranger/SongPlayer.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace
{
int failures = 0;
void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

using namespace cadenza::arranger;
using cadenza::midi::ChordQuality;

std::shared_ptr<Song> demoSong()
{
    // A small chart:
    //   bar 1 : intro,  C
    //   bar 3 : mainA,  C
    //   bar 5 : mainA,  G       (chord change only, same section)
    //   bar 7 : ending, C
    auto s = std::make_shared<Song>();
    s->events = {
        { 1, "intro",  "C"  },
        { 3, "mainA",  "C"  },
        { 5, "mainA",  "G"  },
        { 7, "ending", "C"  },
    };
    return s;
}

void noSongDoesNothing()
{
    SongPlayer p;
    expect(!p.hasSong(), "no song loaded");
    auto step = p.updateToBar(1);
    expect(!step.sectionChanged && !step.chordChanged, "no song -> no changes");
}

void firstBarAppliesSectionAndChord()
{
    SongPlayer p;
    p.setSong(demoSong());
    auto step = p.updateToBar(1);
    expect(step.sectionChanged && step.section == "intro", "bar1 sets intro");
    expect(step.chordChanged && step.chord.rootPitchClass == 0 && step.chord.quality == ChordQuality::Major,
           "bar1 sets C major");
}

void sameEventBarEmitsNoChange()
{
    SongPlayer p;
    p.setSong(demoSong());
    p.updateToBar(1);
    auto step = p.updateToBar(2);   // still under the bar-1 event
    expect(!step.sectionChanged, "bar2 no section change");
    expect(!step.chordChanged, "bar2 no chord change");
}

void sectionAdvances()
{
    SongPlayer p;
    p.setSong(demoSong());
    p.updateToBar(1);
    auto step = p.updateToBar(3);
    expect(step.sectionChanged && step.section == "mainA", "bar3 -> mainA");
    expect(!step.chordChanged, "bar3 chord stays C (no change)");
}

void chordChangesWithoutSectionChange()
{
    SongPlayer p;
    p.setSong(demoSong());
    p.updateToBar(1);
    p.updateToBar(3);
    auto step = p.updateToBar(5);
    expect(!step.sectionChanged, "bar5 stays in mainA");
    expect(step.chordChanged && step.chord.rootPitchClass == 7, "bar5 -> G");
}

void endFlagSetPastLastEvent()
{
    SongPlayer p;
    p.setSong(demoSong());
    expect(p.lastEventBar() == 7, "last event bar is 7");
    auto step = p.updateToBar(9);
    expect(step.atEnd, "bar past last event flags atEnd");
    expect(step.sectionChanged && step.section == "ending", "bar9 still resolves to ending event");
}

void loopingWrapsAndReapplies()
{
    SongPlayer p;
    p.setSong(demoSong());
    p.setLooping(true);
    p.updateToBar(7);                 // ending / C
    auto step = p.updateToBar(8);     // wraps to bar 1 (intro / C)
    expect(!step.atEnd, "looping never flags atEnd");
    expect(step.sectionChanged && step.section == "intro", "loop wraps back to intro");
}

void resetReapplies()
{
    SongPlayer p;
    p.setSong(demoSong());
    p.updateToBar(1);
    p.reset();
    auto step = p.updateToBar(1);
    expect(step.sectionChanged && step.chordChanged, "reset re-emits active section + chord");
}
}

int main()
{
    noSongDoesNothing();
    firstBarAppliesSectionAndChord();
    sameEventBarEmitsNoChange();
    sectionAdvances();
    chordChangesWithoutSectionChange();
    endFlagSetPastLastEvent();
    loopingWrapsAndReapplies();
    resetReapplies();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All SongPlayer tests passed\n";
    return EXIT_SUCCESS;
}
