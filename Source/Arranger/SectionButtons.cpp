#include "SectionButtons.h"
#include "SectionFlow.h"

#include <cctype>

namespace cadenza::arranger
{
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

    std::vector<std::string> sectionIds;
    sectionIds.reserve(style.sections.size());
    for (const auto& section : style.sections)
        sectionIds.push_back(section.name);

    for (const auto& id : preferredSectionOrder(sectionIds))
        buttons.push_back({ id, sectionDisplayLabel(id) });

    return buttons;
}
}
