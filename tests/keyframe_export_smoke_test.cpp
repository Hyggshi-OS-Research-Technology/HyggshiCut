// Headless smoke test for layer *keyframe* animation end to end: a PIP clip
// on a top track is keyframed from the top-left corner to the bottom-right
// corner over 3s, with a background clip underneath for the whole timeline.
// Verifies:
//   1) Clip::transformAt() interpolates correctly in isolation.
//   2) The Exporter/ffmpeg pipeline accepts the extra keyframe-driven
//      segment boundaries and produces a correctly-timed file.
//   3) The exported frame at t=0 differs from the exported frame at
//      t=3s (i.e. the PIP layer actually moved -- not just a single
//      static transform baked in for the whole export).
//
// Usage: KeyframeExportSmokeTest <input_media_path> <output_mp4_path>

#include <QCoreApplication>
#include <QTimer>
#include <QProcess>
#include <QImage>
#include <QDebug>
#include <QDir>
#include <iostream>
#include "../src/core/Project.h"
#include "../src/export/Exporter.h"

using namespace hc;

namespace {

bool checkTransformAtInIsolation() {
    Clip c;
    c.timelineStart = secondsToTicks(1.0);
    c.sourceOut = secondsToTicks(5.0); // 4s long at speed 1.0

    // No keyframes yet: transformAt() must equal the static transform,
    // exactly like before keyframes existed.
    c.transform.scaleX = 0.4;
    c.transform.scaleY = 0.4;
    if (c.transformAt(secondsToTicks(2.0)).scaleX != 0.4) {
        std::cerr << "FAIL: transformAt() without keyframes should fall back to static transform\n";
        return false;
    }

    // Two keyframes: at clip-relative t=0 top-left, at t=3s bottom-right.
    Transform start;
    start.x = -0.7; start.y = -0.7; start.scaleX = 0.3; start.scaleY = 0.3; start.rotationDeg = 0.0;
    Transform end;
    end.x = 0.7; end.y = 0.7; end.scaleX = 0.3; end.scaleY = 0.3; end.rotationDeg = 45.0;
    c.setTransformKeyframe(0, start);
    c.setTransformKeyframe(secondsToTicks(3.0), end);

    if (!c.hasTransformKeyframes()) {
        std::cerr << "FAIL: hasTransformKeyframes() should be true after setTransformKeyframe()\n";
        return false;
    }

    // At the clip's own start (relative t=0) -> exactly `start`.
    const Transform atStart = c.transformAt(c.timelineStart);
    if (std::abs(atStart.x - start.x) > 1e-6 || std::abs(atStart.scaleX - start.scaleX) > 1e-6) {
        std::cerr << "FAIL: transformAt(clip start) should equal first keyframe\n";
        return false;
    }

    // Halfway (relative t=1.5s) -> halfway between start and end.
    const Transform atMid = c.transformAt(c.timelineStart + secondsToTicks(1.5));
    const double expectedMidX = (start.x + end.x) / 2.0;
    const double expectedMidRot = (start.rotationDeg + end.rotationDeg) / 2.0;
    if (std::abs(atMid.x - expectedMidX) > 1e-3 || std::abs(atMid.rotationDeg - expectedMidRot) > 1e-3) {
        std::cerr << "FAIL: transformAt(midpoint) should linearly interpolate x/rotation. Got x="
                  << atMid.x << " rot=" << atMid.rotationDeg << "\n";
        return false;
    }

    // Past the last keyframe (relative t=3.5s, clip is 4s long) -> holds `end`.
    const Transform atAfter = c.transformAt(c.timelineStart + secondsToTicks(3.5));
    if (std::abs(atAfter.x - end.x) > 1e-6) {
        std::cerr << "FAIL: transformAt() past last keyframe should hold its value\n";
        return false;
    }

    // Remove-near / has-near round trip.
    if (!c.hasTransformKeyframeNear(0)) {
        std::cerr << "FAIL: hasTransformKeyframeNear(0) should find the keyframe at relative t=0\n";
        return false;
    }
    if (!c.removeTransformKeyframeNear(0)) {
        std::cerr << "FAIL: removeTransformKeyframeNear(0) should remove the keyframe at relative t=0\n";
        return false;
    }
    if (c.transformKeyframes.size() != 1) {
        std::cerr << "FAIL: exactly one keyframe should remain after removal\n";
        return false;
    }

    std::cout << "PASS: Clip::transformAt() interpolation checks\n";
    return true;
}

// Samples the brightest pixel in a region near the top-left corner of a PNG
// frame -- used to distinguish "PIP layer visible up here" (bright/colourful
// content somewhere in the region) from "PIP layer NOT up here, just black
// background" (everything near-black). Uses max rather than mean because a
// scaled-down PIP thumbnail is letterboxed/pillarboxed (force_original_aspect_ratio
// keeps its own aspect ratio inside the box), so a small fixed sample patch
// can land on the thumbnail's own black padding even while the thumbnail
// itself is genuinely on-screen a few pixels away.
int cornerBrightness(const QString& pngPath) {
    QImage img(pngPath);
    if (img.isNull()) return -1;
    int maxVal = 0, n = 0;
    for (int y = 0; y < std::min(60, img.height()); ++y) {
        for (int x = 0; x < std::min(60, img.width()); ++x) {
            const QColor c = img.pixelColor(x, y);
            maxVal = std::max(maxVal, c.red() + c.green() + c.blue());
            ++n;
        }
    }
    return n > 0 ? maxVal : -1;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_media> <output_mp4>\n";
        return 2;
    }
    const QString inputPath = argv[1];
    const QString outputPath = argv[2];

    if (!checkTransformAtInIsolation()) return 1;

    Project project;
    QString err;
    auto asset = project.importMedia(inputPath, &err);
    if (!asset) {
        std::cerr << "FAIL: probe failed: " << err.toStdString() << "\n";
        return 1;
    }

    project.timeline().frameRate = 30.0;
    project.timeline().videoWidth = 480;
    project.timeline().videoHeight = 270;

    // Background: full-frame for the whole 3s.
    auto& bottomTrack = project.timeline().addTrack(TrackType::Visual, "V1");
    Clip bg;
    bg.assetId = asset->id;
    bg.sourceIn = 0;
    bg.sourceOut = secondsToTicks(3.0);
    bg.timelineStart = 0;
    bottomTrack.addClip(bg);

    // PIP layer: keyframed from the top-left corner (t=0) to the
    // bottom-right corner (t=3s) -- exercises the export-time keyframe
    // sampling/interpolation path, not just the static Clip::transform.
    auto& topTrack = project.timeline().addTrack(TrackType::Visual, "V2");
    Clip pip;
    pip.assetId = asset->id;
    pip.sourceIn = 0;
    pip.sourceOut = secondsToTicks(3.0);
    pip.timelineStart = 0;

    Transform kfStart;
    kfStart.scaleX = 0.3; kfStart.scaleY = 0.3;
    kfStart.x = -0.65; kfStart.y = -0.65;
    Transform kfEnd;
    kfEnd.scaleX = 0.3; kfEnd.scaleY = 0.3;
    kfEnd.x = 0.65; kfEnd.y = 0.65;
    pip.setTransformKeyframe(0, kfStart);
    pip.setTransformKeyframe(secondsToTicks(3.0), kfEnd);
    topTrack.addClip(pip);

    std::cout << "Timeline duration: " << ticksToSeconds(project.timeline().totalDuration()) << "s\n";

    Exporter exporter(&project);
    Exporter::Settings settings;
    settings.outputPath = outputPath;
    settings.width = project.timeline().videoWidth;
    settings.height = project.timeline().videoHeight;
    settings.frameRate = project.timeline().frameRate;
    settings.videoBitrateKbps = 2000;
    settings.audioBitrateKbps = 128;

    bool ok = false;
    QString message;
    QObject::connect(&exporter, &Exporter::finished, [&](bool success, QString msg) {
        ok = success;
        message = msg;
        QCoreApplication::quit();
    });

    exporter.start(settings);
    QTimer::singleShot(30000, &app, []() {
        std::cerr << "FAIL: export timed out\n";
        QCoreApplication::exit(3);
    });
    const int rc = app.exec();
    if (rc != 0) return rc;

    if (!ok) {
        std::cerr << "FAIL: export reported failure: " << message.toStdString() << "\n";
        return 1;
    }
    std::cout << "Export finished: " << message.toStdString() << "\n";

    QProcess probe;
    probe.start("ffprobe", {"-v", "error", "-show_entries", "format=duration",
                             "-of", "default=nk=1:nw=1", outputPath});
    probe.waitForFinished(10000);
    const double outDuration = probe.readAllStandardOutput().trimmed().toDouble();
    const double expected = ticksToSeconds(project.timeline().totalDuration());
    std::cout << "Output duration: " << outDuration << "s (expected ~" << expected << "s)\n";
    if (std::abs(outDuration - expected) > 0.5) {
        std::cerr << "FAIL: output duration off by more than 0.5s\n";
        return 1;
    }

    // Extract frames near t=0 (PIP should be top-left, corner bright) and
    // t=2.9s (PIP should be bottom-right, corner back to background/black)
    // to prove the layer actually animated across the export, not just a
    // single static transform baked in for the whole clip.
    const QString frame0 = outputPath + ".frame0.png";
    const QString frame3 = outputPath + ".frame3.png";
    QProcess::execute("ffmpeg", {"-y", "-v", "error", "-ss", "0.05", "-i", outputPath,
                                  "-frames:v", "1", frame0});
    QProcess::execute("ffmpeg", {"-y", "-v", "error", "-ss", "2.9", "-i", outputPath,
                                  "-frames:v", "1", frame3});

    const int b0 = cornerBrightness(frame0);
    const int b3 = cornerBrightness(frame3);
    std::cout << "Top-left corner brightness: t=0 -> " << b0 << ", t=2.9s -> " << b3 << "\n";
    if (b0 < 0 || b3 < 0) {
        std::cerr << "FAIL: could not read extracted frames\n";
        return 1;
    }
    if (std::abs(b0 - b3) < 5) {
        std::cerr << "FAIL: top-left corner looks identical at t=0 and t=2.9s -- "
                     "keyframe animation does not appear to have been exported\n";
        return 1;
    }

    QFile::remove(frame0);
    QFile::remove(frame3);

    std::cout << "PASS\n";
    return 0;
}
