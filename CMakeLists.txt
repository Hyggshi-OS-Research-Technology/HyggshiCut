cmake_minimum_required(VERSION 3.20)
project(HyggshiCut VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
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

target_compile_options(HyggshiCut PRIVATE -Wall -Wextra -Wno-unused-parameter)

install(TARGETS HyggshiCut RUNTIME DESTINATION bin)

# --- Optional headless smoke test (core engine only, no GUI/OpenGL) ---
option(HYGGSHICUT_BUILD_TESTS "Build the headless export smoke test" OFF)
if(HYGGSHICUT_BUILD_TESTS)
    add_executable(HyggshiCutExportSmokeTest
        tests/export_smoke_test.cpp
        src/core/Timeline.h
        src/core/MediaAsset.cpp
        src/core/Project.cpp
        src/decode/Decoder.cpp
        src/render/TextureCache.cpp
        src/render/TextRenderer.cpp
        src/export/Exporter.cpp
    )
    target_include_directories(HyggshiCutExportSmokeTest PRIVATE src src/core src/decode src/render src/export)
    target_link_libraries(HyggshiCutExportSmokeTest PRIVATE Qt6::Core Qt6::Gui OpenGL::GL PkgConfig::FFMPEG)

    add_executable(HyggshiCutAudioProjectTest
        tests/audio_and_project_test.cpp
        src/core/Timeline.h
        src/core/MediaAsset.cpp
        src/core/Project.cpp
        src/decode/Decoder.cpp
    )
    target_include_directories(HyggshiCutAudioProjectTest PRIVATE src src/core src/decode)
    target_link_libraries(HyggshiCutAudioProjectTest PRIVATE Qt6::Core Qt6::Gui PkgConfig::FFMPEG)

    add_executable(PipTransformSmokeTest
        tests/pip_transform_smoke_test.cpp
        src/core/Timeline.h
        src/core/MediaAsset.cpp
        src/core/Project.cpp
        src/decode/Decoder.cpp
        src/render/TextureCache.cpp
        src/render/TextRenderer.cpp
        src/export/Exporter.cpp
    )
    target_include_directories(PipTransformSmokeTest PRIVATE src src/core src/decode src/render src/export)
    target_link_libraries(PipTransformSmokeTest PRIVATE Qt6::Core Qt6::Gui OpenGL::GL PkgConfig::FFMPEG)

    add_executable(KeyframeExportSmokeTest
        tests/keyframe_export_smoke_test.cpp
        src/core/Timeline.h
        src/core/MediaAsset.cpp
        src/core/Project.cpp
        src/decode/Decoder.cpp
        src/render/TextureCache.cpp
        src/render/TextRenderer.cpp
        src/export/Exporter.cpp
    )
    target_include_directories(KeyframeExportSmokeTest PRIVATE src src/core src/decode src/render src/export)
    target_link_libraries(KeyframeExportSmokeTest PRIVATE Qt6::Core Qt6::Gui OpenGL::GL PkgConfig::FFMPEG)

    add_executable(TextAndImageExportTest
        tests/text_and_image_export_test.cpp
        src/core/Timeline.h
        src/core/MediaAsset.cpp
        src/core/Project.cpp
        src/decode/Decoder.cpp
        src/render/TextureCache.cpp
        src/render/TextRenderer.cpp
        src/export/Exporter.cpp
    )
    target_include_directories(TextAndImageExportTest PRIVATE src src/core src/decode src/render src/export)
    target_link_libraries(TextAndImageExportTest PRIVATE Qt6::Core Qt6::Gui OpenGL::GL PkgConfig::FFMPEG)

    # Offline test: no real media/ffmpeg process needed, just inspects the
    # filter_complex string buildFfmpegArgs() produces (see segment_bound_test.cpp).
    add_executable(SegmentBoundTest
        tests/segment_bound_test.cpp
        src/core/Timeline.h
        src/core/MediaAsset.cpp
        src/core/Project.cpp
        src/decode/Decoder.cpp
        src/render/TextureCache.cpp
        src/render/TextRenderer.cpp
        src/export/Exporter.cpp
    )
    target_include_directories(SegmentBoundTest PRIVATE src src/core src/decode src/render src/export)
    target_link_libraries(SegmentBoundTest PRIVATE Qt6::Core Qt6::Gui OpenGL::GL PkgConfig::FFMPEG)

    add_executable(MixedEffectsExportTest
        tests/mixed_effects_export_test.cpp
        src/core/Timeline.h
        src/core/MediaAsset.cpp
        src/core/Project.cpp
        src/decode/Decoder.cpp
        src/render/TextureCache.cpp
        src/render/TextRenderer.cpp
        src/export/Exporter.cpp
    )
    target_include_directories(MixedEffectsExportTest PRIVATE src src/core src/decode src/render src/export)
    target_link_libraries(MixedEffectsExportTest PRIVATE Qt6::Core Qt6::Gui OpenGL::GL PkgConfig::FFMPEG)

    add_executable(UserProjectExportTest
        tests/user_project_export_test.cpp
        src/core/Timeline.h
        src/core/MediaAsset.cpp
        src/core/Project.cpp
        src/decode/Decoder.cpp
        src/render/TextureCache.cpp
        src/render/TextRenderer.cpp
        src/export/Exporter.cpp
    )
    target_include_directories(UserProjectExportTest PRIVATE src src/core src/decode src/render src/export)
    target_link_libraries(UserProjectExportTest PRIVATE Qt6::Core Qt6::Gui OpenGL::GL PkgConfig::FFMPEG)

    add_executable(TextCacheAndCpuFallbackTest
        tests/text_cache_and_cpu_fallback_test.cpp
        src/core/Timeline.h
        src/core/MediaAsset.cpp
        src/core/Project.cpp
        src/decode/Decoder.cpp
        src/render/TextureCache.cpp
        src/render/TextRenderer.cpp
        src/render/GLVideoWidget.cpp
        src/export/Exporter.cpp
    )
    target_include_directories(TextCacheAndCpuFallbackTest PRIVATE src src/core src/decode src/render src/export)
    target_link_libraries(TextCacheAndCpuFallbackTest PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets OpenGL::GL PkgConfig::FFMPEG)

    add_executable(ScreenRecorderTest
        tests/screen_recorder_test.cpp
        src/capture/ScreenRecorder.cpp
    )
    target_include_directories(ScreenRecorderTest PRIVATE src src/capture)
    target_link_libraries(ScreenRecorderTest PRIVATE Qt6::Core Qt6::DBus)
endif()

