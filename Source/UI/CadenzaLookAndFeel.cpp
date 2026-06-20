#include "CadenzaLookAndFeel.h"

namespace cadenza::ui
{
CadenzaLookAndFeel::CadenzaLookAndFeel()
{
    using juce::Colour;

    // Window / generic surfaces.
    setColour(juce::ResizableWindow::backgroundColourId, background());
    setColour(juce::DocumentWindow::textColourId,        textMain());

    // Text buttons — flat raised surface, amber when toggled on.
    setColour(juce::TextButton::buttonColourId,   panelRaised());
    setColour(juce::TextButton::buttonOnColourId, accent());
    setColour(juce::TextButton::textColourOffId,  textMain());
    setColour(juce::TextButton::textColourOnId,   background());

    // Toggle buttons.
    setColour(juce::ToggleButton::textColourId,   textMain());
    setColour(juce::ToggleButton::tickColourId,   accent());
    setColour(juce::ToggleButton::tickDisabledColourId, outline());

    // Labels.
    setColour(juce::Label::textColourId, textMain());

    // Sliders.
    setColour(juce::Slider::rotarySliderFillColourId,    accent());
    setColour(juce::Slider::rotarySliderOutlineColourId, outline());
    setColour(juce::Slider::thumbColourId,               accent());
    setColour(juce::Slider::trackColourId,               accent());
    setColour(juce::Slider::backgroundColourId,          outline());
    setColour(juce::Slider::textBoxTextColourId,         textMain());
    setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);

    // Combo boxes.
    setColour(juce::ComboBox::backgroundColourId, panelRaised());
    setColour(juce::ComboBox::textColourId,       textMain());
    setColour(juce::ComboBox::outlineColourId,    outline());
    setColour(juce::ComboBox::arrowColourId,      textDim());
    setColour(juce::ComboBox::buttonColourId,     panelRaised());

    // Popup menus (Patterns menu, instrument pickers, etc.).
    setColour(juce::PopupMenu::backgroundColourId,            panel());
    setColour(juce::PopupMenu::textColourId,                  textMain());
    setColour(juce::PopupMenu::headerTextColourId,            textDim());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent());
    setColour(juce::PopupMenu::highlightedTextColourId,       background());

    // Text editors / scrollbars.
    setColour(juce::TextEditor::backgroundColourId, panel());
    setColour(juce::TextEditor::textColourId,       textMain());
    setColour(juce::TextEditor::outlineColourId,    outline());
    setColour(juce::TextEditor::highlightColourId,  accent().withAlpha(0.3f));
    setColour(juce::ScrollBar::thumbColourId,       outline().brighter(0.2f));

    // Tooltips.
    setColour(juce::TooltipWindow::backgroundColourId, panel());
    setColour(juce::TooltipWindow::textColourId,       textMain());
    setColour(juce::TooltipWindow::outlineColourId,    outline());
}

juce::Font CadenzaLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return juce::Font(juce::FontOptions(juce::jmin(14.5f, buttonHeight * 0.5f)));
}

juce::Font CadenzaLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(juce::FontOptions(13.5f));
}

juce::Font CadenzaLookAndFeel::getPopupMenuFont()
{
    return juce::Font(juce::FontOptions(14.5f));
}

void CadenzaLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                              const juce::Colour& backgroundColour,
                                              bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const float corner = 6.0f;
    const bool on = button.getToggleState();
    const bool darkSurface = backgroundColour.getPerceivedBrightness() < 0.32f;

    if (on && backgroundColour == accent())
    {
        // Active = brushed-gold fill with a warm glow.
        g.setGradientFill(juce::ColourGradient(goldBright(), bounds.getX(), bounds.getY(),
                                               goldDeep(), bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(goldBright().withAlpha(0.9f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);
    }
    else if (darkSurface)
    {
        // Normal control = the design's card gradient.
        auto top = highlighted ? cardTop().brighter(0.10f) : cardTop();
        auto bot = down ? cardBot().darker(0.2f) : cardBot();
        g.setGradientFill(juce::ColourGradient(top, bounds.getX(), bounds.getY(),
                                               bot, bounds.getX(), bounds.getBottom(), false));
        g.fillRoundedRectangle(bounds, corner);
        // Inset top highlight + border (gold-tinted on hover).
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawLine(bounds.getX() + corner, bounds.getY() + 1.0f,
                   bounds.getRight() - corner, bounds.getY() + 1.0f, 1.0f);
        g.setColour(highlighted ? accent().withAlpha(0.45f) : juce::Colours::white.withAlpha(0.07f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);
    }
    else
    {
        // A coloured semantic button (Play green, Mute red, Solo orange…).
        auto base = backgroundColour;
        if (down)             base = base.darker(0.18f);
        else if (highlighted) base = base.brighter(0.14f);
        g.setColour(base);
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(base.brighter(0.3f).withAlpha(0.6f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);
    }
}

void CadenzaLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                        bool /*highlighted*/, bool /*down*/)
{
    const bool on = button.getToggleState();
    const auto bg = on ? button.findColour(juce::TextButton::buttonOnColourId)
                       : button.findColour(juce::TextButton::buttonColourId);

    // Pick a readable text colour from the background's brightness so amber/orange
    // on-states get dark text and dark/red surfaces get light text.
    juce::Colour txt = bg.getPerceivedBrightness() > 0.55f ? background() : textMain();
    if (! button.isEnabled()) txt = txt.withAlpha(0.4f);

    g.setColour(txt);
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(7, 0),
                     juce::Justification::centred, 1, 0.9f);
}

void CadenzaLookAndFeel::drawTickBox(juce::Graphics& g, juce::Component& /*c*/, float x, float y,
                                     float w, float h, bool ticked, bool isEnabled,
                                     bool /*isMouseOver*/, bool /*isButtonDown*/)
{
    juce::Rectangle<float> box(x, y, w, h);
    box = box.withSizeKeepingCentre(juce::jmin(w, h) * 0.86f, juce::jmin(w, h) * 0.86f);
    const float corner = 4.0f;

    if (ticked)
    {
        g.setGradientFill(juce::ColourGradient(goldBright(), box.getX(), box.getY(),
                                               goldDeep(), box.getX(), box.getBottom(), false));
        g.fillRoundedRectangle(box, corner);
        g.setColour(goldBright());
    }
    else
    {
        g.setColour(panelRaised());
        g.fillRoundedRectangle(box, corner);
        g.setColour(outline().withAlpha(isEnabled ? 0.9f : 0.4f));
    }
    g.drawRoundedRectangle(box, corner, 1.2f);

    if (ticked)
    {
        juce::Path tick;
        const auto r = box.reduced(box.getWidth() * 0.26f);
        tick.startNewSubPath(r.getX(), r.getCentreY());
        tick.lineTo(r.getCentreX() - r.getWidth() * 0.05f, r.getBottom());
        tick.lineTo(r.getRight(), r.getY());
        g.setColour(background());
        g.strokePath(tick, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
}

void CadenzaLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float rotaryStartAngle,
                                          float rotaryEndAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const float lineW = juce::jmax(2.5f, radius * 0.18f);
    const float arcR = radius - lineW * 0.6f;

    // Track.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(outline());
    g.strokePath(track, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    // Value arc — brushed-gold gradient.
    if (angle > rotaryStartAngle + 0.01f)
    {
        juce::Path val;
        val.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, angle, true);
        g.setGradientFill(juce::ColourGradient(goldBright(), bounds.getX(), bounds.getY(),
                                               goldDeep(), bounds.getX(), bounds.getBottom(), false));
        g.strokePath(val, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    // Inner brass knob face — a domed dark knob with a soft top sheen + brass rim.
    const float knobR = arcR - lineW;
    auto faceRect = juce::Rectangle<float>(knobR * 2.0f, knobR * 2.0f).withCentre(centre);
    g.setGradientFill(juce::ColourGradient(panelRaised().brighter(0.22f), centre.x, centre.y - knobR * 0.35f,
                                           juce::Colour(0xff0b0c10), centre.x, centre.y + knobR, true));
    g.fillEllipse(faceRect);
    g.setColour(accent().withAlpha(0.28f));
    g.drawEllipse(faceRect.reduced(0.5f), 1.0f);                 // thin brass rim
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawEllipse(faceRect.reduced(1.5f), 1.0f);

    // Gold pointer line from the hub to the rim at the value angle.
    const auto pEnd   = centre.getPointOnCircumference(knobR - 3.0f, angle);
    const auto pStart = centre.getPointOnCircumference(knobR * 0.30f, angle);
    g.setColour(goldBright());
    g.drawLine(juce::Line<float>(pStart, pEnd), 2.4f);
}

void CadenzaLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos,
                                               maxSliderPos, style, slider);
        return;
    }

    const float cx = x + width * 0.5f;
    const float trackW = 6.0f;
    juce::Rectangle<float> track(cx - trackW * 0.5f, (float) y, trackW, (float) height);
    g.setColour(outline());
    g.fillRoundedRectangle(track, trackW * 0.5f);

    // Fill from the thumb down to the bottom — gold gradient.
    juce::Rectangle<float> filled(track.getX(), sliderPos, trackW, track.getBottom() - sliderPos);
    g.setGradientFill(juce::ColourGradient(goldBright(), filled.getX(), (float) y,
                                           goldDeep(), filled.getX(), track.getBottom(), false));
    g.fillRoundedRectangle(filled, trackW * 0.5f);

    // Fader cap.
    const float capW = juce::jmin((float) width * 0.82f, 22.0f);
    const float capH = 11.0f;
    juce::Rectangle<float> cap(cx - capW * 0.5f, sliderPos - capH * 0.5f, capW, capH);
    g.setColour(juce::Colour(0xffd8dde4));
    g.fillRoundedRectangle(cap, 3.0f);
    g.setColour(juce::Colour(0xff20242b));
    g.fillRect(cap.withSizeKeepingCentre(cap.getWidth() * 0.6f, 1.4f));
    g.setColour(outline());
    g.drawRoundedRectangle(cap, 3.0f, 1.0f);
}

void CadenzaLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/,
                                      int /*buttonH*/, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
    const float corner = 5.0f;

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, corner);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, corner, 1.0f);

    juce::Path arrow;
    const float aw = 8.0f, ah = 5.0f;
    const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    arrow.addTriangle(ax - aw * 0.5f, ay - ah * 0.5f, ax + aw * 0.5f, ay - ah * 0.5f, ax, ay + ah * 0.5f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.fillPath(arrow);
}
}
