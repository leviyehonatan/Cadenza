#include "NativePanel.h"
#include "../Midi/GmInstruments.h"

namespace cadenza::ui
{
namespace
{
// Show the instrument / drum-kit picker for a mixer strip. Drum channel (10) gets
// GM drum kits; melodic channels get the 128 GM voices grouped by family.
constexpr int kMenuLoadPlugin = 900001;
constexpr int kMenuUseGm       = 900002;
constexpr int kMenuEditPlugin  = 900003;

// Keyboard split point (middle C). Must match MidiRouter's default splitNote so
// the on-screen tint lines up with where the host actually routes chord notes.
constexpr int kKeyboardSplitNote = 60;

void showInstrumentMenu(juce::Component* anchor, int channel,
                        std::function<void(int)> onPick,
                        std::function<void()> onLoadPlugin,
                        std::function<void()> onClearPlugin,
                        std::function<void()> onOpenEditor,
                        bool hasPlugin)
{
    juce::PopupMenu menu;
    menu.addItem(kMenuLoadPlugin, "Load VST3 Instrument...");
    menu.addItem(kMenuEditPlugin, "Open Plugin Editor...", hasPlugin);
    menu.addItem(kMenuUseGm, "Use GM SoundFont");
    menu.addSeparator();
    if (channel == 10) {
        static const std::pair<int, const char*> kits[] = {
            { 0, "Standard Kit" }, { 8, "Room Kit" }, { 16, "Power Kit" },
            { 24, "Electronic Kit" }, { 25, "TR-808 Kit" }, { 32, "Jazz Kit" },
            { 40, "Brush Kit" }, { 48, "Orchestra Kit" }
        };
        for (const auto& k : kits)
            menu.addItem(k.first + 1, k.second);            // id = program + 1
    } else {
        for (int fam = 0; fam < 16; ++fam) {
            juce::PopupMenu sub;
            for (int i = 0; i < 8; ++i) {
                const int prog = fam * 8 + i;
                sub.addItem(prog + 1, juce::String(prog) + "  " + cadenza::midi::gmInstrumentName(prog));
            }
            menu.addSubMenu(cadenza::midi::gmFamilyName(fam), sub);
        }
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(anchor),
                       [onPick, onLoadPlugin, onClearPlugin, onOpenEditor](int result) {
        if (result == kMenuLoadPlugin) { if (onLoadPlugin)  onLoadPlugin();  return; }
        if (result == kMenuEditPlugin) { if (onOpenEditor)  onOpenEditor();  return; }
        if (result == kMenuUseGm)      { if (onClearPlugin) onClearPlugin(); return; }
        if (result > 0) onPick(result - 1);
    });
}

// Voice picker for the Right 1/2/3 layers: VST3 options on top, then the 16 GM
// families of 8 programs each. Picking a GM program switches the layer back to GM.
constexpr int kRightLoadPlugin = 900101;
constexpr int kRightEditPlugin = 900103;

void showRightVoiceMenu(juce::Component* anchor, bool hasPlugin,
                        std::function<void(int)> onPickGm,
                        std::function<void()> onLoadPlugin,
                        std::function<void()> onOpenEditor)
{
    juce::PopupMenu menu;
    menu.addItem(kRightLoadPlugin, "Load VST3 Instrument...");
    menu.addItem(kRightEditPlugin, "Open Plugin Editor...", hasPlugin);
    menu.addSeparator();
    for (int fam = 0; fam < 16; ++fam) {
        juce::PopupMenu sub;
        for (int i = 0; i < 8; ++i) {
            const int prog = fam * 8 + i;
            sub.addItem(prog + 1, juce::String(prog) + "  " + cadenza::midi::gmInstrumentName(prog));
        }
        menu.addSubMenu(cadenza::midi::gmFamilyName(fam), sub);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(anchor),
                       [onPickGm, onLoadPlugin, onOpenEditor](int result) {
        if (result == kRightLoadPlugin) { if (onLoadPlugin) onLoadPlugin(); return; }
        if (result == kRightEditPlugin) { if (onOpenEditor) onOpenEditor(); return; }
        if (result > 0 && onPickGm) onPickGm(result - 1);   // GM voice
    });
}
}

NativePanel::NativePanel()
{
    auto styleCaption = [](juce::Label& l, const juce::String& text) {
        l.setText(text, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        l.setJustificationType(juce::Justification::centredLeft);
    };

    addAndMakeVisible(m_play);
    addAndMakeVisible(m_openStyle);
    addAndMakeVisible(m_openSf);
    addAndMakeVisible(m_openAudio);
    addAndMakeVisible(m_openMidi);
    addAndMakeVisible(m_webToggle);

    styleCaption(m_bpmCaption, "Tempo");
    addAndMakeVisible(m_bpmCaption);
    addAndMakeVisible(m_bpmDown);
    addAndMakeVisible(m_bpmUp);
    addAndMakeVisible(m_bpmVal);
    m_bpmTap.setTooltip("Tap a few beats to set the tempo");
    m_bpmTap.onClick = [this] {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (!m_tapTimesMs.empty() && now - m_tapTimesMs.back() > 2000.0)
            m_tapTimesMs.clear();                       // gap > 2s -> start over
        m_tapTimesMs.push_back(now);
        if (m_tapTimesMs.size() > 4)
            m_tapTimesMs.erase(m_tapTimesMs.begin());   // average the last few taps
        if (m_tapTimesMs.size() >= 2) {
            const double avgInterval = (m_tapTimesMs.back() - m_tapTimesMs.front())
                                       / static_cast<double>(m_tapTimesMs.size() - 1);
            if (avgInterval > 0.0) {
                const int bpm = juce::jlimit(40, 300, juce::roundToInt(60000.0 / avgInterval));
                m_bpmVal.setText(juce::String(bpm), juce::dontSendNotification);
                if (m_cb.onSetTempo) m_cb.onSetTempo(bpm);
            }
        }
    };
    addAndMakeVisible(m_bpmTap);
    m_bpmVal.setJustificationType(juce::Justification::centred);
    m_bpmVal.setColour(juce::Label::textColourId, juce::Colours::white);
    m_bpmVal.setText("120", juce::dontSendNotification);

    addAndMakeVisible(m_styleName);
    m_styleName.setText("(no style)", juce::dontSendNotification);
    m_styleName.setColour(juce::Label::textColourId, juce::Colours::white);
    m_styleName.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));

    addAndMakeVisible(m_chord);
    m_chord.setText("--", juce::dontSendNotification);
    m_chord.setColour(juce::Label::textColourId, juce::Colours::aqua);
    m_chord.setFont(juce::Font(juce::FontOptions(34.0f, juce::Font::bold)));
    m_chord.setJustificationType(juce::Justification::centred);

    styleCaption(m_transposeCaption, "Transpose");
    addAndMakeVisible(m_transposeCaption);
    addAndMakeVisible(m_transposeDown);
    addAndMakeVisible(m_transposeUp);
    addAndMakeVisible(m_transposeVal);
    m_transposeVal.setJustificationType(juce::Justification::centred);
    m_transposeVal.setColour(juce::Label::textColourId, juce::Colours::white);
    m_transposeVal.setText("0", juce::dontSendNotification);

    styleCaption(m_octaveCaption, "Octave (live melody)");
    addAndMakeVisible(m_octaveCaption);
    addAndMakeVisible(m_octaveDown);
    addAndMakeVisible(m_octaveUp);
    addAndMakeVisible(m_octaveVal);
    m_octaveVal.setJustificationType(juce::Justification::centred);
    m_octaveVal.setColour(juce::Label::textColourId, juce::Colours::white);
    m_octaveVal.setText("0", juce::dontSendNotification);

    addAndMakeVisible(m_arranger);
    addAndMakeVisible(m_chordMemory);
    addAndMakeVisible(m_syncroStop);
    addAndMakeVisible(m_autoFill);
    addAndMakeVisible(m_fingeredOnBass);
    m_autoFill.setTooltip("Pressing a Main button while playing inserts its fill-in first");

    styleCaption(m_sectionsCaption, "Sections");
    addAndMakeVisible(m_sectionsCaption);

    // On-screen keyboard (split-aware via injectNote on the host side). The left
    // chord zone (below the split) is tinted blue so the split is visible.
    m_splitNote = kKeyboardSplitNote;
    m_keyboard = std::make_unique<ChordSplitKeyboard>(
        m_keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard, m_splitNote);
    m_keyboard->setAvailableRange(36, 96);   // C2..C7
    m_keyboard->setLowestVisibleKey(36);     // show the chord zone too
    addAndMakeVisible(*m_keyboard);
    m_keyboardState.addListener(this);

    // Draggable split marker ("Chords | Melody") above the keys.
    m_splitBar = std::make_unique<SplitBar>();
    m_splitBar->setKeyboard(m_keyboard.get());
    m_splitBar->setSplitNote(m_splitNote);
    m_splitBar->onSplitChanged = [this](int note) {
        m_splitNote = note;
        m_keyboard->setSplitNote(note);
        if (m_cb.onSplitChanged) m_cb.onSplitChanged(note);
    };
    addAndMakeVisible(*m_splitBar);

    styleCaption(m_mixerCaption, "Mixer");
    addAndMakeVisible(m_mixerCaption);

    styleCaption(m_padsCaption, "Pads");
    addAndMakeVisible(m_padsCaption);
    for (int i = 0; i < 4; ++i) {
        auto pad = std::make_unique<juce::TextButton>("Pad " + juce::String(i + 1));
        const int index = i;
        pad->onClick = [this, index] { if (m_cb.onPad) m_cb.onPad(index); };
        addAndMakeVisible(*pad);
        m_pads.push_back(std::move(pad));
    }

    // Master EQ: three knobs (Low / Mid / High), -12..+12 dB.
    styleCaption(m_eqCaption, "Master EQ (dB)");
    addAndMakeVisible(m_eqCaption);
    auto setupEqKnob = [this](juce::Slider& s) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRange(-12.0, 12.0, 1.0);
        s.setValue(0.0, juce::dontSendNotification);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 16);
        s.onValueChange = [this] {
            if (m_cb.onEqChanged)
                m_cb.onEqChanged((int) m_eqLow.getValue(),
                                 (int) m_eqMid.getValue(),
                                 (int) m_eqHigh.getValue());
        };
        addAndMakeVisible(s);
    };
    setupEqKnob(m_eqLow); setupEqKnob(m_eqMid); setupEqKnob(m_eqHigh);
    auto eqLabel = [this](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    };
    eqLabel(m_eqLowCap, "Low"); eqLabel(m_eqMidCap, "Mid"); eqLabel(m_eqHighCap, "High");

    // Master compressor amount knob (0..100).
    m_comp.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_comp.setRange(0.0, 100.0, 1.0);
    m_comp.setValue(55.0, juce::dontSendNotification);
    m_comp.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 16);
    m_comp.onValueChange = [this] { if (m_cb.onCompChanged) m_cb.onCompChanged((int) m_comp.getValue()); };
    addAndMakeVisible(m_comp);
    eqLabel(m_compCap, "Comp");

    m_master.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_master.setRange(0.0, 127.0, 1.0);
    m_master.setValue(100.0, juce::dontSendNotification);
    m_master.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 16);
    m_master.onValueChange = [this] { if (m_cb.onMasterChanged) m_cb.onMasterChanged((int) m_master.getValue()); };
    addAndMakeVisible(m_master);
    eqLabel(m_masterCap, "Master");

    m_drums.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_drums.setRange(0.0, 127.0, 1.0);
    m_drums.setValue(100.0, juce::dontSendNotification);
    m_drums.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 16);
    m_drums.onValueChange = [this] { if (m_cb.onDrumsChanged) m_cb.onDrumsChanged((int) m_drums.getValue()); };
    addAndMakeVisible(m_drums);
    eqLabel(m_drumsCap, "Drums");

    m_reverb.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    m_reverb.setRange(0.0, 100.0, 1.0);
    m_reverb.setValue(80.0, juce::dontSendNotification);
    m_reverb.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 46, 16);
    m_reverb.onValueChange = [this] { if (m_cb.onReverbChanged) m_cb.onReverbChanged((int) m_reverb.getValue()); };
    addAndMakeVisible(m_reverb);
    eqLabel(m_reverbCap, "Reverb");

    // --- Right 1/2/3 layered right-hand voices ---
    styleCaption(m_rightCaption, "Right Voices");
    addAndMakeVisible(m_rightCaption);
    for (int i = 0; i < 3; ++i) {
        auto& s = m_rightVoices[static_cast<std::size_t>(i)];
        const int layer = i;

        s.enable = std::make_unique<juce::ToggleButton>("Right " + juce::String(i + 1));
        s.enable->setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);
        s.enable->onClick = [this, layer] {
            if (m_cb.onRightEnabled)
                m_cb.onRightEnabled(layer, m_rightVoices[static_cast<std::size_t>(layer)].enable->getToggleState());
        };
        addAndMakeVisible(*s.enable);

        s.instrument = std::make_unique<juce::TextButton>("Grand Piano");
        s.instrument->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333a47));
        s.instrument->onClick = [this, layer] {
            showRightVoiceMenu(m_rightVoices[static_cast<std::size_t>(layer)].instrument.get(),
                               m_rightHasPlugin[static_cast<std::size_t>(layer)],
                               [this, layer](int program) { if (m_cb.onRightInstrument) m_cb.onRightInstrument(layer, program); },
                               [this, layer] { if (m_cb.onRightLoadPlugin) m_cb.onRightLoadPlugin(layer); },
                               [this, layer] { if (m_cb.onRightOpenEditor) m_cb.onRightOpenEditor(layer); });
        };
        addAndMakeVisible(*s.instrument);

        s.volume = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
        s.volume->setRange(0.0, 127.0, 1.0);
        s.volume->setValue(100.0, juce::dontSendNotification);
        s.volume->onValueChange = [this, layer] {
            if (m_cb.onRightVolume)
                m_cb.onRightVolume(layer, static_cast<int>(m_rightVoices[static_cast<std::size_t>(layer)].volume->getValue()));
        };
        addAndMakeVisible(*s.volume);

        s.octDown = std::make_unique<juce::TextButton>("-");
        s.octDown->onClick = [this, layer] { if (m_cb.onRightOctave) m_cb.onRightOctave(layer, -1); };
        addAndMakeVisible(*s.octDown);

        s.octVal = std::make_unique<juce::Label>();
        s.octVal->setText("0", juce::dontSendNotification);
        s.octVal->setJustificationType(juce::Justification::centred);
        s.octVal->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(*s.octVal);

        s.octUp = std::make_unique<juce::TextButton>("+");
        s.octUp->onClick = [this, layer] { if (m_cb.onRightOctave) m_cb.onRightOctave(layer, +1); };
        addAndMakeVisible(*s.octUp);
    }

    // --- Registrations (one-button performance snapshots) ---
    styleCaption(m_regCaption, "Registrations");
    addAndMakeVisible(m_regCaption);
    m_regStore.setClickingTogglesState(true);
    m_regStore.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb04a4a));
    m_regStore.setTooltip("Arm, then click a slot to save the current setup");
    m_regStore.onClick = [this] { m_regStoreArmed = m_regStore.getToggleState(); };
    addAndMakeVisible(m_regStore);
    for (int i = 0; i < 4; ++i) {
        auto b = std::make_unique<juce::TextButton>(juce::String(i + 1));
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333a47));
        const int slot = i;
        b->onClick = [this, slot] {
            if (m_regStoreArmed) {
                if (m_cb.onStoreRegistration) m_cb.onStoreRegistration(slot);
                m_regStoreArmed = false;
                m_regStore.setToggleState(false, juce::dontSendNotification);
            } else if (m_cb.onRecallRegistration) {
                m_cb.onRecallRegistration(slot);
            }
        };
        addAndMakeVisible(*b);
        m_regButtons.push_back(std::move(b));
        m_regUsed.push_back(false);
    }

    // --- One Touch Settings (per-style right-hand voice presets) ---
    styleCaption(m_otsCaption, "One Touch Settings");
    addAndMakeVisible(m_otsCaption);
    m_otsLink.setTooltip("Recall OTS 1-4 automatically when Main A-D starts");
    m_otsLink.onClick = [this] { if (m_cb.setOtsLink) m_cb.setOtsLink(m_otsLink.getToggleState()); };
    addAndMakeVisible(m_otsLink);
    for (int i = 0; i < 4; ++i) {
        auto b = std::make_unique<juce::TextButton>("OTS " + juce::String(i + 1));
        b->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333a47));
        b->setEnabled(false);   // until a style with OTS data is loaded
        const int slot = i;
        b->onClick = [this, slot] { if (m_cb.onOts) m_cb.onOts(slot); };
        addAndMakeVisible(*b);
        m_otsButtons[static_cast<std::size_t>(i)] = std::move(b);
    }

    // --- wire control callbacks (message thread) ---
    m_play.onClick          = [this] { if (m_cb.togglePlay)    m_cb.togglePlay(); };
    m_fade.setTooltip("Fade the band out over a few seconds, then stop");
    m_fade.onClick          = [this] { if (m_cb.fadeOut)       m_cb.fadeOut(); };
    addAndMakeVisible(m_fade);
    m_openStyle.onClick     = [this] { if (m_cb.openStyle)     m_cb.openStyle(); };
    m_openSf.onClick        = [this] { if (m_cb.openSoundFont) m_cb.openSoundFont(); };
    m_openAudio.onClick     = [this] { if (m_cb.openAudioSettings) m_cb.openAudioSettings(); };
    m_openMidi.onClick      = [this] { if (m_cb.openMidiSettings)  m_cb.openMidiSettings(); };
    m_webToggle.onClick     = [this] { if (m_cb.toggleWeb)     m_cb.toggleWeb(); };
    m_transposeDown.onClick = [this] { if (m_cb.nudgeTranspose) m_cb.nudgeTranspose(-1); };
    m_transposeUp.onClick   = [this] { if (m_cb.nudgeTranspose) m_cb.nudgeTranspose(+1); };
    m_octaveDown.onClick    = [this] { if (m_cb.nudgeOctave)    m_cb.nudgeOctave(-1); };
    m_octaveUp.onClick      = [this] { if (m_cb.nudgeOctave)    m_cb.nudgeOctave(+1); };
    m_bpmDown.onClick       = [this] { if (m_cb.nudgeTempo)     m_cb.nudgeTempo(-1); };
    m_bpmUp.onClick         = [this] { if (m_cb.nudgeTempo)     m_cb.nudgeTempo(+1); };

    m_arranger.onClick       = [this] { if (m_cb.setArranger)       m_cb.setArranger(m_arranger.getToggleState()); };
    m_chordMemory.onClick    = [this] { if (m_cb.setChordMemory)    m_cb.setChordMemory(m_chordMemory.getToggleState()); };
    m_syncroStop.onClick     = [this] { if (m_cb.setSyncroStop)     m_cb.setSyncroStop(m_syncroStop.getToggleState()); };
    m_autoFill.onClick       = [this] { if (m_cb.setAutoFill)       m_cb.setAutoFill(m_autoFill.getToggleState()); };
    m_fingeredOnBass.onClick = [this] { if (m_cb.setFingeredOnBass) m_cb.setFingeredOnBass(m_fingeredOnBass.getToggleState()); };
}

NativePanel::~NativePanel()
{
    m_keyboardState.removeListener(this);
}

void NativePanel::handleNoteOn(juce::MidiKeyboardState*, int, int midiNoteNumber, float velocity)
{
    if (m_cb.onKeyboardNote)
        m_cb.onKeyboardNote(midiNoteNumber, juce::jlimit(1, 127, (int)(velocity * 127.0f)), true);
}

void NativePanel::handleNoteOff(juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    if (m_cb.onKeyboardNote)
        m_cb.onKeyboardNote(midiNoteNumber, 0, false);
}

void NativePanel::setBpm(int bpm)
{
    m_bpmVal.setText(juce::String(bpm), juce::dontSendNotification);
}

void NativePanel::setMixerChannels(const std::vector<std::pair<int, std::string>>& channelLabels)
{
    m_mixer.clear();
    for (const auto& [channel, label] : channelLabels) {
        MixerStrip strip;
        strip.channel = channel;

        strip.name = std::make_unique<juce::Label>();
        strip.name->setText(juce::String(label), juce::dontSendNotification);
        strip.name->setJustificationType(juce::Justification::centred);
        strip.name->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        strip.name->setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(*strip.name);

        const int ch = channel;

        strip.instrument = std::make_unique<juce::TextButton>("--");
        strip.instrument->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff333a47));
        strip.instrument->setTooltip("Choose the instrument for this part");
        strip.instrument->onClick = [this, ch] {
            juce::Component* anchor = nullptr;
            for (auto& s : m_mixer) if (s.channel == ch) { anchor = s.instrument.get(); break; }
            showInstrumentMenu(anchor, ch,
                [this, ch](int program) { if (m_cb.onMixerInstrument) m_cb.onMixerInstrument(ch, program); },
                [this, ch] { if (m_cb.onLoadInstrumentPlugin)  m_cb.onLoadInstrumentPlugin(ch); },
                [this, ch] { if (m_cb.onClearInstrumentPlugin) m_cb.onClearInstrumentPlugin(ch); },
                [this, ch] { if (m_cb.onOpenInstrumentEditor)  m_cb.onOpenInstrumentEditor(ch); },
                /*hasPlugin*/ true);
        };
        addAndMakeVisible(*strip.instrument);

        strip.volume = std::make_unique<juce::Slider>(juce::Slider::LinearVertical,
                                                      juce::Slider::NoTextBox);
        strip.volume->setRange(0.0, 127.0, 1.0);
        strip.volume->setValue(100.0, juce::dontSendNotification);
        strip.volume->onValueChange = [this, ch] {
            if (!m_cb.onMixerVolume) return;
            for (auto& s : m_mixer)
                if (s.channel == ch) { m_cb.onMixerVolume(ch, (int) s.volume->getValue()); break; }
        };
        addAndMakeVisible(*strip.volume);

        strip.mute = std::make_unique<juce::TextButton>("M");
        strip.mute->setClickingTogglesState(true);
        strip.mute->setColour(juce::TextButton::buttonOnColourId, juce::Colours::firebrick);
        strip.mute->onClick = [this, ch] {
            if (!m_cb.onMixerMute) return;
            for (auto& s : m_mixer)
                if (s.channel == ch) { m_cb.onMixerMute(ch, s.mute->getToggleState()); break; }
        };
        addAndMakeVisible(*strip.mute);

        strip.solo = std::make_unique<juce::TextButton>("S");
        strip.solo->setClickingTogglesState(true);
        strip.solo->setColour(juce::TextButton::buttonOnColourId, juce::Colours::orange);
        strip.solo->onClick = [this, ch] {
            if (!m_cb.onMixerSolo) return;
            for (auto& s : m_mixer)
                if (s.channel == ch) { m_cb.onMixerSolo(ch, s.solo->getToggleState()); break; }
        };
        addAndMakeVisible(*strip.solo);

        m_mixer.push_back(std::move(strip));
    }
    resized();
}

void NativePanel::updateMixerStrip(int channel, int volume, bool mute, bool solo)
{
    for (auto& s : m_mixer) {
        if (s.channel != channel) continue;
        s.volume->setValue(volume, juce::dontSendNotification);
        s.mute->setToggleState(mute, juce::dontSendNotification);
        s.solo->setToggleState(solo, juce::dontSendNotification);
        return;
    }
}

void NativePanel::setMixerInstrumentName(int channel, const juce::String& instrumentName)
{
    for (auto& s : m_mixer)
        if (s.channel == channel) { s.instrument->setButtonText(instrumentName); return; }
}

void NativePanel::setStyleName(const juce::String& name)
{
    m_styleName.setText(name.isEmpty() ? "(no style)" : name, juce::dontSendNotification);
}

void NativePanel::setSections(const std::vector<std::pair<std::string, std::string>>& idAndLabel)
{
    m_sections.clear();
    for (const auto& [id, label] : idAndLabel) {
        auto button = std::make_unique<juce::TextButton>(juce::String(label));
        const std::string sectionId = id;
        button->onClick = [this, sectionId] { if (m_cb.selectSection) m_cb.selectSection(sectionId); };
        addAndMakeVisible(*button);
        m_sections.push_back({ std::move(button), id });
    }
    refreshSectionHighlights();
    resized();
}

void NativePanel::setActiveSection(const juce::String& sectionId)
{
    m_activeSection = sectionId;
    refreshSectionHighlights();
}

void NativePanel::refreshSectionHighlights()
{
    for (auto& s : m_sections) {
        const bool active = (m_activeSection == juce::String(s.id));
        s.button->setColour(juce::TextButton::buttonColourId,
                            active ? juce::Colours::orange : juce::Colours::darkgrey);
        s.button->setColour(juce::TextButton::textColourOffId,
                            active ? juce::Colours::black : juce::Colours::white);
    }
}

void NativePanel::setChord(const juce::String& chord)
{
    m_chord.setText(chord.isEmpty() ? "--" : chord, juce::dontSendNotification);
}

void NativePanel::setTranspose(int semitones)
{
    m_transposeVal.setText((semitones > 0 ? "+" : "") + juce::String(semitones), juce::dontSendNotification);
}

void NativePanel::setOctave(int octaves)
{
    m_octaveVal.setText((octaves > 0 ? "+" : "") + juce::String(octaves), juce::dontSendNotification);
}

void NativePanel::setPlaying(bool playing)
{
    m_playing = playing;
    m_play.setButtonText(playing ? "Stop" : "Play");
    m_play.setColour(juce::TextButton::buttonColourId,
                     playing ? juce::Colours::firebrick : juce::Colours::darkgreen);
}

void NativePanel::setToggleStates(bool arranger, bool chordMemory, bool syncroStop, bool fingeredOnBass,
                                  bool autoFill)
{
    m_arranger.setToggleState(arranger, juce::dontSendNotification);
    m_chordMemory.setToggleState(chordMemory, juce::dontSendNotification);
    m_syncroStop.setToggleState(syncroStop, juce::dontSendNotification);
    m_fingeredOnBass.setToggleState(fingeredOnBass, juce::dontSendNotification);
    m_autoFill.setToggleState(autoFill, juce::dontSendNotification);
}

void NativePanel::setEqGains(int lowDb, int midDb, int highDb)
{
    m_eqLow.setValue(lowDb,  juce::dontSendNotification);
    m_eqMid.setValue(midDb,  juce::dontSendNotification);
    m_eqHigh.setValue(highDb, juce::dontSendNotification);
}

void NativePanel::setCompAmount(int percent)
{
    m_comp.setValue(percent, juce::dontSendNotification);
}

void NativePanel::setMasterVolume(int percent)
{
    m_master.setValue(percent, juce::dontSendNotification);
}

void NativePanel::setDrumsLevel(int volume)
{
    m_drums.setValue(volume, juce::dontSendNotification);
}

void NativePanel::setReverbAmount(int percent)
{
    m_reverb.setValue(percent, juce::dontSendNotification);
}

void NativePanel::setRightVoice(int layer, bool enabled, int program, int volume, int octave)
{
    if (layer < 0 || layer >= 3)
        return;
    auto& s = m_rightVoices[static_cast<std::size_t>(layer)];
    if (s.enable)     s.enable->setToggleState(enabled, juce::dontSendNotification);
    if (s.volume)     s.volume->setValue(volume, juce::dontSendNotification);
    if (s.octVal)     s.octVal->setText(juce::String(octave), juce::dontSendNotification);
    // Only show the GM name when no VST is loaded on this layer.
    if (!m_rightHasPlugin[static_cast<std::size_t>(layer)] && s.instrument)
        s.instrument->setButtonText(juce::String(cadenza::midi::gmInstrumentName(program)));
}

void NativePanel::setRightVoiceName(int layer, const juce::String& name, bool isPlugin)
{
    if (layer < 0 || layer >= 3)
        return;
    m_rightHasPlugin[static_cast<std::size_t>(layer)] = isPlugin;
    if (auto& b = m_rightVoices[static_cast<std::size_t>(layer)].instrument)
        b->setButtonText(name);
}

void NativePanel::setRegistrationUsed(int slot, bool used)
{
    if (slot < 0 || slot >= static_cast<int>(m_regButtons.size()))
        return;
    m_regUsed[static_cast<std::size_t>(slot)] = used;
    m_regButtons[static_cast<std::size_t>(slot)]->setColour(
        juce::TextButton::buttonColourId,
        used ? juce::Colour(0xff2f6b3a) : juce::Colour(0xff333a47));
}

void NativePanel::setOtsAvailable(const std::array<bool, 4>& available)
{
    for (std::size_t i = 0; i < m_otsButtons.size(); ++i)
        if (m_otsButtons[i])
            m_otsButtons[i]->setEnabled(available[i]);
}

void NativePanel::setOtsLinkEnabled(bool enabled)
{
    m_otsLink.setToggleState(enabled, juce::dontSendNotification);
}

void NativePanel::setSplitPoint(int midiNote)
{
    m_splitNote = midiNote;
    if (m_keyboard) m_keyboard->setSplitNote(midiNote);
    if (m_splitBar) m_splitBar->setSplitNote(midiNote);
}

void NativePanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1c1f26));
    g.setColour(juce::Colour(0xff2a2f3a));
    g.drawRect(getLocalBounds(), 1);
}

void NativePanel::resized()
{
    auto area = getLocalBounds().reduced(10);
    const int row = 34;
    const int gap = 6;

    // On-screen keyboard pinned to the bottom, full width, with the draggable
    // "Chords | Melody" split bar in a thin strip directly above the keys.
    if (m_keyboard) {
        auto kb = area.removeFromBottom(140).reduced(0, 4);
        auto bar = kb.removeFromTop(16);
        if (m_splitBar) m_splitBar->setBounds(bar);
        m_keyboard->setBounds(kb);
        area.removeFromBottom(gap);
    }

    // Mixer band above the keyboard.
    {
        auto mixerArea = area.removeFromBottom(170);
        m_mixerCaption.setBounds(mixerArea.removeFromTop(18));
        const int sw = 78, sgap = 6;
        int mx = mixerArea.getX();
        for (auto& s : m_mixer) {
            juce::Rectangle<int> col(mx, mixerArea.getY(), sw, mixerArea.getHeight());
            s.name->setBounds(col.removeFromTop(15));
            s.instrument->setBounds(col.removeFromTop(18).reduced(1, 0));
            auto btns = col.removeFromBottom(20);
            s.volume->setBounds(col.reduced(6, 2));
            s.mute->setBounds(btns.removeFromLeft(sw / 2 - 2));
            btns.removeFromLeft(4);
            s.solo->setBounds(btns);
            mx += sw + sgap;
        }
        area.removeFromBottom(gap);
    }

    // Row 1: transport + file + web toggle + tempo
    {
        auto r = area.removeFromTop(row);
        m_play.setBounds(r.removeFromLeft(80));        r.removeFromLeft(gap);
        m_fade.setBounds(r.removeFromLeft(56));        r.removeFromLeft(gap);
        m_openStyle.setBounds(r.removeFromLeft(100));  r.removeFromLeft(gap);
        m_openSf.setBounds(r.removeFromLeft(120));     r.removeFromLeft(gap);
        m_openAudio.setBounds(r.removeFromLeft(64));   r.removeFromLeft(gap);
        m_openMidi.setBounds(r.removeFromLeft(56));    r.removeFromLeft(gap);
        m_webToggle.setBounds(r.removeFromLeft(80));   r.removeFromLeft(gap * 3);
        m_bpmCaption.setBounds(r.removeFromLeft(56));
        m_bpmDown.setBounds(r.removeFromLeft(36));     r.removeFromLeft(gap);
        m_bpmVal.setBounds(r.removeFromLeft(56));      r.removeFromLeft(gap);
        m_bpmUp.setBounds(r.removeFromLeft(36));        r.removeFromLeft(gap * 2);
        m_bpmTap.setBounds(r.removeFromLeft(56));
    }
    area.removeFromTop(gap);

    // Row 2: style name (left) + chord (right, large)
    {
        auto r = area.removeFromTop(50);
        m_styleName.setBounds(r.removeFromLeft(juce::jmax(180, r.getWidth() / 2)).withTrimmedTop(14));
        m_chord.setBounds(r);
    }
    area.removeFromTop(gap);

    // Row 3: transpose + octave groups
    {
        auto r = area.removeFromTop(row);
        m_transposeCaption.setBounds(r.removeFromLeft(90));
        m_transposeDown.setBounds(r.removeFromLeft(36)); r.removeFromLeft(gap);
        m_transposeVal.setBounds(r.removeFromLeft(46));  r.removeFromLeft(gap);
        m_transposeUp.setBounds(r.removeFromLeft(36));   r.removeFromLeft(gap * 3);
        m_octaveCaption.setBounds(r.removeFromLeft(140));
        m_octaveDown.setBounds(r.removeFromLeft(36));    r.removeFromLeft(gap);
        m_octaveVal.setBounds(r.removeFromLeft(46));     r.removeFromLeft(gap);
        m_octaveUp.setBounds(r.removeFromLeft(36));
    }
    area.removeFromTop(gap);

    // Row 4: toggles in a horizontal strip
    {
        auto r = area.removeFromTop(28);
        const int tw = 150;
        m_arranger.setBounds(r.removeFromLeft(tw));
        m_chordMemory.setBounds(r.removeFromLeft(tw));
        m_syncroStop.setBounds(r.removeFromLeft(tw));
        m_autoFill.setBounds(r.removeFromLeft(110));
        m_fingeredOnBass.setBounds(r.removeFromLeft(tw));
    }
    area.removeFromTop(gap);

    // Master EQ row: caption + 3 labelled knobs.
    {
        auto r = area.removeFromTop(64);
        m_eqCaption.setBounds(r.removeFromLeft(110).withTrimmedTop(24));
        const int kw = 64;
        auto placeKnob = [&](juce::Slider& s, juce::Label& cap) {
            auto col = r.removeFromLeft(kw);
            cap.setBounds(col.removeFromTop(14));
            s.setBounds(col);
            r.removeFromLeft(6);
        };
        placeKnob(m_eqLow, m_eqLowCap);
        placeKnob(m_eqMid, m_eqMidCap);
        placeKnob(m_eqHigh, m_eqHighCap);
        r.removeFromLeft(14);
        placeKnob(m_comp, m_compCap);
        placeKnob(m_master, m_masterCap);
        placeKnob(m_drums, m_drumsCap);
        placeKnob(m_reverb, m_reverbCap);
    }
    area.removeFromTop(gap);

    // Right Voices row: three layer columns (enable + instrument / volume + octave).
    {
        m_rightCaption.setBounds(area.removeFromTop(18));
        auto r = area.removeFromTop(50);
        const int colGap = 10;
        const int colW = (r.getWidth() - 2 * colGap) / 3;
        for (int i = 0; i < 3; ++i) {
            auto& s = m_rightVoices[static_cast<std::size_t>(i)];
            auto col = r.removeFromLeft(colW);
            if (i < 2) r.removeFromLeft(colGap);

            auto top = col.removeFromTop(24);
            if (s.enable)     s.enable->setBounds(top.removeFromLeft(88));
            if (s.instrument) s.instrument->setBounds(top.reduced(2, 1));

            auto bot = col;
            auto oct = bot.removeFromRight(94);
            if (s.octDown) s.octDown->setBounds(oct.removeFromLeft(26));
            if (s.octVal)  s.octVal->setBounds(oct.removeFromLeft(40));
            if (s.octUp)   s.octUp->setBounds(oct.removeFromLeft(26));
            if (s.volume)  s.volume->setBounds(bot.reduced(2, 4));
        }
    }
    area.removeFromTop(gap);

    // Registrations row: Store toggle + numbered slot buttons.
    {
        m_regCaption.setBounds(area.removeFromTop(18));
        auto r = area.removeFromTop(28);
        m_regStore.setBounds(r.removeFromLeft(70));
        r.removeFromLeft(10);
        const int bw = 44, bgap = 6;
        for (auto& b : m_regButtons) { b->setBounds(r.removeFromLeft(bw)); r.removeFromLeft(bgap); }
    }
    area.removeFromTop(gap);

    // One Touch Settings row: Link toggle + OTS 1..4 buttons.
    {
        m_otsCaption.setBounds(area.removeFromTop(18));
        auto r = area.removeFromTop(28);
        m_otsLink.setBounds(r.removeFromLeft(80));
        r.removeFromLeft(10);
        const int bw = 56, bgap = 6;
        for (auto& b : m_otsButtons) {
            if (b) b->setBounds(r.removeFromLeft(bw));
            r.removeFromLeft(bgap);
        }
    }
    area.removeFromTop(gap);

    // Pads row.
    {
        m_padsCaption.setBounds(area.removeFromTop(18));
        auto r = area.removeFromTop(32);
        const int pw = 90, pgap = 6;
        int px = r.getX();
        for (auto& p : m_pads) { p->setBounds(px, r.getY(), pw, r.getHeight()); px += pw + pgap; }
    }
    area.removeFromTop(gap);

    // Sections caption + flowing buttons fill the rest.
    m_sectionsCaption.setBounds(area.removeFromTop(22));
    const int bw = 92, bh = 30, bgap = 6;
    int x = area.getX(), y = area.getY();
    for (auto& s : m_sections) {
        if (x + bw > area.getRight()) { x = area.getX(); y += bh + bgap; }
        s.button->setBounds(x, y, bw, bh);
        x += bw + bgap;
    }
}
}
