// StylePartEditorWindow — a DocumentWindow hosting the StylePartPianoRoll for
// one Style Recorder part. Owns a small toolbar (grid/snap selector) above the
// roll. Edits are reported in STYLE ticks via onNotesEdited.

#pragma once

#include "StylePartPianoRoll.h"
#include "../Arranger/Style.h"

#include <functional>
#include <memory>
#include <vector>

namespace cadenza::ui
{
class StylePartEditorWindow final : public juce::DocumentWindow
{
public:
    struct Callbacks
    {
        std::function<void(std::vector<cadenza::arranger::PatternNote>)> onNotesEdited;
        std::function<void(int note, int velocity)> onAudition;
        std::function<void()> onClosed;
    };

    explicit StylePartEditorWindow(Callbacks callbacks);
    ~StylePartEditorWindow() override;

    void setPart(const juce::String& partLabel,
                 const std::vector<cadenza::arranger::PatternNote>& notes,
                 int sectionTicks,
                 int ticksPerBeat,
                 int beatsPerBar,
                 bool percussion);

    void setTransportState(int tickInSection, bool playing, bool recordArmed);

    void closeButtonPressed() override;

private:
    class Content;
    Callbacks m_cb;
    std::unique_ptr<Content> m_content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StylePartEditorWindow)
};
}
