#include "PluginHost.h"

namespace cadenza::audio
{
PluginHost::PluginHost()
{
    m_formatManager.addFormat(new juce::VST3PluginFormatHeadless());
}

PluginHost::~PluginHost()
{
    clear();
}

bool PluginHost::loadFromFile(const juce::String& path, juce::String& error)
{
    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, path);

    if (descriptions.isEmpty()) {
        error = "No VST3 sub-plugins found in " + path;
        return false;
    }

    auto instance = m_formatManager.createPluginInstance(
        *descriptions[0], m_sampleRate, m_blockSize, error);
    if (instance == nullptr)
        return false;

    // Prepare BEFORE publishing the pointer so the audio thread never sees an
    // unprepared instance.
    instance->setPlayConfigDetails(2, 2, m_sampleRate, m_blockSize);
    instance->prepareToPlay(m_sampleRate, m_blockSize);
    const auto loadedName = instance->getName();

    std::unique_ptr<juce::AudioPluginInstance> previous;
    {
        const juce::ScopedLock sl(m_lock);
        previous = std::move(m_plugin);
        m_plugin = std::move(instance);
        m_name = loadedName;
    }
    if (previous)
        previous->releaseResources();   // destroy the old one outside the lock

    return true;
}

void PluginHost::clear()
{
    std::unique_ptr<juce::AudioPluginInstance> previous;
    {
        const juce::ScopedLock sl(m_lock);
        previous = std::move(m_plugin);
        m_name = {};
    }
    if (previous)
        previous->releaseResources();
}

void PluginHost::prepare(double sampleRate, int blockSize)
{
    m_sampleRate = sampleRate;
    m_blockSize  = blockSize;

    const juce::ScopedLock sl(m_lock);
    if (m_plugin) {
        m_plugin->setPlayConfigDetails(2, 2, sampleRate, blockSize);
        m_plugin->prepareToPlay(sampleRate, blockSize);
    }
}

bool PluginHost::isLoaded() const
{
    const juce::ScopedLock sl(m_lock);
    return m_plugin != nullptr;
}

juce::String PluginHost::name() const
{
    const juce::ScopedLock sl(m_lock);
    return m_name;
}

void PluginHost::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedTryLock stl(m_lock);
    if (!stl.isLocked())        // a load/clear is in progress — pass audio through untouched
        return;
    if (m_plugin == nullptr)
        return;

    m_plugin->processBlock(buffer, midi);
}
}
