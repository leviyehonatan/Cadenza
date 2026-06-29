// FirstLaunchAssistant — a small, one-time "Quick Start" panel shown on the very
// first launch. It surfaces the four things a new user needs (audio, sound,
// style, MIDI), lets them fix any that aren't ready by reusing the app's existing
// dialogs, plays a test chord, and explains the core idea: left hand = chords
// (the band follows), right hand = melody.
//
// All display logic lives in the pure cadenza::ui::buildFirstLaunchChecklist();
// this class is just the native-JUCE rendering + buttons. It refreshes itself
// every second from a caller-supplied provider so the status dots update live as
// the user loads a SoundFont or style.

#pragma once

#include "FirstLaunchChecklist.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace cadenza::ui
{
class FirstLaunchAssistant final : public juce::Component,
                                   private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<void()> openAudioSettings;
        std::function<void()> chooseSoundFont;
        std::function<void()> loadStyle;
        std::function<void()> testSound;
        std::function<void()> requestClose;   // "Get Started"
    };

    FirstLaunchAssistant(Callbacks callbacks,
                         std::function<FirstLaunchInputs()> provider);
    ~FirstLaunchAssistant() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth  = 470;
    static constexpr int kHeight = 560;

private:
    void timerCallback() override;   // live-refresh the checklist
    void refresh();

    Callbacks                          m_cb;
    std::function<FirstLaunchInputs()> m_provider;
    std::vector<ChecklistItem>         m_items;

    juce::TextButton m_audioBtn { "Audio Settings" };
    juce::TextButton m_soundFontBtn { "Choose SoundFont" };
    juce::TextButton m_styleBtn { "Load Style" };
    juce::TextButton m_testBtn  { juce::String::fromUTF8("\xE2\x96\xB6  Test Sound") };
    juce::TextButton m_startBtn { "Get Started" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FirstLaunchAssistant)
};
}
