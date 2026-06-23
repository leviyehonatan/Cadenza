#include "StylePartPianoRoll.h"
#include "../MusicalTiming.h"

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

juce::String tickPosition(int tick, int ticksPerBar, int ticksPerBeat)
{
    const int barTicks = std::max(1, ticksPerBar);
    const int beatTicks = std::max(1, ticksPerBeat);
    const int clampedTick = std::max(0, tick);
    const int bar = clampedTick / barTicks;
    const int withinBar = clampedTick - bar * barTicks;
    const int beat = withinBar / beatTicks;
    const int tickInBeat = withinBar - beat * beatTicks;
    return juce::String(bar + 1) + ":" + juce::String(beat + 1)
        + ":" + juce::String(tickInBeat);
}

bool sameNotes(const std::vector<PatternNote>& a,
               const std::vector<PatternNote>& b) noexcept
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].tick != b[i].tick
            || a[i].duration != b[i].duration
            || a[i].pitch != b[i].pitch
            || a[i].velocity != b[i].velocity)
            return false;
    }
    return true;
}
}

StylePartPianoRoll::StylePartPianoRoll()
{
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
}

void StylePartPianoRoll::setPart(const std::vector<PatternNote>& notes,
                                 int sectionTicks, int ticksPerBeat, int beatsPerBar,
                                 int beatUnit, bool percussion)
{
    const bool sameTiming = m_sectionTicks == std::max(1, sectionTicks)
        && m_ticksPerBeat == std::max(24, ticksPerBeat)
        && m_beatsPerBar == std::max(1, beatsPerBar)
        && m_beatUnit == std::max(1, beatUnit)
        && m_percussion == percussion;
    const bool sameContent = sameNotes(m_notes, notes);
    const bool preserveEditorState = sameTiming
        && (sameContent || m_preserveHistoryOnNextSetPart);
    m_preserveHistoryOnNextSetPart = false;

    m_notes = notes;
    m_sectionTicks = std::max(1, sectionTicks);
    m_ticksPerBeat = std::max(24, ticksPerBeat);
    m_beatsPerBar = std::max(1, beatsPerBar);
    m_beatUnit = std::max(1, beatUnit);
    m_percussion = percussion;
    bar_workflow::clearSelection(m_barSelection);
    m_noteSelection.clear();
    if (!preserveEditorState) {
        m_undoStack.clear();
        m_redoStack.clear();
        m_velocityUndoArmed = false;
        m_horizontalZoom = 1.0f;
        m_horizontalScrollTick = 0.0f;
        // Sensible default register: GM drums vs. the playing range.
        m_topNote = percussion ? 60 : 84;
    }
    m_lastDrawDuration = gridTicks();
    invalidateGridCache();
    invalidateNoteBoundsCache();
    invalidateSelectionCache();
    clampScroll();
    clampHorizontalScroll();
    repaint();
}

void StylePartPianoRoll::setSnapDivision(int division)
{
    m_snap = std::max(0, division);
    m_lastDrawDuration = gridTicks();
    invalidateGridCache();
    repaint();
}

void StylePartPianoRoll::setPlaybackTick(int tickInSection, bool visible)
{
    const int previousTick = m_playbackTick;
    const bool previousVisible = m_playbackVisible;
    m_playbackTick = tickInSection;
    m_playbackVisible = visible;
    if (previousVisible)
        repaintPlayheadAtTick(previousTick);
    if (m_playbackVisible)
        repaintPlayheadAtTick(m_playbackTick);
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

int StylePartPianoRoll::barTicks() const noexcept
{
    return std::max(1, cadenza::ticksPerBar(m_ticksPerBeat, m_beatsPerBar, m_beatUnit));
}
int StylePartPianoRoll::beatTicks() const noexcept
{
    return std::max(1, cadenza::ticksPerNotatedBeat(m_ticksPerBeat, m_beatUnit));
}
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
                               static_cast<float>(getWidth()),
                               m_horizontalZoom, m_horizontalScrollTick);
}

int StylePartPianoRoll::xToTick(float x) const noexcept
{
    return piano_roll::xToTick(x, m_sectionTicks,
                               static_cast<float>(gridLeft()),
                               static_cast<float>(getWidth()),
                               m_horizontalZoom, m_horizontalScrollTick);
}

int StylePartPianoRoll::visibleStartTick() const noexcept
{
    return xToTick(static_cast<float>(gridLeft()));
}

int StylePartPianoRoll::visibleEndTick() const noexcept
{
    return xToTick(static_cast<float>(getWidth()));
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
    clampHorizontalScroll();
}

void StylePartPianoRoll::clampHorizontalScroll() noexcept
{
    const float oldZoom = m_horizontalZoom;
    const float oldScroll = m_horizontalScrollTick;
    m_horizontalZoom = std::clamp(m_horizontalZoom, 1.0f, 32.0f);
    const float visibleTicks = m_sectionTicks / std::max(1.0f, m_horizontalZoom);
    const float maximumScroll = std::max(0.0f, m_sectionTicks - visibleTicks);
    m_horizontalScrollTick = std::clamp(m_horizontalScrollTick, 0.0f, maximumScroll);
    if (oldZoom != m_horizontalZoom || oldScroll != m_horizontalScrollTick) {
        invalidateGridCache();
        invalidateNoteBoundsCache();
    }
}

void StylePartPianoRoll::setHorizontalZoom(float zoom, float anchorX) noexcept
{
    const int anchorTick = xToTick(anchorX);
    m_horizontalZoom = std::clamp(zoom, 1.0f, 32.0f);
    const float width = std::max(1.0f, static_cast<float>(getWidth() - gridLeft()));
    const float pixelsPerTick = (width / static_cast<float>(std::max(1, m_sectionTicks)))
        * m_horizontalZoom;
    m_horizontalScrollTick = anchorTick - (anchorX - gridLeft()) / pixelsPerTick;
    clampHorizontalScroll();
    invalidateGridCache();
    invalidateNoteBoundsCache();
}

void StylePartPianoRoll::panHorizontalTicks(float deltaTicks) noexcept
{
    m_horizontalScrollTick += deltaTicks;
    clampHorizontalScroll();
    invalidateGridCache();
    invalidateNoteBoundsCache();
}

int StylePartPianoRoll::noteIndexAt(juce::Point<float> p, NoteEdge& edge) const
{
    edge = NoteEdge::None;
    ensureNoteBoundsCache();
    for (int i = (int) m_cachedNoteBounds.size() - 1; i >= 0; --i) {
        const auto& bounds = m_cachedNoteBounds[(std::size_t) i];
        const juce::Rectangle<float> r(bounds.x, bounds.y, bounds.width, bounds.height);
        if (r.contains(p)) {
            if (p.x <= r.getX() + kEdgeGrab)
                edge = NoteEdge::Left;
            else if (p.x >= r.getRight() - kEdgeGrab)
                edge = NoteEdge::Right;
            return bounds.index;
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
    ensureGridCache();
    ensureNoteBoundsCache();
    ensureSelectionCache();

    // Alternating measures make long patterns easier to scan.
    for (const auto& band : m_cachedBarBands) {
        g.setColour(juce::Colour(0xff151a21).withAlpha(0.18f));
        g.fillRect(band.x0, (float) kRulerH, band.x1 - band.x0,
                   (float) (getHeight() - kRulerH));
    }

    // Row backgrounds + horizontal separators.
    for (int row = 0; row < rows; ++row) {
        const int pitch = piano_roll::pitchForRow(row, m_topNote);
        const float y = (float) (kRulerH + row * rowH);
        if (m_percussion)
            g.setColour((row & 1) ? juce::Colour(0xff242a34)
                                  : juce::Colour(0xff303743));
        else
            g.setColour(isBlackKey(pitch) ? juce::Colour(0xff262b34)
                                          : juce::Colour(0xff2c323c));
        g.fillRect((float) gutterRight, y, (float) (gridRight - gutterRight), (float) rowH);
        g.setColour(m_percussion ? juce::Colour(0xff495260)
                                 : juce::Colour(0xff343b47).withAlpha(0.45f));
        g.fillRect((float) gutterRight, y + rowH - 1,
                   (float) (gridRight - gutterRight), 1.0f);
        if (!m_percussion && ((pitch % 12) + 12) % 12 == 0) {   // brighter line under each C
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
    for (const auto& line : m_cachedGridLines) {
        float width = 1.0f;
        if (line.kind == piano_roll::GridLineKind::Bar) {
            g.setColour(juce::Colour(0xff738097));
            width = 2.0f;
        } else if (line.kind == piano_roll::GridLineKind::Beat) {
            g.setColour(juce::Colour(0xff4d5868));
            width = 1.25f;
        } else {
            g.setColour(juce::Colour(0xff343b47));
        }
        g.fillRect(line.x, (float) kRulerH, width,
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
    for (const auto& rulerBar : m_cachedRulerBars) {
        g.drawText(juce::String(rulerBar.measure),
                   (int) rulerBar.x0, 2,
                   std::max(1, (int) (rulerBar.x1 - rulerBar.x0)), kRulerH - 4,
                   juce::Justification::centred, false);
    }

    // Notes.
    const auto clip = g.getClipBounds().toFloat();
    for (const auto& bounds : m_cachedNoteBounds) {
        const int i = bounds.index;
        if (i < 0 || i >= (int) m_notes.size())
            continue;
        const juce::Rectangle<float> noteClipBounds(
            bounds.x, bounds.y, bounds.width, bounds.height);
        if (!clip.intersects(noteClipBounds))
            continue;
        const auto& n = m_notes[i];
        juce::Rectangle<float> r(bounds.x + 1.0f, bounds.y + 2.0f,
                                 std::max(3.0f, bounds.width - 2.0f),
                                 (float) (rowH - 4));
        const float vel = std::clamp(n.velocity / 127.0f, 0.2f, 1.0f);
        const bool selected = i < (int) m_noteSelectionFlags.size()
            && m_noteSelectionFlags[(std::size_t) i];
        const bool hovered = i == m_hoverIndex;
        const float brightness = std::clamp(
            0.55f + 0.45f * vel + (hovered ? 0.10f : 0.0f), 0.0f, 1.0f);
        g.setColour(selected
            ? juce::Colour(0xffffa43a)
            : juce::Colour(0xff3da5ff).withBrightness(brightness));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(selected ? juce::Colour(0xffffe0ad)
                             : (hovered ? juce::Colour(0xffb9dcff)
                                        : juce::Colours::black.withAlpha(0.5f)));
        g.drawRoundedRectangle(r, 3.0f, selected || hovered ? 2.0f : 1.0f);
        if (hovered && m_hoverEdge != NoteEdge::None) {
            const float edgeX = m_hoverEdge == NoteEdge::Left ? r.getX() : r.getRight();
            g.setColour(juce::Colour(0xffffffff).withAlpha(0.72f));
            g.fillRect(edgeX - 1.0f, r.getY() + 1.0f, 2.0f, r.getHeight() - 2.0f);
        }
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
            static_cast<float>(gutterRight), static_cast<float>(getWidth()),
            m_horizontalZoom, m_horizontalScrollTick);
        if (x >= gutterRight && x <= gridRight) {
            g.setColour(juce::Colour(0xffff9f32));
            g.fillRect(x - 1.0f, 0.0f, 2.0f, (float) getHeight());
            juce::Path marker;
            marker.addTriangle(x - 5.0f, 0.0f, x + 5.0f, 0.0f, x, 7.0f);
            g.fillPath(marker);
        }
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

    if (m_dragTooltipVisible && m_dragTooltipText.isNotEmpty()) {
        const auto bounds = dragTooltipBounds().toFloat();
        g.setColour(juce::Colour(0xff10141a).withAlpha(0.94f));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colour(0xfff0f4fb));
        g.setFont(juce::Font(juce::FontOptions(11.0f)).boldened());
        g.drawText(m_dragTooltipText, bounds.toNearestInt().reduced(7, 3),
                   juce::Justification::centredLeft, true);
    }
}

void StylePartPianoRoll::resized()
{
    invalidateGridCache();
    invalidateNoteBoundsCache();
    clampScroll();
}

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
    grabKeyboardFocus();
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
            invalidateSelectionCache();
            notifyNoteSelection();
            notifyBarSelection();
            repaint();
            if (onBarContextMenuRequested)
                onBarContextMenuRequested(e.getPosition());
            return;
        }
        m_noteSelection.clear();
        invalidateSelectionCache();
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

    NoteEdge edge = NoteEdge::None;
    const int idx = noteIndexAt(p, edge);

    // Right-click (or popup modifier) deletes the note under the cursor.
    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
        if (idx >= 0) {
            pushUndoSnapshot();
            if (m_noteSelection.contains(idx) && m_noteSelection.size() > 1)
                m_notes = note_workflow::deleteSelected(m_notes, m_noteSelection);
            else
                m_notes.erase(m_notes.begin() + idx);
            m_noteSelection.clear();
            invalidateNoteBoundsCache();
            invalidateSelectionCache();
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
            invalidateSelectionCache();
            notifyNoteSelection();
            repaint();
            return;
        }
        if (!wasSelected) {
            note_workflow::selectOnly(m_noteSelection, idx);
            invalidateSelectionCache();
        }
        notifyNoteSelection();

        if ((e.mods.isAltDown() || e.mods.isShiftDown()) && edge == NoteEdge::None) {
            pushUndoSnapshot();
            m_dragSnapshotPushed = true;
            const int originalSize = (int) m_notes.size();
            const int selectionOffset = (int) std::distance(
                m_noteSelection.begin(), m_noteSelection.find(idx));
            auto duplicated = note_workflow::duplicateSelected(m_notes, m_noteSelection);
            m_notes = std::move(duplicated.notes);
            m_noteSelection = std::move(duplicated.selection);
            invalidateNoteBoundsCache();
            invalidateSelectionCache();
            m_dragIndex = originalSize + selectionOffset;
        } else {
            m_dragSnapshotPushed = false;
            m_dragIndex = idx;
        }
        m_noteDragStartNotes = m_notes;
        m_dragSelectionIndices = note_workflow::selectedIndices(
            m_noteSelection, (int) m_notes.size());
        m_dragStartTick = m_notes[m_dragIndex].tick;
        m_dragStartPitch = m_notes[m_dragIndex].pitch;
        m_resizeStartDuration = m_notes[m_dragIndex].duration;
        if (edge == NoteEdge::Left) {
            m_drag = Drag::ResizeL;
        } else if (edge == NoteEdge::Right) {
            m_drag = Drag::ResizeR;
        } else {
            m_drag = Drag::Move;
            m_grabTickOffset = xToTick(p.x) - m_notes[m_dragIndex].tick;
        }
        updateDragTooltipForNote(m_dragIndex, p);
        auditionPitch(m_notes[m_dragIndex].pitch);
        clearBarSelection();
        repaint();
        return;
    }

    clearBarSelection();
    m_emptyDragStart = p;
    m_selectionRectangle = {};
    m_drag = Drag::PendingEmpty;
    PatternNote preview;
    preview.tick = snapTick(xToTick(p.x));
    preview.pitch = yToPitch(p.y);
    preview.velocity = 100;
    preview.duration = std::max(gridTicks(), m_lastDrawDuration);
    if (preview.tick + preview.duration > m_sectionTicks)
        preview.duration = std::max(gridTicks(), m_sectionTicks - preview.tick);
    m_dragTooltipText = noteName(preview.pitch) + " "
        + tickPosition(preview.tick, barTicks(), beatTicks())
        + " " + juce::String(preview.duration) + "t"
        + " Vel " + juce::String(preview.velocity);
    m_dragTooltipPosition = p;
    m_dragTooltipVisible = true;
    repaintDragTooltip();
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
            invalidateNoteBoundsCache();
            m_rulerMoveDelta = m_barSelection.first - m_rulerStartSelection.first;
        }
        notifyBarSelection();
        repaint();
        return;
    }

    if (m_drag == Drag::PendingEmpty || m_drag == Drag::BoxSelect) {
        const auto oldTooltip = dragTooltipBounds();
        if (m_drag == Drag::PendingEmpty
            && e.position.getDistanceFrom(m_emptyDragStart) >= 4.0f) {
            m_drag = Drag::BoxSelect;
            clearDragTooltip();
            m_noteSelection.clear();
            invalidateSelectionCache();
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
            invalidateSelectionCache();
            notifyNoteSelection();
            repaint();
        } else {
            m_dragTooltipPosition = e.position;
            repaintDragTooltip(oldTooltip);
        }
        return;
    }

    if (m_drag == Drag::None || m_dragIndex < 0 || m_dragIndex >= (int) m_notes.size())
        return;
    const juce::Point<float> p = e.position;
    const auto oldDirty = noteDirtyBoundsForIndices(m_dragSelectionIndices);
    const auto oldTooltip = dragTooltipBounds();

    if (m_drag == Drag::Move) {
        const int newTick = snapTick(xToTick(p.x) - m_grabTickOffset);
        const int newPitch = yToPitch(p.y);
        note_workflow::moveSelectedInPlace(
            m_notes, m_noteDragStartNotes, m_dragSelectionIndices,
            newTick - m_dragStartTick, newPitch - m_dragStartPitch,
            m_sectionTicks);
        auditionPitch(m_notes[m_dragIndex].pitch);
    } else {
        const int edgeTick = snapTick(xToTick(p.x));
        if (m_drag == Drag::ResizeL) {
            note_workflow::resizeSelectedLeftInPlace(
                m_notes, m_noteDragStartNotes, m_dragSelectionIndices,
                edgeTick - m_dragStartTick, gridTicks(), m_sectionTicks);
        } else {
            note_workflow::resizeSelectedInPlace(
                m_notes, m_noteDragStartNotes, m_dragSelectionIndices,
                (edgeTick - m_notes[m_dragIndex].tick) - m_resizeStartDuration,
                gridTicks(), m_sectionTicks);
        }
    }
    invalidateNoteBoundsCache();
    const auto newDirty = noteDirtyBoundsForIndices(m_dragSelectionIndices);
    auto dirty = oldDirty.isEmpty()
        ? newDirty : (newDirty.isEmpty() ? oldDirty : oldDirty.getUnion(newDirty));
    updateDragTooltipForNote(m_dragIndex, p);
    const auto newTooltip = dragTooltipBounds();
    if (!oldTooltip.isEmpty())
        dirty = dirty.isEmpty() ? oldTooltip : dirty.getUnion(oldTooltip);
    if (!newTooltip.isEmpty())
        dirty = dirty.isEmpty() ? newTooltip : dirty.getUnion(newTooltip);
    if (!dirty.isEmpty())
        repaint(dirty);
}

void StylePartPianoRoll::mouseUp(const juce::MouseEvent&)
{
    const auto oldTooltip = dragTooltipBounds();
    if (m_rulerGesture != RulerGesture::None) {
        if (m_rulerGesture == RulerGesture::Move) {
            if (m_rulerMoveDelta != 0) {
                pushUndoSnapshot(m_rulerStartNotes);
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
        clearDragTooltip();
        repaintDragTooltip(oldTooltip);
        return;
    }

    if (m_drag == Drag::PendingEmpty) {
        pushUndoSnapshot();
        PatternNote note;
        note.tick = snapTick(xToTick(m_emptyDragStart.x));
        note.pitch = yToPitch(m_emptyDragStart.y);
        note.velocity = 100;
        note.duration = std::max(gridTicks(), m_lastDrawDuration);
        if (note.tick + note.duration > m_sectionTicks)
            note.duration = std::max(gridTicks(), m_sectionTicks - note.tick);
        m_notes.push_back(note);
        note_workflow::selectOnly(m_noteSelection, (int) m_notes.size() - 1);
        invalidateNoteBoundsCache();
        invalidateSelectionCache();
        notifyNoteSelection();
        auditionPitch(note.pitch);
        commit();
        repaint(noteDirtyBoundsForIndex((int) m_notes.size() - 1));
    } else if (m_drag == Drag::BoxSelect) {
        const auto dirty = m_selectionRectangle.getSmallestIntegerContainer().expanded(2);
        m_selectionRectangle = {};
        repaint(dirty);
    } else if (m_drag != Drag::None && m_dragIndex >= 0
               && m_dragIndex < (int) m_notes.size()) {
        if (m_drag == Drag::ResizeL || m_drag == Drag::ResizeR)
            m_lastDrawDuration = m_notes[m_dragIndex].duration;
        if (!m_dragSnapshotPushed && !sameNotes(m_noteDragStartNotes, m_notes))
            pushUndoSnapshot(m_noteDragStartNotes);
        commit();
    }
    m_drag = Drag::None;
    m_dragIndex = -1;
    m_dragSnapshotPushed = false;
    m_lastAuditioned = -1;
    m_noteDragStartNotes.clear();
    m_dragSelectionIndices.clear();
    clearDragTooltip();
    repaintDragTooltip(oldTooltip);
}

void StylePartPianoRoll::mouseMove(const juce::MouseEvent& e)
{
    if (e.position.x >= gridLeft() && e.position.y >= kRulerH
        && onGridMousePositionChanged)
        onGridMousePositionChanged(xToTick(e.position.x), yToPitch(e.position.y));
    NoteEdge edge = NoteEdge::None;
    const int idx = noteIndexAt(e.position, edge);
    if (idx != m_hoverIndex || edge != m_hoverEdge) {
        const auto oldDirty = noteDirtyBoundsForIndex(m_hoverIndex);
        m_hoverIndex = idx;
        m_hoverEdge = edge;
        const auto newDirty = noteDirtyBoundsForIndex(m_hoverIndex);
        const auto dirty = oldDirty.isEmpty()
            ? newDirty : (newDirty.isEmpty() ? oldDirty : oldDirty.getUnion(newDirty));
        if (!dirty.isEmpty())
            repaint(dirty);
    }
    setMouseCursor(idx >= 0 && edge != NoteEdge::None
        ? juce::MouseCursor::LeftRightResizeCursor
        : juce::MouseCursor::NormalCursor);
}

void StylePartPianoRoll::mouseWheelMove(const juce::MouseEvent& e,
                                        const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) {
        const float amount = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
        if (amount != 0.0f) {
            const float factor = std::pow(1.20f, amount * 4.0f);
            setHorizontalZoom(m_horizontalZoom * factor, e.position.x);
            repaint();
        }
        return;
    }

    if (e.mods.isShiftDown()) {
        const float amount = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
        if (amount != 0.0f) {
            const float visibleTicks = m_sectionTicks / std::max(1.0f, m_horizontalZoom);
            panHorizontalTicks(-amount * visibleTicks * 0.25f);
            repaint();
        }
        return;
    }

    const int step = (int) std::lround(wheel.deltaY * 6.0f);
    if (step != 0) {
        m_topNote += step;
        invalidateNoteBoundsCache();
        clampScroll();
        repaint();
    }
}

void StylePartPianoRoll::commit()
{
    if (onNotesEdited) {
        m_preserveHistoryOnNextSetPart = true;
        onNotesEdited(m_notes);
        if (m_preserveHistoryOnNextSetPart)
            m_preserveHistoryOnNextSetPart = false;
    }
}

bool StylePartPianoRoll::keyPressed(const juce::KeyPress& key)
{
    return onKeyPressed ? onKeyPressed(key) : false;
}

float StylePartPianoRoll::xForTick(int tick) const noexcept
{
    return tickToX(tick);
}

int StylePartPianoRoll::tickForX(float x) const noexcept
{
    return xToTick(x);
}

void StylePartPianoRoll::pushUndoSnapshot()
{
    pushUndoSnapshot(m_notes);
}

void StylePartPianoRoll::pushUndoSnapshot(const std::vector<PatternNote>& snapshot)
{
    if (!m_undoStack.empty() && sameNotes(m_undoStack.back(), snapshot))
        return;
    m_undoStack.push_back(snapshot);
    if ((int) m_undoStack.size() > kMaxUndoSnapshots)
        m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
}

void StylePartPianoRoll::restoreNoteSnapshot(const std::vector<PatternNote>& snapshot)
{
    m_notes = snapshot;
    m_noteSelection.clear();
    invalidateNoteBoundsCache();
    invalidateSelectionCache();
    notifyNoteSelection();
    commit();
    repaint();
}

bool StylePartPianoRoll::undo()
{
    if (m_undoStack.empty())
        return false;
    m_velocityUndoArmed = false;
    m_redoStack.push_back(m_notes);
    const auto snapshot = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    restoreNoteSnapshot(snapshot);
    return true;
}

bool StylePartPianoRoll::redo()
{
    if (m_redoStack.empty())
        return false;
    m_velocityUndoArmed = false;
    m_undoStack.push_back(m_notes);
    if ((int) m_undoStack.size() > kMaxUndoSnapshots)
        m_undoStack.erase(m_undoStack.begin());
    const auto snapshot = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    restoreNoteSnapshot(snapshot);
    return true;
}

void StylePartPianoRoll::setNoteVelocity(int index, int velocity)
{
    if (index < 0 || index >= static_cast<int>(m_notes.size()))
        return;
    const int clamped = std::clamp(velocity, 1, 127);
    if (m_notes[index].velocity == clamped)
        return;
    if (!m_velocityUndoArmed) {
        pushUndoSnapshot();
        m_velocityUndoArmed = true;
    }
    m_notes[index].velocity = clamped;
    repaintVelocityNote(index);
}

void StylePartPianoRoll::commitNotes()
{
    m_velocityUndoArmed = false;
    commit();
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
    invalidateSelectionCache();
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
    if (sameNotes(m_notes, pasted.notes))
        return;
    pushUndoSnapshot();
    m_notes = std::move(pasted.notes);
    m_noteSelection = std::move(pasted.selection);
    invalidateNoteBoundsCache();
    invalidateSelectionCache();
    notifyNoteSelection();
    commit();
    repaint();
}

void StylePartPianoRoll::duplicateSelectedNotes()
{
    auto duplicated = note_workflow::duplicateToRight(
        m_notes, m_noteSelection, m_sectionTicks);
    if (sameNotes(m_notes, duplicated.notes))
        return;
    pushUndoSnapshot();
    m_notes = std::move(duplicated.notes);
    m_noteSelection = std::move(duplicated.selection);
    invalidateNoteBoundsCache();
    invalidateSelectionCache();
    notifyNoteSelection();
    commit();
    repaint();
}

void StylePartPianoRoll::deleteSelectedNotes()
{
    if (m_noteSelection.empty())
        return;
    pushUndoSnapshot();
    m_notes = note_workflow::deleteSelected(m_notes, m_noteSelection);
    m_noteSelection.clear();
    invalidateNoteBoundsCache();
    invalidateSelectionCache();
    notifyNoteSelection();
    commit();
    repaint();
}

void StylePartPianoRoll::replaceNotes(std::vector<PatternNote> notes)
{
    if (sameNotes(m_notes, notes))
        return;
    pushUndoSnapshot();
    m_noteSelection.clear();
    invalidateSelectionCache();
    notifyNoteSelection();
    m_notes = std::move(notes);
    invalidateNoteBoundsCache();
    commit();
    repaint();
}

void StylePartPianoRoll::replaceNotesAndSelect(
    std::vector<PatternNote> notes,
    note_workflow::NoteSelection selection)
{
    for (auto it = selection.begin(); it != selection.end();) {
        if (*it < 0 || *it >= static_cast<int>(notes.size()))
            it = selection.erase(it);
        else
            ++it;
    }
    if (sameNotes(m_notes, notes)) {
        m_noteSelection = std::move(selection);
        invalidateSelectionCache();
        notifyNoteSelection();
        repaint();
        return;
    }
    pushUndoSnapshot();
    m_notes = std::move(notes);
    m_noteSelection = std::move(selection);
    invalidateNoteBoundsCache();
    invalidateSelectionCache();
    commit();
    notifyNoteSelection();
    repaint();
}

void StylePartPianoRoll::notifyNoteSelection()
{
    invalidateSelectionCache();
    if (onNoteSelectionChanged)
        onNoteSelectionChanged(m_noteSelection);
}

bool StylePartPianoRoll::isNoteSelected(int index) const
{
    ensureSelectionCache();
    return index >= 0 && index < (int) m_noteSelectionFlags.size()
        && m_noteSelectionFlags[(std::size_t) index];
}

void StylePartPianoRoll::invalidateGridCache() noexcept
{
    m_gridCacheValid = false;
}

void StylePartPianoRoll::invalidateNoteBoundsCache() noexcept
{
    m_noteBoundsCacheValid = false;
}

void StylePartPianoRoll::invalidateSelectionCache() noexcept
{
    m_selectionCacheValid = false;
}

void StylePartPianoRoll::ensureGridCache() const
{
    if (m_gridCacheValid)
        return;

    m_cachedBarBands.clear();
    m_cachedGridLines.clear();
    m_cachedRulerBars.clear();

    const int barT = barTicks();
    const int visibleStart = std::max(0, visibleStartTick());
    const int visibleEnd = std::min(m_sectionTicks, visibleEndTick());
    if (barT > 0) {
        const int firstBar = std::max(0, visibleStart / barT);
        const int lastBar = std::min(totalBars() - 1, visibleEnd / barT + 1);
        m_cachedBarBands.reserve((std::size_t) std::max(0, lastBar - firstBar + 1));
        m_cachedRulerBars.reserve((std::size_t) std::max(0, lastBar - firstBar + 1));
        for (int bar = firstBar; bar <= lastBar; ++bar) {
            const int t = bar * barT;
            const float x0 = tickToX(t);
            const float x1 = tickToX(std::min(m_sectionTicks, t + barT));
            if ((bar & 1) != 0)
                m_cachedBarBands.push_back({ x0, x1 });
            m_cachedRulerBars.push_back({
                piano_roll::measureNumberAtTick(t, beatTicks(), m_beatsPerBar),
                x0, x1
            });
        }
    }

    const int bT = beatTicks();
    const int gT = m_snap > 0 ? gridTicks() : bT;
    const int gridStep = std::max(1, gT);
    const int firstGridTick = std::max(0, (visibleStart / gridStep) * gridStep);
    for (int t = firstGridTick;
         t <= m_sectionTicks && t <= visibleEnd + gridStep;
         t += gridStep) {
        m_cachedGridLines.push_back({
            tickToX(t),
            piano_roll::classifyGridLine(t, beatTicks(), m_beatsPerBar, gT)
        });
    }

    m_gridCacheValid = true;
}

void StylePartPianoRoll::ensureNoteBoundsCache() const
{
    if (m_noteBoundsCacheValid)
        return;

    m_cachedNoteBounds.clear();
    m_cachedNoteBounds.reserve(m_notes.size());
    const int gutterRight = gridLeft();
    const int gridRight = getWidth();
    const int rowH = rowHeight();
    for (int i = 0; i < (int) m_notes.size(); ++i) {
        const auto& note = m_notes[i];
        const float x0 = tickToX(note.tick);
        const float x1 = tickToX(std::min(m_sectionTicks, note.tick + note.duration));
        const float y = pitchToY(note.pitch);
        const float width = std::max(3.0f, x1 - x0);
        if (y + rowH < kRulerH || y > getHeight())
            continue;
        if (x0 + width < gutterRight || x0 > gridRight)
            continue;
        m_cachedNoteBounds.push_back({ i, x0, y, width, (float) rowH });
    }
    m_noteBoundsCacheValid = true;
}

void StylePartPianoRoll::ensureSelectionCache() const
{
    if (m_selectionCacheValid && m_noteSelectionFlags.size() == m_notes.size())
        return;
    m_noteSelectionFlags.assign(m_notes.size(), false);
    for (const int index : m_noteSelection)
        if (index >= 0 && index < (int) m_noteSelectionFlags.size())
            m_noteSelectionFlags[(std::size_t) index] = true;
    m_selectionCacheValid = true;
}

const std::vector<note_workflow::NoteBounds>&
StylePartPianoRoll::visibleNoteBounds() const
{
    ensureNoteBoundsCache();
    return m_cachedNoteBounds;
}

juce::Rectangle<int> StylePartPianoRoll::noteDirtyBoundsForIndex(int index) const
{
    if (index < 0 || index >= (int) m_notes.size())
        return {};
    const auto& note = m_notes[(std::size_t) index];
    const float x0 = tickToX(note.tick);
    const float x1 = tickToX(std::min(m_sectionTicks, note.tick + note.duration));
    const float y = pitchToY(note.pitch);
    return juce::Rectangle<float>(
        x0 - 3.0f, y - 4.0f,
        std::max(3.0f, x1 - x0) + 6.0f,
        (float) rowHeight() + 8.0f).getSmallestIntegerContainer();
}

juce::Rectangle<int> StylePartPianoRoll::noteDirtyBoundsForIndices(
    const std::vector<int>& indices) const
{
    juce::Rectangle<int> dirty;
    bool hasDirty = false;
    for (const int index : indices) {
        const auto noteDirty = noteDirtyBoundsForIndex(index);
        if (!noteDirty.isEmpty()) {
            dirty = hasDirty ? dirty.getUnion(noteDirty) : noteDirty;
            hasDirty = true;
        }
    }
    return dirty;
}

juce::Rectangle<int> StylePartPianoRoll::playheadDirtyBoundsForTick(int tick) const
{
    if (tick < 0)
        return {};
    const float x = piano_roll::playheadX(
        tick, m_sectionTicks, static_cast<float>(gridLeft()),
        static_cast<float>(getWidth()), m_horizontalZoom, m_horizontalScrollTick);
    if (x < gridLeft() - 8.0f || x > getWidth() + 8.0f)
        return {};
    return juce::Rectangle<float>(
        x - 7.0f, 0.0f, 14.0f, (float) getHeight()).getSmallestIntegerContainer();
}

juce::Rectangle<int> StylePartPianoRoll::dragTooltipBounds() const
{
    if (!m_dragTooltipVisible || m_dragTooltipText.isEmpty())
        return {};

    juce::Font font(juce::FontOptions(11.0f));
    font = font.boldened();
    const int width = std::min(
        std::max(96, juce::GlyphArrangement::getStringWidthInt(font, m_dragTooltipText) + 18),
        std::max(96, getWidth() - 8));
    const int height = 24;
    int x = (int) std::round(m_dragTooltipPosition.x + 14.0f);
    int y = (int) std::round(m_dragTooltipPosition.y - height - 10.0f);
    if (x + width > getWidth() - 4)
        x = (int) std::round(m_dragTooltipPosition.x - width - 14.0f);
    if (y < 4)
        y = (int) std::round(m_dragTooltipPosition.y + 14.0f);
    x = std::clamp(x, 4, std::max(4, getWidth() - width - 4));
    y = std::clamp(y, 4, std::max(4, getHeight() - height - 4));
    return juce::Rectangle<int>(x, y, width, height).expanded(2);
}

void StylePartPianoRoll::updateDragTooltipForNote(
    int index, juce::Point<float> position)
{
    if (index < 0 || index >= (int) m_notes.size()) {
        clearDragTooltip();
        return;
    }

    const auto& note = m_notes[(std::size_t) index];
    m_dragTooltipText = noteName(note.pitch) + " "
        + tickPosition(note.tick, barTicks(), beatTicks())
        + " " + juce::String(note.duration) + "t"
        + " Vel " + juce::String(note.velocity);
    m_dragTooltipPosition = position;
    m_dragTooltipVisible = true;
}

void StylePartPianoRoll::clearDragTooltip()
{
    m_dragTooltipVisible = false;
    m_dragTooltipText = {};
}

void StylePartPianoRoll::repaintDragTooltip(
    const juce::Rectangle<int>& previousBounds)
{
    auto dirty = previousBounds;
    const auto current = dragTooltipBounds();
    if (!current.isEmpty())
        dirty = dirty.isEmpty() ? current : dirty.getUnion(current);
    if (!dirty.isEmpty())
        repaint(dirty);
}

void StylePartPianoRoll::repaintPlayheadAtTick(int tick)
{
    const auto dirty = playheadDirtyBoundsForTick(tick);
    if (!dirty.isEmpty())
        repaint(dirty);
}

void StylePartPianoRoll::repaintVelocityNote(int index)
{
    const auto dirty = noteDirtyBoundsForIndex(index);
    if (!dirty.isEmpty())
        repaint(dirty);
}
}
