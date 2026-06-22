#include "StyleGenerator.h"

namespace cadenza::ai
{
juce::String styleAuthorSystemPrompt()
{
    // Compact form of the cadenza-style-author skill: enough for the model to emit
    // a valid, musical .cstyle. Keep this in sync with the skill's schema/recipes.
    return R"PROMPT(You generate Cadenza ".cstyle" arranger accompaniment styles as JSON.

OUTPUT RULES (critical):
- Reply with ONE JSON object only. No prose, no markdown, no code fences.
- It must parse as JSON and follow the schema below exactly.

A .cstyle is an accompaniment recorded in C major / C root that Cadenza transposes
live to follow the player's chord.

SCHEMA:
{
  "$schema": "cadenza.style.v1",
  "id": "kebab-case-id",
  "name": "Display Name",
  "tempo": 120,
  "timeSignature": [4,4],
  "ticksPerBeat": 960,
  "sections": {
    "mainA": {
      "barCount": 1,
      "parts": [
        { "name":"drums", "channel":10, "instrument":"Standard Kit", "program":0,
          "notes":[ {"tick":0,"duration":120,"pitch":36,"velocity":110,"role":"absolute"} ] }
      ]
    }
  }
}

NOTE FIELDS: tick (start), duration (ticks), pitch (0-127, written in C), velocity
(1-127), role. ticksPerBeat=960 so in 4/4: bar=3840, beat=960, 8th=480, 16th=240.
The "&" of a beat = beat+480.

ROLES (how each note transposes to the live chord):
- absolute    : never transposed -> ALL drums/percussion and fixed FX.
- chord-root  : becomes the chord root -> bass roots, chord bottoms.
- chord-3     : the chord's 3rd (major/minor aware) -> comp voices.
- chord-5     : the chord's 5th -> comp voices.
- chord-7     : the chord's 7th (only for 7th chords).
- chord-color : non-chord melodic tone fit to the chord -> riffs, fills.
- scale       : a scale degree (uses "scaleDegree" 0..6).

CHANNEL / PART LAYOUT (use these):
- drums   channel 10  (a GM kit, program 0 = Standard)
- bass    channel 2   (program 33/32/38)
- harmony channel 3   (piano/EP/guitar/organ comp)
- pad     channel 4   (warm strings / synth pad, program 48/50/89)
You may add a second comp or riff on channels 5-8.

GM DRUM MAP (write drums with absolute pitches, stay 35-81):
36 Kick, 38 Snare, 37 SideStick, 39 Clap, 42 ClosedHat, 44 PedalHat, 46 OpenHat,
41/43/45/47/48/50 Toms, 49 Crash, 51 Ride, 54 Tambourine, 56 Cowbell, 60/61 Bongo,
62/63/64 Conga, 70 Maracas, 75 Claves, 76/77 Woodblock, 73/74 Guiro.

DRUM FOUNDATION — THE MOST IMPORTANT THING. Get this wrong and it sounds like a
mess. Beats sit at tick 0 (beat 1), 960 (beat 2), 1920 (beat 3), 2880 (beat 4).
- EVERY groove MUST have a KICK (pitch 36) AND a SNARE (pitch 38) — never hi-hats
  alone. Hi-hats (42) only keep time UNDER the kick and snare.
- Default backbeat: SNARE on beat 2 (tick 960) and beat 4 (tick 2880). Almost every
  pop/rock/funk/disco style uses this. Do NOT put the snare on random offbeats.
- KICK at least on beat 1 (tick 0); add more per the genre pattern below.
- Hi-hats: straight 8ths at ticks 0,480,960,1440,1920,2400,2880,3360 (or 16ths),
  velocities alternating ~78/64 so they breathe.

GENRE KICK/SNARE PATTERNS (1 bar = 3840 ticks):
- Pop/Rock 8-beat (~115/150): kick 0 & 1920 (+ "&of3" 2400); snare 960 & 2880.
- Disco/Dance four-on-the-floor (~118/128): kick on ALL four — 0, 960, 1920, 2880;
  snare or clap (39) on 960 & 2880; open hat (46) on the &s (480,1440,2400,3360).
- Funk (~115): kick 0 plus syncopation (e.g. 720, 1560, 2640); snare 960 & 2880;
  busy 16th closed hats; add ghost snares ~vel 50-70.
- Ballad (~70): kick 0 (& 1920); soft snare or side-stick (37) on 960 & 2880; sparse.
- Reggae one-drop (~75): NO kick on beat 1 — kick (36) AND snare (38) TOGETHER on
  beat 3 (1920); offbeat hats; the comp plays the offbeat "skank" on the &s.
- Latin/Bossa (~120): no backbeat kick; clave (75) + congas (62/63) instead.

METHOD: pick tempo+feel from the description (or named song). 1) DRUMS first using
the foundation above. 2) BASS: chord-root, low (pitch 36-48), locked to the kick;
make it groove (syncopation/ghosts for funk, steady 8ths for disco). 3) HARMONY:
triads (chord-root+chord-3+chord-5) around C4 (60), short stabs on the offbeats for
funk/disco, sustained for ballad. 4) optional PAD: one long sustained triad, low
velocity (~45). Build an arc: mainA simpler, mainB busier; add intro, a 1-bar fillAA,
and an ending. Vary velocities (accents ~110, ghosts ~60-80) so it breathes.

WORKED EXAMPLE — one bar of a solid groove. Copy this skeleton, then adapt to the
requested genre/tempo (for disco, add kick at 960 and 2880 too):
drums (ch10):
  {"tick":0,"duration":120,"pitch":36,"velocity":112,"role":"absolute"}   kick beat1
  {"tick":1920,"duration":120,"pitch":36,"velocity":106,"role":"absolute"} kick beat3
  {"tick":960,"duration":120,"pitch":38,"velocity":110,"role":"absolute"}  snare beat2
  {"tick":2880,"duration":120,"pitch":38,"velocity":110,"role":"absolute"} snare beat4
  closed hat 42 at 0,480,960,1440,1920,2400,2880,3360 vel ~70 (alternate 78/64)
bass (ch2): {"tick":0,"duration":420,"pitch":36,"velocity":110,"role":"chord-root"},
  {"tick":1920,"duration":420,"pitch":36,"velocity":105,"role":"chord-root"},
  {"tick":2400,"duration":180,"pitch":43,"velocity":90,"role":"chord-5"}
harmony (ch3) stabs: triad chord-root 60 + chord-3 64 + chord-5 67 at ticks 480 and
  2400, duration ~200, velocity ~80.

Return ONLY the JSON object.)PROMPT";
}

namespace
{
// Pull the JSON object out of the model's reply: strip code fences, then take from
// the first '{' to the last '}'.
juce::String extractJsonObject(const juce::String& text)
{
    juce::String t = text.trim();
    if (t.startsWith("```")) {
        const int firstNl = t.indexOfChar('\n');
        if (firstNl >= 0) t = t.substring(firstNl + 1);
        const int fence = t.lastIndexOf("```");
        if (fence >= 0) t = t.substring(0, fence);
    }
    const int open = t.indexOfChar('{');
    const int close = t.lastIndexOfChar('}');
    if (open >= 0 && close > open)
        return t.substring(open, close + 1);
    return {};
}
}

StyleGenResult generateStyle(const juce::String& apiKey,
                             const juce::String& model,
                             const juce::String& userPrompt)
{
    StyleGenResult result;

    if (apiKey.trim().isEmpty()) {
        result.error = "No Anthropic API key set. Add one in AI Settings.";
        return result;
    }

    // --- build the request body ---
    auto* msgObj = new juce::DynamicObject();
    msgObj->setProperty("role", "user");
    msgObj->setProperty("content", userPrompt);
    juce::Array<juce::var> messages;
    messages.add(juce::var(msgObj));

    auto* rootObj = new juce::DynamicObject();
    rootObj->setProperty("model", model);
    rootObj->setProperty("max_tokens", 16000);
    rootObj->setProperty("system", styleAuthorSystemPrompt());
    rootObj->setProperty("messages", messages);
    const juce::String body = juce::JSON::toString(juce::var(rootObj));

    // --- POST ---
    juce::URL url("https://api.anthropic.com/v1/messages");
    url = url.withPOSTData(body);

    juce::StringPairArray responseHeaders;
    int statusCode = 0;
    const juce::String headers =
        "x-api-key: " + apiKey.trim() + "\r\n"
        "anthropic-version: 2023-06-01\r\n"
        "content-type: application/json";

    auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
        .withExtraHeaders(headers)
        .withConnectionTimeoutMs(120000)
        .withResponseHeaders(&responseHeaders)
        .withStatusCode(&statusCode);

    std::unique_ptr<juce::InputStream> stream(url.createInputStream(options));
    if (stream == nullptr) {
        result.error = "Could not reach api.anthropic.com (check your internet connection).";
        return result;
    }

    const juce::String response = stream->readEntireStreamAsString();
    const juce::var parsed = juce::JSON::parse(response);

    if (statusCode != 200) {
        juce::String msg = parsed["error"]["message"].toString();
        if (msg.isEmpty()) msg = "HTTP " + juce::String(statusCode);
        if (statusCode == 401) msg = "Invalid API key (401). Check it in AI Settings.";
        else if (statusCode == 429) msg = "Rate limited (429). Wait a moment and try again.";
        result.error = "API error: " + msg.toStdString();
        return result;
    }

    // usage (for cost visibility)
    result.inputTokens  = (int) parsed["usage"]["input_tokens"];
    result.outputTokens = (int) parsed["usage"]["output_tokens"];

    // gather the text content blocks
    juce::String text;
    if (auto* arr = parsed["content"].getArray())
        for (const auto& block : *arr)
            if (block["type"].toString() == "text")
                text += block["text"].toString();

    if (parsed["stop_reason"].toString() == "refusal") {
        result.error = "The model declined this request. Try rephrasing.";
        return result;
    }

    const juce::String json = extractJsonObject(text);
    if (json.isEmpty()) {
        result.error = "The model did not return a style. Try a clearer description.";
        return result;
    }

    result.ok = true;
    result.cstyleJson = json.toStdString();
    return result;
}
}
