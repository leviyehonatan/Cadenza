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
#include "StylePartBarWorkflow.h"
#include "StylePartNoteWorkflow.h"
#include "StylePartPianoRollGeometry.h"

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
    std::function<void(const bar_workflow::BarSelection&)> onBarSelectionChanged;
    std::function<void(const note_workflow::NoteSelection&)> onNoteSelectionChanged;
    std::function<void(int tick, int pitch)> onGridMousePositionChanged;
    std::function<void(juce::Point<int>)> onBarContextMenuRequested;

    // Load a part. sectionTicks = loop length (style ticks); ticksPerBeat = PPQ;
    // beatsPerBar = time-signature numerator; percussion picks the default range.
    void setPart(const std::vector<cadenza::arranger::PatternNote>& notes,
                 int sectionTicks, int ticksPerBeat, int beatsPerBar, bool percussion);

    void setSnapDivision(int division);   // 0 = off, 4/8/16/32 = grid
    int  snapDivision() const noexcept { return m_snap; }

    void setPlaybackTick(int tickInSection, bool visible);

    const std::vector<cadenza::arranger::PatternNote>& notes() const noexcept { return m_notes; }
    int sectionTicks() const noexcept { return m_sectionTicks; }
    int ticksPerBeat() const noexcept { return m_ticksPerBeat; }
    int beatsPerBar() const noexcept { return m_beatsPerBar; }
    int ticksPerBar() const noexcept { return barTicks(); }
    int totalBars() const noexcept;
    int gridLeft() const noexcept;
    float xForTick(int tick) const noexcept;
    int tickForX(float x) const noexcept;
    const bar_workflow::BarSelection& barSelection() const noexcept { return m_barSelection; }
    const note_workflow::NoteSelection& noteSelection() const noexcept { return m_noteSelection; }
    bool isNoteSelected(int index) const noexcept { return m_noteSelection.contains(index); }
    void setBarSelection(bar_workflow::BarSelection selection);
    void clearBarSelection();
    void clearNoteSelection();
    note_workflow::NoteClipboard copySelectedNotes() const;
    void pasteNotes(const note_workflow::NoteClipboard&, int tick, int pitch);
    void duplicateSelectedNotes();
    void deleteSelectedNotes();
    void replaceNotes(std::vector<cadenza::arranger::PatternNote> notes);
    void replaceNotesAndSelect(
        std::vector<cadenza::arranger::PatternNote> notes,
        note_workflow::NoteSelection selection);
    void setNoteVelocity(int index, int velocity);

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Drag { None, PendingEmpty, BoxSelect, Move, ResizeR };
    enum class RulerGesture { None, Select, Move };

    int   gridTicks() const noexcept;
    int   snapTick(int tick) const noexcept;
    int   barTicks() const noexcept;
    int   beatTicks() const noexcept;
    int   rowHeight() const noexcept;
    int   rowsVisible() const noexcept;
    float tickToX(int tick) const noexcept;
    int   xToTick(float x) const noexcept;
    int   yToPitch(float y) const noexcept;
    float pitchToY(int pitch) const noexcept;   // top of the row for `pitch`
    int   noteIndexAt(juce::Point<float> p, bool& onRightEdge) const;
    void  clampScroll() noexcept;
    void  commit();
    void  auditionPitch(int pitch);
    int   barAtX(float x) const noexcept;
    void  notifyBarSelection();
    void  notifyNoteSelection();
    std::vector<note_workflow::NoteBounds> visibleNoteBounds() const;

    // geometry
    static constexpr int kRulerH = 24;
    static constexpr int kPianoGutterW = 56;
    static constexpr int kDrumGutterW = 112;
    static constexpr int kPianoRowH = 13;
    static constexpr int kDrumRowH = 22;
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
    note_workflow::NoteSelection m_noteSelection;
    std::vector<cadenza::arranger::PatternNote> m_noteDragStartNotes;
    juce::Point<float> m_emptyDragStart;
    juce::Rectangle<float> m_selectionRectangle;
    int m_dragStartPitch = 0;
    int m_dragStartTick = 0;
    int m_resizeStartDuration = 0;

    bar_workflow::BarSelection m_barSelection;
    bar_workflow::BarSelection m_rulerStartSelection;
    std::vector<cadenza::arranger::PatternNote> m_rulerStartNotes;
    RulerGesture m_rulerGesture = RulerGesture::None;
    int m_rulerStartBar = -1;
    int m_rulerMoveDelta = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StylePartPianoRoll)
};
}
