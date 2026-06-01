// AudioBlockPipeline — the canonical per-audio-block processing order for live
// arranger playback, factored out so the ordering contract is unit-testable
// (the real audio callback in AudioEngine is not).
//
// CRITICAL ORDERING: transport/StyleEngine events must be fired BEFORE the synth
// renders the block. If the synth renders first and notes are scheduled after,
// those notes are not heard until the *next* block — a one-block latency penalty
// on every style event. Firing first lets the just-scheduled note-ons/offs land
// in the synth's event queue before it renders this block's audio.
//
// Pure header (no JUCE) so it links into both the app and the test suite.

#pragma once

namespace cadenza::audio
{
// Runs the four per-block stages in the required order. Each argument is a
// nullary callable (lambda) supplied by AudioEngine:
//   1. fireDueEvents      — advance Transport + let StyleEngine push due MIDI to the synth
//   2. renderSynth        — synth renders this block (clears the buffer, then renders)
//   3. renderMetronome    — metronome clicks mixed on top
//   4. processMasterEffect— master VST3 insert processes the final mix
template <class FireDueEvents,
          class RenderSynth,
          class RenderMetronome,
          class ProcessMasterEffect>
void runAudioBlock(FireDueEvents&& fireDueEvents,
                   RenderSynth&& renderSynth,
                   RenderMetronome&& renderMetronome,
                   ProcessMasterEffect&& processMasterEffect)
{
    fireDueEvents();          // 1) schedule MIDI for this block FIRST
    renderSynth();            // 2) render audio (now includes those events)
    renderMetronome();        // 3) metronome on top
    processMasterEffect();    // 4) master effect on the final mix
}
}
