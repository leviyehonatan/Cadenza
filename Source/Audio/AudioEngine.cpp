#include "AudioEngine.h"
#include "AudioBlockPipeline.h"
#include "MidiChannel.h"

namespace cadenza::audio
{
AudioEngine::AudioEngine()
    : m_synth(createSynthEngine())
{
}

AudioEngine::~AudioEngine()
{
    stopAudioDevice();
}

void AudioEngine::prepareToPlay(int samplesPerBlock, double sampleRate)
{
    m_transport.setSampleRate(sampleRate);
    m_metronome.prepare(sampleRate);
    if (m_synth) m_synth->prepare(sampleRate, 0);
    m_masterEffect.prepare(sampleRate, samplesPerBlock > 0 ? samplesPerBlock : 512);
    m_masterEq.prepare(sampleRate, 2);
    m_masterGlue.prepare(sampleRate);
}

void AudioEngine::releaseResources()
{
    if (m_synth) m_synth->release();
}

void AudioEngine::getNextAudioBlock(const juce::AudioSourceChannelInfo& info)
{
    auto* buffer = info.buffer;

    // Convenience wrapper buffer over the requested slice.
    juce::AudioBuffer<float> view(buffer->getArrayOfWritePointers(),
                                  buffer->getNumChannels(),
                                  info.startSample,
                                  info.numSamples);

    // Live-playback timing: events for this block are fired BEFORE the synth
    // renders, so style note-ons/offs are audible in the same block instead of
    // one block late. See AudioBlockPipeline.h for the ordering rationale.
    runAudioBlock(
        [&] {  // 1) advance transport and let StyleEngine push due MIDI to the synth
            const int ticksAdvanced = m_transport.advance(info.numSamples);
            if (m_onTick && ticksAdvanced > 0)
                m_onTick(ticksAdvanced, m_transport);
        },
        [&] {  // 2) render synth (clears buffer first, then renders queued notes)
            if (m_synth) m_synth->renderBlock(view);
            else view.clear();
        },
        [&] {  // 3) metronome clicks on top
            m_metronome.renderBlock(view, m_transport);
        },
        [&] {  // 4) master chain: EQ -> Airwindows console glue -> insert effect
            float* const* chans = view.getArrayOfWritePointers();
            const int nc = view.getNumChannels();
            const int ns = view.getNumSamples();
            m_masterEq.process(chans, nc, ns);     // tone + tame peaks
            m_masterGlue.process(chans, nc, ns);   // console glue (no-op: disabled by default)
            m_effectMidi.clear();
            m_masterEffect.process(view, m_effectMidi);
        });
}

void AudioEngine::startAudioDevice() { startAudioDevice(nullptr); }

void AudioEngine::startAudioDevice(const juce::XmlElement* savedState)
{
    // Restore the user's saved device choice if present; otherwise default stereo.
    juce::String error;
    if (savedState != nullptr)
        error = m_deviceManager.initialise(0, 2, savedState, true);
    else
        error = m_deviceManager.initialiseWithDefaultDevices(0, 2);
    m_sourcePlayer.setSource(this);
    m_deviceManager.addAudioCallback(&m_sourcePlayer);

    // Log the resulting audio output so a "no sound" problem can be diagnosed.
    if (error.isNotEmpty()) {
        juce::Logger::writeToLog("[Cadenza] AUDIO DEVICE ERROR: " + error);
    } else if (auto* dev = m_deviceManager.getCurrentAudioDevice()) {
        juce::Logger::writeToLog("[Cadenza] Audio device: \"" + dev->getName() + "\""
            + " open=" + juce::String(dev->isOpen() ? "yes" : "no")
            + " playing=" + juce::String(dev->isPlaying() ? "yes" : "no")
            + " sampleRate=" + juce::String(dev->getCurrentSampleRate())
            + " bufferSize=" + juce::String(dev->getCurrentBufferSizeSamples())
            + " outputChannels=" + juce::String(dev->getActiveOutputChannels().toInteger()));
    } else {
        juce::Logger::writeToLog("[Cadenza] AUDIO DEVICE: none opened (no output device available).");
    }
}

void AudioEngine::stopAudioDevice()
{
    m_deviceManager.removeAudioCallback(&m_sourcePlayer);
    m_sourcePlayer.setSource(nullptr);
}

void AudioEngine::play()
{
    if (!m_transport.playing())
        m_transport.startFromBeginning();
}

void AudioEngine::stop()  { m_transport.stop(); if (m_synth) m_synth->allNotesOff(); }
void AudioEngine::setBpm(double bpm) { m_transport.setBpm(bpm); }

void AudioEngine::noteOn(int channel, int note, int velocity)
{
    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->noteOn(*synthChannel, note, velocity);
    }
}

void AudioEngine::noteOff(int channel, int note)
{
    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->noteOff(*synthChannel, note);
    }
}

void AudioEngine::programChange(int channel, int program)
{
    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->programChange(*synthChannel, program);
    }
}

void AudioEngine::controlChange(int channel, int cc, int value)
{
    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->controlChange(*synthChannel, cc, value);
    }
}

void AudioEngine::allNotesOff()
{
    if (m_synth) m_synth->allNotesOff();
}

bool AudioEngine::loadSoundFont(const std::string& path)
{
    return m_synth ? m_synth->loadSoundFont(path) : false;
}

const char* AudioEngine::synthEngineName() const noexcept
{
    return m_synth ? m_synth->engineName() : "NoSynthEngine";
}

bool AudioEngine::supportsSoundFonts() const noexcept
{
    return m_synth != nullptr && m_synth->supportsSoundFonts();
}

bool AudioEngine::loadMasterEffect(const std::string& path, std::string& error)
{
    juce::String err;
    const bool ok = m_masterEffect.loadFromFile(juce::String(path), err);
    if (!ok) error = err.toStdString();
    return ok;
}

void AudioEngine::clearMasterEffect()        { m_masterEffect.clear(); }
bool AudioEngine::hasMasterEffect() const    { return m_masterEffect.isLoaded(); }
std::string AudioEngine::masterEffectName() const { return m_masterEffect.name().toStdString(); }
}
