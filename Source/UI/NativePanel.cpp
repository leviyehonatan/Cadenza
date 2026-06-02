#include "NativePanel.h"
#include "../Midi/GmInstruments.h"

namespace cadenza::ui
{
namespace
{
// Show the instrument / drum-kit picker for a mixer strip. Drum channel (10) gets
// GM drum kits; melodic channels get the 128 GM voices grouped by family.
void showInstrumentMenu(juce::Component* anchor, int channel, std::function<void(int)> onPick)
{
    juce::PopupMenu menu;
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
                       [onPick](int result) { if (result > 0) onPick(result - 1); });
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
    addAndMakeVisible(m_webToggle);

    styleCaption(m_bpmCaption, "Tempo");
    addAndMakeVisible(m_bpmCaption);
    addAndMakeVisible(m_bpmDown);
    addAndMakeVisible(m_bpmUp);
    addAndMakeVisible(m_bpmVal);
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
    addAndMakeVisible(m_fingeredOnBass);

    styleCaption(m_sectionsCaption, "Sections");
    addAndMakeVisible(m_sectionsCaption);

    // On-screen keyboard (split-aware via injectNote on the host side).
    m_keyboard = std::make_unique<juce::MidiKeyboardComponent>(
        m_keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard);
    m_keyboard->setAvailableRange(36, 96);   // C2..C7
    m_keyboard->setLowestVisibleKey(48);
    addAndMakeVisible(*m_keyboard);
    m_keyboardState.addListener(this);

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

    // --- wire control callbacks (message thread) ---
    m_play.onClick          = [this] { if (m_cb.togglePlay)    m_cb.togglePlay(); };
    m_openStyle.onClick     = [this] { if (m_cb.openStyle)     m_cb.openStyle(); };
    m_openSf.onClick        = [this] { if (m_cb.openSoundFont) m_cb.openSoundFont(); };
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
            showInstrumentMenu(anchor, ch, [this, ch](int program) {
                if (m_cb.onMixerInstrument) m_cb.onMixerInstrument(ch, program);
            });
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

void NativePanel::setToggleStates(bool arranger, bool chordMemory, bool syncroStop, bool fingeredOnBass)
{
    m_arranger.setToggleState(arranger, juce::dontSendNotification);
    m_chordMemory.setToggleState(chordMemory, juce::dontSendNotification);
    m_syncroStop.setToggleState(syncroStop, juce::dontSendNotification);
    m_fingeredOnBass.setToggleState(fingeredOnBass, juce::dontSendNotification);
}

void NativePanel::setEqGains(int lowDb, int midDb, int highDb)
{
    m_eqLow.setValue(lowDb,  juce::dontSendNotification);
    m_eqMid.setValue(midDb,  juce::dontSendNotification);
    m_eqHigh.setValue(highDb, juce::dontSendNotification);
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

    // On-screen keyboard pinned to the bottom, full width.
    if (m_keyboard) {
        auto kb = area.removeFromBottom(140);
        m_keyboard->setBounds(kb.reduced(0, 4));
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
        m_openStyle.setBounds(r.removeFromLeft(100));  r.removeFromLeft(gap);
        m_openSf.setBounds(r.removeFromLeft(120));     r.removeFromLeft(gap);
        m_webToggle.setBounds(r.removeFromLeft(80));   r.removeFromLeft(gap * 3);
        m_bpmCaption.setBounds(r.removeFromLeft(56));
        m_bpmDown.setBounds(r.removeFromLeft(36));     r.removeFromLeft(gap);
        m_bpmVal.setBounds(r.removeFromLeft(56));      r.removeFromLeft(gap);
        m_bpmUp.setBounds(r.removeFromLeft(36));
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
