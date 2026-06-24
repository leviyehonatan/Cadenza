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

const char* yamahaFormatToString(YamahaStyleFormat format) noexcept
{
    switch (format) {
        case YamahaStyleFormat::SFF1:    return "sff1";
        case YamahaStyleFormat::SFF2:    return "sff2";
        case YamahaStyleFormat::Unknown: return "unknown";
    }
    return "unknown";
}

YamahaStyleFormat yamahaFormatFromString(const std::string& value) noexcept
{
    if (value == "sff1") return YamahaStyleFormat::SFF1;
    if (value == "sff2") return YamahaStyleFormat::SFF2;
    return YamahaStyleFormat::Unknown;
}

const char* yamahaNtrToString(YamahaNtr value) noexcept
{
    switch (value) {
        case YamahaNtr::RootTransposition: return "root-transposition";
        case YamahaNtr::RootFixed:         return "root-fixed";
        case YamahaNtr::Guitar:            return "guitar";
        case YamahaNtr::Unknown:           return "unknown";
    }
    return "unknown";
}

YamahaNtr yamahaNtrFromString(const std::string& value) noexcept
{
    if (value == "root-transposition") return YamahaNtr::RootTransposition;
    if (value == "root-fixed") return YamahaNtr::RootFixed;
    if (value == "guitar") return YamahaNtr::Guitar;
    return YamahaNtr::Unknown;
}

const char* yamahaNttToString(YamahaNtt value) noexcept
{
    switch (value) {
        case YamahaNtt::Bypass:        return "bypass";
        case YamahaNtt::Melody:        return "melody";
        case YamahaNtt::Chord:         return "chord";
        case YamahaNtt::MelodicMinor:  return "melodic-minor";
        case YamahaNtt::HarmonicMinor: return "harmonic-minor";
        case YamahaNtt::NaturalMinor:  return "natural-minor";
        case YamahaNtt::Dorian:        return "dorian";
        case YamahaNtt::AllPurpose:    return "all-purpose";
        case YamahaNtt::Stroke:        return "stroke";
        case YamahaNtt::Arpeggio:      return "arpeggio";
        case YamahaNtt::Unknown:       return "unknown";
    }
    return "unknown";
}

YamahaNtt yamahaNttFromString(const std::string& value) noexcept
{
    if (value == "bypass") return YamahaNtt::Bypass;
    if (value == "melody") return YamahaNtt::Melody;
    if (value == "chord") return YamahaNtt::Chord;
    if (value == "melodic-minor") return YamahaNtt::MelodicMinor;
    if (value == "harmonic-minor") return YamahaNtt::HarmonicMinor;
    if (value == "natural-minor") return YamahaNtt::NaturalMinor;
    if (value == "dorian") return YamahaNtt::Dorian;
    if (value == "all-purpose") return YamahaNtt::AllPurpose;
    if (value == "stroke") return YamahaNtt::Stroke;
    if (value == "arpeggio") return YamahaNtt::Arpeggio;
    return YamahaNtt::Unknown;
}

const char* yamahaRetriggerRuleToString(YamahaRetriggerRule value) noexcept
{
    switch (value) {
        case YamahaRetriggerRule::Stop:             return "stop";
        case YamahaRetriggerRule::PitchShift:       return "pitch-shift";
        case YamahaRetriggerRule::PitchShiftToRoot: return "pitch-shift-to-root";
        case YamahaRetriggerRule::Retrigger:        return "retrigger";
        case YamahaRetriggerRule::RetriggerToRoot:  return "retrigger-to-root";
        case YamahaRetriggerRule::NoteGenerator:    return "note-generator";
        case YamahaRetriggerRule::Unknown:          return "unknown";
    }
    return "unknown";
}

YamahaRetriggerRule yamahaRetriggerRuleFromString(const std::string& value) noexcept
{
    if (value == "stop") return YamahaRetriggerRule::Stop;
    if (value == "pitch-shift") return YamahaRetriggerRule::PitchShift;
    if (value == "pitch-shift-to-root") return YamahaRetriggerRule::PitchShiftToRoot;
    if (value == "retrigger") return YamahaRetriggerRule::Retrigger;
    if (value == "retrigger-to-root") return YamahaRetriggerRule::RetriggerToRoot;
    if (value == "note-generator") return YamahaRetriggerRule::NoteGenerator;
    return YamahaRetriggerRule::Unknown;
}

YamahaPolicySource yamahaPolicySourceFromString(const std::string& value) noexcept;

Part loadPart(const cadenza::json::Value& partVal)
{
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
    part.octaveOffset = partVal.get("octaveOffset").asInt(part.octaveOffset);

    const auto& policyVal = partVal.get("yamahaPolicy");
    if (policyVal.isObject()) {
        YamahaChannelPolicy policy;
        policy.source = yamahaPolicySourceFromString(policyVal.get("source").asString());
        policy.sourceChannel = policyVal.get("sourceChannel").asInt(policy.sourceChannel);
        policy.destinationPart = policyVal.get("destinationPart").asString();
        policy.destinationType = policyVal.get("destinationType").asString();
        policy.destinationName = policyVal.get("destinationName").asString();
        if (policyVal.get("sourceRoot").isString())
            policy.sourceRoot = policyVal.get("sourceRoot").asString();
        if (policyVal.get("sourceChord").isString())
            policy.sourceChord = policyVal.get("sourceChord").asString();
        policy.ntr = yamahaNtrFromString(policyVal.get("ntr").asString());
        policy.ntt = yamahaNttFromString(policyVal.get("ntt").asString());
        policy.bassOn = policyVal.get("bassOn").asBool(policy.bassOn);
        if (policyVal.get("chordRootUpperLimit").isNumber())
            policy.chordRootUpperLimit = policyVal.get("chordRootUpperLimit").asInt();
        if (policyVal.get("noteLowLimit").isNumber())
            policy.noteLowLimit = policyVal.get("noteLowLimit").asInt();
        if (policyVal.get("noteHighLimit").isNumber())
            policy.noteHighLimit = policyVal.get("noteHighLimit").asInt();
        policy.retriggerRule = yamahaRetriggerRuleFromString(
            policyVal.get("retriggerRule").asString());
        part.yamahaPolicy = std::move(policy);
    }

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

    const auto& autoArr = partVal.get("automation").asArray();
    for (const auto& aVal : autoArr) {
        AutomationEvent ev;
        ev.tick  = aVal.get("tick").asInt(0);
        ev.type  = aVal.get("type").asInt(0);
        ev.value = aVal.get("value").asInt(0);
        part.automation.push_back(ev);
    }

    return part;
}

Section loadSection(const std::string& secName, const cadenza::json::Value& secVal)
{
    Section section;
    section.name = secName;
    section.barCount = secVal.get("barCount").asInt(section.barCount);

    const auto& partsArr = secVal.get("parts").asArray();
    for (const auto& partVal : partsArr)
        section.parts.push_back(loadPart(partVal));

    return section;
}

const char* yamahaPolicySourceToString(YamahaPolicySource value) noexcept
{
    switch (value) {
        case YamahaPolicySource::CASM:     return "casm";
        case YamahaPolicySource::Ctb2:     return "ctb2";
        case YamahaPolicySource::Ctab:     return "ctab";
        case YamahaPolicySource::Fallback: return "fallback";
    }
    return "fallback";
}

YamahaPolicySource yamahaPolicySourceFromString(const std::string& value) noexcept
{
    if (value == "casm") return YamahaPolicySource::CASM;
    if (value == "ctb2") return YamahaPolicySource::Ctb2;
    if (value == "ctab") return YamahaPolicySource::Ctab;
    return YamahaPolicySource::Fallback;
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
    style.yamahaFormat = yamahaFormatFromString(root.get("yamahaFormat").asString());
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
    std::vector<std::string> loadedSectionNames;
    for (const auto& orderVal : root.get("sectionOrder").asArray()) {
        const auto sectionName = orderVal.asString();
        const auto it = sectionsObj.find(sectionName);
        if (sectionName.empty() || it == sectionsObj.end()
            || std::find(loadedSectionNames.begin(), loadedSectionNames.end(), sectionName)
                != loadedSectionNames.end())
            continue;
        style.sections.push_back(loadSection(it->first, it->second));
        loadedSectionNames.push_back(sectionName);
    }
    for (const auto& [secName, secVal] : sectionsObj) {
        if (std::find(loadedSectionNames.begin(), loadedSectionNames.end(), secName)
            != loadedSectionNames.end())
            continue;
        style.sections.push_back(loadSection(secName, secVal));
    }

    for (const auto& slotVal : root.get("ots").asArray()) {
        const int s = slotVal.get("slot").asInt(-1);
        if (s < 0 || s >= static_cast<int>(style.ots.size()))
            continue;
        auto& slot = style.ots[static_cast<std::size_t>(s)];
        slot.present = true;
        for (const auto& layerVal : slotVal.get("layers").asArray()) {
            const int i = layerVal.get("layer").asInt(-1);
            if (i < 0 || i >= static_cast<int>(slot.layers.size()))
                continue;
            auto& voice = slot.layers[static_cast<std::size_t>(i)];
            voice.present = true;
            if (layerVal.get("program").isNumber())
                voice.program = layerVal.get("program").asInt();
            if (layerVal.get("volume").isNumber())
                voice.volume = layerVal.get("volume").asInt();
        }
    }

    result.style = std::move(style);
    return result;
}

SectionsLoadResult loadSectionsFromJson(const std::string& json)
{
    SectionsLoadResult result;

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

    const auto& sectionsValue = root.get("sections");
    if (!sectionsValue.isObject()) {
        result.ok = false;
        result.error = "expected sections object";
        return result;
    }

    for (const auto& [secName, secVal] : sectionsValue.asObject()) {
        if (secName.empty()) {
            result.ok = false;
            result.error = "section id is empty";
            result.sections.clear();
            return result;
        }
        result.sections.push_back(loadSection(secName, secVal));
    }
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
    root["yamahaFormat"] = J::Value::string(yamahaFormatToString(style.yamahaFormat));
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
    J::Array sectionOrder;
    for (const auto& sec : style.sections) {
        sectionOrder.push_back(J::Value::string(sec.name));
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
            partObj["percussion"] = J::Value::boolean(part.percussion);
            partObj["octaveOffset"] = J::Value::number(part.octaveOffset);

            if (part.yamahaPolicy) {
                const auto& policy = *part.yamahaPolicy;
                J::Object policyObj;
                policyObj["source"] = J::Value::string(yamahaPolicySourceToString(policy.source));
                policyObj["sourceChannel"] = J::Value::number(policy.sourceChannel);
                policyObj["destinationPart"] = J::Value::string(policy.destinationPart);
                policyObj["destinationType"] = J::Value::string(policy.destinationType);
                policyObj["destinationName"] = J::Value::string(policy.destinationName);
                if (policy.sourceRoot)
                    policyObj["sourceRoot"] = J::Value::string(*policy.sourceRoot);
                if (policy.sourceChord)
                    policyObj["sourceChord"] = J::Value::string(*policy.sourceChord);
                policyObj["ntr"] = J::Value::string(yamahaNtrToString(policy.ntr));
                policyObj["ntt"] = J::Value::string(yamahaNttToString(policy.ntt));
                policyObj["bassOn"] = J::Value::boolean(policy.bassOn);
                if (policy.chordRootUpperLimit)
                    policyObj["chordRootUpperLimit"] = J::Value::number(*policy.chordRootUpperLimit);
                if (policy.noteLowLimit)
                    policyObj["noteLowLimit"] = J::Value::number(*policy.noteLowLimit);
                if (policy.noteHighLimit)
                    policyObj["noteHighLimit"] = J::Value::number(*policy.noteHighLimit);
                policyObj["retriggerRule"] = J::Value::string(
                    yamahaRetriggerRuleToString(policy.retriggerRule));
                partObj["yamahaPolicy"] = J::Value::object(std::move(policyObj));
            }

            J::Array notes;
            for (const auto& n : part.notes) {
                J::Object noteObj;
                noteObj["tick"]     = J::Value::number(n.tick);
                noteObj["duration"] = J::Value::number(n.duration);
                noteObj["pitch"]    = J::Value::number(n.pitch);
                noteObj["velocity"] = J::Value::number(n.velocity);
                noteObj["role"]     = J::Value::string(roleToString(n.role));
                noteObj["scaleDegree"] = J::Value::number(n.scaleDegree);
                notes.push_back(J::Value::object(std::move(noteObj)));
            }
            partObj["notes"] = J::Value::array(std::move(notes));

            if (!part.automation.empty()) {
                J::Array autos;
                for (const auto& a : part.automation) {
                    J::Object aObj;
                    aObj["tick"]  = J::Value::number(a.tick);
                    aObj["type"]  = J::Value::number(a.type);
                    aObj["value"] = J::Value::number(a.value);
                    autos.push_back(J::Value::object(std::move(aObj)));
                }
                partObj["automation"] = J::Value::array(std::move(autos));
            }
            parts.push_back(J::Value::object(std::move(partObj)));
        }
        secObj["parts"] = J::Value::array(std::move(parts));
        sections[sec.name] = J::Value::object(std::move(secObj));
    }
    root["sections"] = J::Value::object(std::move(sections));
    root["sectionOrder"] = J::Value::array(std::move(sectionOrder));

    bool anyOts = false;
    for (const auto& slot : style.ots)
        if (slot.present) anyOts = true;
    if (anyOts) {
        J::Array ots;
        for (std::size_t s = 0; s < style.ots.size(); ++s) {
            const auto& slot = style.ots[s];
            if (!slot.present)
                continue;
            J::Object slotObj;
            slotObj["slot"] = J::Value::number(static_cast<int>(s));
            J::Array layers;
            for (std::size_t i = 0; i < slot.layers.size(); ++i) {
                const auto& voice = slot.layers[i];
                if (!voice.present)
                    continue;
                J::Object layerObj;
                layerObj["layer"] = J::Value::number(static_cast<int>(i));
                if (voice.program >= 0)
                    layerObj["program"] = J::Value::number(voice.program);
                if (voice.volume >= 0)
                    layerObj["volume"] = J::Value::number(voice.volume);
                layers.push_back(J::Value::object(std::move(layerObj)));
            }
            slotObj["layers"] = J::Value::array(std::move(layers));
            ots.push_back(J::Value::object(std::move(slotObj)));
        }
        root["ots"] = J::Value::array(std::move(ots));
    }

    return J::serialize(J::Value::object(std::move(root)), pretty);
}

LoadResult loadStyleFromFile(const std::string& path)
{
    // Every Yamaha SFF container extension holds the same SMF+CASM payload and
    // must go through the .sty parser; only .cstyle is Cadenza's own JSON format.
    // .sty user, .prs preset (Genos), .sst session, .fps free play, .bcs.
    const auto ext = lowercase(std::filesystem::path(path).extension().string());
    if (ext == ".sty" || ext == ".prs" || ext == ".sst"
        || ext == ".fps" || ext == ".bcs")
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
