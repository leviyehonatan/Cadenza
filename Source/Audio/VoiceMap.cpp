#include "VoiceMap.h"
#include "../Json/Json.h"

#include <cstdlib>

namespace cadenza::audio
{
namespace
{
VoiceMapEntry parseEntry(const cadenza::json::Value& v)
{
    VoiceMapEntry e;
    e.pluginPath  = v.get("plugin").asString();
    e.presetState = v.get("state").asString();
    e.gain        = v.get("gain").asInt(-1);
    return e;
}

void loadIndexed(const cadenza::json::Value& obj, int maxKey, std::map<int, VoiceMapEntry>& out)
{
    if (!obj.isObject())
        return;
    for (const auto& [key, value] : obj.asObject()) {
        if (!value.isObject())
            continue;
        const int idx = std::atoi(key.c_str());
        if (idx < 0 || idx > maxKey)
            continue;
        auto entry = parseEntry(value);
        if (!entry.pluginPath.empty())
            out[idx] = std::move(entry);
    }
}
}

bool VoiceMap::loadFromJson(const std::string& json)
{
    m_byProgram.clear();
    m_byFamily.clear();
    m_drums.reset();

    cadenza::json::ParseError err;
    auto root = cadenza::json::parse(json, &err);
    if (!err.ok() || !root.isObject())
        return false;

    if (root.get("drums").isObject()) {
        auto entry = parseEntry(root.get("drums"));
        if (!entry.pluginPath.empty())
            m_drums = std::move(entry);
    }

    loadIndexed(root.get("programs"), 127, m_byProgram);
    loadIndexed(root.get("families"), 15,  m_byFamily);
    return true;
}

bool VoiceMap::empty() const noexcept
{
    return m_byProgram.empty() && m_byFamily.empty() && !m_drums.has_value();
}

const VoiceMapEntry* VoiceMap::forProgram(int gmProgram) const noexcept
{
    if (gmProgram < 0 || gmProgram > 127)
        return nullptr;
    if (auto it = m_byProgram.find(gmProgram); it != m_byProgram.end())
        return &it->second;
    if (auto it = m_byFamily.find(gmProgram / 8); it != m_byFamily.end())
        return &it->second;
    return nullptr;
}

const VoiceMapEntry* VoiceMap::forDrums() const noexcept
{
    return m_drums.has_value() ? &m_drums.value() : nullptr;
}
}
