#include "MainComponent.h"
#include "Arranger/StyleLoader.h"
#include "Arranger/SongLoader.h"
#include "Audio/MidiChannel.h"
#include "Midi/LiveMelodyVoice.h"
#include "Midi/GmInstruments.h"
#include "Arranger/SectionButtons.h"
#include "UI/NativePanel.h"

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
    const auto ext = lowercase(file.getFileExtension().toStdString());
    return ext == ".cstyle" || ext == ".sty";
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
    }
    juce::Logger::writeToLog("[Cadenza] Synth engine: " + juce::String(m_audio.synthEngineName()));
    if (!m_audio.supportsSoundFonts()) {
        juce::Logger::writeToLog("[Cadenza] WARNING: NullSynthEngine is active; SoundFont loading is unavailable and playback will be silent/log-only.");
    }

    // Wire the style engine into the audio thread's tick callback.
    m_styleEngine.install();
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
    applyMelodyProgram();

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
        if (const auto ev = m_midi.handleVirtualMelodyNote(note, velocity, true)) {
            m_audio.noteOn(ev->channel, ev->note, ev->velocity);
            juce::Logger::writeToLog(juce::String("[Cadenza] UI melody on orig=") + juce::String(note)
                + " shifted=" + juce::String(ev->note)
                + " ch=" + juce::String(ev->channel)
                + " octave=" + juce::String(m_midi.liveOctave()));
        } else {
            m_audio.noteOn(channel == 0 ? 1 : channel, note, velocity);
        }
    };
    hooks.onNoteOff = [this](int channel, int note) {
        if (const auto ev = m_midi.handleVirtualMelodyNote(note, 0, false)) {
            m_audio.noteOff(ev->channel, ev->note);
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
        // The "Bank Memory" voice picker selects the live right-hand melody
        // instrument: map the voice name to a GM program on the melody channel.
        if (m_settings)
            m_settings->state().melodyProgram = cadenza::midi::gmProgramForBankName(bankName);
        applyMelodyProgram();
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
        "*.cstyle;*.sty");

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
    const auto styleJs = juce::String("window.JuceBridge && window.JuceBridge.onStyleLoaded({id:")
        + jsString(sharedStyle->id)
        + ",name:" + jsString(sharedStyle->name)
        + ",path:" + jsString(file.getFullPathName())
        + "});";
    pushToWeb(styleJs);
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
    }

    const auto js = juce::String("window.JuceBridge && window.JuceBridge.onSongModeChanged && ")
                  + "window.JuceBridge.onSongModeChanged(" + boolLiteral(m_songModeActive) + ");";
    pushToWeb(js);
}

void MainComponent::applySongStepForBar(int bar)
{
    auto step = m_songPlayer.updateToBar(bar);

    if (step.sectionChanged) {
        m_styleEngine.allNotesOff();
        m_styleEngine.setSection(step.section);
    }

    if (step.chordChanged) {
        m_styleEngine.setChord(step.chord);
        const auto chordName = step.chord.toString();
        pushToWeb(juce::String("window.JuceBridge && window.JuceBridge.onChordChanged(")
                  + jsString(juce::String(chordName)) + ");");
    }
}

void MainComponent::timerCallback()
{
    // Hot-plug MIDI: rescan ~every 2s (20Hz timer) so a keyboard plugged in after
    // launch is picked up automatically.
    if (++m_midiRescanTicks >= 40) {
        m_midiRescanTicks = 0;
        m_midi.refreshInputs();
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
    applySongStepForBar(bar);
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

void MainComponent::applyMelodyProgram()
{
    const int program = m_settings ? m_settings->state().melodyProgram : 0;
    const int channel = m_midi.melodyChannel();
    m_audio.programChange(channel, program);
    juce::Logger::writeToLog("[Cadenza] live melody program=" + juce::String(program)
                             + " on Cadenza channel=" + juce::String(channel));
}

void MainComponent::buildNativePanel()
{
    m_panel = std::make_unique<cadenza::ui::NativePanel>();
    addAndMakeVisible(*m_panel);

    cadenza::ui::NativePanel::Callbacks cb;

    cb.togglePlay = [this] {
        const bool play = !m_state.playing();
        m_state.setPlaying(play);
        if (play) {
            if (m_songModeActive) { m_songPlayer.reset(); m_lastSongBar = -1; }
            m_audio.play();
        } else {
            m_audio.stop();
        }
        if (m_panel) m_panel->setPlaying(play);
        pushToWeb(juce::String("window.JuceBridge && window.JuceBridge.onPlayStateChanged(")
                  + (play ? "true" : "false") + ");");
    };
    cb.openStyle     = [this] { openStyleFileChooser(); };
    cb.openSoundFont = [this] { openSoundFontFileChooser(); };
    cb.openAudioSettings = [this] { showAudioSettings(); };
    cb.toggleWeb     = [this] {
        m_webView.setVisible(!m_webView.isVisible());
        resized();
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

    cb.nudgeTranspose = [this](int delta) {
        const int t = m_state.setTranspose(m_state.transpose() + delta);
        m_styleEngine.setGlobalTranspose(t);                 // transpose affects style (unchanged)
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
    cb.setFingeredOnBass = [this](bool on) {
        m_midi.setChordDetectionMode(on ? arranger::ChordDetectionMode::FingeredOnBass
                                        : arranger::ChordDetectionMode::Fingered);
        m_state.setChordSourceEnabled("bass", on);
        saveSettings();
    };
    cb.selectSection = [this](const std::string& id) {
        if (m_songModeActive) setSongMode(false);   // manual section control overrides song mode
        m_styleEngine.allNotesOff();
        m_styleEngine.setSection(id);
        applyMixerState();   // re-assert mixer over the section's own channel-volume setup
        if (m_panel) m_panel->setActiveSection(juce::String(id));
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
                             m_state.chordSourceEnabled("bass"));
    if (m_settings) {
        const auto& st = m_settings->state();
        m_panel->setEqGains(st.eqLowDb, st.eqMidDb, st.eqHighDb);
        m_panel->setCompAmount(st.compAmount);
    }
    resized();
}

void MainComponent::updateNativePanelStyle()
{
    if (!m_panel) return;

    auto style = m_styleEngine.currentStyle();
    if (!style) {
        m_panel->setStyleName({});
        m_panel->setSections({});
        return;
    }

    m_panel->setStyleName(juce::String(style->name.empty() ? style->id : style->name));
    m_panel->setBpm(m_state.bpm());   // reflect the style's tempo (set in loadAndApplyStyleFile)

    std::vector<std::pair<std::string, std::string>> pairs;
    for (const auto& b : cadenza::arranger::sectionButtonsForStyle(*style))
        pairs.emplace_back(b.sectionId, b.label);
    m_panel->setSections(pairs);
    m_panel->setActiveSection(juce::String(m_styleEngine.currentSection()));

    // --- Pick a live-melody channel the style does NOT use, so the right-hand
    //     voice never collides with (or overrides the program of) a style part. ---
    bool used[17] = { false };
    for (const auto& section : style->sections)
        for (const auto& part : section.parts)
            if (part.midiChannel >= 1 && part.midiChannel <= 16)
                used[part.midiChannel] = true;
    int melodyChannel = m_midi.melodyChannel();
    for (int c = 1; c <= 16; ++c) {
        if (c == 10) continue;            // never the GM drum channel
        if (!used[c]) { melodyChannel = c; break; }
    }
    if (m_midi.melodyChannel() != melodyChannel) {
        juce::Logger::writeToLog("[Cadenza] live melody channel moved to " + juce::String(melodyChannel)
                                 + " to avoid style channels");
        m_midi.setMelodyChannel(melodyChannel);
    }
    applyMelodyProgram();   // assert melody program on the (possibly new) channel

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
    if (m_settings)
        applyStyleMix(m_settings->state().lastStyleId);

    m_panel->setMixerChannels(labels);
    for (int ch : channels) {
        m_panel->updateMixerStrip(ch, m_mixer.volume(ch), m_mixer.mute(ch), m_mixer.solo(ch));
        const juce::String insName = (ch == 10)
            ? juce::String("Drum Kit")
            : juce::String(cadenza::midi::gmInstrumentName(m_mixer.program(ch)));
        m_panel->setMixerInstrumentName(ch, insName);
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
        mix.push_back(m);
    }
    m_settings->state().styleMixes[styleId] = std::move(mix);
    saveSettings();
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
