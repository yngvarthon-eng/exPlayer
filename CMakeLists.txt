# ==============================================================================
# exPlayer - audio, tracker-module and video player built with JUCE.
#
# Quick start (Linux):
#     cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#     cmake --build build -j"$(nproc)"
#     ./build/exPlayer_artefacts/exPlayer
#
# Feature overview of this file:
#   * JUCE and the CLAP SDK are located locally when present, otherwise
#     downloaded with FetchContent (no manual git clone step).
#   * Optional decoders/plugin hosts (libopenmpt, libxmp, lilv, FluidSynth,
#     libcurl) are auto-detected through pkg-config and can be switched off
#     individually, so a build never silently depends on a package it found by
#     accident.
#   * All sources are declared once: the application, the test executable and
#     the optional dependencies are configured from shared lists, so the two
#     targets can no longer drift apart.
# ==============================================================================

cmake_minimum_required(VERSION 3.22)

project(exPlayer
    VERSION 1.3.0
    DESCRIPTION "Modern audio, tracker module and video player built with JUCE"
    LANGUAGES C CXX)

# ------------------------------------------------------------------------------
# Language / global settings
# ------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json for clangd/IDE tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Single-config generators (make/ninja) default to an empty build type, which
# means "-O0 without debug info". Pick Release so a plain `cmake -B build` is
# usable, while still allowing -DCMAKE_BUILD_TYPE=Debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

# JUCE's helper tools want the platform thread library with pthread flags.
set(THREADS_PREFER_PTHREAD_FLAG ON)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------

option(EXPLAYER_BUILD_TESTS
    "Build the exPlayer feature test executable" ON)
option(EXPLAYER_INSTALL_DESKTOP_FILES
    "Install the .desktop launcher and MIME/icon files (Linux)" ON)
option(EXPLAYER_WARNINGS_AS_ERRORS
    "Treat compiler warnings as errors" OFF)

option(EXPLAYER_ENABLE_OPENMPT
    "Use libopenmpt for tracker module playback when available" ON)
option(EXPLAYER_ENABLE_XMP
    "Use libxmp as a fallback tracker module decoder when available" ON)
option(EXPLAYER_ENABLE_LV2
    "Use lilv to host LV2 plugins when available" ON)
option(EXPLAYER_ENABLE_FLUIDSYNTH
    "Use FluidSynth for SoundFont (.sf2) instrument playback when available" ON)

set(EXPLAYER_JUCE_VERSION "7.0.9" CACHE STRING "JUCE version to build against")
set(EXPLAYER_CLAP_VERSION "1.2.1" CACHE STRING "CLAP SDK version to download")
set(EXPLAYER_CLAP_SDK_DIR "" CACHE PATH
    "Local CLAP SDK checkout (containing include/clap/clap.h). Leave empty to download it.")

# ------------------------------------------------------------------------------
# JUCE
# ------------------------------------------------------------------------------

# Keep JUCE's own extras/examples out of this build.
set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS OFF CACHE BOOL "" FORCE)

include(FetchContent)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "Using the JUCE checkout at ${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    add_subdirectory(JUCE)
else()
    message(STATUS "JUCE not found locally, downloading ${EXPLAYER_JUCE_VERSION}")
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${EXPLAYER_JUCE_VERSION}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# CLAP SDK (header-only; only the headers are needed)
# ------------------------------------------------------------------------------

set(EXPLAYER_CLAP_SDK_DIR "${EXPLAYER_CLAP_SDK_DIR}" )
set(_explayer_clap_include_dir "")

if(EXPLAYER_CLAP_SDK_DIR AND EXISTS "${EXPLAYER_CLAP_SDK_DIR}/include/clap/clap.h")
    set(_explayer_clap_include_dir "${EXPLAYER_CLAP_SDK_DIR}/include")
    message(STATUS "Using the CLAP SDK at ${EXPLAYER_CLAP_SDK_DIR}")
else()
    if(EXPLAYER_CLAP_SDK_DIR)
        message(WARNING
            "EXPLAYER_CLAP_SDK_DIR is set to '${EXPLAYER_CLAP_SDK_DIR}' but does not "
            "contain include/clap/clap.h - downloading CLAP instead")
    endif()

    FetchContent_Declare(clap
        GIT_REPOSITORY https://github.com/free-audio/clap.git
        GIT_TAG        ${EXPLAYER_CLAP_VERSION}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(clap)

    set(_explayer_clap_include_dir "${clap_SOURCE_DIR}/include")
endif()

# Wrapped in our own interface target so the rest of the build never depends on
# a target name defined by the CLAP project itself.
add_library(exPlayerClapHeaders INTERFACE)
target_include_directories(exPlayerClapHeaders SYSTEM INTERFACE
    "${_explayer_clap_include_dir}")
# ------------------------------------------------------------------------------
# Optional dependencies
#
# Each optional feature is looked up once through pkg-config (not once per
# target) and linked as an imported target, which also brings along the
# transitive link flags pkg-config reports.
# ------------------------------------------------------------------------------

find_package(PkgConfig QUIET)

if(PkgConfig_FOUND)
    if(EXPLAYER_ENABLE_OPENMPT)
        pkg_check_modules(OPENMPT IMPORTED_TARGET libopenmpt)
    endif()
    if(EXPLAYER_ENABLE_XMP)
        pkg_check_modules(XMP IMPORTED_TARGET libxmp)
    endif()
    if(EXPLAYER_ENABLE_LV2)
        pkg_check_modules(LILV IMPORTED_TARGET lilv-0)
    endif()
    if(EXPLAYER_ENABLE_FLUIDSYNTH)
        pkg_check_modules(FLUIDSYNTH IMPORTED_TARGET fluidsynth)
    endif()

    # JUCE uses libcurl for juce::URL / WebInputStream (JUCE_USE_CURL defaults
    # to 1 on Linux), so this one is not optional - only optional in the sense
    # that a missing package is reported instead of a linker error.
    pkg_check_modules(CURL IMPORTED_TARGET libcurl)
else()
    message(WARNING
        "pkg-config was not found: building without libcurl, module decoders, "
        "LV2 hosting and SoundFont support")
endif()

function(explayer_report_dependency label option_name found)
    if(NOT ${option_name})
        message(STATUS "  ${label}: disabled by -D${option_name}=OFF")
    elseif(${found})
        message(STATUS "  ${label}: enabled")
    else()
        message(STATUS "  ${label}: not found, feature disabled")
    endif()
endfunction()

message(STATUS "Optional features:")
explayer_report_dependency("libopenmpt (module playback)"
    EXPLAYER_ENABLE_OPENMPT      OPENMPT_FOUND)
explayer_report_dependency("libxmp (module fallback)"
    EXPLAYER_ENABLE_XMP          XMP_FOUND)
explayer_report_dependency("lilv (LV2 plugin hosting)"
    EXPLAYER_ENABLE_LV2          LILV_FOUND)
explayer_report_dependency("FluidSynth (SoundFont playback)"
    EXPLAYER_ENABLE_FLUIDSYNTH   FLUIDSYNTH_FOUND)

# juce_core compiles its WebInputStream against libcurl (JUCE_USE_CURL is 1 by
# default), so this one is required rather than optional - report it as such
# instead of hiding a missing package behind a linker error later on.
if(CURL_FOUND)
    message(STATUS "Required: libcurl (network streams): found")
else()
    message(WARNING
        "libcurl was not found. juce_core uses curl for juce::URL, so the link step "
        "will fail. Install libcurl(-dev) or set JUCE_USE_CURL=0 in "
        "EXPLAYER_COMPILE_DEFINITIONS if the app does not need network streams.")
endif()

# Applies every detected optional dependency, plus the CLAP headers and the
# platform dlopen/dlfcn library, to a target. Both executables are configured
# through this function so that they can never end up with different features.
#
# Pass NO_CURL for targets that already link libcurl through JUCE itself (the
# GUI app does, via juce_add_gui_app(... NEEDS_CURL TRUE)).
function(explayer_apply_optional_dependencies target)
    cmake_parse_arguments(SP "NO_CURL" "" "" ${ARGN})

    if(OPENMPT_FOUND)
        target_link_libraries(${target} PRIVATE PkgConfig::OPENMPT)
        target_compile_definitions(${target} PRIVATE EXPLAYER_OPENMPT_SUPPORT=1)
    endif()

    if(XMP_FOUND)
        target_link_libraries(${target} PRIVATE PkgConfig::XMP)
        target_compile_definitions(${target} PRIVATE EXPLAYER_XMP_SUPPORT=1)
    endif()

    if(LILV_FOUND)
        target_link_libraries(${target} PRIVATE PkgConfig::LILV)
        target_compile_definitions(${target} PRIVATE EXPLAYER_LV2_SUPPORT=1)
    endif()

    if(FLUIDSYNTH_FOUND)
        target_link_libraries(${target} PRIVATE PkgConfig::FLUIDSYNTH)
        target_compile_definitions(${target} PRIVATE EXPLAYER_FLUIDSYNTH_SUPPORT=1)
    endif()

    if(CURL_FOUND AND NOT SP_NO_CURL)
        target_link_libraries(${target} PRIVATE PkgConfig::CURL)
    endif()

    target_link_libraries(${target} PRIVATE
        exPlayerClapHeaders   # CLAP SDK headers for ClapPluginFormat
        ${CMAKE_DL_LIBS})         # dlopen() for CLAP/LV2/VST3 discovery
endfunction()

find_package(Threads REQUIRED)

# ------------------------------------------------------------------------------
# Sources
#
# EXPLAYER_CORE_SOURCES are the parts both executables need (audio engine,
# playlist, effects, plugin hosting, exporters). EXPLAYER_APP_SOURCES add
# the GUI application on top. Headers are listed next to their implementation
# so IDEs show the whole unit.
# ------------------------------------------------------------------------------

set(EXPLAYER_CORE_SOURCES
    Source/Util/AppDataDirectory.h
    Source/Util/AppDataDirectory.cpp
    Source/Audio/AudioEngine.h
    Source/Audio/AudioEngine.cpp
    Source/Audio/FfmpegAudioFormatReader.h
    Source/Audio/FfmpegAudioFormatReader.cpp
    Source/Audio/FfmpegVideoAudioFormatReader.h
    Source/Audio/FfmpegVideoAudioFormatReader.cpp
    Source/Audio/FormatManager.h
    Source/Audio/FormatManager.cpp
    Source/Audio/MidiInputSettings.h
    Source/Audio/MidiInputSettings.cpp
    Source/Audio/ModPlugFormat.h
    Source/Audio/ModPlugFormat.cpp
    Source/Audio/MultiChannelAudioFormatReaderSource.h
    Source/Audio/MultiChannelAudioFormatReaderSource.cpp
    Source/Effects/Equalizer.h
    Source/Effects/Equalizer.cpp
    Source/Export/FfmpegProcess.h
    Source/Export/ScreenRecorder.h
    Source/Export/ScreenRecorder.cpp
    Source/Export/VideoExporter.h
    Source/Export/VideoExporter.cpp
    Source/Export/PlaylistVideoExporter.h
    Source/Export/PlaylistVideoExporter.cpp
    Source/Playlist/PlaylistItem.h
    Source/Playlist/PlaylistManager.h
    Source/Playlist/PlaylistManager.cpp
    Source/Playlist/AudioFileCollector.h
    Source/Playlist/AudioFileCollector.cpp
    Source/Recorder/MicClip.h
    Source/Recorder/MicClip.cpp
    Source/Recorder/MicRecorder.h
    Source/Recorder/MicRecorder.cpp
    Source/Recorder/ClipPlaybackSource.h
    Source/Recorder/ClipPlaybackSource.cpp
    Source/Recorder/ClipEffects.h
    Source/Recorder/ClipEffects.cpp
    Source/Plugins/ClapPluginFormat.h
    Source/Plugins/ClapPluginFormat.cpp
    Source/Plugins/Lv2PluginFormat.h
    Source/Plugins/Lv2PluginFormat.cpp
    Source/Plugins/PluginChain.h
    Source/Plugins/PluginChain.cpp
    Source/Plugins/PluginChainPersistence.h
    Source/Plugins/PluginChainPersistence.cpp
    Source/Plugins/PluginChainPresetManager.h
    Source/Plugins/PluginChainPresetManager.cpp
    Source/Plugins/PluginHostManager.h
    Source/Plugins/PluginHostManager.cpp
    Source/Plugins/SidechainReceiver.h
    Source/UI/MainLayout.h
    Source/UI/MainLayout.cpp
    Source/UI/ModulePatternViewer.h
    Source/UI/ModulePatternViewer.cpp
    Source/Video/VideoFileProbe.h
    Source/Video/VideoFileProbe.cpp
    Source/Video/FfmpegVideoFrameDecoder.h
    Source/Video/FfmpegVideoFrameDecoder.cpp
    Source/Video/SubtitleTrack.h
    Source/Video/SubtitleTrack.cpp
    Source/Visualization/ModulePatternData.h
    Source/Visualization/ModulePatternData.cpp
    Source/Visualization/SpectrumAnalyzer.h
    Source/Visualization/SpectrumAnalyzer.cpp
    Source/Visualization/VisualizationPlugin.h
    Source/Visualization/Plugins/WaveformPlugin.h
    Source/Visualization/Plugins/WaveformPlugin.cpp
    Source/Xtp/XtpInstrumentVoice.h
    Source/Xtp/XtpInstrumentVoice.cpp
    Source/Xtp/XtpSequencer.h
    Source/Xtp/XtpSequencer.cpp
    Source/Xtp/XtpSequencerSource.h
    Source/Xtp/XtpSequencerSource.cpp
    Source/Xtp/XtpSf2Voice.h
    Source/Xtp/XtpSf2Voice.cpp
    Source/Xtp/XtpSong.h
    Source/Xtp/XtpSong.cpp
    Source/Xtp/XtpTransportClock.h
)

set(EXPLAYER_APP_SOURCES
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/Analysis/AudioAnalyzer.h
    Source/Analysis/AudioAnalyzer.cpp
    Source/Effects/EqPresetManager.h
    Source/Effects/EqPresetManager.cpp
    Source/Effects/EqualizerComponent.h
    Source/Effects/EqualizerComponent.cpp
    Source/Export/PlaylistVideoExportController.h
    Source/Export/PlaylistVideoExportController.cpp
    Source/Util/AppLogger.h
    Source/Util/AppLogger.cpp
    Source/Export/VideoExportController.h
    Source/Export/VideoExportController.cpp
    Source/UI/AudioAnalysisComponent.h
    Source/UI/AudioAnalysisComponent.cpp
    Source/UI/AudioQualitySelector.h
    Source/UI/AudioQualitySelector.cpp
    Source/UI/FullScreenHostWindow.h
    Source/UI/ReservesTopRightCorner.h
    Source/UI/FullScreenHostWindow.cpp
    Source/UI/FullScreenVisualizerWindow.h
    Source/UI/FullScreenVisualizerWindow.cpp
    Source/UI/FullScreenWindowController.h
    Source/UI/FullScreenWindowController.cpp
    Source/UI/SequencerSidePanel.h
    Source/UI/SequencerSidePanel.cpp
    Source/UI/ChannelActivityComponent.h
    Source/UI/ChannelActivityComponent.cpp
    Source/UI/PianoRollComponent.h
    Source/UI/PianoRollComponent.cpp
    Source/UI/TrackInfoComponent.h
    Source/UI/TrackInfoComponent.cpp
    Source/UI/SequencerPanelStyle.h
    Source/UI/InstrumentNamesComponent.h
    Source/UI/InstrumentNamesComponent.cpp
    Source/UI/SongMessageComponent.h
    Source/UI/SongMessageComponent.cpp
    Source/UI/ChannelScopesComponent.h
    Source/UI/ChannelScopesComponent.cpp
    Source/UI/FullScreenTransportBar.h
    Source/UI/FullScreenTransportBar.cpp
    Source/UI/MainToolbar.h
    Source/UI/MainToolbar.cpp
    Source/UI/MidiInputMenu.h
    Source/UI/MidiInputMenu.cpp
    Source/UI/MicRecorderComponent.h
    Source/UI/MicRecorderComponent.cpp
    Source/UI/WaveformEditorComponent.h
    Source/UI/WaveformEditorComponent.cpp
    Source/UI/PlayerControlsComponent.h
    Source/UI/PlayerControlsComponent.cpp
    Source/UI/DriveAwareFileBrowser.h
    Source/UI/DriveAwareFileBrowser.cpp
    Source/UI/PlaylistEditorComponent.h
    Source/UI/PlaylistEditorComponent.cpp
    Source/UI/PluginChainComponent.h
    Source/UI/PluginChainComponent.cpp
    Source/UI/TransportShortcuts.h
    Source/UI/VideoExportSettingsDialog.h
    Source/UI/VideoExportSettingsDialog.cpp
    Source/UI/VisualizationComponent.h
    Source/UI/VisualizationComponent.cpp
    Source/UI/VideoPreviewComponent.h
    Source/UI/VideoPreviewComponent.cpp
    Source/Visualization/Plugins/AlbumArtPulsePlugin.h
    Source/Visualization/Plugins/AlbumArtPulsePlugin.cpp
    Source/Visualization/Plugins/BarsPeakHoldPlugin.h
    Source/Visualization/Plugins/BarsPeakHoldPlugin.cpp
    Source/Visualization/Plugins/LissajousPlugin.h
    Source/Visualization/Plugins/LissajousPlugin.cpp
    Source/Visualization/Plugins/ParticleBloomPlugin.h
    Source/Visualization/Plugins/ParticleBloomPlugin.cpp
    Source/Visualization/Plugins/RadialSpectrumPlugin.h
    Source/Visualization/Plugins/RadialSpectrumPlugin.cpp
    Source/Visualization/Plugins/SpectrogramPlugin.h
    Source/Visualization/Plugins/SpectrogramPlugin.cpp
    Source/Visualization/Plugins/SpectrumBarsPlugin.h
    Source/Visualization/Plugins/SpectrumBarsPlugin.cpp
    Source/Visualization/Plugins/StarfieldTunnelPlugin.h
    Source/Visualization/Plugins/StarfieldTunnelPlugin.cpp
    Source/Visualization/Plugins/VUMeterPlugin.h
    Source/Visualization/Plugins/VUMeterPlugin.cpp
)

set(EXPLAYER_TEST_SOURCES
    Source/Tests/FeatureTests.cpp
)
# ------------------------------------------------------------------------------
# Compiler warnings
# ------------------------------------------------------------------------------

# Applied to our own targets only (JUCE module sources are compiled as part of
# each target and would otherwise drown the output in upstream warnings).
function(explayer_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
        if(EXPLAYER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(EXPLAYER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# ------------------------------------------------------------------------------
# Application
# ------------------------------------------------------------------------------

juce_add_gui_app(exPlayer
    PRODUCT_NAME  "exPlayer"
    COMPANY_NAME  "Exodus"
    BUNDLE_ID     "com.exodus.explayer"
    VERSION       ${PROJECT_VERSION}   # drives JUCE_APPLICATION_VERSION_STRING
    NEEDS_CURL    TRUE)                # juce::URL needs libcurl on Linux

target_sources(exPlayer PRIVATE
    ${EXPLAYER_CORE_SOURCES}
    ${EXPLAYER_APP_SOURCES})

# JUCE_PLUGINHOST_* select which plugin formats the host can scan for. The same
# list is used for the test executable, so a format that works in the app is
# also exercised by the tests.
set(EXPLAYER_COMPILE_DEFINITIONS
    JUCE_WEB_BROWSER=0        # avoids a hard GTK/WebKit dependency on Linux
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LADSPA=0)

target_compile_definitions(exPlayer PRIVATE
    ${EXPLAYER_COMPILE_DEFINITIONS}
    # getApplicationName()/getApplicationVersion() read these, so the strings
    # shown in "About" and the window title come from project() above.
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:exPlayer,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:exPlayer,JUCE_VERSION>"
    JUCE_COMPANY_NAME_STRING="$<TARGET_PROPERTY:exPlayer,JUCE_COMPANY_NAME>")

target_link_libraries(exPlayer PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils       # AudioDeviceSelectorComponent and friends
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra)        # CodeEditor/WebBrowser module headers

explayer_apply_optional_dependencies(exPlayer NO_CURL)
explayer_enable_warnings(exPlayer)

target_link_libraries(exPlayer PRIVATE Threads::Threads)
# ------------------------------------------------------------------------------
# Feature test executable
#
# Shares EXPLAYER_CORE_SOURCES and the optional dependency setup with the
# application above, so it always tests the same code and the same features.
# ------------------------------------------------------------------------------

if(EXPLAYER_BUILD_TESTS)
    add_executable(exPlayerTests
        ${EXPLAYER_CORE_SOURCES}
        ${EXPLAYER_TEST_SOURCES})

    target_compile_definitions(exPlayerTests PRIVATE
        ${EXPLAYER_COMPILE_DEFINITIONS})

    target_link_libraries(exPlayerTests PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics)

    explayer_apply_optional_dependencies(exPlayerTests)
    explayer_enable_warnings(exPlayerTests)

    target_link_libraries(exPlayerTests PRIVATE Threads::Threads)

    # Also expose the suite to CTest ("ctest --test-dir build"), which keeps
    # the single test binary usable on its own as well.
    include(CTest)

    if(BUILD_TESTING)
        add_test(NAME exPlayer.Features COMMAND exPlayerTests)
        set_tests_properties(exPlayer.Features PROPERTIES TIMEOUT 600)
    endif()
endif()

# ------------------------------------------------------------------------------
# Installation
# ------------------------------------------------------------------------------

include(GNUInstallDirs)

# JUCE registers install rules of its own for the JUCE CMake package (config
# files plus module headers) and those land in the default component. The rules
# below are split into components so that the application alone can be
# installed without dragging the JUCE SDK into the prefix:
#
#     cmake --install build --component Runtime --component DesktopIntegration
#
install(TARGETS exPlayer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT Runtime)

if(UNIX AND NOT APPLE AND EXPLAYER_INSTALL_DESKTOP_FILES)
    install(FILES packaging/explayer.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
        COMPONENT DesktopIntegration)

    install(FILES packaging/explayer.svg
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
        COMPONENT DesktopIntegration)

    install(FILES packaging/explayer-mime.xml
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/mime/packages
        RENAME explayer.xml
        COMPONENT DesktopIntegration)

    # packaging/explayer.desktop launches "explayer", so provide that
    # name as a symlink next to the real binary (the desktop file predates the
    # CMake install rules).
    #
    # CMAKE_INSTALL_PREFIX is expanded at install time so that
    # "cmake --install ... --prefix <dir>" is honoured, and the symlink is only
    # created when the binary itself was installed - component installs can be
    # run in any order or on their own.
    install(CODE "
        set(explayer_bin_dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
        set(explayer_link \"\${explayer_bin_dir}/explayer\")
        if(EXISTS \"\${explayer_bin_dir}/exPlayer\")
            message(STATUS \"Creating launcher symlink: \${explayer_link}\")
            file(REMOVE \"\${explayer_link}\")
            execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink
                exPlayer \"\${explayer_link}\")
        else()
            message(STATUS
                \"Skipping launcher symlink: install the Runtime component first\")
        endif()
    " COMPONENT DesktopIntegration)
endif()

# ------------------------------------------------------------------------------
# Sanity check
#
# Adding a file under Source/ without listing it above is a silent no-op - the
# file simply never gets compiled. Warn about it at configure time instead.
# ------------------------------------------------------------------------------

file(GLOB_RECURSE _explayer_source_files
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h")

set(_explayer_listed_files
    ${EXPLAYER_CORE_SOURCES}
    ${EXPLAYER_APP_SOURCES}
    ${EXPLAYER_TEST_SOURCES})

foreach(_explayer_file IN LISTS _explayer_source_files)
    if(NOT _explayer_file IN_LIST _explayer_listed_files)
        message(WARNING
            "${_explayer_file} is not listed in the source lists in CMakeLists.txt "
            "and will not be compiled")
    endif()
endforeach()
