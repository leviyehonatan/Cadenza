#include "PluginHost.h"

namespace cadenza::audio
{
namespace
{
// A simple top-level window that owns a plugin's editor. Closing it tells the
// PluginHost to drop the window (which deletes the editor before the plugin).
class PluginEditorWindow final : public juce::DocumentWindow
{
public:
    PluginEditorWindow(const juce::String& title, juce::AudioProcessorEditor* editor)
        : juce::DocumentWindow(title, juce::Colours::darkgrey,
                               juce::DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(editor, true);          // window owns/deletes the editor
        setResizable(editor->isResizable(), false);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
        setAlwaysOnTop(true);
    }

    void closeButtonPressed() override { if (onClose) onClose(); }

    std::function<void()> onClose;
};
}

PluginHost::PluginHost()
{
    m_formatManager.addFormat(new juce::VST3PluginFormat());
}

PluginHost::~PluginHost()
{
    clear();
}

bool PluginHost::loadFromFile(const juce::String& path, juce::String& error)
{
    juce::VST3PluginFormat format;
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

void PluginHost::showEditor(const juce::String& title)
{
    if (m_editorWindow != nullptr) {        // already open — just bring it forward
        m_editorWindow->toFront(true);
        return;
    }

    juce::AudioProcessorEditor* editor = nullptr;
    {
        const juce::ScopedLock sl(m_lock);  // audio thread only try-locks, so it skips
        if (m_plugin == nullptr)
            return;
        if (!m_plugin->hasEditor())
            return;                          // headless / GUI-less plugin
        editor = m_plugin->createEditorIfNeeded();
    }
    if (editor == nullptr)
        return;

    auto* window = new PluginEditorWindow(title.isNotEmpty() ? title : name(), editor);
    window->onClose = [this] { closeEditor(); };
    m_editorWindow.reset(window);
}

void PluginHost::closeEditor()
{
    m_editorWindow.reset();   // deletes the editor (notifies the plugin) before the plugin
}

void PluginHost::clear()
{
    closeEditor();            // editor references the plugin: destroy it first

    std::unique_ptr<juce::AudioPluginInstance> previous;
    {
        const juce::ScopedLock sl(m_lock);
        previous = std::move(m_plugin);
        m_name = {};
    }
    if (previous)
        previous->releaseResources();
}

void PluginHost::applyStateBase64(const juce::String& base64)
{
    if (base64.isEmpty())
        return;
    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, base64))
        return;
    const juce::ScopedLock sl(m_lock);
    if (m_plugin != nullptr)
        m_plugin->setStateInformation(decoded.getData(), static_cast<int>(decoded.getDataSize()));
}

juce::String PluginHost::captureStateBase64() const
{
    juce::MemoryBlock block;
    {
        const juce::ScopedLock sl(m_lock);
        if (m_plugin == nullptr)
            return {};
        m_plugin->getStateInformation(block);
    }
    return juce::Base64::toBase64(block.getData(), block.getSize());
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
