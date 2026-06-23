#pragma once

#include <string>
#include <vector>

namespace cadenza::arranger
{
struct StyleMetadata
{
    std::string id;
    std::string name;
    int tempo = 120;
    int beatsPerBar = 4;
    int beatUnit = 4;
    std::string path;
    std::string kind;
    std::string format;
    bool selectable = true;
};

class StyleLibraryIndex
{
public:
    const std::vector<StyleMetadata>& entriesForFiles(const std::vector<std::string>& paths);
    const StyleMetadata* findByIdNameOrStem(const std::vector<std::string>& paths,
                                            const std::string& query);

    void clear();

private:
    struct CachedMetadata
    {
        std::string path;
        StyleMetadata metadata;
    };

    const StyleMetadata& metadataForPath(const std::string& path);
    void rebuildEntries(const std::vector<std::string>& paths);

    std::vector<std::string> m_orderedPaths;
    std::vector<StyleMetadata> m_entries;
    std::vector<CachedMetadata> m_cache;
};
}
