#pragma once
#include <QString>
#include <QHash>
#include <list>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include "../decode/FrameTypes.h"
#include "../core/TimeTypes.h"

namespace hc {

// FrameCache is an in-memory LRU cache of already-decoded VideoFrame data,
// keyed by (assetId, quantized frame index). This is the "caching" half of
// the anti-lag work: editing is dominated by scrubbing BACK AND FORTH over
// a small window of the timeline (trimming an edit point, nudging a clip,
// replaying the last few seconds), and every one of those revisits used to
// cost a full demuxer seek + forward-decode in Decoder — the single biggest
// source of scrub lag, worst on long-GOP H.264/HEVC sources where decoding
// forward from the nearest keyframe can mean stepping through dozens of
// frames just to reach the one that's actually needed.
//
// PlaybackController checks here BEFORE touching the Decoder. A hit means
// zero FFmpeg calls for that tick. A miss still decodes as before, but the
// resulting frame is inserted here so the next revisit is free.
//
// Not tied to a specific resolution: whatever bytes are decoded (original
// or proxy) get cached as-is, so switching the proxy toggle naturally
// produces different cache entries (see invalidateAsset(), called whenever
// a clip's decode source changes).
//
// Thread safety: mutex-protected so it's safe to share between the GUI
// thread and a future decode thread, mirroring TextureCache's contract.
class FrameCache {
public:
    // maxBytes bounds total resident frame data (not entry count), since a
    // 4K frame and a 960px proxy frame differ by ~10x in size. Default 256MB
    // is enough for several seconds of cached frames per open asset without
    // becoming a memory problem on modest machines.
    explicit FrameCache(size_t maxBytes = 64ull * 1024 * 1024);

    // Quantizes a source-time pts to a stable per-asset frame index so that
    // repeated lookups for "the same frame" (reached via slightly different
    // scrub positions) land on the same cache key.
    static int64_t quantize(Ticks pts, double frameRate);

    // Looks up a cached frame. Returns true and fills `out` on hit (and
    // marks the entry most-recently-used). Returns false on miss.
    bool get(const QString& assetId, int64_t frameIndex, VideoFrame& out);

    // Inserts (or refreshes) a decoded frame into the cache.
    void put(const QString& assetId, int64_t frameIndex, const VideoFrame& frame);

    // Drops every cached frame for one asset — call when that asset's
    // decode source changes (proxy generated/toggled, media relinked) so
    // stale-resolution frames can't be served.
    void invalidateAsset(const QString& assetId);

    void clear();

    size_t currentBytes() const;
    size_t entryCount() const;

private:
    struct Key {
        QString assetId;
        int64_t frameIndex;
        bool operator==(const Key& o) const { return frameIndex == o.frameIndex && assetId == o.assetId; }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return qHash(k.assetId) ^ (std::hash<int64_t>()(k.frameIndex) << 1);
        }
    };
    struct Node {
        VideoFrame frame;
        size_t bytes = 0;
        std::list<Key>::iterator lruIt;
    };

    static size_t frameBytes(const VideoFrame& f) {
        return f.y.size() + f.u.size() + f.v.size();
    }

    void touch(const Key& key);
    void evictIfNeeded();

    mutable std::mutex m_mutex;
    size_t m_maxBytes;
    size_t m_currentBytes = 0;
    std::list<Key> m_lru; // front = most recently used, back = least
    std::unordered_map<Key, Node, KeyHash> m_map;
};

} // namespace hc
