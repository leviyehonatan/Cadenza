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

#include <atomic>
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
    void pitchBend(int channel, int value14);   // 14-bit, 8192 = centre
    void allNotesOff();

    // SoundFont loading (no-op for NullSynthEngine).
    bool loadSoundFont(const std::string& path);

    // Master 3-band EQ (low/mid/high gain in dB) applied to the final mix.
    void setEqGains(float lowDb, float midDb, float highDb) { m_masterEq.setGains(lowDb, midDb, highDb); }
    // Master compressor amount 0..100 (0 = bypass).
    void setCompAmount(int percent) { m_masterComp.setAmount(percent); }
    // Master output volume 0..127 (100 = unity). A final soft limiter keeps it
    // clean when pushed, so the whole mix (drums included) can hit harder.
    void setMasterVolume(int percent) { m_masterGain.store(juce::jlimit(0, 127, percent) / 100.0f); }
    // Master reverb amount 0..100 (maps to FluidSynth reverb level 0..~1.0).
    void setReverbLevel(int percent) { if (m_synth) m_synth->setReverbLevel(juce::jlimit(0, 100, percent) / 100.0); }
    const char* synthEngineName() const noexcept;
    bool supportsSoundFonts() const noexcept;

    // Master insert VST3 effect (e.g. an amp/cab sim on the arranger output).
    bool loadMasterEffect(const std::string& path, std::string& error);
    void clearMasterEffect();
    bool hasMasterEffect() const;
    std::string masterEffectName() const;

    // Per-part VST3 INSTRUMENTS. A channel (1..16) with a loaded instrument has
    // its notes routed to that plugin (and its audio summed into the mix) instead
    // of the FluidSynth SoundFont. When none are loaded there is zero overhead.
    bool loadPartInstrument(int channel, const std::string& path, std::string& error);
    void clearPartInstrument(int channel);
    void clearAllPartInstruments();
    bool hasPartInstrument(int channel) const;
    std::string partInstrumentName(int channel) const;
    std::string partInstrumentPath(int channel) const;   // path used to load (for persistence)
    void showPartInstrumentEditor(int channel);           // open the plugin's GUI (message thread)

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
    std::atomic<float> m_masterGain { 1.0f };   // master output volume (100% = unity)

    // Per-part instrument hosting (channels 1..16; index 0 unused).
    static constexpr int kNumChannels = 17;
    void renderPartInstruments(juce::AudioBuffer<float>& view);   // audio thread
    PluginHost               m_partInstrument[kNumChannels];
    juce::MidiMessageCollector m_partCollector[kNumChannels];
    std::atomic<bool>        m_partLoaded[kNumChannels] {};
    std::atomic<float>       m_partGain[kNumChannels] {};   // per-channel CC7 gain 0..1 (for VST parts)
    std::atomic<int>         m_partInstrumentCount { 0 };
    juce::AudioBuffer<float> m_partScratch;
    juce::MidiBuffer         m_partMidiScratch;
    std::string              m_partPath[kNumChannels];   // message-thread only
    double m_currentSampleRate = 48000.0;
    int    m_currentBlockSize  = 512;
    juce::MidiBuffer m_effectMidi;   // scratch (empty) MIDI for effect processing

    juce::AudioDeviceManager  m_deviceManager;
    juce::AudioSourcePlayer   m_sourcePlayer;

    TickCallback m_onTick;
};
}
