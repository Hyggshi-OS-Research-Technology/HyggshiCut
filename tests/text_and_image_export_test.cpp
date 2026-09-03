#include <QGuiApplication>
#include <QTimer>
#include <QProcess>
#include <QDebug>
#include <QDir>
#include <iostream>
#include <cassert>
#include "../src/core/Project.h"
#include "../src/export/Exporter.h"

using namespace hc;

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <input_video> <input_image> <output_mp4>\n";
        return 2;
    }
    const QString videoPath = argv[1];
    const QString imagePath = argv[2];
    const QString outputPath = argv[3];

    Project project;
    QString err;

    // 1. Probe video
    auto videoAsset = project.importMedia(videoPath, &err);
    if (!videoAsset || videoAsset->kind != MediaKind::Video) {
        std::cerr << "FAIL: video probe failed or wrong kind\n";
        return 1;
    }

    // 2. Probe image
    auto imageAsset = project.importMedia(imagePath, &err);
    if (!imageAsset || imageAsset->kind != MediaKind::Image) {
        std::cerr << "FAIL: image probe failed or not recognized as MediaKind::Image\n";
        return 1;
    }
    std::cout << "Image probed OK: kind=Image, dur=" << ticksToSeconds(imageAsset->duration) << "s\n";

    // 3. Test undo / redo track signaling
    int tracksChangedCount = 0;
    QObject::connect(&project.timeline(), &Timeline::tracksChanged, [&]() {
        tracksChangedCount++;
    });

    project.timeline().frameRate = 30.0;
    project.timeline().videoWidth = 480;
    project.timeline().videoHeight = 270;

    auto& vTrack = project.timeline().addTrack(TrackType::Visual, "Visual 1");
    // Track 1: Video clip 0..2s
    Clip clipVideo;
    clipVideo.assetId = videoAsset->id;
    clipVideo.type = ClipType::Video;
    clipVideo.sourceIn = 0;
    clipVideo.sourceOut = secondsToTicks(2.0);
    clipVideo.timelineStart = 0;
    vTrack.addClip(clipVideo);

    // Track 1: Image clip 2..4s
    Clip clipImg;
    clipImg.assetId = imageAsset->id;
    clipImg.type = ClipType::Image;
    clipImg.sourceIn = 0;
    clipImg.sourceOut = secondsToTicks(2.0);
    clipImg.timelineStart = secondsToTicks(2.0);
    vTrack.addClip(clipImg);

    // Track 2: Text overlay on top of video from 0.5s to 2.5s
    auto& textTrack = project.timeline().addTrack(TrackType::Visual, "Text Layer");
    Clip clipText;
    clipText.type = ClipType::Text;
    clipText.displayLabel = "Hello HyggshiCut Text Overlay";
    clipText.sourceIn = 0;
    clipText.sourceOut = secondsToTicks(2.0);
    clipText.timelineStart = secondsToTicks(0.5);
    textTrack.addClip(clipText);

    // Test Undo/Redo
    project.pushUndoSnapshot();
    project.timeline().addTrack(TrackType::Audio, "Dummy Track");
    assert(project.timeline().tracks().size() == 3);
    const int countBefore = tracksChangedCount;
    project.undo();
    assert(project.timeline().tracks().size() == 2);
    assert(tracksChangedCount > countBefore);
    std::cout << "Undo/Redo tracksChanged signal OK!\n";

    // Export test
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

    // Verify output file duration with ffprobe
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

    std::cout << "PASS ALL TEXT AND IMAGE TESTS\n";
    return 0;
}
