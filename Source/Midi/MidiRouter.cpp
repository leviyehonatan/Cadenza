#include "MidiRouter.h"

namespace cadenza::midi
{
MidiRouter::MidiRouter()
    : m_router(arranger::ArrangerState{
          /*splitNote=*/             60,
          /*chordMode=*/             arranger::ChordDetectionMode::Fingered,
          /*syncroStarted=*/         false,
          /*chordMemory=*/           false,
          /*syncroStopOnRelease=*/   true,
      })
{
    // Wire sync events from the underlying router straight through.
    m_router.addSyncObserver([this](arranger::SyncEvent ev) {
        if (m_onSync) m_onSync(ev == arranger::SyncEvent::Started);
    });
}

MidiRouter::~MidiRouter()
{
    closeInputs();
}

juce::StringArray MidiRouter::availableInputs() const
{
    juce::StringArray names;
    for (const auto& info : juce::MidiInput::getAvailableDevices())
        names.add(info.name);
    return names;
}

bool MidiRouter::openInput(const juce::String& deviceName)
{
    closeInputs();
    const auto available = juce::MidiInput::getAvailableDevices();
    if (available.isEmpty()) return false;

    juce::MidiDeviceInfo chosen;
    if (deviceName.isEmpty()) {
        chosen = available.getFirst();
    } else {
        for (const auto& d : available)
            if (d.name == deviceName) { chosen = d; break; }
        if (chosen.identifier.isEmpty()) chosen = available.getFirst();
    }

    auto input = juce::MidiInput::openDevice(chosen.identifier, this);
    if (!input) return false;
    input->start();
    m_inputs.add(std::move(input));
    return true;
}

void MidiRouter::closeInputs()
{
    for (auto* in : m_inputs) in->stop();
    m_inputs.clear();
    m_openIdentifiers.clear();
    m_router.reset();
    m_rightHand.reset();
}

int MidiRouter::refreshInputs()
{
    const auto available = juce::MidiInput::getAvailableDevices();

    // Log the available device list, but only when it changes (avoids spam from
    // the periodic hot-plug poll).
    juce::String sig;
    for (const auto& d : available) sig << d.name << "|";
    if (sig != m_lastDeviceSignature) {
        m_lastDeviceSignature = sig;
        if (available.isEmpty()) {
            juce::Logger::writeToLog("[Cadenza] MIDI inputs available: (none)");
        } else {
            juce::String names;
            for (const auto& d : available) names << "\"" << d.name << "\" ";
            juce::Logger::writeToLog("[Cadenza] MIDI inputs available: " + names);
        }
    }

    for (const auto& d : available) {
        if (m_openIdentifiers.contains(d.identifier))
            continue;
        if (auto input = juce::MidiInput::openDevice(d.identifier, this)) {
            input->start();
            m_openIdentifiers.add(d.identifier);
            m_inputs.add(std::move(input));
            juce::Logger::writeToLog("[Cadenza] MIDI input OPENED: \"" + d.name + "\"");
        } else {
            juce::Logger::writeToLog("[Cadenza] MIDI input FAILED to open: \"" + d.name + "\"");
        }
    }
    return m_inputs.size();
}

void MidiRouter::setSplitPoint(int midiNote) noexcept
{
    m_router.setSplitNote(static_cast<std::uint8_t>(midiNote & 0x7F));
}

int MidiRouter::splitPoint() const noexcept
{
    return static_cast<int>(m_router.state().splitNote);
}

void MidiRouter::setChordDetectionMode(arranger::ChordDetectionMode mode) noexcept
{
    m_router.setChordDetectionMode(mode);
}

std::vector<LiveMelodyEvent> MidiRouter::handleVirtualMelodyNote(int note, int velocity, bool isOn)
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    const bool melodyZone = note >= static_cast<int>(m_router.state().splitNote);
    return m_rightHand.handleNote(note, velocity, isOn, melodyZone);
}

void MidiRouter::injectNote(int note, int velocity, bool isOn)
{
    std::optional<arranger::ChordRecognitionResult> currentChord;
    std::vector<LiveMelodyEvent> events;
    std::string chordName;
    bool chordChanged = false;
    {
        std::lock_guard<std::mutex> lk(m_publishMutex);
        const auto wrapped = isOn
            ? arranger::MidiMessage::noteOn (0, static_cast<std::uint8_t>(note),
                                             static_cast<std::uint8_t>(velocity))
            : arranger::MidiMessage::noteOff(0, static_cast<std::uint8_t>(note));
        const auto routed = m_router.handle(wrapped);
        currentChord = m_router.detectChord();
        const auto target = routed.empty() ? arranger::RouteTarget::Ignored : routed.front().target;
        events = m_rightHand.handleNote(note, velocity, isOn, target == arranger::RouteTarget::MelodySide);

        chordName = currentChord.has_value() ? currentChord->displayName : std::string{};
        if (chordName != m_lastChordName) { m_lastChordName = chordName; chordChanged = true; }
    }

    if (m_onNote)
        for (const auto& ev : events)
            m_onNote(ev.channel, ev.note, ev.velocity, ev.isOn);
    if (chordChanged && m_onChord)
        m_onChord(toCadenzaChord(currentChord), chordName);
}

void MidiRouter::setChordMemory(bool enabled) noexcept
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    m_router.setChordMemory(enabled);
}

void MidiRouter::setSyncroStopOnRelease(bool enabled) noexcept
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    m_router.setSyncroStopOnRelease(enabled);
}

std::string MidiRouter::currentChordDisplayName() const
{
    std::lock_guard<std::mutex> lk(m_publishMutex);
    const auto chord = m_router.detectChord();
    return chord.has_value() ? chord->displayName : std::string{};
}

void MidiRouter::handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& msg)
{
    // Capture every message for debug observability — including non-note messages
    // (CC, pitch bend, etc.) that the arranger router ignores. This is observability
    // only; arranger behavior is unchanged.
    const std::string deviceName = source != nullptr ? source->getName().toStdString() : std::string{};
    const int status = msg.getRawDataSize() > 0
        ? static_cast<int>(static_cast<unsigned char>(msg.getRawData()[0]))
        : 0;

    int debugNote = -1;
    int debugVelocity = -1;
    std::string debugRoute = "ignored";
    std::optional<arranger::ChordRecognitionResult> currentChord;

    const bool syncBefore = m_router.state().syncroStarted;

    // --- MIDI control buttons / MIDI learn (arranger control: sections, start/stop) ---
    // A "press" is a CC with value > 0 or a note-on. Determine the trigger; if we're
    // learning, capture it; otherwise fire any mapped command. A mapped NOTE is then
    // consumed so it doesn't also play as a chord/melody note.
    {
        int trigger = -1;
        bool press = false;
        if (msg.isController()) {
            trigger = controlTriggerForCC(msg.getControllerNumber());
            press   = msg.getControllerValue() > 0;
        } else if (msg.isNoteOn()) {
            trigger = controlTriggerForNote(msg.getNoteNumber());
            press   = msg.getVelocity() > 0;
        }

        if (trigger >= 0 && press && m_learnArmed.load()) {
            m_learnArmed.store(false);
            if (m_onControlLearn) m_onControlLearn(trigger);
            return;   // consume: this press was for learning, not for playing
        }
        if (trigger >= 0) {
            std::optional<std::string> command;
            {
                std::lock_guard<std::mutex> lk(m_publishMutex);
                command = m_controlMap.commandFor(trigger);
            }
            if (command) {
                if (press && m_onControl) m_onControl(*command);
                return;   // consume: mapped controls never play notes / drive chords
            }
        }
    }

    if (msg.isNoteOnOrOff()) {
        const int channel  = msg.getChannel();
        const int note     = msg.getNoteNumber();
        const int velocity = msg.getVelocity();
        const bool isOn    = msg.isNoteOn();

        debugNote = note;
        debugVelocity = isOn ? velocity : 0;

        // 1. Forward to the arranger router FIRST so we know whether this note is
        //    in the melody (right-hand) zone before deciding how it should sound.
        //    The router/chord recogniser always sees the ORIGINAL pitch — Octave
        //    must not change chord detection.
        std::vector<arranger::RoutedMidiMessage> routed;
        arranger::RouteTarget target = arranger::RouteTarget::Ignored;
        std::vector<LiveMelodyEvent> events;
        {
            std::lock_guard<std::mutex> lk(m_publishMutex);
            const auto wrapped = isOn
                ? arranger::MidiMessage::noteOn (static_cast<std::uint8_t>(channel - 1),
                                                 static_cast<std::uint8_t>(note),
                                                 static_cast<std::uint8_t>(velocity))
                : arranger::MidiMessage::noteOff(static_cast<std::uint8_t>(channel - 1),
                                                 static_cast<std::uint8_t>(note));
            routed = m_router.handle(wrapped);
            currentChord = m_router.detectChord();
            target = routed.empty() ? arranger::RouteTarget::Ignored : routed.front().target;

            // 2. Decide the live right-hand output (one event per enabled Right
            //    layer) under the same lock as routing so per-note state stays
            //    consistent. Octave never reaches the chord recogniser.
            events = m_rightHand.handleNote(note, velocity, isOn, target == arranger::RouteTarget::MelodySide);
        }

        switch (target) {
            case arranger::RouteTarget::ChordSide:  debugRoute = "chord";   break;
            case arranger::RouteTarget::MelodySide: debugRoute = "melody";  break;
            case arranger::RouteTarget::Ignored:    debugRoute = "ignored"; break;
        }

        // Only right-hand (melody-zone) notes sound, on each enabled layer's
        // channel with its Octave shift; chord-zone notes make no melody sound.
        for (const auto& ev : events) {
            if (m_onNote) m_onNote(ev.channel, ev.note, ev.velocity, ev.isOn);
            juce::Logger::writeToLog(
                juce::String("[Cadenza] live right ") + (ev.isOn ? "on " : "off")
                + " orig=" + juce::String(note)
                + " shifted=" + juce::String(ev.note)
                + " ch=" + juce::String(ev.channel)
                + " vel=" + juce::String(ev.velocity));
        }

        // 3. Fire ChordCallback when the displayed chord changes (m_lastChordName
        //    is shared with injectNote, so guard the compare/update).
        const std::string name = currentChord.has_value() ? currentChord->displayName : std::string{};
        bool chordChanged = false;
        {
            std::lock_guard<std::mutex> lk(m_publishMutex);
            if (name != m_lastChordName) { m_lastChordName = name; chordChanged = true; }
        }
        if (chordChanged && m_onChord)
            m_onChord(toCadenzaChord(currentChord), name);
    } else {
        // Non-note messages: leave the arranger router untouched but still query
        // the current chord so the debug panel can show what's held.
        std::lock_guard<std::mutex> lk(m_publishMutex);
        currentChord = m_router.detectChord();
    }

    const bool syncAfter = m_router.state().syncroStarted;
    std::string syncStr = "none";
    if (syncBefore != syncAfter) syncStr = syncAfter ? "start" : "stop";

    if (m_onDebug) {
        MidiDebugEvent ev;
        ev.deviceName = deviceName;
        ev.status     = status;
        ev.note       = debugNote;
        ev.velocity   = debugVelocity;
        ev.route      = std::move(debugRoute);
        ev.chordName  = currentChord.has_value() ? currentChord->displayName : std::string{};
        ev.sync       = std::move(syncStr);
        m_onDebug(ev);
    }
}

std::optional<Chord> MidiRouter::toCadenzaChord(const std::optional<arranger::ChordRecognitionResult>& result)
{
    if (!result.has_value()) return std::nullopt;

    Chord c;
    c.rootPitchClass = result->root % 12;
    c.quality = toCadenzaQuality(result->quality);

    // Approximate bassMidi from the bass pitch class, parked in the middle octave.
    // StyleEngine + PatternTransposer only care about the pitch class for chord
    // roles, but Chord::bassMidi != -1 lets toString() print slash chords correctly.
    if (result->bass.has_value()) {
        c.bassMidi = static_cast<int>(*result->bass) + 36; // octave 1 in MIDI numbering
    } else {
        c.bassMidi = -1;
    }
    return c;
}

ChordQuality MidiRouter::toCadenzaQuality(const std::string& s)
{
    // Map ArrangerMidiRouter's string qualities into the existing enum used by
    // StyleEngine. Extensions (7(9), 7(b9), etc.) collapse to Dominant7 for now.
    if (s == "major")  return ChordQuality::Major;
    if (s == "m")      return ChordQuality::Minor;
    if (s == "maj7")   return ChordQuality::Major7;
    if (s == "m7")     return ChordQuality::Minor7;
    if (s == "dim")    return ChordQuality::Diminished;
    if (s == "aug")    return ChordQuality::Augmented;
    if (s == "sus2")   return ChordQuality::Sus2;
    if (s == "sus4")   return ChordQuality::Sus4;
    // Anything starting with "7" (incl. 7(9), 7(#11), 7(13), 7(b9), 7(#9)).
    if (!s.empty() && s.front() == '7') return ChordQuality::Dominant7;
    return ChordQuality::Major;
}
}
