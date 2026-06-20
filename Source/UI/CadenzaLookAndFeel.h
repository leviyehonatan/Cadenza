// CadenzaLookAndFeel — the app-wide "sleek dark DAW" theme: near-black charcoal
// surfaces, a single warm amber accent, flat rounded controls, custom rotary
// knobs and vertical faders. Installed as the default LookAndFeel in Main.cpp so
// it cascades to the control panel, the Part Editor, the piano roll and all
// popup menus. It deliberately does NOT override getLabelFont so per-label fonts
// (the big chord display, the style name) keep their sizes.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cadenza::ui
{
class CadenzaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CadenzaLookAndFeel();

    // Shared palette — taken from the "Cadenza Arranger" black-and-gold design.
    static juce::Colour background()  { return juce::Colour(0xff0a0b0d); }  // deepest body
    static juce::Colour backgroundHi(){ return juce::Colour(0xff1b1d22); }  // body gradient top
    static juce::Colour cardTop()     { return juce::Colour(0xff1c1f27); }  // card gradient top
    static juce::Colour cardBot()     { return juce::Colour(0xff0e1015); }  // card gradient bottom
    static juce::Colour panel()       { return juce::Colour(0xff1c1f27); }
    static juce::Colour panelRaised() { return juce::Colour(0xff23262f); }
    static juce::Colour outline()     { return juce::Colour(0xff34373d); }
    static juce::Colour textMain()    { return juce::Colour(0xffd8dce3); }
    static juce::Colour textDim()     { return juce::Colour(0xff8a8f99); }
    static juce::Colour cream()       { return juce::Colour(0xfffcf9f1); }  // bright readouts / logo
    static juce::Colour accent()      { return juce::Colour(0xffc9a96a); }  // gold
    static juce::Colour goldBright()  { return juce::Colour(0xfff0d488); }
    static juce::Colour goldDeep()    { return juce::Colour(0xffb8923e); }
    static juce::Colour led()         { return juce::Colour(0xffff6a00); }  // orange LED glow

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;

    void drawTickBox(juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                     bool ticked, bool isEnabled, bool isMouseOver, bool isButtonDown) override;

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                          float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle,
                          juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown, int buttonX,
                      int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
};
}
