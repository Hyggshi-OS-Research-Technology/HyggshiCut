#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <iostream>
#include <cmath>
#include "../src/core/Project.h"
#include "../src/export/Exporter.h"

using namespace hc;

bool testMixedEffectsFilterGraph() {
    Project project;
    Timeline& tl = project.timeline();
    tl.frameRate = 30.0;
    tl.videoWidth = 1920;
    tl.videoHeight = 1080;

    QString err;
    auto asset = project.importMedia("build/test_sample.mp4", &err);
    if (!asset) {
        std::cerr << "FAIL: Could not import test_sample.mp4: " << err.toStdString() << "\n";
        return false;
    }

    Track& vtrack = tl.addTrack(TrackType::Visual, "V1");
    Clip clip;
    clip.assetId = asset->id;
    clip.timelineStart = 0;
    clip.sourceIn = 0;
    clip.sourceOut = secondsToTicks(3.0);
    clip.fadeInDuration = secondsToTicks(0.5);
    clip.fadeOutDuration = secondsToTicks(0.5);

    // 1. Crop Effect (Left 10%, Top 10%, Right 10%, Bottom 10%)
    Effect cropEff;
    cropEff.type = "crop";
    cropEff.enabled = true;
    cropEff.params.push_back({"left", 0.1});
    cropEff.params.push_back({"top", 0.1});
    cropEff.params.push_back({"right", 0.1});
    cropEff.params.push_back({"bottom", 0.1});
    clip.effects.push_back(cropEff);

    // 2. Brightness, Contrast, Saturation
    Effect bEff; bEff.type = "brightness"; bEff.enabled = true; bEff.params.push_back({"amount", 0.15});
    Effect cEff; cEff.type = "contrast"; cEff.enabled = true; cEff.params.push_back({"amount", 1.25});
    Effect sEff; sEff.type = "saturation"; sEff.enabled = true; sEff.params.push_back({"amount", 1.40});
    clip.effects.push_back(bEff);
    clip.effects.push_back(cEff);
    clip.effects.push_back(sEff);

    // 3. Sepia
    Effect sepiaEff; sepiaEff.type = "sepia"; sepiaEff.enabled = true; sepiaEff.params.push_back({"amount", 0.75});
    clip.effects.push_back(sepiaEff);

    // 4. Blur & Sharpen
    Effect blurEff; blurEff.type = "blur"; blurEff.enabled = true; blurEff.params.push_back({"radius", 3.0});
    Effect sharpEff; sharpEff.type = "sharpen"; sharpEff.enabled = true; sharpEff.params.push_back({"amount", 1.5});
    clip.effects.push_back(blurEff);
    clip.effects.push_back(sharpEff);

    // 5. Vignette & Color Grade
    Effect vigEff; vigEff.type = "vignette"; vigEff.enabled = true; vigEff.params.push_back({"strength", 0.8});
    Effect cgEff; cgEff.type = "color_grade"; cgEff.enabled = true;
    cgEff.params.push_back({"lift_r", 0.05});
    cgEff.params.push_back({"gain_b", 0.10});
    clip.effects.push_back(vigEff);
    clip.effects.push_back(cgEff);

    vtrack.addClip(clip);

    Exporter exporter(&project);
    Exporter::Settings settings;
    settings.outputPath = "build/out_mixed_effects.mp4";
    settings.width = 1920;
    settings.height = 1080;
    settings.frameRate = 30.0;

    QString filterGraph;
    exporter.buildFfmpegArgs(settings, &filterGraph);

    std::cout << "Filter graph:\n" << filterGraph.toStdString() << "\n";

    if (!filterGraph.contains("crop=") || !filterGraph.contains("pad=")) {
        std::cerr << "FAIL: Filter graph missing crop + pad chain!\n";
        return false;
    }
    if (!filterGraph.contains("eq=brightness=0.15:contrast=1.25:saturation=1.40")) {
        std::cerr << "FAIL: Filter graph missing combined eq filter!\n";
        return false;
    }
    if (!filterGraph.contains("colorchannelmixer=rr=")) {
        std::cerr << "FAIL: Filter graph missing sepia colorchannelmixer!\n";
        return false;
    }
    if (!filterGraph.contains("boxblur=3.0") || !filterGraph.contains("unsharp=5:5:1.5:5:5:0.0")) {
        std::cerr << "FAIL: Filter graph missing blur or sharpen!\n";
        return false;
    }
    if (!filterGraph.contains("vignette=angle=")) {
        std::cerr << "FAIL: Filter graph missing vignette angle!\n";
        return false;
    }
    if (!filterGraph.contains("lut=r='clip(pow(max((val/maxval)+0.05")) {
        std::cerr << "FAIL: Filter graph missing color grade lut!\n";
        return false;
    }
    if (!filterGraph.contains("afade=t=in:st=0:d=0.500000") || !filterGraph.contains("afade=t=out:st=2.500000:d=0.500000")) {
        std::cerr << "FAIL: Filter graph missing audio afade filters!\n";
        return false;
    }

    std::cout << "PASS: Filter graph contains all mixed effects and audio fades properly formatted!\n";

    // Run export execution test
    QFile::remove(settings.outputPath);
    bool finished = false;
    bool success = false;
    QString finishMsg;

    QObject::connect(&exporter, &Exporter::finished, [&](bool ok, const QString& msg) {
        finished = true;
        success = ok;
        finishMsg = msg;
        QCoreApplication::quit();
    });

    exporter.start(settings);
    QCoreApplication::exec();

    if (!success || !QFile::exists(settings.outputPath) || QFile(settings.outputPath).size() < 1000) {
        std::cerr << "FAIL: Export failed with message: " << finishMsg.toStdString() << "\n";
        return false;
    }

    std::cout << "PASS: Export finished successfully with valid output video file ("
              << QFile(settings.outputPath).size() << " bytes)!\n";
    return true;
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (!testMixedEffectsFilterGraph()) {
        return 1;
    }
    return 0;
}
