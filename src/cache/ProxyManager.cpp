#include "ProxyManager.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QIODevice>
#include <QSettings>
#include <algorithm>

namespace hc {

namespace {
constexpr const char* kIndexFileName = "index.json";
}

ProxyManager::ProxyManager(QObject* parent) : QObject(parent) {
    QSettings pref("HyggshiCut", "Preferences");
    QString customCache = pref.value("proxy/cacheDir").toString();
    if (!customCache.isEmpty() && QDir().mkpath(customCache)) {
        m_cacheDir = customCache;
    } else {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        m_cacheDir = (base.isEmpty() ? QDir::tempPath() : base) + "/HyggshiCut/proxies";
        QDir().mkpath(m_cacheDir);
    }
    m_maxProxyWidth = pref.value("proxy/maxProxyWidth", 0).toInt(); // 0 = auto tiering
    if (m_maxProxyWidth < 0) m_maxProxyWidth = 0;
    loadIndex();
}

ProxyManager::~ProxyManager() {
    cancelAll();
}

void ProxyManager::setCacheDirectory(const QString& dir) {
    if (dir.isEmpty() || dir == m_cacheDir) return;
    cancelAll();
    m_cacheDir = dir;
    QDir().mkpath(m_cacheDir);
    QSettings pref("HyggshiCut", "Preferences");
    pref.setValue("proxy/cacheDir", m_cacheDir);
    m_readyProxyPaths.clear();
    m_statusByKey.clear();
    loadIndex();
}

void ProxyManager::clearCache() {
    cancelAll();
    QDir dir(m_cacheDir);
    const auto files = dir.entryInfoList(QStringList() << "*.mp4" << "*.json", QDir::Files);
    for (const auto& f : files) {
        QFile::remove(f.absoluteFilePath());
    }
    m_readyProxyPaths.clear();
    m_statusByKey.clear();
}

qint64 ProxyManager::cacheSizeBytes() const {
    QDir dir(m_cacheDir);
    qint64 total = 0;
    const auto files = dir.entryInfoList(QStringList() << "*.mp4", QDir::Files);
    for (const auto& f : files) {
        total += f.size();
    }
    return total;
}

int ProxyManager::cachedProxyCount() const {
    QDir dir(m_cacheDir);
    return dir.entryList(QStringList() << "*.mp4", QDir::Files).size();
}

QString ProxyManager::identityKeyForFile(const QString& filePath) {
    QFileInfo fi(filePath);
    const QString basis = QString("%1|%2|%3")
        .arg(fi.absoluteFilePath())
        .arg(fi.size())
        .arg(fi.lastModified().toSecsSinceEpoch());
    return QString::fromLatin1(
        QCryptographicHash::hash(basis.toUtf8(), QCryptographicHash::Md5).toHex());
}

QString ProxyManager::proxyFilePathForKey(const QString& key) const {
    return m_cacheDir + "/" + key + ".mp4";
}

void ProxyManager::loadIndex() {
    QFile f(m_cacheDir + "/" + kIndexFileName);
    if (!f.open(QIODevice::ReadOnly)) return;
    const auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;
    const auto obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        const QString key = it.key();
        const QString fileName = it.value().toObject().value("file").toString();
        const QString fullPath = m_cacheDir + "/" + fileName;
        if (!fileName.isEmpty() && QFileInfo::exists(fullPath)) {
            m_readyProxyPaths[key] = fullPath;
        }
    }
}

void ProxyManager::saveIndex() const {
    QJsonObject obj;
    for (const auto& [key, path] : m_readyProxyPaths) {
        QJsonObject entry;
        entry["file"] = QFileInfo(path).fileName();
        obj[key] = entry;
    }
    QFile f(m_cacheDir + "/" + kIndexFileName);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    }
}

ProxyStatus ProxyManager::statusForAsset(const MediaAssetPtr& asset) const {
    if (!asset) return ProxyStatus::NotGenerated;
    const QString key = identityKeyForFile(asset->filePath);
    if (m_readyProxyPaths.count(key)) return ProxyStatus::Ready;
    if (m_hasCurrentJob && m_currentJob.identityKey == key) return ProxyStatus::Generating;
    for (const auto& job : m_queue) {
        if (job.identityKey == key) return ProxyStatus::Queued;
    }
    auto it = m_statusByKey.find(key);
    if (it != m_statusByKey.end()) return it->second;
    return ProxyStatus::NotGenerated;
}

QString ProxyManager::proxyPathForAsset(const MediaAssetPtr& asset) const {
    if (!asset) return {};
    const QString key = identityKeyForFile(asset->filePath);
    auto it = m_readyProxyPaths.find(key);
    return it != m_readyProxyPaths.end() ? it->second : QString();
}

void ProxyManager::requestProxy(const MediaAssetPtr& asset) {
    if (!asset || !asset->hasVideo()) return;
    const auto status = statusForAsset(asset);
    if (status == ProxyStatus::Ready || status == ProxyStatus::Generating || status == ProxyStatus::Queued) {
        return;
    }
    const QString key = identityKeyForFile(asset->filePath);
    m_statusByKey[key] = ProxyStatus::Queued;
    m_queue.push_back(Job{asset, key});
    m_queueTotal++;
    emit proxyStatusChanged(asset->id, ProxyStatus::Queued);
    emit queueProgress(m_queueDone, m_queueTotal);
    if (!m_hasCurrentJob) {
        startNextInQueue();
    }
}

void ProxyManager::requestProxiesForAssets(const std::vector<MediaAssetPtr>& assets) {
    for (const auto& asset : assets) {
        requestProxy(asset);
    }
}

void ProxyManager::cancelAll() {
    m_queue.clear();
    m_queueTotal = 0;
    m_queueDone = 0;
    if (m_process) {
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_hasCurrentJob) {
        if (!m_currentJobTempPath.isEmpty()) QFile::remove(m_currentJobTempPath);
        m_statusByKey[m_currentJob.identityKey] = ProxyStatus::NotGenerated;
        emit proxyStatusChanged(m_currentJob.asset->id, ProxyStatus::NotGenerated);
        m_hasCurrentJob = false;
    }
}

int ProxyManager::effectiveProxyWidth(const MediaAssetPtr& asset) const {
    const int srcW = asset ? std::max(0, asset->width) : 0;
    if (m_maxProxyWidth > 0) {
        // Explicit preset — the scale filter's min(width, iw) still prevents
        // upscaling a source that is smaller than the preset.
        return m_maxProxyWidth;
    }
    // AUTO tiering, keyed to source resolution so 4K/8K footage is edited
    // through a light 720p proxy while HD footage uses a 480p proxy — both
    // tiny in RAM/VRAM, which is the point on weak machines.
    if (srcW <= 0) return 960;        // unknown resolution → balanced default
    if (srcW >= 3840) return 1280;    // 4K / 8K → 720p
    if (srcW >= 1920) return 854;     // 1080p / 1440p → 480p
    return std::min(srcW, 854);       // SD / 720p → cap at 480p, never upscale
}

void ProxyManager::startNextInQueue() {
    if (m_queue.isEmpty()) {
        m_hasCurrentJob = false;
        return;
    }
    m_currentJob = m_queue.takeFirst();
    m_hasCurrentJob = true;

    const auto& asset = m_currentJob.asset;
    m_currentJobDuration = asset->duration;
    m_currentJobTempPath = proxyFilePathForKey(m_currentJob.identityKey) + ".tmp";
    QFile::remove(m_currentJobTempPath);

    // All-intra (every frame a keyframe) proxy: bigger on disk than a
    // normal long-GOP encode, but that's exactly the point — it makes
    // Decoder::seek() (keyframe seek + forward-decode-to-target) resolve
    // in a single decode call instead of walking a GOP, which is where
    // scrub lag on the original file comes from. -an drops audio entirely:
    // PlaybackController always pulls audio from the original file (see
    // audioDecoderFor), so the proxy never needs it.
    const int targetWidth = effectiveProxyWidth(asset);
    QStringList args;
    args << "-y" << "-i" << asset->filePath
         << "-vf" << QString("scale='min(%1,iw)':-2").arg(targetWidth)
         << "-c:v" << "libx264" << "-preset" << "ultrafast" << "-crf" << "20" << "-threads" << "1"
         << "-g" << "1" << "-sc_threshold" << "0" << "-pix_fmt" << "yuv420p"
         << "-an"
         // The temp file is named "<key>.mp4.tmp" (see startNextInQueue),
         // so ffmpeg can't infer the container from the extension the way
         // it normally would — force it explicitly rather than relying on
         // the output filename.
         << "-f" << "mp4"
         << "-progress" << "pipe:1" << "-nostats"
         << m_currentJobTempPath;

    qDebug().noquote() << "[ProxyManager] ffmpeg" << args.join(' ');

    m_stdoutBuffer.clear();
    m_process = new QProcess(this);
    m_process->setProgram("ffmpeg");
    m_process->setArguments(args);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ProxyManager::onReadyReadStandardOutput);
    connect(m_process, &QProcess::finished, this, &ProxyManager::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ProxyManager::onProcessErrorOccurred);
    m_process->start();

    emit proxyStatusChanged(asset->id, ProxyStatus::Generating);
}

void ProxyManager::onReadyReadStandardOutput() {
    if (!m_process || !m_hasCurrentJob) return;
    m_stdoutBuffer += m_process->readAllStandardOutput();

    int newlineIdx;
    while ((newlineIdx = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newlineIdx).trimmed();
        m_stdoutBuffer.remove(0, newlineIdx + 1);

        if (line.startsWith("out_time_us=")) {
            const qint64 outTimeUs = line.mid(QByteArrayLiteral("out_time_us=").size()).toLongLong();
            const double totalSec = ticksToSeconds(m_currentJobDuration);
            if (totalSec > 0.0001) {
                const double frac = std::clamp(static_cast<double>(outTimeUs) / 1'000'000.0 / totalSec, 0.0, 1.0);
                emit proxyProgress(m_currentJob.asset->id, frac);
            }
        }
    }
}

void ProxyManager::finishCurrentJob(bool success, const QString& finalPathOnSuccess) {
    if (!m_hasCurrentJob) return;
    const QString assetId = m_currentJob.asset->id;
    const QString key = m_currentJob.identityKey;

    if (success) {
        m_readyProxyPaths[key] = finalPathOnSuccess;
        m_statusByKey.erase(key);
        saveIndex();
        emit proxyStatusChanged(assetId, ProxyStatus::Ready);
        emit proxyReady(assetId, finalPathOnSuccess);
    } else {
        m_statusByKey[key] = ProxyStatus::Failed;
        emit proxyStatusChanged(assetId, ProxyStatus::Failed);
        emit proxyFailed(assetId, tr("ffmpeg loi khi tao proxy"));
    }

    m_queueDone++;
    emit queueProgress(m_queueDone, m_queueTotal);
    if (m_queue.isEmpty()) {
        m_queueTotal = 0;
        m_queueDone = 0;
    }

    m_hasCurrentJob = false;
    m_currentJobTempPath.clear();
    startNextInQueue();
}

void ProxyManager::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    const bool success = (status == QProcess::NormalExit && exitCode == 0);
    QString finalPath;
    if (success && m_hasCurrentJob) {
        finalPath = proxyFilePathForKey(m_currentJob.identityKey);
        QFile::remove(finalPath);
        if (!QFile::rename(m_currentJobTempPath, finalPath)) {
            finalPath.clear();
        }
    }
    if (!success || finalPath.isEmpty()) {
        if (!m_currentJobTempPath.isEmpty()) QFile::remove(m_currentJobTempPath);
    }
    if (m_process) { m_process->deleteLater(); m_process = nullptr; }
    finishCurrentJob(success && !finalPath.isEmpty(), finalPath);
}

void ProxyManager::onProcessErrorOccurred(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        qWarning() << "[ProxyManager] khong the chay ffmpeg (kiem tra ffmpeg co trong PATH khong)";
        if (m_process) { m_process->deleteLater(); m_process = nullptr; }
        if (!m_currentJobTempPath.isEmpty()) QFile::remove(m_currentJobTempPath);
        finishCurrentJob(false, QString());
    }
}

} // namespace hc
