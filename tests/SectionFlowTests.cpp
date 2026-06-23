#include "Arranger/SectionFlow.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;

void expect(bool cond, const std::string& msg)
{
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

using namespace cadenza::arranger;

void directTransitionFillWins()
{
    const auto fill = chooseAutoFill("mainA", "mainB", { "fillAB", "fillBB" });
    expect(fill.has_value(), "A to B finds a fill");
    expect(fill.value_or("") == "fillAB", "A to B prefers fillAB");
}

void targetSelfFillIsFallback()
{
    const auto fill = chooseAutoFill("mainA", "mainB", { "fillBB" });
    expect(fill.has_value(), "A to B falls back to a fill");
    expect(fill.value_or("") == "fillBB", "A to B falls back to fillBB");
}

void sameMainUsesSameLetterFill()
{
    const auto fill = chooseAutoFill("mainA", "mainA", { "fillAA" });
    expect(fill.has_value(), "A to A finds fillAA");
    expect(fill.value_or("") == "fillAA", "A to A chooses fillAA");
}

void missingFillsReturnNone()
{
    const auto fill = chooseAutoFill("mainA", "mainB", { "mainA", "mainB" });
    expect(!fill.has_value(), "A to B without fillAB or fillBB has no fill");
}

void mainSectionsMapToOtsSlots()
{
    expect(mainSectionToOtsSlot("mainA").value_or(-1) == 0, "mainA maps to OTS 1");
    expect(mainSectionToOtsSlot("mainB").value_or(-1) == 1, "mainB maps to OTS 2");
    expect(mainSectionToOtsSlot("mainC").value_or(-1) == 2, "mainC maps to OTS 3");
    expect(mainSectionToOtsSlot("mainD").value_or(-1) == 3, "mainD maps to OTS 4");
    expect(!mainSectionToOtsSlot("fillAB").has_value(), "fillAB does not map to OTS");
    expect(!mainSectionToOtsSlot("intro").has_value(), "intro does not map to OTS");
}

void preferredOrderIncludesAllPresentFillPairs()
{
    const std::vector<std::string> available {
        "ending", "fillDD", "fillBA", "fillAC", "fillBreak", "mainB",
        "intro", "fillAB", "fillCA", "fillBD", "fillAA", "fillDB",
        "mainA", "fillDC", "customX"
    };
    const auto ordered = preferredSectionOrder(available);

    const std::vector<std::string> expected {
        "intro",
        "mainA", "mainB",
        "fillAA", "fillAB", "fillAC",
        "fillBA", "fillBD",
        "fillCA",
        "fillDB", "fillDC", "fillDD",
        "fillBreak",
        "ending",
        "customX"
    };
    expect(ordered == expected, "present fill pairs are ordered before break/endings and unknowns append");
}
}

int main()
{
    directTransitionFillWins();
    targetSelfFillIsFallback();
    sameMainUsesSameLetterFill();
    missingFillsReturnNone();
    mainSectionsMapToOtsSlots();
    preferredOrderIncludesAllPresentFillPairs();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All SectionFlow tests passed\n";
    return EXIT_SUCCESS;
}
