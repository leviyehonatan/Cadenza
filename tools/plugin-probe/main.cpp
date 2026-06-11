// plugin-probe — load a VST3 plugin headlessly, report its descriptor and I/O,
// and push one audio block through it. A CLI smoke test for Cadenza's VST3
// hosting path (same JUCE module + format used by the app's PluginHost).
//
// Usage: plugin-probe <path-to-plugin.vst3> [--pjson <preset.pjson>]
//                     [--note <0..127>] [--blocks <N>]
//
// --pjson restores a Dinamo Audio program preset: the JSON's
// program.instrument.state field holds a hex-encoded JUCE VST3 state blob
// which is passed to setStateInformation before rendering.
// --note renders a note-on through the instrument for N blocks and reports
// the peak RMS, proving the plugin actually produces audio (samples found).
//
// Exit code 0 on success, non-zero on failure.

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <cstdio>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: plugin-probe <path-to-plugin.vst3> [--pjson <preset.pjson>] "
                    "[--note <0..127>] [--blocks <N>]\n");
        return 2;
    }

    // VST3 scanning/instantiation needs a message manager.
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::String path = juce::CharPointer_UTF8(argv[1]);
    juce::String pjsonPath;
    int note = -1;
    int renderBlocks = 200;
    int programChange = -1;
    bool listParams = false;
    for (int i = 2; i < argc; ++i) {
        const juce::String arg = juce::CharPointer_UTF8(argv[i]);
        if (arg == "--pjson" && i + 1 < argc)
            pjsonPath = juce::CharPointer_UTF8(argv[++i]);
        else if (arg == "--note" && i + 1 < argc)
            note = juce::String(juce::CharPointer_UTF8(argv[++i])).getIntValue();
        else if (arg == "--blocks" && i + 1 < argc)
            renderBlocks = juce::String(juce::CharPointer_UTF8(argv[++i])).getIntValue();
        else if (arg == "--pc" && i + 1 < argc)
            programChange = juce::String(juce::CharPointer_UTF8(argv[++i])).getIntValue();
        else if (arg == "--params")
            listParams = true;
    }

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

    // ---- optional: restore a Dinamo .pjson program preset ----
    if (pjsonPath.isNotEmpty()) {
        const juce::File pjsonFile(juce::File::getCurrentWorkingDirectory()
                                       .getChildFile(pjsonPath));
        if (!pjsonFile.existsAsFile()) {
            std::printf("FAIL: pjson not found: %s\n", pjsonPath.toRawUTF8());
            return 1;
        }
        const auto parsed = juce::JSON::parse(pjsonFile.loadFileAsString());
        const auto state = parsed.getProperty("program", {})
                               .getProperty("instrument", {})
                               .getProperty("state", {})
                               .toString();
        if (state.isEmpty()) {
            std::printf("FAIL: no program.instrument.state in %s\n", pjsonPath.toRawUTF8());
            return 1;
        }
        juce::MemoryBlock blob;
        blob.loadFromHexString(state);
        std::printf("pjson state: %d hex chars -> %d bytes\n",
                    static_cast<int>(state.length()), static_cast<int>(blob.getSize()));
        instance->setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
        // Samplers load their audio on the message thread / background threads
        // after a state restore — give that real time before judging silence.
        juce::MessageManager::getInstance()->runDispatchLoopUntil(3000);
        std::printf("state restored; programs=%d current=%d ('%s')\n",
                    instance->getNumPrograms(),
                    instance->getCurrentProgram(),
                    instance->getProgramName(instance->getCurrentProgram()).toRawUTF8());
    }

    if (listParams) {
        const auto& params = instance->getParameters();
        std::printf("parameters: %d\n", params.size());
        for (int i = 0; i < juce::jmin(40, params.size()); ++i)
            std::printf("  [%d] '%s' = %.3f ('%s')\n", i,
                        params[i]->getName(64).toRawUTF8(),
                        params[i]->getValue(),
                        params[i]->getCurrentValueAsText().toRawUTF8());
    }

    juce::AudioBuffer<float> buffer(2, blockSize);

    if (programChange >= 0) {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::programChange(1, programChange), 0);
        buffer.clear();
        instance->processBlock(buffer, midi);
        // Let the sampler stream the selected program in.
        juce::MessageManager::getInstance()->runDispatchLoopUntil(3000);
        std::printf("sent MIDI program change %d\n", programChange);
    }

    if (note >= 0) {
        // ---- render a note through the instrument and report peak RMS ----
        float peakRms = 0.0f;
        int firstAudibleBlock = -1;
        for (int b = 0; b < renderBlocks; ++b) {
            buffer.clear();
            juce::MidiBuffer midi;
            // Re-strike the note every second of rendered audio in case the
            // sampler was still loading when an earlier strike arrived.
            if (b % 94 == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
            // Pump the message loop periodically: samplers often finish loading
            // their audio files on the message thread or timers.
            if (b % 10 == 0)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
            instance->processBlock(buffer, midi);
            const float rms = buffer.getRMSLevel(0, 0, blockSize)
                            + buffer.getRMSLevel(1, 0, blockSize);
            if (rms > 0.0001f && firstAudibleBlock < 0)
                firstAudibleBlock = b;
            if (rms > peakRms)
                peakRms = rms;
        }
        std::printf("rendered %d blocks (%.2fs) note=%d: peak RMS=%.6f firstAudibleBlock=%d\n",
                    renderBlocks, renderBlocks * blockSize / sampleRate, note, peakRms,
                    firstAudibleBlock);
        if (peakRms > 0.0001f) {
            std::printf("SOUND OK\n");
        } else {
            std::printf("SILENT\n");
            instance->releaseResources();
            return 3;
        }
    } else {
        // Feed a unit impulse through both channels so an effect produces output.
        buffer.clear();
        buffer.setSample(0, 0, 1.0f);
        buffer.setSample(1, 0, 1.0f);
        juce::MidiBuffer midi;
        instance->processBlock(buffer, midi);
        std::printf("processed one block: out RMS ch0=%.6f ch1=%.6f\n",
                    buffer.getRMSLevel(0, 0, blockSize),
                    buffer.getRMSLevel(1, 0, blockSize));
    }

    instance->releaseResources();
    std::printf("OK\n");
    return 0;
}
