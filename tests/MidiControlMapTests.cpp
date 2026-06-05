// MidiControlMapTests — MIDI button/key -> arranger action mapping.

#include "Midi/MidiControlMap.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

using namespace cadenza::midi;

void triggerEncoding()
{
    const int cc = controlTriggerForCC(80);
    expect(triggerIsCC(cc), "CC trigger flagged as CC");
    expect(triggerData(cc) == 80, "CC trigger keeps the number");

    const int note = controlTriggerForNote(36);
    expect(!triggerIsCC(note), "note trigger not flagged CC");
    expect(triggerData(note) == 36, "note trigger keeps the number");

    expect(cc != controlTriggerForNote(80), "CC 80 and Note 80 are distinct triggers");
    expect(describeTrigger(cc) == "CC 80", "describes CC");
    expect(describeTrigger(note) == "Note 36", "describes note");
}

void assignAndLookup()
{
    MidiControlMap m;
    expect(m.empty(), "fresh map empty");
    m.assign(controlTriggerForCC(80), "mainA");
    expect(m.commandFor(controlTriggerForCC(80)).value_or("") == "mainA", "CC80 -> mainA");
    expect(!m.commandFor(controlTriggerForCC(81)).has_value(), "CC81 unmapped");
    expect(m.triggerFor("mainA").value_or(-1) == controlTriggerForCC(80), "reverse lookup");
}

void oneButtonPerAction()
{
    MidiControlMap m;
    m.assign(controlTriggerForCC(80), "mainA");
    m.assign(controlTriggerForCC(81), "mainA");   // re-map mainA to a different button
    expect(!m.commandFor(controlTriggerForCC(80)).has_value(), "old button released");
    expect(m.commandFor(controlTriggerForCC(81)).value_or("") == "mainA", "new button mapped");
    expect(m.entries().size() == 1, "still one entry for the action");
}

void reassignTriggerReplacesCommand()
{
    MidiControlMap m;
    m.assign(controlTriggerForCC(80), "mainA");
    m.assign(controlTriggerForCC(80), "fillAA");   // same button, new action
    expect(m.commandFor(controlTriggerForCC(80)).value_or("") == "fillAA", "button now fillAA");
    expect(!m.triggerFor("mainA").has_value(), "mainA no longer mapped");
}

void clearOps()
{
    MidiControlMap m;
    m.assign(controlTriggerForCC(80), "mainA");
    m.assign(controlTriggerForNote(36), "play");
    m.clearTrigger(controlTriggerForCC(80));
    expect(!m.commandFor(controlTriggerForCC(80)).has_value(), "trigger cleared");
    m.clearCommand("play");
    expect(!m.triggerFor("play").has_value(), "command cleared");
    expect(m.empty(), "map empty after clears");
}
}

int main()
{
    triggerEncoding();
    assignAndLookup();
    oneButtonPerAction();
    reassignTriggerReplacesCommand();
    clearOps();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All MidiControlMap tests passed\n";
    return EXIT_SUCCESS;
}
