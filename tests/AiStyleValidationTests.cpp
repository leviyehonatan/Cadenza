#include "Ai/StyleGenerator.h"
#include "Arranger/StyleLoader.h"

#include <iostream>
#include <stdexcept>

namespace
{
cadenza::arranger::PatternNote note(int tick, int pitch)
{
    cadenza::arranger::PatternNote n;
    n.tick = tick;
    n.duration = 120;
    n.pitch = pitch;
    n.velocity = 100;
    n.role = cadenza::arranger::NoteRole::Absolute;
    return n;
}

cadenza::arranger::Part part(const std::string& name, int channel, std::vector<cadenza::arranger::PatternNote> notes)
{
    cadenza::arranger::Part p;
    p.name = name;
    p.midiChannel = channel;
    p.instrument = name == "drums" ? "Standard Kit" : "Piano";
    p.program = name == "drums" ? 0 : 1;
    p.percussion = name == "drums";
    p.notes = std::move(notes);
    return p;
}

cadenza::arranger::Section section(const std::string& id, int bars)
{
    cadenza::arranger::Section s;
    s.name = id;
    s.barCount = bars;
    s.parts.push_back(part("drums", 10, { note(0, 36), note(960, 38) }));
    s.parts.push_back(part("bass", 2, { note(0, 36) }));
    return s;
}

cadenza::arranger::Style baseStyle()
{
    cadenza::arranger::Style style;
    style.id = "test-style";
    style.name = "Test Style";
    style.defaultTempo = 112;
    style.beatsPerBar = 4;
    style.beatUnit = 4;
    style.ticksPerBeat = 960;
    style.sections.push_back(section("mainA", 2));
    style.sections.push_back(section("mainB", 2));
    return style;
}

void check(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void acceptsOnlyAllowedAddedFillIntroEndingSections()
{
    const auto original = baseStyle();
    auto ai = original;
    ai.sections.push_back(section("fillAA", 1));
    ai.sections.push_back(section("fillAB", 1));
    ai.sections.push_back(section("fillBA", 1));
    ai.sections.push_back(section("intro", 1));
    ai.sections.push_back(section("ending", 2));

    check(cadenza::ai::validateAiAddedSectionsOnly(original, ai),
          "allowed fill/intro/ending sections should be accepted");
}

void rejectsFillGenerationWhenExistingSectionIsMissing()
{
    const auto original = baseStyle();
    auto ai = original;
    ai.sections.erase(ai.sections.begin());

    check(!cadenza::ai::validateAiAddedSectionsOnly(original, ai),
          "missing original section should be rejected");
}

void rejectsFillGenerationWhenExistingNotesChange()
{
    const auto original = baseStyle();
    auto ai = original;
    ai.sections[0].parts[0].notes[0].pitch = 40;

    check(!cadenza::ai::validateAiAddedSectionsOnly(original, ai),
          "changed existing notes should be rejected");
}

void rejectsFillGenerationWhenAddedSectionIdIsNotAllowed()
{
    const auto original = baseStyle();
    auto ai = original;
    ai.sections.push_back(section("fillAC", 1));

    check(!cadenza::ai::validateAiAddedSectionsOnly(original, ai),
          "disallowed added section id should be rejected");
}

void acceptsPolishWhenOnlyNotesChange()
{
    const auto original = baseStyle();
    auto ai = original;
    ai.sections[0].parts[0].notes[0].pitch = 40;
    ai.sections[1].parts[1].notes.push_back(note(480, 43));

    check(cadenza::ai::validatePolishKeptStructure(original, ai),
          "note-only polish should be accepted");
}

void rejectsPolishWhenSectionIdsChange()
{
    const auto original = baseStyle();
    auto ai = original;
    ai.sections[1].name = "mainC";

    check(!cadenza::ai::validatePolishKeptStructure(original, ai),
          "changed section id should be rejected");
}

void rejectsPolishWhenPartStructureChanges()
{
    const auto original = baseStyle();
    auto ai = original;
    ai.sections[0].parts[1].instrument = "Different Bass";

    check(!cadenza::ai::validatePolishKeptStructure(original, ai),
          "changed part metadata should be rejected");
}

void mergesSectionsOnlyResponseWithoutChangingExistingSections()
{
    const auto original = baseStyle();
    const std::string sectionsJson = R"({
      "sections": {
        "fillAA": {
          "barCount": 1,
          "parts": [
            {
              "name": "drums",
              "channel": 10,
              "instrument": "Standard Kit",
              "program": 0,
              "percussion": true,
              "notes": [
                { "tick": 0, "duration": 120, "pitch": 36, "velocity": 110, "role": "absolute" },
                { "tick": 960, "duration": 120, "pitch": 38, "velocity": 112, "role": "absolute" }
              ]
            }
          ]
        },
        "mainA": {
          "barCount": 1,
          "parts": [
            {
              "name": "drums",
              "channel": 10,
              "instrument": "Standard Kit",
              "program": 0,
              "percussion": true,
              "notes": [
                { "tick": 0, "duration": 120, "pitch": 40, "velocity": 110, "role": "absolute" }
              ]
            }
          ]
        },
        "fillAC": {
          "barCount": 1,
          "parts": [
            {
              "name": "drums",
              "channel": 10,
              "instrument": "Standard Kit",
              "program": 0,
              "percussion": true,
              "notes": [
                { "tick": 0, "duration": 120, "pitch": 36, "velocity": 110, "role": "absolute" }
              ]
            }
          ]
        }
      }
    })";

    const auto merged = cadenza::ai::mergeAiGeneratedSections(original, sectionsJson);
    check(merged.ok, "sections-only merge should succeed");
    check(merged.addedSections == 1, "only allowed new fillAA should be added");
    check(merged.skippedSections == 2, "existing mainA and disallowed fillAC should be skipped");
    check(merged.style.sections.size() == original.sections.size() + 1, "one section appended");
    check(merged.style.findSection("fillAA") != nullptr, "fillAA added");
    check(merged.style.findSection("fillAC") == nullptr, "fillAC skipped");

    const auto* originalMainA = original.findSection("mainA");
    const auto* mergedMainA = merged.style.findSection("mainA");
    check(originalMainA != nullptr && mergedMainA != nullptr, "mainA present before and after merge");
    check(mergedMainA->barCount == originalMainA->barCount, "mainA bar count unchanged");
    check(mergedMainA->parts.size() == originalMainA->parts.size(), "mainA parts unchanged");
    check(mergedMainA->parts[0].notes[0].pitch == originalMainA->parts[0].notes[0].pitch,
          "mainA notes unchanged");

    const auto roundTripJson = cadenza::arranger::saveStyleToJson(merged.style, false);
    const auto reloaded = cadenza::arranger::loadStyleFromJson(roundTripJson);
    check(reloaded.ok, "merged style should reload");
    check(reloaded.style.findSection("fillAA") != nullptr, "fillAA survives round trip");
    check(reloaded.style.findSection("mainA") != nullptr, "mainA survives round trip");
}
}

int main()
{
    acceptsOnlyAllowedAddedFillIntroEndingSections();
    rejectsFillGenerationWhenExistingSectionIsMissing();
    rejectsFillGenerationWhenExistingNotesChange();
    rejectsFillGenerationWhenAddedSectionIdIsNotAllowed();
    acceptsPolishWhenOnlyNotesChange();
    rejectsPolishWhenSectionIdsChange();
    rejectsPolishWhenPartStructureChanges();
    mergesSectionsOnlyResponseWithoutChangingExistingSections();

    std::cout << "AiStyleValidationTests passed\n";
    return 0;
}
