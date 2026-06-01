#pragma once

#include "ApplicationState.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace cadenza
{
class BridgeValue
{
public:
    enum class Type
    {
        missing,
        integer,
        boolean,
        text,
    };

    static BridgeValue integer(int value) noexcept;
    static BridgeValue boolean(bool value) noexcept;
    static BridgeValue text(std::string value);

    Type type() const noexcept { return m_type; }
    int asInt(int fallback = 0) const noexcept;
    bool asBool(bool fallback = false) const noexcept;
    std::string asString(const std::string& fallback = {}) const;

private:
    Type m_type = Type::missing;
    int m_intValue = 0;
    bool m_boolValue = false;
    std::string m_textValue;
};

using BridgePayload = std::unordered_map<std::string, BridgeValue>;

struct BridgeMessage
{
    std::string type;
    BridgePayload payload;
};

struct BridgeResult
{
    bool handled = false;
    std::string javascript;
};

// ------------------------------------------------------------
// Hooks the host (JUCE app) installs so that bridge messages
// produce real audio / style actions. cadenza_core stays
// JUCE-free; these are plain std::function callbacks.
// ------------------------------------------------------------
struct BridgeHooks
{
    std::function<void(int channel, int note, int velocity)> onNoteOn;
    std::function<void(int channel, int note)>               onNoteOff;
    std::function<void(bool playing)>                        onPlayStateChanged;
    std::function<void(int bpm)>                             onBpmChanged;
    std::function<void(const std::string& key)>              onKeyChanged;
    std::function<void(int semitones)>                       onTransposeChanged;
    std::function<void(int octaves)>                         onOctaveChanged;
    std::function<void(const std::string& bankName)>         onBankMemoryChanged;
    std::function<void(int slot)>                            onStyleMemoryChanged;
    std::function<void(int padIndex, bool on)>               onPadChanged;
    std::function<void(const std::string& styleId)>          onSelectStyle;
    std::function<void()>                                    onOpenStyleFile;
    std::function<void()>                                    onOpenSoundFontFile;
    std::function<void()>                                    onOpenSongFile;
    std::function<void(bool enabled)>                        onSongModeChanged;
    std::function<void()>                                    onOpenPluginFile;
    std::function<void()>                                    onClearPlugin;
    std::function<void()>                                    onExportPlaybackDiagnostics;
    std::function<void(const std::string& source, bool enabled)> onChordSourceChanged;
    std::function<void(bool enabled)>                        onSyncroStopChanged;
    std::function<void()>                                    onExitApp;
    std::function<void()>                                    onPageReady;
};

class BridgeRouter
{
public:
    explicit BridgeRouter(ApplicationState& state);

    void setHooks(BridgeHooks hooks) { m_hooks = std::move(hooks); }

    BridgeResult route(const BridgeMessage& message);

private:
    const BridgeValue& value(const BridgePayload& payload, const std::string& key) const noexcept;
    static std::string escapeJavascriptString(const std::string& value);
    static std::string call(const std::string& functionName, const std::string& argument);

    ApplicationState& m_state;
    BridgeHooks m_hooks;
    BridgeValue m_missing;
};
}
