#include "Exporter.h"
#include "../audio/AudioFilterDesc.h"
#include "../render/TextRenderer.h"
#include "../core/SystemInfo.h"
#include <QElapsedTimer>
#include <QMap>
#include <QRegularExpression>
#include <QDebug>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QImage>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QThread>
#include <cmath>
#include <algorithm>

namespace hc {

namespace {
QString secStr(Ticks t) {
    return QString::number(ticksToSeconds(t), 'f', 6);
}

// Builds a nested FFmpeg `if(lte(var, t), ...)` piecewise linear interpolation expression
// for keyframed values (opacity, position x/y, rotation, scale) evaluated per frame.
static QString buildPiecewiseLinearExpr(const QString& timeVar, const QList<QPair<double, double>>& points) {
    if (points.isEmpty()) return "0.0";
    if (points.size() == 1) return QString::number(points[0].second, 'f', 4);

    QString expr = QString::number(points.last().second, 'f', 4);
    for (int i = points.size() - 2; i >= 0; --i) {
        const double t0 = points[i].first;
        const double t1 = points[i + 1].first;
        const double v0 = points[i].second;
        const double v1 = points[i + 1].second;
        const double span = t1 - t0;
        if (span <= 0.000001) {
            expr = QString("if(lte(%1,%2),%3,%4)")
                .arg(timeVar).arg(t0, 0, 'f', 6)
                .arg(v0, 0, 'f', 4).arg(expr);
        } else {
            const QString lerp = QString("(%1+(%2-%1)*(%3-%4)/%5)")
                .arg(v0, 0, 'f', 4).arg(v1, 0, 'f', 4)
                .arg(timeVar).arg(t0, 0, 'f', 6).arg(span, 0, 'f', 6);
            expr = QString("if(lte(%1,%2),%3,%4)")
                .arg(timeVar).arg(t1, 0, 'f', 6)
                .arg(lerp).arg(expr);
        }
    }
    expr = QString("if(lte(%1,%2),%3,%4)")
        .arg(timeVar).arg(points.first().first, 0, 'f', 6)
        .arg(points.first().second, 0, 'f', 4).arg(expr);
    return expr;
}

static QString buildOpacityFilterChain(const Clip& clip, double startSec) {
    if (!clip.hasTransformKeyframes()) {
        const double staticOp = std::clamp(clip.opacity * clip.transform.opacity, 0.0, 1.0);
        if (staticOp < 0.999) {
            return QString(",colorchannelmixer=aa=%1").arg(staticOp, 0, 'f', 3);
        }
        return {};
    }

    const auto& kfs = clip.transformKeyframes;
    double baseOpacity = kfs.first().value.opacity;
    bool hasAnimatedOpacity = false;
    for (const auto& kf : kfs) {
        if (std::abs(kf.value.opacity - baseOpacity) > 0.001) {
            hasAnimatedOpacity = true;
            break;
        }
    }

    if (!hasAnimatedOpacity) {
        if (baseOpacity < 0.999) {
            return QString(",colorchannelmixer=aa=%1").arg(std::clamp(baseOpacity, 0.0, 1.0), 0, 'f', 3);
        }
        return {};
    }

    // High performance optimization: use C-SIMD `fade` filter for standard fade in/out curves
    if (kfs.size() == 2) {
        const double t0 = startSec + ticksToSeconds(kfs[0].time);
        const double t1 = startSec + ticksToSeconds(kfs[1].time);
        const double v0 = kfs[0].value.opacity;
        const double v1 = kfs[1].value.opacity;
        const double dur = std::max(0.001, t1 - t0);

        if (std::abs(v0 - 0.0) < 0.05 && std::abs(v1 - 1.0) < 0.05) {
            return QString(",fade=t=in:st=%1:d=%2:alpha=1").arg(t0, 0, 'f', 6).arg(dur, 0, 'f', 6);
        }
        if (std::abs(v0 - 1.0) < 0.05 && std::abs(v1 - 0.0) < 0.05) {
            return QString(",fade=t=out:st=%1:d=%2:alpha=1").arg(t0, 0, 'f', 6).arg(dur, 0, 'f', 6);
        }
    } else if (kfs.size() == 4) {
        const double t0 = startSec + ticksToSeconds(kfs[0].time);
        const double t1 = startSec + ticksToSeconds(kfs[1].time);
        const double t2 = startSec + ticksToSeconds(kfs[2].time);
        const double t3 = startSec + ticksToSeconds(kfs[3].time);
        if (std::abs(kfs[0].value.opacity - 0.0) < 0.05 &&
            std::abs(kfs[1].value.opacity - 1.0) < 0.05 &&
            std::abs(kfs[2].value.opacity - 1.0) < 0.05 &&
            std::abs(kfs[3].value.opacity - 0.0) < 0.05) {
            return QString(",fade=t=in:st=%1:d=%2:alpha=1,fade=t=out:st=%3:d=%4:alpha=1")
                .arg(t0, 0, 'f', 6).arg(std::max(0.001, t1 - t0), 0, 'f', 6)
                .arg(t2, 0, 'f', 6).arg(std::max(0.001, t3 - t2), 0, 'f', 6);
        }
    }

    // General continuous piecewise linear evaluator using geq
    QList<QPair<double, double>> opPoints;
    for (const auto& kf : kfs) {
        const double tSec = startSec + ticksToSeconds(kf.time);
        const double val = std::clamp(kf.value.opacity, 0.0, 1.0);
        opPoints.push_back({tSec, val});
    }
    const QString opExpr = buildPiecewiseLinearExpr("T", opPoints);
    return QString(",geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='alpha(X,Y)*clip(%1,0,1)'").arg(opExpr);
}

// Maps our BlendMode enum to ffmpeg blend filter name.
//
// Must mirror what GLVideoWidget::applyBlendMode() actually draws in
// Preview, not just what ffmpeg's blend filter happens to support by that
// name — otherwise Export and Preview visibly diverge for these modes.
// GLVideoWidget only implements dedicated glBlendFunc/glBlendEquation
// combinations for Normal, Multiply, Screen, Overlay, Add, Subtract,
// Darken, Lighten, Difference and Exclusion; HardLight, SoftLight, Dodge,
// Burn, Saturate and the four HSL modes all fall through its switch's
// shared `default:` case, which is plain Normal blending (no dedicated
// GL equation is implemented for them). ffmpeg's blend filter DOES have
// real "hardlight"/"softlight"/"dodge"/"burn"/"hue"/"saturation"/"color"/
// "luminosity" filters, but using them here would make Export show a
// genuinely different blend than what the user saw in Preview, so they
// map to "normal" here too until GLVideoWidget grows real implementations
// for them.
QString ffmpegBlendMode(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal:        return "normal";
        case BlendMode::Multiply:      return "multiply";
        case BlendMode::Screen:        return "screen";
        case BlendMode::Overlay:       return "overlay";
        case BlendMode::Add:           return "addition";
        case BlendMode::Subtract:      return "subtract";
        case BlendMode::Darken:        return "darken";
        case BlendMode::Lighten:       return "lighten";
        case BlendMode::Difference:    return "difference";
        case BlendMode::Exclusion:     return "exclusion";
        // No dedicated GL blend equation in Preview for these — match its
        // Normal fallback instead of using ffmpeg's real (but different-
        // looking) filter of the same name.
        case BlendMode::HardLight:     return "normal";
        case BlendMode::SoftLight:     return "normal";
        case BlendMode::Dodge:         return "normal";
        case BlendMode::Burn:          return "normal";
        case BlendMode::Saturate:      return "normal";
        case BlendMode::HSLHue:        return "normal";
        case BlendMode::HSLSaturation: return "normal";
        case BlendMode::HSLColor:      return "normal";
        case BlendMode::HSLLuminosity: return "normal";
    }
    return "normal";
}

struct TargetFitDimensions {
    int targetW;
    int targetH;
};

static TargetFitDimensions computeTargetFit(int srcW, int srcH, int canvasW, int canvasH, double scaleX, double scaleY) {
    if (canvasW <= 0) canvasW = 1920;
    if (canvasH <= 0) canvasH = 1080;
    if (srcW <= 0) srcW = canvasW;
    if (srcH <= 0) srcH = canvasH;

    const double widgetAspect = static_cast<double>(canvasW) / static_cast<double>(canvasH);
    const double sourceAspect = static_cast<double>(srcW) / static_cast<double>(srcH);

    double fitW, fitH;
    if (sourceAspect > widgetAspect) {
        fitW = static_cast<double>(canvasW);
        fitH = static_cast<double>(canvasW) / sourceAspect;
    } else {
        fitH = static_cast<double>(canvasH);
        fitW = static_cast<double>(canvasH) * sourceAspect;
    }

    int tw = qMax(2, (qRound(fitW * std::abs(scaleX)) / 2) * 2);
    int th = qMax(2, (qRound(fitH * std::abs(scaleY)) / 2) * 2);
    return { tw, th };
}

static QString buildVideoEffectsFilterChain(const std::vector<Effect>& effects, int tw, int th) {
    QString chain;
    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float hueRotate = 0.0f;
    float sepia = 0.0f;
    float invert = 0.0f;
    float vignetteStrength = 0.0f;
    float blurRadius = 0.0f;
    float sharpenAmount = 0.0f;
    double cl = 0.0, ct = 0.0, cr = 0.0, cb = 0.0;
    float liftR = 0.0f, liftG = 0.0f, liftB = 0.0f;
    float gammaR = 0.0f, gammaG = 0.0f, gammaB = 0.0f;
    float gainR = 0.0f, gainG = 0.0f, gainB = 0.0f;

    for (const auto& eff : effects) {
        if (!eff.enabled) continue;
        if (eff.type == "brightness") {
            brightness += static_cast<float>(eff.paramValue("amount", 0.0));
        } else if (eff.type == "contrast") {
            contrast *= static_cast<float>(eff.paramValue("amount", 1.0));
        } else if (eff.type == "saturation") {
            saturation *= static_cast<float>(eff.paramValue("amount", 1.0));
        } else if (eff.type == "hue_rotate") {
            hueRotate += static_cast<float>(eff.paramValue("degrees", 0.0));
        } else if (eff.type == "sepia") {
            sepia = std::max(sepia, static_cast<float>(eff.paramValue("amount", 0.8)));
        } else if (eff.type == "invert") {
            invert = 1.0f;
        } else if (eff.type == "blur") {
            blurRadius = std::max(blurRadius, static_cast<float>(eff.paramValue("radius", 5.0)));
        } else if (eff.type == "sharpen") {
            sharpenAmount = std::max(sharpenAmount, static_cast<float>(eff.paramValue("amount", 1.0)));
        } else if (eff.type == "vignette") {
            vignetteStrength = static_cast<float>(eff.paramValue("strength", 0.5));
        } else if (eff.type == "color_grade") {
            float lr = static_cast<float>(eff.paramValue("lift_r", eff.paramValue("lift", 0.0)));
            float lg = static_cast<float>(eff.paramValue("lift_g", eff.paramValue("lift", 0.0)));
            float lb = static_cast<float>(eff.paramValue("lift_b", eff.paramValue("lift", 0.0)));
            float gr = static_cast<float>(eff.paramValue("gamma_r", eff.paramValue("gamma", 0.0)));
            float gg = static_cast<float>(eff.paramValue("gamma_g", eff.paramValue("gamma", 0.0)));
            float gb = static_cast<float>(eff.paramValue("gamma_b", eff.paramValue("gamma", 0.0)));
            float gar = static_cast<float>(eff.paramValue("gain_r", eff.paramValue("gain", 0.0)));
            float gag = static_cast<float>(eff.paramValue("gain_g", eff.paramValue("gain", 0.0)));
            float gab = static_cast<float>(eff.paramValue("gain_b", eff.paramValue("gain", 0.0)));
            liftR += lr; liftG += lg; liftB += lb;
            gammaR += gr; gammaG += gg; gammaB += gb;
            gainR += gar; gainG += gag; gainB += gab;
        } else if (eff.type == "crop") {
            cl = std::clamp(cl + eff.paramValue("left", 0.0), 0.0, 0.99);
            ct = std::clamp(ct + eff.paramValue("top", 0.0), 0.0, 0.99);
            cr = std::clamp(cr + eff.paramValue("right", 0.0), 0.0, 0.99);
            cb = std::clamp(cb + eff.paramValue("bottom", 0.0), 0.0, 0.99);
        }
    }

    // 1. Crop + Pad (preserves outer bounding box & center anchor point)
    if (cl > 0.001 || ct > 0.001 || cr > 0.001 || cb > 0.001) {
        int cw = qMax(2, (qRound(tw * (1.0 - cl - cr)) / 2) * 2);
        int ch = qMax(2, (qRound(th * (1.0 - ct - cb)) / 2) * 2);
        int cx = (qRound(tw * cl) / 2) * 2;
        int cy = (qRound(th * ct) / 2) * 2;
        chain += QString(",crop=%1:%2:%3:%4,pad=%5:%6:%3:%4:color=black@0")
            .arg(cw).arg(ch).arg(cx).arg(cy).arg(tw).arg(th);
    }

    // 2. Blur
    if (blurRadius > 0.5f) {
        chain += QString(",boxblur=%1").arg(blurRadius, 0, 'f', 1);
    }

    // 3. Sharpen
    if (sharpenAmount > 0.1f) {
        chain += QString(",unsharp=5:5:%1:5:5:0.0").arg(sharpenAmount, 0, 'f', 1);
    }

    // 4. Brightness, Contrast, Saturation
    if (std::abs(brightness) > 0.001f || std::abs(contrast - 1.0f) > 0.001f || std::abs(saturation - 1.0f) > 0.001f) {
        chain += QString(",eq=brightness=%1:contrast=%2:saturation=%3")
            .arg(brightness, 0, 'f', 2)
            .arg(contrast, 0, 'f', 2)
            .arg(saturation, 0, 'f', 2);
    }

    // 5. Hue Rotate
    if (std::abs(hueRotate) > 0.001f) {
        chain += QString(",hue=h=%1").arg(hueRotate, 0, 'f', 1);
    }

    // 6. Sepia (dynamic blend matrix according to amount)
    if (sepia > 0.001f) {
        const double a = std::clamp<double>(sepia, 0.0, 1.0);
        const double rr = (1.0 - a) + a * 0.393;
        const double rg = a * 0.769;
        const double rb = a * 0.189;
        const double gr = a * 0.349;
        const double gg = (1.0 - a) + a * 0.686;
        const double gb = a * 0.168;
        const double br = a * 0.272;
        const double bg = a * 0.534;
        const double bb = (1.0 - a) + a * 0.131;
        chain += QString(",colorchannelmixer=rr=%1:rg=%2:rb=%3:gr=%4:gg=%5:gb=%6:br=%7:bg=%8:bb=%9")
            .arg(rr, 0, 'f', 3).arg(rg, 0, 'f', 3).arg(rb, 0, 'f', 3)
            .arg(gr, 0, 'f', 3).arg(gg, 0, 'f', 3).arg(gb, 0, 'f', 3)
            .arg(br, 0, 'f', 3).arg(bg, 0, 'f', 3).arg(bb, 0, 'f', 3);
    }

    // 7. Invert
    if (invert > 0.5f) {
        chain += ",negate";
    }

    // 8. Color Grade (Lift / Gamma / Gain)
    // The GLSL shader applies (per channel, values normalised 0..1):
    //   lift:  out = in + L*(1-in)
    //   gamma: out = pow(max(out, 0.0001), 1/(1+G))
    //   gain:  out = out * (1+G)
    // ffmpeg's `colorbalance` filter is a different algorithm (it adjusts
    // shadow/midtone/highlight balance, not the same math), so we use the
    // `lut` filter with per-channel lookup expressions that replicate the
    // shader formula exactly.  `val` in a lut expression is the integer
    // sample value in [0,maxval]; `maxval` is 255 for 8-bit or 65535 for
    // 16-bit.  We write a self-contained expression for each channel so
    // there is no need to chain multiple filters.
    const bool hasColorGrade = (std::abs(liftR) > 0.001f || std::abs(liftG) > 0.001f || std::abs(liftB) > 0.001f ||
                                std::abs(gammaR) > 0.001f || std::abs(gammaG) > 0.001f || std::abs(gammaB) > 0.001f ||
                                std::abs(gainR) > 0.001f || std::abs(gainG) > 0.001f || std::abs(gainB) > 0.001f);
    if (hasColorGrade) {
        // Build a per-channel lut expression.
        // Formula (all in normalised [0,1] space):
        //   t = val / maxval
        //   t = t + L*(1-t)                              [lift]
        //   t = pow(max(t, 0.0001), 1/(1+G))             [gamma]
        //   t = t * (1+Gn)                               [gain]
        //   out = clamp(t, 0, 1) * maxval
        auto lutExpr = [](double L, double G, double Gn) -> QString {
            // Clamp denominators to avoid division-by-zero / NaN in the lut.
            const double gammaExp = 1.0 / std::max(0.01, 1.0 + G);
            return QString(
                "clip("
                  "pow(max("
                    "(val/maxval)+%1*(1-(val/maxval))"
                  ",0.0001),%2)"
                  "*(1+%3)"
                "*maxval,0,maxval)")
                .arg(L, 0, 'g', 6)
                .arg(gammaExp, 0, 'g', 6)
                .arg(Gn, 0, 'g', 6);
        };
        chain += QString(",lut=r='%1':g='%2':b='%3'")
            .arg(lutExpr(liftR, gammaR, gainR))
            .arg(lutExpr(liftG, gammaG, gainG))
            .arg(lutExpr(liftB, gammaB, gainB));
    }

    // 9. Vignette
    if (vignetteStrength > 0.001f) {
        const double angle = std::clamp<double>(0.785398 * vignetteStrength, 0.05, 1.57);
        chain += QString(",vignette=angle=%1").arg(angle, 0, 'f', 3);
    }

    return chain;
}

bool lowMemoryExportMode() {
    // Read /proc/meminfo once (via systeminfo) and capture both
    // MemAvailable and MemTotal. This is the same signal the exporter uses
    // to decide whether to drop to a single encode thread and cap
    // allocations; it's reused here so the threshold lives in one place.
    const uint64_t avail = systeminfo::availableMemoryBytes();
    const uint64_t total = systeminfo::totalMemoryBytes();
    // Conservative: trigger low-RAM mode when available < 12 GiB OR the
    // machine has <= 16 GiB total (page-cache can hide real pressure).
    if (avail > 0) {
        return avail < (12ull << 30) || (total > 0 && total <= (16ull << 30));
    }
    // Non-Linux (or unreadable): fall back to total-RAM-only detection.
    if (total > 0) return total <= (16ull << 30);
    return false;
}

} // namespace

Exporter::Exporter(Project* project, QObject* parent) : QObject(parent), m_project(project) {}

Exporter::~Exporter() {
    cancel();
}

QStringList Exporter::buildFfmpegArgs(const Settings& s, QString* filterGraphDebugOut) {
    Timeline& tl = m_project->timeline();
    m_totalDurationTicks = tl.totalDuration();

    // Guard against degenerate canvas sizes that would make ffmpeg fail.
    const int outW = std::max(s.width, 2);
    const int outH = std::max(s.height, 2);
    const bool lowMem = lowMemoryExportMode();
    if (lowMem) {
        qWarning() << "[Exporter] Low-memory mode enabled automatically"
                   << "(available < 12 GiB or total <= 16 GiB)";
    }

    // --- collect unique input files, in first-seen order ---
    QMap<QString, int> inputIndexByPath; // filePath -> ffmpeg -i index
    QMap<QString, bool> isImageByPath;   // filePath -> is still image
    QStringList inputPaths;
    // Declared here (ahead of the rasterization pass below) purely so the
    // inputIndexFor lambda's [&] capture can see it lexically — C++ name
    // lookup inside a lambda body resolves at the point the lambda is
    // defined, not at the point it's later called, so textPngPaths must
    // already be in scope even though it isn't actually filled in until
    // the text/title rasterization loop below runs (which is still
    // guaranteed to happen before inputIndexFor is ever invoked).
    QStringList textPngPaths;
    auto inputIndexFor = [&](const QString& assetId) -> int {
        auto asset = m_project->findAsset(assetId);
        if (!asset) return -1;
        // Missing/unlinked media must NOT become an ffmpeg input — that would
        // make the whole export fail instead of just rendering a black gap.
        if (asset->kind == MediaKind::Unknown || !QFileInfo::exists(asset->filePath)) return -1;
        auto it = inputIndexByPath.find(asset->filePath);
        if (it != inputIndexByPath.end()) return it.value();
        // Text-layer PNG inputs are always placed first in argv (see the
        // rasterization pass above, which fully populates textPngPaths
        // before this lambda is ever called) so every ordinary media
        // input's real ffmpeg index is offset by that fixed count.
        const int idx = textPngPaths.size() + inputPaths.size();
        inputPaths.push_back(asset->filePath);
        inputIndexByPath.insert(asset->filePath, idx);
        // "-loop 1" is only a valid demuxer option for still images (or a
        // single-frame video source, which we detect via frameRate<=0 as a
        // fallback for probe quirks). It must NOT be applied to audio-only
        // assets: MediaAsset::frameRate is documented as "0 for audio-only",
        // so the frameRate<=0.0001 fallback used to also match every mp3/wav
        // input, producing "-loop 1 -i foo.mp3" — ffmpeg rejects -loop for
        // non-image demuxers ("Option loop not found") and the whole export
        // fails before the filter graph is even touched. Requiring hasVideo()
        // (and not hasAudio()) restricts the fallback to genuine still-image-
        // like video sources.
        const bool looksLikeStillImage = asset->kind == MediaKind::Image ||
            (asset->hasVideo() && !asset->hasAudio() && asset->frameRate <= 0.0001);
        isImageByPath.insert(asset->filePath, looksLikeStillImage);
        return idx;
    };

    // --- text/title layers: rasterize each text clip to a transparent PNG ---
    // Rasterized once per clip (content/canvas-size dependent only, not
    // per-segment) and then composited through the SAME per-segment layer
    // loop as video/image clips below — see textInputIndex. This is what
    // keeps text position, scale, rotation, opacity, blend mode and track
    // order identical to what PlaybackController/GLVideoWidget draws in
    // Preview, instead of an independent always-on-top, always-at-(0,0)
    // overlay pass.
    QMap<const Clip*, int> textInputIndex; // Clip* -> index within textPngPaths
    m_tempDir = QTemporaryDir();
    for (const auto& track : tl.tracks()) {
        // Only track.hidden gates visual output (matches
        // Timeline::activeVisualClipsAt); track.muted is audio-only and
        // must not hide a text layer.
        if (track.hidden) continue;
        for (const auto& clip : track.clips()) {
            if (clip.type != ClipType::Text) continue;
            if (clip.displayLabel.trimmed().isEmpty()) continue;
            if (clip.timelineEnd() <= clip.timelineStart) continue;
            const QString pngPath = m_tempDir.filePath(
                QString("hc_text_%1.png").arg(textPngPaths.size()));
            QImage raster = TextRenderer::renderText(clip, outW, outH, true);
            if (raster.isNull()) continue;
            if (!raster.save(pngPath, "PNG")) continue;
            textInputIndex.insert(&clip, textPngPaths.size());
            textPngPaths.append(pngPath);
        }
    }

    QStringList videoFilterParts;
    QStringList videoSegLabels;

    // Low-RAM visual fast path:
    // The former exporter built one trim/settb/scale/overlay chain for EVERY
    // timeline segment. A long clip therefore caused the same input stream to
    // be decoded/filtered dozens (or hundreds) of times, which is exactly why
    // FFmpeg printed repeated "Stream #N:0 -> settb" lines and why the process
    // could hit the kernel OOM killer even with -threads 1.
    //
    // For ordinary static-transform clips we don't need segment splitting at
    // all. Build each visual clip exactly once, then gate its compositor with
    // overlay's `enable=between(t,start,end)`. This keeps one decoder/filter
    // branch per clip and bounded frame buffering. Animated transform
    // keyframes and explicit crossfades still use the legacy segment path so
    // Single-pass visual compositor:
    // Every visual clip (video, image, text) is decoded and filtered exactly ONCE.
    // Dynamic transforms (position X/Y, scale, rotation) and opacity keyframes are
    // evaluated per frame using FFmpeg's native continuous expression engine:
    // - Opacity: geq alpha evaluation with piecewise linear keyframe interpolation
    // - Position: overlay x/y dynamic expressions evaluated on every output frame
    // - Rotation: rotate dynamic expression evaluated on every output frame
    // - Scale: scale eval=frame dynamic expression evaluated on every output frame
    // This completely eliminates:
    // 1) Choppy 5fps stair-stepping from coarse segment splitting
    // 2) Black-frame flickering/flashing caused by trim PTS rounding mismatch with canvas duration
    // 3) Massive memory duplication from decoding the same clip dozens of times in parallel
    const double totalSec = std::max(ticksToSeconds(tl.totalDuration()), 0.001);
    QString current = "canvas_single";
    videoFilterParts << QString(
        "color=c=black:s=%1x%2:d=%3:r=%4,format=rgba[%5]")
        .arg(outW).arg(outH).arg(totalSec, 0, 'f', 6)
        .arg(s.frameRate, 0, 'f', 6).arg(current);

    int layerCounter = 0;
    for (const auto& track : tl.tracks()) {
        if (track.type != TrackType::Visual || track.hidden) continue;
        for (const auto& clip : track.clips()) {
            if (clip.timelineEnd() <= clip.timelineStart) continue;
            const bool isText = (clip.type == ClipType::Text);
            bool isStillImage = isText;
            int srcW = outW;
            int srcH = outH;
            if (!isText) {
                auto asset = m_project->findAsset(clip.assetId);
                if (asset) {
                    if (asset->kind == MediaKind::Image) isStillImage = true;
                    if (asset->width > 0 && asset->height > 0) {
                        srcW = asset->width;
                        srcH = asset->height;
                    }
                }
            }

            const int idx = isText ? textInputIndex.value(&clip, -1) : inputIndexFor(clip.assetId);
            if (idx < 0) continue;

            const double startSec = std::max(0.0, ticksToSeconds(clip.timelineStart));
            const double endSec = std::max(startSec, ticksToSeconds(clip.timelineEnd()));
            const double durationSec = std::max(endSec - startSec, 1.0 / std::max(s.frameRate, 1.0));

            // Determine if scale is animated across keyframes
            bool hasAnimatedScale = false;
            double baseScaleX = std::abs(clip.transform.scaleX);
            double baseScaleY = std::abs(clip.transform.scaleY);
            if (clip.hasTransformKeyframes()) {
                baseScaleX = std::abs(clip.transformKeyframes.first().value.scaleX);
                baseScaleY = std::abs(clip.transformKeyframes.first().value.scaleY);
                for (const auto& kf : clip.transformKeyframes) {
                    if (std::abs(std::abs(kf.value.scaleX) - baseScaleX) > 0.001 ||
                        std::abs(std::abs(kf.value.scaleY) - baseScaleY) > 0.001) {
                        hasAnimatedScale = true;
                        break;
                    }
                }
            }

            const TargetFitDimensions fitDim = computeTargetFit(srcW, srcH, outW, outH, 1.0, 1.0);
            const int boxW = hasAnimatedScale ? fitDim.targetW : qMax(2, (qRound(fitDim.targetW * baseScaleX) / 2) * 2);
            const int boxH = hasAnimatedScale ? fitDim.targetH : qMax(2, (qRound(fitDim.targetH * baseScaleY) / 2) * 2);

            QString chain;
            if (isStillImage) {
                chain = QString(
                    "[%1:v]trim=duration=%2,setpts=PTS+%3/TB,fps=%4:round=near,"
                    "scale=%5:%6:flags=bilinear,setsar=1,format=rgba")
                    .arg(idx)
                    .arg(durationSec, 0, 'f', 6)
                    .arg(startSec, 0, 'f', 6)
                    .arg(s.frameRate, 0, 'f', 6)
                    .arg(boxW).arg(boxH);
            } else {
                const double srcStart = ticksToSeconds(std::max<Ticks>(0, clip.sourceIn));
                const double srcEnd = ticksToSeconds(std::max<Ticks>(clip.sourceIn, clip.sourceOut));
                const double clipSpeed = clip.speed > 0.01 ? clip.speed : 1.0;
                chain = QString(
                    "[%1:v]trim=start=%2:end=%3,setpts=(PTS-STARTPTS)/%7+%8/TB,"
                    "fps=%6:round=near,scale=%4:%5:flags=bilinear,setsar=1,format=rgba")
                    .arg(idx)
                    .arg(srcStart, 0, 'f', 6)
                    .arg(srcEnd, 0, 'f', 6)
                    .arg(boxW).arg(boxH)
                    .arg(s.frameRate, 0, 'f', 6)
                    .arg(clipSpeed, 0, 'f', 6)
                    .arg(startSec, 0, 'f', 6);
            }

            if (hasAnimatedScale) {
                QList<QPair<double, double>> scaleXPoints, scaleYPoints;
                for (const auto& kf : clip.transformKeyframes) {
                    const double tSec = startSec + ticksToSeconds(kf.time);
                    scaleXPoints.push_back({tSec, std::abs(kf.value.scaleX)});
                    scaleYPoints.push_back({tSec, std::abs(kf.value.scaleY)});
                }
                chain += QString(",scale=eval=frame:w='max(2,trunc(%1*(%2)/2)*2)':h='max(2,trunc(%3*(%4)/2)*2)':flags=bilinear")
                    .arg(fitDim.targetW)
                    .arg(buildPiecewiseLinearExpr("t", scaleXPoints))
                    .arg(fitDim.targetH)
                    .arg(buildPiecewiseLinearExpr("t", scaleYPoints));
            }

            if (clip.transitionInDuration > 0) {
                const double transSec = std::max(0.0, ticksToSeconds(clip.transitionInDuration));
                if (transSec > 0.000001) {
                    chain += QString(",fade=t=in:st=%1:d=%2:alpha=1")
                        .arg(startSec, 0, 'f', 6)
                        .arg(transSec, 0, 'f', 6);
                }
            }

            chain += buildVideoEffectsFilterChain(clip.effects, boxW, boxH);

            // Dynamic rotation
            bool hasAnimatedRot = false;
            double baseRot = clip.transform.rotationDeg;
            if (clip.hasTransformKeyframes()) {
                baseRot = clip.transformKeyframes.first().value.rotationDeg;
                for (const auto& kf : clip.transformKeyframes) {
                    if (std::abs(kf.value.rotationDeg - baseRot) > 0.01) {
                        hasAnimatedRot = true;
                        break;
                    }
                }
            }
            if (hasAnimatedRot) {
                QList<QPair<double, double>> rotPoints;
                for (const auto& kf : clip.transformKeyframes) {
                    const double tSec = startSec + ticksToSeconds(kf.time);
                    rotPoints.push_back({tSec, kf.value.rotationDeg});
                }
                const QString rotExpr = QString("-(%1)*PI/180.0").arg(buildPiecewiseLinearExpr("t", rotPoints));
                chain += QString(",rotate=a='%1':ow=hypot(iw\\,ih):oh=hypot(iw\\,ih):c=black@0").arg(rotExpr);
            } else if (std::abs(baseRot) > 0.001) {
                const double rad = -baseRot * M_PI / 180.0;
                chain += QString(",rotate=%1:ow=hypot(iw\\,ih):oh=hypot(iw\\,ih):c=black@0")
                    .arg(rad, 0, 'f', 6);
            }

            // Smooth opacity keyframing & static opacity
            chain += buildOpacityFilterChain(clip, startSec);

            const QString pre = QString("singleLayer%1").arg(layerCounter);
            chain += QString("[%1]").arg(pre);
            videoFilterParts << chain;

            // Position X / Y (animated vs static)
            bool hasAnimatedPos = false;
            double baseX = clip.transform.x;
            double baseY = clip.transform.y;
            if (clip.hasTransformKeyframes()) {
                baseX = clip.transformKeyframes.first().value.x;
                baseY = clip.transformKeyframes.first().value.y;
                for (const auto& kf : clip.transformKeyframes) {
                    if (std::abs(kf.value.x - baseX) > 0.001 || std::abs(kf.value.y - baseY) > 0.001) {
                        hasAnimatedPos = true;
                        break;
                    }
                }
            }

            QString posXExpr, posYExpr;
            if (hasAnimatedPos) {
                QList<QPair<double, double>> xPoints, yPoints;
                for (const auto& kf : clip.transformKeyframes) {
                    const double tSec = startSec + ticksToSeconds(kf.time);
                    xPoints.push_back({tSec, kf.value.x});
                    yPoints.push_back({tSec, kf.value.y});
                }
                posXExpr = QString("(main_w-overlay_w)/2+(%1)*%2/2.0")
                    .arg(buildPiecewiseLinearExpr("t", xPoints))
                    .arg(outW);
                posYExpr = QString("(main_h-overlay_h)/2+(%1)*%2/2.0")
                    .arg(buildPiecewiseLinearExpr("t", yPoints))
                    .arg(outH);
            } else {
                const int offX = qRound(baseX * outW / 2.0);
                const int offY = qRound(baseY * outH / 2.0);
                posXExpr = QString("(main_w-overlay_w)/2+%1").arg(offX);
                posYExpr = QString("(main_h-overlay_h)/2+%1").arg(offY);
            }

            const QString next = QString("singleComp%1").arg(layerCounter);
            QString overlayBlend;
            if (clip.blendMode != BlendMode::Normal) {
                overlayBlend = QString(":blend=%1").arg(ffmpegBlendMode(clip.blendMode));
            }

            videoFilterParts << QString(
                "[%1][%2]overlay=x='%3':y='%4':"
                "format=auto:eof_action=pass:repeatlast=0%5:enable='between(t,%6,%7)'[%8]")
                .arg(current, pre)
                .arg(posXExpr, posYExpr)
                .arg(overlayBlend)
                .arg(startSec, 0, 'f', 6)
                .arg(endSec, 0, 'f', 6)
                .arg(next);
            current = next;
            ++layerCounter;
        }
    }

    videoFilterParts << QString("[%1]format=yuv420p[vout]").arg(current);
    qInfo() << "[Exporter] single-pass visual graph:"
            << layerCounter << "visual clip branches";

    // --- audio: mix every unmuted clip on visible, unmuted tracks ---
    QStringList audioFilterParts;
    QStringList audioSegLabels;
    int audioSegCounter = 0;
    for (const auto& track : tl.tracks()) {
        if (track.muted || track.hidden) continue;
        for (const auto& clip : track.clips()) {
            if (clip.muted || clip.volume <= 0.0001) continue;
            if (clip.type == ClipType::Text) continue;
            auto asset = m_project->findAsset(clip.assetId);
            if (!asset || !asset->hasAudio()) continue;
            const int idx = inputIndexFor(clip.assetId);
            if (idx < 0) continue;

            const QString label = QString("aseg%1").arg(audioSegCounter++);
            const qint64 delayMs = static_cast<qint64>(std::llround(ticksToSeconds(clip.timelineStart) * 1000.0));
            const QString filterDesc = buildAudioFilterDescription(clip.audioFilters);
            const QString filterStage = filterDesc.isEmpty() ? QString() : ("," + filterDesc);

            QString speedFilter;
            if (std::abs(clip.speed - 1.0) > 0.01 && clip.speed > 0.05) {
                double spd = clip.speed;
                while (spd > 2.0) {
                    speedFilter += ",atempo=2.0";
                    spd /= 2.0;
                }
                while (spd < 0.5) {
                    speedFilter += ",atempo=0.5";
                    spd /= 0.5;
                }
                speedFilter += QString(",atempo=%1").arg(spd, 0, 'f', 3);
            }

            QString fadeFilter;
            const double clipDurSec = ticksToSeconds(clip.timelineDuration());
            if (clip.fadeInDuration > 0) {
                const double fadeInSec = std::min(clipDurSec, ticksToSeconds(clip.fadeInDuration));
                if (fadeInSec > 0.000001) {
                    fadeFilter += QString(",afade=t=in:st=0:d=%1").arg(fadeInSec, 0, 'f', 6);
                }
            }
            if (clip.fadeOutDuration > 0) {
                const double fadeOutSec = std::min(clipDurSec, ticksToSeconds(clip.fadeOutDuration));
                const double fadeOutStartSec = std::max(0.0, clipDurSec - fadeOutSec);
                if (fadeOutSec > 0.000001) {
                    fadeFilter += QString(",afade=t=out:st=%1:d=%2").arg(fadeOutStartSec, 0, 'f', 6).arg(fadeOutSec, 0, 'f', 6);
                }
            }

            audioFilterParts << QString(
                "[%1:a]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS%4%5%6,volume=%7,adelay=delays=%8:all=1[%9]")
                .arg(idx).arg(secStr(clip.sourceIn)).arg(secStr(clip.sourceOut)).arg(speedFilter).arg(filterStage).arg(fadeFilter)
                .arg(clip.volume, 0, 'f', 3).arg(delayMs).arg(label);
            audioSegLabels << QString("[%1]").arg(label);
        }
    }
    const bool hasAudio = !audioSegLabels.isEmpty();
    if (hasAudio) {
        audioFilterParts << QString(
            "%1amix=inputs=%2:duration=longest:dropout_transition=0:normalize=0,"
            "atrim=duration=%3,asetpts=PTS-STARTPTS[aout]")
            .arg(audioSegLabels.join("")).arg(audioSegLabels.size())
            .arg(ticksToSeconds(tl.totalDuration()), 0, 'f', 3);
    }

    QString filterGraph = videoFilterParts.join(";");
    if (hasAudio) filterGraph += ";" + audioFilterParts.join(";");
    // (Text layers are no longer a separate post-concat overlay pass — they
    // are composited per-segment above, in track order, alongside video and
    // image layers.)
    if (filterGraphDebugOut) *filterGraphDebugOut = filterGraph;

    QStringList args;
    args << "-y" << "-hide_banner";
    // Text-layer inputs go FIRST (indices 0..textPngPaths.size()-1) — same
    // still-image loop trick as an image asset. inputIndexFor() above
    // offsets every ordinary media input's index by textPngPaths.size()
    // to match this order.
    if (!textPngPaths.isEmpty()) {
        const double totalSec = ticksToSeconds(tl.totalDuration());
        for (const auto& png : textPngPaths) {
            args << "-loop" << "1" << "-t" << QString::number(std::max(totalSec, 0.1), 'f', 3) << "-i" << png;
        }
    }
    for (const auto& path : inputPaths) {
        if (isImageByPath.value(path, false)) {
            // "-loop 1 -t <dur>" keeps the image demuxer from running out of
            // frames before the filter graph is done with it. Without "-t"
            // the demuxer may loop indefinitely and stall the export.
            const double totalSec = ticksToSeconds(tl.totalDuration());
            args << "-loop" << "1" << "-t" << QString::number(std::max(totalSec, 0.1), 'f', 3);
        }
        args << "-i" << path;
    }

    // Write the filter graph to a temp file and use the new
    // "-/filter_complex <file>" syntax (FFmpeg 8.x). This avoids the
    // hard shell-argument-length limit on very large graphs, avoids
    // escaping issues with special characters in the graph, AND avoids
    // the deprecation warning emitted by the old -filter_complex_script
    // flag since FFmpeg 7.1.
    const QString filterScriptPath = m_tempDir.filePath("hc_filter_complex.txt");
    QFile filterFile(filterScriptPath);
    bool useFilterFile = false;
    if (filterFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        filterFile.write(filterGraph.toUtf8());
        filterFile.close();
        // "-/filter_complex <file>" is the supported replacement for the
        // deprecated "-filter_complex_script <file>" in FFmpeg >= 7.1.
        // For older FFmpeg builds that don't know "-/", fall back to inline
        // "-filter_complex" (graph is written inline; escaping is handled by
        // QProcess which passes each arg as a separate argv element).
        args << "-/filter_complex" << filterScriptPath;
        useFilterFile = true;
    }
    if (!useFilterFile) {
        args << "-filter_complex" << filterGraph;
    }

    args << "-map" << "[vout]";
    if (hasAudio && s.audioCodec != "none") args << "-map" << "[aout]";

    // Thread allocation strategy:
    // The filter_complex graph and filter_complex_threads ALWAYS stay at 1
    // to keep graph RAM bounded — a complex overlay/blend filter_complex can
    // itself consume several hundred MB per extra thread on a 1080p timeline.
    // The global -threads flag (codec decode/encode pool) gets 2 workers in
    // normal mode: enough to pipeline I/O with encoding without the 4-thread
    // reference-frame pool that caused the OOM crash.
    // In low-RAM mode everything stays at 1 and max_alloc is capped.
    args << "-filter_threads" << "1"
         << "-filter_complex_threads" << "1";
    if (lowMem) {
        args << "-threads" << "1";
        // Refuse unusually large single allocations before Linux has to
        // reclaim the entire desktop. This turns catastrophic OOM pressure
        // into a controlled FFmpeg allocation failure instead.
        args << "-max_alloc" << "134217728"; // 128 MiB
    } else {
        // 2 global threads on multi-core machines: one for demux/decode,
        // one for encode. More than 2 creates large reference-frame pools
        // inside libx264 (one full 1080p YUV buffer per thread × 16 ref
        // frames). On single-core machines a single thread avoids pointless
        // context-switch overhead between the decode and encode stages.
        const int cores = std::max(1, systeminfo::cpuCoreCount());
        args << "-threads" << (cores >= 2 ? "2" : "1");
    }

    // Video encoding flags
    if (s.videoCodec == "none") {
        args << "-vn";
    } else {
        QString vcodec = s.videoCodec.isEmpty() ? "libx264" : s.videoCodec;
        if (lowMem && vcodec == "libx265") {
            // x265 is substantially more memory-hungry even with a single
            // worker because of its per-frame coding structures. In the
            // automatic low-RAM profile, prefer x264 for stability.
            qWarning() << "[Exporter] Low-memory auto fallback: libx265 -> libx264";
            vcodec = "libx264";
        }
        args << "-c:v" << vcodec;
        if (!s.preset.isEmpty() && vcodec != "copy" && vcodec != "prores_ks") {
            QString preset = s.preset;
            if (lowMem && preset == "medium") preset = "veryfast";
            args << "-preset" << preset;
        }
        if (s.rateControlMode == "crf" && vcodec != "copy" && vcodec != "prores_ks") {
            args << "-crf" << QString::number(s.crf);
        } else if (vcodec != "copy" && vcodec != "prores_ks") {
            args << "-b:v" << QString("%1k").arg(s.videoBitrateKbps);
            // VBV window: maxrate at 1.2× target is tight enough to honor the
            // user's chosen bitrate without letting libx264 spend 3× on a
            // complex scene. bufsize = 1× bitrate (1 second of buffer) keeps
            // the per-thread VBV buffer allocation at a sane ~1.5 MB for
            // 1080p instead of the ~3 MB that 2× caused, which multiplied by
            // 16 reference frames × 2–4 threads was the root cause of OOM.
            const int maxrateKbps = static_cast<int>(s.videoBitrateKbps * 1.2);
            const int bufsizeKbps = s.videoBitrateKbps;  // 1× = 1 second window
            args << "-maxrate" << QString("%1k").arg(maxrateKbps);
            args << "-bufsize" << QString("%1k").arg(bufsizeKbps);
        }
        if (vcodec == "libx265") {
            // x265 creates its own internal thread pool separate from the
            // global -threads limit. Explicitly bound it to 1 worker always
            // (not just lowMem) to prevent the 8-thread pool OOM on export.
            if (lowMem) {
                args << "-x265-params"
                     << "pools=1:frame-threads=1:wpp=0:lookahead-threads=1:pmode=0:pme=0:rc-lookahead=10";
            } else {
                args << "-x265-params"
                     << "pools=1:frame-threads=1:wpp=1:lookahead-threads=1:rc-lookahead=20";
            }
        } else if (vcodec == "libx264") {
            // Always set x264 internal threads explicitly so it doesn't
            // spawn more workers than the global -threads value. In low-RAM
            // mode go fully single-threaded; otherwise allow 2 internal
            // threads (CABAC + encode pipeline) without the 4-thread pool.
            if (lowMem) {
                args << "-x264-params" << "threads=1:lookahead_threads=1:sync_lookahead=0:rc-lookahead=10";
            } else {
                args << "-x264-params" << "threads=2:lookahead_threads=1:sync_lookahead=0:rc-lookahead=20";
            }
        }
        if (vcodec != "copy") {
            args << "-pix_fmt" << (s.pixelFormat.isEmpty() ? "yuv420p" : s.pixelFormat);
            args << "-r" << QString::number(s.frameRate, 'f', 3);
        }
    }

    // Audio encoding flags
    if (hasAudio && s.audioCodec != "none") {
        const QString acodec = s.audioCodec.isEmpty() ? "aac" : s.audioCodec;
        args << "-c:a" << acodec;
        if (acodec != "pcm_s16le" && acodec != "copy" && acodec != "flac") {
            args << "-b:a" << QString("%1k").arg(s.audioBitrateKbps);
        }
        if (s.audioSampleRate > 0) {
            args << "-ar" << QString::number(s.audioSampleRate);
        }
        if (s.audioChannels > 0) {
            args << "-ac" << QString::number(s.audioChannels);
        }
    } else {
        args << "-an";
    }

    if (s.outputPath.endsWith(".mp4", Qt::CaseInsensitive) || s.outputPath.endsWith(".mov", Qt::CaseInsensitive)) {
        args << "-movflags" << "+faststart";
    }

    // The segment-based filter graph can produce a few extra frames at
    // segment boundaries because each `color`/`trim` segment is quantized to
    // the filter's frame grid.  Normally `concat` keeps this within a frame,
    // but with many boundaries FFmpeg can accumulate the rounding into a
    // noticeably longer output (e.g. a ~46 s timeline becoming ~1 minute).
    // The timeline is the authoritative duration, so clamp the FINAL muxed
    // output to exactly that duration.  This is deliberately an output
    // option (after the filter/codec options), not an input `-t`, so it cuts
    // only the rendered result and never changes source trimming.
    const double outputDurationSec = ticksToSeconds(m_totalDurationTicks);
    if (outputDurationSec > 0.000001) {
        args << "-t" << QString::number(outputDurationSec, 'f', 6);
    }

    args << "-progress" << "pipe:1" << "-nostats";
    args << s.outputPath;
    return args;
}

void Exporter::start(const Settings& settings) {
    if (m_process) return;

    m_finishedEmitted = false;
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_lastFps = 0.0f;
    m_lastSpeed = 0.0f;

    // Ensure output parent directory exists
    const QString parentDirPath = QFileInfo(settings.outputPath).absolutePath();
    if (!parentDirPath.isEmpty()) {
        QDir().mkpath(parentDirPath);
    }

    QString filterDebug;
    const QStringList args = buildFfmpegArgs(settings, &filterDebug);
    qDebug().noquote() << "[Exporter] ffmpeg" << args.join(' ');

    // Robust ffmpeg executable lookup
    QString ffmpegProg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpegProg.isEmpty()) {
        for (const char* candidate : {"/usr/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/snap/bin/ffmpeg", "/bin/ffmpeg"}) {
            if (QFileInfo::exists(candidate) && QFileInfo(candidate).isExecutable()) {
                ffmpegProg = candidate;
                break;
            }
        }
    }
    if (ffmpegProg.isEmpty()) ffmpegProg = "ffmpeg";

    m_process = new QProcess(this);
    m_process->setProgram(ffmpegProg);
    m_process->setArguments(args);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Exporter::onReadyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        if (!m_process) return;
        const QByteArray err = m_process->readAllStandardError();
        if (!err.isEmpty()) {
            m_stderrBuffer += err;
            qWarning().noquote() << "[Exporter][ffmpeg]" << QString::fromLocal8Bit(err).trimmed();
        }
    });
    connect(m_process, &QProcess::finished, this, &Exporter::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &Exporter::onProcessErrorOccurred);
    m_process->start();
}

void Exporter::cancel() {
    if (!m_process) return;
    // Graceful shutdown: send SIGTERM first so ffmpeg can flush its output
    // buffers and clean up its temp files. If it doesn't exit within 3 s,
    // escalate to SIGKILL. Never call deleteLater() while the process is still
    // running — that is the root cause of the
    // "QProcess: Destroyed while process still running" warning.
    m_process->terminate();
    if (!m_process->waitForFinished(3000)) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
    m_process->deleteLater();
    m_process = nullptr;
}

void Exporter::onReadyReadStandardOutput() {
    if (!m_process) return;
    m_stdoutBuffer += m_process->readAllStandardOutput();

    // FFmpeg -progress emits newline-separated "key=value" lines.
    // We collect all keys from the current "progress block" (ends when we
    // see "progress=continue" or "progress=end") and emit one progress signal
    // per complete block so the UI updates smoothly even at high frame rates.
    double pendingFrac = -1.0;
    QString pendingInfo;

    int newlineIdx;
    while ((newlineIdx = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newlineIdx).trimmed();
        m_stdoutBuffer.remove(0, newlineIdx + 1);

        if (line.startsWith("out_time_us=")) {
            const qint64 outTimeUs = line.mid(QByteArrayLiteral("out_time_us=").size()).toLongLong();
            const double totalSec = ticksToSeconds(m_totalDurationTicks);
            if (totalSec > 0.0001 && outTimeUs > 0) {
                pendingFrac = std::clamp(static_cast<double>(outTimeUs) / 1'000'000.0 / totalSec, 0.0, 1.0);
            }
        } else if (line.startsWith("fps=")) {
            const QByteArray val = line.mid(4).trimmed();
            if (!val.isEmpty() && val != "0") {
                m_lastFps = val.toFloat();
            }
        } else if (line.startsWith("speed=")) {
            QByteArray val = line.mid(6).trimmed();
            val.replace('x', "");
            if (!val.isEmpty() && val != "0") {
                m_lastSpeed = val.toFloat();
            }
        } else if (line.startsWith("progress=")) {
            // Build a compact status string: "45 fps | 1.50x"
            QStringList infoParts;
            if (m_lastFps > 0.5f)  infoParts << QStringLiteral("%1 fps").arg(qRound(m_lastFps));
            if (m_lastSpeed > 0.0f) infoParts << QStringLiteral("%1x").arg(m_lastSpeed, 0, 'f', 2);
            pendingInfo = infoParts.join(QStringLiteral(" | "));
        }
    }

    if (pendingFrac >= 0.0) {
        emit progress(pendingFrac, pendingInfo);
    }
}

void Exporter::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    if (m_finishedEmitted) return;
    m_finishedEmitted = true;
    const bool success = (status == QProcess::NormalExit && exitCode == 0);

    QString lastErrorLine;
    if (!success && !m_stderrBuffer.isEmpty()) {
        const QString errStr = QString::fromLocal8Bit(m_stderrBuffer);
        const QStringList lines = errStr.split('\n', Qt::SkipEmptyParts);
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            const QString l = it->trimmed();
            if (!l.isEmpty() && !l.startsWith("frame=") && !l.startsWith("video:") && !l.startsWith("size=") && !l.startsWith("Last message repeated")) {
                lastErrorLine = l;
                break;
            }
        }
    }

    const QString message = success
        ? QStringLiteral("Xuất video thành công")
        : (lastErrorLine.isEmpty()
            ? QStringLiteral("FFmpeg thoát với mã lỗi %1").arg(exitCode)
            : QStringLiteral("Lỗi FFmpeg: %1").arg(lastErrorLine));

    if (m_process) { m_process->deleteLater(); m_process = nullptr; }
    emit progress(success ? 1.0 : 0.0, QString());
    emit finished(success, message);
}

void Exporter::onProcessErrorOccurred(QProcess::ProcessError error) {
    if (m_finishedEmitted) return;
    m_finishedEmitted = true;
    const QString errStr = m_process ? m_process->errorString() : QStringLiteral("Unknown");
    if (m_process) { m_process->deleteLater(); m_process = nullptr; }
    emit finished(false, QStringLiteral("Không thể khởi chạy FFmpeg: %1 (Error code %2)").arg(errStr).arg(error));
}

} // namespace hc
