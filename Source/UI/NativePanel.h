// NativePanel — a minimal native JUCE control surface for Cadenza's core live
// arranger controls. It does NOT use the JS bridge: every control invokes a
// std::function callback that MainComponent wires straight to the audio / MIDI /
// style engines. The WebView remains available but secondary; this panel is the
// source of truth for live performance.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cadenza::ui
{
class NativePanel : public juce::Component,
                    private juce::MidiKeyboardState::Listener
{
public:
    struct Callbacks
    {
        std::function<void()> togglePlay;
        std::function<void()> openStyle;
        std::function<void()> openSoundFont;
        std::function<void()> openAudioSettings;
        std::function<void()> toggleWeb;
        std::function<void(int)> nudgeTranspose;     // delta -1 / +1
        std::function<void(int)> nudgeOctave;         // delta -1 / +1
        std::function<void(int)> nudgeTempo;          // delta in BPM (e.g. -1 / +1)
        std::function<void(bool)> setArranger;
        std::function<void(bool)> setChordMemory;
        std::function<void(bool)> setSyncroStop;
        std::function<void(bool)> setFingeredOnBass;
        std::function<void(const std::string&)> selectSection;
        std::function<void(int, int, bool)> onKeyboardNote;  // note, velocity, isOn
        std::function<void(int, int)> onMixerVolume;          // channel, volume 0..127
        std::function<void(int, bool)> onMixerMute;           // channel, muted
        std::function<void(int, bool)> onMixerSolo;           // channel, soloed
        std::function<void(int, int)> onMixerInstrument;      // channel, GM program 0..127
        std::function<void(int)> onLoadInstrumentPlugin;      // channel -> choose+load a VST3 instrument
        std::function<void(int)> onClearInstrumentPlugin;     // channel -> back to GM SoundFont
        std::function<void(int)> onOpenInstrumentEditor;      // channel -> show the VST3 plugin's GUI
        std::function<void(int)> onPad;                       // pad index 0..3
        std::function<void(int, int, int)> onEqChanged;       // low, mid, high gain in dB
        std::function<void(int)> onCompChanged;               // master compressor amount 0..100
    };

    NativePanel();
    ~NativePanel() override;

    void setCallbacks(Callbacks cb) { m_cb = std::move(cb); }

    // State setters — must be called on the message thread.
    void setStyleName(const juce::String& name);
    void setSections(const std::vector<std::pair<std::string, std::string>>& idAndLabel);
    void setActiveSection(const juce::String& sectionId);
    void setChord(const juce::String& chord);
    void setTranspose(int semitones);
    void setOctave(int octaves);
    void setBpm(int bpm);
    void setPlaying(bool playing);

    // Mixer: rebuild strips for (channel, label) pairs, then sync a strip's state.
    void setMixerChannels(const std::vector<std::pair<int, std::string>>& channelLabels);
    void updateMixerStrip(int channel, int volume, bool mute, bool solo);
    void setMixerInstrumentName(int channel, const juce::String& instrumentName);
    void setToggleStates(bool arranger, bool chordMemory, bool syncroStop, bool fingeredOnBass);
    void setEqGains(int lowDb, int midDb, int highDb);   // init the EQ knobs (no callback)
    void setCompAmount(int percent);                     // init the Comp knob (no callback)

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void refreshSectionHighlights();

    // juce::MidiKeyboardState::Listener
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;

    Callbacks m_cb;

    juce::TextButton m_play       { "Play" };
    juce::TextButton m_openStyle  { "Open Style" };
    juce::TextButton m_openSf     { "Open SoundFont" };
    juce::TextButton m_openAudio  { "Audio" };
    juce::TextButton m_webToggle  { "Web UI" };

    juce::Label      m_bpmCaption;
    juce::TextButton m_bpmDown { "-" };
    juce::TextButton m_bpmUp   { "+" };
    juce::Label      m_bpmVal;

    juce::Label m_styleName;
    juce::Label m_chord;

    juce::Label      m_transposeCaption;
    juce::TextButton m_transposeDown { "-" };
    juce::TextButton m_transposeUp   { "+" };
    juce::Label      m_transposeVal;

    juce::Label      m_octaveCaption;
    juce::TextButton m_octaveDown { "-" };
    juce::TextButton m_octaveUp   { "+" };
    juce::Label      m_octaveVal;

    juce::ToggleButton m_arranger       { "Arranger" };
    juce::ToggleButton m_chordMemory    { "Chord Memory" };
    juce::ToggleButton m_syncroStop     { "Syncro Stop" };
    juce::ToggleButton m_fingeredOnBass { "Chord on Bass" };

    juce::Label m_sectionsCaption;
    struct SectionButton
    {
        std::unique_ptr<juce::TextButton> button;
        std::string id;
    };
    std::vector<SectionButton> m_sections;
    juce::String m_activeSection;
    bool m_playing = false;

    juce::MidiKeyboardState m_keyboardState;
    std::unique_ptr<juce::MidiKeyboardComponent> m_keyboard;

    juce::Label m_mixerCaption;
    struct MixerStrip
    {
        int channel = 0;
        std::unique_ptr<juce::Label>      name;
        std::unique_ptr<juce::TextButton> instrument;   // opens GM instrument / drum-kit picker
        std::unique_ptr<juce::Slider>     volume;
        std::unique_ptr<juce::TextButton> mute;
        std::unique_ptr<juce::TextButton> solo;
    };
    std::vector<MixerStrip> m_mixer;

    juce::Label m_padsCaption;
    std::vector<std::unique_ptr<juce::TextButton>> m_pads;

    juce::Label  m_eqCaption;
    juce::Label  m_eqLowCap, m_eqMidCap, m_eqHighCap, m_compCap;
    juce::Slider m_eqLow, m_eqMid, m_eqHigh, m_comp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NativePanel)
};
}
