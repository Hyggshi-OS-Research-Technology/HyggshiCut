#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include "../core/TimeTypes.h"

namespace hc {

// A decoded video frame, always normalized to planar YUV420P so the rest of
// the pipeline (GL upload, exporter) never has to deal with the long tail of
// pixel formats FFmpeg can hand back (NV12, 10-bit, etc). The conversion
// happens once in Decoder via swscale.
struct VideoFrame {
    int width = 0;
    int height = 0;
    Ticks pts = 0; // presentation time, in ticks, relative to source start

    // Container/display-matrix orientation. FFmpeg normally decodes the
    // coded pixels without applying this metadata; the GL compositor applies
    // it at render time so the source is shown upright without a CPU rotate.
    // Positive/negative values follow FFmpeg's av_display_rotation_get().
    double displayRotationDeg = 0.0;

    // Which YUV->RGB matrix this frame's Y/U/V bytes were encoded with:
    // false = BT.601, true = BT.709. Decoder sets this from the source
    // stream's tagged colorspace, falling back to a resolution heuristic
    // (SD -> 601, HD -> 709) when the source doesn't tag it — the same
    // fallback ffmpeg itself applies when converting YUV to RGB in the
    // Exporter's filter graph. GLVideoWidget must use the matching matrix
    // per layer or Preview and Export show visibly different colors for
    // sources that aren't 601 (i.e. most HD footage).
    bool colorMatrixBt709 = false;

    // One buffer per plane (Y, U, V), each with its own row stride, since
    // chroma planes are half-resolution in both dimensions for 4:2:0.
    std::vector<uint8_t> y, u, v;
    int strideY = 0, strideU = 0, strideV = 0;

    bool isValid() const { return width > 0 && height > 0 && !y.empty(); }
};

// A decoded, resampled chunk of interleaved 16-bit signed PCM audio,
// always normalized to stereo (or mono passthrough) at the project's
// output sample rate so PlaybackController and the AudioSink stay simple.
struct AudioFrame {
    Ticks pts = 0;
    int sampleRate = 0;
    int channels = 0;
    std::vector<int16_t> samples; // interleaved

    bool isValid() const { return sampleRate > 0 && channels > 0 && !samples.empty(); }
};

} // namespace hc
