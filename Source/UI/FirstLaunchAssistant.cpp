#include "FirstLaunchAssistant.h"

namespace cadenza::ui
{
namespace
{
constexpr int kPad      = 24;
constexpr int kRowsTop  = 96;
constexpr int kRowH     = 58;
constexpr int kBtnW     = 140;
constexpr int kBtnH     = 30;

juce::Colour dotColour(const ChecklistItem& it)
{
    if (it.ok)        return juce::Colour(0xff4caf50);   // green
    if (it.required)  return juce::Colour(0xffe5733a);   // amber/orange = needs attention
    return juce::Colour(0xff7a7f8a);                     // grey = optional, not set
}
}

FirstLaunchAssistant::FirstLaunchAssistant(Callbacks callbacks,
                                           std::function<FirstLaunchInputs()> provider)
    : m_cb(std::move(callbacks)), m_provider(std::move(provider))
{
    setSize(kWidth, kHeight);

    auto addBtn = [this](juce::TextButton& b, std::function<void()> fn) {
        addAndMakeVisible(b);
        b.onClick = [fn = std::move(fn)] { if (fn) fn(); };
    };
    addBtn(m_audioBtn,     m_cb.openAudioSettings);
    addBtn(m_soundFontBtn, m_cb.chooseSoundFont);
    addBtn(m_styleBtn,     m_cb.loadStyle);
    addBtn(m_testBtn,      m_cb.testSound);
    addBtn(m_startBtn,     m_cb.requestClose);

    // Make "Get Started" the visually primary action.
    m_startBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a7d44));

    refresh();
    startTimer(1000);   // keep the status dots live as the user fixes things
}

FirstLaunchAssistant::~FirstLaunchAssistant()
{
    stopTimer();
}

void FirstLaunchAssistant::refresh()
{
    if (m_provider)
        m_items = buildFirstLaunchChecklist(m_provider());
    repaint();
}

void FirstLaunchAssistant::timerCallback()
{
    refresh();
}

void FirstLaunchAssistant::resized()
{
    auto rowBtn = [this](juce::TextButton& b, int row) {
        const int y = kRowsTop + row * kRowH + (kRowH - kBtnH) / 2;
        b.setBounds(getWidth() - kPad - kBtnW, y, kBtnW, kBtnH);
    };
    rowBtn(m_audioBtn,     0);
    rowBtn(m_soundFontBtn, 1);
    rowBtn(m_styleBtn,     2);
    // Row 3 (MIDI) is optional/info — no button.

    const int by = getHeight() - kPad - 34;
    m_testBtn.setBounds(kPad, by, 150, 34);
    m_startBtn.setBounds(getWidth() - kPad - 150, by, 150, 34);
}

void FirstLaunchAssistant::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff20242e));

    // Title + subtitle.
    g.setColour(juce::Colour(0xffe8c46a));
    g.setFont(juce::Font(juce::FontOptions(26.0f, juce::Font::bold)));
    g.drawText("Welcome to Cadenza", kPad, 18, getWidth() - 2 * kPad, 32,
               juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xffb8bdc8));
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText("Quick start - let's get you playing in a few seconds.",
               kPad, 54, getWidth() - 2 * kPad, 20, juce::Justification::centredLeft);

    // Checklist rows.
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        const auto& it = m_items[i];
        const int rowY = kRowsTop + static_cast<int>(i) * kRowH;

        // Status dot.
        g.setColour(dotColour(it));
        const float cx = static_cast<float>(kPad + 7);
        const float cy = static_cast<float>(rowY + kRowH / 2);
        g.fillEllipse(cx - 7.0f, cy - 7.0f, 14.0f, 14.0f);
        if (it.ok)
        {
            // a little check mark
            g.setColour(juce::Colours::white);
            juce::Path tick;
            tick.startNewSubPath(cx - 3.2f, cy);
            tick.lineTo(cx - 0.8f, cy + 3.0f);
            tick.lineTo(cx + 3.6f, cy - 3.2f);
            g.strokePath(tick, juce::PathStrokeType(1.8f));
        }

        // Rows 0-2 have an action button on the right; the optional MIDI row (and
        // any buttonless row) gets the full width for its text.
        const bool hasButton = (i < 3);
        const int textX = kPad + 28;
        const int textW = getWidth() - textX - kPad - (hasButton ? (kBtnW + 12) : 0);

        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        g.drawText(it.label, textX, rowY + 8, textW, 20, juce::Justification::centredLeft);

        g.setColour(it.ok ? juce::Colour(0xff9fd6a4) : juce::Colour(0xffb8bdc8));
        g.setFont(juce::Font(juce::FontOptions(13.0f)));
        g.drawText(it.detail, textX, rowY + 28, textW, 20, juce::Justification::centredLeft);
    }

    // "How to play" hint panel.
    const int hintY = kRowsTop + 4 * kRowH + 6;
    juce::Rectangle<int> hint(kPad, hintY, getWidth() - 2 * kPad, 96);
    g.setColour(juce::Colour(0xff2a3340));
    g.fillRoundedRectangle(hint.toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff3a4658));
    g.drawRoundedRectangle(hint.toFloat(), 8.0f, 1.0f);

    g.setColour(juce::Colour(0xffe8c46a));
    g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
    g.drawText("How to play", hint.getX() + 12, hint.getY() + 8,
               hint.getWidth() - 24, 18, juce::Justification::centredLeft);

    g.setColour(juce::Colour(0xffd2d6de));
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    const juce::String body =
        juce::String::fromUTF8(
            "\xE2\x80\xA2  Left of the split = chords. Hold one and the band follows it.\n"
            "\xE2\x80\xA2  Right of the split = your melody.\n"
            "\xE2\x80\xA2  Press Play, then hold a chord. No keyboard? Click the keys below.");
    g.drawMultiLineText(body, hint.getX() + 12, hint.getY() + 34,
                        hint.getWidth() - 24, juce::Justification::left, 1.2f);
}
}
