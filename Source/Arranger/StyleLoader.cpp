#include "StyleLoader.h"
#include "StyParser.h"
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
}

const char* roleToString(NoteRole role) noexcept
{
    switch (role) {
        case NoteRole::Absolute:   return "absolute";
        case NoteRole::ChordRoot:  return "chord-root";
        case NoteRole::Chord3:     return "chord-3";
        case NoteRole::Chord5:     return "chord-5";
        case NoteRole::Chord7:     return "chord-7";
        case NoteRole::ChordColor: return "chord-color";
        case NoteRole::ScaleTone:  return "scale-tone";
    }
    return "absolute";
}

NoteRole roleFromString(const std::string& s) noexcept
{
    if (s == "chord-root") return NoteRole::ChordRoot;
    if (s == "chord-3")    return NoteRole::Chord3;
    if (s == "chord-5")    return NoteRole::Chord5;
    if (s == "chord-7")    return NoteRole::Chord7;
    if (s == "chord-color") return NoteRole::ChordColor;
    if (s == "scale-tone") return NoteRole::ScaleTone;
    return NoteRole::Absolute;
}

LoadResult loadStyleFromJson(const std::string& json)
{
    LoadResult result;

    cadenza::json::ParseError perr;
    auto root = cadenza::json::parse(json, &perr);
    if (!perr.ok()) {
        result.ok = false;
        result.error = "JSON parse error: " + perr.message;
        return result;
    }
    if (!root.isObject()) {
        result.ok = false;
        result.error = "expected top-level object";
        return result;
    }

    Style style;
    style.schema       = root.get("$schema").asString(style.schema);
    style.id           = root.get("id").asString();
    style.name         = root.get("name").asString();
    style.defaultTempo = root.get("tempo").asInt(style.defaultTempo);
    style.ticksPerBeat = root.get("ticksPerBeat").asInt(style.ticksPerBeat);
    for (const auto& warning : root.get("parseWarnings").asArray()) {
        const auto text = warning.asString();
        if (!text.empty())
            style.parseWarnings.push_back(text);
    }

    const auto& ts = root.get("timeSignature").asArray();
    if (ts.size() == 2) {
        style.beatsPerBar = ts[0].asInt(style.beatsPerBar);
        style.beatUnit    = ts[1].asInt(style.beatUnit);
    }

    const auto& sectionsObj = root.get("sections").asObject();
    for (const auto& [secName, secVal] : sectionsObj) {
        Section section;
        section.name = secName;
        section.barCount = secVal.get("barCount").asInt(section.barCount);

        const auto& partsArr = secVal.get("parts").asArray();
        for (const auto& partVal : partsArr) {
            Part part;
            part.name        = partVal.get("name").asString();
            part.midiChannel = partVal.get("channel").asInt(part.midiChannel);
            part.instrument  = partVal.get("instrument").asString();
            if (partVal.get("bankMsb").isNumber())
                part.bankMsb = partVal.get("bankMsb").asInt();
            if (partVal.get("bankLsb").isNumber())
                part.bankLsb = partVal.get("bankLsb").asInt();
            if (partVal.get("program").isNumber())
                part.program = partVal.get("program").asInt();
            if (partVal.get("volume").isNumber())
                part.volume = partVal.get("volume").asInt();
            if (partVal.get("pan").isNumber())
                part.pan = partVal.get("pan").asInt();
            if (partVal.get("reverb").isNumber())
                part.reverb = partVal.get("reverb").asInt();
            if (partVal.get("chorus").isNumber())
                part.chorus = partVal.get("chorus").asInt();
            part.percussion = partVal.get("percussion").asBool(part.midiChannel == 10);

            const auto& notesArr = partVal.get("notes").asArray();
            for (const auto& noteVal : notesArr) {
                PatternNote n;
                n.tick        = noteVal.get("tick").asInt(0);
                n.duration    = noteVal.get("duration").asInt(0);
                n.pitch       = noteVal.get("pitch").asInt(60);
                n.velocity    = noteVal.get("velocity").asInt(100);
                n.role        = roleFromString(noteVal.get("role").asString("absolute"));
                n.scaleDegree = noteVal.get("scaleDegree").asInt(0);
                part.notes.push_back(n);
            }
            section.parts.push_back(std::move(part));
        }
        style.sections.push_back(std::move(section));
    }

    result.style = std::move(style);
    return result;
}

std::string saveStyleToJson(const Style& style, bool pretty)
{
    namespace J = cadenza::json;
    J::Object root;
    root["$schema"]      = J::Value::string(style.schema);
    root["id"]           = J::Value::string(style.id);
    root["name"]         = J::Value::string(style.name);
    root["tempo"]        = J::Value::number(style.defaultTempo);
    root["ticksPerBeat"] = J::Value::number(style.ticksPerBeat);
    if (!style.parseWarnings.empty()) {
        J::Array warnings;
        for (const auto& warning : style.parseWarnings)
            warnings.push_back(J::Value::string(warning));
        root["parseWarnings"] = J::Value::array(std::move(warnings));
    }

    J::Array ts;
    ts.push_back(J::Value::number(style.beatsPerBar));
    ts.push_back(J::Value::number(style.beatUnit));
    root["timeSignature"] = J::Value::array(std::move(ts));

    J::Object sections;
    for (const auto& sec : style.sections) {
        J::Object secObj;
        secObj["barCount"] = J::Value::number(sec.barCount);

        J::Array parts;
        for (const auto& part : sec.parts) {
            J::Object partObj;
            partObj["name"]       = J::Value::string(part.name);
            partObj["channel"]    = J::Value::number(part.midiChannel);
            partObj["instrument"] = J::Value::string(part.instrument);
            if (part.bankMsb)
                partObj["bankMsb"] = J::Value::number(*part.bankMsb);
            if (part.bankLsb)
                partObj["bankLsb"] = J::Value::number(*part.bankLsb);
            if (part.program)
                partObj["program"] = J::Value::number(*part.program);
            if (part.volume)
                partObj["volume"] = J::Value::number(*part.volume);
            if (part.pan)
                partObj["pan"] = J::Value::number(*part.pan);
            if (part.reverb)
                partObj["reverb"] = J::Value::number(*part.reverb);
            if (part.chorus)
                partObj["chorus"] = J::Value::number(*part.chorus);
            if (part.percussion)
                partObj["percussion"] = J::Value::boolean(true);

            J::Array notes;
            for (const auto& n : part.notes) {
                J::Object noteObj;
                noteObj["tick"]     = J::Value::number(n.tick);
                noteObj["duration"] = J::Value::number(n.duration);
                noteObj["pitch"]    = J::Value::number(n.pitch);
                noteObj["velocity"] = J::Value::number(n.velocity);
                noteObj["role"]     = J::Value::string(roleToString(n.role));
                if (n.role == NoteRole::ScaleTone)
                    noteObj["scaleDegree"] = J::Value::number(n.scaleDegree);
                notes.push_back(J::Value::object(std::move(noteObj)));
            }
            partObj["notes"] = J::Value::array(std::move(notes));
            parts.push_back(J::Value::object(std::move(partObj)));
        }
        secObj["parts"] = J::Value::array(std::move(parts));
        sections[sec.name] = J::Value::object(std::move(secObj));
    }
    root["sections"] = J::Value::object(std::move(sections));

    return J::serialize(J::Value::object(std::move(root)), pretty);
}

LoadResult loadStyleFromFile(const std::string& path)
{
    const auto ext = lowercase(std::filesystem::path(path).extension().string());
    if (ext == ".sty")
        return loadStyleFromStyFile(path);

    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        LoadResult r;
        r.ok = false;
        r.error = "cannot open file: " + path;
        return r;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return loadStyleFromJson(ss.str());
}

LoadResult loadStyleFromStyFile(const std::string& path)
{
    StyParseOptions options;
    options.overrideName = styleNameFromPath(path);
    options.overrideId = styleIdFromName(options.overrideName);

    auto parsed = parseStyFile(path, options);

    LoadResult result;
    result.ok = parsed.ok;
    result.error = std::move(parsed.error);
    result.style = std::move(parsed.style);
    return result;
}

bool saveStyleToFile(const Style& style, const std::string& path, bool pretty)
{
    std::ofstream out(path, std::ios::binary);
    if (!out.good()) return false;
    out << saveStyleToJson(style, pretty);
    return out.good();
}
}
