// StylePartEditorWindow — piano-roll editing for one Style Recorder part.
//
// Wraps the vendored Piano-Roll-Editor component (Source/UI/PianoRoll, MIT)
// in a DocumentWindow. The editor shows the recorder's target part; every
// grid edit is converted back to PatternNotes and handed to the host, which
// writes them into the in-progress style (live, while the section loops).
//
// Tick domains: the grid runs at the vendored editor's fixed 480 PPQ; style
// ticks (typically 960/1920 PPQ) are scaled on the way in and out.

#pragma once

#include "PianoRoll/PianoRollEditorComponent.hpp"
#include "../Arranger/Style.h"

#include <functional>
#include <vector>

namespace cadenza::ui
{
class StylePartEditorWindow final : public juce::DocumentWindow
{
public:
    struct Callbacks
    {
        // Fired after any grid edit, with the notes in STYLE ticks.
        std::function<void(std::vector<cadenza::arranger::PatternNote>)> onNotesEdited;
        // Note audition while dragging/creating notes (velocity 0 = release).
        std::function<void(int note, int velocity)> onAudition;
        // The user closed the window.
        std::function<void()> onClosed;
    };

    explicit StylePartEditorWindow(Callbacks callbacks);

    // (Re)load the editor with a part's notes. `sectionTicks` is the loop
    // length in style ticks; `ticksPerBeat` the style PPQ.
    void setPart(const juce::String& partLabel,
                 const std::vector<cadenza::arranger::PatternNote>& notes,
                 int sectionTicks,
                 int ticksPerBeat);

    // Move the playback marker (style ticks within the section).
    void setPlaybackTick(int tickInSection, bool visible);

    void closeButtonPressed() override;

private:
    void pushEditsToHost();

    Callbacks m_cb;
    PianoRollEditorComponent m_editor;
    double m_toGrid = 1.0;      // style ticks -> grid ticks (480 PPQ domain)
    int m_sectionTicks = 0;     // style ticks
    bool m_loading = false;     // suppress onEdit while loadSequence runs

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StylePartEditorWindow)
};
}
