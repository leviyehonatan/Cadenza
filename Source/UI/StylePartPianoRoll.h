// StylePartPianoRoll — a self-contained FL-Studio-style piano roll for editing
// one Style Recorder part. No external dependencies beyond JUCE.
//
// Interaction (left mouse unless noted):
//   * click empty grid          -> add a note (drag to set its length)
//   * drag a note body          -> move it (pitch + time)
//   * drag a note's right edge   -> resize its length
//   * right-click a note        -> delete it
//   * click a piano key (left)  -> audition that pitch
//   * mouse wheel               -> scroll the visible pitch range
// All times snap to the grid (default 1/16). Every edit fires onNotesEdited
// with the notes in STYLE ticks; notes are auditioned via onAudition.

#pragma once

#include "../Arranger/Style.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace cadenza::ui
{
class StylePartPianoRoll final : public juce::Component
{
public:
    StylePartPianoRoll();

    std::function<void(std::vector<cadenza::arranger::PatternNote>)> onNotesEdited;
    std::function<void(int note, int velocity)> onAudition;

    // Load a part. sectionTicks = loop length (style ticks); ticksPerBeat = PPQ;
    // beatsPerBar = time-signature numerator; percussion picks the default range.
    void setPart(const std::vector<cadenza::arranger::PatternNote>& notes,
                 int sectionTicks, int ticksPerBeat, int beatsPerBar, bool percussion);

    void setSnapDivision(int division);   // 0 = off, 4/8/16/32 = grid
    int  snapDivision() const noexcept { return m_snap; }

    void setPlaybackTick(int tickInSection, bool visible);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Drag { None, Create, Move, ResizeR };

    int   gridTicks() const noexcept;
    int   snapTick(int tick) const noexcept;
    int   barTicks() const noexcept;
    int   beatTicks() const noexcept;
    int   rowsVisible() const noexcept;
    float tickToX(int tick) const noexcept;
    int   xToTick(float x) const noexcept;
    int   yToPitch(float y) const noexcept;
    float pitchToY(int pitch) const noexcept;   // top of the row for `pitch`
    int   noteIndexAt(juce::Point<float> p, bool& onRightEdge) const;
    void  clampScroll() noexcept;
    void  commit();
    void  auditionPitch(int pitch);

    // geometry
    static constexpr int kKeyboardW = 56;
    static constexpr int kRulerH = 24;
    static constexpr int kNoteH = 13;
    static constexpr int kEdgeGrab = 6;
    int m_topNote = 84;   // highest pitch shown (top grid row)

    // model (working copy, style ticks)
    std::vector<cadenza::arranger::PatternNote> m_notes;
    int  m_sectionTicks = 7680;
    int  m_ticksPerBeat = 960;
    int  m_beatsPerBar = 4;
    bool m_percussion = false;
    int  m_snap = 16;

    int  m_playbackTick = -1;
    bool m_playbackVisible = false;

    // interaction state
    Drag m_drag = Drag::None;
    int  m_dragIndex = -1;
    int  m_grabTickOffset = 0;
    int  m_lastDrawDuration = 0;
    int  m_lastAuditioned = -1;
    int  m_hoverIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StylePartPianoRoll)
};
}
