#include "ArrangerMidiRouter.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace arranger {

namespace {

struct ChordTemplate {
    std::string_view quality;
    std::vector<std::uint8_t> intervals;
};

struct TemplateMatch {
    std::uint8_t root = 0;
    std::string_view quality;
};

const std::array<ChordTemplate, 18>& chordTemplates()
{
    static const std::array<ChordTemplate, 18> templates{{
        {"major", {0, 4, 7}},
        {"m", {0, 3, 7}},
        {"7", {0, 4, 7, 10}},
        {"maj7", {0, 4, 7, 11}},
        {"dim", {0, 3, 6}},
        {"aug", {0, 4, 8}},
        {"sus4", {0, 5, 7}},
        {"sus2", {0, 2, 7}},
        {"m7", {0, 3, 7, 10}},
        {"7", {0, 4, 10}},
        {"7(9)", {0, 2, 4, 10}},
        {"7(#11)", {0, 4, 6, 10}},
        {"7(13)", {0, 4, 9, 10}},
        {"7(b9)", {0, 1, 4, 10}},
        {"7(#9)", {0, 3, 4, 10}},
        {"sus4", {0, 5}},
        {"sus2", {0, 2}},
        {"m7", {0, 3, 10}},
    }};

    return templates;
}

std::string_view pitchClassName(std::uint8_t pitchClass) noexcept
{
    static constexpr std::array<std::string_view, 12> names{
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
    };

    return names[pitchClass % 12];
}

std::uint8_t pitchClass(std::uint8_t note) noexcept
{
    return static_cast<std::uint8_t>(note % 12);
}

std::vector<std::uint8_t> normalizedIntervals(
    const std::vector<std::uint8_t>& pitchClasses,
    std::uint8_t root)
{
    std::vector<std::uint8_t> intervals;
    intervals.reserve(pitchClasses.size());

    for (const auto pitchClassValue : pitchClasses) {
        intervals.push_back(static_cast<std::uint8_t>((pitchClassValue + 12u - root) % 12u));
    }

    std::sort(intervals.begin(), intervals.end());
    intervals.erase(std::unique(intervals.begin(), intervals.end()), intervals.end());
    return intervals;
}

std::optional<std::string_view> findTemplateQuality(const std::vector<std::uint8_t>& intervals)
{
    const auto& templates = chordTemplates();
    const auto match = std::find_if(
        templates.begin(),
        templates.end(),
        [&](const ChordTemplate& chordTemplate) {
            return chordTemplate.intervals == intervals;
        });

    if (match == templates.end()) {
        return std::nullopt;
    }

    return match->quality;
}

std::optional<TemplateMatch> matchChordTemplate(const std::vector<std::uint8_t>& notes)
{
    if (notes.empty()) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> activePitchClasses;
    activePitchClasses.reserve(notes.size());

    for (const auto note : notes) {
        activePitchClasses.push_back(pitchClass(note));
    }

    std::sort(activePitchClasses.begin(), activePitchClasses.end());
    activePitchClasses.erase(
        std::unique(activePitchClasses.begin(), activePitchClasses.end()),
        activePitchClasses.end());

    for (std::uint8_t root = 0; root < 12; ++root) {
        const auto intervals = normalizedIntervals(activePitchClasses, root);
        const auto quality = findTemplateQuality(intervals);
        if (quality.has_value()) {
            return TemplateMatch{root, *quality};
        }
    }

    return std::nullopt;
}

std::size_t uniquePitchClassCount(const std::vector<std::uint8_t>& notes)
{
    std::vector<std::uint8_t> pitchClasses;
    pitchClasses.reserve(notes.size());

    for (const auto note : notes) {
        pitchClasses.push_back(pitchClass(note));
    }

    std::sort(pitchClasses.begin(), pitchClasses.end());
    pitchClasses.erase(std::unique(pitchClasses.begin(), pitchClasses.end()), pitchClasses.end());
    return pitchClasses.size();
}

// Minimum distinct pitch classes required to register a NEW chord in a given mode.
// Below this, the held set is treated as a transient (e.g. a chord being released
// one finger at a time) and the previous chord is kept instead of downgrading to a
// weaker partial (power/sus/single). Single-finger modes accept one note as a chord.
std::size_t minPitchClassesForMode(ChordDetectionMode mode) noexcept
{
    switch (mode) {
        case ChordDetectionMode::SingleFinger:        return 1;
        case ChordDetectionMode::MultiFinger:         return 1;
        case ChordDetectionMode::FingeredIncomplete:  return 2;
        case ChordDetectionMode::Fingered:
        case ChordDetectionMode::FingeredOnBass:
        case ChordDetectionMode::FullKeyboard:
        case ChordDetectionMode::FullKeyboardNoInterval:
        default:                                      return 3;
    }
}

std::string formatChordName(std::uint8_t root, std::string_view quality)
{
    std::string name{pitchClassName(root)};
    if (quality != "major") {
        name += quality;
    }
    return name;
}

std::string formatDisplayName(
    std::uint8_t root,
    std::string_view quality,
    std::optional<std::uint8_t> bass,
    ChordDetectionMode mode)
{
    auto name = formatChordName(root, quality);
    if (mode == ChordDetectionMode::FingeredOnBass && bass.has_value() && *bass != root) {
        name += "/";
        name += pitchClassName(*bass);
    }
    return name;
}

} // namespace

MidiMessage MidiMessage::noteOn(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity)
{
    return MidiMessage{
        static_cast<std::uint8_t>(0x90u | (channel & 0x0Fu)),
        static_cast<std::uint8_t>(note & 0x7Fu),
        static_cast<std::uint8_t>(velocity & 0x7Fu),
    };
}

MidiMessage MidiMessage::noteOff(std::uint8_t channel, std::uint8_t note, std::uint8_t velocity)
{
    return MidiMessage{
        static_cast<std::uint8_t>(0x80u | (channel & 0x0Fu)),
        static_cast<std::uint8_t>(note & 0x7Fu),
        static_cast<std::uint8_t>(velocity & 0x7Fu),
    };
}

ArrangerMidiRouter::ArrangerMidiRouter(ArrangerState state)
    : state_(state)
{
    state_.splitNote = clampMidi7Bit(state_.splitNote);
}

const ArrangerState& ArrangerMidiRouter::state() const noexcept
{
    return state_;
}

void ArrangerMidiRouter::setSplitNote(std::uint8_t splitNote) noexcept
{
    state_.splitNote = clampMidi7Bit(splitNote);
}

void ArrangerMidiRouter::setChordDetectionMode(ChordDetectionMode mode) noexcept
{
    state_.chordMode = mode;
}

void ArrangerMidiRouter::setChordMemory(bool enabled) noexcept
{
    state_.chordMemory = enabled;
    if (!enabled && activeChordNoteCount_ == 0) {
        currentChord_.reset();
    }
}

void ArrangerMidiRouter::setSyncroStopOnRelease(bool enabled) noexcept
{
    state_.syncroStopOnRelease = enabled;
}

void ArrangerMidiRouter::addNoteCountObserver(NoteCountObserver observer)
{
    if (observer) {
        noteCountObservers_.push_back(std::move(observer));
    }
}

void ArrangerMidiRouter::addSyncObserver(SyncObserver observer)
{
    if (observer) {
        syncObservers_.push_back(std::move(observer));
    }
}

std::vector<RoutedMidiMessage> ArrangerMidiRouter::handle(const MidiMessage& message)
{
    if (!isNoteMessage(message)) {
        return {{RouteTarget::Ignored, message}};
    }

    const auto note = clampMidi7Bit(message.data1);
    const auto target = routeForNote(note);
    const auto previousCount = activeChordNoteCount_;

    if (isNoteOn(message)) {
        applyNoteOn(target, note);
        if (target == RouteTarget::ChordSide) {
            (void)detectChord();
        }
        notifyNoteCountIfChanged(previousCount);
        maybeStartSyncro(previousCount);
    } else if (isNoteOff(message)) {
        applyNoteOff(target, note);
        if (target == RouteTarget::ChordSide) {
            (void)detectChord();
        }
        notifyNoteCountIfChanged(previousCount);
        maybeStopSyncro(previousCount);
    }

    return {{target, MidiMessage{message.status, note, clampMidi7Bit(message.data2)}}};
}

std::size_t ArrangerMidiRouter::activeChordNoteCount() const noexcept
{
    return activeChordNoteCount_;
}

bool ArrangerMidiRouter::isChordNoteActive(std::uint8_t note) const noexcept
{
    return chordNoteCounters_[clampMidi7Bit(note)] > 0;
}

bool ArrangerMidiRouter::isMelodyNoteActive(std::uint8_t note) const noexcept
{
    return melodyNoteCounters_[clampMidi7Bit(note)] > 0;
}

std::optional<ChordRecognitionResult> ArrangerMidiRouter::detectChord() const
{
    std::vector<std::uint8_t> activeNotes;

    for (std::size_t note = 0; note < chordNoteCounters_.size(); ++note) {
        if (chordNoteCounters_[note] == 0) {
            continue;
        }

        activeNotes.push_back(static_cast<std::uint8_t>(note));
    }

    if (activeNotes.empty()) {
        if (state_.chordMemory) {
            return currentChord_;
        }

        currentChord_.reset();
        return std::nullopt;
    }

    // While a chord is being released finger-by-finger, the shrinking note set
    // matches weaker templates (power/sus/single). Don't downgrade: require enough
    // distinct pitch classes to register a NEW chord for this mode; otherwise keep
    // the current chord. This makes "hold Am, release slowly" stay Am.
    if (currentChord_.has_value()
        && uniquePitchClassCount(activeNotes) < minPitchClassesForMode(state_.chordMode)) {
        return currentChord_;
    }

    const auto bassPitchClass = pitchClass(activeNotes.front());
    auto match = matchChordTemplate(activeNotes);

    if (state_.chordMode == ChordDetectionMode::FingeredOnBass && activeNotes.size() > 1) {
        auto chordOnlyNotes = activeNotes;
        chordOnlyNotes.erase(chordOnlyNotes.begin());
        const auto chordOnlyMatch = matchChordTemplate(chordOnlyNotes);

        if (uniquePitchClassCount(chordOnlyNotes) >= 3 &&
            chordOnlyMatch.has_value() &&
            chordOnlyMatch->root != bassPitchClass) {
            match = chordOnlyMatch;
        }
    }

    if (!match.has_value()) {
        if (state_.chordMemory) {
            return currentChord_;
        }

        currentChord_.reset();
        return std::nullopt;
    }

    const auto hasInversion = bassPitchClass != match->root;
    const auto bass = hasInversion ? std::optional<std::uint8_t>{bassPitchClass} : std::nullopt;

    currentChord_ = ChordRecognitionResult{
        match->root,
        std::string{match->quality},
        bass,
        hasInversion,
        formatDisplayName(match->root, match->quality, bass, state_.chordMode),
    };
    return currentChord_;
}

void ArrangerMidiRouter::reset()
{
    const auto previousCount = activeChordNoteCount_;

    chordNoteCounters_.fill(0);
    melodyNoteCounters_.fill(0);
    activeChordNoteCount_ = 0;
    currentChord_.reset();

    notifyNoteCountIfChanged(previousCount);
    maybeStopSyncro(previousCount);
}

bool ArrangerMidiRouter::isNoteMessage(const MidiMessage& message) noexcept
{
    const auto type = messageType(message);
    return type == 0x80 || type == 0x90;
}

bool ArrangerMidiRouter::isNoteOn(const MidiMessage& message) noexcept
{
    return messageType(message) == 0x90 && clampMidi7Bit(message.data2) != 0;
}

bool ArrangerMidiRouter::isNoteOff(const MidiMessage& message) noexcept
{
    return messageType(message) == 0x80 ||
        (messageType(message) == 0x90 && clampMidi7Bit(message.data2) == 0);
}

std::uint8_t ArrangerMidiRouter::messageType(const MidiMessage& message) noexcept
{
    return static_cast<std::uint8_t>(message.status & 0xF0u);
}

std::uint8_t ArrangerMidiRouter::clampMidi7Bit(std::uint8_t value) noexcept
{
    return static_cast<std::uint8_t>(value & 0x7Fu);
}

bool ArrangerMidiRouter::isFullKeyboardMode(ChordDetectionMode mode) noexcept
{
    return mode == ChordDetectionMode::FullKeyboard ||
        mode == ChordDetectionMode::FullKeyboardNoInterval;
}

RouteTarget ArrangerMidiRouter::routeForNote(std::uint8_t note) const noexcept
{
    // Convention used by hardware arrangers: the split note itself counts
    // as the top of the chord zone, not the bottom of the melody zone.
    if (note <= state_.splitNote) {
        return RouteTarget::ChordSide;
    }

    if (isFullKeyboardMode(state_.chordMode)) {
        return RouteTarget::ChordSide;
    }

    return RouteTarget::MelodySide;
}

void ArrangerMidiRouter::applyNoteOn(RouteTarget target, std::uint8_t note)
{
    auto& counters = target == RouteTarget::ChordSide ? chordNoteCounters_ : melodyNoteCounters_;
    auto& counter = counters[note];

    if (counter == 0 && target == RouteTarget::ChordSide) {
        ++activeChordNoteCount_;
    }

    ++counter;
}

void ArrangerMidiRouter::applyNoteOff(RouteTarget target, std::uint8_t note)
{
    auto& counters = target == RouteTarget::ChordSide ? chordNoteCounters_ : melodyNoteCounters_;
    auto& counter = counters[note];

    if (counter == 0) {
        return;
    }

    --counter;

    if (counter == 0 && target == RouteTarget::ChordSide && activeChordNoteCount_ > 0) {
        --activeChordNoteCount_;
    }
}

void ArrangerMidiRouter::notifyNoteCountIfChanged(std::size_t previousCount)
{
    if (previousCount == activeChordNoteCount_) {
        return;
    }

    for (const auto& observer : noteCountObservers_) {
        observer(activeChordNoteCount_);
    }
}

void ArrangerMidiRouter::maybeStartSyncro(std::size_t previousCount)
{
    if (previousCount != 0 || activeChordNoteCount_ == 0 || state_.syncroStarted) {
        return;
    }

    state_.syncroStarted = true;
    for (const auto& observer : syncObservers_) {
        observer(SyncEvent::Started);
    }
}

void ArrangerMidiRouter::maybeStopSyncro(std::size_t previousCount)
{
    if (!state_.syncroStopOnRelease) {
        return;
    }

    if (previousCount == 0 || activeChordNoteCount_ != 0 || !state_.syncroStarted) {
        return;
    }

    state_.syncroStarted = false;
    for (const auto& observer : syncObservers_) {
        observer(SyncEvent::Stopped);
    }
}

} // namespace arranger
