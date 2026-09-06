#pragma once
#include <QObject>
#include <QString>
#include <QProcess>
#include <QList>
#include <unordered_map>
#include <algorithm>
#include "../core/MediaAsset.h"

namespace hc {

enum class ProxyStatus {
    NotGenerated,
    Queued,
    Generating,
    Ready,
    Failed,
};

// ProxyManager is the "proxy" half of the anti-lag work: it transcodes each
// video MediaAsset down to a small, all-intra (every frame a keyframe) H.264
// file that decodes and seeks far faster than the camera-original source —
// exactly the two operations PlaybackController hammers on every scrub tick
// (Decoder::seek() + forward-decode). A 4K/8K or heavily-compressed
// long-GOP source can take tens of milliseconds just to reach the right
// frame after a seek; a small all-intra proxy reaches it in one decode call.
//
// This intentionally reuses the same "shell out to ffmpeg via QProcess"
// approach as Exporter (see export/Exporter.h) rather than driving
// libavcodec's encoder directly — same rationale: battle-tested encoding,
// smaller codebase, and it keeps proxy generation decoupled from the
// editor's own decode/render pipeline (Decoder only ever needs to open and
// read files, never encode).
//
// Correctness note: proxies are VIDEO-ONLY (no audio track) and are used
// exclusively to speed up on-screen preview. Audio playback always decodes
// the original file (see PlaybackController::audioDecoderFor), and Exporter
// always renders from MediaAsset::filePath — the proxy path is never used
// for anything that ends up in an exported file, so the "does export match
// what what I saw while editing" guarantee (see Exporter.h) still holds for
// audio and for final quality; only the on-screen video during editing is
// affected by the proxy toggle.
//
// Proxies are cached on disk under a per-machine cache directory, keyed by
// file identity (path + size + mtime) rather than MediaAsset::id, so a
// proxy generated once is reused across project reloads and re-imports of
// the same file — generation is the slow, one-time cost this is meant to
// amortize.
class ProxyManager : public QObject {
    Q_OBJECT
public:
    explicit ProxyManager(QObject* parent = nullptr);
    ~ProxyManager() override;

    // Current state for this asset (NotGenerated if never requested/found
    // in the on-disk cache index).
    ProxyStatus statusForAsset(const MediaAssetPtr& asset) const;

    // Absolute path to the ready proxy file, or empty if not Ready.
    QString proxyPathForAsset(const MediaAssetPtr& asset) const;

    // Enqueues proxy generation for one asset. No-op if the asset has no
    // video, or a proxy for it is already Ready/Queued/Generating.
    void requestProxy(const MediaAssetPtr& asset);

    // Convenience: requests every video asset in the list (e.g. "generate
    // proxies for all media" from the menu).
    void requestProxiesForAssets(const std::vector<MediaAssetPtr>& assets);

    // Stops the in-flight job (if any) and clears the pending queue.
    // Already-Ready proxies on disk are left alone.
    void cancelAll();

    // Maximum proxy width in pixels (height scales to preserve aspect
    // ratio, rounded to an even number as required by yuv420p). Applies to
    // jobs started after this call. Default 960.
    void setMaxProxyWidth(int width) { m_maxProxyWidth = std::max(160, width); }
    int maxProxyWidth() const { return m_maxProxyWidth; }

    QString cacheDirectory() const { return m_cacheDir; }
    void setCacheDirectory(const QString& dir);
    void clearCache();
    qint64 cacheSizeBytes() const;
    int cachedProxyCount() const;

signals:
    // Fired on every state transition for one asset.
    void proxyStatusChanged(QString assetId, ProxyStatus status);
    // Fired once a proxy finishes encoding successfully.
    void proxyReady(QString assetId, QString proxyPath);
    void proxyFailed(QString assetId, QString error);
    // 0..1 progress for the job currently encoding.
    void proxyProgress(QString assetId, double fraction);
    // Whole-queue progress, for a simple status bar readout: e.g. "2/5".
    void queueProgress(int done, int total);

private slots:
    void onReadyReadStandardOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError error);

private:
    struct Job {
        MediaAssetPtr asset;
        QString identityKey;
    };

    static QString identityKeyForFile(const QString& filePath);
    QString proxyFilePathForKey(const QString& key) const;

    void loadIndex();
    void saveIndex() const;

    void startNextInQueue();
    void finishCurrentJob(bool success, const QString& proxyFilePathOnSuccess);

    QString m_cacheDir;
    std::unordered_map<QString, QString> m_readyProxyPaths; // identityKey -> absolute proxy file path
    std::unordered_map<QString, ProxyStatus> m_statusByKey;  // identityKey -> status (Generating/Failed only; Ready implied by m_readyProxyPaths)

    QList<Job> m_queue;
    Job m_currentJob;
    bool m_hasCurrentJob = false;
    int m_queueTotal = 0;
    int m_queueDone = 0;

    QProcess* m_process = nullptr;
    QByteArray m_stdoutBuffer;
    Ticks m_currentJobDuration = 0;
    QString m_currentJobTempPath;

    int m_maxProxyWidth = 960;
};

} // namespace hc
