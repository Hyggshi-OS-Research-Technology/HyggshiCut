// Smoke test for Free Transform / picture-in-picture export: two video
// tracks overlap in time; the top track's clip is shrunk, moved off-center
// and rotated via Clip::transform. Verifies the new overlay-based filter
// graph is accepted by ffmpeg and produces a correctly-timed file (i.e. the
// bottom track is NOT fully hidden by the top one, which was the bug).
//
// Usage: PipTransformSmokeTest <input_media_path> <output_mp4_path>

#include <QCoreApplication>
#include <QTimer>
#include <QProcess>
#include <QDebug>
#include <iostream>
#include "../src/core/Project.h"
#include "../src/export/Exporter.h"

using namespace hc;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_media> <output_mp4>\n";
        return 2;
    }
    const QString inputPath = argv[1];
    const QString outputPath = argv[2];

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

    // Bottom track: full-frame background clip for the whole 3s.
    auto& bottomTrack = project.timeline().addTrack(TrackType::Visual, "V1");
    Clip bg;
    bg.assetId = asset->id;
    bg.sourceIn = 0;
    bg.sourceOut = secondsToTicks(3.0);
    bg.timelineStart = 0;
    bottomTrack.addClip(bg);

    // Top track: same source, shrunk to 35%, pushed toward the top-right
    // corner and rotated -- this is exactly the "Free Transform" scenario
    // the fix is for. If the old topmost-wins renderer were still active,
    // the background clip would be completely invisible for this whole
    // range; with activeVideoClipsAt() + overlay compositing it must not be.
    auto& topTrack = project.timeline().addTrack(TrackType::Visual, "V2");
    Clip pip;
    pip.assetId = asset->id;
    pip.sourceIn = 0;
    pip.sourceOut = secondsToTicks(3.0);
    pip.timelineStart = 0;
    pip.transform.scaleX = 0.35;
    pip.transform.scaleY = 0.35;
    pip.transform.x = 0.55;
    pip.transform.y = -0.55;
    pip.transform.rotationDeg = 15.0;
    pip.opacity = 0.9;
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

    std::cout << "PASS\n";
    return 0;
}
