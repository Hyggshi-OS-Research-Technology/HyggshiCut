#include "TextureCache.h"

#include <QDebug>
#include <QOpenGLContext>
#include <QThreadPool>
#include <QRunnable>
#include <algorithm>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace hc {
namespace {

void generateMipmap() {
    if (auto* ctx = QOpenGLContext::currentContext()) {
        if (auto* f = ctx->functions()) f->glGenerateMipmap(GL_TEXTURE_2D);
    }
}

size_t mipmappedRGBABytes(int w, int h) {
    size_t total = 0;
    while (w > 0 && h > 0) {
        total += static_cast<size_t>(w) * static_cast<size_t>(h) * 4ull;
        if (w == 1 && h == 1) break;
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
    }
    return total;
}

unsigned int makeRGBATexture() {
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

} // namespace

TextureCache::TextureCache(size_t gpuBudgetBytes, size_t cpuBudgetBytes)
    : m_gpuBudgetBytes(std::max<size_t>(gpuBudgetBytes, 16ull * 1024ull * 1024ull)),
      m_cpuBudgetBytes(std::max<size_t>(cpuBudgetBytes, 32ull * 1024ull * 1024ull)) {
    // Keep image decoding bounded on low-RAM machines. Qt's default thread
    // pool can otherwise create one worker per CPU core, and each image job
    // may temporarily hold multiple decoded RGBA/tile buffers.
    m_pool.setMaxThreadCount(1);
}

TextureCache::~TextureCache() {
    m_pool.waitForDone();
    // A TextureCache owns GL resources, so the normal owner should call clear()
    // while its context is current before destruction.
}

uint64_t TextureCache::tileKey(int x, int y) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) |
           static_cast<uint32_t>(y);
}

bool TextureCache::contains(const QString& assetId) const {
    QMutexLocker lock(&m_mutex);
    auto it = m_states.find(assetId);
    return it != m_states.end() && it->second->entry.valid;
}

bool TextureCache::hasPendingWork() const {
    QMutexLocker lock(&m_mutex);
    if (!m_completed.empty()) return true;
    for (const auto& kv : m_states) {
        if (kv.second->loading) return true;
    }
    return false;
}

bool TextureCache::isLoading(const QString& assetId) const {
    QMutexLocker lock(&m_mutex);
    auto it = m_states.find(assetId);
    return it != m_states.end() && it->second->loading;
}

bool TextureCache::requestAsset(const MediaAsset& asset) {
    if (asset.id.isEmpty() || asset.filePath.isEmpty()) return false;

    {
        QMutexLocker lock(&m_mutex);
        auto it = m_states.find(asset.id);
        if (it != m_states.end()) {
            if (it->second->loading || it->second->entry.valid) return true;
            if (it->second->failed) return false;
        }

        auto state = std::make_shared<AssetState>();
        state->loading = true;
        state->sourcePath = asset.filePath;
        m_states[asset.id] = state;
    }

    enqueueDecode(asset);
    return true;
}

void TextureCache::enqueueDecode(const MediaAsset& asset) {
    struct Job final : QRunnable {
        TextureCache* owner = nullptr;
        MediaAsset asset;
        void run() override {
            QString error;
            auto cpu = TextureCache::decodeAsset(asset, &error);
            QMutexLocker lock(&owner->m_mutex);
            owner->m_completed.push_back(CompletedJob{asset.id, std::move(cpu), error, false});
        }
    };

    auto* job = new Job;
    job->setAutoDelete(true);
    job->owner = this;
    job->asset = asset;
    m_pool.start(job);
}

// Re-decodes just enough to refill the CPU tile pixels of an asset whose
// tiles were previously evicted by evictCpuUntilFits(). Distinct from
// enqueueDecode() only by the `cpuReload` tag on the resulting job, which
// tells pumpUploads() to splice the new pixels back into the existing
// AssetState instead of treating it as a brand-new load.
void TextureCache::requestCpuReload(const QString& assetId, const QString& sourcePath) {
    if (sourcePath.isEmpty()) return;

    struct ReloadJob final : QRunnable {
        TextureCache* owner = nullptr;
        QString assetId;
        MediaAsset asset;
        void run() override {
            QString error;
            auto cpu = TextureCache::decodeAsset(asset, &error);
            QMutexLocker lock(&owner->m_mutex);
            owner->m_completed.push_back(CompletedJob{assetId, std::move(cpu), error, true});
        }
    };

    auto* job = new ReloadJob;
    job->setAutoDelete(true);
    job->owner = this;
    job->assetId = assetId;
    job->asset.id = assetId;
    job->asset.filePath = sourcePath;
    m_pool.start(job);
}

std::shared_ptr<TextureCache::CpuAsset>
TextureCache::decodeAsset(const MediaAsset& asset, QString* errorOut) {
    const QByteArray path = asset.filePath.toUtf8();
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.constData(), nullptr, nullptr) < 0) {
        if (errorOut) *errorOut = QStringLiteral("avformat_open_input failed");
        return nullptr;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        if (errorOut) *errorOut = QStringLiteral("avformat_find_stream_info failed");
        return nullptr;
    }

    const int streamIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        avformat_close_input(&fmt);
        if (errorOut) *errorOut = QStringLiteral("no image/video stream");
        return nullptr;
    }

    AVStream* stream = fmt->streams[streamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        if (errorOut) *errorOut = QStringLiteral("no decoder");
        return nullptr;
    }

    AVCodecContext* cc = avcodec_alloc_context3(codec);
    if (!cc || avcodec_parameters_to_context(cc, stream->codecpar) < 0 || avcodec_open2(cc, codec, nullptr) < 0) {
        avcodec_free_context(&cc);
        avformat_close_input(&fmt);
        if (errorOut) *errorOut = QStringLiteral("codec initialization failed");
        return nullptr;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool gotFrame = false;
    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == streamIndex) {
            const int sendRc = avcodec_send_packet(cc, packet);
            av_packet_unref(packet);
            if (sendRc >= 0 && avcodec_receive_frame(cc, frame) >= 0) {
                gotFrame = true;
                break;
            }
        } else {
            av_packet_unref(packet);
        }
    }

    std::shared_ptr<CpuAsset> result;
    if (gotFrame && frame->width > 0 && frame->height > 0) {
        const int w = frame->width;
        const int h = frame->height;
        const auto fmtDesc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
        if (!fmtDesc) {
            if (errorOut) *errorOut = QStringLiteral("unknown source pixel format");
        } else {
            SwsContext* sws = sws_getContext(
                w, h, static_cast<AVPixelFormat>(frame->format),
                w, h, AV_PIX_FMT_RGBA,
                SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws) {
                if (errorOut) *errorOut = QStringLiteral("sws_getContext failed");
            } else {
                result = std::make_shared<CpuAsset>();
                result->assetId = asset.id;
                result->width = w;
                result->height = h;
                result->tileSize = kDefaultTileSize;
                const size_t fullBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4ull;
                result->tiled = fullBytes >= kHugeImageBytes || w >= kHugeImageDimension || h >= kHugeImageDimension;

                const int stride = w * 4;
                result->rgba.resize(fullBytes);
                uint8_t* dst[4] = {result->rgba.data(), nullptr, nullptr, nullptr};
                int dstStride[4] = {stride, 0, 0, 0};
                sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dstStride);
                sws_freeContext(sws);

                if (result->tiled) {
                    const int cols = (w + result->tileSize - 1) / result->tileSize;
                    const int rows = (h + result->tileSize - 1) / result->tileSize;
                    result->tiles.reserve(static_cast<size_t>(cols) * rows);
                    for (int ty = 0; ty < rows; ++ty) {
                        for (int tx = 0; tx < cols; ++tx) {
                            CpuTile tile;
                            tile.x = tx;
                            tile.y = ty;
                            tile.width = std::min(result->tileSize, w - tx * result->tileSize);
                            tile.height = std::min(result->tileSize, h - ty * result->tileSize);
                            tile.rgba.resize(static_cast<size_t>(tile.width) * tile.height * 4ull);
                            for (int row = 0; row < tile.height; ++row) {
                                const uint8_t* src = result->rgba.data() +
                                    static_cast<size_t>(ty * result->tileSize + row) * stride +
                                    static_cast<size_t>(tx * result->tileSize) * 4ull;
                                uint8_t* dstRow = tile.rgba.data() + static_cast<size_t>(row) * tile.width * 4ull;
                                std::memcpy(dstRow, src, static_cast<size_t>(tile.width) * 4ull);
                            }
                            result->tiles.push_back(std::move(tile));
                        }
                    }
                    // The CPU full-frame staging buffer is no longer needed.
                    result->rgba.clear();
                    result->rgba.shrink_to_fit();
                }
            }
        }
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);

    if (!result && errorOut && errorOut->isEmpty()) *errorOut = QStringLiteral("decode returned no frame");
    return result;
}

int TextureCache::pumpUploads(int maxUploads) {
    if (!QOpenGLContext::currentContext()) return 0;
    maxUploads = std::max(1, maxUploads);
    int done = 0;

    while (done < maxUploads) {
        CompletedJob job;
        {
            QMutexLocker lock(&m_mutex);
            if (m_completed.empty()) break;
            job = std::move(m_completed.front());
            m_completed.pop_front();
        }

        std::shared_ptr<AssetState> state;
        {
            QMutexLocker lock(&m_mutex);
            auto it = m_states.find(job.assetId);
            if (it == m_states.end()) continue;
            state = it->second;
            if (job.cpuReload) {
                // A refill for tiles evicted by evictCpuUntilFits(). The
                // asset's GPU entry/tiles are untouched; only the CPU pixel
                // payload is being restored so uploadTile() can proceed.
                if (!state->cpuReloading) continue;
                state->cpuReloading = false;
                if (!job.cpu || !job.cpu->tiled) continue; // source changed/removed under us
                state->cpu = job.cpu;
            } else {
                if (!state->loading) continue;
                state->loading = false;
                state->cpu = job.cpu;
                state->error = job.error;
                state->failed = !job.cpu;
            }
        }

        if (job.cpu) {
            if (job.cpu->tiled) {
                if (!job.cpuReload) {
                    state->entry.width = job.cpu->width;
                    state->entry.height = job.cpu->height;
                    state->entry.tileSize = job.cpu->tileSize;
                    state->entry.isTiled = true;
                    state->entry.isRGBA = true;
                    state->entry.valid = true;
                }
                size_t cpuBytes = 0;
                for (const auto& tile : job.cpu->tiles) cpuBytes += tile.rgba.capacity();
                job.cpu->cpuBytes = cpuBytes;
                job.cpu->cpuResident = true;
                job.cpu->cpuLastUsed = ++m_useCounter;
                m_cpuBytesUsed += cpuBytes;
                evictCpuUntilFits(job.assetId);
            } else if (!job.cpuReload) {
                uploadNormal(*state);
            }
        }
        ++done;
    }
    return done;
}

void TextureCache::uploadNormal(AssetState& state) {
    if (!state.cpu || state.cpu->rgba.empty()) return;
    const int w = state.cpu->width;
    const int h = state.cpu->height;
    const size_t bytes = mipmappedRGBABytes(w, h);
    evictUntilFits(bytes, QString(), 0);

    const unsigned int tex = makeRGBATexture();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, w);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, state.cpu->rgba.data());
    generateMipmap();
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    state.entry.texRGBA = tex;
    state.entry.width = w;
    state.entry.height = h;
    state.entry.tileSize = kDefaultTileSize;
    state.entry.isTiled = false;
    state.entry.isRGBA = true;
    state.entry.valid = true;
    state.entry.lastUsed = ++m_useCounter;
    m_gpuBytesUsed += bytes;
    state.cpu.reset();
}

bool TextureCache::hasTile(const QString& assetId, int tileX, int tileY) const {
    QMutexLocker lock(&m_mutex);
    auto it = m_states.find(assetId);
    if (it == m_states.end() || !it->second->cpu || !it->second->entry.valid || !it->second->entry.isTiled) return false;
    const auto key = tileKey(tileX, tileY);
    for (const auto& tile : it->second->cpu->tiles) {
        if (tileKey(tile.x, tile.y) == key) return true;
    }
    return it->second->gpuTiles.find(key) != it->second->gpuTiles.end();
}

unsigned int TextureCache::acquireTexture(const QString& assetId) {
    QMutexLocker lock(&m_mutex);
    auto it = m_states.find(assetId);
    if (it == m_states.end() || !it->second->entry.valid || it->second->entry.isTiled) return 0;
    it->second->entry.lastUsed = ++m_useCounter;
    return it->second->entry.texRGBA;
}

bool TextureCache::uploadTile(AssetState& state, const QString& assetId, int tileX, int tileY) {
    if (!state.cpu || !state.entry.isTiled) return false;
    const uint64_t key = tileKey(tileX, tileY);
    if (auto it = state.gpuTiles.find(key); it != state.gpuTiles.end()) {
        it->second.lastUsed = ++m_useCounter;
        return true;
    }

    CpuTile* source = nullptr;
    for (auto& tile : state.cpu->tiles) {
        if (tile.x == tileX && tile.y == tileY) { source = &tile; break; }
    }
    if (!source) return false;
    if (source->rgba.empty()) {
        // The tile exists but its pixels were dropped by evictCpuUntilFits().
        // Kick off a re-decode (once) and report "not ready yet"; the caller
        // (paintGL, typically) will retry on a later frame once pumpUploads()
        // splices the refreshed CpuAsset back in.
        if (!state.cpuReloading) {
            state.cpuReloading = true;
            requestCpuReload(assetId, state.sourcePath);
        }
        return false;
    }
    state.cpu->cpuLastUsed = ++m_useCounter;

    const size_t bytes = mipmappedRGBABytes(source->width, source->height);
    evictUntilFits(bytes, QString(), key);

    const unsigned int tex = makeRGBATexture();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, source->width);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, source->width, source->height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, source->rgba.data());
    generateMipmap();
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    state.gpuTiles[key] = GpuTile{tex, bytes, ++m_useCounter};
    m_gpuBytesUsed += bytes;
    // Keep CPU tile data available for GPU-LRU re-uploads. A future pass can
    // add a separate CPU tile budget; keeping the source tile here avoids a
    // full image re-decode whenever a GPU tile is evicted.
    source->uploaded = true;
    return true;
}

unsigned int TextureCache::acquireTile(const QString& assetId, int tileX, int tileY) {
    QMutexLocker lock(&m_mutex);
    auto it = m_states.find(assetId);
    if (it == m_states.end() || !it->second->entry.valid || !it->second->entry.isTiled) return 0;
    if (!uploadTile(*it->second, assetId, tileX, tileY)) return 0;
    return it->second->gpuTiles[tileKey(tileX, tileY)].texture;
}

void TextureCache::deleteTileTexture(GpuTile& tile) {
    if (tile.texture) {
        glDeleteTextures(1, &tile.texture);
        tile.texture = 0;
    }
}

void TextureCache::deleteEntryTextures(AssetState& state) {
    if (state.entry.texRGBA) {
        glDeleteTextures(1, &state.entry.texRGBA);
        state.entry.texRGBA = 0;
        // The normal texture's exact byte size is derivable from dimensions.
        m_gpuBytesUsed = m_gpuBytesUsed > mipmappedRGBABytes(state.entry.width, state.entry.height)
            ? m_gpuBytesUsed - mipmappedRGBABytes(state.entry.width, state.entry.height) : 0;
    }
    for (auto& kv : state.gpuTiles) {
        m_gpuBytesUsed = m_gpuBytesUsed > kv.second.bytes ? m_gpuBytesUsed - kv.second.bytes : 0;
        deleteTileTexture(kv.second);
    }
    state.gpuTiles.clear();
}

void TextureCache::evictUntilFits(size_t requiredBytes, const QString& protectedAsset, uint64_t protectedTile) {
    while (m_gpuBytesUsed + requiredBytes > m_gpuBudgetBytes) {
        QString victimAsset;
        uint64_t victimKey = 0;
        uint64_t oldest = UINT64_MAX;
        enum class VictimKind { None, Normal, Tile } victimKind = VictimKind::None;

        for (const auto& assetPair : m_states) {
            const auto& state = *assetPair.second;
            if (assetPair.first != protectedAsset && state.entry.texRGBA &&
                state.entry.lastUsed < oldest) {
                oldest = state.entry.lastUsed;
                victimAsset = assetPair.first;
                victimKind = VictimKind::Normal;
            }
            for (const auto& tilePair : state.gpuTiles) {
                if (assetPair.first == protectedAsset && tilePair.first == protectedTile) continue;
                if (tilePair.second.lastUsed < oldest) {
                    oldest = tilePair.second.lastUsed;
                    victimAsset = assetPair.first;
                    victimKey = tilePair.first;
                    victimKind = VictimKind::Tile;
                }
            }
        }

        if (victimKind == VictimKind::None) break;
        auto it = m_states.find(victimAsset);
        if (it == m_states.end()) break;
        auto& state = *it->second;
        if (victimKind == VictimKind::Normal) {
            if (state.entry.texRGBA) {
                const size_t bytes = mipmappedRGBABytes(state.entry.width, state.entry.height);
                m_gpuBytesUsed = m_gpuBytesUsed > bytes ? m_gpuBytesUsed - bytes : 0;
                glDeleteTextures(1, &state.entry.texRGBA);
                state.entry.texRGBA = 0;
                state.entry.valid = state.entry.isTiled;
            }
            continue;
        }
        auto tileIt = state.gpuTiles.find(victimKey);
        if (tileIt == state.gpuTiles.end()) break;
        m_gpuBytesUsed = m_gpuBytesUsed > tileIt->second.bytes ? m_gpuBytesUsed - tileIt->second.bytes : 0;
        deleteTileTexture(tileIt->second);
        state.gpuTiles.erase(tileIt);
    }
}
void TextureCache::evictCpuUntilFits(const QString& protectedAsset) {
    while (m_cpuBytesUsed > m_cpuBudgetBytes) {
        QString victimAsset;
        uint64_t oldest = UINT64_MAX;

        for (const auto& assetPair : m_states) {
            if (assetPair.first == protectedAsset) continue;
            const auto& cpu = assetPair.second->cpu;
            if (!cpu || !cpu->tiled || !cpu->cpuResident || cpu->tiles.empty()) continue;
            if (cpu->cpuLastUsed < oldest) {
                oldest = cpu->cpuLastUsed;
                victimAsset = assetPair.first;
            }
        }

        if (victimAsset.isEmpty()) break; // nothing left to reclaim (or it's all in-use)

        auto it = m_states.find(victimAsset);
        if (it == m_states.end()) break;
        auto& cpu = *it->second->cpu;
        for (auto& tile : cpu.tiles) {
            tile.rgba.clear();
            tile.rgba.shrink_to_fit();
            // uploaded/GPU-tile state is untouched: already-uploaded GPU
            // tiles keep rendering fine, this only removes the CPU-side
            // fallback used to re-upload them without a full re-decode.
        }
        m_cpuBytesUsed = m_cpuBytesUsed > cpu.cpuBytes ? m_cpuBytesUsed - cpu.cpuBytes : 0;
        cpu.cpuBytes = 0;
        cpu.cpuResident = false;
    }
}

void TextureCache::invalidate(const QString& assetId) {
    QMutexLocker lock(&m_mutex);
    auto it = m_states.find(assetId);
    if (it == m_states.end()) return;
    deleteEntryTextures(*it->second);
    if (it->second->cpu && it->second->cpu->cpuResident) {
        m_cpuBytesUsed = m_cpuBytesUsed > it->second->cpu->cpuBytes
            ? m_cpuBytesUsed - it->second->cpu->cpuBytes : 0;
    }
    m_states.erase(it);
}

void TextureCache::clear() {
    QMutexLocker lock(&m_mutex);
    for (auto& kv : m_states) deleteEntryTextures(*kv.second);
    m_states.clear();
    m_completed.clear();
    m_gpuBytesUsed = 0;
    m_cpuBytesUsed = 0;
}

std::optional<TextureCache::Snapshot> TextureCache::snapshot(const QString& assetId) const {
    QMutexLocker lock(&m_mutex);
    auto it = m_states.find(assetId);
    if (it == m_states.end() || !it->second->entry.valid) return std::nullopt;
    const auto& e = it->second->entry;
    return Snapshot{true, e.isTiled, e.width, e.height, e.tileSize};
}

size_t TextureCache::gpuBytesUsed() const {
    QMutexLocker lock(&m_mutex);
    return m_gpuBytesUsed;
}

void TextureCache::setGpuBudgetBytes(size_t bytes) {
    QMutexLocker lock(&m_mutex);
    m_gpuBudgetBytes = std::max<size_t>(bytes, 16ull * 1024ull * 1024ull);
}

size_t TextureCache::cpuBytesUsed() const {
    QMutexLocker lock(&m_mutex);
    return m_cpuBytesUsed;
}

void TextureCache::setCpuBudgetBytes(size_t bytes) {
    QMutexLocker lock(&m_mutex);
    m_cpuBudgetBytes = std::max<size_t>(bytes, 32ull * 1024ull * 1024ull);
    evictCpuUntilFits(QString());
}

} // namespace hc
