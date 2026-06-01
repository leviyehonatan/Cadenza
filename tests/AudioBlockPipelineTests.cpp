#include "Audio/AudioBlockPipeline.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;
void expect(bool cond, const std::string& msg) {
    if (cond) return;
    ++failures;
    std::cerr << "FAIL: " << msg << '\n';
}

int indexOf(const std::vector<std::string>& v, const std::string& s)
{
    const auto it = std::find(v.begin(), v.end(), s);
    return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}

void firesDueEventsBeforeRenderingSynth()
{
    std::vector<std::string> order;
    cadenza::audio::runAudioBlock(
        [&] { order.push_back("fire"); },
        [&] { order.push_back("render"); },
        [&] { order.push_back("metronome"); },
        [&] { order.push_back("effect"); });

    expect(order.size() == 4, "all four stages run exactly once");

    // The whole point of the fix: due MIDI is scheduled before the synth renders.
    expect(indexOf(order, "fire") < indexOf(order, "render"),
           "fireDueEvents must run before renderSynth (no one-block latency)");

    // Synth audio exists before the metronome mixes on top of it.
    expect(indexOf(order, "render") < indexOf(order, "metronome"),
           "synth renders before metronome");

    // Master VST3 effect always processes the final mix last.
    expect(indexOf(order, "metronome") < indexOf(order, "effect"),
           "metronome before master effect");
    expect(indexOf(order, "effect") == 3, "master effect is the final stage");
}

void fullExpectedSequence()
{
    std::vector<std::string> order;
    cadenza::audio::runAudioBlock(
        [&] { order.push_back("fire"); },
        [&] { order.push_back("render"); },
        [&] { order.push_back("metronome"); },
        [&] { order.push_back("effect"); });

    const std::vector<std::string> expected = { "fire", "render", "metronome", "effect" };
    expect(order == expected, "block pipeline runs in fire->render->metronome->effect order");
}
}

int main()
{
    firesDueEventsBeforeRenderingSynth();
    fullExpectedSequence();

    if (failures != 0) return EXIT_FAILURE;
    std::cout << "All AudioBlockPipeline tests passed\n";
    return EXIT_SUCCESS;
}
