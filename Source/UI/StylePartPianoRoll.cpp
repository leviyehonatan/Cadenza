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
    g.setColour(juce::Colour(0xffedf1f7));
    g.setFont(juce::Font(juce::FontOptions(12.5f)).boldened());
    if (barT > 0) {
        for (int t = 0; t < m_sectionTicks; t += barT) {
            const float x = tickToX(t);
            g.drawText(juce::String(piano_roll::measureNumberAtTick(
                           t, m_ticksPerBeat, m_beatsPerBar)),
                       (int) x + 5, 2, 40, kRulerH - 4,
                       juce::Justification::centredLeft, false);
        }
    }

    // Notes.
    for (const auto& n : m_notes) {
        const float x0 = tickToX(n.tick);
        const float x1 = tickToX(std::min(m_sectionTicks, n.tick + n.duration));
        const float y = pitchToY(n.pitch);
        if (y + rowH < kRulerH || y > getHeight()) continue;   // off-screen
        juce::Rectangle<float> r(x0 + 1.0f, y + 2.0f,
                                 std::max(3.0f, x1 - x0 - 2.0f),
                                 (float) (rowH - 4));
        const float vel = std::clamp(n.velocity / 127.0f, 0.2f, 1.0f);
        g.setColour(juce::Colour(0xff3da5ff).withBrightness(0.55f + 0.45f * vel));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawRoundedRectangle(r, 3.0f, 1.0f);
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
    if (p.y < kRulerH) return;   // ruler

    bool onRightEdge = false;
    const int idx = noteIndexAt(p, onRightEdge);

    // Right-click (or popup modifier) deletes the note under the cursor.
    if (e.mods.isRightButtonDown() || e.mods.isPopupMenu()) {
        if (idx >= 0) {
            m_notes.erase(m_notes.begin() + idx);
            commit();
            repaint();
        }
        return;
    }

    m_lastAuditioned = -1;
    if (idx >= 0) {
        m_dragIndex = idx;
        if (onRightEdge) {
            m_drag = Drag::ResizeR;
        } else {
            m_drag = Drag::Move;
            m_grabTickOffset = xToTick(p.x) - m_notes[idx].tick;
        }
        auditionPitch(m_notes[idx].pitch);
        return;
    }

    // Empty grid -> create a note (drag extends its length).
    PatternNote n;
    n.tick = snapTick(xToTick(p.x));
    n.pitch = yToPitch(p.y);
    n.velocity = 100;
    n.duration = std::max(gridTicks(), m_lastDrawDuration);
    if (n.tick + n.duration > m_sectionTicks)
        n.duration = std::max(gridTicks(), m_sectionTicks - n.tick);
    m_notes.push_back(n);
    m_dragIndex = (int) m_notes.size() - 1;
    m_drag = Drag::Create;
    auditionPitch(n.pitch);
    repaint();
}

void StylePartPianoRoll::mouseDrag(const juce::MouseEvent& e)
{
    if (m_drag == Drag::None || m_dragIndex < 0 || m_dragIndex >= (int) m_notes.size())
        return;
    auto& n = m_notes[m_dragIndex];
    const juce::Point<float> p = e.position;

    if (m_drag == Drag::Move) {
        const int newTick = snapTick(xToTick(p.x) - m_grabTickOffset);
        n.tick = std::clamp(newTick, 0, std::max(0, m_sectionTicks - gridTicks()));
        if (n.tick + n.duration > m_sectionTicks)
            n.duration = std::max(gridTicks(), m_sectionTicks - n.tick);
        const int newPitch = yToPitch(p.y);
        if (newPitch != n.pitch) { n.pitch = newPitch; auditionPitch(newPitch); }
    } else {   // Create or ResizeR
        const int end = snapTick(xToTick(p.x));
        n.duration = std::clamp(end - n.tick, gridTicks(), m_sectionTicks - n.tick);
    }
    repaint();
}

void StylePartPianoRoll::mouseUp(const juce::MouseEvent&)
{
    if (m_drag != Drag::None && m_dragIndex >= 0 && m_dragIndex < (int) m_notes.size()) {
        if (m_drag == Drag::Create || m_drag == Drag::ResizeR)
            m_lastDrawDuration = m_notes[m_dragIndex].duration;
        commit();
    }
    m_drag = Drag::None;
    m_dragIndex = -1;
    m_lastAuditioned = -1;
}

void StylePartPianoRoll::mouseMove(const juce::MouseEvent& e)
{
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
}
