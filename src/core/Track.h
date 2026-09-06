#pragma once
#include <QString>
#include <QUuid>
#include <vector>
#include <optional>
#include <algorithm>
#include "Clip.h"

namespace hc {

// Two track kinds: Visual carries compositable clips (video, image, text),
// Audio carries audible clips (video-with-audio, audio-only).  Text clips
// now live on Visual tracks alongside video and image clips instead of
// being siloed into a separate Text track type.
enum class TrackType { Visual, Audio };

// A Track owns its clips and keeps them sorted by timelineStart.
// Overlap resolution for visual tracks is "last clip wins" at a given
// time when tracks are stacked (higher track index = drawn on top);
// Track itself does not prevent two clips from overlapping in time — the
// UI / model layer is responsible for nudging clips during drag operations.
class Track {
public:
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    TrackType type = TrackType::Visual;
    QString name;
    bool muted = false;
    bool hidden = false;
    bool locked = false;

    const std::vector<Clip>& clips() const { return m_clips; }
    std::vector<Clip>& clips() { return m_clips; }

    Clip* addClip(Clip clip) {
        // Remember the id BEFORE sorting — sortClips() can move the clip we
        // just pushed to anywhere in the vector (e.g. it lands before an
        // existing clip with a later timelineStart), so `m_clips.back()`
        // is not reliably "the clip we just added" once the sort has run.
        // Looking it up by id afterwards always finds the right one.
        const QString newId = clip.id;
        m_clips.push_back(std::move(clip));
        sortClips();
        return findClip(newId);
    }

    bool removeClip(const QString& clipId) {
        const auto before = m_clips.size();
        m_clips.erase(std::remove_if(m_clips.begin(), m_clips.end(),
                                      [&](const Clip& c) { return c.id == clipId; }),
                      m_clips.end());
        return m_clips.size() != before;
    }

    Clip* findClip(const QString& clipId) {
        for (auto& c : m_clips) {
            if (c.id == clipId) return &c;
        }
        return nullptr;
    }

    // Returns the clip active at timeline time `t`, if any. When clips
    // overlap (only possible transiently during a drag) the last one
    // in source order (== most recently added / rightmost) wins.
    const Clip* clipAt(Ticks t) const {
        const Clip* result = nullptr;
        for (const auto& c : m_clips) {
            if (c.containsTimelineTime(t)) result = &c;
        }
        return result;
    }

    Ticks totalDuration() const {
        Ticks maxEnd = 0;
        for (const auto& c : m_clips) maxEnd = std::max(maxEnd, c.timelineEnd());
        return maxEnd;
    }

    void sortClips() {
        std::sort(m_clips.begin(), m_clips.end(),
                  [](const Clip& a, const Clip& b) { return a.timelineStart < b.timelineStart; });
    }

private:
    std::vector<Clip> m_clips;
};

} // namespace hc
