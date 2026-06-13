#include "StylePartEditor.h"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

namespace cadenza::ui
{
namespace
{
constexpr int kContextCopy = 1;
constexpr int kContextPaste = 2;
constexpr int kContextDuplicate = 3;
constexpr int kContextClear = 4;
constexpr int kHelpShortcuts = 101;
constexpr int kHelpMouse = 102;
constexpr int kHelpAbout = 103;

bar_workflow::ShortcutKey shortcutKey(const juce::KeyPress& key)
{
    const int code = key.getKeyCode();
    if (code == juce::KeyPress::spaceKey) return bar_workflow::ShortcutKey::Space;
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
        return bar_workflow::ShortcutKey::DeleteKey;
    if (code == juce::KeyPress::escapeKey) return bar_workflow::ShortcutKey::Escape;

    auto character = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (character < 32)
        character = juce::CharacterFunctions::toLowerCase(
            static_cast<juce::juce_wchar>(code));
    switch (character) {
        case 'r': return bar_workflow::ShortcutKey::R;
        case 'c': return bar_workflow::ShortcutKey::C;
        case 'v': return bar_workflow::ShortcutKey::V;
        case 'd': return bar_workflow::ShortcutKey::D;
        default:  return bar_workflow::ShortcutKey::Other;
    }
}
}

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

            if (roll.barSelection().valid()) {
                const auto& selection = roll.barSelection();
                const float x0 = roll.xForTick(selection.first * roll.ticksPerBar());
                const float x1 = roll.xForTick(
                    std::min(roll.sectionTicks(),
                             (selection.last + 1) * roll.ticksPerBar()));
                g.setColour(juce::Colour(0xff4d8fe8).withAlpha(0.13f));
                g.fillRect(x0, 0.0f, x1 - x0, (float) getHeight());
            }

            const float baseline = static_cast<float>(getHeight() - 5);
            const float usableHeight = static_cast<float>(std::max(1, getHeight() - 24));
            for (int i = 0; i < (int) roll.notes().size(); ++i) {
                const auto& note = roll.notes()[i];
                const float x = roll.xForTick(note.tick);
                const float height = usableHeight * (note.velocity / 127.0f);
                g.setColour(roll.isNoteSelected(i)
                    ? juce::Colour(0xffffa43a) : juce::Colour(0xff4aa8f5));
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
            const int velocity = piano_roll::velocityAtY(
                y - 20.0f, static_cast<float>(std::max(1, getHeight() - 24)));
            roll.setNoteVelocity(selectedNote, velocity);
            repaint();
        }

        StylePartPianoRoll& roll;
        int selectedNote = -1;
    };

    explicit Content(StylePartEditorWindow::Callbacks& cb)
        : callbacks(cb), velocityLane(roll)
    {
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(true);
        roll.setWantsKeyboardFocus(true);

        addAndMakeVisible(roll);
        roll.onNotesEdited = [&cb](std::vector<cadenza::arranger::PatternNote> notes) {
            if (cb.onNotesEdited) cb.onNotesEdited(std::move(notes));
        };
        roll.onAudition = [&cb](int note, int velocity) {
            if (cb.onAudition) cb.onAudition(note, velocity);
        };
        roll.onBarSelectionChanged = [this](const bar_workflow::BarSelection&) {
            updateSelectionStatus();
            velocityLane.repaint();
        };
        roll.onNoteSelectionChanged = [this](const note_workflow::NoteSelection&) {
            updateSelectionStatus();
            velocityLane.repaint();
        };
        roll.onGridMousePositionChanged = [this](int tick, int pitch) {
            lastMouseTick = tick;
            lastMousePitch = pitch;
        };
        roll.onBarContextMenuRequested = [this](juce::Point<int> position) {
            showBarContextMenu(position);
        };

        configureButton(play, "Play", [this] {
            executeCommand(bar_workflow::EditorCommand::TogglePlay);
        });
        configureButton(record, "Record", [this] {
            executeCommand(bar_workflow::EditorCommand::ToggleRecord);
        });
        configureButton(copy, "Copy", [this] {
            executeCommand(bar_workflow::EditorCommand::Copy);
        });
        configureButton(paste, "Paste", [this] {
            executeCommand(bar_workflow::EditorCommand::Paste);
        });
        configureButton(duplicate, "Duplicate", [this] {
            executeCommand(bar_workflow::EditorCommand::Duplicate);
        });
        configureButton(clear, "Clear", [this] {
            executeCommand(bar_workflow::EditorCommand::Clear);
        });
        configureButton(help, "Help", [this] {
            showHelpMenu();
        });

        mode.addItem("Overdub", 1);
        mode.addItem("Replace", 2);
        mode.setSelectedId(1, juce::dontSendNotification);
        mode.setTooltip("Replace recording is not implemented; recording remains overdub");
        addAndMakeVisible(mode);

        snapLabel.setText("Grid", juce::dontSendNotification);
        snapLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        snapLabel.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(snapLabel);

        snap.addItem("1/4", 4);
        snap.addItem("1/8", 8);
        snap.addItem("1/16", 16);
        snap.addItem("1/32", 32);
        snap.addItem("Off", 1);
        snap.setSelectedId(16, juce::dontSendNotification);
        snap.onChange = [this] {
            const int id = snap.getSelectedId();
            roll.setSnapDivision(id == 1 ? 0 : id);
        };
        addAndMakeVisible(snap);

        selectionStatus.setColour(juce::Label::textColourId, juce::Colour(0xffaeb8c7));
        selectionStatus.setJustificationType(juce::Justification::centred);
        selectionStatus.setFont(juce::Font(juce::FontOptions(11.5f)).boldened());
        addAndMakeVisible(selectionStatus);

        position.setColour(juce::Label::textColourId, juce::Colour(0xffdce2eb));
        position.setJustificationType(juce::Justification::centredRight);
        position.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
        addAndMakeVisible(position);

        rec.setText("REC", juce::dontSendNotification);
        rec.setJustificationType(juce::Justification::centred);
        rec.setFont(juce::Font(juce::FontOptions(12.0f)).boldened());
        addAndMakeVisible(rec);
        updateRecColour(false);

        paste.setEnabled(false);
        addAndMakeVisible(velocityLane);
        updateSelectionStatus();
        setSize(1160, 680);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop(36);
        play.setBounds(bar.removeFromLeft(56).reduced(3));
        record.setBounds(bar.removeFromLeft(68).reduced(3));
        mode.setBounds(bar.removeFromLeft(92).reduced(3));
        copy.setBounds(bar.removeFromLeft(52).reduced(3));
        paste.setBounds(bar.removeFromLeft(54).reduced(3));
        duplicate.setBounds(bar.removeFromLeft(76).reduced(3));
        clear.setBounds(bar.removeFromLeft(52).reduced(3));
        help.setBounds(bar.removeFromLeft(48).reduced(3));
        bar.removeFromLeft(6);
        snapLabel.setBounds(bar.removeFromLeft(40));
        snap.setBounds(bar.removeFromLeft(76).reduced(2, 3));
        rec.setBounds(bar.removeFromRight(54).reduced(2, 3));
        position.setBounds(bar.removeFromRight(62));
        selectionStatus.setBounds(bar.removeFromRight(190).reduced(3));

        velocityLane.setBounds(area.removeFromBottom(84));
        roll.setBounds(area);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff1c1f26));
    }

    void setPart(const std::vector<cadenza::arranger::PatternNote>& notes,
                 int sectionTicks, int ticksPerBeat, int beatsPerBar, bool percussion)
    {
        roll.setPart(notes, sectionTicks, ticksPerBeat, beatsPerBar, percussion);
        updateSelectionStatus();
        velocityLane.repaint();
    }

    void setTransportState(int tickInSection, bool playing, bool recordArmed)
    {
        playbackTick = tickInSection;
        roll.setPlaybackTick(tickInSection, playing);
        play.setButtonText(playing ? "Stop" : "Play");
        record.setButtonText(recordArmed ? "Stop Rec" : "Record");
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

    bool keyPressed(const juce::KeyPress& key) override
    {
        const auto command = bar_workflow::commandForShortcut(
            shortcutKey(key), key.getModifiers().isCtrlDown());
        if (command == bar_workflow::EditorCommand::None)
            return false;
        executeCommand(command);
        return true;
    }

private:
    void configureButton(juce::TextButton& button, const juce::String& text,
                         std::function<void()> onClick)
    {
        button.setButtonText(text);
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff303744));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff315f9d));
        button.onClick = std::move(onClick);
        addAndMakeVisible(button);
    }

    void updateRecColour(bool armed)
    {
        rec.setColour(juce::Label::textColourId,
                      armed ? juce::Colour(0xffff5252) : juce::Colour(0xff727b89));
        rec.setColour(juce::Label::backgroundColourId,
                      armed ? juce::Colour(0xff531d24) : juce::Colour(0xff272c35));
    }

    void updateSelectionStatus()
    {
        const int selectedNotes = (int) roll.noteSelection().size();
        if (selectedNotes > 0) {
            selectionStatus.setText(
                juce::String(selectedNotes) + (selectedNotes == 1
                    ? " note selected" : " notes selected"),
                juce::dontSendNotification);
            selectionStatus.setColour(juce::Label::textColourId,
                                      juce::Colour(0xfffff0dc));
            selectionStatus.setColour(juce::Label::backgroundColourId,
                                      juce::Colour(0xff9b5b14));
            return;
        }
        const auto& selection = roll.barSelection();
        if (!selection.valid()) {
            selectionStatus.setText("Click or drag the bar ruler to select bars",
                                    juce::dontSendNotification);
            selectionStatus.setColour(juce::Label::textColourId,
                                      juce::Colour(0xff9aa5b5));
            selectionStatus.setColour(juce::Label::backgroundColourId,
                                      juce::Colour(0xff252b34));
            return;
        }
        const juce::String text = selection.first == selection.last
            ? "Bar " + juce::String(selection.first + 1) + " selected"
            : "Bars " + juce::String(selection.first + 1) + "-"
                + juce::String(selection.last + 1) + " selected";
        selectionStatus.setText(text, juce::dontSendNotification);
        selectionStatus.setColour(juce::Label::textColourId,
                                  juce::Colour(0xffeaf4ff));
        selectionStatus.setColour(juce::Label::backgroundColourId,
                                  juce::Colour(0xff285a96));
    }

    void showHelpMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(kHelpShortcuts, "Keyboard shortcuts");
        menu.addItem(kHelpMouse, "Mouse controls");
        menu.addSeparator();
        menu.addItem(kHelpAbout, "About editor");

        juce::Component::SafePointer<Content> safe(this);
        menu.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(&help),
            [safe](int result) {
                auto* content = safe.getComponent();
                if (content == nullptr)
                    return;
                if (result == kHelpShortcuts) {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        "Keyboard shortcuts",
                        "Space  Play/Stop\nR  Record\nCtrl+C  Copy selection\n"
                        "Ctrl+V  Paste\nCtrl+D  Duplicate\nDelete  Delete selection\n"
                        "Esc  Clear selection");
                } else if (result == kHelpMouse) {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        "Mouse controls",
                        "Click note  Select\nCtrl+click note  Add/remove selection\n"
                        "Drag empty grid  Box select\nDrag selected notes  Move\n"
                        "Shift+drag selected notes  Duplicate and move\n"
                        "Drag note right edge  Resize\nRight-click note  Delete");
                } else if (result == kHelpAbout) {
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        "About Part Editor",
                        "Cadenza Part Editor\nPiano-roll editing for melodic and drum parts.");
                }
            });
    }

    void showBarContextMenu(juce::Point<int> position)
    {
        juce::PopupMenu menu;
        menu.addItem(kContextCopy, "Copy", roll.barSelection().valid());
        menu.addItem(kContextPaste, "Paste", !clipboard.empty());
        menu.addItem(kContextDuplicate, "Duplicate", roll.barSelection().valid());
        menu.addSeparator();
        menu.addItem(kContextClear, "Clear", roll.barSelection().valid());

        juce::Component::SafePointer<Content> safe(this);
        const auto screenPoint = roll.localPointToGlobal(position);
        menu.showMenuAsync(
            juce::PopupMenu::Options()
                .withTargetComponent(&roll)
                .withTargetScreenArea({ screenPoint.x, screenPoint.y, 1, 1 }),
            [safe](int result) {
                auto* content = safe.getComponent();
                if (content == nullptr)
                    return;
                switch (result) {
                    case kContextCopy:
                        content->executeCommand(bar_workflow::EditorCommand::Copy);
                        break;
                    case kContextPaste:
                        content->executeCommand(bar_workflow::EditorCommand::Paste);
                        break;
                    case kContextDuplicate:
                        content->executeCommand(bar_workflow::EditorCommand::Duplicate);
                        break;
                    case kContextClear:
                        content->executeCommand(bar_workflow::EditorCommand::Clear);
                        break;
                    default:
                        break;
                }
            });
    }

    void executeCommand(bar_workflow::EditorCommand command)
    {
        const auto selection = roll.barSelection();
        switch (command) {
            case bar_workflow::EditorCommand::TogglePlay:
                if (callbacks.onTogglePlayback) callbacks.onTogglePlayback();
                break;
            case bar_workflow::EditorCommand::ToggleRecord:
                if (callbacks.onToggleRecord) callbacks.onToggleRecord();
                break;
            case bar_workflow::EditorCommand::Copy:
                if (!roll.noteSelection().empty()) {
                    noteClipboard = roll.copySelectedNotes();
                    clipboardKind = ClipboardKind::Notes;
                } else {
                    clipboard = bar_workflow::copyBars(
                        roll.notes(), selection, roll.ticksPerBar());
                    clipboardKind = ClipboardKind::Bars;
                }
                paste.setEnabled(
                    clipboardKind == ClipboardKind::Notes
                        ? !noteClipboard.empty() : !clipboard.empty());
                break;
            case bar_workflow::EditorCommand::Paste: {
                if (clipboardKind == ClipboardKind::Notes) {
                    if (noteClipboard.empty())
                        break;
                    const int tick = lastMouseTick >= 0 ? lastMouseTick : playbackTick;
                    roll.pasteNotes(noteClipboard, std::max(0, tick), lastMousePitch);
                    break;
                }
                if (clipboardKind != ClipboardKind::Bars || clipboard.empty())
                    break;
                const int destination = bar_workflow::resolvePasteBar(
                    selection, playbackTick, roll.totalBars(), roll.ticksPerBar());
                roll.replaceNotes(bar_workflow::pasteBars(
                    roll.notes(), clipboard, destination,
                    roll.totalBars(), roll.ticksPerBar()));
                const int last = std::min(
                    roll.totalBars() - 1, destination + clipboard.lengthBars - 1);
                roll.setBarSelection({ destination, destination, last });
                break;
            }
            case bar_workflow::EditorCommand::Duplicate: {
                if (!roll.noteSelection().empty()) {
                    roll.duplicateSelectedNotes();
                    break;
                }
                if (!selection.valid())
                    break;
                const int destination = selection.last + 1;
                if (destination >= roll.totalBars())
                    break;
                roll.replaceNotes(bar_workflow::duplicateBars(
                    roll.notes(), selection, roll.totalBars(), roll.ticksPerBar()));
                const int last = std::min(
                    roll.totalBars() - 1, destination + selection.length() - 1);
                roll.setBarSelection({ destination, destination, last });
                break;
            }
            case bar_workflow::EditorCommand::Clear:
                if (!roll.noteSelection().empty())
                    roll.deleteSelectedNotes();
                else if (selection.valid())
                    roll.replaceNotes(bar_workflow::clearBars(
                        roll.notes(), selection, roll.ticksPerBar()));
                break;
            case bar_workflow::EditorCommand::ClearSelection:
                roll.clearNoteSelection();
                roll.clearBarSelection();
                break;
            case bar_workflow::EditorCommand::None:
                break;
        }
        grabKeyboardFocus();
    }

    StylePartEditorWindow::Callbacks& callbacks;
    StylePartPianoRoll roll;
    VelocityLane velocityLane;
    juce::TextButton play, record, copy, paste, duplicate, clear, help;
    juce::ComboBox mode, snap;
    juce::Label snapLabel, selectionStatus, position, rec;
    bar_workflow::BarClipboard clipboard;
    note_workflow::NoteClipboard noteClipboard;
    enum class ClipboardKind { None, Bars, Notes };
    ClipboardKind clipboardKind = ClipboardKind::None;
    int playbackTick = -1;
    int lastMouseTick = -1;
    int lastMousePitch = -1;
};

StylePartEditorWindow::StylePartEditorWindow(Callbacks callbacks)
    : juce::DocumentWindow("Part Editor",
                           juce::Colour(0xff1c1f26),
                           juce::DocumentWindow::closeButton
                               | juce::DocumentWindow::minimiseButton
                               | juce::DocumentWindow::maximiseButton),
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

void StylePartEditorWindow::setPart(
    const juce::String& partLabel,
    const std::vector<cadenza::arranger::PatternNote>& notes,
    int sectionTicks, int ticksPerBeat, int beatsPerBar, bool percussion)
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

bool StylePartEditorWindow::keyPressed(const juce::KeyPress& key)
{
    return m_content && m_content->keyPressed(key);
}
}
