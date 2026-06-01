#include "ApplicationState.h"
#include "BridgeRouter.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (condition)
        return;

    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

cadenza::BridgeMessage message(std::string type, cadenza::BridgePayload payload = {})
{
    return { std::move(type), std::move(payload) };
}

void playAndStopMirrorTransportStateToJavascript()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    const auto play = router.route(message("play"));
    expect(play.handled, "play should be handled");
    expect(state.playing(), "play should set ApplicationState::playing");
    expect(play.javascript == "window.JuceBridge && window.JuceBridge.onPlayStateChanged(true);",
           "play should emit onPlayStateChanged(true)");

    const auto stop = router.route(message("stop"));
    expect(stop.handled, "stop should be handled");
    expect(!state.playing(), "stop should clear ApplicationState::playing");
    expect(stop.javascript == "window.JuceBridge && window.JuceBridge.onPlayStateChanged(false);",
           "stop should emit onPlayStateChanged(false)");
}

void bpmIsClampedAndAcknowledged()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    const auto high = router.route(message("bpm", { { "value", cadenza::BridgeValue::integer(999) } }));
    expect(high.handled, "bpm should be handled");
    expect(state.bpm() == cadenza::ApplicationState::maxBpm, "bpm should clamp to max");
    expect(high.javascript == "window.JuceBridge && window.JuceBridge.onBpmChanged(240);",
           "bpm should acknowledge clamped value");

    router.route(message("bpm", { { "value", cadenza::BridgeValue::integer(20) } }));
    expect(state.bpm() == cadenza::ApplicationState::minBpm, "bpm should clamp to min");
}

void transposeAndOctaveInvokeRuntimeHooks()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    int transpose = 999;
    int octave = 999;
    cadenza::BridgeHooks hooks;
    hooks.onTransposeChanged = [&](int value) { transpose = value; };
    hooks.onOctaveChanged = [&](int value) { octave = value; };
    router.setHooks(std::move(hooks));

    expect(router.route(message("transpose", { { "value", cadenza::BridgeValue::integer(99) } })).handled,
           "transpose should be handled");
    expect(state.transpose() == cadenza::ApplicationState::maxTranspose, "transpose clamps to max");
    expect(transpose == cadenza::ApplicationState::maxTranspose, "transpose hook receives clamped value");

    expect(router.route(message("octave", { { "value", cadenza::BridgeValue::integer(-99) } })).handled,
           "octave should be handled");
    expect(state.octave() == cadenza::ApplicationState::minOctave, "octave clamps to min");
    expect(octave == cadenza::ApplicationState::minOctave, "octave hook receives clamped value");
}

void octaveMessageRoutesAbsoluteValueToHook()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    int got = 999;
    cadenza::BridgeHooks hooks;
    hooks.onOctaveChanged = [&](int value) { got = value; };
    router.setHooks(std::move(hooks));

    expect(router.route(message("octave", { { "value", cadenza::BridgeValue::integer(2) } })).handled,
           "octave message should be handled");
    expect(state.octave() == 2, "octave state should update to 2");
    expect(got == 2, "octave hook should receive the new value (drives MidiRouter::setLiveOctave)");

    router.route(message("octave", { { "value", cadenza::BridgeValue::integer(-1) } }));
    expect(state.octave() == -1 && got == -1, "octave -1 routes through to the hook");
}

void channelCommandsUpdateMixerState()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    router.route(message("volume", {
        { "channel", cadenza::BridgeValue::text("right1") },
        { "value", cadenza::BridgeValue::integer(100) },
    }));
    router.route(message("pan", {
        { "channel", cadenza::BridgeValue::text("right1") },
        { "value", cadenza::BridgeValue::integer(-99) },
    }));
    router.route(message("solo", {
        { "channel", cadenza::BridgeValue::text("right1") },
        { "value", cadenza::BridgeValue::boolean(true) },
    }));

    const auto* channel = state.channel("right1");
    expect(channel != nullptr, "right1 should exist");
    expect(channel != nullptr && channel->volumeDb == cadenza::ApplicationState::maxVolumeDb,
           "volume should clamp to max dB");
    expect(channel != nullptr && channel->pan == cadenza::ApplicationState::minPan,
           "pan should clamp to min");
    expect(channel != nullptr && channel->solo, "solo should update channel state");
}

void chordSourceAndSyncroStopInvokeRuntimeHooks()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    std::string source;
    bool sourceEnabled = true;
    bool syncroStop = true;
    cadenza::BridgeHooks hooks;
    hooks.onChordSourceChanged = [&](const std::string& s, bool enabled) {
        source = s;
        sourceEnabled = enabled;
    };
    hooks.onSyncroStopChanged = [&](bool enabled) { syncroStop = enabled; };
    router.setHooks(std::move(hooks));

    expect(router.route(message("chordSource", {
        { "source", cadenza::BridgeValue::text("memory") },
        { "value", cadenza::BridgeValue::boolean(true) },
    })).handled, "chordSource memory should be handled");
    expect(state.chordSourceEnabled("memory"), "memory source state should update");
    expect(source == "memory" && sourceEnabled, "chordSource hook should receive source and enabled");

    expect(router.route(message("syncroStop", {
        { "value", cadenza::BridgeValue::boolean(false) },
    })).handled, "syncroStop should be handled");
    expect(!state.syncroStopOnRelease(), "syncroStop state should update");
    expect(!syncroStop, "syncroStop hook should receive disabled");
}

void noteOnFlashesTheWebPiano()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    const auto result = router.route(message("noteOn", { { "note", cadenza::BridgeValue::integer(60) } }));
    expect(result.handled, "noteOn should be handled");
    expect(result.javascript == "window.JuceBridge && window.JuceBridge.onNoteReceived(60);",
           "noteOn should emit onNoteReceived with the MIDI note");
}

void openStyleFileInvokesHook()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    bool opened = false;
    cadenza::BridgeHooks hooks;
    hooks.onOpenStyleFile = [&] { opened = true; };
    router.setHooks(std::move(hooks));

    const auto result = router.route(message("openStyleFile"));
    expect(result.handled, "openStyleFile should be handled");
    expect(opened, "openStyleFile should invoke host hook");
}

void openSoundFontFileInvokesHook()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    bool opened = false;
    cadenza::BridgeHooks hooks;
    hooks.onOpenSoundFontFile = [&] { opened = true; };
    router.setHooks(std::move(hooks));

    const auto result = router.route(message("openSoundFontFile"));
    expect(result.handled, "openSoundFontFile should be handled");
    expect(opened, "openSoundFontFile should invoke host hook");
}

void pluginMessagesInvokeHooks()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    bool opened = false, cleared = false;
    cadenza::BridgeHooks hooks;
    hooks.onOpenPluginFile = [&] { opened = true; };
    hooks.onClearPlugin = [&] { cleared = true; };
    router.setHooks(std::move(hooks));

    expect(router.route(message("openPluginFile")).handled, "openPluginFile should be handled");
    expect(opened, "openPluginFile should invoke host hook");
    expect(router.route(message("clearPlugin")).handled, "clearPlugin should be handled");
    expect(cleared, "clearPlugin should invoke host hook");
}

void exportPlaybackDiagnosticsInvokesHook()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    bool exported = false;
    cadenza::BridgeHooks hooks;
    hooks.onExportPlaybackDiagnostics = [&] { exported = true; };
    router.setHooks(std::move(hooks));

    const auto result = router.route(message("exportPlaybackDiagnostics"));
    expect(result.handled, "exportPlaybackDiagnostics should be handled");
    expect(exported, "exportPlaybackDiagnostics should invoke host hook");
}

void openSongFileInvokesHook()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    bool opened = false;
    cadenza::BridgeHooks hooks;
    hooks.onOpenSongFile = [&] { opened = true; };
    router.setHooks(std::move(hooks));

    const auto result = router.route(message("openSongFile"));
    expect(result.handled, "openSongFile should be handled");
    expect(opened, "openSongFile should invoke host hook");
}

void songModeInvokesHookWithEnabledFlag()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    int calls = 0;
    bool lastEnabled = false;
    cadenza::BridgeHooks hooks;
    hooks.onSongModeChanged = [&](bool enabled) { ++calls; lastEnabled = enabled; };
    router.setHooks(std::move(hooks));

    expect(router.route(message("songMode", { { "value", cadenza::BridgeValue::boolean(true) } })).handled,
           "songMode should be handled");
    expect(calls == 1 && lastEnabled, "songMode true should invoke hook with enabled=true");

    router.route(message("songMode", { { "value", cadenza::BridgeValue::boolean(false) } }));
    expect(calls == 2 && !lastEnabled, "songMode false should invoke hook with enabled=false");
}

void selectStyleInvokesHook()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    std::string selected;
    cadenza::BridgeHooks hooks;
    hooks.onSelectStyle = [&](const std::string& name) { selected = name; };
    router.setHooks(std::move(hooks));

    const auto result = router.route(message("selectStyle", {
        { "name", cadenza::BridgeValue::text("8-beat-pop") },
    }));
    expect(result.handled, "selectStyle should be handled");
    expect(selected == "8-beat-pop", "selectStyle should invoke host hook with name");
}

void invalidMessagesAreRejected()
{
    cadenza::ApplicationState state;
    cadenza::BridgeRouter router(state);

    expect(!router.route(message("not-a-command")).handled, "unknown commands should not be handled");
    expect(!router.route(message("pad", { { "index", cadenza::BridgeValue::integer(99) } })).handled,
           "invalid pad indexes should not be handled");
    expect(!router.route(message("volume", { { "channel", cadenza::BridgeValue::text("missing") } })).handled,
           "unknown mixer channels should not be handled");
}
}

int main()
{
    playAndStopMirrorTransportStateToJavascript();
    bpmIsClampedAndAcknowledged();
    transposeAndOctaveInvokeRuntimeHooks();
    octaveMessageRoutesAbsoluteValueToHook();
    channelCommandsUpdateMixerState();
    chordSourceAndSyncroStopInvokeRuntimeHooks();
    noteOnFlashesTheWebPiano();
    openStyleFileInvokesHook();
    openSoundFontFileInvokesHook();
    openSongFileInvokesHook();
    songModeInvokesHookWithEnabledFlag();
    pluginMessagesInvokeHooks();
    exportPlaybackDiagnosticsInvokesHook();
    selectStyleInvokesHook();
    invalidMessagesAreRejected();

    if (failures != 0)
        return EXIT_FAILURE;

    std::cout << "All bridge router tests passed\n";
    return EXIT_SUCCESS;
}
