// Offline unit test for EFFECT LAYERS (adjustment layers).
//
// An Effect Layer is a media-less clip (ClipType::EffectLayer) that lives on a
// Visual track and lends its effect stack to every visual clip on the tracks
// BELOW it, for as long as the two overlap in time. It never composites a frame
// of its own.
//
// What is covered here (no display, no ffmpeg process is spawned):
//   1. Model: the layer is not returned as a compositing layer, its stack is
//      handed to the clips below it in the right order (clip's own effects
//      first, then the layers bottom-to-top), only inside its time span, and
//      never upwards to the tracks above it. Hidden tracks disable it.
//   2. Duration: an Effect Layer stretched past the end of the footage does
//      not lengthen the timeline (which would add a black tail to exports).
//   3. Project round-trip: the clip type and the effect stack survive
//      save/load of a .hcproj.
//   4. Exporter: the ffmpeg filter graph applies the inherited stack to the
//      clip below (full-cover layers extend that clip's chain, partially
//      overlapping layers become a time-gated split/overlay stage) and never
//      to a clip above the layer.
//
// The exporter part generates its own tiny PNG so the test stays self
// contained; if the platform cannot write or probe it, those checks report
// SKIP instead of failing (the model checks above are unconditional).
//
// Usage: EffectLayerTest (no arguments)

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QColor>
#include <QString>
#include <cmath>
#include <iostream>
#include <string>
#include "../src/core/Project.h"
#include "../src/export/Exporter.h"

using namespace hc;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool cond, const std::string& what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

Effect makeEffect(const QString& type, const QString& paramName, double value) {
    Effect eff;
    eff.type = type;
    eff.enabled = true;
    if (!paramName.isEmpty()) eff.params.push_back(EffectParameter{paramName, value});
    return eff;
}

// A media-less image clip; `assetId` may be a dummy for the model checks
// (Timeline never resolves assets).
Clip makeImageClip(const QString& assetId, double startSec, double durationSec) {
    Clip c;
    c.type = ClipType::Image;
    c.assetId = assetId;
    c.sourceIn = 0;
    c.sourceOut = secondsToTicks(durationSec);
    c.timelineStart = secondsToTicks(startSec);
    return c;
}

// ── 1. Model: inheritance rules ─────────────────────────────────────────
bool testInheritanceRules() {
    Project project;
    Timeline& tl = project.timeline();
    tl.frameRate = 30.0;
    tl.videoWidth = 320;
    tl.videoHeight = 180;

    // NOTE: a Track& pointing into Timeline::tracks() dangles as soon as
    // another track is added (std::vector reallocation), so every track is
    // filled in immediately after it is created, and anything read later goes
    // through tracks()[i] by index.
    Track& v1 = tl.addTrack(TrackType::Visual, "V1");  // index 0 == bottom
    Clip clip = makeImageClip("dummy-asset", 0.0, 4.0);
    clip.effects.push_back(makeEffect("blur", "radius", 4.0));
    v1.addClip(clip);

    Track& fx = tl.addTrack(TrackType::Visual, "FX");  // index 1 == above it
    Clip layer = Clip::makeEffectLayer(secondsToTicks(1.0), secondsToTicks(2.0)); // 1s..3s
    layer.effects.push_back(makeEffect("invert", QString(), 0.0));
    fx.addClip(std::move(layer));

    // The layer must never show up as a compositing layer.
    const auto atHalf = tl.activeVisualClipsAt(secondsToTicks(0.5));
    check(atHalf.size() == 1, "effect layer is not itself composited (outside its span)");
    const auto atTwo = tl.activeVisualClipsAt(secondsToTicks(2.0));
    check(atTwo.size() == 1, "effect layer is not itself composited (inside its span)");
    if (atTwo.empty()) return false;
    check(atTwo[0].clip->type == ClipType::Image, "only the media clip is returned");
    check(atTwo[0].trackIndex == 0, "the returned layer reports its track index");

    // Outside the layer's span nothing is inherited.
    if (!atHalf.empty()) {
        check(atHalf[0].effectLayers.empty(), "no inheritance outside the layer's time span");
        const auto own = Timeline::mergedEffects(*atHalf[0].clip, atHalf[0].effectLayers);
        check(own.size() == 1 && own[0].type == QStringLiteral("blur"),
              "outside the span the clip keeps only its own effects");
    }

    // Inside the span: own effects first, then the layer's stack.
    check(atTwo[0].effectLayers.size() == 1, "one inherited effect layer inside its span");
    const auto merged = Timeline::mergedEffects(*atTwo[0].clip, atTwo[0].effectLayers);
    check(merged.size() == 2, "merged stack holds the clip's own effect plus the layer's");
    check(merged.size() == 2 && merged[0].type == QStringLiteral("blur") &&
              merged[1].type == QStringLiteral("invert"),
          "order is: clip's own effects, then the inherited layer stack");

    // Exporter-side query: exact overlap window, and it is NOT a full cover.
    const auto ranges = tl.effectLayerRangesFor(tl.tracks()[0].clips()[0], 0);
    check(ranges.size() == 1, "exporter sees exactly one overlapping effect layer");
    if (ranges.size() == 1) {
        check(ranges[0].rangeStart == secondsToTicks(1.0) &&
                  ranges[0].rangeEnd == secondsToTicks(3.0),
              "inherited window is the intersection of layer and clip");
        check(!ranges[0].coversWholeClip, "a 1s..3s layer does not cover a 0s..4s clip");
    }

    // Two stacked layers are inherited bottom-to-top.
    Track& fx2 = tl.addTrack(TrackType::Visual, "FX2"); // index 2 == top
    Clip layer2 = Clip::makeEffectLayer(0, secondsToTicks(4.0));
    layer2.effects.push_back(makeEffect("hue_rotate", "degrees", 90.0));
    fx2.addClip(std::move(layer2));
    const auto twoRanges = tl.effectLayerRangesFor(tl.tracks()[0].clips()[0], 0);
    check(twoRanges.size() == 2, "both layers above the clip are inherited");
    check(twoRanges.size() == 2 && twoRanges[0].trackIndex == 1 && twoRanges[1].trackIndex == 2,
          "layers are inherited bottom-to-top");
    if (twoRanges.size() == 2) {
        check(twoRanges[1].coversWholeClip, "a layer spanning the whole clip reports full cover");
    }

    // A layer BELOW a clip must not affect it (it only grades what is under it).
    const auto belowRanges = tl.effectLayerRangesFor(tl.tracks()[2].clips()[0], 2);
    check(belowRanges.empty(), "a clip never inherits from layers below it");

    // Hiding the layer's track disables the layer.
    tl.tracks()[1].hidden = true;
    tl.tracks()[2].hidden = true;
    check(tl.activeEffectLayersAt(secondsToTicks(2.0)).empty(),
          "hidden tracks disable the effect layers they carry");
    check(tl.effectLayerRangesFor(tl.tracks()[0].clips()[0], 0).empty(),
          "hidden effect layers are not inherited by the exporter either");
    const auto hiddenComposite = tl.activeVisualClipsAt(secondsToTicks(2.0));
    check(hiddenComposite.size() == 1 && hiddenComposite[0].effectLayers.empty(),
          "with the layer tracks hidden the clip renders with its own effects only");

    // topmostVisualClipAt() must look straight through an Effect Layer.
    tl.tracks()[1].hidden = false;
    check(tl.topmostVisualClipAt(secondsToTicks(2.0)) != nullptr &&
              !tl.topmostVisualClipAt(secondsToTicks(2.0))->isEffectLayer(),
          "topmost visual clip skips invisible effect layers");

    // The convenience wrapper agrees with mergedEffects() (this is what the
    // preview renderer calls per frame).
    const auto eff = tl.effectiveEffectsAt(tl.tracks()[0].clips()[0], 0, secondsToTicks(2.0));
    check(eff.size() == 2 && eff[1].type == QStringLiteral("invert"),
          "effectiveEffectsAt() matches mergedEffects()");
    check(tl.trackIndexOf(tl.tracks()[0].id) == 0 &&
              tl.trackIndexOf(QStringLiteral("no-such-track")) == -1,
          "trackIndexOf() resolves track stacking order");

    return true;
}

// ── 2. Duration: layers never lengthen the timeline ─────────────────────
bool testDurationExclusion() {
    Project project;
    Timeline& tl = project.timeline();
    tl.frameRate = 30.0;

    Track& v1 = tl.addTrack(TrackType::Visual, "V1");
    v1.addClip(makeImageClip("dummy-asset", 0.0, 3.0));
    check(tl.totalDuration() == secondsToTicks(3.0), "baseline duration is the footage's");

    Track& fx = tl.addTrack(TrackType::Visual, "FX");
    fx.addClip(Clip::makeEffectLayer(0, secondsToTicks(30.0)));
    check(tl.totalDuration() == secondsToTicks(3.0),
          "an effect layer stretched past the footage does not lengthen the timeline");

    // A project holding ONLY an effect layer has no content duration at all.
    Project empty;
    empty.timeline().frameRate = 30.0;
    Track& onlyFx = empty.timeline().addTrack(TrackType::Visual, "FX");
    onlyFx.addClip(Clip::makeEffectLayer(0, secondsToTicks(10.0)));
    check(empty.timeline().totalDuration() == 0, "a lone effect layer contributes no duration");

    return true;
}

// ── 3. Project round-trip ───────────────────────────────────────────────
bool testProjectRoundTrip() {
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::cerr << "FAIL: cannot create a temporary directory\n";
        return false;
    }

    Project project;
    project.timeline().frameRate = 30.0;
    Track& v1 = project.timeline().addTrack(TrackType::Visual, "V1");
    v1.addClip(makeImageClip("dummy-asset", 0.0, 4.0));
    Track& fx = project.timeline().addTrack(TrackType::Visual, "FX");
    Clip layer = Clip::makeEffectLayer(secondsToTicks(1.0), secondsToTicks(2.0));
    layer.effects.push_back(makeEffect("sepia", "amount", 0.75));
    layer.effects.push_back(makeEffect("brightness", "amount", -0.2));
    fx.addClip(std::move(layer));

    const QString path = dir.filePath("effect_layer.hcproj");
    QString err;
    check(project.saveToFile(path, &err), std::string("project saves: ") + err.toStdString());

    Project reloaded;
    check(reloaded.loadFromFile(path, &err), std::string("project loads: ") + err.toStdString());
    check(reloaded.timeline().tracks().size() == 2, "both tracks survive the round-trip");
    if (reloaded.timeline().tracks().size() != 2) return false;

    const auto& clips = reloaded.timeline().tracks()[1].clips();
    check(clips.size() == 1, "the effect layer clip survives the round-trip");
    if (clips.size() != 1) return false;
    const Clip& back = clips[0];
    check(back.isEffectLayer(), "clip type is restored as an effect layer");
    check(back.assetId.isEmpty(), "an effect layer stays media-less after reload");
    check(back.timelineStart == secondsToTicks(1.0) &&
              back.timelineEnd() == secondsToTicks(3.0),
          "the layer's span survives the round-trip");
    check(back.effects.size() == 2, "the layer's effect stack survives the round-trip");
    if (back.effects.size() == 2) {
        check(back.effects[0].type == QStringLiteral("sepia") &&
                  std::abs(back.effects[0].paramValue("amount", 0.0) - 0.75) < 1e-9,
              "effect parameters survive the round-trip");
        check(back.effects[1].type == QStringLiteral("brightness") &&
                  std::abs(back.effects[1].paramValue("amount", 0.0) + 0.2) < 1e-9,
              "the second effect survives the round-trip");
    }
    // Inheritance still works after reload.
    const auto ranges = reloaded.timeline().effectLayerRangesFor(
        reloaded.timeline().tracks()[0].clips()[0], 0);
    check(ranges.size() == 1 && ranges[0].layer->effects.size() == 2,
          "inheritance is computed from the reloaded project too");

    return true;
}

// ── 4. Exporter filter graph ────────────────────────────────────────────
bool testExporterGraph() {
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::cerr << "FAIL: cannot create a temporary directory\n";
        return false;
    }

    // Generate a tiny real image so the exporter resolves an actual input
    // (it skips clips whose asset cannot be found, which would leave nothing
    // to assert on).
    const QString pngPath = dir.filePath("effect_layer_src.png");
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(QColor(200, 30, 30));
    if (!img.save(pngPath, "PNG")) {
        std::cout << "SKIP: exporter checks (cannot write a PNG on this platform)\n";
        return true;
    }

    Project project;
    QString err;
    auto asset = project.importMedia(pngPath, &err);
    if (!asset) {
        std::cout << "SKIP: exporter checks (cannot probe the generated PNG: "
                  << err.toStdString() << ")\n";
        return true;
    }

    Timeline& tl = project.timeline();
    tl.frameRate = 30.0;
    tl.videoWidth = 320;
    tl.videoHeight = 180;

    // Each track is filled in immediately after being created: a Track&
    // dangles once the next addTrack() reallocates the track vector.
    Track& below = tl.addTrack(TrackType::Visual, "V1");  // index 0: inherits
    Clip c1 = makeImageClip(asset->id, 0.0, 4.0);
    c1.effects.push_back(makeEffect("blur", "radius", 4.0));
    below.addClip(c1);

    Track& mid = tl.addTrack(TrackType::Visual, "FX");    // index 1: the layer
    Clip layer = Clip::makeEffectLayer(0, secondsToTicks(4.0)); // full cover
    layer.effects.push_back(makeEffect("invert", QString(), 0.0));
    mid.addClip(std::move(layer));

    Track& top = tl.addTrack(TrackType::Visual, "V2");    // index 2: must NOT inherit
    top.addClip(makeImageClip(asset->id, 0.0, 4.0));

    Exporter exporter(&project);
    Exporter::Settings settings;
    settings.outputPath = dir.filePath("out_effect_layer.mp4");
    settings.width = 320;
    settings.height = 180;
    settings.frameRate = 30.0;

    QString graph;
    exporter.buildFfmpegArgs(settings, &graph);

    const int ownFx = graph.indexOf(QStringLiteral("boxblur=4.0"));
    const int inheritedFx = graph.indexOf(QStringLiteral(",negate"));
    check(ownFx >= 0, "the clip's own effect is in the filter graph");
    check(inheritedFx >= 0, "the effect layer's stack is applied to the clip below it");
    check(ownFx >= 0 && inheritedFx > ownFx,
          "inherited effects run after the clip's own effects (preview order)");
    check(graph.count(QStringLiteral(",negate")) == 1,
          "the clip ABOVE the effect layer does not inherit its stack");
    check(!graph.contains(QStringLiteral("split=2")),
          "a full-cover layer needs no time-gated stage");

    // A disabled layer contributes nothing.
    tl.tracks()[1].clips()[0].effects[0].enabled = false;
    QString graphDisabled;
    exporter.buildFfmpegArgs(settings, &graphDisabled);
    check(!graphDisabled.contains(QStringLiteral(",negate")),
          "a disabled effect on the layer is not exported");
    tl.tracks()[1].clips()[0].effects[0].enabled = true;

    // Hiding the layer's track disables the whole layer.
    tl.tracks()[1].hidden = true;
    QString graphHidden;
    exporter.buildFfmpegArgs(settings, &graphHidden);
    check(!graphHidden.contains(QStringLiteral(",negate")),
          "a hidden effect-layer track is not exported");
    tl.tracks()[1].hidden = false;

    // Trimming the layer so it only partially overlaps the clip must produce a
    // time-gated stage covering exactly the overlap (1s..3s of a 0s..4s clip).
    tl.tracks()[1].clips()[0].timelineStart = secondsToTicks(1.0);
    tl.tracks()[1].clips()[0].sourceOut = secondsToTicks(2.0); // 2s long: spans 1s..3s
    QString graphPartial;
    exporter.buildFfmpegArgs(settings, &graphPartial);
    check(graphPartial.contains(QStringLiteral("split=2")),
          "a partially overlapping layer is applied through a gated split/overlay");
    check(graphPartial.contains(QStringLiteral("enable='between(t,1.000000,3.000000)'")),
          "the gate covers exactly the overlap between layer and clip");
    check(graphPartial.contains(QStringLiteral(",negate")),
          "the gated stage still carries the layer's effect");

    return true;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    bool ok = true;
    ok = testInheritanceRules() && ok;
    ok = testDurationExclusion() && ok;
    ok = testProjectRoundTrip() && ok;
    ok = testExporterGraph() && ok;

    if (!ok || g_failures > 0) {
        std::cerr << "EffectLayerTest: " << g_failures << " of " << g_checks
                  << " checks failed\n";
        return 1;
    }
    std::cout << "PASS: effect layer inheritance, duration, save/load and export graph ("
              << g_checks << " checks)\n";
    return 0;
}
