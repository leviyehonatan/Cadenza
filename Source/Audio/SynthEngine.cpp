#include "SynthEngine.h"

#if defined(CADENZA_HAVE_FLUIDSYNTH)
  #include <fluidsynth.h>
  #include <mutex>
#endif

namespace cadenza::audio
{
// ============================================================
// NullSynthEngine — logs to JUCE Logger; produces silence.
// ============================================================
void NullSynthEngine::renderBlock(juce::AudioBuffer<float>& buffer)
{
    buffer.clear();
}

void NullSynthEngine::noteOn(int channel, int note, int velocity)
{
    juce::Logger::writeToLog("[NullSynth] noteOn ch=" + juce::String(channel)
                             + " n=" + juce::String(note)
                             + " v=" + juce::String(velocity));
}

void NullSynthEngine::noteOff(int channel, int note)
{
    juce::Logger::writeToLog("[NullSynth] noteOff ch=" + juce::String(channel)
                             + " n=" + juce::String(note));
}

void NullSynthEngine::programChange(int channel, int program)
{
    juce::Logger::writeToLog("[NullSynth] programChange ch=" + juce::String(channel)
                             + " pg=" + juce::String(program));
}

void NullSynthEngine::controlChange(int channel, int controller, int value)
{
    juce::Logger::writeToLog("[NullSynth] controlChange ch=" + juce::String(channel)
                             + " cc=" + juce::String(controller)
                             + " value=" + juce::String(value));
}

void NullSynthEngine::allNotesOff()
{
    juce::Logger::writeToLog("[NullSynth] allNotesOff");
}

// ============================================================
// FluidSynthEngine — wraps fluidsynth (if available)
// ============================================================
#if defined(CADENZA_HAVE_FLUIDSYNTH)

class FluidSynthEngine final : public SynthEngine
{
public:
    FluidSynthEngine() {
        m_settings = new_fluid_settings();
        if (m_settings) {
            // Configure for offline-rendering style (we render into JUCE's buffer ourselves).
            fluid_settings_setstr(m_settings, "audio.driver", "file");
            fluid_settings_setnum(m_settings, "synth.gain", 0.5);   // headroom; master limiter catches peaks
            fluid_settings_setint(m_settings, "synth.audio-channels", 1);
            fluid_settings_setint(m_settings, "synth.lock-memory", 0);
            fluid_settings_setint(m_settings, "synth.drums-channel.active", 1);

            // Ambience: drive an explicit hall-ish reverb + light chorus so the
            // band has depth and air instead of the dry, dead "GM default" sound.
            // Parts still control how much they send via CC91/CC93.
            fluid_settings_setint(m_settings, "synth.reverb.active", 1);
            fluid_settings_setint(m_settings, "synth.chorus.active", 1);
            fluid_settings_setnum(m_settings, "synth.reverb.room-size", 0.75);
            fluid_settings_setnum(m_settings, "synth.reverb.damp", 0.30);
            fluid_settings_setnum(m_settings, "synth.reverb.width", 0.90);
            fluid_settings_setnum(m_settings, "synth.reverb.level", 0.80);

            m_synth = new_fluid_synth(m_settings);
            juce::Logger::writeToLog("[Cadenza] FluidSynth GM drum channel active: synth channel 9; reverb+chorus on");
        }
    }

    ~FluidSynthEngine() override {
        if (m_synth) delete_fluid_synth(m_synth);
        if (m_settings) delete_fluid_settings(m_settings);
    }

    void prepare(double sampleRate, int /*blockSize*/) override {
        SynthEngine::prepare(sampleRate, 0);
        if (m_settings) {
            fluid_settings_setnum(m_settings, "synth.sample-rate", sampleRate);
        }
    }

    void renderBlock(juce::AudioBuffer<float>& buffer) override {
        buffer.clear();
        if (!m_synth) return;

        const int n = buffer.getNumSamples();
        float* left  = buffer.getWritePointer(0);
        float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : left;

        // fluid_synth_write_float writes interleaved into two separate float arrays.
        const std::lock_guard<std::mutex> lock(m_mutex);
        fluid_synth_write_float(m_synth, n, left, 0, 1, right, 0, 1);
    }

    void noteOn(int channel, int note, int velocity) override {
        if (!m_synth) return;
        const std::lock_guard<std::mutex> lock(m_mutex);
        fluid_synth_noteon(m_synth, channel, note, velocity);
    }

    void noteOff(int channel, int note) override {
        if (!m_synth) return;
        const std::lock_guard<std::mutex> lock(m_mutex);
        fluid_synth_noteoff(m_synth, channel, note);
    }

    void programChange(int channel, int program) override {
        if (!m_synth) return;
        const std::lock_guard<std::mutex> lock(m_mutex);

        // Select the requested voice (the channel's bank was set by prior CC0/CC32).
        fluid_synth_program_change(m_synth, channel, program);

        // Cross-SoundFont fallback: if the requested bank/program isn't in the
        // loaded SoundFont, FluidSynth leaves the channel with no preset. So a
        // Yamaha/XG style voice plays correctly on an XG SoundFont (e.g. Timbres
        // of Heaven) but still gets a sensible voice on a plain GM SoundFont.
        if (fluid_synth_get_channel_preset(m_synth, channel) != nullptr)
            return;   // voice found in this SoundFont — nothing to do

        if (channel == kDrumChannel) {
            // Keep percussion: fall back to the standard kit on the drum bank
            // (never bank 0 here — that would turn drums into a melodic voice).
            fluid_synth_program_change(m_synth, channel, 0);
        } else {
            // Fall back to the GM bank for this program number.
            fluid_synth_bank_select(m_synth, channel, 0);
            fluid_synth_program_change(m_synth, channel, program);
        }
    }

    void controlChange(int channel, int controller, int value) override {
        if (!m_synth) return;
        const std::lock_guard<std::mutex> lock(m_mutex);
        fluid_synth_cc(m_synth, channel, controller, value);
    }

    void allNotesOff() override {
        if (!m_synth) return;
        const std::lock_guard<std::mutex> lock(m_mutex);
        for (int ch = 0; ch < 16; ++ch) fluid_synth_all_notes_off(m_synth, ch);
    }

    bool loadSoundFont(const std::string& path) override {
        if (!m_synth) return false;
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (m_soundFontId >= 0) {
            fluid_synth_sfunload(m_synth, m_soundFontId, 1);
            m_soundFontId = -1;
        }
        m_soundFontId = fluid_synth_sfload(m_synth, path.c_str(), 1);
        return m_soundFontId >= 0;
    }

    const char* engineName() const noexcept override { return "FluidSynthEngine"; }
    bool supportsSoundFonts() const noexcept override { return true; }

private:
    // GM percussion channel (0-based). FluidSynth synth.drums-channel.active makes
    // synth channel 9 the drum channel; voice fallback must not move it off the
    // percussion bank.
    static constexpr int kDrumChannel = 9;

    fluid_settings_t* m_settings = nullptr;
    fluid_synth_t*    m_synth = nullptr;
    int               m_soundFontId = -1;
    std::mutex        m_mutex;
};

#endif

// ============================================================
// Factory
// ============================================================
std::unique_ptr<SynthEngine> createSynthEngine()
{
#if defined(CADENZA_HAVE_FLUIDSYNTH)
    return std::make_unique<FluidSynthEngine>();
#else
    return std::make_unique<NullSynthEngine>();
#endif
}
}
