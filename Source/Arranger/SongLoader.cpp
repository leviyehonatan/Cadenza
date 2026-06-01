#include "SongLoader.h"
#include "../Json/Json.h"

#include <fstream>
#include <sstream>
#include <utility>

namespace cadenza::arranger
{
SongLoadResult loadSongFromJson(const std::string& json)
{
    SongLoadResult r;
    cadenza::json::ParseError perr;
    auto root = cadenza::json::parse(json, &perr);
    if (!perr.ok()) { r.ok = false; r.error = "JSON parse error: " + perr.message; return r; }
    if (!root.isObject()) { r.ok = false; r.error = "expected top-level object"; return r; }

    Song s;
    s.schema       = root.get("$schema").asString(s.schema);
    s.id           = root.get("id").asString();
    s.name         = root.get("name").asString();
    s.styleId      = root.get("style").asString();
    s.defaultTempo = root.get("tempo").asInt(s.defaultTempo);
    s.key          = root.get("key").asString(s.key);

    const auto& evArr = root.get("events").asArray();
    for (const auto& ev : evArr) {
        SongEvent e;
        e.bar     = ev.get("bar").asInt(1);
        e.section = ev.get("section").asString();
        e.chord   = ev.get("chord").asString();
        s.events.push_back(std::move(e));
    }

    r.song = std::move(s);
    return r;
}

std::string saveSongToJson(const Song& s, bool pretty)
{
    namespace J = cadenza::json;
    J::Object root;
    root["$schema"] = J::Value::string(s.schema);
    root["id"]      = J::Value::string(s.id);
    root["name"]    = J::Value::string(s.name);
    root["style"]   = J::Value::string(s.styleId);
    root["tempo"]   = J::Value::number(s.defaultTempo);
    root["key"]     = J::Value::string(s.key);

    J::Array events;
    for (const auto& e : s.events) {
        J::Object o;
        o["bar"]     = J::Value::number(e.bar);
        if (!e.section.empty()) o["section"] = J::Value::string(e.section);
        if (!e.chord.empty())   o["chord"]   = J::Value::string(e.chord);
        events.push_back(J::Value::object(std::move(o)));
    }
    root["events"] = J::Value::array(std::move(events));

    return J::serialize(J::Value::object(std::move(root)), pretty);
}

SongLoadResult loadSongFromFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        SongLoadResult r;
        r.ok = false;
        r.error = "cannot open file: " + path;
        return r;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return loadSongFromJson(ss.str());
}

bool saveSongToFile(const Song& s, const std::string& path, bool pretty)
{
    std::ofstream out(path, std::ios::binary);
    if (!out.good()) return false;
    out << saveSongToJson(s, pretty);
    return out.good();
}
}
