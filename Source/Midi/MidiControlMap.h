// MidiControlMap — maps MIDI controller buttons/keys to arranger actions, so a
// hardware keyboard can drive Start/Stop, Intro, Main A-D, Fills, Ending, etc.
//
// A "trigger" is a channel-agnostic identifier for an incoming MIDI message:
//   * a Control Change number (most arranger keyboards send CC for their buttons)
//   * or a note number (for pad/key controllers)
// Each trigger maps to a "command" string: a style section id ("mainA", "fillAA",
// "intro", "ending", ...) or a transport command ("play").
//
// Pure (cadenza_core, no JUCE): the router computes triggers and looks them up;
// the app turns commands into actions and edits the map (MIDI learn).

#pragma once

#include <map>
#include <optional>
#include <string>

namespace cadenza::midi
{
// Trigger ids encode the MIDI channel (1..16), a CC-vs-note flag, and the
// data byte (CC number or note number). Including the channel lets a pad that
// sends on its own channel be distinguished from a piano key with the same note.
//   bits 0..6  = data (0..127)   bit 8 = CC flag   bits 9..13 = channel (1..16)
constexpr int kControlCcFlag = 0x100;
inline int controlTrigger(int channel, bool isCC, int data1) noexcept {
    return ((channel & 0x1F) << 9) | (isCC ? kControlCcFlag : 0) | (data1 & 0x7F);
}
inline int controlTriggerForCC(int channel, int cc) noexcept    { return controlTrigger(channel, true,  cc); }
inline int controlTriggerForNote(int channel, int note) noexcept { return controlTrigger(channel, false, note); }
inline bool triggerIsCC(int trigger) noexcept     { return (trigger & kControlCcFlag) != 0; }
inline int  triggerData(int trigger) noexcept     { return trigger & 0x7F; }
inline int  triggerChannel(int trigger) noexcept  { return (trigger >> 9) & 0x1F; }

class MidiControlMap
{
public:
    // Map a trigger to a command (replacing any existing command for that trigger,
    // and removing the command from any other trigger so each action has one button).
    void assign(int trigger, const std::string& command);
    void clearTrigger(int trigger);
    void clearCommand(const std::string& command);
    void clear();

    bool empty() const noexcept { return m_map.empty(); }

    // The command bound to a trigger, or nullopt.
    std::optional<std::string> commandFor(int trigger) const;
    // The trigger bound to a command, or nullopt (for showing the mapping in the UI).
    std::optional<int> triggerFor(const std::string& command) const;

    const std::map<int, std::string>& entries() const noexcept { return m_map; }
    void setEntries(std::map<int, std::string> entries) { m_map = std::move(entries); }

private:
    std::map<int, std::string> m_map;   // trigger -> command
};

// Human-readable label for a trigger, e.g. "CC 80" or "Note 36" (UI display).
std::string describeTrigger(int trigger);
}
