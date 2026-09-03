// Offline unit test (no real media, no ffmpeg process spawned) for the
// segment-count safety cap added to keyframeSampleBoundaries /
// transitionSampleBoundaries in Exporter.cpp. Before the cap, a long
// clip with transform keyframes spanning its whole duration generated
// one sample every 6 frames across the ENTIRE span with no upper bound
// -- a 10-minute animated PIP clip at 30fps produced ~3000 segments on
// its own, and that scales linearly with clip length. Long/animation-heavy
// timelines could blow past what ffmpeg can reliably parse/start.
//
// This test builds such a pathological timeline (no real video files
// needed -- buildFfmpegArgs() skips clips whose asset file doesn't
// exist, but still emits the segment/overlay scaffolding driven by
// keyframe sample boundaries) and asserts the resulting filter graph's
// segment count stays bounded instead of growing unbounded with clip
// duration.
//
// Usage: SegmentBoundTest (no arguments)

#include <QCoreApplication>
#include <QDebug>
#include <iostream>
#include "../src/core/Project.h"
#include "../src/export/Exporter.h"

using namespace hc;

namespace {

int countSegments(const QString& filterGraph) {
    // Each video segment produces exactly one "[vsegN]" output label from
    // the per-segment "format=yuv420p[vsegN]" filter (see Exporter.cpp).
    int count = 0;
    int pos = 0;
    while ((pos = filterGraph.indexOf("[vseg", pos)) != -1) {
        ++count;
        pos += 5;
    }
    return count;
}

bool checkLongKeyframedClipStaysBounded() {
    Project project;
    Timeline& tl = project.timeline();
    tl.frameRate = 30.0;
    tl.videoWidth = 1920;
    tl.videoHeight = 1080;

    Track& track = tl.addTrack(TrackType::Visual, "V1");
    Clip c;
    c.assetId = "missing-asset"; // fine: buildFfmpegArgs skips unresolvable assets, still builds the graph
    c.timelineStart = 0;
    c.sourceOut = secondsToTicks(600.0); // 10-minute clip

    // Keyframes across the whole clip -> old code sampled every 6 frames
    // (0.2s) for the full 600s span = ~3000 boundary points from this ONE
    // clip alone.
    Transform start;
    start.x = -0.8; start.scaleX = 0.3; start.scaleY = 0.3;
    Transform end;
    end.x = 0.8; end.scaleX = 0.3; end.scaleY = 0.3;
    c.setTransformKeyframe(0, start);
    c.setTransformKeyframe(secondsToTicks(600.0), end);
    track.addClip(c);

    Exporter exporter(&project);
    Exporter::Settings settings;
    settings.outputPath = "/tmp/segment_bound_test_unused.mp4";
    settings.width = 1920;
    settings.height = 1080;
    settings.frameRate = 30.0;

    QString filterGraph;
    const QStringList args = exporter.buildFfmpegArgs(settings, &filterGraph);
    const int segments = countSegments(filterGraph);

    // Regression check for export-duration clamping: the final ffmpeg output
    // must be capped to the timeline duration, otherwise segment PTS/frame
    // rounding can make a long or heavily segmented export noticeably longer
    // than the timeline shown by the editor.
    const int durationOpt = args.indexOf("-t");
    if (durationOpt < 0 || durationOpt + 1 >= args.size()) {
        std::cerr << "FAIL: exporter did not add final output duration (-t)\n";
        return false;
    }
    const double outputDuration = args.at(durationOpt + 1).toDouble();
    if (std::abs(outputDuration - 600.0) > 1e-6) {
        std::cerr << "FAIL: final output duration should be 600s, got "
                  << outputDuration << "s\n";
        return false;
    }

    // The single-pass compositor evaluates keyframes continuously via native expressions
    // without splitting the timeline into hundreds of discrete segments.
    if (segments > 0) {
        std::cerr << "FAIL: expected 0 segmented cuts (single-pass continuous graph), got " << segments << "\n";
        return false;
    }
    if (!filterGraph.contains("canvas_single") || !filterGraph.contains("[vout]")) {
        std::cerr << "FAIL: expected single-pass canvas in filter graph\n";
        return false;
    }

    std::cout << "PASS: 10-minute keyframed clip produced clean single-pass continuous filter graph\n";
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const bool ok = checkLongKeyframedClipStaysBounded();
    return ok ? 0 : 1;
}
