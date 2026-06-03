// PluginHost — loads a single VST3 plugin and processes audio through it.
//
// Used as a master insert effect in AudioEngine and as per-part instruments.
// Loading/unloading happens on the message thread; process() runs on the audio
// thread and never blocks (it skips a block if a load/clear is mid-flight).
// Uses the full JUCE plugin-hosting module so the plugin's own editor GUI can
// be shown in a window via showEditor().

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace cadenza::audio
{
class PluginHost
{
public:
    PluginHost();
    ~PluginHost();

    // ---- message thread ----
    // Load a .vst3 (file or bundle). Returns false and sets `error` on failure;
    // any previously-loaded plugin stays active on failure.
    bool loadFromFile(const juce::String& path, juce::String& error);
    void clear();
    void prepare(double sampleRate, int blockSize);

    bool        isLoaded() const;
    juce::String name() const;

    // Open (or focus) the plugin's own editor GUI in a resizable window.
    // No-op if nothing is loaded or the plugin has no editor. Message thread only.
    void showEditor(const juce::String& title);
    void closeEditor();   // message thread only

    // ---- audio thread ----
    void process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

private:
    juce::AudioPluginFormatManager m_formatManager;
    mutable juce::CriticalSection  m_lock;     // guards m_plugin / m_name
    std::unique_ptr<juce::AudioPluginInstance> m_plugin;
    std::unique_ptr<juce::DocumentWindow> m_editorWindow;   // message thread only
    juce::String m_name;
    double m_sampleRate = 48000.0;
    int    m_blockSize  = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};
}
