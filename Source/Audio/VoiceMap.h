// VoiceMap — maps a style/part voice to a VST3 instrument + preset, mirroring
// Giglad's "a voice is a sampler preset" model (see docs/GIGLAD_ARCHITECTURE.md).
//
// A style sets each part to a GM program (and the drum channel to a kit). With a
// VoiceMap configured, those GM programs can resolve to high-quality VST3 voices
// (a sampler + a saved preset state) instead of the GM SoundFont, which is how
// Cadenza reaches Giglad-class sound using the per-part VST3 hosting it already
// has.
//
// Pure (cadenza_core, no JUCE): parses JSON and answers lookups. The app layer
// does the actual plugin loading from the returned entries.

#pragma once

#include <map>
#include <optional>
#include <string>

namespace cadenza::audio
{
struct VoiceMapEntry
{
    std::string pluginPath;    // VST3 file/bundle path to load on the part's channel
    std::string presetState;   // optional VST3 state blob to restore ("" = plugin default)
    int         gain = -1;     // optional CC7 volume 0..127 (-1 = leave the style's volume)
};

class VoiceMap
{
public:
    // Replace the map from a JSON document. Returns false on malformed JSON (the
    // map is cleared either way). Entries without a "plugin" path are ignored.
    //
    // {
    //   "drums":    { "plugin": "...", "state": "...", "gain": 100 },
    //   "programs": { "0": { "plugin": "..." }, "24": { "plugin": "..." } },
    //   "families": { "3": { "plugin": "..." } }   // 0..15, each = 8 GM programs
    // }
    bool loadFromJson(const std::string& json);

    bool empty() const noexcept;

    // Instrument for a melodic GM program 0..127: exact program first, then its
    // family (program / 8) as a fallback, else nullptr.
    const VoiceMapEntry* forProgram(int gmProgram) const noexcept;

    // Instrument for the drum/percussion channel, or nullptr.
    const VoiceMapEntry* forDrums() const noexcept;

    // Build/update the map in memory (used by the in-app "set default voice"
    // capture), then serialize to the same JSON shape loadFromJson() accepts.
    void setProgram(int gmProgram, const VoiceMapEntry& entry);   // 0..127
    void setFamily(int family, const VoiceMapEntry& entry);       // 0..15
    void setDrums(const VoiceMapEntry& entry);
    std::string toJson() const;

private:
    std::map<int, VoiceMapEntry> m_byProgram;   // key 0..127
    std::map<int, VoiceMapEntry> m_byFamily;    // key 0..15
    std::optional<VoiceMapEntry> m_drums;
};
}
