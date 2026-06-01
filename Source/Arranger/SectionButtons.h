// SectionButtons — pure helper that turns a loaded Style's sections into an
// ordered list of arranger section buttons (Intro / Main A..D / Fill* / Ending),
// for the native control panel. No JUCE; lives in cadenza_core so it is tested.

#pragma once

#include "Style.h"

#include <string>
#include <vector>

namespace cadenza::arranger
{
struct SectionButton
{
    std::string sectionId;   // matches Style section id (e.g. "mainA")
    std::string label;       // human label for the button (e.g. "Main A")
};

// Human-friendly label for a Cadenza section id. Unknown ids are returned as-is.
std::string sectionDisplayLabel(const std::string& sectionId);

// Ordered buttons for the sections that actually exist in `style`, in
// conventional arranger order (Intro, Main A-D, Fills, Break, Ending). Any
// sections not in the known order are appended in their existing order.
std::vector<SectionButton> sectionButtonsForStyle(const Style& style);
}
