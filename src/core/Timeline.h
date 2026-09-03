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

    // ── Visual compositing queries ──────────────────────────────────
    // One layer to composite, paired with a blend weight for crossfade
    // transitions (see Clip::transitionInDuration). weight == 1.0 outside
    // any transition window — existing callers that don't care about
    // transitions can just ignore the field and get identical behavior
    // to before.
    struct VisualLayer {
        const Clip* clip;
        double weight = 1.0;
    };

    // Every *visual* clip (video, image, text) active at time `t`, ordered
    // bottom-to-top (ascending track index, then ascending timelineStart
    // within a track) — the order they should be composited. Each later
    // entry is drawn on top of the previous ones. During a crossfade, both
    // the outgoing clip (weight 1.0) and the incoming clip (weight ramping
    // 0→1) are returned for the same track, back to back, so a plain
    // alpha-over of the two — done identically by PlaybackController's GL
    // compositor and by Exporter's ffmpeg overlay chain — produces the
    // same linear dissolve in both preview and export.
    // This is the SINGLE query both PlaybackController and Exporter use
    // so the exported video always matches the live preview.
    std::vector<VisualLayer> activeVisualClipsAt(Ticks t) const {
        std::vector<VisualLayer> result;
        for (const auto& track : m_tracks) {
            if (track.type != TrackType::Visual) continue;
            if (track.hidden) continue;
            // track.clips() is kept sorted by timelineStart, so iterating
            // in order naturally draws the outgoing clip of a transition
            // before the incoming one.
            for (const auto& c : track.clips()) {
                if (!c.containsTimelineTime(t)) continue;
                double weight = 1.0;
                if (c.transitionInDuration > 0 && t < c.timelineStart + c.transitionInDuration) {
                    weight = std::clamp(
                        static_cast<double>(t - c.timelineStart) / static_cast<double>(c.transitionInDuration),
                        0.0, 1.0);
                }
                result.push_back(VisualLayer{&c, weight});
            }
        }
        return result;
    }

    // Convenience: topmost visual clip only (callers that only need one).
    const Clip* topmostVisualClipAt(Ticks t) const {
        for (auto it = m_tracks.rbegin(); it != m_tracks.rend(); ++it) {
            if (it->type != TrackType::Visual) continue;
            if (it->hidden) continue;
            if (const Clip* c = it->clipAt(t)) return c;
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

signals:
    void tracksChanged();
    void clipsChanged(const QString& trackId);

private:
    std::vector<Track> m_tracks;
};

} // namespace hc
