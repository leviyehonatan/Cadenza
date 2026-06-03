// AudioEngine — top-level juce::AudioSource for Cadenza.
// Owns the SynthEngine, Metronome, and Transport. Plugs into juce::AudioSourcePlayer
// which drives it from the JUCE AudioDeviceManager.

#pragma once

#include "MasterCompressor.h"
#include "MasterEq.h"
#include "MasterGlue.h"
#include "Metronome.h"
#include "PluginHost.h"
#include "SynthEngine.h"
#include "Transport.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>
#include <memory>

namespace cadenza::audio
{
class AudioEngine final : public juce::AudioSource
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // juce::AudioSource
    void prepareToPlay(int samplesPerBlock, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

    // Lifecycle helpers
    void startAudioDevice();
    void stopAudioDevice();

    // Transport control
    void play();
    void stop();
    void setBpm(double bpm);
    void setMetronomeEnabled(bool enabled) { m_metronome.setEnabled(enabled); }
    bool isMetronomeEnabled() const        { return m_metronome.isEnabled(); }

    // MIDI input — thread-safe.
    void noteOn(int channel, int note, int velocity);
    void noteOff(int channel, int note);
    void programChange(int channel, int program);
    void controlChange(int channel, int cc, int value);
    void allNotesOff();

    // SoundFont loading (no-op for NullSynthEngine).
    bool loadSoundFont(const std::string& path);

    // Master 3-band EQ (low/mid/high gain in dB) applied to the final mix.
    void setEqGains(float lowDb, float midDb, float highDb) { m_masterEq.setGains(lowDb, midDb, highDb); }
    // Master compressor amount 0..100 (0 = bypass).
    void setCompAmount(int percent) { m_masterComp.setAmount(percent); }
    const char* synthEngineName() const noexcept;
    bool supportsSoundFonts() const noexcept;

    // Master insert VST3 effect (e.g. an amp/cab sim on the arranger output).
    bool loadMasterEffect(const std::string& path, std::string& error);
    void clearMasterEffect();
    bool hasMasterEffect() const;
    std::string masterEffectName() const;

    // Block callback for the style engine to push notes on the audio thread.
    using TickCallback = std::function<void(int ticksAdvanced, Transport&)>;
    void setOnTick(TickCallback cb) { m_onTick = std::move(cb); }

    Transport& transport() noexcept { return m_transport; }
    SynthEngine& synth() noexcept   { return *m_synth; }

    // Output-device selection (so the user can avoid a coloured virtual device).
    juce::AudioDeviceManager& deviceManager() noexcept { return m_deviceManager; }
    // Restore a previously-saved device setup (createStateXml) before defaulting.
    void startAudioDevice(const juce::XmlElement* savedState);

private:
    std::unique_ptr<SynthEngine> m_synth;
    Metronome m_metronome;
    Transport m_transport;
    PluginHost m_masterEffect;
    MasterEq         m_masterEq;
    MasterCompressor m_masterComp;
    MasterGlue       m_masterGlue;
    juce::MidiBuffer m_effectMidi;   // scratch (empty) MIDI for effect processing

    juce::AudioDeviceManager  m_deviceManager;
    juce::AudioSourcePlayer   m_sourcePlayer;

    TickCallback m_onTick;
};
}
