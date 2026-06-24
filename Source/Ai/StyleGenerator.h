// StyleGenerator — turn a text description into a Cadenza `.cstyle` via the
// Anthropic Messages API (bring-your-own-key). Raw HTTPS (no C++ SDK exists).
//
// generateStyle() is BLOCKING (it does a network round-trip) — call it from a
// background thread, then marshal the result back to the message thread.

#pragma once

#include "Arranger/Style.h"

#include <juce_core/juce_core.h>

#include <string>

namespace cadenza::ai
{
struct StyleGenResult
{
    bool ok = false;
    std::string cstyleJson;   // extracted .cstyle JSON on success
    std::string error;        // human-readable error on failure
    int inputTokens = 0;
    int outputTokens = 0;
};

struct SectionsMergeResult
{
    bool ok = false;
    std::string error;
    cadenza::arranger::Style style;
    int addedSections = 0;
    int replacedSections = 0;
    int skippedSections = 0;
};

// Calls POST https://api.anthropic.com/v1/messages with the cadenza-style-author
// system prompt and the user's description. Returns the parsed `.cstyle` JSON text
// (validate/load it with StyleLoader) or an error.
//
// If currentStyleJson is non-empty, the request EDITS that style per the prompt and
// returns the full updated style. If empty, it CREATES a new style (and auto-retries
// once if the generated drums have no kick/snare foundation).
StyleGenResult generateStyle(const juce::String& apiKey,
                             const juce::String& model,
                             const juce::String& userPrompt,
                             const juce::String& currentStyleJson = juce::String());

StyleGenResult generateStyleSectionsOnly(const juce::String& apiKey,
                                         const juce::String& model,
                                         const juce::String& userPrompt,
                                         const juce::String& currentStyleJson);

SectionsMergeResult mergeAiGeneratedSections(const cadenza::arranger::Style& original,
                                             const std::string& sectionsJson);

SectionsMergeResult mergeAiPolishedSection(const cadenza::arranger::Style& original,
                                           const std::string& sectionId,
                                           const std::string& sectionsJson);

// Conservative post-generation checks for edit-mode AI actions. These do not
// call the network and are kept pure so UI code can reject unsafe AI output.
bool validateAiAddedSectionsOnly(const cadenza::arranger::Style& original,
                                 const cadenza::arranger::Style& aiStyle);

bool validatePolishKeptStructure(const cadenza::arranger::Style& original,
                                 const cadenza::arranger::Style& aiStyle);

// The system prompt embedding the .cstyle schema, note roles, channel layout,
// GM drum map, and genre recipes (a compact form of the cadenza-style-author skill).
juce::String styleAuthorSystemPrompt();
}
