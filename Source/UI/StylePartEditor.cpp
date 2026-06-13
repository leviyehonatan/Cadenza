#include "StylePartEditor.h"

#include <algorithm>
#include <vector>

namespace cadenza::ui
{
// Content: a thin container holding a toolbar (grid/snap selector + hint) above
// the piano roll, laid out by resized().
class StylePartEditorWindow::Content final : public juce::Component
{
public:
    class VelocityLane final : public juce::Component
    {
    public:
        explicit VelocityLane(StylePartPianoRoll& owner) : roll(owner) {}

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff20252d));
            g.setColour(juce::Colour(0xff4b5463));
            g.fillRect(0, 0, getWidth(), 1);
            g.setColour(juce::Colour(0xff9aa5b5));
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText("VELOCITY", 6, 3, std::max(0, roll.gridLeft() - 10), 16,
                       juce::Justification::centredLeft, false);

            const float baseline = static_cast<float>(getHeight() - 5);
            const float usableHeight = static_cast<float>(std::max(1, getHeight() - 24));
            for (const auto& note : roll.notes()) {
                const float x = roll.xForTick(note.tick);
                const float height = usableHeight * (note.velocity / 127.0f);
                g.setColour(juce::Colour(0xff4aa8f5));
                g.fillRoundedRectangle(x - 3.0f, baseline - height,
                                       7.0f, height, 2.0f);
            }
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            selectNote(e.position.x);
            applyVelocity(e.position.y);
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            applyVelocity(e.position.y);
        }

    private:
        void selectNote(float x)
        {
            std::vector<piano_roll::VelocityNote> candidates;
            candidates.reserve(roll.notes().size());
            for (const auto& note : roll.notes())
                candidates.push_back({ note.tick, note.duration });
            selectedNote = piano_roll::findNearestNoteAtTick(
                candidates, roll.tickForX(x));
        }

        void applyVelocity(float y)
        {
            if (selectedNote < 0)
                return;
            const float laneTop = 20.0f;
            const int velocity = piano_roll::velocityAtY(
                y - laneTop, static_cast<float>(std::max(1, getHeight() - 24)));
            roll.setNoteVelocity(selectedNote, velocity);
            repaint();
        }

        StylePartPianoRoll& roll;
        int selectedNote = -1;
    };

    explicit Content(StylePartEditorWindow::Callbacks& cb)
        : velocityLane(roll)
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

        position.setColour(juce::Label::textColourId, juce::Colour(0xffdce2eb));
        position.setJustificationType(juce::Justification::centredRight);
        position.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
        addAndMakeVisible(position);

        rec.setText("REC", juce::dontSendNotification);
        rec.setJustificationType(juce::Justification::centred);
        rec.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
        addAndMakeVisible(rec);
        updateRecColour(false);

        addAndMakeVisible(velocityLane);
        setSize(1160, 680);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop(30);
        snapLabel.setBounds(bar.removeFromLeft(44));
        snap.setBounds(bar.removeFromLeft(80).reduced(2, 3));
        bar.removeFromLeft(12);
        rec.setBounds(bar.removeFromRight(54).reduced(2, 3));
        position.setBounds(bar.removeFromRight(76));
        hint.setBounds(bar);

        velocityLane.setBounds(area.removeFromBottom(84));
        roll.setBounds(area);
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff1c1f26)); }

    void setPart(const std::vector<cadenza::arranger::PatternNote>& notes,
                 int sectionTicks, int ticksPerBeat, int beatsPerBar, bool percussion)
    {
        roll.setPart(notes, sectionTicks, ticksPerBeat, beatsPerBar, percussion);
        velocityLane.repaint();
    }

    void setTransportState(int tickInSection, bool playing, bool recordArmed)
    {
        roll.setPlaybackTick(tickInSection, playing);
        updateRecColour(recordArmed);

        const int beatTicks = std::max(1, roll.ticksPerBeat());
        const int beatsPerBar = std::max(1, roll.beatsPerBar());
        const int wrappedTick = piano_roll::wrapPlaybackTick(
            tickInSection, roll.sectionTicks());
        const int totalBeats = wrappedTick / beatTicks;
        const int bar = totalBeats / beatsPerBar + 1;
        const int beat = totalBeats % beatsPerBar + 1;
        position.setText(juce::String(bar) + "." + juce::String(beat),
                         juce::dontSendNotification);
        velocityLane.repaint();
    }

    void updateRecColour(bool armed)
    {
        rec.setColour(juce::Label::textColourId,
                      armed ? juce::Colour(0xffff5252) : juce::Colour(0xff727b89));
        rec.setColour(juce::Label::backgroundColourId,
                      armed ? juce::Colour(0xff531d24) : juce::Colour(0xff272c35));
    }

    StylePartPianoRoll roll;
    VelocityLane velocityLane;
    juce::Label   snapLabel, hint, position, rec;
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
    centreWithSize(1180, 720);
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
    m_content->setPart(notes, sectionTicks, ticksPerBeat, beatsPerBar, percussion);
}

void StylePartEditorWindow::setTransportState(int tickInSection,
                                              bool playing,
                                              bool recordArmed)
{
    m_content->setTransportState(tickInSection, playing, recordArmed);
}

void StylePartEditorWindow::closeButtonPressed()
{
    if (m_cb.onClosed)
        m_cb.onClosed();
}
}
