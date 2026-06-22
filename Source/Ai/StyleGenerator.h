// StyleGenerator — turn a text description into a Cadenza `.cstyle` via the
// Anthropic Messages API (bring-your-own-key). Raw HTTPS (no C++ SDK exists).
//
// generateStyle() is BLOCKING (it does a network round-trip) — call it from a
// background thread, then marshal the result back to the message thread.

#pragma once

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

// Calls POST https://api.anthropic.com/v1/messages with the cadenza-style-author
// system prompt and the user's description. Returns the parsed `.cstyle` JSON text
// (validate/load it with StyleLoader) or an error.
StyleGenResult generateStyle(const juce::String& apiKey,
                             const juce::String& model,
                             const juce::String& userPrompt);

// The system prompt embedding the .cstyle schema, note roles, channel layout,
// GM drum map, and genre recipes (a compact form of the cadenza-style-author skill).
juce::String styleAuthorSystemPrompt();
}
