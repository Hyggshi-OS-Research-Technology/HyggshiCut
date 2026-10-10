cmake_minimum_required(VERSION 3.20)
project(HyggshiCut VERSION 1.0.0 LANGUAGES CXX)

# Target C++23 where the toolchain supports it, falling back to C++20.
#
# Not a blind bump: C++23 is a compiler-level switch here, while the *library*
# side varies a lot by toolchain. GCC 12, for example, accepts -std=c++23 and
# ships <expected> but has neither std::format nor std::print. Any use of a
# C++23 library facility therefore has to be guarded with the corresponding
# __cpp_lib_* feature macro (see src/core/CxxFeatures.h) rather than assumed
# from the standard version alone.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-std=c++23" HC_HAS_CXX23)
if(HC_HAS_CXX23)
    set(CMAKE_CXX_STANDARD 23)
else()
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
message(STATUS "HyggshiCut: building as C++${CMAKE_CXX_STANDARD}")
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets DBus)
find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
    libavfilter
)

# Direct ALSA PCM output for realtime timeline-preview audio (see
# src/playback/AlsaAudioOutput.h). Deliberately NOT using Qt6::Multimedia
# here: its native-pipewire QAudioSink backend fails to enumerate any audio
# device at all on Qt 6.10.2 + PipeWire 1.6.2 (a bug in Qt's own SPA pod
# parser), so this bypasses it entirely and talks to ALSA directly instead.
pkg_check_modules(ALSA REQUIRED IMPORTED_TARGET alsa)

# libmpv: powers SourcePreviewDialog (real audio+video playback of a raw
# source file, via the mpv client API + render API). Independent of the
# libav* pipeline above, which is still what Decoder/Exporter use for
# timeline compositing and export.
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*.h
)

add_executable(HyggshiCut ${SOURCES})

target_include_directories(HyggshiCut PRIVATE
    src
    src/core
    src/decode
    src/render
    src/playback
    src/export
    src/ui
    src/cache
    src/capture
)

target_link_libraries(HyggshiCut PRIVATE
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::DBus
    OpenGL::GL
    PkgConfig::FFMPEG
    PkgConfig::MPV
    PkgConfig::ALSA
)

# Single source of truth for the version string reported by --version, so it
# cannot drift away from the project() version / debian/changelog again.
target_compile_definitions(HyggshiCut PRIVATE HYGGSHICUT_VERSION="${PROJECT_VERSION}")

target_compile_options(HyggshiCut PRIVATE -Wall -Wextra -Wno-unused-parameter)

install(TARGETS HyggshiCut RUNTIME DESTINATION bin)

# Bundled runtime assets. LanguageManager::discoverBundledLanguages() and
# main.cpp's loadBundledAssets() look for these next to the executable, in
# the current directory, and finally under the system data dir — so an
# installed build (including the .deb) has to ship them or the UI falls back
# to raw translation keys and loads no effect presets.
install(DIRECTORY languages/
        DESTINATION share/hyggshicut/languages
        FILES_MATCHING PATTERN "*.langhc")
install(DIRECTORY plugins/
        DESTINATION share/hyggshicut/plugins
        FILES_MATCHING PATTERN "*.plhc")

# --- Optional headless smoke tests ---
option(HYGGSHICUT_BUILD_TESTS "Build the headless export smoke test" OFF)
if(HYGGSHICUT_BUILD_TESTS)
    include(CTest)
    enable_testing()

    # The offline engine tests all compile the same slice of the core: the
    # timeline model, the decoder and the exporter's filter-graph builder.
    # They were ten near-identical 10-line blocks; a single list plus a helper
    # keeps them from drifting apart (three of them had already grown
    # different include-directory sets for no reason).
    set(HC_ENGINE_SOURCES
        src/core/Timeline.h
        src/core/MediaAsset.cpp
        src/core/Project.cpp
        src/decode/Decoder.cpp
        src/render/TextureCache.cpp
        src/render/TextRenderer.cpp
        src/export/Exporter.cpp
    )

    # add_engine_test(<target> <source> [EXTRA_SOURCES...])
    #
    # Registers the executable with CTest as well as building it, so `ctest`
    # actually runs the suite. Previously every test had to be named by hand
    # in CI and in the Dockerfile, which is why some of them were built but
    # never executed.
    function(add_engine_test target source)
        add_executable(${target} ${source} ${HC_ENGINE_SOURCES} ${ARGN})
        target_include_directories(${target} PRIVATE
            src src/core src/decode src/render src/export)
        target_link_libraries(${target} PRIVATE
            Qt6::Core Qt6::Gui OpenGL::GL PkgConfig::FFMPEG)
        add_test(NAME ${target} COMMAND ${target})
    endfunction()

    add_engine_test(HyggshiCutExportSmokeTest tests/export_smoke_test.cpp)
    add_engine_test(PipTransformSmokeTest     tests/pip_transform_smoke_test.cpp)
    add_engine_test(KeyframeExportSmokeTest   tests/keyframe_export_smoke_test.cpp)
    add_engine_test(TextAndImageExportTest    tests/text_and_image_export_test.cpp)
    add_engine_test(MixedEffectsExportTest    tests/mixed_effects_export_test.cpp)
    add_engine_test(UserProjectExportTest     tests/user_project_export_test.cpp)

    # Offline: inspects the filter_complex string buildFfmpegArgs() produces
    # (see segment_bound_test.cpp). Spawns no ffmpeg process.
    add_engine_test(SegmentBoundTest tests/segment_bound_test.cpp)

    # Autosave / crash recovery, plus the atomic-save guarantee in
    # Project::saveToFile(). Data-loss paths, so they are covered headlessly.
    add_engine_test(AutosaveTest tests/autosave_test.cpp src/core/Autosave.cpp)

    # Offline: Effect Layers (adjustment layers) - inheritance rules in the
    # timeline model, the .hcproj round-trip, and the filter graph built for
    # them (full-cover vs time-gated). Generates its own tiny PNG and SKIPs
    # the export checks if that cannot be probed.
    add_engine_test(EffectLayerTest tests/effect_layer_test.cpp)

    # Needs the widget stack too, for the CPU-fallback path in GLVideoWidget.
    add_executable(TextCacheAndCpuFallbackTest
        tests/text_cache_and_cpu_fallback_test.cpp
        ${HC_ENGINE_SOURCES}
        src/render/GLVideoWidget.cpp
    )
    target_include_directories(TextCacheAndCpuFallbackTest PRIVATE
        src src/core src/decode src/render src/export)
    target_link_libraries(TextCacheAndCpuFallbackTest PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets
        OpenGL::GL PkgConfig::FFMPEG)
    add_test(NAME TextCacheAndCpuFallbackTest COMMAND TextCacheAndCpuFallbackTest)

    # Does not use the exporter/render slice, so it is not an engine test.
    add_executable(HyggshiCutAudioProjectTest
        tests/audio_and_project_test.cpp
        src/core/Timeline.h
        src/core/MediaAsset.cpp
        src/core/Project.cpp
        src/decode/Decoder.cpp
    )
    target_include_directories(HyggshiCutAudioProjectTest PRIVATE src src/core src/decode)
    target_link_libraries(HyggshiCutAudioProjectTest PRIVATE
        Qt6::Core Qt6::Gui PkgConfig::FFMPEG)
    add_test(NAME HyggshiCutAudioProjectTest COMMAND HyggshiCutAudioProjectTest)

    # Runtime environment diagnostics (ffmpeg/codecs/audio/assets/proxy cache).
    # Needs no display: QCoreApplication only.
    add_executable(EnvironmentCheckTest
        tests/environment_check_test.cpp
        src/core/EnvironmentCheck.cpp
    )
    target_include_directories(EnvironmentCheckTest PRIVATE src src/core)
    target_link_libraries(EnvironmentCheckTest PRIVATE
        Qt6::Core PkgConfig::FFMPEG PkgConfig::ALSA)
    add_test(NAME EnvironmentCheckTest COMMAND EnvironmentCheckTest)

    add_executable(ScreenRecorderTest
        tests/screen_recorder_test.cpp
        src/capture/ScreenRecorder.cpp
    )
    target_include_directories(ScreenRecorderTest PRIVATE src src/capture)
    target_link_libraries(ScreenRecorderTest PRIVATE Qt6::Core Qt6::DBus)
    add_test(NAME ScreenRecorderTest COMMAND ScreenRecorderTest)
    set_tests_properties(ScreenRecorderTest PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    set(APP_LIB_SOURCES ${SOURCES})
    list(FILTER APP_LIB_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

    # add_gui_test(<target> <source>)
    #
    # Links the whole application (minus main.cpp) and therefore needs the
    # full Qt/mpv/ALSA set. These require a Qt platform plugin, so CTest runs
    # them with QT_QPA_PLATFORM=offscreen.
    function(add_gui_test target source)
        add_executable(${target} ${source} ${APP_LIB_SOURCES})
        target_include_directories(${target} PRIVATE
            src src/core src/decode src/render src/playback
            src/export src/ui src/cache src/capture)
        target_link_libraries(${target} PRIVATE
            Qt6::Widgets Qt6::OpenGLWidgets Qt6::DBus OpenGL::GL
            PkgConfig::FFMPEG PkgConfig::MPV PkgConfig::ALSA)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endfunction()

    # Regression test for the keyboard shortcut table: catches two actions
    # sharing a QKeySequence (which makes Qt fire neither) and editing
    # shortcuts that would shadow typing in a text field.
    add_gui_test(ShortcutTest tests/shortcut_test.cpp)
    add_gui_test(SettingsDialogTest tests/settings_dialog_test.cpp)
endif()

