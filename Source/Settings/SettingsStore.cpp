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
    m_state.eqLowDb  = root.get("eqLowDb").asInt(m_state.eqLowDb);
    m_state.eqMidDb  = root.get("eqMidDb").asInt(m_state.eqMidDb);
    m_state.eqHighDb = root.get("eqHighDb").asInt(m_state.eqHighDb);
    m_state.compAmount = root.get("compAmount").asInt(m_state.compAmount);
    m_state.masterVolume = root.get("masterVolume").asInt(m_state.masterVolume);
    m_state.splitNote  = root.get("splitNote").asInt(m_state.splitNote);

    // Right 1/2/3 layered voices. If absent (older settings), migrate Right 1 from
    // melodyProgram so behaviour is unchanged.
    const auto& rightLayers = root.get("rightLayers");
    if (rightLayers.isArray()) {
        const auto& arr = rightLayers.asArray();
        for (int i = 0; i < 3 && i < static_cast<int>(arr.size()); ++i) {
            const auto& e = arr[static_cast<std::size_t>(i)];
            if (!e.isObject()) continue;
            m_state.rightLayers[i].enabled = e.get("enabled").asBool(m_state.rightLayers[i].enabled);
            m_state.rightLayers[i].program = e.get("program").asInt(m_state.rightLayers[i].program);
            m_state.rightLayers[i].volume  = e.get("volume").asInt(m_state.rightLayers[i].volume);
            m_state.rightLayers[i].octave  = e.get("octave").asInt(m_state.rightLayers[i].octave);
            m_state.rightLayers[i].pluginPath = e.get("pluginPath").asString(m_state.rightLayers[i].pluginPath);
        }
    } else {
        m_state.rightLayers[0].program = m_state.melodyProgram;
    }
    m_state.melodyProgram = m_state.rightLayers[0].program;   // keep the mirror in sync

    // Registrations (performance snapshots).
    const auto& regs = root.get("registrations");
    if (regs.isArray()) {
        const auto& arr = regs.asArray();
        for (int i = 0; i < Settings::kNumRegistrations && i < static_cast<int>(arr.size()); ++i) {
            const auto& e = arr[static_cast<std::size_t>(i)];
            if (!e.isObject()) continue;
            auto& r = m_state.registrations[i];
            r.used      = e.get("used").asBool(false);
            r.name      = e.get("name").asString(r.name);
            r.styleId   = e.get("styleId").asString(r.styleId);
            r.stylePath = e.get("stylePath").asString(r.stylePath);
            r.bpm       = e.get("bpm").asInt(r.bpm);
            r.transpose = e.get("transpose").asInt(r.transpose);
            r.octave    = e.get("octave").asInt(r.octave);
            r.splitNote = e.get("splitNote").asInt(r.splitNote);
            r.eqLowDb   = e.get("eqLowDb").asInt(r.eqLowDb);
            r.eqMidDb   = e.get("eqMidDb").asInt(r.eqMidDb);
            r.eqHighDb  = e.get("eqHighDb").asInt(r.eqHighDb);
            r.compAmount = e.get("compAmount").asInt(r.compAmount);
            r.bankMemory = e.get("bankMemory").asString(r.bankMemory);
            r.chordArrangerEnabled = e.get("chordArrangerEnabled").asBool(r.chordArrangerEnabled);
            r.chordMemoryEnabled   = e.get("chordMemoryEnabled").asBool(r.chordMemoryEnabled);
            r.chordBassEnabled     = e.get("chordBassEnabled").asBool(r.chordBassEnabled);
            r.syncroStopOnRelease  = e.get("syncroStopOnRelease").asBool(r.syncroStopOnRelease);
            const auto& layers = e.get("rightLayers");
            if (layers.isArray()) {
                const auto& la = layers.asArray();
                for (int j = 0; j < 3 && j < static_cast<int>(la.size()); ++j) {
                    const auto& le = la[static_cast<std::size_t>(j)];
                    if (!le.isObject()) continue;
                    r.rightLayers[j].enabled = le.get("enabled").asBool(r.rightLayers[j].enabled);
                    r.rightLayers[j].program = le.get("program").asInt(r.rightLayers[j].program);
                    r.rightLayers[j].volume  = le.get("volume").asInt(r.rightLayers[j].volume);
                    r.rightLayers[j].octave  = le.get("octave").asInt(r.rightLayers[j].octave);
                    r.rightLayers[j].pluginPath = le.get("pluginPath").asString(r.rightLayers[j].pluginPath);
                }
            }
        }
    }

    m_state.midiControlMap.clear();
    const auto& midiMap = root.get("midiControlMap");
    if (midiMap.isObject()) {
        for (const auto& [key, val] : midiMap.asObject()) {
            if (!val.isString()) continue;
            try { m_state.midiControlMap[std::stoi(key)] = val.asString(); }
            catch (...) { /* skip malformed key */ }
        }
    }

    m_state.styleMixes.clear();
    const auto& styleMixes = root.get("styleMixes");
    if (styleMixes.isObject()) {
        for (const auto& [styleId, arrVal] : styleMixes.asObject()) {
            if (!arrVal.isArray())
                continue;
            std::vector<StyleChannelMix> channels;
            for (const auto& e : arrVal.asArray()) {
                if (!e.isObject())
                    continue;
                StyleChannelMix m;
                m.channel = e.get("channel").asInt(0);
                m.program = e.get("program").asInt(-1);
                m.volume  = e.get("volume").asInt(-1);
                m.mute    = e.get("mute").asBool(false);
                m.solo    = e.get("solo").asBool(false);
                m.pluginPath = e.get("pluginPath").asString("");
                channels.push_back(m);
            }
            m_state.styleMixes[styleId] = std::move(channels);
        }
    }
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
    root["eqLowDb"]  = J::Value::number(m_state.eqLowDb);
    root["eqMidDb"]  = J::Value::number(m_state.eqMidDb);
    root["eqHighDb"] = J::Value::number(m_state.eqHighDb);
    root["compAmount"] = J::Value::number(m_state.compAmount);
    root["masterVolume"] = J::Value::number(m_state.masterVolume);
    root["splitNote"]  = J::Value::number(m_state.splitNote);

    J::Array rightLayers;
    for (const auto& layer : m_state.rightLayers) {
        J::Object o;
        o["enabled"] = J::Value::boolean(layer.enabled);
        o["program"] = J::Value::number(layer.program);
        o["volume"]  = J::Value::number(layer.volume);
        o["octave"]  = J::Value::number(layer.octave);
        o["pluginPath"] = J::Value::string(layer.pluginPath);
        rightLayers.push_back(J::Value::object(std::move(o)));
    }
    root["rightLayers"] = J::Value::array(std::move(rightLayers));

    auto serializeLayers = [](const RightLayer (&layers)[3]) {
        J::Array arr;
        for (const auto& layer : layers) {
            J::Object o;
            o["enabled"] = J::Value::boolean(layer.enabled);
            o["program"] = J::Value::number(layer.program);
            o["volume"]  = J::Value::number(layer.volume);
            o["octave"]  = J::Value::number(layer.octave);
            o["pluginPath"] = J::Value::string(layer.pluginPath);
            arr.push_back(J::Value::object(std::move(o)));
        }
        return arr;
    };

    J::Array regs;
    for (const auto& r : m_state.registrations) {
        J::Object o;
        o["used"]      = J::Value::boolean(r.used);
        o["name"]      = J::Value::string(r.name);
        o["styleId"]   = J::Value::string(r.styleId);
        o["stylePath"] = J::Value::string(r.stylePath);
        o["bpm"]       = J::Value::number(r.bpm);
        o["transpose"] = J::Value::number(r.transpose);
        o["octave"]    = J::Value::number(r.octave);
        o["splitNote"] = J::Value::number(r.splitNote);
        o["eqLowDb"]   = J::Value::number(r.eqLowDb);
        o["eqMidDb"]   = J::Value::number(r.eqMidDb);
        o["eqHighDb"]  = J::Value::number(r.eqHighDb);
        o["compAmount"] = J::Value::number(r.compAmount);
        o["bankMemory"] = J::Value::string(r.bankMemory);
        o["chordArrangerEnabled"] = J::Value::boolean(r.chordArrangerEnabled);
        o["chordMemoryEnabled"]   = J::Value::boolean(r.chordMemoryEnabled);
        o["chordBassEnabled"]     = J::Value::boolean(r.chordBassEnabled);
        o["syncroStopOnRelease"]  = J::Value::boolean(r.syncroStopOnRelease);
        o["rightLayers"] = J::Value::array(serializeLayers(r.rightLayers));
        regs.push_back(J::Value::object(std::move(o)));
    }
    root["registrations"] = J::Value::array(std::move(regs));

    J::Object midiMap;
    for (const auto& [trigger, command] : m_state.midiControlMap)
        midiMap[std::to_string(trigger)] = J::Value::string(command);
    root["midiControlMap"] = J::Value::object(std::move(midiMap));

    J::Object styleMixes;
    for (const auto& [styleId, channels] : m_state.styleMixes) {
        J::Array arr;
        for (const auto& m : channels) {
            J::Object o;
            o["channel"] = J::Value::number(m.channel);
            o["program"] = J::Value::number(m.program);
            o["volume"]  = J::Value::number(m.volume);
            o["mute"]    = J::Value::boolean(m.mute);
            o["solo"]    = J::Value::boolean(m.solo);
            if (!m.pluginPath.empty())
                o["pluginPath"] = J::Value::string(m.pluginPath);
            arr.push_back(J::Value::object(std::move(o)));
        }
        styleMixes[styleId] = J::Value::array(std::move(arr));
    }
    root["styleMixes"] = J::Value::object(std::move(styleMixes));

    std::ofstream out(m_path, std::ios::binary);
    if (!out.good()) return false;
    out << J::serialize(J::Value::object(std::move(root)), true);
    return out.good();
}
}
