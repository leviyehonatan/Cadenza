#include "MidiControlMap.h"

namespace cadenza::midi
{
void MidiControlMap::assign(int trigger, const std::string& command)
{
    clearCommand(command);     // one button per action
    m_map[trigger] = command;
}

void MidiControlMap::clearTrigger(int trigger)
{
    m_map.erase(trigger);
}

void MidiControlMap::clearCommand(const std::string& command)
{
    for (auto it = m_map.begin(); it != m_map.end();) {
        if (it->second == command) it = m_map.erase(it);
        else ++it;
    }
}

void MidiControlMap::clear()
{
    m_map.clear();
}

std::optional<std::string> MidiControlMap::commandFor(int trigger) const
{
    const auto it = m_map.find(trigger);
    if (it == m_map.end()) return std::nullopt;
    return it->second;
}

std::optional<int> MidiControlMap::triggerFor(const std::string& command) const
{
    for (const auto& [trigger, cmd] : m_map)
        if (cmd == command) return trigger;
    return std::nullopt;
}

std::string describeTrigger(int trigger)
{
    const int ch = triggerChannel(trigger);
    std::string chPart = ch > 0 ? ("Ch" + std::to_string(ch) + " ") : std::string();
    return chPart + (triggerIsCC(trigger) ? "CC " : "Note ") + std::to_string(triggerData(trigger));
}
}
