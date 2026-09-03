#pragma once

#include <QString>
#include <QOpenGLFunctions>
#include <QMutex>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include <QThreadPool>
#include "../core/MediaAsset.h"
#include "../decode/FrameTypes.h"

namespace hc {

// Image cache used by the preview renderer.
//
// Design:
//   * Still images are decoded off the GUI/GL thread.
//   * Normal images become one RGBA8 texture; their CPU staging buffer is
//     freed the instant the upload completes (uploadNormal()), so they never
//     contribute to steady-state RAM use.
//   * Very large images (e.g. a 100 MP RGBA source is ~381 MB raw) are split
//     into RGBA8 tiles and uploaded lazily.
//   * GPU tiles are managed by an LRU VRAM budget (m_gpuBudgetBytes).
//   * CPU tile payloads used to be retained forever so an evicted GPU tile
//     could be re-uploaded without decoding the source again — but nothing
//     ever freed them, so opening a handful of huge images could quietly
//     pin gigabytes of resident RAM even though only a fraction was ever
//     on-screen. They are now bounded by a second, independent LRU budget
//     (m_cpuBudgetBytes): when it's exceeded, the CPU tiles for the
//     least-recently-used tiled asset are dropped (their pixel bytes freed,
//     dimensions kept). If that asset is touched again, uploadTile()
//     transparently re-decodes it from disk via requestCpuReload().
//   * Video frames are NOT stored here; the video pipeline remains YUV420P
//     and is bounded separately by FrameCache's own byte budget.
//
// All OpenGL operations must happen on the thread owning the current GL
// context. requestAsset()/pumpUploads() are thread-safe; acquireTile() and
// clear()/invalidate() must be called while the GL context is current.
class TextureCache {
public:
    static constexpr int kDefaultTileSize = 512;
    static constexpr size_t kDefaultGpuBudgetBytes = 256ull * 1024ull * 1024ull;
    // CPU-side budget for decoded tile pixels of huge (tiled) images. Larger
    // than the GPU budget by default since CPU RAM is typically more
    // plentiful than VRAM, but it exists specifically so a project with
    // several huge images open doesn't grow without bound. Suggested tiers
    // for a settings UI, mirroring FrameCache's per-machine sizing:
    //   ~4 GB RAM machine  -> 512 MB
    //   ~8 GB RAM machine  -> 1024 MB (default)
    //   ~16 GB+ RAM machine -> 2048-4096 MB
    static constexpr size_t kDefaultCpuBudgetBytes = 128ull * 1024ull * 1024ull;
    static constexpr size_t kHugeImageBytes = 64ull * 1024ull * 1024ull;
    static constexpr int kHugeImageDimension = 4096;

    struct Entry {
        unsigned int texRGBA = 0;
        bool isRGBA = true;
        bool isTiled = false;
        int width = 0;
        int height = 0;
        int tileSize = kDefaultTileSize;
        bool valid = false;
        uint64_t lastUsed = 0;
    };

    struct Snapshot {
        bool valid = false;
        bool isTiled = false;
        int width = 0;
        int height = 0;
        int tileSize = kDefaultTileSize;
    };

    explicit TextureCache(size_t gpuBudgetBytes = kDefaultGpuBudgetBytes,
                           size_t cpuBudgetBytes = kDefaultCpuBudgetBytes);
    ~TextureCache();

    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    bool contains(const QString& assetId) const;
    bool isLoading(const QString& assetId) const;
    bool hasPendingWork() const;
    bool requestAsset(const MediaAsset& asset);

    // Uploads a small bounded amount of ready CPU data. Call once per paintGL.
    // Returns number of assets/tiles uploaded or finalized.
    int pumpUploads(int maxUploads = 2);

    std::optional<Snapshot> snapshot(const QString& assetId) const;

    // For normal images, returns the single RGBA texture. For tiled images
    // this returns 0; use acquireTile(). Must be called from the GL thread.
    unsigned int acquireTexture(const QString& assetId);

    // Lazily uploads one tile and marks it MRU. Must be called from GL thread.
    // Returns 0 when the tile is not decoded/ready yet.
    unsigned int acquireTile(const QString& assetId, int tileX, int tileY);

    // Returns true if a tile exists in the decoded source and can be rendered.
    bool hasTile(const QString& assetId, int tileX, int tileY) const;

    void invalidate(const QString& assetId);
    void clear();

    size_t gpuBytesUsed() const;
    size_t gpuBudgetBytes() const { return m_gpuBudgetBytes; }
    void setGpuBudgetBytes(size_t bytes);

    // CPU-side budget for retained tile pixels of huge images (see class
    // comment). Independent from the GPU/VRAM budget above.
    size_t cpuBytesUsed() const;
    size_t cpuBudgetBytes() const { return m_cpuBudgetBytes; }
    void setCpuBudgetBytes(size_t bytes);

private:
    struct CpuTile {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
        bool uploaded = false;
    };

    struct CpuAsset {
        QString assetId;
        int width = 0;
        int height = 0;
        int tileSize = kDefaultTileSize;
        bool tiled = false;
        std::vector<uint8_t> rgba;
        std::vector<CpuTile> tiles;
        // Whether `tiles[*].rgba` currently holds pixel data. Set false by
        // evictCpuUntilFits() once the CPU budget is exceeded; the tile
        // metadata (x/y/width/height) is kept so uploadTile() can detect a
        // resident-but-empty tile and trigger requestCpuReload() instead of
        // silently rendering nothing.
        bool cpuResident = true;
        uint64_t cpuLastUsed = 0;
        size_t cpuBytes = 0; // sum of tiles[*].rgba capacity while resident
    };

    struct GpuTile {
        unsigned int texture = 0;
        size_t bytes = 0;
        uint64_t lastUsed = 0;
    };

    struct AssetState {
        Entry entry;
        bool loading = false;
        bool failed = false;
        bool cpuReloading = false; // a requestCpuReload() job is in flight
        QString error;
        QString sourcePath; // kept for requestCpuReload(); set on first requestAsset()
        std::shared_ptr<CpuAsset> cpu;
        std::unordered_map<uint64_t, GpuTile> gpuTiles;
    };

    struct CompletedJob {
        QString assetId;
        std::shared_ptr<CpuAsset> cpu;
        QString error;
        bool cpuReload = false; // true: a re-decode to refill evicted CPU tiles
    };

    static uint64_t tileKey(int x, int y);
    static std::shared_ptr<CpuAsset> decodeAsset(const MediaAsset& asset, QString* errorOut);

    void enqueueDecode(const MediaAsset& asset);
    void requestCpuReload(const QString& assetId, const QString& sourcePath);
    void uploadNormal(AssetState& state);
    bool uploadTile(AssetState& state, const QString& assetId, int tileX, int tileY);
    void evictUntilFits(size_t requiredBytes, const QString& protectedAsset, uint64_t protectedTile);
    // Evicts (frees the pixel bytes of) least-recently-used tiled assets'
    // CPU tiles until m_cpuBytesUsed is back within m_cpuBudgetBytes.
    // `protectedAsset` (typically the one that just grew) is never evicted,
    // so a single huge image larger than the whole budget still loads.
    void evictCpuUntilFits(const QString& protectedAsset);
    void deleteEntryTextures(AssetState& state);
    void deleteTileTexture(GpuTile& tile);

    mutable QMutex m_mutex;
    std::unordered_map<QString, std::shared_ptr<AssetState>> m_states;
    std::deque<CompletedJob> m_completed;
    uint64_t m_useCounter = 0;
    size_t m_gpuBytesUsed = 0;
    size_t m_gpuBudgetBytes = kDefaultGpuBudgetBytes;
    size_t m_cpuBytesUsed = 0;
    size_t m_cpuBudgetBytes = kDefaultCpuBudgetBytes;
    QThreadPool m_pool;
};

} // namespace hc
