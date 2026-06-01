#include "SectionButtons.h"

#include <algorithm>
#include <cctype>

namespace cadenza::arranger
{
namespace
{
// Conventional arranger ordering of the section ids Cadenza produces
// (see mapSectionMarker in StyParser).
const std::vector<std::string>& preferredOrder()
{
    static const std::vector<std::string> order = {
        "intro", "introB", "introC",
        "mainA", "mainB", "mainC", "mainD",
        "fillAA", "fillAB", "fillBA", "fillBB",
        "fillAC", "fillCA", "fillCC", "fillDD",
        "fillBreak",
        "ending", "endingB", "endingC",
    };
    return order;
}
}

std::string sectionDisplayLabel(const std::string& id)
{
    if (id == "intro")  return "Intro";
    if (id == "introB") return "Intro B";
    if (id == "introC") return "Intro C";
    if (id == "ending") return "Ending";
    if (id == "endingB") return "Ending B";
    if (id == "endingC") return "Ending C";
    if (id == "fillBreak") return "Break";

    if (id.rfind("main", 0) == 0 && id.size() == 5)          // mainA..mainD
        return std::string("Main ") + static_cast<char>(std::toupper(id[4]));

    if (id.rfind("fill", 0) == 0 && id.size() == 6) {        // fillAA..fillDD
        std::string s = "Fill ";
        s += static_cast<char>(std::toupper(id[4]));
        s += static_cast<char>(std::toupper(id[5]));
        return s;
    }

    return id;   // unknown id: show as-is
}

std::vector<SectionButton> sectionButtonsForStyle(const Style& style)
{
    std::vector<SectionButton> buttons;
    buttons.reserve(style.sections.size());

    // 1) Known sections in conventional order.
    for (const auto& id : preferredOrder()) {
        if (style.findSection(id) != nullptr)
            buttons.push_back({ id, sectionDisplayLabel(id) });
    }

    // 2) Any remaining style sections not covered above, in existing order.
    const auto& order = preferredOrder();
    for (const auto& section : style.sections) {
        const bool alreadyKnown =
            std::find(order.begin(), order.end(), section.name) != order.end();
        if (!alreadyKnown)
            buttons.push_back({ section.name, sectionDisplayLabel(section.name) });
    }

    return buttons;
}
}
