#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <iostream>
#include <cassert>
#include "../src/core/Project.h"
#include "../src/decode/Decoder.h"

using namespace hc;

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_media.mp4>\n";
        return 1;
    }

    const QString mediaPath = argv[1];

    // 1. Test MediaAsset probe
    QString err;
    auto asset = MediaAsset::probe(mediaPath, &err);
    if (!asset) {
        std::cerr << "FAIL: probe failed: " << err.toStdString() << "\n";
        return 1;
    }
    std::cout << "Test 1: MediaAsset probe OK (hasAudio=" << asset->hasAudio() << ", sampleRate=" << asset->sampleRate << ")\n";

    // 2. Test Decoder audio decoding
    Decoder decoder;
    if (!decoder.open(mediaPath, &err)) {
        std::cerr << "FAIL: decoder open failed: " << err.toStdString() << "\n";
        return 1;
    }
    auto audioFrame = decoder.decodeNextAudioFrame(48000, 2);
    if (!audioFrame || !audioFrame->isValid()) {
        std::cerr << "FAIL: decodeNextAudioFrame failed\n";
        return 1;
    }
    std::cout << "Test 2: Decoder audio decode OK (" << audioFrame->samples.size() << " samples, " << audioFrame->sampleRate << "Hz)\n";

    // 3. Test Project saveToFile with .hcproj
    Project p;
    p.name = "MyAwesomeProject";
    p.importMedia(mediaPath);
    auto& vTrack = p.timeline().addTrack(TrackType::Visual, "Visual 1");
    Clip c1;
    c1.assetId = p.assets().front()->id;
    c1.sourceIn = 0;
    c1.sourceOut = secondsToTicks(3.0);
    c1.timelineStart = 0;
    vTrack.addClip(c1);

    // Use the system temp directory instead of a hardcoded developer-machine
    // path so this test runs on any machine/CI, not just the original author's.
    const QString testProjPath = QDir(QDir::tempPath()).filePath("hyggshicut_test_project.hcproj");
    if (!p.saveToFile(testProjPath, &err)) {
        std::cerr << "FAIL: project saveToFile failed: " << err.toStdString() << "\n";
        return 1;
    }
    std::cout << "Test 3: Project saveToFile OK -> " << testProjPath.toStdString() << "\n";

    // 4. Test Project loadFromFile
    Project p2;
    if (!p2.loadFromFile(testProjPath, &err)) {
        std::cerr << "FAIL: project loadFromFile failed: " << err.toStdString() << "\n";
        return 1;
    }
    if (p2.name != "MyAwesomeProject" || p2.assets().size() != 1 || p2.timeline().tracks().size() != 1) {
        std::cerr << "FAIL: project data mismatch on load\n";
        return 1;
    }
    std::cout << "Test 4: Project loadFromFile OK (Name: " << p2.name.toStdString() << ", tracks: " << p2.timeline().tracks().size() << ")\n";

    std::cout << "\nALL AUDIO AND PROJECT TESTS PASSED!\n";
    return 0;
}
