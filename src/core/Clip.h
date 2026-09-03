#pragma once
#include <QString>
#include <QUuid>
#include <QList>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include "TimeTypes.h"
#include "MediaAsset.h"

namespace hc {

enum class ClipType { Video, Audio, Image, Text };

// Compositing blend mode for visual clips.
// Note: simple OpenGL-blend modes (Normal..Lighten) are handled via glBlendFunc.
// Advanced modes (HardLight..HSLLuminosity) require a shader-based composite pass
// and are applied through the effect pipeline in GLVideoWidget.
enum class BlendMode {
    Normal,       // default: alpha blend (src-over)
    Multiply,     // multiply colour channels
    Screen,       // inverse multiply (brightening)
    Overlay,      // multiply + screen combo
    Add,          // additive blend
    Subtract,     // subtractive blend
    Darken,       // min of each channel
    Lighten,      // max of each channel
    HardLight,    // like Overlay but reversed
    SoftLight,    // soft Overlay variant
    Difference,   // absolute difference of channels
    Exclusion,    // softer Difference
    Dodge,        // lighten (colour dodge)
    Burn,         // darken (colour burn)
    Saturate,     // Saturate blend
    HSLHue,       // HSL Hue
    HSLSaturation,// HSL Saturation
    HSLColor,     // HSL Color
    HSLLuminosity // HSL Luminosity
};

// Named parameter for a visual effect (blur radius, brightness, etc.).
struct EffectParameter {
    QString name;    // e.g. "radius", "amount", "threshold"
    double value = 0.0;
};

// A single visual effect that can be applied to a clip.
// Supported types (applied as GPU post-process passes in GLVideoWidget):
//   "brightness"  — amount: -1.0 (black) .. 0.0 (no change) .. +1.0 (white)
//   "contrast"    — amount:  0.0 (flat)  .. 1.0 (no change) .. 3.0 (high)
//   "saturation"  — amount:  0.0 (grey)  .. 1.0 (no change) .. 3.0 (vivid)
//   "hue_rotate"  — degrees: 0..360
//   "blur"        — radius: 0..30 (Gaussian, multi-pass)
//   "sharpen"     — amount: 0..3
//   "vignette"    — strength: 0..1, radius: 0..1
//   "invert"      — (no params)
//   "sepia"       — amount: 0..1
//   "color_grade" — lift_r/g/b, gamma_r/g/b, gain_r/g/b each -1..+1
struct Effect {
    QString type;
    bool enabled = true;
    std::vector<EffectParameter> params;

    double paramValue(const QString& name, double fallback = 0.0) const {
        for (const auto& p : params)
            if (p.name == name) return p.value;
        return fallback;
    }
};

// Position/scale/rotation used to composite this clip within the output
// frame. Defaults are the identity transform — a clip fitted (letterboxed)
// to fill the whole frame, exactly like before Free Transform existed — so
// old projects and clips that never touch Transform render unchanged.
// Coordinates are normalized to the OUTPUT frame, independent of the
// clip's own source resolution:
//   x, y          offset of the clip's center from the frame center, in
//                 units of half the frame width/height (so +1.0 on x moves
//                 the center fully to the right edge).
//   scaleX/scaleY multiply the clip's normal letterboxed-fit size
//                 (1.0 = fills the frame on that axis; 0.5 = half size).
//   rotationDeg   clockwise rotation, in degrees, about the clip's center.
struct Transform {
    double x = 0.0;
    double y = 0.0;
    double scaleX = 1.0;
    double scaleY = 1.0;
    double rotationDeg = 0.0;
    double opacity = 1.0;

    bool isIdentity() const {
        return x == 0.0 && y == 0.0 && scaleX == 1.0 && scaleY == 1.0 && rotationDeg == 0.0 && opacity == 1.0;
    }
};

// A single keyframe on a clip's Free Transform ("layer") animation track.
// `time` is relative to the clip's own timelineStart (ticks), so keyframes
// stay attached to the clip when it's dragged to a new position.
struct TransformKeyframe {
    Ticks time = 0;
    Transform value;
};

// A Clip is a *reference* to a MediaAsset trimmed to [sourceIn, sourceOut)
// and placed on the timeline starting at `timelineStart`. Multiple clips can
// reference the same MediaAsset (e.g. the same file cut into several pieces).
class Clip {
public:
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString assetId;            // MediaAsset::id this clip reads from
    ClipType type = ClipType::Video;

    Ticks sourceIn = 0;         // trim in-point, relative to source media
    Ticks sourceOut = 0;        // trim out-point, relative to source media
    Ticks timelineStart = 0;    // position on the timeline

    double speed = 1.0;         // playback speed multiplier (1.0 = normal)
    double volume = 1.0;        // 0..1 (audio-bearing clips only)
    double opacity = 1.0;       // 0..1 (video/image clips only)
    bool muted = false;

    // Free Transform: position/scale/rotation within the output frame.
    Transform transform;

    // Compositing blend mode. Normal (alpha blend) by default.
    BlendMode blendMode = BlendMode::Normal;

    // Visual effects chain, applied in order.
    std::vector<Effect> effects;

    // Simple linear fades, expressed in ticks measured from each edge of the clip.
    Ticks fadeInDuration = 0;
    Ticks fadeOutDuration = 0;

    // Crossfade transition with the PREVIOUS clip on the same Visual track.
    // Non-zero means: this clip's timelineStart has been pulled `transitionInDuration`
    // ticks earlier so it deliberately overlaps the outgoing clip's tail —
    // during that overlap window this clip fades in (weight 0→1) while the
    // outgoing clip stays fully opaque underneath, giving a standard
    // cross-dissolve. Set/cleared together with timelineStart by
    // TimelineWidget's transition-marker double-click — see
    // Timeline::activeVisualClipsAt() for where the weight is applied.
    Ticks transitionInDuration = 0;

    // Audio processing chain (EQ / noise reduction / compressor), applied to
    // this clip's own audio before volume/fades/mixing. See
    // audio/AudioFilterDesc.h for how this turns into an actual ffmpeg
    // filter string — the SAME string drives both the ffmpeg CLI export
    // (Exporter) and the real-time libavfilter graph used for preview
    // (PlaybackController's AudioFilterChain), so what you hear while
    // editing is what ends up in the exported file.
    struct AudioFilterSettings {
        // 3-band EQ, gain in dB at fixed center frequencies (~100Hz /
        // ~1kHz / ~8kHz). 0 = no change on that band.
        double eqLowDb = 0.0;
        double eqMidDb = 0.0;
        double eqHighDb = 0.0;

        // FFT-based noise reduction (ffmpeg `afftdn`). amountDb is how much
        // noise to remove, roughly 0(off)..97, ffmpeg default 12.
        bool denoiseEnabled = false;
        double denoiseAmountDb = 12.0;

        // Dynamic range compressor (ffmpeg `acompressor`).
        bool compressorEnabled = false;
        double compressorThresholdDb = -18.0; // level where compression kicks in
        double compressorRatio = 3.0;          // e.g. 3 = 3:1

        bool isDefault() const {
            return eqLowDb == 0.0 && eqMidDb == 0.0 && eqHighDb == 0.0 &&
                   !denoiseEnabled && !compressorEnabled;
        }
    };
    AudioFilterSettings audioFilters;

    QString displayLabel;       // caption/content for Text clips, otherwise unused

    // Text Layer Styling (used when type == ClipType::Text)
    QString textFontFamily = "Segoe UI";   // Font family
    int textFontSize = 64;                 // Base font size in pixels (relative to 1080p)
    QString textFontColor = "#FFFFFF";     // Text fill color (hex format)
    bool textBold = false;                 // Bold
    bool textItalic = false;               // Italic
    bool textUnderline = false;            // Underline
    int textAlignment = 0;                 // 0: Center, 1: Left, 2: Right
    bool textOutlineEnabled = true;        // Outline stroke enabled
    QString textOutlineColor = "#000000";  // Outline stroke color
    int textOutlineWidth = 2;              // Outline width in pixels
    bool textBackgroundEnabled = false;    // Background rectangle enabled
    QString textBackgroundColor = "#00000080"; // Background fill color
    int textPadding = 12;                  // Padding around text in pixels

    // --- Keyframe animation support (layer/Transform keyframes) ---
    // When empty, the clip's Free Transform is the static `transform`
    // field above (old behaviour, unchanged). When non-empty, the
    // effective transform at any timeline time is linearly interpolated
    // between the surrounding keyframes — this animates position, scale
    // and rotation over the clip's lifetime (e.g. a PIP layer that slides
    // in from off-screen). Kept sorted by `time` ascending.
    QList<TransformKeyframe> transformKeyframes;

    bool hasTransformKeyframes() const { return !transformKeyframes.isEmpty(); }

    // Effective Free Transform at `timelineTime` (absolute timeline ticks).
    // Falls back to the static `transform` field when there are no
    // keyframes, so clips that never touch keyframing render exactly as
    // before. Both PlaybackController (preview) and Exporter (render) call
    // this so the exported video always matches what's on screen.
    Transform transformAt(Ticks timelineTime) const {
        if (transformKeyframes.isEmpty()) return transform;
        const Ticks relTime = timelineTime - timelineStart;

        if (relTime <= transformKeyframes.first().time) return transformKeyframes.first().value;
        if (relTime >= transformKeyframes.last().time) return transformKeyframes.last().value;

        for (int i = 0; i + 1 < transformKeyframes.size(); ++i) {
            const TransformKeyframe& a = transformKeyframes[i];
            const TransformKeyframe& b = transformKeyframes[i + 1];
            if (relTime < a.time || relTime > b.time) continue;
            const Ticks span = b.time - a.time;
            const double f = span > 0 ? static_cast<double>(relTime - a.time) / static_cast<double>(span) : 0.0;
            Transform out;
            out.x = a.value.x + (b.value.x - a.value.x) * f;
            out.y = a.value.y + (b.value.y - a.value.y) * f;
            out.scaleX = a.value.scaleX + (b.value.scaleX - a.value.scaleX) * f;
            out.scaleY = a.value.scaleY + (b.value.scaleY - a.value.scaleY) * f;
            out.rotationDeg = a.value.rotationDeg + (b.value.rotationDeg - a.value.rotationDeg) * f;
            out.opacity = a.value.opacity + (b.value.opacity - a.value.opacity) * f;
            return out;
        }
        return transformKeyframes.last().value;
    }

    double opacityAt(Ticks timelineTime) const {
        if (transformKeyframes.isEmpty()) return opacity;
        return transformAt(timelineTime).opacity;
    }

    // Adds a keyframe at `relTime` (ticks, relative to clip start) holding
    // `value`, replacing any existing keyframe at that exact time. Keeps
    // the keyframe list sorted.
    void setTransformKeyframe(Ticks relTime, const Transform& value) {
        relTime = std::max<Ticks>(0, relTime);
        for (auto& kf : transformKeyframes) {
            if (kf.time == relTime) { kf.value = value; return; }
        }
        transformKeyframes.push_back(TransformKeyframe{relTime, value});
        std::sort(transformKeyframes.begin(), transformKeyframes.end(),
                  [](const TransformKeyframe& a, const TransformKeyframe& b) { return a.time < b.time; });
    }

    // Removes the keyframe nearest `relTime` if one sits within
    // `toleranceTicks` (default ~1 frame at 30fps). Returns true if removed.
    bool removeTransformKeyframeNear(Ticks relTime, Ticks toleranceTicks = kTicksPerSecond / 30) {
        for (int i = 0; i < transformKeyframes.size(); ++i) {
            if (std::llabs(static_cast<long long>(transformKeyframes[i].time - relTime)) <= toleranceTicks) {
                transformKeyframes.removeAt(i);
                return true;
            }
        }
        return false;
    }

    // True if a keyframe already sits within `toleranceTicks` of `relTime`.
    bool hasTransformKeyframeNear(Ticks relTime, Ticks toleranceTicks = kTicksPerSecond / 30) const {
        for (const auto& kf : transformKeyframes) {
            if (std::llabs(static_cast<long long>(kf.time - relTime)) <= toleranceTicks) return true;
        }
        return false;
    }

    Ticks sourceDuration() const { return sourceOut - sourceIn; }
    Ticks timelineDuration() const {
        return speed > 0.0001 ? static_cast<Ticks>(sourceDuration() / speed) : 0;
    }
    Ticks timelineEnd() const { return timelineStart + timelineDuration(); }

    bool containsTimelineTime(Ticks t) const {
        return t >= timelineStart && t < timelineEnd();
    }

    // Whether this clip is a "static visual" — image or text — whose decoded
    // frame does not change over its duration. The TextureCache exploits this
    // to upload the GPU texture only once instead of every tick.
    bool isStaticVisual() const {
        return type == ClipType::Image || type == ClipType::Text;
    }

    // Whether this clip produces a visual frame (video / image / text).
    bool hasVisual() const {
        return type == ClipType::Video || type == ClipType::Image || type == ClipType::Text;
    }

    // Converts a timeline position (already known to be inside this clip)
    // to the corresponding position in the source media, honouring speed.
    Ticks timelineTimeToSourceTime(Ticks timelineTime) const {
        const Ticks offset = timelineTime - timelineStart;
        return sourceIn + static_cast<Ticks>(offset * speed);
    }
};

} // namespace hc
