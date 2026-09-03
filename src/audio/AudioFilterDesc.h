#pragma once
#include <QString>
#include <QStringList>
#include <cmath>
#include <algorithm>
#include "../core/Clip.h"

namespace hc {

// Turns a clip's AudioFilterSettings into an ffmpeg audio-filter chain
// description (the comma-separated part you'd put between two labels in a
// -filter_complex graph, e.g. "equalizer=f=100:...,afftdn=nr=12"). Returns
// an empty string when every setting is at its default (no-op) — callers
// should skip inserting the filter stage entirely in that case rather than
// pass an empty string to avfilter_graph_parse2/ffmpeg, which errors on it.
//
// This is the ONE place that knows how our simplified "EQ / denoise /
// compressor" knobs map to real ffmpeg filters. Both Exporter (ffmpeg CLI,
// -filter_complex) and AudioFilterChain (libavfilter graph for real-time
// preview) call this exact function, so preview always matches export.
inline QString buildAudioFilterDescription(const Clip::AudioFilterSettings& f) {
    if (f.isDefault()) return QString();

    QStringList stages;

    // 3-band EQ: only emit a stage for bands the user actually touched, to
    // keep the graph (and the CPU cost of running it) as small as possible.
    // width_type=o + width=2 (2 octaves) gives a gentle, musical shelf/bell
    // rather than a narrow surgical notch — appropriate for "basic" EQ.
    if (std::abs(f.eqLowDb) > 0.01) {
        stages << QString("equalizer=f=100:width_type=o:width=2:g=%1").arg(f.eqLowDb, 0, 'f', 2);
    }
    if (std::abs(f.eqMidDb) > 0.01) {
        stages << QString("equalizer=f=1000:width_type=o:width=1:g=%1").arg(f.eqMidDb, 0, 'f', 2);
    }
    if (std::abs(f.eqHighDb) > 0.01) {
        stages << QString("equalizer=f=8000:width_type=o:width=2:g=%1").arg(f.eqHighDb, 0, 'f', 2);
    }

    if (f.denoiseEnabled) {
        const double nr = std::clamp(f.denoiseAmountDb, 0.01, 97.0);
        stages << QString("afftdn=nr=%1").arg(nr, 0, 'f', 2);
    }

    if (f.compressorEnabled) {
        // ffmpeg acompressor's threshold is LINEAR amplitude (0..1], not dB —
        // convert from the dB value the UI shows.
        const double thresholdLinear = std::clamp(std::pow(10.0, f.compressorThresholdDb / 20.0), 0.001, 1.0);
        const double ratio = std::clamp(f.compressorRatio, 1.0, 20.0);
        stages << QString("acompressor=threshold=%1:ratio=%2")
                      .arg(thresholdLinear, 0, 'f', 4).arg(ratio, 0, 'f', 2);
    }

    return stages.join(",");
}

} // namespace hc
