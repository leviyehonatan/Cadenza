#include "StylePartEditor.h"

namespace cadenza::ui
{
// Content: a thin container holding a toolbar (grid/snap selector + hint) above
// the piano roll, laid out by resized().
class StylePartEditorWindow::Content final : public juce::Component
{
public:
    explicit Content(StylePartEditorWindow::Callbacks& cb)
    {
        addAndMakeVisible(roll);
        roll.onNotesEdited = [&cb](std::vector<cadenza::arranger::PatternNote> n) {
            if (cb.onNotesEdited) cb.onNotesEdited(std::move(n));
        };
        roll.onAudition = [&cb](int note, int velocity) {
            if (cb.onAudition) cb.onAudition(note, velocity);
        };

        snapLabel.setText("Grid", juce::dontSendNotification);
        snapLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        snapLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(snapLabel);

        snap.addItem("1/4", 4);
        snap.addItem("1/8", 8);
        snap.addItem("1/16", 16);
        snap.addItem("1/32", 32);
        snap.addItem("Off", 1);   // id 1 -> snap division 0
        snap.setSelectedId(16, juce::dontSendNotification);
        snap.onChange = [this] {
            const int id = snap.getSelectedId();
            roll.setSnapDivision(id == 1 ? 0 : id);
        };
        addAndMakeVisible(snap);

        hint.setText("Click empty space to draw  •  drag to move  •  drag right edge to resize  "
                     "•  right-click to delete  •  wheel to scroll",
                     juce::dontSendNotification);
        hint.setColour(juce::Label::textColourId, juce::Colours::grey);
        hint.setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(hint);

        setSize(960, 560);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop(30);
        snapLabel.setBounds(bar.removeFromLeft(44));
        snap.setBounds(bar.removeFromLeft(80).reduced(2, 3));
        bar.removeFromLeft(12);
        hint.setBounds(bar);
        roll.setBounds(area);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff1c1f26)); }

    StylePartPianoRoll roll;
    juce::Label   snapLabel, hint;
    juce::ComboBox snap;
};

StylePartEditorWindow::StylePartEditorWindow(Callbacks callbacks)
    : juce::DocumentWindow("Part Editor",
                           juce::Colour(0xff1c1f26),
                           juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      m_cb(std::move(callbacks))
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    m_content = std::make_unique<Content>(m_cb);
    setContentNonOwned(m_content.get(), true);
    centreWithSize(980, 600);
}

StylePartEditorWindow::~StylePartEditorWindow()
{
    clearContentComponent();
}

void StylePartEditorWindow::setPart(const juce::String& partLabel,
                                    const std::vector<cadenza::arranger::PatternNote>& notes,
                                    int sectionTicks,
                                    int ticksPerBeat,
                                    int beatsPerBar,
                                    bool percussion)
{
    setName("Part Editor - " + partLabel);
    m_content->roll.setPart(notes, sectionTicks, ticksPerBeat, beatsPerBar, percussion);
}

void StylePartEditorWindow::setPlaybackTick(int tickInSection, bool visible)
{
    m_content->roll.setPlaybackTick(tickInSection, visible);
}

void StylePartEditorWindow::closeButtonPressed()
{
    if (m_cb.onClosed)
        m_cb.onClosed();
}
}
