#include "StylePartEditor.h"

#include <algorithm>
#include <cmath>

namespace cadenza::ui
{
namespace
{
constexpr int kGridPpq = PRE::defaultResolution;   // the vendored editor's fixed 480
constexpr int kGridTicksPerBar = kGridPpq * 4;     // its bars are 4 quarters wide
}

StylePartEditorWindow::StylePartEditorWindow(Callbacks callbacks)
    : juce::DocumentWindow("Part Editor",
                           juce::Colour(0xff1c1f26),
                           juce::DocumentWindow::closeButton | juce::DocumentWindow::minimiseButton),
      m_cb(std::move(callbacks))
{
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setContentNonOwned(&m_editor, false);
    centreWithSize(980, 560);

    m_editor.onEdit = [this] {
        if (!m_loading)
            pushEditsToHost();
    };
    m_editor.sendChange = [this](int note, int velocity) {
        if (m_cb.onAudition)
            m_cb.onAudition(note, velocity);
    };
}

void StylePartEditorWindow::setPart(const juce::String& partLabel,
                                    const std::vector<cadenza::arranger::PatternNote>& notes,
                                    int sectionTicks,
                                    int ticksPerBeat,
                                    bool percussion)
{
    m_sectionTicks = std::max(1, sectionTicks);
    m_toGrid = static_cast<double>(kGridPpq) / std::max(1, ticksPerBeat);

    setName("Part Editor - " + partLabel + "  (drag notes; double-click to add; Delete to remove)");

    const int gridTicks = static_cast<int>(std::lround(m_sectionTicks * m_toGrid));
    const int bars = std::max(1, (gridTicks + kGridTicksPerBar - 1) / kGridTicksPerBar);
    m_editor.setup(bars, 420, 12);

    PRESequence sequence;
    sequence.tsLow = 4;
    sequence.tsHight = 4;
    for (const auto& n : notes) {
        NoteModel model(static_cast<u8>(std::clamp(n.pitch, 0, 127)),
                        static_cast<u8>(std::clamp(n.velocity, 1, 127)),
                        static_cast<st_int>(std::lround(n.tick * m_toGrid)),
                        static_cast<st_int>(std::max<long>(1, std::lround(n.duration * m_toGrid))),
                        {});
        sequence.events.push_back(model);
    }

    m_loading = true;
    m_editor.loadSequence(sequence);   // fires onEdit internally; m_loading gates it
    m_loading = false;

    // Scroll to a useful note range instead of the top of the 0..127 grid:
    // drums show the GM percussion area (~kick..cymbals), melodic parts show
    // the playing register (~C2..C6). setScroll takes a 0..1 proportion where
    // 0 is the top (note 127). Aim a note near the top of the viewport.
    const int topNote = percussion ? 64 : 86;     // first note visible below the top
    const double y = juce::jlimit(0.0, 1.0, (127.0 - topNote) / 127.0);
    m_editor.setScroll(0.0, y);
}

void StylePartEditorWindow::setPlaybackTick(int tickInSection, bool visible)
{
    m_editor.setPlaybackMarkerPosition(
        static_cast<st_int>(std::lround(std::max(0, tickInSection) * m_toGrid)), visible);
}

void StylePartEditorWindow::closeButtonPressed()
{
    if (m_cb.onClosed)
        m_cb.onClosed();
}

void StylePartEditorWindow::pushEditsToHost()
{
    if (!m_cb.onNotesEdited)
        return;

    const double fromGrid = m_toGrid > 0.0 ? 1.0 / m_toGrid : 1.0;
    std::vector<cadenza::arranger::PatternNote> notes;
    auto sequence = m_editor.getSequence();
    for (auto& model : sequence.events) {
        cadenza::arranger::PatternNote n;
        n.pitch = model.getNote();
        n.velocity = std::max<int>(1, model.getVelocity());
        n.tick = static_cast<int>(std::lround(model.getStartTime() * fromGrid));
        n.duration = std::max(1, static_cast<int>(std::lround(model.getNoteLegnth() * fromGrid)));
        if (n.tick >= m_sectionTicks)
            n.tick = n.tick % m_sectionTicks;   // grid is rounded up to whole bars
        notes.push_back(n);
    }
    m_cb.onNotesEdited(std::move(notes));
}
}
