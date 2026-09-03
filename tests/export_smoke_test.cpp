// Headless smoke test (no GUI): builds a tiny timeline from a real media
// file and runs the actual Exporter/ffmpeg pipeline, then sanity-checks
// the produced file with ffprobe. Not a unit test framework — just enough
// automated verification that the core engine (probe -> decode -> filter
// graph -> encode) actually works end to end, since that's the riskiest
// part of this codebase to get subtly wrong.
//
// Usage: HyggshiCutExportSmokeTest <input_media_path> <output_mp4_path>

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
    std::cout << "Probed OK: " << asset->width << "x" << asset->height
              << " @ " << asset->frameRate << "fps, duration="
              << ticksToSeconds(asset->duration) << "s, hasAudio="
              << asset->hasAudio() << "\n";

    project.timeline().frameRate = 30.0;
    project.timeline().videoWidth = 480;
    project.timeline().videoHeight = 270;

    // Two video clips back to back (tests concat + trim), one full-length
    // audio clip (tests amix + adelay), with a 0.5s gap before the second
    // video clip (tests the black-fill gap path in the filter graph).
    auto& videoTrack = project.timeline().addTrack(TrackType::Visual, "V1");
    Clip clipA;
    clipA.assetId = asset->id;
    clipA.sourceIn = 0;
    clipA.sourceOut = secondsToTicks(2.0);
    clipA.timelineStart = 0;
    videoTrack.addClip(clipA);

    Clip clipB;
    clipB.assetId = asset->id;
    clipB.sourceIn = secondsToTicks(2.0);
    clipB.sourceOut = secondsToTicks(4.0);
    clipB.timelineStart = secondsToTicks(2.5); // 0.5s gap -> exercises the black-fill path
    videoTrack.addClip(clipB);

    if (asset->hasAudio()) {
        auto& audioTrack = project.timeline().addTrack(TrackType::Audio, "A1");
        Clip audioClip;
        audioClip.assetId = asset->id;
        audioClip.sourceIn = 0;
        audioClip.sourceOut = secondsToTicks(4.0);
        audioClip.timelineStart = 0;
        audioClip.volume = 0.8;
        audioTrack.addClip(audioClip);
    }

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
    QObject::connect(&exporter, &Exporter::progress, [](double frac, QString) {
        std::cout << "progress: " << static_cast<int>(frac * 100) << "%\n";
    });
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

    // Verify the output with ffprobe: duration should be close to the
    // timeline's total duration (4.5s: 2s + 0.5s gap + 2s).
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
