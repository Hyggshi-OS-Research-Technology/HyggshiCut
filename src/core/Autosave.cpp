#include "Autosave.h"
#include "Project.h"

#include <QTimer>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

#include <algorithm>
#include <ranges>

#if defined(__linux__)
#  include <unistd.h>
#endif

namespace hc {

namespace {

constexpr auto kSettingsOrg = "HyggshiCut";
constexpr auto kSettingsApp = "Preferences";
constexpr auto kKeyEnabled = "autosave/enabled";
constexpr auto kKeyInterval = "autosave/intervalSec";

constexpr auto kSnapshotSuffix = ".hcproj";
constexpr auto kMetaSuffix = ".session.json";

QSettings prefs() {
    return QSettings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
}

// Linux process start time, in clock ticks since boot (field 22 of
// /proc/<pid>/stat). Combined with the pid this uniquely identifies a
// process: the kernel recycles pids, but not a pid at the same start time.
// Returns 0 when it cannot be read.
qulonglong processStartTicks(qint64 pid) {
#if defined(__linux__)
    QFile f(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return 0;
    const QByteArray content = f.readAll();

    // The comm field is parenthesised and may itself contain spaces or ')',
    // so split after the LAST ')' rather than tokenising the whole line.
    const int close = content.lastIndexOf(')');
    if (close < 0) return 0;
    const QList<QByteArray> fields =
        content.mid(close + 1).simplified().split(' ');
    // After comm, field 3 of the man page (state) is index 0 here, so
    // starttime (field 22) is index 19.
    constexpr int kStartTimeIndex = 19;
    if (fields.size() <= kStartTimeIndex) return 0;
    bool ok = false;
    const qulonglong ticks = fields.at(kStartTimeIndex).toULongLong(&ok);
    return ok ? ticks : 0;
#else
    Q_UNUSED(pid);
    return 0;
#endif
}

QString currentExecutableName() {
    return QFileInfo(QCoreApplication::applicationFilePath()).fileName();
}

// Best-effort check that `pid` is the same program as us, so an unrelated
// process that happens to hold a recycled pid is not mistaken for a live
// HyggshiCut.
bool pidLooksLikeHyggshiCut(qint64 pid) {
#if defined(__linux__)
    QFile f(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    const QString comm = QString::fromUtf8(f.readAll()).trimmed();
    if (comm.isEmpty()) return false;

    const QString self = currentExecutableName();
    // /proc/<pid>/comm is truncated to 15 characters, so compare prefixes.
    const QString a = comm.left(15);
    const QString b = self.left(15);
    return !b.isEmpty() && a == b;
#else
    Q_UNUSED(pid);
    return false;
#endif
}

} // namespace

AutosaveManager::AutosaveManager(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, &AutosaveManager::onTimeout);

    // One snapshot per process. The pid is part of the id so two concurrent
    // instances never fight over the same file.
    m_sessionId = QStringLiteral("session-%1-%2")
                      .arg(QCoreApplication::applicationPid())
                      .arg(QDateTime::currentSecsSinceEpoch());
}

AutosaveManager::~AutosaveManager() {
    // Deliberately NOT calling stop() here. A destructor running during an
    // abnormal teardown must not delete the very snapshot that teardown is
    // the reason to keep. MainWindow::closeEvent() calls stop() explicitly on
    // the clean path.
}

bool AutosaveManager::isEnabled() {
    return prefs().value(QString::fromLatin1(kKeyEnabled), true).toBool();
}

void AutosaveManager::setEnabled(bool enabled) {
    auto s = prefs();
    s.setValue(QString::fromLatin1(kKeyEnabled), enabled);
    s.sync();
}

int AutosaveManager::intervalSeconds() {
    const int v = prefs().value(QString::fromLatin1(kKeyInterval), kDefaultIntervalSec).toInt();
    return std::clamp(v, kMinIntervalSec, kMaxIntervalSec);
}

void AutosaveManager::setIntervalSeconds(int seconds) {
    auto s = prefs();
    s.setValue(QString::fromLatin1(kKeyInterval),
               std::clamp(seconds, kMinIntervalSec, kMaxIntervalSec));
    s.sync();
}

QString AutosaveManager::recoveryDir() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath() + QStringLiteral("/hyggshicut");
    }
    const QString dir = base + QStringLiteral("/recovery");
    QDir().mkpath(dir);
    return dir;
}

bool AutosaveManager::isProcessAlive(qint64 pid, const QDateTime& startedAt) {
    Q_UNUSED(startedAt);
    if (pid <= 0) return false;
#if defined(__linux__)
    if (!QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid))) return false;
    return pidLooksLikeHyggshiCut(pid);
#else
    return false;
#endif
}

QString AutosaveManager::metaPath() const {
    return QDir(recoveryDir()).filePath(m_sessionId + QString::fromLatin1(kMetaSuffix));
}

QString AutosaveManager::sessionSnapshotPath() const {
    return QDir(recoveryDir()).filePath(m_sessionId + QString::fromLatin1(kSnapshotSuffix));
}

QString AutosaveManager::sessionId() const { return m_sessionId; }
QDateTime AutosaveManager::lastSaveTime() const { return m_lastSave; }
bool AutosaveManager::isRunning() const { return m_timer && m_timer->isActive(); }

bool AutosaveManager::writeMeta() {
    QJsonObject o;
    o[QStringLiteral("sessionId")] = m_sessionId;
    o[QStringLiteral("pid")] = static_cast<double>(QCoreApplication::applicationPid());
    o[QStringLiteral("pidStartTicks")] =
        QString::number(processStartTicks(QCoreApplication::applicationPid()));
    o[QStringLiteral("originalPath")] = m_project ? m_project->filePath : QString();
    o[QStringLiteral("projectName")] = m_project ? m_project->name : QString();
    o[QStringLiteral("savedAt")] = QDateTime::currentDateTime().toString(Qt::ISODate);
    o[QStringLiteral("executable")] = currentExecutableName();

    QSaveFile f(metaPath());
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray payload = QJsonDocument(o).toJson(QJsonDocument::Indented);
    if (f.write(payload) != payload.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

void AutosaveManager::start(Project* project, std::function<bool()> isModified) {
    m_project = project;
    m_isModified = std::move(isModified);
    m_stopped = false;

    if (!project) {
        m_timer->stop();
        return;
    }
    reloadSettings();
}

void AutosaveManager::reloadSettings() {
    if (m_stopped || !m_project) return;
    if (!isEnabled()) {
        m_timer->stop();
        return;
    }
    m_timer->start(intervalSeconds() * 1000);
}

void AutosaveManager::stop() {
    m_stopped = true;
    if (m_timer) m_timer->stop();

    // Remove this session's files so the next launch does not treat a clean
    // exit as a crash.
    QFile::remove(sessionSnapshotPath());
    QFile::remove(metaPath());
}

void AutosaveManager::onTimeout() {
    if (!m_project) return;
    // Skip untouched projects: autosave should not spin the disk, and should
    // not refresh the snapshot timestamp when nothing changed.
    if (m_isModified && !m_isModified()) return;

    QString err;
    if (!saveNow(&err)) {
        qWarning() << "[Autosave] failed:" << err;
        emit autosaveFailed(err);
    }
}

bool AutosaveManager::saveNow(QString* errorOut) {
    if (!m_project) {
        if (errorOut) *errorOut = QStringLiteral("No project to autosave.");
        return false;
    }

    // Project::saveToFile() assigns the path it wrote to Project::filePath.
    // For an autosave that would repoint the project at the recovery file,
    // so the user's next Ctrl+S would overwrite the snapshot instead of their
    // real .hcproj. Save and restore it around the call.
    const QString userPath = m_project->filePath;

    QString err;
    const bool ok = m_project->saveToFile(sessionSnapshotPath(), &err);

    m_project->filePath = userPath;

    if (!ok) {
        if (errorOut) *errorOut = err;
        return false;
    }
    if (!writeMeta()) {
        // The snapshot exists but is unattributed; without meta it cannot be
        // offered for recovery, so treat it as a failure rather than claiming
        // success.
        if (errorOut) *errorOut = QStringLiteral("Could not write the recovery metadata.");
        return false;
    }

    m_lastSave = QDateTime::currentDateTime();
    emit autosaved(sessionSnapshotPath(), m_lastSave);
    return true;
}

QList<RecoverySession> AutosaveManager::findRecoverableSessions() {
    QList<RecoverySession> out;

    QDir dir(recoveryDir());
    const QStringList metas =
        dir.entryList({QStringLiteral("*%1").arg(QString::fromLatin1(kMetaSuffix))}, QDir::Files);

    for (const QString& metaName : metas) {
        const QString metaFull = dir.filePath(metaName);
        QFile f(metaFull);
        if (!f.open(QIODevice::ReadOnly)) continue;

        QJsonParseError perr{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
        f.close();
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            // Corrupt metadata is useless; drop it so it is not re-examined
            // on every launch forever.
            QFile::remove(metaFull);
            continue;
        }
        const QJsonObject o = doc.object();

        RecoverySession s;
        s.sessionId = o.value(QStringLiteral("sessionId")).toString();
        s.originalPath = o.value(QStringLiteral("originalPath")).toString();
        s.projectName = o.value(QStringLiteral("projectName")).toString();
        s.pid = static_cast<qint64>(o.value(QStringLiteral("pid")).toDouble());
        s.savedAt = QDateTime::fromString(
            o.value(QStringLiteral("savedAt")).toString(), Qt::ISODate);

        if (s.sessionId.isEmpty()) {
            QFile::remove(metaFull);
            continue;
        }

        s.autosavePath = dir.filePath(s.sessionId + QString::fromLatin1(kSnapshotSuffix));
        if (!QFileInfo::exists(s.autosavePath)) {
            // Metadata without a snapshot: nothing to recover.
            QFile::remove(metaFull);
            continue;
        }

        // Our own session, or a second instance running right now, is not a
        // crash. Leave those alone.
        if (s.pid == QCoreApplication::applicationPid()) continue;
        if (isProcessAlive(s.pid)) continue;

        out.push_back(s);
    }

    // Newest first: the most recent crash is the one the user cares about.
    // Explicit comparator rather than a projection + std::ranges::greater:
    // the projection form needs QDateTime to satisfy totally_ordered, which
    // depends on the Qt version's comparison operators. This works anywhere.
    std::ranges::sort(out, [](const RecoverySession& a, const RecoverySession& b) {
        return a.savedAt > b.savedAt;
    });
    return out;
}

bool AutosaveManager::discardSession(const QString& sessionId) {
    if (sessionId.isEmpty()) return false;
    QDir dir(recoveryDir());
    const bool a = QFile::remove(dir.filePath(sessionId + QString::fromLatin1(kSnapshotSuffix)));
    const bool b = QFile::remove(dir.filePath(sessionId + QString::fromLatin1(kMetaSuffix)));
    return a || b;
}

} // namespace hc
