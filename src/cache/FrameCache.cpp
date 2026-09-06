#include "FrameCache.h"
#include <cmath>
#include <algorithm>

namespace hc {

FrameCache::FrameCache(size_t maxBytes) : m_maxBytes(maxBytes) {}

int64_t FrameCache::quantize(Ticks pts, double frameRate) {
    if (frameRate <= 0.0) return pts; // degrade to raw pts if fps unknown (still exact-match usable)
    const double idx = static_cast<double>(pts) * frameRate / static_cast<double>(kTicksPerSecond);
    return static_cast<int64_t>(std::llround(idx));
}

bool FrameCache::get(const QString& assetId, int64_t frameIndex, VideoFrame& out) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const Key key{assetId, frameIndex};
    auto it = m_map.find(key);
    if (it == m_map.end()) return false;
    out = it->second.frame;
    m_lru.erase(it->second.lruIt);
    m_lru.push_front(key);
    it->second.lruIt = m_lru.begin();
    return true;
}

void FrameCache::put(const QString& assetId, int64_t frameIndex, const VideoFrame& frame) {
    if (!frame.isValid()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    const Key key{assetId, frameIndex};

    auto existing = m_map.find(key);
    if (existing != m_map.end()) {
        m_currentBytes -= existing->second.bytes;
        m_lru.erase(existing->second.lruIt);
        m_map.erase(existing);
    }

    const size_t bytes = frameBytes(frame);
    // A single frame larger than the whole budget just isn't cached — this
    // only happens with a tiny maxBytes configuration, never in practice.
    if (bytes > m_maxBytes) return;

    m_lru.push_front(key);
    Node node;
    node.frame = frame;
    node.bytes = bytes;
    node.lruIt = m_lru.begin();
    m_map.emplace(key, std::move(node));
    m_currentBytes += bytes;

    evictIfNeeded();
}

void FrameCache::setMaxBytes(size_t bytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    // Guard against a nonsensically small budget (a single 1080p YUV420P
    // frame is ~3 MB; anything smaller would evict everything immediately).
    m_maxBytes = std::max<size_t>(bytes, 8ull * 1024ull * 1024ull);
    evictIfNeeded();
}

void FrameCache::evictIfNeeded() {
    while (m_currentBytes > m_maxBytes && !m_lru.empty()) {
        const Key victim = m_lru.back();
        m_lru.pop_back();
        auto it = m_map.find(victim);
        if (it != m_map.end()) {
            m_currentBytes -= it->second.bytes;
            m_map.erase(it);
        }
    }
}

void FrameCache::invalidateAsset(const QString& assetId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto it = m_map.begin(); it != m_map.end();) {
        if (it->first.assetId == assetId) {
            m_currentBytes -= it->second.bytes;
            m_lru.erase(it->second.lruIt);
            it = m_map.erase(it);
        } else {
            ++it;
        }
    }
}

void FrameCache::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_map.clear();
    m_lru.clear();
    m_currentBytes = 0;
}

size_t FrameCache::currentBytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentBytes;
}

size_t FrameCache::entryCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_map.size();
}

} // namespace hc
