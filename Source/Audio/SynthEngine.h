// SynthEngine — interface to a polyphonic MIDI synth.
// Two implementations:
//   - NullSynthEngine: logs notes, no audio output (always available).
//   - FluidSynthEngine: real FluidSynth-backed synth (compiled when CADENZA_HAVE_FLUIDSYNTH is defined).
//
// Audio rendering happens on the audio thread. Note-on/off may arrive from
// either the message thread (web bridge) or audio thread (live MIDI input)
// — implementations must be thread-safe for those calls.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <string>

namespace cadenza::audio
{
class SynthEngine
{
public:
    virtual ~SynthEngine() = default;

    // Called once before audio starts.
    virtual void prepare(double sampleRate, int /*blockSize*/) { m_sampleRate = sampleRate; }
    virtual void release() {}

    // Render audio into the buffer. May be silent.
    virtual void renderBlock(juce::AudioBuffer<float>& buffer) = 0;

    // Note-on / note-off / program-change / control-change — thread-safe.
    virtual void noteOn(int channel, int note, int velocity) = 0;
    virtual void noteOff(int channel, int note) = 0;
    virtual void programChange(int channel, int program) = 0;
    virtual void controlChange(int channel, int controller, int value) = 0;
    virtual void allNotesOff() = 0;

    // Optional: load a SoundFont file. Returns true on success. Default impl is a no-op.
    virtual bool loadSoundFont(const std::string& /*path*/) { return false; }
    // Optional: master reverb send level (0..~1.2). Default no-op.
    virtual void setReverbLevel(double /*level*/) {}
    virtual const char* engineName() const noexcept { return "SynthEngine"; }
    virtual bool supportsSoundFonts() const noexcept { return false; }

    double sampleRate() const noexcept { return m_sampleRate; }

protected:
    double m_sampleRate = 48000.0;
};

class NullSynthEngine final : public SynthEngine
{
public:
    void renderBlock(juce::AudioBuffer<float>& buffer) override;
    void noteOn(int channel, int note, int velocity) override;
    void noteOff(int channel, int note) override;
    void programChange(int channel, int program) override;
    void controlChange(int channel, int controller, int value) override;
    void allNotesOff() override;
    const char* engineName() const noexcept override { return "NullSynthEngine"; }
};

// Factory — returns FluidSynthEngine if available, NullSynthEngine otherwise.
std::unique_ptr<SynthEngine> createSynthEngine();
}
