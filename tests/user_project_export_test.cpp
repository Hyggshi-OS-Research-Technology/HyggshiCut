#include <QGuiApplication>
#include <QTimer>
#include <QProcess>
#include <QImage>
#include <QDebug>
#include <iostream>
#include "../src/core/Project.h"
#include "../src/export/Exporter.h"

using namespace hc;

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    Project project;
    QString err;
    if (!project.loadFromFile("build/user_project.hcproj", &err)) {
        std::cerr << "FAIL: Could not load user_project.hcproj: " << err.toStdString() << "\n";
        return 1;
    }

    std::cout << "Loaded user project successfully. Timeline duration: "
              << ticksToSeconds(project.timeline().totalDuration()) << "s\n";

    Exporter exporter(&project);
    Exporter::Settings settings;
    settings.outputPath = "build/user_project_out.mp4";
    settings.width = 1920;
    settings.height = 1080;
    settings.frameRate = 30.0;
    settings.videoBitrateKbps = 4000;
    settings.audioBitrateKbps = 192;

    QString filterGraph;
    exporter.buildFfmpegArgs(settings, &filterGraph);
    std::cout << "Filter graph:\n" << filterGraph.toStdString() << "\n";

    bool ok = false;
    QString message;
    QObject::connect(&exporter, &Exporter::progress, [](double frac, QString eta) {
        std::cout << "progress: " << static_cast<int>(frac * 100) << "% ETA: " << eta.toStdString() << "\n";
    });
    QObject::connect(&exporter, &Exporter::finished, [&](bool success, QString msg) {
        ok = success;
        message = msg;
        QCoreApplication::quit();
    });

    exporter.start(settings);
    QTimer::singleShot(120000, &app, []() {
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

    // Extract frame at t=7.0s and verify brightness
    QProcess::execute("ffmpeg", {"-y", "-ss", "7.0", "-i", settings.outputPath, "-frames:v", "1", "build/frame_7s.png"});
    QImage img("build/frame_7s.png");
    if (img.isNull()) {
        std::cerr << "FAIL: Could not load extracted frame at 7s\n";
        return 1;
    }
    long long totalBrightness = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            totalBrightness += c.red() + c.green() + c.blue();
        }
    }
    double avgBrightness = static_cast<double>(totalBrightness) / (img.width() * img.height() * 3.0);
    std::cout << "Average brightness at 7s: " << avgBrightness << " (out of 255)\n";
    if (avgBrightness < 5.0) {
        std::cerr << "FAIL: Frame at 7s is almost completely black!\n";
        return 1;
    }

    std::cout << "PASS: Frame at 7s is brightly rendered (avg brightness = " << avgBrightness << ")!\n";
    return 0;
}
