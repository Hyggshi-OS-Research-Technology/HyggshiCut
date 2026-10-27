#include "../src/core/Project.h"

#include <QCoreApplication>
#include <iostream>
#include <utility>

using namespace hc;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) {
        std::cerr << "Usage: CreateTestProject <input-video> <output-project>\n";
        return 2;
    }

    Project project;
    QString error;
    const auto asset = project.importMedia(QString::fromLocal8Bit(argv[1]), &error);
    if (!asset) {
        std::cerr << "Could not import fixture video: " << error.toStdString() << '\n';
        return 1;
    }

    Timeline& timeline = project.timeline();
    timeline.frameRate = 30.0;
    timeline.videoWidth = 640;
    timeline.videoHeight = 360;
    Track& videoTrack = timeline.addTrack(TrackType::Visual, QStringLiteral("Video 1"));
    Clip clip;
    clip.assetId = asset->id;
    clip.type = ClipType::Video;
    clip.sourceIn = 0;
    clip.sourceOut = asset->duration;
    clip.timelineStart = 0;
    videoTrack.addClip(std::move(clip));

    if (!project.saveToFile(QString::fromLocal8Bit(argv[2]), &error)) {
        std::cerr << "Could not save fixture project: " << error.toStdString() << '\n';
        return 1;
    }
    return 0;
}
