#include "Style.h"

namespace cadenza::arranger
{
const Section* Style::findSection(const std::string& name) const noexcept
{
    for (const auto& s : sections) {
        if (s.name == name) return &s;
    }
    return nullptr;
}
}
