#include "MainComponent.h"
#include "Arranger/StyleLoader.h"
#include "Arranger/SongLoader.h"
#include "Audio/MidiChannel.h"
#include "Midi/LiveMelodyVoice.h"
#include "Midi/GmInstruments.h"
#include "Arranger/OtsRecall.h"
#include "Arranger/SectionButtons.h"
#include "UI/NativePanel.h"
#include "UI/StylePartEditor.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace
{
constexpr auto compatibilityShim = R"js(
(() => {
  const install = () => {
    if (!window.__JUCE__ || !window.__JUCE__.backend) return false;
    window.__juce__ = {
      postMessage: (message) => window.__JUCE__.backend.emitEvent("cadenzaMessage", message)
    };
    return true;
  };

  if (!install()) {
    window.addEventListener("DOMContentLoaded", install, { once: true });
  }
})();
)js";

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool equalsIgnoreCase(const std::string& a, const std::string& b)
{
    return lowercase(a) == lowercase(b);
}

bool isSupportedStyleFile(const juce::File& file)
{
    // Cadenza's own JSON style, plus every Yamaha SFF container extension (all
    // share the same SMF+CASM payload): .sty user, .prs preset, .sst session,
    // .fps free play, .bcs. Genos/Genos 2 preset styles ship as .prs.
    const auto ext = lowercase(file.getFileExtension().toStdString());
    return ext == ".cstyle" || ext == ".sty" || ext == ".prs"
        || ext == ".sst" || ext == ".fps" || ext == ".bcs";
}

bool isSupportedSoundFontFile(const juce::File& file)
{
    const auto ext = lowercase(file.getFileExtension().toStdString());
    return ext == ".sf2" || ext == ".sf3";
}

bool isSupportedSongFile(const juce::File& file)
{
    return lowercase(file.getFileExtension().toStdString()) == ".csong";
}

bool isSupportedPluginFile(const juce::File& file)
{
    return lowercase(file.getFileExtension().toStdString()) == ".vst3";
}

juce::String jsString(const juce::String& value)
{
    return juce::JSON::toString(value);
}

juce::String boolLiteral(bool value)
{
    return value ? "true" : "false";
}
}

MainComponent::MainComponent()
    : m_webView(createBrowserOptions())
{
    addAndMakeVisible(m_webView);
    setSize(1280, 800);

    // Load persisted settings BEFORE starting audio so saved BPM / device take effect.
    m_settings = std::make_unique<cadenza::settings::SettingsStore>(settingsFilePath());
    loadSettings();

    // Start audio before installing hooks so noteOn arrivals have somewhere to go.
    // Restore the user's saved output-device choice (avoids a coloured virtual
    // device being silently re-selected each launch).
    std::unique_ptr<juce::XmlElement> audioState(
        juce::XmlDocument::parse(juce::File(audioStateFilePath())));
    m_audio.startAudioDevice(audioState.get());
    m_audio.deviceManager().addChangeListener(this);   // persist future changes
    m_audio.setBpm(static_cast<double>(m_state.bpm()));
    if (m_settings) {
        const auto& st = m_settings->state();
        m_audio.setEqGains(static_cast<float>(st.eqLowDb),
                           static_cast<float>(st.eqMidDb),
                           static_cast<float>(st.eqHighDb));
        m_audio.setCompAmount(st.compAmount);
        m_audio.setMasterVolume(st.masterVolume);
        m_audio.setReverbLevel(st.reverbLevel);
        m_midi.setSplitPoint(st.splitNote);
    }
    juce::Logger::writeToLog("[Cadenza] Synth engine: " + juce::String(m_audio.synthEngineName()));
    if (!m_audio.supportsSoundFonts()) {
        juce::Logger::writeToLog("[Cadenza] WARNING: NullSynthEngine is active; SoundFont loading is unavailable and playback will be silent/log-only.");
    }

    // Wire the style engine into the audio thread's tick callback.
    m_styleEngine.install();

    // Sample-tight section sequencing fires these from the audio thread; marshal
    // to the message thread for UI + mixer re-assert + stop.
    m_styleEngine.setSectionChangedCallback([this](const std::string& name) {
        juce::Component::SafePointer<MainComponent> safe(this);
        const juce::String n(name);
        juce::MessageManager::callAsync([safe, n] {
            if (auto* self = safe.getComponent()) {
                self->applyMixerState();
                if (self->m_panel) self->m_panel->setActiveSection(n);
                self->applyOtsLinkForSection(n.toStdString());
            }
        });
    });
    m_styleEngine.setStopRequestedCallback([this] {
        juce::Component::SafePointer<MainComponent> safe(this);
        juce::MessageManager::callAsync([safe] {
            if (auto* self = safe.getComponent(); self && self->m_state.playing()) {
                self->m_state.setPlaying(false);
                if (self->m_panel) self->m_panel->setPlaying(false);
                self->pushToWeb("window.JuceBridge && window.JuceBridge.onPlayStateChanged(false);");
            }
        });
    });

    m_styleEngine.setGlobalTranspose(m_state.transpose());
    m_midi.setLiveOctave(m_state.octave());   // Octave affects live right-hand only, not style parts
    applyRuntimeStateToEngines();

    // Build the native control panel before loading content so style loads can
    // populate its section buttons. This panel — not the WebView — is the source
    // of truth for the core live-arranger controls.
    buildNativePanel();

    // Try optional factory content (fail silently if absent — no SF2 yet etc.).
    tryLoadFactorySoundFont();
    if (!tryLoadLastStyle())
        tryLoadFactoryStyle();

    // Assert the live melody voice program on its dedicated channel AFTER the
    // style's channel setup, so the right-hand voice isn't left on the style's
    // channel-1 program.
    applyRightHand();

    // Wire web bridge messages -> audio / style / midi.
    installBridgeHooks();

    // Connect hardware MIDI: notes through ChordRecognizer + AudioEngine + web piano.
    m_midi.setNoteCallback([this](int channel, int note, int velocity, bool isOn) {
        if (isOn) m_audio.noteOn(channel, note, velocity);
        else      m_audio.noteOff(channel, note);
        const auto js = juce::String("window.JuceBridge && window.JuceBridge.")
                       + (isOn ? "onNoteReceived(" : "onNoteOff(")
                       + juce::String(note) + ");";
        pushToWeb(js);
    });
    m_midi.setChordCallback([this](const std::optional<cadenza::midi::Chord>& chord,
                                   const std::string& chordName) {
        // 1) Push display name to the web UI (empty string when no chord held).
        const auto js = juce::String("window.JuceBridge && window.JuceBridge.onChordChanged(\"")
                      + juce::String(chordName) + "\");";
        pushToWeb(js);

        // 2) Feed the chord into the style engine so auto-accompaniment follows.
        if (chord) m_styleEngine.setChord(*chord);

        // 3) Update the native panel's chord display (marshalled to the UI thread).
        juce::Component::SafePointer<MainComponent> safe(this);
        const juce::String name(chordName);
        juce::MessageManager::callAsync([safe, name] {
            if (auto* p = safe.getComponent(); p && p->m_panel)
                p->m_panel->setChord(name);
        });
    });

    // Style Recorder capture (MIDI thread): audition the played note on the
    // target part's channel, and write it into the take while armed.
    m_midi.setCaptureCallback([this](int note, int velocity, bool isOn) {
        const auto& info = cadenza::arranger::recorderPartInfo(m_recorder.targetPart());
        if (isOn) m_audio.noteOn(info.midiChannel, note, velocity);
        else      m_audio.noteOff(info.midiChannel, note);

        if (m_recordArmed.load() && m_audio.transport().playing()) {
            const int tick = m_audio.transport().positionTickInt();
            if (isOn) m_recorder.noteOn(note, velocity, tick);
            else      m_recorder.noteOff(note, tick);
        }
    });

    // Syncro Start / Stop: when the first chord-zone note arrives, start playback;
    // when the last one is released, stop. This is what makes the "S.Start" UI pill
    // meaningful — playing a chord left-hand triggers the backing band automatically.
    m_midi.setSyncCallback([this](bool started) {
        if (started) {
            m_audio.play();
        } else {
            m_audio.stop();
        }
        m_state.setPlaying(started);
        // Mirror to the web UI so the Play button reflects reality.
        const auto js = juce::String("window.JuceBridge && window.JuceBridge.onPlayStateChanged(")
                      + (started ? "true" : "false") + ");";
        pushToWeb(js);
        // Update the native panel's Play/Stop button (marshalled to the UI thread).
        juce::Component::SafePointer<MainComponent> safe(this);
        juce::MessageManager::callAsync([safe, started] {
            if (auto* p = safe.getComponent(); p && p->m_panel)
                p->m_panel->setPlaying(started);
        });
    });

    // MIDI input debug observability — fires on every incoming MIDI message.
    // Logs to the debug output AND pushes the same info to the web UI's debug panel.
    m_midi.setDebugCallback([this](const cadenza::midi::MidiDebugEvent& ev) {
        // 1) Plain log (visible in DbgView or the VS Output window).
        juce::String line = juce::String::formatted(
            "[MIDI] dev=\"%s\" status=0x%02X note=%d vel=%d route=%s chord=%s sync=%s",
            ev.deviceName.c_str(),
            ev.status,
            ev.note,
            ev.velocity,
            ev.route.c_str(),
            ev.chordName.empty() ? "-" : ev.chordName.c_str(),
            ev.sync.c_str());
        juce::Logger::writeToLog(line);

        // 2) Push the same info into the web UI as a JS-safe object literal.
        auto esc = [](const std::string& s) {
            std::string out;
            out.reserve(s.size() + 2);
            for (char c : s) {
                if (c == '\\' || c == '"') out += '\\';
                if (c >= ' ' && static_cast<unsigned char>(c) < 127) out += c;
            }
            return out;
        };
        juce::String js = juce::String::formatted(
            "window.JuceBridge && window.JuceBridge.onMidiDebug && "
            "window.JuceBridge.onMidiDebug({"
            "device:\"%s\",status:%d,note:%d,velocity:%d,"
            "route:\"%s\",chord:\"%s\",sync:\"%s\"});",
            esc(ev.deviceName).c_str(),
            ev.status,
            ev.note,
            ev.velocity,
            esc(ev.route).c_str(),
            esc(ev.chordName).c_str(),
            esc(ev.sync).c_str());
        pushToWeb(js);
    });

    // MIDI control mapping: hardware buttons -> arranger commands (sections / play).
    if (m_settings)
        m_midi.setControlMap(m_settings->state().midiControlMap);
    m_midi.setControlCallback([this](const std::string& command) {
        juce::Component::SafePointer<MainComponent> safe(this);
        juce::MessageManager::callAsync([safe, command] {
            if (auto* self = safe.getComponent()) self->executeControlCommand(command);
        });
    });
    m_midi.setControlLearnCallback([this](int trigger) {
        juce::Component::SafePointer<MainComponent> safe(this);
        juce::MessageManager::callAsync([safe, trigger] {
            auto* self = safe.getComponent();
            if (!self || !self->m_settings || self->m_learnCommand.empty()) return;
            cadenza::midi::MidiControlMap map;
            map.setEntries(self->m_settings->state().midiControlMap);
            map.assign(trigger, self->m_learnCommand);          // one button per action
            self->m_settings->state().midiControlMap = map.entries();
            self->m_midi.setControlMap(map.entries());
            self->m_learnCommand.clear();
            self->saveSettings();   // the MIDI Settings window re-reads the map on its own timer
        });
    });

    m_midi.refreshInputs();   // open ALL available MIDI inputs (logs the device list)

    // Always-on timer: hot-plug MIDI rescan (~every 2s) + song-mode auto-stepping.
    startTimerHz(20);

    m_webView.goToURL(startupUrl());

    // The native panel is the source of truth; the WebView starts hidden and can
    // be shown via the panel's "Web UI" toggle. (It still loads/runs in the
    // background so the toggle is instant and bridge mirroring keeps working.)
    m_webView.setVisible(false);
    resized();
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveSettings();
    m_styleEngine.allNotesOff();
    m_audio.deviceManager().removeChangeListener(this);
    m_audio.stopAudioDevice();
    m_midi.closeInputs();
}

std::string MainComponent::settingsFilePath() const
{
    const auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Cadenza");
    if (!dir.isDirectory()) dir.createDirectory();
    return dir.getChildFile("settings.json").getFullPathName().toStdString();
}

std::string MainComponent::audioStateFilePath() const
{
    return juce::File(settingsFilePath()).getParentDirectory()
        .getChildFile("audio-device.xml").getFullPathName().toStdString();
}

void MainComponent::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // The audio device changed (user picked a different output) — persist it.
    if (auto xml = m_audio.deviceManager().createStateXml())
        xml->writeTo(juce::File(audioStateFilePath()));
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent(
        m_audio.deviceManager(),
        /*minInputCh*/  0, /*maxInputCh*/  0,
        /*minOutputCh*/ 1, /*maxOutputCh*/ 2,
        /*showMidiIn*/  false, /*showMidiOut*/ false,
        /*stereoPairs*/ true,  /*hideAdvanced*/ false);
    selector->setSize(520, 360);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(selector);
    opts.dialogTitle = "Audio Output Settings";
    opts.dialogBackgroundColour = juce::Colour(0xff2a2f3a);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

namespace {
// MIDI Settings / button-mapping window: lists connected MIDI inputs and one row
// per arranger action (current mapping + Learn + Clear). Refreshes the displayed
// mappings on a timer so a MIDI-learn capture shows up live.
class MidiSettingsComponent : public juce::Component, private juce::Timer
{
public:
    using MappingTextFn = std::function<juce::String(const std::string&)>;
    using CommandFn     = std::function<void(const std::string&)>;

    MidiSettingsComponent(const juce::StringArray& devices,
                          const std::vector<std::pair<juce::String, std::string>>& actions,
                          MappingTextFn mappingText, CommandFn onLearn, CommandFn onClear)
        : m_mappingText(std::move(mappingText)), m_onLearn(std::move(onLearn)), m_onClear(std::move(onClear))
    {
        m_devices.setText(devices.isEmpty() ? "No MIDI inputs detected"
                                            : "MIDI inputs: " + devices.joinIntoString(", "),
                          juce::dontSendNotification);
        m_devices.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(m_devices);

        for (const auto& a : actions) {
            auto row = std::make_unique<Row>();
            row->command = a.second;
            row->name.setText(a.first, juce::dontSendNotification);
            row->name.setColour(juce::Label::textColourId, juce::Colours::white);
            row->mapping.setColour(juce::Label::textColourId, juce::Colours::skyblue);
            row->mapping.setJustificationType(juce::Justification::centred);
            row->learn.setButtonText("Learn");
            row->clear.setButtonText("X");
            const std::string cmd = a.second;
            auto* mapLabel = &row->mapping;
            row->learn.onClick = [this, cmd, mapLabel] {
                m_pending = cmd;
                m_pendingPrev = m_mappingText ? m_mappingText(cmd) : juce::String();
                mapLabel->setText("press a button...", juce::dontSendNotification);
                if (m_onLearn) m_onLearn(cmd);
            };
            row->clear.onClick = [this, cmd] { if (m_onClear) m_onClear(cmd); };
            addAndMakeVisible(row->name);
            addAndMakeVisible(row->mapping);
            addAndMakeVisible(row->learn);
            addAndMakeVisible(row->clear);
            m_rows.push_back(std::move(row));
        }
        refresh();
        startTimerHz(5);
        setSize(440, 70 + static_cast<int>(m_rows.size()) * 30);
    }

    void resized() override
    {
        auto a = getLocalBounds().reduced(10);
        m_devices.setBounds(a.removeFromTop(24));
        a.removeFromTop(8);
        for (auto& r : m_rows) {
            auto row = a.removeFromTop(26);
            a.removeFromTop(4);
            r->name.setBounds(row.removeFromLeft(120));
            r->clear.setBounds(row.removeFromRight(30));
            row.removeFromRight(6);
            r->learn.setBounds(row.removeFromRight(64));
            row.removeFromRight(8);
            r->mapping.setBounds(row);
        }
    }

private:
    struct Row {
        std::string command;
        juce::Label name, mapping;
        juce::TextButton learn, clear;
    };

    void refresh()
    {
        for (auto& r : m_rows) {
            const juce::String t = m_mappingText ? m_mappingText(r->command) : juce::String();
            if (m_pending == r->command) {
                if (t != m_pendingPrev) m_pending.clear();   // a new mapping was captured
                else { r->mapping.setText("press a button...", juce::dontSendNotification); continue; }
            }
            r->mapping.setText(t.isEmpty() ? juce::String("--") : t, juce::dontSendNotification);
        }
    }

    void timerCallback() override { refresh(); }

    MappingTextFn m_mappingText;
    CommandFn m_onLearn, m_onClear;
    juce::Label m_devices;
    std::vector<std::unique_ptr<Row>> m_rows;
    std::string m_pending;
    juce::String m_pendingPrev;
};
}

void MainComponent::showMidiSettings()
{
    if (!m_settings) return;

    const std::vector<std::pair<juce::String, std::string>> actions = {
        { "Start / Stop", "play" },
        { "Intro",   "intro"  }, { "Intro B", "introB" }, { "Intro C", "introC" },
        { "Main A",  "mainA"  }, { "Main B",  "mainB"  }, { "Main C", "mainC" }, { "Main D", "mainD" },
        { "Fill A",  "fillAA" }, { "Fill B",  "fillBB" }, { "Fill C", "fillCC" }, { "Fill D", "fillDD" },
        { "Ending",  "ending" }, { "Ending B","endingB" }, { "Ending C","endingC" },
    };

    juce::Component::SafePointer<MainComponent> safe(this);
    auto mappingText = [safe](const std::string& cmd) -> juce::String {
        auto* self = safe.getComponent();
        if (!self || !self->m_settings) return {};
        cadenza::midi::MidiControlMap m;
        m.setEntries(self->m_settings->state().midiControlMap);
        if (auto t = m.triggerFor(cmd)) return juce::String(cadenza::midi::describeTrigger(*t));
        return {};
    };
    auto onLearn = [safe](const std::string& cmd) {
        if (auto* self = safe.getComponent()) { self->m_learnCommand = cmd; self->m_midi.armControlLearn(true); }
    };
    auto onClear = [safe](const std::string& cmd) {
        auto* self = safe.getComponent();
        if (!self || !self->m_settings) return;
        cadenza::midi::MidiControlMap m;
        m.setEntries(self->m_settings->state().midiControlMap);
        m.clearCommand(cmd);
        self->m_settings->state().midiControlMap = m.entries();
        self->m_midi.setControlMap(m.entries());
        self->saveSettings();
    };

    auto* comp = new MidiSettingsComponent(m_midi.availableInputs(), actions,
                                           std::move(mappingText), std::move(onLearn), std::move(onClear));
    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(comp);
    opts.dialogTitle = "MIDI Settings & Button Mapping";
    opts.dialogBackgroundColour = juce::Colour(0xff2a2f3a);
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = false;
    opts.launchAsync();
}

void MainComponent::loadSettings()
{
    if (!m_settings) return;
    if (!m_settings->load()) return;   // file missing or corrupt → keep defaults

    const auto& s = m_settings->state();
    m_state.setBpm(s.bpm);
    m_state.setTranspose(s.transpose);
    m_state.setOctave(s.octave);
    m_state.setKey(s.key);
    m_state.setBankMemory(s.bankMemory);
    m_state.setStyleMemory(s.styleMemory);
    m_state.setCrossfade(s.crossfade);
    m_state.setSyncroStopOnRelease(s.syncroStopOnRelease);
    m_state.setAutoFillEnabled(s.autoFillEnabled);
    m_state.setChordSourceEnabled("bass", s.chordBassEnabled);
    m_state.setChordSourceEnabled("arranger", s.chordArrangerEnabled);
    m_state.setChordSourceEnabled("memory", s.chordMemoryEnabled);

    juce::Logger::writeToLog("[Cadenza] Loaded settings from " + juce::String(m_settings->path()));
}

void MainComponent::saveSettings()
{
    if (!m_settings) return;

    auto& s = m_settings->state();
    s.bpm         = m_state.bpm();
    s.transpose   = m_state.transpose();
    s.octave      = m_state.octave();
    s.key         = m_state.key();
    s.bankMemory  = m_state.bankMemory();
    s.styleMemory = m_state.styleMemory();
    s.crossfade   = m_state.crossfade();
    s.syncroStopOnRelease = m_state.syncroStopOnRelease();
    s.autoFillEnabled = m_state.autoFillEnabled();
    s.chordBassEnabled = m_state.chordSourceEnabled("bass");
    s.chordArrangerEnabled = m_state.chordSourceEnabled("arranger");
    s.chordMemoryEnabled = m_state.chordSourceEnabled("memory");
    // lastStyleId / lastStylePath / lastSoundFontPath / midiInputDevice are updated as those change.

    m_settings->save();
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

    if (m_webView.isVisible()) {
        // Secondary web UI shown: native panel on the left, WebView fills the rest.
        if (m_panel)
            m_panel->setBounds(bounds.removeFromLeft(440));
        m_webView.setBounds(bounds);
    } else {
        // Native panel is the whole window (source of truth).
        if (m_panel)
            m_panel->setBounds(bounds);
    }
}

juce::WebBrowserComponent::Options MainComponent::createBrowserOptions()
{
    juce::WebBrowserComponent::Options options;

#if JUCE_WINDOWS
    options = options.withBackend(juce::WebBrowserComponent::Options::Backend::webview2);
#endif

    return options
        .withNativeIntegrationEnabled()
        .withUserScript(compatibilityShim)
        .withEventListener("cadenzaMessage", [this](juce::var payload) {
            handleBridgePayload(payload);
        });
}

juce::File MainComponent::findWebRoot()
{
    const auto executableResources = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getChildFile("resources")
        .getChildFile("web");

    if (executableResources.getChildFile("Cadenza Workstation.html").existsAsFile())
        return executableResources;

    const auto workingResources = juce::File::getCurrentWorkingDirectory()
        .getChildFile("resources")
        .getChildFile("web");

    if (workingResources.getChildFile("Cadenza Workstation.html").existsAsFile())
        return workingResources;

    return juce::File::getCurrentWorkingDirectory();
}

juce::String MainComponent::startupUrl()
{
    const auto html = findWebRoot().getChildFile("Cadenza Workstation.html");
    return juce::URL(html).toString(true);
}

void MainComponent::pushToWeb(const juce::String& js)
{
    if (js.isEmpty()) return;
    // evaluateJavascript must be called on the message thread. All callers
    // (MIDI thread, audio thread) marshal here so WebView2 is never called
    // from a background thread.
    juce::Component::SafePointer<MainComponent> safe(this);
    juce::MessageManager::callAsync([safe, js] {
        if (auto* p = safe.getComponent())
            p->m_webView.evaluateJavascript(js);
    });
}

void MainComponent::applyRuntimeStateToEngines()
{
    m_styleEngine.setGlobalTranspose(m_state.transpose());
    m_midi.setLiveTranspose(m_state.transpose());   // transpose shifts the right-hand melody too
    m_midi.setLiveOctave(m_state.octave());   // Octave affects live right-hand only, not style parts
    m_styleEngine.setEnabled(m_state.chordSourceEnabled("arranger"));
    m_midi.setChordDetectionMode(m_state.chordSourceEnabled("bass")
        ? arranger::ChordDetectionMode::FingeredOnBass
        : arranger::ChordDetectionMode::Fingered);
    m_midi.setChordMemory(m_state.chordSourceEnabled("memory"));
    m_midi.setSyncroStopOnRelease(m_state.syncroStopOnRelease());
}

juce::Array<juce::File> MainComponent::factoryStyleFiles() const
{
    const auto stylesDir = findWebRoot()
        .getParentDirectory()
        .getChildFile("factory")
        .getChildFile("styles");

    juce::Array<juce::File> files;
    if (!stylesDir.isDirectory())
        return files;

    files.addArray(stylesDir.findChildFiles(juce::File::findFiles, false, "*.cstyle"));
    files.addArray(stylesDir.findChildFiles(juce::File::findFiles, false, "*.sty"));
    return files;
}

void MainComponent::pushFactoryStylesToWeb()
{
    auto files = factoryStyleFiles();

    juce::String js = "window.JuceBridge && window.JuceBridge.onFactoryStyles([";
    for (int i = 0; i < files.size(); ++i) {
        const auto& file = files.getReference(i);
        auto loaded = cadenza::arranger::loadStyleFromFile(file.getFullPathName().toStdString());
        const auto name = loaded.ok && !loaded.style.name.empty()
            ? juce::String(loaded.style.name)
            : file.getFileNameWithoutExtension();
        const auto id = loaded.ok && !loaded.style.id.empty()
            ? juce::String(loaded.style.id)
            : file.getFileNameWithoutExtension();

        if (i > 0) js += ",";
        js += "{";
        js += "id:" + jsString(id);
        js += ",name:" + jsString(name);
        js += ",path:" + jsString(file.getFullPathName());
        js += ",kind:" + jsString(file.getFileExtension().trimCharactersAtStart(".").toLowerCase());
        js += "}";
    }
    js += "]);";
    pushToWeb(js);
}

void MainComponent::pushRuntimeStateToWeb()
{
    const auto current = m_styleEngine.currentStyle();
    const auto styleName = current ? juce::String(current->name) : juce::String();
    const auto styleId = current ? juce::String(current->id) : juce::String();

    juce::String js = "window.JuceBridge && window.JuceBridge.onRuntimeState({";
    js += "bpm:" + juce::String(m_state.bpm());
    js += ",transpose:" + juce::String(m_state.transpose());
    js += ",octave:" + juce::String(m_state.octave());
    js += ",syncroStop:" + boolLiteral(m_state.syncroStopOnRelease());
    js += ",activeBass:" + boolLiteral(m_state.chordSourceEnabled("bass"));
    js += ",activeArranger:" + boolLiteral(m_state.chordSourceEnabled("arranger"));
    js += ",activeMemory:" + boolLiteral(m_state.chordSourceEnabled("memory"));
    js += ",styleName:" + jsString(styleName);
    js += ",styleId:" + jsString(styleId);
    const auto soundFontPath = m_settings ? juce::String(m_settings->state().lastSoundFontPath) : juce::String();
    const auto soundFontName = soundFontPath.isNotEmpty()
        ? juce::File(soundFontPath).getFileName()
        : juce::String("None");
    js += ",synthEngine:" + jsString(m_audio.synthEngineName());
    js += ",soundFontName:" + jsString(soundFontName);
    js += ",soundFontPath:" + jsString(soundFontPath);
    js += "});";
    pushToWeb(js);
}

void MainComponent::installBridgeHooks()
{
    cadenza::BridgeHooks hooks;
    hooks.onNoteOn = [this](int channel, int note, int velocity) {
        // On-screen piano notes go through the SAME live melody voice as hardware
        // keys, so the Octave control and the melody instrument apply to right-hand
        // (above-split) notes. Below-split notes sound directly (unshifted).
        const auto events = m_midi.handleVirtualMelodyNote(note, velocity, true);
        if (!events.empty()) {
            for (const auto& ev : events)            // one per enabled Right layer
                m_audio.noteOn(ev.channel, ev.note, ev.velocity);
        } else {
            m_audio.noteOn(channel == 0 ? 1 : channel, note, velocity);
        }
    };
    hooks.onNoteOff = [this](int channel, int note) {
        const auto events = m_midi.handleVirtualMelodyNote(note, 0, false);
        if (!events.empty()) {
            for (const auto& ev : events)
                m_audio.noteOff(ev.channel, ev.note);
        } else {
            m_audio.noteOff(channel == 0 ? 1 : channel, note);
        }
    };
    hooks.onPlayStateChanged = [this](bool playing) {
        if (playing) {
            // Restart the chord chart from the current bar when song mode is on.
            if (m_songModeActive) {
                m_songPlayer.reset();
                m_lastSongBar = -1;
                applySongStepForBar(1);
                queueSongSectionForBar(2);
            }
            m_audio.play();
        } else {
            m_audio.stop();
        }
    };
    hooks.onBpmChanged = [this](int bpm) {
        m_audio.setBpm(static_cast<double>(bpm));
        saveSettings();
    };
    hooks.onTransposeChanged = [this](int semitones) {
        m_styleEngine.setGlobalTranspose(semitones);
        m_midi.setLiveTranspose(semitones);   // shift the right-hand melody too
        saveSettings();
    };
    hooks.onOctaveChanged = [this](int octaves) {
        // Octave is a live right-hand feature only — it must not move style parts.
        m_midi.setLiveOctave(octaves);
        juce::Logger::writeToLog("[Cadenza] octave changed -> setLiveOctave(" + juce::String(octaves)
                                 + "); MidiRouter now reports " + juce::String(m_midi.liveOctave()));
        saveSettings();
    };
    hooks.onKeyChanged = [this](const std::string& key) {
        // map "C" .. "B" to pitch class. Accept "#" but not "b".
        static const std::string names[12] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        for (int i = 0; i < 12; ++i)
            if (names[i] == key) { m_styleEngine.setKeyTonic(i); break; }
        saveSettings();
    };
    hooks.onBankMemoryChanged = [this](const std::string& bankName) {
        // The "Bank Memory" voice picker selects Right 1's instrument: map the
        // voice name to a GM program for the primary right-hand layer.
        if (m_settings) {
            const int program = cadenza::midi::gmProgramForBankName(bankName);
            m_settings->state().melodyProgram = program;
            m_settings->state().rightLayers[0].program = program;
        }
        applyRightHand();
        saveSettings();
    };
    hooks.onStyleMemoryChanged = [this](int slot) {
        // Map the 4 Style Memory pads to the 4 most useful sections.
        static const char* slotSection[4] = { "mainA", "mainB", "intro", "ending" };
        if (slot >= 1 && slot <= 4) {
            // Manual section control overrides the song chart.
            if (m_songModeActive)
                setSongMode(false);
            m_styleEngine.allNotesOff();
            m_styleEngine.setSection(slotSection[slot - 1]);
            saveSettings();
        }
    };
    hooks.onPadChanged = [this](int /*padIndex*/, bool /*on*/) {
        // Future: pad sample triggers (one-shot SF2 hits / audio loops).
    };
    hooks.onSelectStyle = [this](const std::string& name) {
        const juce::File directFile { juce::String(name) };
        if (directFile.existsAsFile() && isSupportedStyleFile(directFile)) {
            loadAndApplyStyleFile(directFile);
            return;
        }

        for (const auto& f : factoryStyleFiles()) {
            auto loaded = cadenza::arranger::loadStyleFromFile(f.getFullPathName().toStdString());
            if (loaded.ok &&
                (equalsIgnoreCase(loaded.style.id, name) ||
                 equalsIgnoreCase(loaded.style.name, name) ||
                 equalsIgnoreCase(f.getFileNameWithoutExtension().toStdString(), name))) {
                loadAndApplyStyleFile(f);
                return;
            }
        }
        juce::Logger::writeToLog("[Cadenza] No style file matches: " + juce::String(name));
    };
    hooks.onOpenStyleFile = [this] {
        openStyleFileChooser();
    };
    hooks.onOpenSoundFontFile = [this] {
        openSoundFontFileChooser();
    };
    hooks.onOpenSongFile = [this] {
        openSongFileChooser();
    };
    hooks.onSongModeChanged = [this](bool enabled) {
        setSongMode(enabled);
    };
    hooks.onOpenPluginFile = [this] {
        openPluginFileChooser();
    };
    hooks.onClearPlugin = [this] {
        clearMasterEffect();
    };
    hooks.onExportPlaybackDiagnostics = [this] {
        exportPlaybackDiagnostics();
    };

    hooks.onChordSourceChanged = [this](const std::string& source, bool enabled) {
        if (source == "bass") {
            m_midi.setChordDetectionMode(enabled
                ? arranger::ChordDetectionMode::FingeredOnBass
                : arranger::ChordDetectionMode::Fingered);
        } else if (source == "arranger") {
            if (!enabled)
                m_styleEngine.allNotesOff();
            m_styleEngine.setEnabled(enabled);
        } else if (source == "memory") {
            m_midi.setChordMemory(enabled);
        }
        saveSettings();
    };

    hooks.onSyncroStopChanged = [this](bool enabled) {
        m_midi.setSyncroStopOnRelease(enabled);
        saveSettings();
        juce::Logger::writeToLog(juce::String("[Cadenza] Syncro Stop on release: ")
                                 + (enabled ? "ON" : "OFF"));
    };

    hooks.onExitApp = [this] {
        juce::JUCEApplicationBase::quit();
    };

    hooks.onPageReady = [this] {
        // The web page just (re)loaded its JS. Push current state so the UI
        // reflects whatever the C++ engine already knows.
        const auto chordName = m_midi.currentChordDisplayName();
        const auto chordJs = juce::String("window.JuceBridge && window.JuceBridge.onChordChanged(\"")
                           + juce::String(chordName) + "\");";
        pushToWeb(chordJs);
        pushFactoryStylesToWeb();
        pushRuntimeStateToWeb();
        pushPluginStateToWeb();
        juce::Logger::writeToLog("[Cadenza] pageReady: pushed chord = "
                                 + (chordName.empty() ? juce::String("(none)") : juce::String(chordName)));
    };

    m_router.setHooks(std::move(hooks));
}

void MainComponent::tryLoadFactoryStyle()
{
    for (const auto& file : factoryStyleFiles()) {
        if (file.getFileNameWithoutExtension().equalsIgnoreCase("8-beat-pop")) {
            loadAndApplyStyleFile(file);
            return;
        }
    }

    const auto files = factoryStyleFiles();
    if (!files.isEmpty()) {
        loadAndApplyStyleFile(files.getFirst());
        return;
    }
}

bool MainComponent::tryLoadLastStyle()
{
    if (!m_settings)
        return false;

    const auto path = m_settings->state().lastStylePath;
    if (path.empty())
        return false;

    const juce::File file { juce::String(path) };
    return loadAndApplyStyleFile(file);
}

void MainComponent::tryLoadFactorySoundFont()
{
    if (!m_audio.supportsSoundFonts())
        return;

    if (m_settings && !m_settings->state().lastSoundFontPath.empty()) {
        const juce::File saved { juce::String(m_settings->state().lastSoundFontPath) };
        if (saved.existsAsFile() && isSupportedSoundFontFile(saved)) {
            if (loadAndApplySoundFontFile(saved, false))
                return;
        } else {
            juce::Logger::writeToLog("[Cadenza] Saved SoundFont is missing or unsupported: " + saved.getFullPathName());
        }
    }

    // Look for any .sf2/.sf3 in resources/sf2 (factory or user-dropped).
    const auto sf2Dir = findWebRoot()
        .getParentDirectory()
        .getChildFile("sf2");
    if (!sf2Dir.isDirectory()) return;

    auto results = sf2Dir.findChildFiles(juce::File::findFiles, false, "*.sf2");
    results.addArray(sf2Dir.findChildFiles(juce::File::findFiles, false, "*.sf3"));
    if (results.isEmpty()) return;

    // Prefer the LARGEST SoundFont in the folder — bigger almost always means a
    // higher-quality sample set, so simply dropping a better .sf2 here upgrades
    // the sound with no further steps.
    juce::File best = results.getReference(0);
    for (const auto& f : results)
        if (f.getSize() > best.getSize())
            best = f;

    juce::Logger::writeToLog("[Cadenza] Auto-loading largest SoundFont in resources/sf2: "
                             + best.getFileName() + " (" + juce::String(best.getSize() / (1024 * 1024)) + " MB)");
    loadAndApplySoundFontFile(best, true);
}

void MainComponent::openStyleFileChooser()
{
    const auto stylesDir = findWebRoot()
        .getParentDirectory()
        .getChildFile("factory")
        .getChildFile("styles");

    m_styleChooser = std::make_unique<juce::FileChooser>(
        "Choose a Cadenza or Yamaha style",
        stylesDir.isDirectory() ? stylesDir : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.cstyle;*.sty;*.prs;*.sst;*.fps;*.bcs");

    juce::Component::SafePointer<MainComponent> safe(this);
    m_styleChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser) {
            if (auto* self = safe.getComponent()) {
                const auto file = chooser.getResult();
                if (file.existsAsFile())
                    self->loadAndApplyStyleFile(file);
                self->m_styleChooser.reset();
            }
        });
}

void MainComponent::openSoundFontFileChooser()
{
    const auto sf2Dir = findWebRoot()
        .getParentDirectory()
        .getChildFile("sf2");

    m_soundFontChooser = std::make_unique<juce::FileChooser>(
        "Choose a SoundFont",
        sf2Dir.isDirectory() ? sf2Dir : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.sf2;*.sf3");

    juce::Component::SafePointer<MainComponent> safe(this);
    m_soundFontChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser) {
            if (auto* self = safe.getComponent()) {
                const auto file = chooser.getResult();
                if (file.existsAsFile())
                    self->loadAndApplySoundFontFile(file, true);
                self->m_soundFontChooser.reset();
            }
        });
}

void MainComponent::choosePartInstrument(int channel)
{
    juce::File vst3Dir("C:\\Program Files\\Common Files\\VST3");
    m_partPluginChooser = std::make_unique<juce::FileChooser>(
        "Choose a VST3 instrument for this part",
        vst3Dir.isDirectory() ? vst3Dir
                              : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.vst3");

    juce::Component::SafePointer<MainComponent> safe(this);
    m_partPluginChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::canSelectDirectories,   // .vst3 is a bundle dir on Windows
        [safe, channel](const juce::FileChooser& chooser) {
            if (auto* self = safe.getComponent()) {
                const auto file = chooser.getResult();
                if (file.exists()) {
                    std::string err;
                    if (self->m_audio.loadPartInstrument(channel, file.getFullPathName().toStdString(), err)) {
                        juce::Logger::writeToLog("[Cadenza] Loaded part instrument ch=" + juce::String(channel)
                                                 + ": " + juce::String(self->m_audio.partInstrumentName(channel)));
                        if (self->m_panel)
                            self->m_panel->setMixerInstrumentName(
                                channel, juce::String(self->m_audio.partInstrumentName(channel)));
                        self->m_audio.showPartInstrumentEditor(channel);   // pop the plugin's GUI
                        self->persistStyleMix();   // remember this plugin for the style
                    } else {
                        juce::Logger::writeToLog("[Cadenza] Part instrument load failed ch="
                                                 + juce::String(channel) + ": " + juce::String(err));
                    }
                }
                self->m_partPluginChooser.reset();
            }
        });
}

void MainComponent::chooseRightLayerInstrument(int layer)
{
    if (!m_settings || layer < 0 || layer >= 3)
        return;
    juce::File vst3Dir("C:\\Program Files\\Common Files\\VST3");
    m_partPluginChooser = std::make_unique<juce::FileChooser>(
        "Choose a VST3 instrument for Right " + juce::String(layer + 1),
        vst3Dir.isDirectory() ? vst3Dir
                              : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.vst3");

    juce::Component::SafePointer<MainComponent> safe(this);
    m_partPluginChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::canSelectDirectories,
        [safe, layer](const juce::FileChooser& chooser) {
            if (auto* self = safe.getComponent()) {
                const auto file = chooser.getResult();
                if (file.exists()) {
                    const int channel = self->m_midi.rightLayerChannel(layer);
                    std::string err;
                    if (self->m_audio.loadPartInstrument(channel, file.getFullPathName().toStdString(), err)) {
                        auto& L = self->m_settings->state().rightLayers[layer];
                        L.pluginPath = file.getFullPathName().toStdString();
                        self->m_audio.controlChange(channel, 7, L.volume);   // apply the layer volume
                        if (self->m_panel)
                            self->m_panel->setRightVoiceName(
                                layer, juce::String(self->m_audio.partInstrumentName(channel)), true);
                        self->m_audio.showPartInstrumentEditor(channel);     // pop the plugin GUI
                        self->saveSettings();
                    } else {
                        juce::Logger::writeToLog("[Cadenza] Right " + juce::String(layer + 1)
                                                 + " VST load failed: " + juce::String(err));
                    }
                }
                self->m_partPluginChooser.reset();
            }
        });
}

bool MainComponent::loadAndApplySoundFontFile(const juce::File& file, bool persist)
{
    if (!file.existsAsFile() || !isSupportedSoundFontFile(file)) {
        juce::Logger::writeToLog("[Cadenza] Unsupported SoundFont file: " + file.getFullPathName());
        return false;
    }

    if (!m_audio.supportsSoundFonts()) {
        juce::Logger::writeToLog("[Cadenza] WARNING: " + juce::String(m_audio.synthEngineName())
                                 + " is active; cannot load SoundFont: " + file.getFullPathName());
        return false;
    }

    const bool ok = m_audio.loadSoundFont(file.getFullPathName().toStdString());
    if (!ok) {
        juce::Logger::writeToLog("[Cadenza] SoundFont failed or was rejected by FluidSynth: "
                                 + file.getFullPathName());
        return false;
    }

    if (m_settings) {
        m_settings->state().lastSoundFontPath = file.getFullPathName().toStdString();
        if (persist)
            saveSettings();
    }

    juce::Logger::writeToLog("[Cadenza] SoundFont loaded via " + juce::String(m_audio.synthEngineName())
                             + ": " + file.getFileName() + " (" + file.getFullPathName() + ")");
    m_styleEngine.reapplyCurrentSectionChannelSetup();
    pushRuntimeStateToWeb();
    return true;
}

void MainComponent::exportPlaybackDiagnostics()
{
    const auto diagnosticsDir = juce::File::getCurrentWorkingDirectory()
        .getChildFile("diagnostics");
    const auto result = m_styleEngine.exportCurrentSectionDiagnostics(
        diagnosticsDir.getFullPathName().toStdString());

    if (!result.ok) {
        juce::Logger::writeToLog("[Cadenza] Playback diagnostic export failed: "
                                 + juce::String(result.error));
        return;
    }

    juce::Logger::writeToLog("[Cadenza] Playback diagnostics exported: "
                             + juce::String(result.csvPath)
                             + ", " + juce::String(result.midiPath)
                             + ", " + juce::String(result.summaryPath)
                             + " events=" + juce::String(result.eventCount));
}

bool MainComponent::loadAndApplyStyleFile(const juce::File& file)
{
    if (!file.existsAsFile() || !isSupportedStyleFile(file)) {
        juce::Logger::writeToLog("[Cadenza] Unsupported style file: " + file.getFullPathName());
        return false;
    }

    auto loaded = cadenza::arranger::loadStyleFromFile(file.getFullPathName().toStdString());
    if (!loaded.ok) {
        juce::Logger::writeToLog("[Cadenza] Failed to load style: " + juce::String(loaded.error));
        return false;
    }

    auto sharedStyle = std::make_shared<const cadenza::arranger::Style>(std::move(loaded.style));
    const auto initialSection = sharedStyle->findSection("mainA") != nullptr
        ? std::string("mainA")
        : (sharedStyle->sections.empty() ? std::string() : sharedStyle->sections.front().name);

    auto appliedBpm = m_state.bpm();
    if (sharedStyle->defaultTempo > 0) {
        appliedBpm = m_state.setBpm(sharedStyle->defaultTempo);
        m_audio.setBpm(static_cast<double>(appliedBpm));
    }
    m_styleEngine.allNotesOff();
    m_styleEngine.setStyle(sharedStyle);
    if (!initialSection.empty())
        m_styleEngine.setSection(initialSection);

    // Refresh the native panel's style name + sections + mixer, and (re)assign the
    // live-melody channel/program so it never collides with this style's channels.
    updateNativePanelStyle();

    if (m_settings) {
        m_settings->state().lastStyleId = sharedStyle->id;
        m_settings->state().lastStylePath = file.getFullPathName().toStdString();
    }

    pushRuntimeStateToWeb();

    saveSettings();

    juce::Logger::writeToLog("[Cadenza] Loaded style: " + juce::String(sharedStyle->name)
                             + " bpm=" + juce::String(appliedBpm)
                             + " ppq=" + juce::String(sharedStyle->ticksPerBeat)
                             + " timeSig=" + juce::String(sharedStyle->beatsPerBar)
                             + "/" + juce::String(sharedStyle->beatUnit)
                             + " from " + file.getFullPathName());
    return true;
}

void MainComponent::openSongFileChooser()
{
    const auto songsDir = findWebRoot()
        .getParentDirectory()
        .getChildFile("factory")
        .getChildFile("songs");

    m_songChooser = std::make_unique<juce::FileChooser>(
        "Choose a Cadenza song (chord chart)",
        songsDir.isDirectory() ? songsDir : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.csong");

    juce::Component::SafePointer<MainComponent> safe(this);
    m_songChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe](const juce::FileChooser& chooser) {
            if (auto* self = safe.getComponent()) {
                const auto file = chooser.getResult();
                if (file.existsAsFile())
                    self->loadAndApplySongFile(file);
                self->m_songChooser.reset();
            }
        });
}

bool MainComponent::selectStyleById(const std::string& styleId)
{
    if (styleId.empty())
        return false;

    for (const auto& f : factoryStyleFiles()) {
        auto loaded = cadenza::arranger::loadStyleFromFile(f.getFullPathName().toStdString());
        if (loaded.ok &&
            (equalsIgnoreCase(loaded.style.id, styleId) ||
             equalsIgnoreCase(loaded.style.name, styleId) ||
             equalsIgnoreCase(f.getFileNameWithoutExtension().toStdString(), styleId))) {
            return loadAndApplyStyleFile(f);
        }
    }
    return false;
}

bool MainComponent::loadAndApplySongFile(const juce::File& file)
{
    if (!file.existsAsFile() || !isSupportedSongFile(file)) {
        juce::Logger::writeToLog("[Cadenza] Unsupported song file: " + file.getFullPathName());
        return false;
    }

    auto loaded = cadenza::arranger::loadSongFromFile(file.getFullPathName().toStdString());
    if (!loaded.ok) {
        juce::Logger::writeToLog("[Cadenza] Failed to load song: " + juce::String(loaded.error));
        return false;
    }

    // Load the style the song references (best-effort; keep current style if missing).
    if (!loaded.song.styleId.empty() && !selectStyleById(loaded.song.styleId)) {
        juce::Logger::writeToLog("[Cadenza] Song references style '" + juce::String(loaded.song.styleId)
                                 + "' which was not found; keeping current style.");
    }

    if (loaded.song.defaultTempo > 0) {
        const auto bpm = m_state.setBpm(loaded.song.defaultTempo);
        m_audio.setBpm(static_cast<double>(bpm));
    }

    // Apply the song's key so scale-tone parts reference the right tonic.
    if (!loaded.song.key.empty()) {
        static const std::string keyNames[12] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        for (int i = 0; i < 12; ++i) {
            if (keyNames[i] == loaded.song.key) {
                m_styleEngine.setKeyTonic(i);
                m_state.setKey(loaded.song.key);
                break;
            }
        }
    }

    const auto songName = loaded.song.name.empty() ? file.getFileNameWithoutExtension().toStdString()
                                                    : loaded.song.name;
    m_songPlayer.setSong(std::make_shared<const cadenza::arranger::Song>(std::move(loaded.song)));

    juce::Logger::writeToLog("[Cadenza] Loaded song: " + juce::String(songName)
                             + " (" + juce::String(m_songPlayer.lastEventBar()) + " bars) from "
                             + file.getFullPathName());

    setSongMode(true);
    pushRuntimeStateToWeb();
    return true;
}

void MainComponent::setSongMode(bool enabled)
{
    m_songModeActive = enabled && m_songPlayer.hasSong();
    m_lastSongBar = -1;

    if (m_songModeActive) {
        m_songPlayer.reset();
        // Prime the active section/chord immediately so the UI/engine reflect
        // the chart even before the transport rolls. (The always-on timer in the
        // ctor drives the per-bar stepping.)
        applySongStepForBar(std::max(1, m_audio.transport().positionBar() + 1));
    } else {
        m_styleEngine.cancelSectionRequest();
    }

    const auto js = juce::String("window.JuceBridge && window.JuceBridge.onSongModeChanged && ")
                  + "window.JuceBridge.onSongModeChanged(" + boolLiteral(m_songModeActive) + ");";
    pushToWeb(js);
}

void MainComponent::applySongStepForBar(int bar, bool applySection)
{
    auto step = m_songPlayer.updateToBar(bar);

    if (applySection && step.sectionChanged) {
        m_styleEngine.allNotesOff();
        m_styleEngine.setSection(step.section);
        if (m_panel) m_panel->setActiveSection(juce::String(step.section));
    }

    if (step.chordChanged) {
        m_styleEngine.setChord(step.chord);
        const auto chordName = step.chord.toString();
        pushToWeb(juce::String("window.JuceBridge && window.JuceBridge.onChordChanged(")
                  + jsString(juce::String(chordName)) + ");");
    }
}

void MainComponent::queueSongSectionForBar(int bar)
{
    if (m_songPlayer.shouldStopAtBar(bar)) {
        m_styleEngine.requestStopAtBarBoundary();
        return;
    }

    const auto step = m_songPlayer.previewToBar(bar);
    if (step.sectionChanged)
        m_styleEngine.requestSection(step.section, false, {});
}

// ---- Style Recorder ----------------------------------------------------

juce::String MainComponent::recorderStatusText() const
{
    const auto& info = cadenza::arranger::recorderPartInfo(m_recorder.targetPart());
    const auto cfg = m_recorder.config();
    return juce::String(cfg.bars) + " bars at " + juce::String(cfg.tempo)
         + " BPM, part: " + info.label;
}

void MainComponent::recorderNewSession(int bars)
{
    if (m_songModeActive) setSongMode(false);
    if (m_state.playing()) togglePlayback();   // stop cleanly first

    cadenza::arranger::RecorderConfig cfg;
    cfg.name = "My Style";
    cfg.tempo = m_state.bpm();
    cfg.beatsPerBar = m_audio.transport().beatsPerBar();
    cfg.beatUnit = m_audio.transport().beatUnit();
    cfg.ticksPerBeat = m_audio.transport().ticksPerBeat();
    cfg.bars = std::max(1, bars);
    m_recorder.startSession(cfg);
    m_recordArmed.store(false);

    m_styleEngine.setStyle(m_recorder.snapshotStyle());
    m_styleEngine.setSection("mainA");
    m_currentMain = "mainA";
    m_audio.setMetronomeEnabled(m_metronomeOn);
    m_midi.setCaptureMode(true);   // whole keyboard plays/records the target part
    recorderPrepareTargetChannel();
    updateNativePanelStyle();
    if (m_panel)
        m_panel->setRecorderState(true, false,
            recorderStatusText() + " - press Record, then play");
    juce::Logger::writeToLog("[Cadenza] Style Recorder: new session, "
                             + juce::String(cfg.bars) + " bars at "
                             + juce::String(cfg.tempo) + " BPM");
}

void MainComponent::recorderSetPart(int partIndex)
{
    m_recorder.setTargetPart(cadenza::arranger::recorderPartInfo(partIndex).part);
    if (m_recorder.sessionActive())
        recorderPrepareTargetChannel();   // audible voice on the newly-selected channel
    recorderReloadEditor();
    if (m_recorder.sessionActive() && m_panel)
        m_panel->setRecorderState(true, m_recordArmed.load(),
            recorderStatusText() + " - press Record, then play");
}

void MainComponent::recorderArm(bool on)
{
    if (!m_recorder.sessionActive()) {
        if (m_panel) m_panel->setRecorderState(false, false, "Press New to record your own style");
        return;
    }

    if (on) {
        m_recorder.discardTake();
        m_recordArmed.store(true);
        if (!m_state.playing()) togglePlayback();   // loop the section + metronome
        if (m_panel)
            m_panel->setRecorderState(true, true,
                "RECORDING " + juce::String(cadenza::arranger::recorderPartInfo(
                                   m_recorder.targetPart()).label)
                + " - the loop overdubs; click Record again to keep the take");
    } else {
        m_recordArmed.store(false);
        const bool added = m_recorder.commitTake();
        recorderRefreshStyle();
        recorderReloadEditor();
        if (m_panel)
            m_panel->setRecorderState(true, false,
                added ? juce::String("Take kept - pick another part, Record again, or Save")
                      : juce::String("No notes captured - press Record and play"));
        juce::Logger::writeToLog(juce::String("[Cadenza] Style Recorder: take ")
                                 + (added ? "committed" : "empty"));
    }
}

void MainComponent::recorderRefreshStyle()
{
    if (!m_recorder.sessionActive()) return;

    m_styleEngine.setStyle(m_recorder.snapshotStyle());
    updateNativePanelStyle();
    applyMixerState();

    // While playing, the style swap lands on the next audio block; refresh the
    // panel once more shortly after so new mixer strips appear.
    juce::Component::SafePointer<MainComponent> safe(this);
    juce::Timer::callAfterDelay(150, [safe] {
        if (auto* self = safe.getComponent(); self && self->m_recorder.sessionActive()) {
            self->updateNativePanelStyle();
            self->applyMixerState();
        }
    });
}

void MainComponent::recorderClearPart()
{
    if (!m_recorder.sessionActive()) return;
    const bool removed = m_recorder.clearTargetPart();
    if (removed) {
        recorderRefreshStyle();
        recorderReloadEditor();
    }
    if (m_panel)
        m_panel->setRecorderState(true, false,
            removed ? juce::String("Cleared ")
                          + cadenza::arranger::recorderPartInfo(m_recorder.targetPart()).label
                    : juce::String("Nothing recorded on this part yet"));
}

void MainComponent::recorderSave()
{
    if (!m_recorder.sessionActive()) return;

    const auto defaultDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    m_recSaveChooser = std::make_unique<juce::FileChooser>(
        "Save your style (.cstyle)",
        defaultDir.getChildFile("My Style.cstyle"), "*.cstyle");

    juce::Component::SafePointer<MainComponent> safe(this);
    m_recSaveChooser->launchAsync(
        juce::FileBrowserComponent::saveMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [safe](const juce::FileChooser& chooser) {
            auto* self = safe.getComponent();
            if (self == nullptr) return;

            auto file = chooser.getResult();
            if (file == juce::File{}) { self->m_recSaveChooser.reset(); return; }
            if (!file.hasFileExtension("cstyle"))
                file = file.withFileExtension("cstyle");

            self->m_recorder.setStyleName(
                file.getFileNameWithoutExtension().toStdString());
            const bool ok = self->m_recorder.save(file.getFullPathName().toStdString());

            if (ok) {
                self->recorderRefreshStyle();   // pick up the new name
                if (self->m_settings) {         // come back to this style next launch
                    auto& st = self->m_settings->state();
                    st.lastStyleId = self->m_recorder.snapshotStyle()->id;
                    st.lastStylePath = file.getFullPathName().toStdString();
                    self->saveSettings();
                }
                juce::Logger::writeToLog("[Cadenza] Style Recorder: saved "
                                         + file.getFullPathName());
            }
            if (self->m_panel)
                self->m_panel->setRecorderState(true, false,
                    ok ? "Saved " + file.getFileName() + " - keep recording or Exit"
                       : "SAVE FAILED: " + file.getFullPathName());
            self->m_recSaveChooser.reset();
        });
}

void MainComponent::recorderOpenEditor()
{
    if (!m_recorder.sessionActive()) return;

    if (!m_partEditor) {
        cadenza::ui::StylePartEditorWindow::Callbacks cb;
        cb.onNotesEdited = [this](std::vector<cadenza::arranger::PatternNote> notes) {
            m_recorder.replacePartNotes(std::move(notes));
            recorderRefreshStyle();   // hear the edit immediately while looping
        };
        cb.onAudition = [this](int note, int velocity) {
            const auto& info = cadenza::arranger::recorderPartInfo(m_recorder.targetPart());
            const int channel = info.midiChannel;
            if (velocity > 0) {
                m_audio.noteOn(channel, note, velocity);
                // The editor only reports presses; release the audition note shortly.
                juce::Component::SafePointer<MainComponent> safe(this);
                juce::Timer::callAfterDelay(220, [safe, channel, note] {
                    if (auto* self = safe.getComponent())
                        self->m_audio.noteOff(channel, note);
                });
            } else {
                m_audio.noteOff(channel, note);
            }
        };
        cb.onClosed = [this] {
            juce::Component::SafePointer<MainComponent> safe(this);
            juce::MessageManager::callAsync([safe] {
                if (auto* self = safe.getComponent())
                    self->m_partEditor.reset();   // not from inside the window's callback
            });
        };
        m_partEditor = std::make_unique<cadenza::ui::StylePartEditorWindow>(std::move(cb));
    }

    recorderReloadEditor();
    m_partEditor->setVisible(true);
    m_partEditor->toFront(true);
}

void MainComponent::recorderReloadEditor()
{
    if (!m_partEditor || !m_recorder.sessionActive()) return;
    const auto& info = cadenza::arranger::recorderPartInfo(m_recorder.targetPart());
    m_partEditor->setPart(info.label,
                          m_recorder.targetPartNotes(),
                          m_recorder.sectionLengthTicks(),
                          m_recorder.config().ticksPerBeat,
                          info.percussion);
}

void MainComponent::recorderPrepareTargetChannel()
{
    // Give the target part's channel an audible voice so playing the keyboard
    // (audition) and the recorded loop are heard. An empty recorder section
    // sends no channel setup of its own, so without this the channel can be
    // silent or stuck on a stale/wrong preset.
    const auto& info = cadenza::arranger::recorderPartInfo(m_recorder.targetPart());
    if (info.percussion) {
        // SynthEngine forces the drum bank for the drum synth channels, so a
        // program change here selects the kit (0 = standard).
        m_audio.programChange(info.midiChannel, 0);
    } else {
        m_audio.controlChange(info.midiChannel, 0, 0);    // bank MSB 0 (GM)
        m_audio.controlChange(info.midiChannel, 32, 0);   // bank LSB 0
        m_audio.programChange(info.midiChannel,
                              cadenza::midi::defaultGmProgramForRole(info.partName));
    }
    m_audio.controlChange(info.midiChannel, 7, 100);      // audible volume
}

void MainComponent::recorderCloseEditor()
{
    m_partEditor.reset();
}

void MainComponent::recorderExit()
{
    m_recordArmed.store(false);
    m_midi.setCaptureMode(false);
    m_audio.setMetronomeEnabled(false);
    if (m_state.playing()) togglePlayback();
    recorderCloseEditor();
    m_recorder.endSession();

    if (m_panel)
        m_panel->setRecorderState(false, false, "Press New to record your own style");

    // Back to the regular arranger: reload the previous style.
    if (!tryLoadLastStyle())
        tryLoadFactoryStyle();
    updateNativePanelStyle();
    juce::Logger::writeToLog("[Cadenza] Style Recorder: session closed");
}

namespace {
// Classify a section id by its name prefix.
std::string sectionType(const std::string& id)
{
    auto starts = [&id](const char* p) { return id.rfind(p, 0) == 0; };
    if (starts("intro"))  return "intro";
    if (starts("fill"))   return "fill";
    if (starts("ending")) return "ending";
    return "main";
}
}

void MainComponent::startFadeOut()
{
    if (!m_state.playing())
        return;
    m_audio.startFadeOut(8.0);
    juce::Logger::writeToLog("[Cadenza] Fade out started");
}

void MainComponent::togglePlayback()
{
    const bool play = !m_state.playing();
    m_state.setPlaying(play);
    m_audio.cancelFadeOut();   // a manual start/stop overrides a running fade
    if (play) {
        if (m_songModeActive) {
            m_songPlayer.reset();
            m_lastSongBar = -1;
            applySongStepForBar(1);
            queueSongSectionForBar(2);
        }
        m_audio.play();   // restarts the transport from bar 0
    } else {
        m_audio.stop();
    }
    if (m_panel) m_panel->setPlaying(play);
    pushToWeb(juce::String("window.JuceBridge && window.JuceBridge.onPlayStateChanged(")
              + (play ? "true" : "false") + ");");
}

void MainComponent::executeControlCommand(const std::string& command)
{
    if (command == "play") {
        togglePlayback();
    } else {
        if (m_songModeActive) setSongMode(false);
        triggerSection(command);   // section id ("mainA", "fillAA", "intro", "ending", ...)
    }
}

void MainComponent::triggerSection(const std::string& id)
{
    const std::string type = sectionType(id);
    if (type == "main")
        m_currentMain = id;   // remember as the return target for fills/intros

    // Auto Fill-In (Yamaha behavior): pressing a Main button while the band is
    // playing inserts that main's own fill for one pattern, then lands on the
    // main. Pressing the active main again just plays its fill.
    if (type == "main" && m_state.autoFillEnabled() && m_audio.transport().playing()) {
        // "mainB" -> "fillBB"; skip when the style has no such fill.
        if (id.size() == 5 && id.rfind("main", 0) == 0) {
            const char v = id[4];
            const std::string fillId = std::string("fill") + v + v;
            const auto style = m_styleEngine.currentStyle();
            if (style && style->findSection(fillId) != nullptr) {
                m_styleEngine.requestSection(fillId, true, id);
                if (m_panel) m_panel->setActiveSection(juce::String(fillId));
                return;
            }
        }
    }

    const bool once = (type != "main");   // intro / fill / ending are one-shots
    const std::string returnTo = (type == "ending")
        ? std::string()
        : (m_currentMain.empty() ? std::string("mainA") : m_currentMain);

    if (m_audio.transport().playing()) {
        // Quantized: the engine switches exactly at the next bar boundary, and
        // handles the one-shot return / ending stop itself (sample-tight).
        m_styleEngine.requestSection(id, once, returnTo);
    } else {
        // Stopped: set it up immediately so Play starts on the chosen section.
        m_styleEngine.setSection(id, once, returnTo);
        applyMixerState();
    }
    if (m_panel) m_panel->setActiveSection(juce::String(id));   // show intent now
}

void MainComponent::timerCallback()
{
    // Hot-plug MIDI: rescan ~every 2s (20Hz timer) so a keyboard plugged in after
    // launch is picked up automatically.
    if (++m_midiRescanTicks >= 40) {
        m_midiRescanTicks = 0;
        m_midi.refreshInputs();
    }

    // Section one-shot returns / ending stops are now handled sample-tight in the
    // StyleEngine (audio thread) via its section-changed / stop callbacks.

    // Piano-roll editor: sweep the playback marker across the looping section.
    if (m_partEditor && m_recorder.sessionActive()) {
        const int len = m_recorder.sectionLengthTicks();
        const bool playing = m_audio.transport().playing();
        const int tick = (playing && len > 0)
            ? m_audio.transport().positionTickInt() % len
            : 0;
        m_partEditor->setPlaybackTick(tick, playing);
    }

    // A fade-out that reached silence stopped the transport on the audio thread;
    // sync the UI play state here.
    if (m_audio.consumeFadeCompleted() && m_state.playing()) {
        m_state.setPlaying(false);
        if (m_panel) m_panel->setPlaying(false);
        pushToWeb("window.JuceBridge && window.JuceBridge.onPlayStateChanged(false);");
        juce::Logger::writeToLog("[Cadenza] Fade out complete; stopped");
    }

    if (!m_songModeActive)
        return;

    auto& transport = m_audio.transport();
    if (!transport.playing())
        return;

    const int bar = transport.positionBar() + 1;  // Transport is 0-based; chart is 1-based.
    if (bar == m_lastSongBar)
        return;

    m_lastSongBar = bar;
    // Consume the current chart state without re-switching its section after the
    // boundary, then queue the following section one full bar ahead.
    applySongStepForBar(bar, false);
    queueSongSectionForBar(bar + 1);
}

void MainComponent::openPluginFileChooser()
{
    // Default to the project root so the bundled VST3/ and NeuralPi/ folders are visible.
    const auto projectRoot = findWebRoot().getParentDirectory().getParentDirectory();
    const auto startDir = projectRoot.isDirectory()
        ? projectRoot
        : juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory);

    m_pluginChooser = std::make_unique<juce::FileChooser>(
        "Choose a VST3 plugin (.vst3)", startDir, "*.vst3");

    juce::Component::SafePointer<MainComponent> safe(this);
    m_pluginChooser->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::canSelectDirectories,
        [safe](const juce::FileChooser& chooser) {
            if (auto* self = safe.getComponent()) {
                const auto file = chooser.getResult();
                if (file.exists())
                    self->loadAndApplyPluginFile(file);
                self->m_pluginChooser.reset();
            }
        });
}

bool MainComponent::loadAndApplyPluginFile(const juce::File& file)
{
    if (!file.exists() || !isSupportedPluginFile(file)) {
        juce::Logger::writeToLog("[Cadenza] Not a .vst3 plugin: " + file.getFullPathName());
        return false;
    }

    std::string error;
    const bool ok = m_audio.loadMasterEffect(file.getFullPathName().toStdString(), error);
    if (!ok) {
        juce::Logger::writeToLog("[Cadenza] VST3 load failed: " + juce::String(error));
        pushPluginStateToWeb();
        return false;
    }

    juce::Logger::writeToLog("[Cadenza] Master effect loaded: "
                             + juce::String(m_audio.masterEffectName())
                             + " (" + file.getFullPathName() + ")");
    pushPluginStateToWeb();
    return true;
}

void MainComponent::clearMasterEffect()
{
    m_audio.clearMasterEffect();
    juce::Logger::writeToLog("[Cadenza] Master effect cleared");
    pushPluginStateToWeb();
}

void MainComponent::pushPluginStateToWeb()
{
    const auto name = m_audio.hasMasterEffect()
        ? juce::String(m_audio.masterEffectName())
        : juce::String();
    pushToWeb(juce::String("window.JuceBridge && window.JuceBridge.onPluginChanged && ")
              + "window.JuceBridge.onPluginChanged(" + jsString(name) + ");");
}

void MainComponent::applyRightHand()
{
    if (!m_settings) return;
    const auto& st = m_settings->state();
    for (int i = 0; i < cadenza::midi::MidiRouter::kNumRightLayers; ++i) {
        const auto& layer = st.rightLayers[i];
        m_midi.setRightLayerEnabled(i, layer.enabled);
        m_midi.setRightLayerOctave(i, layer.octave);
        const int channel = m_midi.rightLayerChannel(i);
        if (!layer.pluginPath.empty()) {
            // VST3 voice for this layer (notes route to the plugin by channel).
            std::string err;
            if (!m_audio.loadPartInstrument(channel, layer.pluginPath, err))   // idempotent
                juce::Logger::writeToLog("[Cadenza] Right " + juce::String(i + 1)
                                         + " VST load failed: " + juce::String(err));
        } else {
            m_audio.clearPartInstrument(channel);            // back to the GM SoundFont
            m_audio.programChange(channel, layer.program);   // GM voice
        }
        m_audio.controlChange(channel, 7 /*CC7 volume/gain*/, layer.volume);
        juce::Logger::writeToLog("[Cadenza] Right " + juce::String(i + 1)
            + (layer.enabled ? " ON" : " off")
            + (layer.pluginPath.empty() ? (" program=" + juce::String(layer.program)) : juce::String(" VST"))
            + " vol=" + juce::String(layer.volume)
            + " oct=" + juce::String(layer.octave)
            + " ch=" + juce::String(channel));
    }
}

void MainComponent::buildNativePanel()
{
    m_panel = std::make_unique<cadenza::ui::NativePanel>();
    addAndMakeVisible(*m_panel);

    cadenza::ui::NativePanel::Callbacks cb;

    cb.togglePlay = [this] { togglePlayback(); };
    cb.openStyle     = [this] { openStyleFileChooser(); };
    cb.openSoundFont = [this] { openSoundFontFileChooser(); };
    cb.openAudioSettings = [this] { showAudioSettings(); };
    cb.openMidiSettings  = [this] { showMidiSettings(); };

    cb.onLoadInstrumentPlugin = [this](int channel) { choosePartInstrument(channel); };
    cb.onOpenInstrumentEditor = [this](int channel) { m_audio.showPartInstrumentEditor(channel); };
    cb.onClearInstrumentPlugin = [this](int channel) {
        m_audio.clearPartInstrument(channel);   // back to the GM SoundFont
        if (m_panel) {
            const juce::String insName = (channel == 10)
                ? juce::String("Drum Kit")
                : juce::String(cadenza::midi::gmInstrumentName(m_mixer.program(channel)));
            m_panel->setMixerInstrumentName(channel, insName);
        }
        persistStyleMix();   // remember "GM" for this channel
    };
    cb.toggleWeb     = [this] {
        m_webView.setVisible(!m_webView.isVisible());
        resized();
    };
    cb.onSetTempo = [this](int target) {
        const int bpm = m_state.setBpm(target);
        m_audio.setBpm(static_cast<double>(bpm));
        if (m_panel) m_panel->setBpm(bpm);
        pushToWeb("window.JuceBridge && window.JuceBridge.setBPM(" + juce::String(bpm) + ");");
        saveSettings();
    };
    cb.nudgeTempo = [this](int delta) {
        const int bpm = m_state.setBpm(m_state.bpm() + delta);
        m_audio.setBpm(static_cast<double>(bpm));
        if (m_panel) m_panel->setBpm(bpm);
        pushToWeb("window.JuceBridge && window.JuceBridge.setBPM(" + juce::String(bpm) + ");");
        saveSettings();
    };
    cb.onKeyboardNote = [this](int note, int velocity, bool isOn) {
        m_midi.injectNote(note, velocity, isOn);   // full path: chord detect + live melody
    };
    cb.onMixerVolume = [this](int channel, int volume) {
        m_mixer.setVolume(channel, volume);
        applyMixerState();
        persistStyleMix();
    };
    cb.onMixerMute = [this](int channel, bool mute) {
        m_mixer.setMute(channel, mute);
        applyMixerState();
        persistStyleMix();
    };
    cb.onMixerSolo = [this](int channel, bool solo) {
        m_mixer.setSolo(channel, solo);
        applyMixerState();
        persistStyleMix();
    };
    cb.onMixerInstrument = [this](int channel, int program) {
        m_mixer.setProgram(channel, program);
        applyMixerState();   // re-asserts program + volume on the channel
        persistStyleMix();
        if (m_panel)
            m_panel->setMixerInstrumentName(channel, juce::String(cadenza::midi::gmInstrumentName(program)));
    };
    cb.onPad = [this](int index) {
        // Trigger a one-shot GM percussion hit on the drum channel (10).
        static const int padNotes[4] = { 49 /*crash*/, 39 /*hand clap*/, 54 /*tambourine*/, 56 /*cowbell*/ };
        if (index >= 0 && index < 4)
            m_audio.noteOn(10, padNotes[index], 112);
    };

    cb.onEqChanged = [this](int lowDb, int midDb, int highDb) {
        m_audio.setEqGains(static_cast<float>(lowDb),
                           static_cast<float>(midDb),
                           static_cast<float>(highDb));
        if (m_settings) {
            m_settings->state().eqLowDb  = lowDb;
            m_settings->state().eqMidDb  = midDb;
            m_settings->state().eqHighDb = highDb;
            saveSettings();
        }
    };

    cb.onCompChanged = [this](int amount) {
        m_audio.setCompAmount(amount);
        if (m_settings) {
            m_settings->state().compAmount = amount;
            saveSettings();
        }
    };

    cb.onMasterChanged = [this](int volume) {
        m_audio.setMasterVolume(volume);
        if (m_settings) {
            m_settings->state().masterVolume = volume;
            saveSettings();
        }
    };

    cb.onReverbChanged = [this](int amount) {
        m_audio.setReverbLevel(amount);
        if (m_settings) {
            m_settings->state().reverbLevel = amount;
            saveSettings();
        }
    };

    cb.onDrumsChanged = [this](int volume) {
        // Quick drum-channel level (cadenza channel 10): mirror the drum mixer strip.
        if (!m_mixer.has(10)) return;
        m_mixer.setVolume(10, volume);
        applyMixerState();
        if (m_panel) m_panel->updateMixerStrip(10, volume, m_mixer.mute(10), m_mixer.solo(10));
        persistStyleMix();
    };

    cb.onSplitChanged = [this](int note) {
        m_midi.setSplitPoint(note);                 // notes < split drive chords; >= play melody
        if (m_settings) {
            m_settings->state().splitNote = note;
            saveSettings();
        }
    };

    // --- Right 1/2/3 layered right-hand voices ---
    cb.onRightEnabled = [this](int layer, bool on) {
        if (!m_settings || layer < 0 || layer >= 3) return;
        m_settings->state().rightLayers[layer].enabled = on;
        applyRightHand();
        if (!on) m_audio.allNotesOff();   // release anything the layer was holding
        saveSettings();
    };
    cb.onRightInstrument = [this](int layer, int program) {
        if (!m_settings || layer < 0 || layer >= 3) return;
        auto& L = m_settings->state().rightLayers[layer];
        L.program = program;
        L.pluginPath.clear();                                           // switch back to a GM voice
        if (layer == 0) m_settings->state().melodyProgram = program;   // keep the mirror in sync
        applyRightHand();                                              // clears any VST, sets the GM program
        if (m_panel)
            m_panel->setRightVoiceName(layer, juce::String(cadenza::midi::gmInstrumentName(program)), false);
        saveSettings();
    };
    cb.onRightLoadPlugin = [this](int layer) { chooseRightLayerInstrument(layer); };
    cb.onRightOpenEditor = [this](int layer) {
        m_audio.showPartInstrumentEditor(m_midi.rightLayerChannel(layer));
    };
    cb.onRightVolume = [this](int layer, int volume) {
        if (!m_settings || layer < 0 || layer >= 3) return;
        m_settings->state().rightLayers[layer].volume = volume;
        applyRightHand();
        saveSettings();
    };
    cb.onRightOctave = [this](int layer, int delta) {
        if (!m_settings || layer < 0 || layer >= 3) return;
        auto& L = m_settings->state().rightLayers[layer];
        L.octave = juce::jlimit(-2, 2, L.octave + delta);
        applyRightHand();
        if (m_panel)
            m_panel->setRightVoice(layer, L.enabled, L.program, L.volume, L.octave);
        saveSettings();
    };

    cb.onStoreRegistration  = [this](int slot) { captureRegistration(slot); };
    cb.onRecallRegistration = [this](int slot) { recallRegistration(slot); };

    cb.onRecNew   = [this](int bars) { recorderNewSession(bars); };
    cb.onRecPart  = [this](int idx)  { recorderSetPart(idx); };
    cb.onRecMetronome = [this](bool on) {
        m_metronomeOn = on;
        if (m_recorder.sessionActive()) m_audio.setMetronomeEnabled(on);
    };
    cb.onRecArm   = [this](bool on)  { recorderArm(on); };
    cb.onRecEdit  = [this] { recorderOpenEditor(); };
    cb.onRecClear = [this] { recorderClearPart(); };
    cb.onRecSave  = [this] { recorderSave(); };
    cb.onRecExit  = [this] { recorderExit(); };

    cb.onOts = [this](int slot) { applyOts(slot); };
    cb.setOtsLink = [this](bool on) {
        if (!m_settings) return;
        m_settings->state().otsLinkEnabled = on;
        saveSettings();
    };

    cb.nudgeTranspose = [this](int delta) {
        const int t = m_state.setTranspose(m_state.transpose() + delta);
        m_styleEngine.setGlobalTranspose(t);                 // transpose affects style
        m_midi.setLiveTranspose(t);                          // ...and the right-hand melody
        if (m_panel) m_panel->setTranspose(t);
        pushToWeb("window.JuceBridge && window.JuceBridge.setTranspose(" + juce::String(t) + ");");
        saveSettings();
    };
    cb.nudgeOctave = [this](int delta) {
        const int o = m_state.setOctave(m_state.octave() + delta);
        m_midi.setLiveOctave(o);                             // octave affects live melody only
        if (m_panel) m_panel->setOctave(o);
        pushToWeb("window.JuceBridge && window.JuceBridge.setOctave(" + juce::String(o) + ");");
        juce::Logger::writeToLog("[Cadenza] native octave -> " + juce::String(o));
        saveSettings();
    };

    cb.setArranger = [this](bool on) {
        if (!on) m_styleEngine.allNotesOff();
        m_styleEngine.setEnabled(on);
        m_state.setChordSourceEnabled("arranger", on);
        saveSettings();
    };
    cb.setChordMemory = [this](bool on) {
        m_midi.setChordMemory(on);
        m_state.setChordSourceEnabled("memory", on);
        saveSettings();
    };
    cb.setSyncroStop = [this](bool on) {
        m_midi.setSyncroStopOnRelease(on);
        m_state.setSyncroStopOnRelease(on);
        saveSettings();
    };
    cb.setAutoFill = [this](bool on) {
        m_state.setAutoFillEnabled(on);
        saveSettings();
    };
    cb.fadeOut = [this] { startFadeOut(); };
    cb.setFingeredOnBass = [this](bool on) {
        m_midi.setChordDetectionMode(on ? arranger::ChordDetectionMode::FingeredOnBass
                                        : arranger::ChordDetectionMode::Fingered);
        m_state.setChordSourceEnabled("bass", on);
        saveSettings();
    };
    cb.selectSection = [this](const std::string& id) {
        if (m_songModeActive) setSongMode(false);   // manual section control overrides song mode
        triggerSection(id);   // switch section + handle one-shot intro/fill/ending transitions
        saveSettings();
    };

    m_panel->setCallbacks(std::move(cb));

    // Reflect current persisted state on the panel.
    m_panel->setTranspose(m_state.transpose());
    m_panel->setOctave(m_state.octave());
    m_panel->setBpm(m_state.bpm());
    m_panel->setPlaying(m_state.playing());
    m_panel->setToggleStates(m_state.chordSourceEnabled("arranger"),
                             m_state.chordSourceEnabled("memory"),
                             m_state.syncroStopOnRelease(),
                             m_state.chordSourceEnabled("bass"),
                             m_state.autoFillEnabled());
    if (m_settings) {
        const auto& st = m_settings->state();
        m_panel->setEqGains(st.eqLowDb, st.eqMidDb, st.eqHighDb);
        m_panel->setCompAmount(st.compAmount);
        m_panel->setMasterVolume(st.masterVolume);
        m_panel->setReverbAmount(st.reverbLevel);
        m_panel->setSplitPoint(st.splitNote);
        for (int i = 0; i < 3; ++i) {
            const auto& L = st.rightLayers[i];
            m_panel->setRightVoice(i, L.enabled, L.program, L.volume, L.octave);
        }
        for (int i = 0; i < cadenza::settings::Settings::kNumRegistrations; ++i)
            m_panel->setRegistrationUsed(i, st.registrations[i].used);
        m_panel->setOtsLinkEnabled(st.otsLinkEnabled);
    }
    refreshOtsAvailability();
    resized();
}

void MainComponent::updateNativePanelStyle()
{
    if (!m_panel) return;

    auto style = m_styleEngine.currentStyle();
    if (!style) {
        m_panel->setStyleName({});
        m_panel->setSections({});
        refreshOtsAvailability();
        m_lastLinkedMain.clear();
        return;
    }

    m_panel->setStyleName(juce::String(style->name.empty() ? style->id : style->name));
    m_panel->setBpm(m_state.bpm());   // reflect the style's tempo (set in loadAndApplyStyleFile)

    std::vector<std::pair<std::string, std::string>> pairs;
    for (const auto& b : cadenza::arranger::sectionButtonsForStyle(*style))
        pairs.emplace_back(b.sectionId, b.label);
    m_panel->setSections(pairs);
    m_panel->setActiveSection(juce::String(m_styleEngine.currentSection()));
    refreshOtsAvailability();
    m_lastLinkedMain.clear();   // a new style means OTS Link starts fresh

    // --- Assign a free channel to each Right 1/2/3 layer that the style does NOT
    //     use, so the right-hand voices never collide with style parts. ---
    bool used[17] = { false };
    for (const auto& section : style->sections)
        for (const auto& part : section.parts)
            if (part.midiChannel >= 1 && part.midiChannel <= 16)
                used[part.midiChannel] = true;

    constexpr int kLayers = cadenza::midi::MidiRouter::kNumRightLayers;
    int oldRightCh[kLayers];
    for (int i = 0; i < kLayers; ++i)
        oldRightCh[i] = m_midi.rightLayerChannel(i);

    int layerChannel[kLayers];
    int lastFree = 1;
    for (int i = 0; i < kLayers; ++i) {
        int ch = lastFree;
        for (int c = lastFree; c <= 16; ++c) {
            if (c == 9 || c == 10 || used[c]) continue;   // skip both drum channels (RHY1/RHY2) and style parts
            ch = c; break;
        }
        used[ch] = true;                        // don't hand the same channel to two layers
        layerChannel[i] = ch;
        lastFree = ch + 1;
        m_midi.setRightLayerChannel(i, ch);
    }
    // If a layer moved to a new channel, clear any VST left on its old channel so
    // it doesn't linger (applyRightHand reloads the VST on the new channel).
    for (int i = 0; i < kLayers; ++i)
        if (oldRightCh[i] != layerChannel[i])
            m_audio.clearPartInstrument(oldRightCh[i]);
    const int melodyChannel = layerChannel[0];   // Right 1 = the primary "Melody" strip
    applyRightHand();   // assert each layer's enable/program/volume/octave on its channel

    // --- Mixer: live melody + one strip per distinct style channel ---
    struct StripSeed { int channel; int volume; int program; };
    std::vector<int> channels { melodyChannel };
    std::vector<std::pair<int, std::string>> labels { { melodyChannel, "Melody" } };
    std::vector<StripSeed> seeds;   // channel -> style part volume + instrument program
    const int melodyProgram = m_settings ? m_settings->state().melodyProgram : 0;
    seeds.push_back({ melodyChannel, 100, melodyProgram });
    for (const auto& section : style->sections) {
        for (const auto& part : section.parts) {
            const int ch = part.midiChannel;
            if (std::find(channels.begin(), channels.end(), ch) != channels.end())
                continue;
            channels.push_back(ch);
            labels.emplace_back(ch, part.name.empty() ? ("Ch " + std::to_string(ch)) : part.name);
            // Instrument: the style's own program, else the JJazzLab-style role default.
            const int program = part.program.value_or(cadenza::midi::defaultGmProgramForRole(part.name));
            seeds.push_back({ ch, part.volume.value_or(100), program });
        }
    }

    m_mixer.setChannels(channels);
    for (const auto& s : seeds) {
        m_mixer.setVolume(s.channel, s.volume);
        m_mixer.setProgram(s.channel, s.program);
    }

    // Re-apply the player's saved per-style tweaks on top of the defaults.
    // Reconcile per-part VST instruments instead of tearing every plugin down and
    // rebuilding it on each style switch (slow, froze the UI): keep a channel's
    // plugin if the new style wants the same one, and only clear ones that change
    // or are no longer used. applyStyleMix then loads the rest (loadPartInstrument
    // is idempotent, so unchanged plugins are a no-op).
    if (m_settings) {
        std::map<int, std::string> desired;
        const auto it = m_settings->state().styleMixes.find(m_settings->state().lastStyleId);
        if (it != m_settings->state().styleMixes.end())
            for (const auto& m : it->second)
                if (!m.pluginPath.empty())
                    desired[m.channel] = m.pluginPath;

        for (int ch = 1; ch <= 16; ++ch) {
            const auto d = desired.find(ch);
            const std::string want = (d == desired.end()) ? std::string() : d->second;
            if (m_audio.partInstrumentPath(ch) != want)
                m_audio.clearPartInstrument(ch);   // changed or no longer wanted
        }
        applyStyleMix(m_settings->state().lastStyleId);
    } else {
        m_audio.clearAllPartInstruments();
    }

    m_panel->setMixerChannels(labels);
    for (int ch : channels) {
        m_panel->updateMixerStrip(ch, m_mixer.volume(ch), m_mixer.mute(ch), m_mixer.solo(ch));
        const juce::String insName = (ch == 10)
            ? juce::String("Drum Kit")
            : (ch == 9)
                ? juce::String("Drum Kit (Rhythm 2)")
                : juce::String(cadenza::midi::gmInstrumentName(m_mixer.program(ch)));
        m_panel->setMixerInstrumentName(ch, insName);
    }

    // Sync the quick Drums knob to the drum channel's volume.
    m_panel->setDrumsLevel(m_mixer.has(10) ? m_mixer.volume(10) : 100);

    // Refresh the Right 1/2/3 voice strips (VST name if a plugin is loaded on the
    // layer's new channel, else the GM voice name).
    if (m_settings) {
        for (int i = 0; i < 3; ++i) {
            const auto& L = m_settings->state().rightLayers[i];
            m_panel->setRightVoice(i, L.enabled, L.program, L.volume, L.octave);
            if (!L.pluginPath.empty())
                m_panel->setRightVoiceName(i, juce::String(m_audio.partInstrumentName(m_midi.rightLayerChannel(i))), true);
            else
                m_panel->setRightVoiceName(i, juce::String(cadenza::midi::gmInstrumentName(L.program)), false);
        }
    }

    applyMixerState();
}

void MainComponent::applyMixerState()
{
    for (const auto& c : m_mixer.channels()) {
        // Re-assert the strip's instrument (program) over the style's own setup,
        // then its effective volume. On the drum channel a program selects the kit.
        m_audio.programChange(c.channel, c.program);
        m_audio.controlChange(c.channel, 7 /*CC7 volume*/, m_mixer.effectiveVolume(c.channel));
    }
}

void MainComponent::applyStyleMix(const std::string& styleId)
{
    if (!m_settings || styleId.empty())
        return;
    const auto it = m_settings->state().styleMixes.find(styleId);
    if (it == m_settings->state().styleMixes.end())
        return;

    // Apply the player's saved tweaks on top of the style's own defaults.
    for (const auto& m : it->second) {
        if (!m_mixer.has(m.channel))
            continue;   // only channels this style actually uses
        if (m.program >= 0) m_mixer.setProgram(m.channel, m.program);
        if (m.volume  >= 0) m_mixer.setVolume(m.channel, m.volume);
        m_mixer.setMute(m.channel, m.mute);
        m_mixer.setSolo(m.channel, m.solo);

        // Restore a saved per-part VST3 instrument for this channel.
        if (!m.pluginPath.empty()) {
            std::string err;
            if (m_audio.loadPartInstrument(m.channel, m.pluginPath, err)) {
                if (m_panel)
                    m_panel->setMixerInstrumentName(
                        m.channel, juce::String(m_audio.partInstrumentName(m.channel)));
            } else {
                juce::Logger::writeToLog("[Cadenza] Saved part instrument missing/failed ch="
                                         + juce::String(m.channel) + ": " + juce::String(err));
            }
        }
    }
}

void MainComponent::persistStyleMix()
{
    if (!m_settings)
        return;
    const std::string styleId = m_settings->state().lastStyleId;
    if (styleId.empty())
        return;

    const int melodyChannel = m_midi.melodyChannel();  // persists via melodyProgram, skip here
    std::vector<cadenza::settings::StyleChannelMix> mix;
    for (const auto& c : m_mixer.channels()) {
        if (c.channel == melodyChannel)
            continue;
        cadenza::settings::StyleChannelMix m;
        m.channel = c.channel;
        m.program = m_mixer.program(c.channel);
        m.volume  = m_mixer.volume(c.channel);
        m.mute    = m_mixer.mute(c.channel);
        m.solo    = m_mixer.solo(c.channel);
        m.pluginPath = m_audio.partInstrumentPath(c.channel);
        mix.push_back(m);
    }
    m_settings->state().styleMixes[styleId] = std::move(mix);
    saveSettings();
}

void MainComponent::captureRegistration(int slot)
{
    if (!m_settings || slot < 0 || slot >= cadenza::settings::Settings::kNumRegistrations)
        return;
    auto& st = m_settings->state();
    auto& r = st.registrations[slot];
    r.used      = true;
    r.styleId   = st.lastStyleId;
    r.stylePath = st.lastStylePath;
    r.bpm       = m_state.bpm();
    r.transpose = m_state.transpose();
    r.octave    = m_state.octave();
    r.splitNote = st.splitNote;
    r.eqLowDb = st.eqLowDb; r.eqMidDb = st.eqMidDb; r.eqHighDb = st.eqHighDb; r.compAmount = st.compAmount;
    r.bankMemory = st.bankMemory;
    r.chordArrangerEnabled = m_state.chordSourceEnabled("arranger");
    r.chordMemoryEnabled   = m_state.chordSourceEnabled("memory");
    r.chordBassEnabled     = m_state.chordSourceEnabled("bass");
    r.syncroStopOnRelease  = m_state.syncroStopOnRelease();
    r.autoFillEnabled      = m_state.autoFillEnabled();
    for (int i = 0; i < 3; ++i) r.rightLayers[i] = st.rightLayers[i];
    saveSettings();
    if (m_panel) m_panel->setRegistrationUsed(slot, true);
    juce::Logger::writeToLog("[Cadenza] Stored registration " + juce::String(slot + 1));
}

void MainComponent::recallRegistration(int slot)
{
    if (!m_settings || slot < 0 || slot >= cadenza::settings::Settings::kNumRegistrations)
        return;
    const auto r = m_settings->state().registrations[slot];   // copy: loading a style mutates settings
    if (!r.used)
        return;

    // 1) Reload the style first (resets tempo/mix), then override below.
    if (!r.styleId.empty()) {
        if (!selectStyleById(r.styleId) && !r.stylePath.empty())
            loadAndApplyStyleFile(juce::File(juce::String(r.stylePath)));
    }

    auto& st = m_settings->state();
    // 2) Tempo / transpose / octave.
    m_audio.setBpm(m_state.setBpm(r.bpm));
    m_state.setTranspose(r.transpose);
    m_state.setOctave(r.octave);
    // 3) Split / EQ / compressor.
    st.splitNote = r.splitNote;
    m_midi.setSplitPoint(r.splitNote);
    st.eqLowDb = r.eqLowDb; st.eqMidDb = r.eqMidDb; st.eqHighDb = r.eqHighDb; st.compAmount = r.compAmount;
    m_audio.setEqGains(static_cast<float>(r.eqLowDb), static_cast<float>(r.eqMidDb), static_cast<float>(r.eqHighDb));
    m_audio.setCompAmount(r.compAmount);
    // 4) Chord modes.
    m_state.setChordSourceEnabled("arranger", r.chordArrangerEnabled);
    m_state.setChordSourceEnabled("memory",   r.chordMemoryEnabled);
    m_state.setChordSourceEnabled("bass",     r.chordBassEnabled);
    m_state.setSyncroStopOnRelease(r.syncroStopOnRelease);
    m_state.setAutoFillEnabled(r.autoFillEnabled);
    // 5) Right-hand layers + bank.
    st.bankMemory = r.bankMemory;
    for (int i = 0; i < 3; ++i) st.rightLayers[i] = r.rightLayers[i];
    st.melodyProgram = st.rightLayers[0].program;

    applyRuntimeStateToEngines();
    applyRightHand();

    if (m_panel) {
        m_panel->setBpm(r.bpm);
        m_panel->setTranspose(r.transpose);
        m_panel->setOctave(r.octave);
        m_panel->setSplitPoint(r.splitNote);
        m_panel->setEqGains(r.eqLowDb, r.eqMidDb, r.eqHighDb);
        m_panel->setCompAmount(r.compAmount);
        m_panel->setToggleStates(r.chordArrangerEnabled, r.chordMemoryEnabled,
                                 r.syncroStopOnRelease, r.chordBassEnabled,
                                 r.autoFillEnabled);
        for (int i = 0; i < 3; ++i)
            m_panel->setRightVoice(i, r.rightLayers[i].enabled, r.rightLayers[i].program,
                                   r.rightLayers[i].volume, r.rightLayers[i].octave);
    }
    saveSettings();
    juce::Logger::writeToLog("[Cadenza] Recalled registration " + juce::String(slot + 1));
}

void MainComponent::applyOts(int slot)
{
    if (!m_settings || slot < 0 || slot >= 4)
        return;
    const auto style = m_styleEngine.currentStyle();
    if (!style)
        return;
    const auto& setting = style->ots[static_cast<std::size_t>(slot)];
    if (!setting.present) {
        juce::Logger::writeToLog("[Cadenza] OTS " + juce::String(slot + 1)
                                 + ": style defines no setting");
        return;
    }

    auto& st = m_settings->state();
    const auto targets = cadenza::arranger::otsRecallTargets(setting);
    bool anyLayerDisabled = false;
    for (int i = 0; i < 3; ++i) {
        auto& L = st.rightLayers[i];
        if (L.enabled && !targets[i].enabled)
            anyLayerDisabled = true;
        L.enabled = targets[i].enabled;
        if (targets[i].setProgram) {
            L.program = targets[i].program;
            L.pluginPath.clear();   // OTS voices are GM; drop any per-layer VST
        }
        if (targets[i].setVolume)
            L.volume = targets[i].volume;
    }
    st.melodyProgram = st.rightLayers[0].program;

    applyRightHand();
    if (anyLayerDisabled)
        m_audio.allNotesOff();   // release anything a now-disabled layer held

    if (m_panel) {
        for (int i = 0; i < 3; ++i) {
            const auto& L = st.rightLayers[i];
            m_panel->setRightVoice(i, L.enabled, L.program, L.volume, L.octave);
            if (L.pluginPath.empty())
                m_panel->setRightVoiceName(
                    i, juce::String(cadenza::midi::gmInstrumentName(L.program)), false);
        }
    }
    saveSettings();
    juce::Logger::writeToLog("[Cadenza] Applied OTS " + juce::String(slot + 1));
}

void MainComponent::applyOtsLinkForSection(const std::string& name)
{
    if (!m_settings || !m_settings->state().otsLinkEnabled)
        return;
    int slot = -1;
    if      (name == "mainA") slot = 0;
    else if (name == "mainB") slot = 1;
    else if (name == "mainC") slot = 2;
    else if (name == "mainD") slot = 3;
    if (slot < 0)
        return;                        // fills/intros/endings never retrigger
    if (name == m_lastLinkedMain)
        return;                        // fill returning to the same Main
    m_lastLinkedMain = name;
    applyOts(slot);
}

void MainComponent::refreshOtsAvailability()
{
    if (!m_panel)
        return;
    std::array<bool, 4> available { false, false, false, false };
    if (const auto style = m_styleEngine.currentStyle())
        for (std::size_t i = 0; i < available.size(); ++i)
            available[i] = style->ots[i].present;
    m_panel->setOtsAvailable(available);
}

void MainComponent::handleBridgePayload(const juce::var& payload)
{
    const auto json = payload.toString();
    const auto result = m_router.route(parseBridgeMessage(json));

    if (!result.handled)
    {
        juce::Logger::writeToLog("Unhandled Cadenza bridge message: " + json);
        return;
    }

    if (!result.javascript.empty())
        m_webView.evaluateJavascript(juce::String(result.javascript));
}

cadenza::BridgeMessage MainComponent::parseBridgeMessage(const juce::String& json) const
{
    cadenza::BridgeMessage message;

    const auto parsed = juce::JSON::parse(json);
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return message;

    message.type = object->getProperty("type").toString().toStdString();

    const auto payload = object->getProperty("payload");
    const auto* payloadObject = payload.getDynamicObject();
    if (payloadObject == nullptr)
        return message;

    const auto& properties = payloadObject->getProperties();
    for (int index = 0; index < properties.size(); ++index)
    {
        const auto& property = properties.getName(index).toString();
        message.payload[property.toStdString()] = convertValue(properties.getValueAt(index));
    }

    return message;
}

cadenza::BridgeValue MainComponent::convertValue(const juce::var& value) const
{
    if (value.isBool())
        return cadenza::BridgeValue::boolean(static_cast<bool>(value));

    if (value.isInt() || value.isInt64() || value.isDouble())
        return cadenza::BridgeValue::integer(static_cast<int>(value));

    return cadenza::BridgeValue::text(value.toString().toStdString());
}
