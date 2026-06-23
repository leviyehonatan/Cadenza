// StyleRecorder — record your own accompaniment patterns into a Style.
//
// The first-class way to create Cadenza styles without a Yamaha .sty file:
// pick a part (drums / bass / chord / pad / phrase), loop a section, play it
// on the keyboard, and the recorder bakes what you played into role-tagged
// PatternNotes (the same convention the .sty importer uses: record over a
// C-major source chord; the arranger then re-voices it for any live chord).
// Saved as .cstyle JSON via StyleLoader.
//
// Pure C++ (no JUCE), fully unit-testable. Thread-safe: noteOn/noteOff may be
// called from the MIDI thread while the message thread commits/saves.

#pragma once

#include "Style.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cadenza::arranger
{
// The recordable parts, on their standard SFF channels.
enum class RecorderPart
{
    Drums,     // ch 10 (percussion)
    Bass,      // ch 11
    Chord1,    // ch 12
    Chord2,    // ch 13
    Pad,       // ch 14
    Phrase1,   // ch 15
    Phrase2,   // ch 16
};
constexpr int kNumRecorderParts = 7;

struct RecorderPartInfo
{
    RecorderPart part;
    const char* label;       // "Drums", "Bass", ...
    const char* partName;    // style part name: "drums", "bass", ...
    int midiChannel;         // 10..16
    bool percussion;
};
const RecorderPartInfo& recorderPartInfo(RecorderPart part) noexcept;
const RecorderPartInfo& recorderPartInfo(int index) noexcept;   // 0..6
bool isEditableCadenzaStyle(const Style& style) noexcept;

// Convert an imported Yamaha style into an editable native Cadenza style:
// clears the Yamaha flag and remaps each section's parts onto the recorder's
// channels (all drum/percussion channels merged onto ch10; melodic parts onto
// ch11..16 in ascending-channel order; parts beyond the 7 slots are dropped).
// Names of any dropped parts are appended to `droppedParts` when provided.
// After this returns, isEditableCadenzaStyle(style) is true and loadSession
// accepts the style. No-op (safe) if the style is already editable.
void makeStyleEditable(Style& style, std::vector<std::string>* droppedParts = nullptr);

struct RecorderConfig
{
    std::string name = "My Style";
    int tempo = 120;
    int beatsPerBar = 4;
    int beatUnit = 4;
    int ticksPerBeat = 960;
    int bars = 4;
    std::string section = "mainA";
};

class StyleRecorder
{
public:
    // Begin a new recording session: a fresh one-section style skeleton.
    void startSession(const RecorderConfig& config);
    // Resume editing a native Cadenza style. Yamaha-derived styles remain
    // read-only because their imported multi-section semantics are not recorder-owned.
    bool loadSession(const Style& style, const std::string& sectionName = {});
    // Switch which section of the loaded multi-section style is being edited.
    // Returns false if there is no such section. Keeps the current target part and
    // updates the active bar count to the new section's length.
    bool setEditSection(const std::string& sectionName);
    void endSession();
    bool sessionActive() const;
    RecorderConfig config() const;

    // A copy of the in-progress style, ready for StyleEngine::setStyle.
    std::shared_ptr<const Style> snapshotStyle() const;

    void setTargetPart(RecorderPart part);
    RecorderPart targetPart() const;

    // Rename the in-progress style (e.g. from the save dialog's file name).
    void setStyleName(const std::string& name);

    // Quantize grid as notes-per-whole-note (16 = sixteenth-note grid).
    // 0 disables quantization. Note starts snap; durations are preserved.
    void setQuantizeDivision(int division);
    int quantizeDivision() const;

    int sectionLengthTicks() const;
    bool setBarCount(int bars);

    // Live capture. `absoluteTick` is the transport tick; it is folded into the
    // looping section internally. Notes released after the loop wraps get a
    // wrap-aware duration. Callable from the MIDI thread.
    void noteOn(int pitch, int velocity, int absoluteTick);
    void noteOff(int pitch, int absoluteTick);

    bool hasPendingTake() const;

    // Quantize the pending take, bake note roles (C-major source convention,
    // drums absolute), and merge it into the target part. Returns true if any
    // notes were added.
    bool commitTake();
    void discardTake();

    // Remove the target part's notes entirely. Returns true if it had any.
    bool clearTargetPart();
    bool targetPartHasNotes() const;

    // Replace the target part's notes wholesale (piano-roll editing). Roles are
    // re-baked from the new pitches; the part is created when missing. Empty
    // replacements keep the part metadata and clear notes/automation. Returns
    // the stored, normalized notes after quantize/merge.
    std::vector<PatternNote> replacePartNotes(std::vector<PatternNote> notes);

    // The target part's current notes (empty when none recorded yet).
    std::vector<PatternNote> targetPartNotes() const;

    // Write the current style as .cstyle JSON. Returns false on I/O failure.
    bool save(const std::string& path) const;

private:
    struct TakeNote
    {
        int pitch = 60;
        int velocity = 100;
        int startTick = 0;       // within the section
        int durationTicks = 1;
    };
    struct OpenNote
    {
        int startTick = 0;
        int velocity = 100;
    };

    Part& findOrCreateTargetPart();   // caller holds m_mutex
    Section* editableSection() noexcept;
    const Section* editableSection() const noexcept;

    mutable std::mutex m_mutex;
    bool m_active = false;
    RecorderConfig m_config;
    Style m_style;
    std::string m_sectionName = "mainA";
    RecorderPart m_target = RecorderPart::Drums;
    int m_quantizeDivision = 16;
    std::vector<TakeNote> m_take;
    OpenNote m_open[128];
    bool m_openActive[128] = {};
};
}
