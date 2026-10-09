#pragma once

// Autosave and crash recovery.
//
// A video editor loses hours of work when it dies, and it dies more than most
// applications: GPU driver resets, an ffmpeg child going out of memory on a
// 4K export, and OOM-killer kills on large timelines are all routine. Before
// this, HyggshiCut only ever wrote a .hcproj when the user pressed Ctrl+S, so
// everything since the last manual save was simply gone.
//
// AutosaveManager periodically snapshots the open project into a recovery
// directory and records which process owns that snapshot. On a clean exit the
// snapshot is deleted. Anything still present at startup therefore belongs to
// a session that did not exit cleanly, and is offered back to the user.
//
// Design notes:
//   * The snapshot is a normal .hcproj. Recovery is just "open this file", so
//     there is no second format to keep in sync with Project's serialiser.
//   * Project::saveToFile() assigns to Project::filePath. Autosave must not
//     do that, or the next Ctrl+S would silently overwrite the recovery file
//     instead of the user's real project. saveNow() saves and restores it.
//   * Ownership is tracked by pid *and* process start time, so a recycled pid
//     belonging to an unrelated process cannot make a real crash look live.
//   * This class is GUI-free (QObject + QTimer only) so it can be tested
//     headlessly; the recovery prompt lives in MainWindow.

#include <QObject>
#include <QString>
#include <QList>
#include <QDateTime>
#include <functional>

class QTimer;

namespace hc {

class Project;

// One recoverable snapshot found on disk.
struct RecoverySession {
    QString sessionId;      // identifies the files on disk
    QString autosavePath;   // the .hcproj snapshot itself
    QString originalPath;   // where the user's project lives, "" if never saved
    QString projectName;
    QDateTime savedAt;
    qint64 pid = 0;
};

class AutosaveManager : public QObject {
    Q_OBJECT

public:
    explicit AutosaveManager(QObject* parent = nullptr);
    ~AutosaveManager() override;

    // Bounds exist because the interval is user-editable: a 1-second autosave
    // on a large timeline would stall the UI thread repeatedly.
    static constexpr int kDefaultIntervalSec = 120;
    static constexpr int kMinIntervalSec = 15;
    static constexpr int kMaxIntervalSec = 1800;

    // Preferences (QSettings "HyggshiCut"/"Preferences", group "autosave").
    static bool isEnabled();
    static void setEnabled(bool enabled);
    static int intervalSeconds();
    static void setIntervalSeconds(int seconds);

    // Directory holding recovery snapshots. Created on demand.
    static QString recoveryDir();

    // Snapshots whose owning process is gone, newest first. These are exactly
    // the sessions that crashed.
    static QList<RecoverySession> findRecoverableSessions();

    // Delete a session's files. Call after the user recovers or discards.
    static bool discardSession(const QString& sessionId);

    // True if `pid` is a currently running HyggshiCut process. Used to tell a
    // crashed session apart from a second instance running right now.
    static bool isProcessAlive(qint64 pid, const QDateTime& startedAt = {});

    // Begin autosaving `project`. `isModified` is polled on each tick so a
    // project sitting untouched does not rewrite the same bytes forever.
    // Passing nullptr stops autosaving.
    void start(Project* project, std::function<bool()> isModified);

    // Clean shutdown: stops the timer and removes this session's snapshot, so
    // the next launch does not offer to recover a project that was closed
    // normally. Safe to call more than once.
    void stop();

    // Snapshot immediately, ignoring the modified check. Returns false and
    // sets errorOut on failure.
    bool saveNow(QString* errorOut = nullptr);

    bool isRunning() const;
    QString sessionId() const;
    QString sessionSnapshotPath() const;
    QDateTime lastSaveTime() const;

    // Re-reads the preferences and restarts the timer. Call after the user
    // changes the autosave settings so the change takes effect immediately.
    void reloadSettings();

signals:
    void autosaved(const QString& path, const QDateTime& when);
    void autosaveFailed(const QString& error);

private:
    void onTimeout();
    QString metaPath() const;
    bool writeMeta();

    Project* m_project = nullptr;
    std::function<bool()> m_isModified;
    QTimer* m_timer = nullptr;
    QString m_sessionId;
    QDateTime m_lastSave;
    bool m_stopped = false;
};

} // namespace hc
