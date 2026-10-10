// Tests for autosave / crash recovery (src/core/Autosave.*) and for the
// atomic-save guarantee in Project::saveToFile().
//
// These are data-loss paths, so the tests are written around the ways the
// feature could silently destroy work rather than around its happy path:
//
//   * an autosave must not repoint Project::filePath at the recovery file,
//     or the user's next Ctrl+S overwrites the snapshot instead of their
//     project;
//   * a clean exit must remove the snapshot, or every launch nags about
//     recovering a project that was closed deliberately;
//   * a crashed session must be found, and a live one must not be;
//   * a failed save must leave the previous .hcproj intact and parseable.
//
// Headless: QCoreApplication only, no display, no media, no ffmpeg.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDateTime>

#include <iostream>
#include <string>

#include "../src/core/Autosave.h"
#include "../src/core/Project.h"
#include "../src/core/CxxFeatures.h"

using namespace hc;

namespace {

int g_failures = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "  [ok]   " << what << std::endl;
    } else {
        std::cout << "  [FAIL] " << what << std::endl;
        ++g_failures;
    }
}

// A pid that is almost certainly not running. The kernel caps pids at
// /proc/sys/kernel/pid_max (4194304 by default), so this is out of range.
constexpr qint64 kDeadPid = 4194303;

bool writeSessionFiles(const QString& sessionId, qint64 pid, const QString& originalPath,
                       const QString& projectName, const QDateTime& savedAt,
                       bool withSnapshot = true) {
    const QDir dir(AutosaveManager::recoveryDir());

    if (withSnapshot) {
        // A minimal but genuinely loadable .hcproj, so recovery can be
        // exercised end to end rather than just the bookkeeping.
        Project p;
        p.name = projectName;
        QString err;
        if (!p.saveToFile(dir.filePath(sessionId + ".hcproj"), &err)) {
            std::cout << "    could not write snapshot: " << err.toStdString() << std::endl;
            return false;
        }
    }

    QJsonObject o;
    o["sessionId"] = sessionId;
    o["pid"] = static_cast<double>(pid);
    o["originalPath"] = originalPath;
    o["projectName"] = projectName;
    o["savedAt"] = savedAt.toString(Qt::ISODate);
    o["executable"] = "HyggshiCut";

    QFile f(dir.filePath(sessionId + ".session.json"));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson());
    f.close();
    return true;
}

void clearRecoveryDir() {
    QDir dir(AutosaveManager::recoveryDir());
    for (const QString& n : dir.entryList(QDir::Files)) {
        QFile::remove(dir.filePath(n));
    }
}

} // namespace

int main(int argc, char** argv) {
    // Keep every QSettings write and the recovery directory inside a temp
    // dir, so running the suite never touches the developer's real config
    // or their actual recovery snapshots.
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir configDir;
    if (configDir.isValid()) {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configDir.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("HyggshiCut");
    QCoreApplication::setOrganizationName("Hyggshi OS Foundation");

    std::cout << "Running AutosaveTest..." << std::endl;
    std::cout << "  (building as C++" << (__cplusplus >= 202100L ? "23" : "20")
              << ", std::expected=" << HC_HAS_STD_EXPECTED << ")" << std::endl;

    clearRecoveryDir();

    // -----------------------------------------------------------------
    std::cout << "[1] Recovery directory..." << std::endl;
    {
        const QString dir = AutosaveManager::recoveryDir();
        check(!dir.isEmpty(), "recoveryDir() returns a path");
        check(QFileInfo(dir).isDir(), "recovery directory exists after the call");

        // It must not live inside the user's project folder; snapshots are
        // private scratch state.
        check(!dir.contains("/tmp/hyggshicut-project"), "recovery dir is app data, not project data");
    }

    // -----------------------------------------------------------------
    std::cout << "[2] Interval preference is clamped..." << std::endl;
    {
        AutosaveManager::setIntervalSeconds(1);
        check(AutosaveManager::intervalSeconds() >= AutosaveManager::kMinIntervalSec,
              "a 1-second interval is clamped up to the minimum");

        AutosaveManager::setIntervalSeconds(99999);
        check(AutosaveManager::intervalSeconds() <= AutosaveManager::kMaxIntervalSec,
              "an absurd interval is clamped down to the maximum");

        AutosaveManager::setIntervalSeconds(60);
        check(AutosaveManager::intervalSeconds() == 60, "an in-range interval is kept verbatim");

        AutosaveManager::setEnabled(false);
        check(!AutosaveManager::isEnabled(), "enabled flag round-trips false");
        AutosaveManager::setEnabled(true);
        check(AutosaveManager::isEnabled(), "enabled flag round-trips true");
    }

    // -----------------------------------------------------------------
    // The critical one: autosaving must not hijack Project::filePath.
    // -----------------------------------------------------------------
    std::cout << "[3] Autosave does not repoint the project at the snapshot..." << std::endl;
    {
        QTemporaryDir work;
        const QString userProject = QDir(work.path()).filePath("my_film.hcproj");

        Project proj;
        proj.name = "My Film";
        QString err;
        check(proj.saveToFile(userProject, &err), "project saves to the user's chosen path");
        check(proj.filePath == userProject, "filePath points at the user's file after a real save");

        AutosaveManager mgr;
        mgr.start(&proj, [] { return true; });

        check(mgr.saveNow(&err), "saveNow() succeeds (" + err.toStdString() + ")");
        check(QFileInfo::exists(mgr.sessionSnapshotPath()), "a snapshot file was written");

        // If this regressed, the next Ctrl+S would write into the recovery
        // directory and the user's actual project would stop being updated.
        check(proj.filePath == userProject,
              "filePath STILL points at the user's file after an autosave");

        // The snapshot must be independently loadable, not just present.
        Project reloaded;
        QString lerr;
        check(reloaded.loadFromFile(mgr.sessionSnapshotPath(), &lerr),
              "the snapshot is a loadable .hcproj");
        check(reloaded.name == "My Film", "the snapshot preserves project content");

        // ... and the metadata must point back at the real file, so recovery
        // can restore the association.
        QFile mf(QDir(AutosaveManager::recoveryDir())
                     .filePath(mgr.sessionId() + ".session.json"));
        if (mf.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(mf.readAll()).object();
            check(o.value("originalPath").toString() == userProject,
                  "metadata records the user's original path");
        } else {
            check(false, "metadata file is readable");
        }

        std::cout << "[4] A clean stop() removes the snapshot..." << std::endl;
        const QString snap = mgr.sessionSnapshotPath();
        mgr.stop();
        check(!QFileInfo::exists(snap), "snapshot deleted on clean shutdown");
        check(!QFileInfo::exists(QDir(AutosaveManager::recoveryDir())
                                     .filePath(mgr.sessionId() + ".session.json")),
              "metadata deleted on clean shutdown");
    }

    // -----------------------------------------------------------------
    std::cout << "[5] Crashed sessions are found, live ones are not..." << std::endl;
    {
        clearRecoveryDir();

        const QDateTime older = QDateTime::currentDateTime().addSecs(-600);
        const QDateTime newer = QDateTime::currentDateTime().addSecs(-60);

        check(writeSessionFiles("session-dead-1", kDeadPid, "/home/u/a.hcproj", "Older", older),
              "wrote a crashed session (older)");
        check(writeSessionFiles("session-dead-2", kDeadPid, "", "Newer", newer),
              "wrote a crashed session (newer, never saved)");
        // Our own pid is alive by definition: this stands in for "a second
        // instance is running right now", which must not be offered.
        check(writeSessionFiles("session-live", QCoreApplication::applicationPid(),
                                "/home/u/live.hcproj", "Live", newer),
              "wrote a session owned by a live process");

        const QList<RecoverySession> found = AutosaveManager::findRecoverableSessions();
        check(found.size() == 2,
              "exactly the two dead sessions are offered (got " +
                  std::to_string(found.size()) + ")");

        bool sawLive = false;
        for (const RecoverySession& s : found) {
            if (s.sessionId == "session-live") sawLive = true;
        }
        check(!sawLive, "a session owned by a running process is NOT offered");

        if (found.size() == 2) {
            check(found[0].savedAt >= found[1].savedAt, "results are newest-first");
            check(found[0].projectName == "Newer", "newest session is the one reported first");
            check(found[0].originalPath.isEmpty(),
                  "a never-saved project reports an empty original path");
            check(!found[1].originalPath.isEmpty(),
                  "a previously-saved project reports its original path");
        }

        std::cout << "[6] discardSession removes both files..." << std::endl;
        check(AutosaveManager::discardSession("session-dead-1"), "discard reports success");
        const QList<RecoverySession> after = AutosaveManager::findRecoverableSessions();
        check(after.size() == 1, "discarded session is gone");
    }

    // -----------------------------------------------------------------
    std::cout << "[7] Junk in the recovery directory is self-healing..." << std::endl;
    {
        clearRecoveryDir();
        const QDir dir(AutosaveManager::recoveryDir());

        // Corrupt metadata: must be dropped, not re-read forever.
        QFile bad(dir.filePath("session-bad.session.json"));
        bad.open(QIODevice::WriteOnly);
        bad.write("{ this is not json");
        bad.close();

        // Metadata with no snapshot: nothing to recover from.
        writeSessionFiles("session-orphan", kDeadPid, "", "Orphan",
                          QDateTime::currentDateTime(), /*withSnapshot=*/false);

        const QList<RecoverySession> found = AutosaveManager::findRecoverableSessions();
        check(found.isEmpty(), "neither corrupt nor orphaned metadata is offered");
        check(!QFileInfo::exists(dir.filePath("session-bad.session.json")),
              "corrupt metadata is cleaned up");
        check(!QFileInfo::exists(dir.filePath("session-orphan.session.json")),
              "orphaned metadata is cleaned up");
    }

    // -----------------------------------------------------------------
    // Project::saveToFile() used to open the real file with Truncate, so a
    // failure part-way through destroyed the previous version.
    // -----------------------------------------------------------------
    std::cout << "[8] A failed save leaves the previous project intact..." << std::endl;
    {
        QTemporaryDir work;
        const QString target = QDir(work.path()).filePath("important.hcproj");

        Project good;
        good.name = "Important Work";
        QString err;
        check(good.saveToFile(target, &err), "baseline save succeeds");

        const QByteArray before = [&] {
            QFile f(target);
            f.open(QIODevice::ReadOnly);
            return f.readAll();
        }();
        check(!before.isEmpty(), "baseline file has content");

        // Make the directory read-only so the save cannot complete. QSaveFile
        // writes a temporary alongside the target and renames it, so this
        // fails at open/commit without touching the original.
        QFile::setPermissions(work.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner);

        Project replacement;
        replacement.name = "Should Not Land";
        QString saveErr;
        const bool saved = replacement.saveToFile(target, &saveErr);

        // Restore permissions before asserting, so the temp dir can clean up
        // even if an assertion below fails.
        QFile::setPermissions(work.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                               QFileDevice::ExeOwner);

        if (saved) {
            // Some filesystems / running as root ignore the permission bits.
            std::cout << "    (SKIP: the filesystem allowed the write anyway)" << std::endl;
        } else {
            check(!saveErr.isEmpty(), "a failed save reports an error");

            QFile f(target);
            check(f.open(QIODevice::ReadOnly), "the original file still exists");
            const QByteArray after = f.readAll();
            f.close();

            check(after == before, "the original file is byte-for-byte unchanged");

            QJsonParseError pe{};
            QJsonDocument::fromJson(after, &pe);
            check(pe.error == QJsonParseError::NoError,
                  "the original file is still valid JSON (not truncated)");

            Project check2;
            QString lerr;
            check(check2.loadFromFile(target, &lerr), "the original project still loads");
            check(check2.name == "Important Work", "the original content is preserved");
        }
    }

    // -----------------------------------------------------------------
    std::cout << "[9] A disabled autosave does not run..." << std::endl;
    {
        clearRecoveryDir();
        AutosaveManager::setEnabled(false);

        Project proj;
        proj.name = "Disabled";
        AutosaveManager mgr;
        mgr.start(&proj, [] { return true; });
        check(!mgr.isRunning(), "timer is not started when autosave is disabled");

        AutosaveManager::setEnabled(true);
        mgr.reloadSettings();
        check(mgr.isRunning(), "re-enabling and reloading starts the timer");
        mgr.stop();
        check(!mgr.isRunning(), "stop() halts the timer");
    }

    clearRecoveryDir();

    if (g_failures == 0) {
        std::cout << "AutosaveTest: ALL CHECKS PASSED" << std::endl;
        return 0;
    }
    std::cout << "AutosaveTest: " << g_failures << " CHECK(S) FAILED" << std::endl;
    return 1;
}
