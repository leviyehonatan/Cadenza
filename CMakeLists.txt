cmake_minimum_required(VERSION 3.22)

project(Cadenza VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CADENZA_BUILD_TESTS "Build Cadenza unit tests" ON)

# ------------------------------------------------------------
# Optional FluidSynth. If not found, AudioEngine uses
# NullSynthEngine (logs notes, emits no sound). The build always works.
# ------------------------------------------------------------
find_package(FluidSynth QUIET)
if(NOT FluidSynth_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FLUIDSYNTH QUIET IMPORTED_TARGET fluidsynth)
    endif()
endif()

if(FluidSynth_FOUND OR FLUIDSYNTH_FOUND)
    message(STATUS "Cadenza: FluidSynth available - real synth enabled")
    set(CADENZA_HAVE_FLUIDSYNTH ON)
else()
    message(STATUS "Cadenza: FluidSynth NOT found - using NullSynthEngine fallback")
    set(CADENZA_HAVE_FLUIDSYNTH OFF)
endif()

# ============================================================
# cadenza_core : pure C++ library, no JUCE, no FluidSynth.
# ============================================================
add_library(cadenza_core STATIC
    Source/ApplicationState.cpp
    Source/BridgeRouter.cpp
    Source/Json/Json.cpp
    Source/Midi/ChordRecognizer.cpp
    Source/Midi/ArrangerMidiRouter.cpp
    Source/Midi/LiveMelodyVoice.cpp
    Source/Midi/GmInstruments.cpp
    Source/Arranger/Style.cpp
    Source/Arranger/StyleLoader.cpp
    Source/Arranger/StyParser.cpp
    Source/Arranger/PatternTransposer.cpp
    Source/Arranger/RuntimePlayback.cpp
    Source/Arranger/PlaybackDiagnostics.cpp
    Source/Arranger/Song.cpp
    Source/Arranger/SongLoader.cpp
    Source/Arranger/SongPlayer.cpp
    Source/Arranger/SectionButtons.cpp
    Source/Audio/MidiChannel.cpp
    Source/Audio/MixerModel.cpp
    Source/Audio/Transport.cpp
    Source/Settings/SettingsStore.cpp
)

target_include_directories(cadenza_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

# ============================================================
# Tests
# ============================================================
if(CADENZA_BUILD_TESTS)
    enable_testing()

    function(cadenza_add_test name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE cadenza_core)
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    cadenza_add_test(cadenza_core_tests        tests/BridgeRouterTests.cpp)
    cadenza_add_test(cadenza_json_tests        tests/JsonTests.cpp)
    cadenza_add_test(cadenza_chord_tests       tests/ChordRecognizerTests.cpp)
    cadenza_add_test(cadenza_style_tests       tests/StyleLoaderTests.cpp)
    cadenza_add_test(cadenza_transpose_tests   tests/PatternTransposerTests.cpp)
    cadenza_add_test(cadenza_transport_tests   tests/TransportTests.cpp)
    cadenza_add_test(cadenza_audioblock_tests  tests/AudioBlockPipelineTests.cpp)
    cadenza_add_test(cadenza_runtime_tests     tests/RuntimePlaybackTests.cpp)
    cadenza_add_test(cadenza_octave_tests      tests/OctaveRoutingTests.cpp)
    cadenza_add_test(cadenza_livemelody_tests  tests/LiveMelodyVoiceTests.cpp)
    cadenza_add_test(cadenza_sections_tests     tests/SectionButtonsTests.cpp)
    cadenza_add_test(cadenza_mixer_tests        tests/MixerModelTests.cpp)
    cadenza_add_test(cadenza_gm_tests           tests/GmInstrumentsTests.cpp)
    cadenza_add_test(cadenza_settings_tests    tests/SettingsStoreTests.cpp)
    cadenza_add_test(cadenza_song_tests        tests/SongLoaderTests.cpp)
    cadenza_add_test(cadenza_songplayer_tests  tests/SongPlayerTests.cpp)
    cadenza_add_test(cadenza_arranger_router_tests tests/ArrangerMidiRouterTests.cpp)
endif()

# ============================================================
# tools/sty-converter — standalone CLI that converts Yamaha .sty
# (or plain .mid) files into Cadenza's .cstyle JSON format.
# Pure C++, depends only on cadenza_core.
# ============================================================
add_executable(sty-to-cstyle
    tools/sty-converter/main.cpp
)
target_link_libraries(sty-to-cstyle PRIVATE cadenza_core)

add_executable(style-probe
    tools/style-probe/main.cpp
)
target_link_libraries(style-probe PRIVATE cadenza_core)

add_executable(style-scan
    tools/style-scan/main.cpp
)
target_link_libraries(style-scan PRIVATE cadenza_core)
if(MSVC)
    # /EHa lets _set_se_translator turn native faults (access violations in the
    # parser on a malformed .sty) into catchable C++ exceptions so the batch
    # scan survives a bad file instead of aborting the whole run.
    target_compile_options(style-scan PRIVATE /EHa)
endif()

if(CADENZA_BUILD_TESTS)
    add_test(
        NAME style_probe_shared_setup_owner
        COMMAND style-probe
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/style-probe-shared-drums.cstyle"
            C
            mainA
            1
    )
    set_tests_properties(style_probe_shared_setup_owner PROPERTIES
        PASS_REGULAR_EXPRESSION "channel 10 setup owner: drums bankMsb=120 program=8"
        FAIL_REGULAR_EXPRESSION "channel 10 setup owner: rhythm2"
    )

    add_test(
        NAME style_probe_stable_note_names
        COMMAND style-probe
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/style-probe-note-range.cstyle"
            C
            mainA
            2
    )
    set_tests_properties(style_probe_stable_note_names PROPERTIES
        PASS_REGULAR_EXPRESSION "played range A1\\(33\\)..E3\\(52\\)"
    )

    add_test(
        NAME style_probe_diagnostics_none
        COMMAND style-probe
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/style-probe-note-range.cstyle"
            C
            mainA
            1
    )
    set_tests_properties(style_probe_diagnostics_none PROPERTIES
        PASS_REGULAR_EXPRESSION "Diagnostics: none"
    )

    add_test(
        NAME style_probe_diagnostics_one_warning
        COMMAND style-probe
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/style-probe-one-warning.cstyle"
            C
            mainA
            1
    )
    set_tests_properties(style_probe_diagnostics_one_warning PROPERTIES
        PASS_REGULAR_EXPRESSION "Diagnostics: 1 warning"
    )

    add_test(
        NAME style_probe_diagnostics_plural_warnings
        COMMAND style-probe
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/style-probe-two-warnings.cstyle"
            C
            mainA
            1
    )
    set_tests_properties(style_probe_diagnostics_plural_warnings PROPERTIES
        PASS_REGULAR_EXPRESSION "Diagnostics: 2 warnings"
    )

    add_executable(cadenza_sty_tests
        tools/sty-converter/StyParserTests.cpp
    )
    target_link_libraries(cadenza_sty_tests PRIVATE cadenza_core)
    add_test(NAME cadenza_sty_tests COMMAND cadenza_sty_tests)
endif()

# ============================================================
# Native JUCE GUI app (built only if lib/JUCE is checked out).
# ============================================================
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/JUCE/CMakeLists.txt")
    add_subdirectory(lib/JUCE)

    # ----- plugin-probe: headless CLI to verify VST3 hosting works -----
    juce_add_console_app(plugin-probe PRODUCT_NAME "plugin-probe")
    target_sources(plugin-probe PRIVATE tools/plugin-probe/main.cpp)
    target_compile_definitions(plugin-probe PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(plugin-probe PRIVATE
        juce::juce_audio_processors_headless
        juce::juce_recommended_config_flags)

    juce_add_gui_app(Cadenza
        PRODUCT_NAME "Cadenza Workstation"
        COMPANY_NAME "Cadenza"
        NEEDS_WEBVIEW2 TRUE
    )

    target_sources(Cadenza
        PRIVATE
            Source/Main.cpp
            Source/MainComponent.cpp
            Source/Audio/SynthEngine.cpp
            Source/Audio/Metronome.cpp
            Source/Audio/AudioEngine.cpp
            Source/Audio/MasterEq.cpp
            Source/Audio/PluginHost.cpp
            Source/Midi/MidiRouter.cpp
            Source/Arranger/StyleEngine.cpp
            Source/UI/NativePanel.cpp
    )

    target_compile_definitions(Cadenza
        PRIVATE
            JUCE_WEB_BROWSER=1
            JUCE_USE_WIN_WEBVIEW2=1
            JUCE_PLUGINHOST_VST3=1
    )

    if(CADENZA_HAVE_FLUIDSYNTH)
        target_compile_definitions(Cadenza PRIVATE CADENZA_HAVE_FLUIDSYNTH=1)
        if(FluidSynth_FOUND)
            target_link_libraries(Cadenza PRIVATE FluidSynth::libfluidsynth)
        elseif(FLUIDSYNTH_FOUND)
            target_link_libraries(Cadenza PRIVATE PkgConfig::FLUIDSYNTH)
        endif()
    endif()

    target_link_libraries(Cadenza
        PRIVATE
            cadenza_core
            juce::juce_audio_utils
            juce::juce_audio_devices
            juce::juce_audio_processors_headless
            juce::juce_gui_extra
    )

    add_custom_command(TARGET Cadenza POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:Cadenza>/resources"
        COMMENT "Copying Cadenza resources next to the executable"
    )
else()
    message(WARNING "lib/JUCE was not found, so the native host target is skipped. Clone JUCE into lib/JUCE to build Cadenza.exe.")
endif()
