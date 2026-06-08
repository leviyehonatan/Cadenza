#include "AudioEngine.h"
#include "AudioBlockPipeline.h"
#include "MidiChannel.h"

#include <algorithm>
#include <cmath>

namespace cadenza::audio
{
namespace
{
// Soft limiter: linear up to 0.9, then a tanh knee so peaks stay below ~1.0
// instead of hard-clipping when the master volume is pushed.
inline float softLimit(float x) noexcept
{
    constexpr float t = 0.9f;
    if (x >  t) return  t + std::tanh((x - t) * 2.0f) * (1.0f - t);
    if (x < -t) return -t + std::tanh((x + t) * 2.0f) * (1.0f - t);
    return x;
}
}

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
    m_masterComp.prepare(sampleRate);
    // Master-bus compression is OFF by default: with an arranger the kick + bass
    // make a low-threshold comp duck the whole band on every beat, and adding a
    // right-hand melody pumps it harder ("loses power"). Peaks are already caught
    // cleanly by the final soft-limiter, so the mix stays loud without breathing.
    m_masterComp.setEnabled(false);
    m_masterGlue.prepare(sampleRate);

    m_currentSampleRate = sampleRate;
    m_currentBlockSize  = samplesPerBlock > 0 ? samplesPerBlock : 512;
    m_partScratch.setSize(2, m_currentBlockSize, false, false, true);
    for (int ch = 1; ch < kNumChannels; ++ch) {
        m_partCollector[ch].reset(sampleRate);
        m_partInstrument[ch].prepare(sampleRate, m_currentBlockSize);
        m_partGain[ch].store(1.0f);
    }
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
            renderPartInstruments(view);   // sum any per-part VST instruments (no-op if none)
        },
        [&] {  // 3) metronome clicks on top
            m_metronome.renderBlock(view, m_transport);
        },
        [&] {  // 4) master chain: EQ -> Airwindows console glue -> insert effect
            float* const* chans = view.getArrayOfWritePointers();
            const int nc = view.getNumChannels();
            const int ns = view.getNumSamples();
            m_masterEq.process(chans, nc, ns);     // tone + tame peaks
            m_masterComp.process(chans, nc, ns);   // gentle master glue/density
            m_masterGlue.process(chans, nc, ns);   // console glue (no-op: disabled by default)
            m_effectMidi.clear();
            m_masterEffect.process(view, m_effectMidi);

            // Master output volume + final soft limiter: lets the whole mix (drums
            // included) be pushed loud and stay clean instead of hard-clipping.
            const float gain = m_masterGain.load();
            for (int c = 0; c < nc; ++c) {
                float* d = chans[c];
                for (int s = 0; s < ns; ++s)
                    d[s] = softLimit(d[s] * gain);
            }
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

    // Tighter live latency: if the device opened with a large block, nudge it
    // down toward a live-play buffer so right-hand melody feels responsive. Only
    // ever reduce (never raise) so we respect a deliberately-larger saved choice.
    {
        auto setup = m_deviceManager.getAudioDeviceSetup();
        constexpr int kLiveBuffer = 256;
        if (setup.bufferSize > kLiveBuffer) {
            setup.bufferSize = kLiveBuffer;
            m_deviceManager.setAudioDeviceSetup(setup, true);
        }
    }

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
    if (channel > 0 && channel < kNumChannels && m_partLoaded[channel].load()) {
        auto m = juce::MidiMessage::noteOn(1, note, (juce::uint8) velocity);
        m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        m_partCollector[channel].addMessageToQueue(m);   // thread-safe intake
        return;
    }
    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->noteOn(*synthChannel, note, velocity);
    }
}

void AudioEngine::noteOff(int channel, int note)
{
    if (channel > 0 && channel < kNumChannels && m_partLoaded[channel].load()) {
        auto m = juce::MidiMessage::noteOff(1, note);
        m.setTimeStamp(juce::Time::getMillisecondCounterHiRes() * 0.001);
        m_partCollector[channel].addMessageToQueue(m);
        return;
    }
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
    // CC7 (volume) doubles as the per-part gain for VST-instrument channels, so
    // the mixer fader / mute / solo (sent as effective CC7) affects them too.
    if (cc == 7 && channel > 0 && channel < kNumChannels)
        m_partGain[channel].store(std::clamp(value, 0, 127) / 127.0f);

    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->controlChange(*synthChannel, cc, value);
    }
}

void AudioEngine::pitchBend(int channel, int value14)
{
    // Pitch bend goes to the internal synth only; VST-instrument parts would
    // need a MIDI pitch-bend in their collector (not wired yet — rare on the
    // accompaniment channels that use VSTs).
    if (m_synth) {
        if (auto synthChannel = synthChannelFromCadenzaChannel(channel))
            m_synth->pitchBend(*synthChannel, value14);
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

// ---- per-part VST instruments ----

void AudioEngine::renderPartInstruments(juce::AudioBuffer<float>& view)
{
    if (m_partInstrumentCount.load() <= 0)
        return;   // common case: no per-part instruments — zero work

    const int ns = view.getNumSamples();
    const int nc = juce::jmin(view.getNumChannels(), 2);

    for (int ch = 1; ch < kNumChannels; ++ch) {
        if (!m_partLoaded[ch].load())
            continue;

        juce::AudioBuffer<float> scratch(m_partScratch.getArrayOfWritePointers(), 2, ns);
        scratch.clear();
        m_partMidiScratch.clear();
        m_partCollector[ch].removeNextBlockOfMessages(m_partMidiScratch, ns);
        m_partInstrument[ch].process(scratch, m_partMidiScratch);   // VST instrument: MIDI -> audio

        const float gain = m_partGain[ch].load();
        for (int c = 0; c < nc; ++c)
            view.addFrom(c, 0, scratch, c, 0, ns, gain);   // mixer fader / mute / solo
    }
}

bool AudioEngine::loadPartInstrument(int channel, const std::string& path, std::string& error)
{
    if (channel <= 0 || channel >= kNumChannels) { error = "invalid channel"; return false; }
    // Idempotent: if this exact plugin is already loaded on this channel, keep it.
    // This makes re-applying a style (or switching to one that reuses the same
    // plugin) cheap instead of tearing the instance down and rebuilding it.
    if (m_partLoaded[channel].load() && m_partPath[channel] == path)
        return true;
    m_partInstrument[channel].prepare(m_currentSampleRate, m_currentBlockSize);
    juce::String err;
    if (!m_partInstrument[channel].loadFromFile(juce::String(path), err)) {
        error = err.toStdString();
        return false;
    }
    m_partPath[channel] = path;
    if (!m_partLoaded[channel].exchange(true))
        m_partInstrumentCount.fetch_add(1);
    return true;
}

void AudioEngine::clearPartInstrument(int channel)
{
    if (channel <= 0 || channel >= kNumChannels) return;
    if (m_partLoaded[channel].exchange(false))
        m_partInstrumentCount.fetch_sub(1);
    m_partInstrument[channel].clear();
    m_partPath[channel].clear();
}

void AudioEngine::clearAllPartInstruments()
{
    for (int ch = 1; ch < kNumChannels; ++ch)
        clearPartInstrument(ch);
}

std::string AudioEngine::partInstrumentPath(int channel) const
{
    if (channel <= 0 || channel >= kNumChannels) return {};
    return m_partPath[channel];
}

bool AudioEngine::hasPartInstrument(int channel) const
{
    return channel > 0 && channel < kNumChannels && m_partLoaded[channel].load();
}

std::string AudioEngine::partInstrumentName(int channel) const
{
    if (channel <= 0 || channel >= kNumChannels) return {};
    return m_partInstrument[channel].name().toStdString();
}

void AudioEngine::showPartInstrumentEditor(int channel)
{
    if (channel <= 0 || channel >= kNumChannels) return;
    m_partInstrument[channel].showEditor(
        juce::String(m_partInstrument[channel].name()) + "  (part " + juce::String(channel) + ")");
}
}
