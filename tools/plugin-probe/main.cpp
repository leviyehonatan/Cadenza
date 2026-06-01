// plugin-probe — load a VST3 plugin headlessly, report its descriptor and I/O,
// and push one audio block through it. A CLI smoke test for Cadenza's VST3
// hosting path (same JUCE module + format used by the app's PluginHost).
//
// Usage: plugin-probe <path-to-plugin.vst3>
// Exit code 0 on success, non-zero on failure.

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: plugin-probe <path-to-plugin.vst3>\n");
        return 2;
    }

    // VST3 scanning/instantiation needs a message manager.
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::String path = juce::CharPointer_UTF8(argv[1]);
    const double sampleRate = 48000.0;
    const int    blockSize  = 512;

    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> descriptions;
    format.findAllTypesForFile(descriptions, path);

    if (descriptions.isEmpty()) {
        std::printf("FAIL: no VST3 sub-plugins found in %s\n", argv[1]);
        return 1;
    }

    for (auto* d : descriptions) {
        std::printf("type: name='%s' vendor='%s' category='%s' isInstrument=%d\n",
                    d->name.toRawUTF8(),
                    d->manufacturerName.toRawUTF8(),
                    d->category.toRawUTF8(),
                    static_cast<int>(d->isInstrument));
    }

    juce::AudioPluginFormatManager manager;
    manager.addFormat(new juce::VST3PluginFormatHeadless());

    juce::String error;
    auto instance = manager.createPluginInstance(*descriptions[0], sampleRate, blockSize, error);
    if (instance == nullptr) {
        std::printf("FAIL: createPluginInstance: %s\n", error.toRawUTF8());
        return 1;
    }

    std::printf("loaded: '%s' inputs=%d outputs=%d acceptsMidi=%d producesMidi=%d\n",
                instance->getName().toRawUTF8(),
                instance->getTotalNumInputChannels(),
                instance->getTotalNumOutputChannels(),
                static_cast<int>(instance->acceptsMidi()),
                static_cast<int>(instance->producesMidi()));

    instance->setPlayConfigDetails(2, 2, sampleRate, blockSize);
    instance->prepareToPlay(sampleRate, blockSize);

    // Feed a unit impulse through both channels so an effect produces output.
    juce::AudioBuffer<float> buffer(2, blockSize);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f);
    buffer.setSample(1, 0, 1.0f);

    juce::MidiBuffer midi;
    instance->processBlock(buffer, midi);

    std::printf("processed one block: out RMS ch0=%.6f ch1=%.6f\n",
                buffer.getRMSLevel(0, 0, blockSize),
                buffer.getRMSLevel(1, 0, blockSize));

    instance->releaseResources();
    std::printf("OK\n");
    return 0;
}
