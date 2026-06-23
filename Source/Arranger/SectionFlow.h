// SectionFlow - pure arranger section decision helpers.

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace cadenza::arranger
{
std::optional<std::string> chooseAutoFill(const std::string& prevMainId,
                                          const std::string& targetMainId,
                                          const std::vector<std::string>& availableSectionIds);

std::optional<int> mainSectionToOtsSlot(const std::string& mainId);

std::vector<std::string> preferredSectionOrder(const std::vector<std::string>& availableSectionIds);
}
