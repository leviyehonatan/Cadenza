#pragma once

#include "ApplicationState.h"
#include "BridgeRouter.h"
#include "Audio/AudioEngine.h"
#include "Audio/MixerModel.h"
#include "Midi/MidiRouter.h"
#include "Arranger/StyleEngine.h"
#include "Arranger/Style.h"
#include "Arranger/SongPlayer.h"
#include "Arranger/StyleRecorder.h"

#include <atomic>
#include "Settings/SettingsStore.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <string>

namespace cadenza::ui { class NativePanel; class StylePartEditorWindow; }

class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

    // juce::Timer — drives song-mode auto-stepping on the message thread.
    void timerCallback() override;

    // juce::ChangeListener — persists the audio-device choice when it changes.
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void showAudioSettings();
    void showMidiSettings();        // MIDI device list + button-mapping (learn) window
    std::string audioStateFilePath() const;

private:
    juce::WebBrowserComponent::Options createBrowserOptions();
    static juce::File findWebRoot();
    static juce::String startupUrl();

    void handleBridgePayload(const juce::var& payload);
    cadenza::BridgeMessage parseBridgeMessage(const juce::String& json) const;
    cadenza::BridgeValue convertValue(const juce::var& value) const;

    void installBridgeHooks();
    void applyRuntimeStateToEngines();
    void tryLoadFactoryStyle();
    bool tryLoadLastStyle();
    void tryLoadFactorySoundFont();
    void openStyleFileChooser();
    void openSoundFontFileChooser();
    void openSongFileChooser();
    void openPluginFileChooser();
    void choosePartInstrument(int channel);   // load a VST3 instrument onto a mixer channel
    void chooseRightLayerInstrument(int layer); // load a VST3 instrument onto a Right 1/2/3 layer
    bool loadAndApplyPluginFile(const juce::File& file);
    void clearMasterEffect();
    void pushPluginStateToWeb();
    void applyRightHand();       // (re)assert Right 1/2/3 enable + program + volume + octave

    // Native control panel (source of truth for live performance controls).
    void buildNativePanel();
    void updateNativePanelStyle();   // push current style name + sections + mixer to the panel
    void applyMixerState();          // send each channel's effective volume (CC7) to the synth
    void applyStyleMix(const std::string& styleId);  // apply saved per-style mixer overrides
    void persistStyleMix();          // save current mixer strips for the current style
    void captureRegistration(int slot);   // snapshot the live setup into a registration
    void recallRegistration(int slot);    // restore a saved registration
    void applyOts(int slot);                                // recall one One Touch Setting slot
    void applyOtsLinkForSection(const std::string& name);   // OTS Link on Main A-D changes
    void refreshOtsAvailability();                          // dim/enable the panel's OTS buttons
    void exportPlaybackDiagnostics();
    void triggerSection(const std::string& id);   // select a section, with one-shot intro/fill/ending handling
    void togglePlayback();                         // start/stop (shared by the Play button + MIDI map)
    void startFadeOut();                           // fade the master out, then stop
    void executeControlCommand(const std::string& command);  // run a MIDI-mapped command
    bool loadAndApplyStyleFile(const juce::File& file);
    bool loadAndApplySongFile(const juce::File& file);
    bool selectStyleById(const std::string& styleId);
    void setSongMode(bool enabled);

    // Style Recorder (record your own style patterns into a .cstyle).
    void recorderNewSession(int bars);
    void recorderSetPart(int partIndex);
    void recorderArm(bool on);            // off commits the take into the style
    void recorderClearPart();
    void recorderSave();
    void recorderExit();
    void recorderRefreshStyle();          // republish the in-progress style + UI
    juce::String recorderStatusText() const;
    void recorderOpenEditor();            // piano-roll editor for the target part
    void recorderReloadEditor();          // refresh the editor contents (if open)
    void recorderCloseEditor();
    void applySongStepForBar(int bar, bool applySection = true);
    void queueSongSectionForBar(int bar);
    bool loadAndApplySoundFontFile(const juce::File& file, bool persist);
    juce::Array<juce::File> factoryStyleFiles() const;
    void pushFactoryStylesToWeb();
    void pushRuntimeStateToWeb();

    void pushToWeb(const juce::String& js);

    std::string settingsFilePath() const;
    void loadSettings();
    void saveSettings();

    // State / message routing.
    cadenza::ApplicationState m_state;
    cadenza::BridgeRouter m_router { m_state };

    // Audio + MIDI + arranger.
    cadenza::audio::AudioEngine m_audio;
    cadenza::midi::MidiRouter   m_midi;
    cadenza::arranger::StyleEngine m_styleEngine { m_audio };

    // Song mode (chord-chart auto-stepping).
    cadenza::arranger::SongPlayer m_songPlayer;
    bool m_songModeActive = false;
    int  m_lastSongBar = -1;

    // Style Recorder. m_recordArmed is read on the MIDI thread (capture callback).
    cadenza::arranger::StyleRecorder m_recorder;
    std::atomic<bool> m_recordArmed { false };
    std::unique_ptr<juce::FileChooser> m_recSaveChooser;
    std::unique_ptr<cadenza::ui::StylePartEditorWindow> m_partEditor;

    // Live sections: one-shot returns / ending stops are sequenced inside the
    // StyleEngine (sample-tight); we only track the last Main as the return target.
    std::string m_currentMain = "mainA";

    // Last Main that OTS Link applied, so a fill returning to the same Main
    // doesn't re-trigger the OTS (which would undo manual voice tweaks).
    std::string m_lastLinkedMain;

    cadenza::audio::MixerModel m_mixer;
    int m_midiRescanTicks = 0;   // timer ticks since last MIDI hot-plug rescan

    // Persistent settings.
    std::unique_ptr<cadenza::settings::SettingsStore> m_settings;
    std::unique_ptr<juce::FileChooser> m_styleChooser;
    std::unique_ptr<juce::FileChooser> m_soundFontChooser;
    std::unique_ptr<juce::FileChooser> m_songChooser;
    std::unique_ptr<juce::FileChooser> m_pluginChooser;
    std::unique_ptr<juce::FileChooser> m_partPluginChooser;
    std::string m_learnCommand;        // command currently waiting for a MIDI-learn press

    std::unique_ptr<cadenza::ui::NativePanel> m_panel;
    juce::WebBrowserComponent m_webView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
