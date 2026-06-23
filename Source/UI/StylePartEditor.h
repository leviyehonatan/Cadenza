// StylePartEditorView — an embeddable FL-Studio-style piano-roll editor for one
// Style Recorder part: toolbar (play/record/copy/paste/duplicate/clear/import/
// patterns/help + snap), a velocity lane, and optional "style chrome" (Section /
// Instrument / Bars / Save) so it can live either inside a page or a window.
// Edits report in STYLE ticks via onNotesEdited.
//
// StylePartEditorWindow is a thin DocumentWindow wrapper kept for compatibility;
// the dedicated Editor page (style chrome on) is the primary home now.

#pragma once

#include "StylePartPianoRoll.h"
#include "../Arranger/Style.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cadenza::ui
{
class StylePartEditorView final : public juce::Component
{
public:
    struct Callbacks
    {
        std::function<void(std::vector<cadenza::arranger::PatternNote>)> onNotesEdited;
        std::function<void(int note, int velocity)> onAudition;
        std::function<void()> onTogglePlayback;
        std::function<void()> onToggleRecord;
        std::function<void(int division)> onSnapDivisionChanged;
        // Style-chrome callbacks (only fire when style controls are visible):
        std::function<void(std::string sectionId)> onPickSection;
        std::function<void(int slot)> onPickPart;   // 0..6 (Drums..Phrase2)
        std::function<void()> onSave;
    };

    explicit StylePartEditorView(Callbacks callbacks);
    ~StylePartEditorView() override;

    void setPart(const std::vector<cadenza::arranger::PatternNote>& notes,
                 int sectionTicks, int ticksPerBeat, int beatsPerBar, int beatUnit,
                 bool percussion);
    void setTransportState(int tickInSection, bool playing, bool recordArmed);

    // Style chrome (Section + Instrument + Bars + Save). Off by default (window mode).
    void setStyleControlsVisible(bool visible);
    void setSections(const std::vector<std::pair<std::string, std::string>>& idAndLabel);
    void setActiveSection(const std::string& sectionId);
    void setActivePart(int slot);   // 0..6
    // Enable editing, or dim the editor and show a hint when nothing is editable.
    void setEditorEnabled(bool enabled, const juce::String& hint = {});

    void resized() override;
    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StylePartEditorView)
};

class StylePartEditorWindow final : public juce::DocumentWindow
{
public:
    struct Callbacks
    {
        std::function<void(std::vector<cadenza::arranger::PatternNote>)> onNotesEdited;
        std::function<void(int note, int velocity)> onAudition;
        std::function<void()> onTogglePlayback;
        std::function<void()> onToggleRecord;
        std::function<void(int division)> onSnapDivisionChanged;
        std::function<void()> onClosed;
    };

    explicit StylePartEditorWindow(Callbacks callbacks);
    ~StylePartEditorWindow() override;

    void setPart(const juce::String& partLabel,
                 const std::vector<cadenza::arranger::PatternNote>& notes,
                 int sectionTicks,
                 int ticksPerBeat,
                 int beatsPerBar,
                 int beatUnit,
                 bool percussion);

    void setTransportState(int tickInSection, bool playing, bool recordArmed);

    void closeButtonPressed() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    Callbacks m_cb;
    std::unique_ptr<StylePartEditorView> m_view;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StylePartEditorWindow)
};
}
