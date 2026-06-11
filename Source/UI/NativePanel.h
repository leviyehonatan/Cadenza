// NativePanel — a minimal native JUCE control surface for Cadenza's core live
// arranger controls. It does NOT use the JS bridge: every control invokes a
// std::function callback that MainComponent wires straight to the audio / MIDI /
// style engines. The WebView remains available but secondary; this panel is the
// source of truth for live performance.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cadenza::ui
{
// On-screen keyboard that tints the left-hand chord zone (notes below the split
// point) so the player can see where chords end and the melody begins.
class ChordSplitKeyboard final : public juce::MidiKeyboardComponent
{
public:
    ChordSplitKeyboard(juce::MidiKeyboardState& state, Orientation orientation, int splitNote)
        : juce::MidiKeyboardComponent(state, orientation), m_split(splitNote) {}

    void setSplitNote(int n) { m_split = n; repaint(); }
    int  splitNote() const   { return m_split; }

    void drawWhiteNote(int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                       bool isDown, bool isOver, juce::Colour line, juce::Colour text) override
    {
        juce::MidiKeyboardComponent::drawWhiteNote(midiNoteNumber, g, area, isDown, isOver, line, text);
        if (midiNoteNumber < m_split) { g.setColour(juce::Colour(0x33409cff)); g.fillRect(area); }
    }
    void drawBlackNote(int midiNoteNumber, juce::Graphics& g, juce::Rectangle<float> area,
                       bool isDown, bool isOver, juce::Colour fill) override
    {
        juce::MidiKeyboardComponent::drawBlackNote(midiNoteNumber, g, area, isDown, isOver, fill);
        if (midiNoteNumber < m_split) { g.setColour(juce::Colour(0x55409cff)); g.fillRect(area); }
    }

private:
    int m_split;
};

// A thin strip drawn above the keyboard: "Chords" / "Melody" labels and a
// draggable triangle marker at the split point. Dragging snaps to white keys.
class SplitBar final : public juce::Component
{
public:
    void setKeyboard(juce::MidiKeyboardComponent* kb) { m_kb = kb; }
    void setSplitNote(int n) { m_split = n; repaint(); }
    int  splitNote() const   { return m_split; }

    std::function<void(int)> onSplitChanged;   // fired with the new split note

    void paint(juce::Graphics& g) override
    {
        const float x = markerX();
        auto b = getLocalBounds();
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.setColour(juce::Colours::skyblue.withAlpha(0.9f));
        g.drawText("Chords", b.withWidth(juce::jmax(0, (int) x)).reduced(4, 0),
                   juce::Justification::centredRight, false);
        g.setColour(juce::Colours::lightgrey);
        g.drawText("Melody", b.withLeft((int) x).reduced(4, 0),
                   juce::Justification::centredLeft, false);
        juce::Path tri;
        const float w = 6.0f, h = (float) getHeight() - 1.0f;
        tri.addTriangle(x - w, 0.0f, x + w, 0.0f, x, h);
        g.setColour(juce::Colours::white);
        g.fillPath(tri);
    }

    void mouseDown(const juce::MouseEvent& e) override { setFromX((float) e.x); }
    void mouseDrag(const juce::MouseEvent& e) override { setFromX((float) e.x); }

private:
    float markerX() const { return m_kb ? m_kb->getRectangleForKey(m_split).getX() : 0.0f; }

    void setFromX(float x)
    {
        if (m_kb == nullptr) return;
        int best = m_split; float bestDist = 1.0e9f;
        for (int n = 24; n <= 108; ++n) {
            const int pc = n % 12;
            const bool white = (pc==0||pc==2||pc==4||pc==5||pc==7||pc==9||pc==11);
            if (!white) continue;
            const float left = m_kb->getRectangleForKey(n).getX();
            const float d = left > x ? left - x : x - left;
            if (d < bestDist) { bestDist = d; best = n; }
        }
        if (best != m_split) {
            m_split = best;
            repaint();
            if (onSplitChanged) onSplitChanged(best);
        }
    }

    juce::MidiKeyboardComponent* m_kb = nullptr;
    int m_split = 60;
};

class NativePanel : public juce::Component,
                    private juce::MidiKeyboardState::Listener
{
public:
    struct Callbacks
    {
        std::function<void()> togglePlay;
        std::function<void()> fadeOut;                // fade the master out, then stop
        std::function<void()> openStyle;
        std::function<void()> openSoundFont;
        std::function<void()> openAudioSettings;
        std::function<void()> openMidiSettings;
        std::function<void()> toggleWeb;
        std::function<void(int)> nudgeTranspose;     // delta -1 / +1
        std::function<void(int)> nudgeOctave;         // delta -1 / +1
        std::function<void(int)> nudgeTempo;          // delta in BPM (e.g. -1 / +1)
        std::function<void(int)> onSetTempo;          // absolute BPM (e.g. from tap tempo)
        std::function<void(bool)> setArranger;
        std::function<void(bool)> setChordMemory;
        std::function<void(bool)> setSyncroStop;
        std::function<void(bool)> setAutoFill;
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
        std::function<void(int)> onMasterChanged;             // master output volume 0..127
        std::function<void(int)> onDrumsChanged;              // drum channel volume 0..127
        std::function<void(int)> onReverbChanged;             // master reverb amount 0..100
        std::function<void(int)> onSplitChanged;              // keyboard split MIDI note
        // Right 1/2/3 layered right-hand voices (layer 0..2).
        std::function<void(int, bool)> onRightEnabled;        // layer, on/off
        std::function<void(int, int)>  onRightInstrument;     // layer, GM program 0..127
        std::function<void(int, int)>  onRightVolume;         // layer, volume 0..127
        std::function<void(int, int)>  onRightOctave;         // layer, octave delta -1/+1
        std::function<void(int)>       onRightLoadPlugin;     // layer -> choose+load a VST3
        std::function<void(int)>       onRightOpenEditor;     // layer -> show the VST3 editor
        std::function<void(int)> onRecallRegistration;        // slot -> recall
        std::function<void(int)> onStoreRegistration;         // slot -> store current setup
        std::function<void(int)>  onOts;                      // OTS slot 0..3 -> recall
        std::function<void(bool)> setOtsLink;                 // OTS Link toggle changed
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
    void setToggleStates(bool arranger, bool chordMemory, bool syncroStop, bool fingeredOnBass,
                         bool autoFill);
    void setEqGains(int lowDb, int midDb, int highDb);   // init the EQ knobs (no callback)
    void setCompAmount(int percent);                     // init the Comp knob (no callback)
    void setMasterVolume(int percent);                   // init the Master knob (no callback)
    void setDrumsLevel(int volume);                      // init the Drums knob (no callback)
    void setReverbAmount(int percent);                   // init the Reverb knob (no callback)
    void setSplitPoint(int midiNote);                    // init the split marker (no callback)
    // Init a Right 1/2/3 voice strip (no callback): layer 0..2.
    void setRightVoice(int layer, bool enabled, int program, int volume, int octave);
    // Show a layer's loaded VST3 name (isPlugin=true) or a GM voice name.
    void setRightVoiceName(int layer, const juce::String& name, bool isPlugin);
    // Mark a registration slot as used (saved) so its button is highlighted.
    void setRegistrationUsed(int slot, bool used);
    // Enable/dim the four OTS buttons to match the loaded style's OTS slots.
    void setOtsAvailable(const std::array<bool, 4>& available);
    // Init the OTS Link toggle (no callback).
    void setOtsLinkEnabled(bool enabled);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void refreshSectionHighlights();

    // juce::MidiKeyboardState::Listener
    void handleNoteOn (juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;

    Callbacks m_cb;

    juce::TextButton m_play       { "Play" };
    juce::TextButton m_fade       { "Fade" };
    juce::TextButton m_openStyle  { "Open Style" };
    juce::TextButton m_openSf     { "Open SoundFont" };
    juce::TextButton m_openAudio  { "Audio" };
    juce::TextButton m_openMidi   { "MIDI" };
    juce::TextButton m_webToggle  { "Web UI" };

    juce::Label      m_bpmCaption;
    juce::TextButton m_bpmDown { "-" };
    juce::TextButton m_bpmUp   { "+" };
    juce::TextButton m_bpmTap  { "Tap" };
    juce::Label      m_bpmVal;
    std::vector<double> m_tapTimesMs;   // recent tap timestamps for tempo detection

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
    juce::ToggleButton m_autoFill       { "Auto Fill" };
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
    std::unique_ptr<ChordSplitKeyboard> m_keyboard;
    std::unique_ptr<SplitBar>           m_splitBar;
    int m_splitNote = 60;

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
    juce::Label  m_eqLowCap, m_eqMidCap, m_eqHighCap, m_compCap, m_masterCap, m_drumsCap, m_reverbCap;
    juce::Slider m_eqLow, m_eqMid, m_eqHigh, m_comp, m_master, m_drums, m_reverb;

    // Right 1/2/3 layered right-hand voices.
    juce::Label m_rightCaption;
    struct RightVoiceStrip
    {
        std::unique_ptr<juce::ToggleButton> enable;     // "Right N"
        std::unique_ptr<juce::TextButton>   instrument; // GM voice picker
        std::unique_ptr<juce::Slider>       volume;     // 0..127
        std::unique_ptr<juce::TextButton>   octDown, octUp;
        std::unique_ptr<juce::Label>        octVal;
    };
    std::array<RightVoiceStrip, 3> m_rightVoices;
    std::array<bool, 3> m_rightHasPlugin { false, false, false };

    // Registrations (one-button performance snapshots).
    juce::Label        m_regCaption;
    juce::TextButton   m_regStore { "Store" };   // armed = next slot click saves
    bool               m_regStoreArmed = false;
    std::vector<std::unique_ptr<juce::TextButton>> m_regButtons;
    std::vector<bool>  m_regUsed;

    // One Touch Settings (per-style right-hand voice presets).
    juce::Label        m_otsCaption;
    juce::ToggleButton m_otsLink { "OTS Link" };
    std::array<std::unique_ptr<juce::TextButton>, 4> m_otsButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NativePanel)
};
}
