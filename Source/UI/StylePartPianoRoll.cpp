#include "StylePartPianoRoll.h"

#include <algorithm>
#include <cmath>

namespace cadenza::ui
{
using cadenza::arranger::PatternNote;

namespace
{
bool isBlackKey(int pitch) noexcept
{
    switch (((pitch % 12) + 12) % 12) {
        case 1: case 3: case 6: case 8: case 10: return true;
        default: return false;
    }
}

juce::String noteName(int pitch)
{
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int pc = ((pitch % 12) + 12) % 12;
    const int octave = pitch / 12 - 1;   // MIDI: 60 = C4
    return juce::String(names[pc]) + juce::String(octave);
}
}

StylePartPianoRoll::StylePartPianoRoll()
{
    setWantsKeyboardFocus(false);
}

void StylePartPianoRoll::setPart(const std::vector<PatternNote>& notes,
                                 int sectionTicks, int ticksPerBeat, int beatsPerBar,
                                 bool percussion)
{
    m_notes = notes;
    m_sectionTicks = std::max(1, sectionTicks);
    m_ticksPerBeat = std::max(24, ticksPerBeat);
    m_beatsPerBar = std::max(1, beatsPerBar);
    m_percussion = percussion;
    bar_workflow::clearSelection(m_barSelection);
    m_noteSelection.clear();
    m_lastDrawDuration = gridTicks();
    // Sensible default register: GM drums vs. the playing range.
    m_topNote = percussion ? 60 : 84;
    clampScroll();
    repaint();
}

void StylePartPianoRoll::setSnapDivision(int division)
{
    m_snap = std::max(0, division);
    m_lastDrawDuration = gridTicks();
    repaint();
}

void StylePartPianoRoll::setPlaybackTick(int tickInSection, bool visible)
{
    m_playbackTick = tickInSection;
    if (visible != m_playbackVisible || visible)
        repaint();
    m_playbackVisible = visible;
}

// --- geometry helpers ---

int StylePartPianoRoll::gridTicks() const noexcept
{
    if (m_snap <= 0) return 1;
    return std::max(1, (m_ticksPerBeat * 4) / m_snap);
}

int StylePartPianoRoll::snapTick(int tick) const noexcept
{
    if (m_snap <= 0) return std::clamp(tick, 0, m_sectionTicks);
    const int g = gridTicks();
    return std::clamp((tick + g / 2) / g * g, 0, m_sectionTicks);
}

int StylePartPianoRoll::barTicks() const noexcept { return m_ticksPerBeat * m_beatsPerBar; }
int StylePartPianoRoll::beatTicks() const noexcept { return m_ticksPerBeat; }
int StylePartPianoRoll::totalBars() const noexcept
{
    return std::max(1, (m_sectionTicks + barTicks() - 1) / barTicks());
}
int StylePartPianoRoll::rowHeight() const noexcept
{
    return m_percussion ? kDrumRowH : kPianoRowH;
}

int StylePartPianoRoll::rowsVisible() const noexcept
{
    return std::max(1, (getHeight() - kRulerH) / rowHeight());
}

int StylePartPianoRoll::gridLeft() const noexcept
{
    return m_percussion ? kDrumGutterW : kPianoGutterW;
}

float StylePartPianoRoll::tickToX(int tick) const noexcept
{
    return piano_roll::tickToX(tick, m_sectionTicks,
                               static_cast<float>(gridLeft()),
                               static_cast<float>(getWidth()));
}

int StylePartPianoRoll::xToTick(float x) const noexcept
{
    return piano_roll::xToTick(x, m_sectionTicks,
                               static_cast<float>(gridLeft()),
                               static_cast<float>(getWidth()));
}

int StylePartPianoRoll::yToPitch(float y) const noexcept
{
    const int row = (int) std::floor((y - kRulerH) / rowHeight());
    return piano_roll::pitchForRow(row, m_topNote);
}

float StylePartPianoRoll::pitchToY(int pitch) const noexcept
{
    return (float) (kRulerH + (m_topNote - pitch) * rowHeight());
}

void StylePartPianoRoll::clampScroll() noexcept
{
    const int rows = rowsVisible();
    m_topNote = std::clamp(m_topNote, rows - 1, 127);
}

int StylePartPianoRoll::noteIndexAt(juce::Point<float> p, bool& onRightEdge) const
{
    onRightEdge = false;
    for (int i = (int) m_notes.size() - 1; i >= 0; --i) {
        const auto& n = m_notes[i];
        const float x0 = tickToX(n.tick);
        const float x1 = tickToX(std::min(m_sectionTicks, n.tick + n.duration));
        const float y0 = pitchToY(n.pitch);
        const juce::Rectangle<float> r(x0, y0, std::max(3.0f, x1 - x0),
                                       (float) rowHeight());
        if (r.contains(p)) {
            onRightEdge = (p.x >= r.getRight() - kEdgeGrab);
            return i;
        }
    }
    return -1;
}

// --- painting ---

void StylePartPianoRoll::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff20242c));

    const int rows = rowsVisible();
    const int gutterRight = gridLeft();
    const int gridRight = getWidth();
    const int rowH = rowHeight();
    const int barT = barTicks();

    // Alternating measures make long patterns easier to scan.
    if (barT > 0) {
        for (int t = 0, bar = 0; t < m_sectionTicks; t += barT, ++bar) {
            if ((bar & 1) == 0)
                continue;
            const float x0 = tickToX(t);
            const float x1 = tickToX(std::min(m_sectionTicks, t + barT));
            g.setColour(juce::Colour(0xff151a21).withAlpha(0.18f));
            g.fillRect(x0, (float) kRulerH, x1 - x0,
                       (float) (getHeight() - kRulerH));
        }
    }

    // Row backgrounds (black-key rows a touch darker) + horizontal separators.
    for (int row = 0; row < rows; ++row) {
        const int pitch = piano_roll::pitchForRow(row, m_topNote);
        const float y = (float) (kRulerH + row * rowH);
        g.setColour(isBlackKey(pitch) ? juce::Colour(0xff262b34) : juce::Colour(0xff2c323c));
        g.fillRect((float) gutterRight, y, (float) (gridRight - gutterRight), (float) rowH);
        if (((pitch % 12) + 12) % 12 == 0) {   // brighter line under each C
            g.setColour(juce::Colour(0xff3a4150));
            g.fillRect((float) gutterRight, y + rowH - 1,
                       (float) (gridRight - gutterRight), 1.0f);
        }
    }

    if (m_barSelection.valid()) {
        const float x0 = tickToX(m_barSelection.first * barT);
        const float x1 = tickToX(std::min(m_sectionTicks,
                                         (m_barSelection.last + 1) * barT));
        g.setColour(juce::Colour(0xff3f86df).withAlpha(0.30f));
        g.fillRect(x0, (float) kRulerH, x1 - x0,
                   (float) (getHeight() - kRulerH));
        g.setColour(juce::Colour(0xff7db7ff));
        g.fillRect(x0, (float) kRulerH, 3.0f,
                   (float) (getHeight() - kRulerH));
        g.fillRect(x1 - 3.0f, (float) kRulerH, 3.0f,
                   (float) (getHeight() - kRulerH));
    }

    // Vertical grid: snap subdivisions (faint), beats (medium), bars (bright).
    const int bT = beatTicks();
    const int gT = m_snap > 0 ? gridTicks() : bT;
    for (int t = 0; t <= m_sectionTicks; t += std::max(1, gT)) {
        const float x = tickToX(t);
        const auto kind = piano_roll::classifyGridLine(
            t, m_ticksPerBeat, m_beatsPerBar, gT);
        float width = 1.0f;
        if (kind == piano_roll::GridLineKind::Bar) {
            g.setColour(juce::Colour(0xff738097));
            width = 2.0f;
        } else if (kind == piano_roll::GridLineKind::Beat) {
            g.setColour(juce::Colour(0xff4d5868));
            width = 1.25f;
        } else {
            g.setColour(juce::Colour(0xff343b47));
        }
        g.fillRect(x, (float) kRulerH, width,
                   (float) (getHeight() - kRulerH));
    }

    // Ruler with bar numbers.
    g.setColour(juce::Colour(0xff181b21));
    g.fillRect(0, 0, getWidth(), kRulerH);
    if (m_barSelection.valid()) {
        const float x0 = tickToX(m_barSelection.first * barT);
        const float x1 = tickToX(std::min(m_sectionTicks,
                                         (m_barSelection.last + 1) * barT));
        g.setColour(juce::Colour(0xff2f6db5));
        g.fillRect(x0, 0.0f, x1 - x0, (float) kRulerH);
        g.setColour(juce::Colour(0xff8bc3ff));
        g.fillRect(x0, (float) kRulerH - 3.0f, x1 - x0, 3.0f);
    }
    g.setColour(juce::Colour(0xffedf1f7));
    g.setFont(juce::Font(juce::FontOptions(12.5f)).boldened());
    if (barT > 0) {
        for (int t = 0; t < m_sectionTicks; t += barT) {
            const float x = tickToX(t);
            const float nextX = tickToX(std::min(m_sectionTicks, t + barT));
            g.drawText(juce::String(piano_roll::measureNumberAtTick(
                           t, m_ticksPerBeat, m_beatsPerBar)),
                       (int) x, 2, std::max(1, (int) (nextX - x)), kRulerH - 4,
                       juce::Justification::centred, false);
        }
    }

    // Notes.
    for (int i = 0; i < (int) m_notes.size(); ++i) {
        const auto& n = m_notes[i];
        const float x0 = tickToX(n.tick);
        const float x1 = tickToX(std::min(m_sectionTicks, n.tick + n.duration));
        const float y = pitchToY(n.pitch);
        if (y + rowH < kRulerH || y > getHeight()) continue;   // off-screen
        juce::Rectangle<float> r(x0 + 1.0f, y + 2.0f,
                                 std::max(3.0f, x1 - x0 - 2.0f),
                                 (float) (rowH - 4));
        const float vel = std::clamp(n.velocity / 127.0f, 0.2f, 1.0f);
        const bool selected = m_noteSelection.contains(i);
        g.setColour(selected
            ? juce::Colour(0xffffa43a)
            : juce::Colour(0xff3da5ff).withBrightness(0.55f + 0.45f * vel));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(selected ? juce::Colour(0xffffe0ad)
                             : juce::Colours::black.withAlpha(0.5f));
        g.drawRoundedRectangle(r, 3.0f, selected ? 2.0f : 1.0f);
    }

    if (m_drag == Drag::BoxSelect && !m_selectionRectangle.isEmpty()) {
        g.setColour(juce::Colour(0xff6eb4ff).withAlpha(0.18f));
        g.fillRect(m_selectionRectangle);
        g.setColour(juce::Colour(0xff8bc7ff));
        g.drawRect(m_selectionRectangle, 1.5f);
    }

    // Playback marker.
    if (m_playbackVisible && m_playbackTick >= 0) {
        const float x = piano_roll::playheadX(
            m_playbackTick, m_sectionTicks,
            static_cast<float>(gutterRight), static_cast<float>(getWidth()));
        g.setColour(juce::Colour(0xffff9f32));
        g.fillRect(x - 1.0f, 0.0f, 2.0f, (float) getHeight());
        juce::Path marker;
        marker.addTriangle(x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 7.0f);
        g.fillPath(marker);
    }

    // Instrument gutter (drawn last so it overlays the grid edge).
    g.setColour(juce::Colour(0xff14161b));
    g.fillRect(0, kRulerH, gutterRight, getHeight() - kRulerH);
    for (int row = 0; row < rows; ++row) {
        const int pitch = piano_roll::pitchForRow(row, m_topNote);
        const float y = (float) (kRulerH + row * rowH);
        if (piano_roll::gutterMode(m_percussion) == piano_roll::GutterMode::Drums) {
            g.setColour((row & 1) ? juce::Colour(0xff20252e)
                                  : juce::Colour(0xff262c36));
            g.fillRect(1.0f, y + 1.0f, (float) (gutterRight - 2), (float) (rowH - 1));
            g.setColour(juce::Colour(0xffd8dee8));
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(piano_roll::drumLabelForPitch(pitch), 6, (int) y,
                       gutterRight - 10, rowH, juce::Justification::centredLeft, true);
        } else {
            const bool black = isBlackKey(pitch);
            g.setColour(black ? juce::Colour(0xff1c1f26) : juce::Colour(0xffe8e8ee));
            g.fillRect(1.0f, y + 1.0f, (float) (gutterRight - 2), (float) (rowH - 1));
            if (((pitch % 12) + 12) % 12 == 0) {
                g.setColour(juce::Colours::black);
                g.setFont(juce::Font(juce::FontOptions(10.0f)));
                g.drawText(noteName(pitch), 2, (int) y, gutterRight - 4, rowH,
                           juce::Justification::centredRight, false);
            }
        }
    }
    g.setColour(juce::Colour(0xff3a4150));
    g.drawVerticalLine(gutterRight, (float) kRulerH, (float) getHeight());
}

void StylePartPianoRoll::resized() { clampScroll(); }

// --- interaction ---

void StylePartPianoRoll::auditionPitch(int pitch)
{
    if (onAudition && pitch != m_lastAuditioned) {
        onAudition(pitch, 100);
        m_lastAuditioned = pitch;
    }
}

void StylePartPianoRoll::mouseDown(const juce::MouseEvent& e)
{
    const juce::Point<float> p = e.position;

    // Piano key click -> audition only.
    if (p.x < gridLeft()) {
        if (p.y >= kRulerH) {
            const int pitch = yToPitch(p.y);
            m_lastAuditioned = -1;
            auditionPitch(pitch);
        }
        return;
    }
    if (p.y < kRulerH) {
        if (p.x < gridLeft())
            return;
        const int bar = barAtX(p.x);
        if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
            if (!m_barSelection.contains(bar))
                bar_workflow::selectBar(m_barSelection, bar, totalBars());
            m_noteSelection.clear();
            notifyNoteSelection();
            notifyBarSelection();
            repaint();
            if (onBarContextMenuRequested)
                onBarContextMenuRequested(e.getPosition());
            return;
        }
        m_noteSelection.clear();
        notifyNoteSelection();
        m_rulerStartBar = bar;
        m_rulerMoveDelta = 0;
        if (e.mods.isShiftDown()) {
            bar_workflow::extendSelection(m_barSelection, bar, totalBars());
            m_rulerGesture = RulerGesture::Select;
        } else if (m_barSelection.contains(bar)) {
            m_rulerGesture = RulerGesture::Move;
            m_rulerStartSelection = m_barSelection;
            m_rulerStartNotes = m_notes;
        } else {
            bar_workflow::selectBar(m_barSelection, bar, totalBars());
            m_rulerGesture = RulerGesture::Select;
        }
        notifyBarSelection();
        repaint();
        return;
    }

    bool onRightEdge = false;
    const int idx = noteIndexAt(p, onRightEdge);

    // Right-click (or popup modifier) deletes the note under the cursor.
    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
        if (idx >= 0) {
            if (m_noteSelection.contains(idx) && m_noteSelection.size() > 1)
                m_notes = note_workflow::deleteSelected(m_notes, m_noteSelection);
            else
                m_notes.erase(m_notes.begin() + idx);
            m_noteSelection.clear();
            notifyNoteSelection();
            commit();
            repaint();
        } else if (m_barSelection.contains(barAtX(p.x))
                   && onBarContextMenuRequested) {
            onBarContextMenuRequested(e.getPosition());
        }
        return;
    }

    m_lastAuditioned = -1;
    if (idx >= 0) {
        const bool wasSelected = m_noteSelection.contains(idx);
        if (e.mods.isCtrlDown()) {
            note_workflow::toggle(m_noteSelection, idx);
            notifyNoteSelection();
            repaint();
            return;
        }
        if (!wasSelected)
            note_workflow::selectOnly(m_noteSelection, idx);
        notifyNoteSelection();

        if (e.mods.isShiftDown() && wasSelected && !onRightEdge) {
            const int originalSize = (int) m_notes.size();
            const int selectionOffset = (int) std::distance(
                m_noteSelection.begin(), m_noteSelection.find(idx));
            auto duplicated = note_workflow::duplicateSelected(m_notes, m_noteSelection);
            m_notes = std::move(duplicated.notes);
            m_noteSelection = std::move(duplicated.selection);
            m_dragIndex = originalSize + selectionOffset;
        } else {
            m_dragIndex = idx;
        }
        m_noteDragStartNotes = m_notes;
        m_dragStartTick = m_notes[m_dragIndex].tick;
        m_dragStartPitch = m_notes[m_dragIndex].pitch;
        m_resizeStartDuration = m_notes[m_dragIndex].duration;
        if (onRightEdge) {
            m_drag = Drag::ResizeR;
        } else {
            m_drag = Drag::Move;
            m_grabTickOffset = xToTick(p.x) - m_notes[m_dragIndex].tick;
        }
        auditionPitch(m_notes[m_dragIndex].pitch);
        clearBarSelection();
        repaint();
        return;
    }

    clearBarSelection();
    m_emptyDragStart = p;
    m_selectionRectangle = {};
    m_drag = Drag::PendingEmpty;
}

void StylePartPianoRoll::mouseDrag(const juce::MouseEvent& e)
{
    if (m_rulerGesture != RulerGesture::None) {
        const int bar = barAtX(e.position.x);
        if (m_rulerGesture == RulerGesture::Select) {
            bar_workflow::dragSelection(m_barSelection, bar, totalBars());
        } else {
            const int delta = bar - m_rulerStartBar;
            const auto moved = bar_workflow::moveBars(
                m_rulerStartNotes, m_rulerStartSelection,
                delta, totalBars(), barTicks());
            m_notes = moved.notes;
            m_barSelection = moved.selection;
            m_rulerMoveDelta = m_barSelection.first - m_rulerStartSelection.first;
        }
        notifyBarSelection();
        repaint();
        return;
    }

    if (m_drag == Drag::PendingEmpty || m_drag == Drag::BoxSelect) {
        if (m_drag == Drag::PendingEmpty
            && e.position.getDistanceFrom(m_emptyDragStart) >= 4.0f) {
            m_drag = Drag::BoxSelect;
            m_noteSelection.clear();
        }
        if (m_drag == Drag::BoxSelect) {
            const float left = std::min(m_emptyDragStart.x, e.position.x);
            const float top = std::min(m_emptyDragStart.y, e.position.y);
            m_selectionRectangle = {
                left, top,
                std::abs(e.position.x - m_emptyDragStart.x),
                std::abs(e.position.y - m_emptyDragStart.y)
            };
            note_workflow::Rectangle rectangle {
                m_selectionRectangle.getX(), m_selectionRectangle.getY(),
                m_selectionRectangle.getWidth(), m_selectionRectangle.getHeight()
            };
            m_noteSelection = note_workflow::selectIntersecting(
                visibleNoteBounds(), rectangle);
            notifyNoteSelection();
            repaint();
        }
        return;
    }

    if (m_drag == Drag::None || m_dragIndex < 0 || m_dragIndex >= (int) m_notes.size())
        return;
    const juce::Point<float> p = e.position;

    if (m_drag == Drag::Move) {
        const int newTick = snapTick(xToTick(p.x) - m_grabTickOffset);
        const int newPitch = yToPitch(p.y);
        m_notes = note_workflow::moveSelected(
            m_noteDragStartNotes, m_noteSelection,
            newTick - m_dragStartTick, newPitch - m_dragStartPitch,
            m_sectionTicks);
        auditionPitch(m_notes[m_dragIndex].pitch);
    } else {
        const int end = snapTick(xToTick(p.x));
        m_notes = note_workflow::resizeSelected(
            m_noteDragStartNotes, m_noteSelection,
            (end - m_notes[m_dragIndex].tick) - m_resizeStartDuration,
            gridTicks(), m_sectionTicks);
    }
    repaint();
}

void StylePartPianoRoll::mouseUp(const juce::MouseEvent&)
{
    if (m_rulerGesture != RulerGesture::None) {
        if (m_rulerGesture == RulerGesture::Move) {
            if (m_rulerMoveDelta != 0) {
                commit();
            } else {
                bar_workflow::selectBar(
                    m_barSelection, m_rulerStartBar, totalBars());
                notifyBarSelection();
                repaint();
            }
        }
        m_rulerGesture = RulerGesture::None;
        m_rulerStartBar = -1;
        m_rulerStartNotes.clear();
        return;
    }

    if (m_drag == Drag::PendingEmpty) {
        PatternNote note;
        note.tick = snapTick(xToTick(m_emptyDragStart.x));
        note.pitch = yToPitch(m_emptyDragStart.y);
        note.velocity = 100;
        note.duration = std::max(gridTicks(), m_lastDrawDuration);
        if (note.tick + note.duration > m_sectionTicks)
            note.duration = std::max(gridTicks(), m_sectionTicks - note.tick);
        m_notes.push_back(note);
        note_workflow::selectOnly(m_noteSelection, (int) m_notes.size() - 1);
        notifyNoteSelection();
        auditionPitch(note.pitch);
        commit();
        repaint();
    } else if (m_drag == Drag::BoxSelect) {
        m_selectionRectangle = {};
        repaint();
    } else if (m_drag != Drag::None && m_dragIndex >= 0
               && m_dragIndex < (int) m_notes.size()) {
        if (m_drag == Drag::ResizeR)
            m_lastDrawDuration = m_notes[m_dragIndex].duration;
        commit();
    }
    m_drag = Drag::None;
    m_dragIndex = -1;
    m_lastAuditioned = -1;
    m_noteDragStartNotes.clear();
}

void StylePartPianoRoll::mouseMove(const juce::MouseEvent& e)
{
    if (e.position.x >= gridLeft() && e.position.y >= kRulerH
        && onGridMousePositionChanged)
        onGridMousePositionChanged(xToTick(e.position.x), yToPitch(e.position.y));
    bool edge = false;
    const int idx = noteIndexAt(e.position, edge);
    if (idx != m_hoverIndex) m_hoverIndex = idx;
    setMouseCursor(idx >= 0 && edge ? juce::MouseCursor::LeftRightResizeCursor
                                    : juce::MouseCursor::NormalCursor);
}

void StylePartPianoRoll::mouseWheelMove(const juce::MouseEvent&,
                                        const juce::MouseWheelDetails& wheel)
{
    const int step = (int) std::lround(wheel.deltaY * 6.0f);
    if (step != 0) {
        m_topNote += step;
        clampScroll();
        repaint();
    }
}

void StylePartPianoRoll::commit()
{
    if (onNotesEdited)
        onNotesEdited(m_notes);
}

float StylePartPianoRoll::xForTick(int tick) const noexcept
{
    return tickToX(tick);
}

int StylePartPianoRoll::tickForX(float x) const noexcept
{
    return xToTick(x);
}

void StylePartPianoRoll::setNoteVelocity(int index, int velocity)
{
    if (index < 0 || index >= static_cast<int>(m_notes.size()))
        return;
    const int clamped = std::clamp(velocity, 1, 127);
    if (m_notes[index].velocity == clamped)
        return;
    m_notes[index].velocity = clamped;
    commit();
    repaint();
}

int StylePartPianoRoll::barAtX(float x) const noexcept
{
    return std::clamp(xToTick(x) / std::max(1, barTicks()), 0, totalBars() - 1);
}

void StylePartPianoRoll::notifyBarSelection()
{
    if (onBarSelectionChanged)
        onBarSelectionChanged(m_barSelection);
}

void StylePartPianoRoll::setBarSelection(bar_workflow::BarSelection selection)
{
    m_barSelection = selection;
    notifyBarSelection();
    repaint();
}

void StylePartPianoRoll::clearBarSelection()
{
    bar_workflow::clearSelection(m_barSelection);
    notifyBarSelection();
    repaint();
}

void StylePartPianoRoll::clearNoteSelection()
{
    m_noteSelection.clear();
    notifyNoteSelection();
    repaint();
}

note_workflow::NoteClipboard StylePartPianoRoll::copySelectedNotes() const
{
    return note_workflow::copySelected(m_notes, m_noteSelection);
}

void StylePartPianoRoll::pasteNotes(
    const note_workflow::NoteClipboard& clipboard, int tick, int pitch)
{
    auto pasted = note_workflow::pasteNotes(
        m_notes, clipboard, tick, pitch, m_sectionTicks);
    m_notes = std::move(pasted.notes);
    m_noteSelection = std::move(pasted.selection);
    notifyNoteSelection();
    commit();
    repaint();
}

void StylePartPianoRoll::duplicateSelectedNotes()
{
    auto duplicated = note_workflow::duplicateToRight(
        m_notes, m_noteSelection, m_sectionTicks);
    m_notes = std::move(duplicated.notes);
    m_noteSelection = std::move(duplicated.selection);
    notifyNoteSelection();
    commit();
    repaint();
}

void StylePartPianoRoll::deleteSelectedNotes()
{
    if (m_noteSelection.empty())
        return;
    m_notes = note_workflow::deleteSelected(m_notes, m_noteSelection);
    m_noteSelection.clear();
    notifyNoteSelection();
    commit();
    repaint();
}

void StylePartPianoRoll::replaceNotes(std::vector<PatternNote> notes)
{
    m_noteSelection.clear();
    notifyNoteSelection();
    m_notes = std::move(notes);
    commit();
    repaint();
}

void StylePartPianoRoll::notifyNoteSelection()
{
    if (onNoteSelectionChanged)
        onNoteSelectionChanged(m_noteSelection);
}

std::vector<note_workflow::NoteBounds> StylePartPianoRoll::visibleNoteBounds() const
{
    std::vector<note_workflow::NoteBounds> bounds;
    bounds.reserve(m_notes.size());
    for (int i = 0; i < (int) m_notes.size(); ++i) {
        const auto& note = m_notes[i];
        const float x0 = tickToX(note.tick);
        const float x1 = tickToX(std::min(m_sectionTicks, note.tick + note.duration));
        const float y = pitchToY(note.pitch);
        bounds.push_back({ i, x0, y, std::max(3.0f, x1 - x0),
                           (float) rowHeight() });
    }
    return bounds;
}
}
