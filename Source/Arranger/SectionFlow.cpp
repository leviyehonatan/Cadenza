#include "SectionFlow.h"

#include <algorithm>
#include <array>

namespace cadenza::arranger
{
namespace
{
bool isMainSection(const std::string& id) noexcept
{
    return id.size() == 5 && id.rfind("main", 0) == 0
        && id[4] >= 'A' && id[4] <= 'D';
}

bool containsSection(const std::vector<std::string>& ids, const std::string& id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

const std::vector<std::string>& fullPreferredOrder()
{
    static const std::vector<std::string> order = {
        "intro", "introB", "introC",
        "mainA", "mainB", "mainC", "mainD",
        "fillAA", "fillAB", "fillAC", "fillAD",
        "fillBA", "fillBB", "fillBC", "fillBD",
        "fillCA", "fillCB", "fillCC", "fillCD",
        "fillDA", "fillDB", "fillDC", "fillDD",
        "fillBreak",
        "ending", "endingB", "endingC",
    };
    return order;
}
}

std::optional<std::string> chooseAutoFill(const std::string& prevMainId,
                                          const std::string& targetMainId,
                                          const std::vector<std::string>& availableSectionIds)
{
    if (!isMainSection(prevMainId) || !isMainSection(targetMainId))
        return std::nullopt;

    const std::array candidates {
        std::string("fill") + prevMainId[4] + targetMainId[4],
        std::string("fill") + targetMainId[4] + targetMainId[4],
    };
    for (const auto& candidate : candidates)
        if (containsSection(availableSectionIds, candidate))
            return candidate;
    return std::nullopt;
}

std::optional<int> mainSectionToOtsSlot(const std::string& mainId)
{
    if (!isMainSection(mainId))
        return std::nullopt;
    return mainId[4] - 'A';
}

std::vector<std::string> preferredSectionOrder(const std::vector<std::string>& availableSectionIds)
{
    std::vector<std::string> ordered;
    ordered.reserve(availableSectionIds.size());

    const auto& preferred = fullPreferredOrder();
    for (const auto& id : preferred)
        if (containsSection(availableSectionIds, id))
            ordered.push_back(id);

    for (const auto& id : availableSectionIds) {
        const bool known = std::find(preferred.begin(), preferred.end(), id) != preferred.end();
        const bool alreadyAdded = containsSection(ordered, id);
        if (!known && !alreadyAdded)
            ordered.push_back(id);
    }

    return ordered;
}
}
