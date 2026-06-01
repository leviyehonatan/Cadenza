#include "BridgeRouter.h"

#include <sstream>
#include <utility>

namespace cadenza
{
BridgeValue BridgeValue::integer(int value) noexcept
{
    BridgeValue result;
    result.m_type = Type::integer;
    result.m_intValue = value;
    return result;
}

BridgeValue BridgeValue::boolean(bool value) noexcept
{
    BridgeValue result;
    result.m_type = Type::boolean;
    result.m_boolValue = value;
    return result;
}

BridgeValue BridgeValue::text(std::string value)
{
    BridgeValue result;
    result.m_type = Type::text;
    result.m_textValue = std::move(value);
    return result;
}

int BridgeValue::asInt(int fallback) const noexcept
{
    if (m_type == Type::integer) return m_intValue;
    if (m_type == Type::boolean) return m_boolValue ? 1 : 0;
    return fallback;
}

bool BridgeValue::asBool(bool fallback) const noexcept
{
    if (m_type == Type::boolean) return m_boolValue;
    if (m_type == Type::integer) return m_intValue != 0;
    return fallback;
}

std::string BridgeValue::asString(const std::string& fallback) const
{
    if (m_type == Type::text) return m_textValue;
    return fallback;
}

BridgeRouter::BridgeRouter(ApplicationState& state)
    : m_state(state)
{
}

BridgeResult BridgeRouter::route(const BridgeMessage& message)
{
    if (message.type == "play")
    {
        m_state.setPlaying(true);
        if (m_hooks.onPlayStateChanged) m_hooks.onPlayStateChanged(true);
        return { true, call("onPlayStateChanged", "true") };
    }

    if (message.type == "stop")
    {
        m_state.setPlaying(false);
        if (m_hooks.onPlayStateChanged) m_hooks.onPlayStateChanged(false);
        return { true, call("onPlayStateChanged", "false") };
    }

    if (message.type == "bpm")
    {
        const auto bpm = m_state.setBpm(value(message.payload, "value").asInt(m_state.bpm()));
        if (m_hooks.onBpmChanged) m_hooks.onBpmChanged(bpm);
        return { true, call("onBpmChanged", std::to_string(bpm)) };
    }

    if (message.type == "transpose")
    {
        const auto t = m_state.setTranspose(value(message.payload, "value").asInt(m_state.transpose()));
        if (m_hooks.onTransposeChanged) m_hooks.onTransposeChanged(t);
        return { true, {} };
    }

    if (message.type == "octave")
    {
        const auto o = m_state.setOctave(value(message.payload, "value").asInt(m_state.octave()));
        if (m_hooks.onOctaveChanged) m_hooks.onOctaveChanged(o);
        return { true, {} };
    }

    if (message.type == "key")
    {
        const auto k = value(message.payload, "value").asString(m_state.key());
        m_state.setKey(k);
        if (m_hooks.onKeyChanged) m_hooks.onKeyChanged(k);
        return { true, {} };
    }

    if (message.type == "record")
    {
        m_state.setRecording(value(message.payload, "value").asBool(m_state.recording()));
        return { true, {} };
    }

    if (message.type == "volume")
    {
        const auto channel = value(message.payload, "channel").asString();
        const auto handled = m_state.setChannelVolume(channel, value(message.payload, "value").asInt());
        return { handled, {} };
    }

    if (message.type == "pan")
    {
        const auto channel = value(message.payload, "channel").asString();
        const auto handled = m_state.setChannelPan(channel, value(message.payload, "value").asInt());
        return { handled, {} };
    }

    if (message.type == "solo")
    {
        const auto channel = value(message.payload, "channel").asString();
        const auto handled = m_state.setChannelSolo(channel, value(message.payload, "value").asBool());
        return { handled, {} };
    }

    if (message.type == "mute")
    {
        const auto channel = value(message.payload, "channel").asString();
        const auto handled = m_state.setChannelMute(channel, value(message.payload, "value").asBool());
        return { handled, {} };
    }

    if (message.type == "melodyOnOff")
    {
        const auto channel = value(message.payload, "channel").asString();
        const auto handled = m_state.setMelodyChannelEnabled(channel, value(message.payload, "value").asBool());
        return { handled, {} };
    }

    if (message.type == "chordSource")
    {
        const auto source = value(message.payload, "source").asString();
        const auto handled = m_state.setChordSourceEnabled(source, value(message.payload, "value").asBool());
        if (handled && m_hooks.onChordSourceChanged)
            m_hooks.onChordSourceChanged(source, m_state.chordSourceEnabled(source));
        return { handled, {} };
    }

    if (message.type == "bankMemory")
    {
        const auto name = value(message.payload, "name").asString(m_state.bankMemory());
        m_state.setBankMemory(name);
        if (m_hooks.onBankMemoryChanged) m_hooks.onBankMemoryChanged(name);
        return { true, {} };
    }

    if (message.type == "pad")
    {
        const auto index = value(message.payload, "index").asInt(-1);
        const auto on = value(message.payload, "value").asBool();
        const auto handled = m_state.setPad(index, on);
        if (handled && m_hooks.onPadChanged) m_hooks.onPadChanged(index, on);
        return { handled, {} };
    }

    if (message.type == "styleMemory")
    {
        const auto slot = m_state.setStyleMemory(value(message.payload, "slot").asInt(m_state.styleMemory()));
        if (m_hooks.onStyleMemoryChanged) m_hooks.onStyleMemoryChanged(slot);
        return { true, {} };
    }

    if (message.type == "crossfade")
    {
        m_state.setCrossfade(value(message.payload, "value").asInt(m_state.crossfade()));
        return { true, {} };
    }

    if (message.type == "noteOn")
    {
        const auto note = value(message.payload, "note").asInt(-1);
        const auto vel  = value(message.payload, "velocity").asInt(100);
        if (note < 0) return { false, {} };
        if (m_hooks.onNoteOn) m_hooks.onNoteOn(0, note, vel);
        return { true, call("onNoteReceived", std::to_string(note)) };
    }

    if (message.type == "noteOff")
    {
        const auto note = value(message.payload, "note").asInt(-1);
        if (note < 0) return { false, {} };
        if (m_hooks.onNoteOff) m_hooks.onNoteOff(0, note);
        return { true, {} };
    }

    if (message.type == "selectStyle")
    {
        const auto name = value(message.payload, "name").asString();
        if (m_hooks.onSelectStyle) m_hooks.onSelectStyle(name);
        return { true, {} };
    }

    if (message.type == "openStyleFile")
    {
        if (m_hooks.onOpenStyleFile) m_hooks.onOpenStyleFile();
        return { true, {} };
    }

    if (message.type == "openSoundFontFile")
    {
        if (m_hooks.onOpenSoundFontFile) m_hooks.onOpenSoundFontFile();
        return { true, {} };
    }

    if (message.type == "openSongFile")
    {
        if (m_hooks.onOpenSongFile) m_hooks.onOpenSongFile();
        return { true, {} };
    }

    if (message.type == "songMode")
    {
        const auto enabled = value(message.payload, "value").asBool(true);
        if (m_hooks.onSongModeChanged) m_hooks.onSongModeChanged(enabled);
        return { true, {} };
    }

    if (message.type == "openPluginFile")
    {
        if (m_hooks.onOpenPluginFile) m_hooks.onOpenPluginFile();
        return { true, {} };
    }

    if (message.type == "clearPlugin")
    {
        if (m_hooks.onClearPlugin) m_hooks.onClearPlugin();
        return { true, {} };
    }

    if (message.type == "exportPlaybackDiagnostics")
    {
        if (m_hooks.onExportPlaybackDiagnostics) m_hooks.onExportPlaybackDiagnostics();
        return { true, {} };
    }

    if (message.type == "syncroStop")
    {
        const auto enabled = value(message.payload, "value").asBool(true);
        m_state.setSyncroStopOnRelease(enabled);
        if (m_hooks.onSyncroStopChanged) m_hooks.onSyncroStopChanged(enabled);
        return { true, {} };
    }

    if (message.type == "exitApp")
    {
        if (m_hooks.onExitApp) m_hooks.onExitApp();
        return { true, {} };
    }

    if (message.type == "pageReady")
    {
        if (m_hooks.onPageReady) m_hooks.onPageReady();
        return { true, {} };
    }

    if (message.type == "selectPart" || message.type == "selectChannel")
        return { true, {} };

    return { false, {} };
}

const BridgeValue& BridgeRouter::value(const BridgePayload& payload, const std::string& key) const noexcept
{
    const auto iter = payload.find(key);
    if (iter == payload.end())
        return m_missing;

    return iter->second;
}

std::string BridgeRouter::escapeJavascriptString(const std::string& value)
{
    std::ostringstream stream;
    stream << '"';

    for (const auto character : value)
    {
        switch (character)
        {
            case '\\': stream << "\\\\"; break;
            case '"':  stream << "\\\""; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:   stream << character; break;
        }
    }

    stream << '"';
    return stream.str();
}

std::string BridgeRouter::call(const std::string& functionName, const std::string& argument)
{
    return "window.JuceBridge && window.JuceBridge." + functionName + "(" + argument + ");";
}
}
