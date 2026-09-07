#pragma once
#include <QObject>
#include <QUuid>
#include <vector>
#include <algorithm>
#include "Track.h"

namespace hc {

// Timeline owns the ordered list of Tracks and is the single source of
// truth the PlaybackController and Exporter read from. It emits Qt signals
// so the TimelineWidget can repaint incrementally instead of polling.
//
// Tracks are now unified into two types: Visual and Audio.  Visual tracks
// carry video, image, AND text clips — compositing treats all three as
// layers that get drawn bottom-to-top.  Audio tracks carry audio-only or
// video-with-audio clips for mixing.
class Timeline : public QObject {
    Q_OBJECT
public:
    explicit Timeline(QObject* parent = nullptr) : QObject(parent) {}

    double frameRate = 30.0;
    int videoWidth = 1920;
    int videoHeight = 1080;

    std::vector<Track>& tracks() { return m_tracks; }
    const std::vector<Track>& tracks() const { return m_tracks; }

    Track& addTrack(TrackType type, const QString& name) {
        Track t;
        t.type = type;
        t.name = name;
        m_tracks.push_back(std::move(t));
        emit tracksChanged();
        return m_tracks.back();
    }

    bool removeTrack(const QString& trackId) {
        const auto before = m_tracks.size();
        m_tracks.erase(std::remove_if(m_tracks.begin(), m_tracks.end(),
                                       [&](const Track& t) { return t.id == trackId; }),
                       m_tracks.end());
        const bool changed = m_tracks.size() != before;
        if (changed) emit tracksChanged();
        return changed;
    }

    Track* findTrack(const QString& trackId) {
        for (auto& t : m_tracks) {
            if (t.id == trackId) return &t;
        }
        return nullptr;
    }

    Ticks totalDuration() const {
        Ticks maxEnd = 0;
        for (const auto& t : m_tracks) maxEnd = std::max(maxEnd, t.totalDuration());
        return maxEnd;
    }

    // ── Transition rendering (shared with PlaybackController + Exporter) ─
    // Progress (0..1) of an incoming clip's transition at time `t`.
    // Returns 1.0 when the time is past the window or there is no transition.
    static double transitionProgress(const Clip& c, Ticks t) {
        if (c.transitionInDuration <= 0) return 1.0;
        const Ticks rel = t - c.timelineStart;
        if (rel <= 0) return 0.0;
        if (rel >= c.transitionInDuration) return 1.0;
        return static_cast<double>(rel) / static_cast<double>(c.transitionInDuration);
    }

    // Per-frame visual state for an incoming clip's transition. The same
    // numbers drive the GL preview and the ffmpeg export, so the two always
    // agree. All fields are identity/no-op when no transition is active.
    struct TransitionVisual {
        double progress = 1.0;     // 0..1 within the transition window
        double opacity = 1.0;      // opacity multiplier for the incoming clip
        double offsetX = 0.0;      // slide offset, Transform units (x: +right)
        double offsetY = 0.0;      // slide offset, Transform units (y: +down)
        double wipeProgress = 1.0; // < 1.0 => wipe reveal fraction
        int wipeDirection = 0;     // 0 L→R, 1 R→L, 2 T→B, 3 B→T
        double colorAlpha = 0.0;   // > 0 => dip-to-color overlay alpha
    };

    static TransitionVisual transitionVisual(const Clip& c, Ticks t) {
        TransitionVisual v;
        v.progress = transitionProgress(c, t);
        if (c.transitionInDuration <= 0 || v.progress >= 1.0) return v;
        const double p = v.progress;
        switch (c.transitionType) {
        case TransitionType::Dissolve:
            v.opacity = p;
            break;
        case TransitionType::Wipe:
            v.wipeProgress = p;
            v.wipeDirection = c.transitionDirection;
            break;
        case TransitionType::Slide: {
            const double k = (1.0 - p) * 2.0; // full canvas width, off-screen at p=0
            switch (c.transitionDirection) {
                case 0: v.offsetX = -k; break; // enters from the left
                case 1: v.offsetX =  k; break; // enters from the right
                case 2: v.offsetY = -k; break; // enters from the top
                case 3: v.offsetY =  k; break; // enters from the bottom
                default: break;
            }
            break;
        }
        case TransitionType::DipToColor:
            // Incoming only starts appearing halfway through, once the solid
            // colour has fully covered the outgoing clip.
            v.opacity = std::clamp(2.0 * p - 1.0, 0.0, 1.0);
            v.colorAlpha = std::clamp(2.0 * p, 0.0, 1.0);
            break;
        }
        return v;
    }

    // ── Visual compositing queries ──────────────────────────────────
    // One layer to composite, paired with a blend weight. The weight is
    // always 1.0 today (all transition math lives in transitionVisual()
    // above); the field is kept so callers that read it keep compiling.
    struct VisualLayer {
        const Clip* clip = nullptr;
        double weight = 1.0;
        // Index of the track this clip sits on within tracks() (needed to
        // know which Effect Layers are "above" it).
        int trackIndex = -1;
        // Effect Layers (adjustment layers) on the tracks ABOVE this clip
        // that are active at the queried time, ordered bottom-to-top. Empty
        // in the common case; feed it to mergedEffects() to get the exact
        // stack the GL preview, the CPU preview and the exporter must apply.
        std::vector<const Clip*> effectLayers;
    };

    // ── Effect Layers (adjustment layers) ────────────────────────────
    // An Effect Layer is a media-less clip (ClipType::EffectLayer) that lives
    // on a Visual track and lends its effect stack to every visual clip on
    // the tracks BELOW it (lower track index == drawn underneath) for as long
    // as the two overlap in time. The layer itself composites nothing, so it
    // never shows up in activeVisualClipsAt(); hidden tracks disable the
    // layers they carry, exactly like they hide ordinary clips.
    //
    // Inherited stacks are applied AFTER the clip's own effects, and layers
    // are applied bottom-to-top, so a stack of two layers reads the same way
    // it does in a classic NLE: the lower layer grades the footage, the upper
    // layer grades the result.
    struct EffectLayerHit {
        const Clip* clip = nullptr;
        int trackIndex = -1;
    };

    // Every Effect Layer active at time `t`, ordered bottom-to-top (ascending
    // track index, then timeline order within a track).
    std::vector<EffectLayerHit> activeEffectLayersAt(Ticks t) const {
        std::vector<EffectLayerHit> out;
        for (int i = 0; i < static_cast<int>(m_tracks.size()); ++i) {
            const Track& track = m_tracks[i];
            if (track.type != TrackType::Visual) continue;
            if (track.hidden) continue;
            for (const auto& c : track.clips()) {
                if (!c.isEffectLayer()) continue;
                if (!c.containsTimelineTime(t)) continue;
                out.push_back(EffectLayerHit{&c, i});
            }
        }
        return out;
    }

    // One Effect Layer's contribution to a specific clip: the layer plus the
    // exact time window where the two overlap. `coversWholeClip` lets the
    // exporter know whether the stack can simply be appended to the clip's
    // own filter chain, or has to be time-gated to the overlap window.
    struct EffectLayerRange {
        const Clip* layer = nullptr;
        int trackIndex = -1;
        Ticks rangeStart = 0;   // absolute timeline ticks: max(clip start, layer start)
        Ticks rangeEnd = 0;     // absolute timeline ticks: min(clip end, layer end)
        bool coversWholeClip = false;
    };

    // Every Effect Layer that overlaps `clip` (which sits on track
    // `clipTrackIndex`), ordered bottom-to-top. Used by the Exporter, which
    // builds ONE filter branch per clip for its whole duration and therefore
    // needs the precise sub-range each inherited stack applies to.
    std::vector<EffectLayerRange> effectLayerRangesFor(const Clip& clip, int clipTrackIndex) const {
        std::vector<EffectLayerRange> out;
        const Ticks clipStart = clip.timelineStart;
        const Ticks clipEnd = clip.timelineEnd();
        if (clipEnd <= clipStart) return out;
        for (int i = std::max(0, clipTrackIndex + 1); i < static_cast<int>(m_tracks.size()); ++i) {
            const Track& track = m_tracks[i];
            if (track.type != TrackType::Visual) continue;
            if (track.hidden) continue;
            for (const auto& c : track.clips()) {
                if (!c.isEffectLayer()) continue;
                if (c.effects.empty()) continue;   // nothing to inherit
                const Ticks a = std::max(clipStart, c.timelineStart);
                const Ticks b = std::min(clipEnd, c.timelineEnd());
                if (b <= a) continue;
                EffectLayerRange r;
                r.layer = &c;
                r.trackIndex = i;
                r.rangeStart = a;
                r.rangeEnd = b;
                r.coversWholeClip = (a <= clipStart && b >= clipEnd);
                out.push_back(r);
            }
        }
        return out;
    }

    // The effect stack a clip is actually rendered with: its own effects
    // first, then each inherited Effect Layer's stack (bottom-to-top).
    static std::vector<Effect> mergedEffects(const Clip& clip,
                                             const std::vector<const Clip*>& effectLayers) {
        std::vector<Effect> out = clip.effects;
        for (const Clip* layer : effectLayers) {
            if (!layer) continue;
            for (const auto& eff : layer->effects) out.push_back(eff);
        }
        return out;
    }

    // Convenience wrapper: merged stack for `clip` at time `t`.
    std::vector<Effect> effectiveEffectsAt(const Clip& clip, int clipTrackIndex, Ticks t) const {
        std::vector<const Clip*> layers;
        for (const auto& hit : activeEffectLayersAt(t)) {
            if (hit.trackIndex > clipTrackIndex) layers.push_back(hit.clip);
        }
        return mergedEffects(clip, layers);
    }

    // Index of a track by id, or -1 when unknown. Handy for callers holding a
    // Track& (or a trackId from the UI) that need the stacking order to ask
    // for inherited Effect Layers.
    int trackIndexOf(const QString& trackId) const {
        for (int i = 0; i < static_cast<int>(m_tracks.size()); ++i) {
            if (m_tracks[i].id == trackId) return i;
        }
        return -1;
    }

    // Every *visual* clip (video, image, text) active at time `t`, ordered
    // bottom-to-top (ascending track index, then ascending timelineStart
    // within a track) — the order they should be composited. Each later
    // entry is drawn on top of the previous ones. During a transition, both
    // the outgoing clip and the incoming clip are returned for the same
    // track, back to back. The per-type transition look (dissolve opacity,
    // wipe mask, slide offset, dip-to-color) is applied uniformly by
    // transitionVisual() above so the GL compositor and the ffmpeg exporter
    // stay pixel-consistent; this query only decides *which* clips are on
    // screen and in what order.
    //
    // Effect Layers are never returned here (they composite nothing), but
    // each returned layer carries the Effect Layers above it so the caller
    // can build the inherited effect stack with mergedEffects().
    std::vector<VisualLayer> activeVisualClipsAt(Ticks t) const {
        std::vector<VisualLayer> result;
        // Collect the active Effect Layers once instead of re-scanning every
        // track for every clip (this runs once per rendered frame).
        const std::vector<EffectLayerHit> layers = activeEffectLayersAt(t);
        for (int trackIndex = 0; trackIndex < static_cast<int>(m_tracks.size()); ++trackIndex) {
            const Track& track = m_tracks[trackIndex];
            if (track.type != TrackType::Visual) continue;
            if (track.hidden) continue;
            // track.clips() is kept sorted by timelineStart, so iterating
            // in order naturally draws the outgoing clip of a transition
            // before the incoming one.
            for (const auto& c : track.clips()) {
                if (c.isEffectLayer()) continue;
                if (!c.containsTimelineTime(t)) continue;
                VisualLayer layer;
                layer.clip = &c;
                layer.weight = 1.0;
                layer.trackIndex = trackIndex;
                for (const auto& hit : layers) {
                    if (hit.trackIndex > trackIndex) layer.effectLayers.push_back(hit.clip);
                }
                result.push_back(std::move(layer));
            }
        }
        return result;
    }

    // Convenience: topmost visual clip only (callers that only need one).
    const Clip* topmostVisualClipAt(Ticks t) const {
        for (auto it = m_tracks.rbegin(); it != m_tracks.rend(); ++it) {
            if (it->type != TrackType::Visual) continue;
            if (it->hidden) continue;
            const Clip* c = it->clipAt(t);
            // An Effect Layer is not a visible clip: keep looking underneath.
            if (c && !c->isEffectLayer()) return c;
        }
        return nullptr;
    }

    // Every distinct clip start/end boundary across all visual tracks,
    // clamped to [0, totalDuration()]. Used to split the composited output
    // into flat, single-source segments for export (see Exporter).
    std::vector<Ticks> visualBoundaryTimes() const {
        std::vector<Ticks> bounds{0, totalDuration()};
        for (const auto& t : m_tracks) {
            if (t.type != TrackType::Visual) continue;
            for (const auto& c : t.clips()) {
                bounds.push_back(std::clamp<Ticks>(c.timelineStart, 0, totalDuration()));
                bounds.push_back(std::clamp<Ticks>(c.timelineEnd(), 0, totalDuration()));
            }
        }
        std::sort(bounds.begin(), bounds.end());
        bounds.erase(std::unique(bounds.begin(), bounds.end()), bounds.end());
        return bounds;
    }

    // ── Audio query ──────────────────────────────────────────────────
    // Topmost *audio-bearing* clip at time `t` across Audio tracks.
    const Clip* topmostAudioClipAt(Ticks t) const {
        for (auto it = m_tracks.rbegin(); it != m_tracks.rend(); ++it) {
            if (it->type != TrackType::Audio) continue;
            if (it->hidden || it->locked) continue;
            if (const Clip* c = it->clipAt(t)) return c;
        }
        return nullptr;
    }

    // ── Clip operations ──────────────────────────────────────────────
    // Splits the clip under `clipId` on `track` at timeline time `t`,
    // producing two clips with correctly adjusted in/out points.
    // Returns false if `t` does not fall strictly inside the clip.
    bool splitClip(const QString& trackId, const QString& clipId, Ticks t) {
        Track* track = findTrack(trackId);
        if (!track) return false;
        Clip* clip = track->findClip(clipId);
        if (!clip) return false;
        if (t <= clip->timelineStart || t >= clip->timelineEnd()) return false;

        Clip rightHalf = *clip;
        const Ticks sourceSplitPoint = clip->timelineTimeToSourceTime(t);
        const Ticks splitRelTime = t - clip->timelineStart;

        rightHalf.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        rightHalf.sourceIn = sourceSplitPoint;
        rightHalf.timelineStart = t;
        rightHalf.transitionInDuration = 0;
        rightHalf.fadeInDuration = 0;

        clip->sourceOut = sourceSplitPoint;
        clip->fadeOutDuration = 0;

        // Partition transform keyframes across split point
        if (clip->hasTransformKeyframes()) {
            QList<TransformKeyframe> leftKfs;
            QList<TransformKeyframe> rightKfs;
            const Transform splitTransform = clip->transformAt(t);

            for (const auto& kf : clip->transformKeyframes) {
                if (kf.time < splitRelTime) {
                    leftKfs.push_back(kf);
                } else if (kf.time > splitRelTime) {
                    TransformKeyframe rkf = kf;
                    rkf.time -= splitRelTime;
                    rightKfs.push_back(rkf);
                }
            }

            if (!leftKfs.isEmpty() || !rightKfs.isEmpty()) {
                leftKfs.push_back(TransformKeyframe{splitRelTime, splitTransform});
                rightKfs.push_front(TransformKeyframe{0, splitTransform});
            }

            clip->transformKeyframes = leftKfs;
            rightHalf.transformKeyframes = rightKfs;
        }

        track->addClip(std::move(rightHalf));
        emit clipsChanged(trackId);
        return true;
    }

    void notifyClipChanged(const QString& trackId) { emit clipsChanged(trackId); }

    // ── Ripple delete ───────────────────────────────────────────────
    // Deletes `clipId` and shifts every clip on the same track that starts
    // after it LEFT by the deleted clip's timeline duration, so the gap
    // closes automatically instead of leaving a hole. Returns false if the
    // clip or track doesn't exist. Clips keep their order (sorted by
    // timelineStart) because later clips all shift by the same amount.
    bool rippleDeleteClip(const QString& trackId, const QString& clipId) {
        Track* track = findTrack(trackId);
        if (!track) return false;
        const Clip* clip = track->findClip(clipId);
        if (!clip) return false;
        const Ticks removedStart = clip->timelineStart;
        const Ticks removedDur = clip->timelineDuration();
        if (!track->removeClip(clipId)) return false;
        for (auto& c : track->clips()) {
            if (c.timelineStart > removedStart) {
                c.timelineStart = std::max<Ticks>(0, c.timelineStart - removedDur);
            }
        }
        emit clipsChanged(trackId);
        return true;
    }

signals:
    void tracksChanged();
    void clipsChanged(const QString& trackId);

private:
    std::vector<Track> m_tracks;
};

} // namespace hc
