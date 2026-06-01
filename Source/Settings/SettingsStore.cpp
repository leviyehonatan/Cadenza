#include "SettingsStore.h"
#include "../Json/Json.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace cadenza::settings
{
SettingsStore::SettingsStore(std::string path)
    : m_path(std::move(path))
{
}

bool SettingsStore::load()
{
    std::ifstream in(m_path, std::ios::binary);
    if (!in.good()) return false;

    std::ostringstream ss;
    ss << in.rdbuf();
    const auto text = ss.str();

    cadenza::json::ParseError err;
    auto root = cadenza::json::parse(text, &err);
    if (!err.ok() || !root.isObject()) return false;

    m_state.bpm        = root.get("bpm").asInt(m_state.bpm);
    m_state.transpose  = root.get("transpose").asInt(m_state.transpose);
    m_state.octave     = root.get("octave").asInt(m_state.octave);
    m_state.melodyProgram = root.get("melodyProgram").asInt(m_state.melodyProgram);
    m_state.key        = root.get("key").asString(m_state.key);
    m_state.bankMemory = root.get("bankMemory").asString(m_state.bankMemory);
    m_state.styleMemory = root.get("styleMemory").asInt(m_state.styleMemory);
    m_state.lastStyleId = root.get("lastStyleId").asString(m_state.lastStyleId);
    m_state.lastStylePath = root.get("lastStylePath").asString(m_state.lastStylePath);
    m_state.lastSongId  = root.get("lastSongId").asString(m_state.lastSongId);
    m_state.lastSoundFontPath = root.get("lastSoundFontPath").asString(m_state.lastSoundFontPath);
    m_state.midiInputDevice = root.get("midiInputDevice").asString(m_state.midiInputDevice);
    m_state.crossfade = root.get("crossfade").asInt(m_state.crossfade);
    m_state.chordBassEnabled = root.get("chordBassEnabled").asBool(m_state.chordBassEnabled);
    m_state.chordArrangerEnabled = root.get("chordArrangerEnabled").asBool(m_state.chordArrangerEnabled);
    m_state.chordMemoryEnabled = root.get("chordMemoryEnabled").asBool(m_state.chordMemoryEnabled);
    m_state.syncroStopOnRelease = root.get("syncroStopOnRelease").asBool(m_state.syncroStopOnRelease);
    return true;
}

bool SettingsStore::save() const
{
    namespace J = cadenza::json;
    J::Object root;
    root["bpm"]               = J::Value::number(m_state.bpm);
    root["transpose"]         = J::Value::number(m_state.transpose);
    root["octave"]            = J::Value::number(m_state.octave);
    root["melodyProgram"]     = J::Value::number(m_state.melodyProgram);
    root["key"]               = J::Value::string(m_state.key);
    root["bankMemory"]        = J::Value::string(m_state.bankMemory);
    root["styleMemory"]       = J::Value::number(m_state.styleMemory);
    root["lastStyleId"]       = J::Value::string(m_state.lastStyleId);
    root["lastStylePath"]     = J::Value::string(m_state.lastStylePath);
    root["lastSongId"]        = J::Value::string(m_state.lastSongId);
    root["lastSoundFontPath"] = J::Value::string(m_state.lastSoundFontPath);
    root["midiInputDevice"]   = J::Value::string(m_state.midiInputDevice);
    root["crossfade"]         = J::Value::number(m_state.crossfade);
    root["chordBassEnabled"] = J::Value::boolean(m_state.chordBassEnabled);
    root["chordArrangerEnabled"] = J::Value::boolean(m_state.chordArrangerEnabled);
    root["chordMemoryEnabled"] = J::Value::boolean(m_state.chordMemoryEnabled);
    root["syncroStopOnRelease"] = J::Value::boolean(m_state.syncroStopOnRelease);

    std::ofstream out(m_path, std::ios::binary);
    if (!out.good()) return false;
    out << J::serialize(J::Value::object(std::move(root)), true);
    return out.good();
}
}
