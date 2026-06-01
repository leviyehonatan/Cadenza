#include "Arranger/SectionButtons.h"
#include "Arranger/Style.h"

#include <cstdlib>
#include <iostream>
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

Style styleWith(const std::vector<std::string>& sectionIds)
{
    Style s;
    for (const auto& id : sectionIds) {
        Section sec;
        sec.name = id;
        s.sections.push_back(sec);
    }
    return s;
}

void labelsAreHumanFriendly()
{
    expect(sectionDisplayLabel("intro") == "Intro", "intro label");
    expect(sectionDisplayLabel("mainA") == "Main A", "mainA label");
    expect(sectionDisplayLabel("mainD") == "Main D", "mainD label");
    expect(sectionDisplayLabel("fillAA") == "Fill AA", "fillAA label");
    expect(sectionDisplayLabel("fillBreak") == "Break", "fillBreak label");
    expect(sectionDisplayLabel("ending") == "Ending", "ending label");
    expect(sectionDisplayLabel("weird") == "weird", "unknown id passes through");
}

void buttonsAreOrderedAndFiltered()
{
    // Sections supplied out of order; only those present should appear, ordered.
    const auto style = styleWith({ "ending", "mainB", "intro", "mainA", "fillAA" });
    const auto buttons = sectionButtonsForStyle(style);

    expect(buttons.size() == 5, "all five sections produce buttons");
    expect(buttons[0].sectionId == "intro",  "intro first");
    expect(buttons[1].sectionId == "mainA",  "mainA second");
    expect(buttons[2].sectionId == "mainB",  "mainB third");
    expect(buttons[3].sectionId == "fillAA", "fill after mains");
    expect(buttons[4].sectionId == "ending", "ending last");
    expect(buttons[1].label == "Main A", "label carried through");
}

void onlyExistingSectionsAppear()
{
    const auto style = styleWith({ "mainA", "ending" });
    const auto buttons = sectionButtonsForStyle(style);
    expect(buttons.size() == 2, "only present sections appear");
    expect(buttons[0].sectionId == "mainA" && buttons[1].sectionId == "ending", "kept order, dropped absent");
}

void unknownSectionsAreAppended()
{
    const auto style = styleWith({ "mainA", "customX", "intro" });
    const auto buttons = sectionButtonsForStyle(style);
    expect(buttons.size() == 3, "three buttons incl. custom");
    expect(buttons[0].sectionId == "intro", "known intro ordered first");
    expect(buttons[1].sectionId == "mainA", "known mainA next");
    expect(buttons[2].sectionId == "customX", "unknown appended last");
}
}

int main()
{
    labelsAreHumanFriendly();
    buttonsAreOrderedAndFiltered();
    onlyExistingSectionsAppear();
    unknownSectionsAreAppended();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All SectionButtons tests passed\n";
    return EXIT_SUCCESS;
}
