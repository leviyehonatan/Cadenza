#include "StyleLibraryIndex.h"
#include "../Json/Json.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace cadenza::arranger
{
namespace
{
std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool equalsIgnoreCase(const std::string& a, const std::string& b)
{
    return lowercase(a) == lowercase(b);
}

std::string extensionWithoutDot(const std::filesystem::path& path)
{
    auto ext = lowercase(path.extension().string());
    if (!ext.empty() && ext.front() == '.')
        ext.erase(ext.begin());
    return ext;
}

std::string styleNameFromPath(const std::string& path)
{
    const auto stem = std::filesystem::path(path).stem().string();
    return stem.empty() ? std::string("Imported Style") : stem;
}

std::string styleIdFromName(const std::string& name)
{
    std::string id;
    bool previousDash = false;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            id += static_cast<char>(std::tolower(c));
            previousDash = false;
        } else if (!previousDash && !id.empty()) {
            id += '-';
            previousDash = true;
        }
    }
    while (!id.empty() && id.back() == '-')
        id.pop_back();
    return id.empty() ? std::string("imported-style") : id;
}

StyleMetadata fallbackMetadata(const std::string& path)
{
    const std::filesystem::path fsPath(path);
    StyleMetadata metadata;
    metadata.path = path;
    metadata.kind = extensionWithoutDot(fsPath);
    metadata.format = metadata.kind;
    metadata.name = styleNameFromPath(path);
    metadata.id = fsPath.stem().string();
    if (metadata.id.empty())
        metadata.id = styleIdFromName(metadata.name);
    return metadata;
}

StyleMetadata readCstyleMetadata(const std::string& path)
{
    auto metadata = fallbackMetadata(path);

    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        metadata.selectable = false;
        return metadata;
    }

    std::ostringstream ss;
    ss << in.rdbuf();

    cadenza::json::ParseError perr;
    const auto root = cadenza::json::parse(ss.str(), &perr);
    if (!perr.ok() || !root.isObject()) {
        metadata.selectable = false;
        return metadata;
    }

    const auto id = root.get("id").asString();
    const auto name = root.get("name").asString();
    if (!id.empty())
        metadata.id = id;
    if (!name.empty())
        metadata.name = name;
    metadata.tempo = root.get("tempo").asInt(metadata.tempo);

    const auto& ts = root.get("timeSignature").asArray();
    if (ts.size() == 2) {
        metadata.beatsPerBar = ts[0].asInt(metadata.beatsPerBar);
        metadata.beatUnit = ts[1].asInt(metadata.beatUnit);
    }

    metadata.format = root.get("yamahaFormat").asString(metadata.format);
    return metadata;
}

StyleMetadata readMetadata(const std::string& path)
{
    const auto metadata = fallbackMetadata(path);
    if (metadata.kind == "cstyle")
        return readCstyleMetadata(path);

    // Yamaha style listing historically used the loader's path-derived
    // override id/name, so browsing does not need to parse the full SMF/CASM.
    auto yamaha = metadata;
    yamaha.id = styleIdFromName(yamaha.name);
    return yamaha;
}

std::string stemFromPath(const std::string& path)
{
    return std::filesystem::path(path).stem().string();
}
}

const std::vector<StyleMetadata>& StyleLibraryIndex::entriesForFiles(
    const std::vector<std::string>& paths)
{
    if (paths != m_orderedPaths)
        rebuildEntries(paths);

    return m_entries;
}

const StyleMetadata* StyleLibraryIndex::findByIdNameOrStem(
    const std::vector<std::string>& paths,
    const std::string& query)
{
    const auto& entries = entriesForFiles(paths);
    for (const auto& entry : entries) {
        if (!entry.selectable)
            continue;
        if (equalsIgnoreCase(entry.id, query)
            || equalsIgnoreCase(entry.name, query)
            || equalsIgnoreCase(stemFromPath(entry.path), query))
            return &entry;
    }
    return nullptr;
}

void StyleLibraryIndex::clear()
{
    m_orderedPaths.clear();
    m_entries.clear();
    m_cache.clear();
}

const StyleMetadata& StyleLibraryIndex::metadataForPath(const std::string& path)
{
    auto it = std::find_if(m_cache.begin(), m_cache.end(), [&](const auto& cached) {
        return cached.path == path;
    });

    if (it == m_cache.end()) {
        CachedMetadata cached;
        cached.path = path;
        cached.metadata = readMetadata(path);
        m_cache.push_back(std::move(cached));
        return m_cache.back().metadata;
    }

    return it->metadata;
}

void StyleLibraryIndex::rebuildEntries(const std::vector<std::string>& paths)
{
    m_orderedPaths = paths;
    m_entries.clear();
    m_entries.reserve(paths.size());
    for (const auto& path : paths)
        m_entries.push_back(metadataForPath(path));
}
}
