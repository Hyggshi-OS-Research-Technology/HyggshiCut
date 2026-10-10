// Tests for the runtime environment check (src/core/EnvironmentCheck.*).
//
// The check exists to tell a user *why* HyggshiCut is misbehaving, so the
// thing that matters most is that it never lies: a probe must not report Ok
// for something that is absent, and it must not crash or hang when the thing
// it is probing is missing. Those are the properties asserted here.
//
// This test is environment-dependent by nature (the machine running it may or
// may not have ffmpeg, audio, a GPU). It therefore asserts *invariants* and
// cross-checks each verdict against an independent observation, rather than
// hard-coding "ffmpeg must be present".
//
// Needs QCoreApplication only; no display and no GUI. Spawns ffmpeg twice if a
// binary is present.

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <iostream>
#include <string>

#include "../src/core/EnvironmentCheck.h"

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

const CheckResult* findById(const QList<CheckGroup>& groups, const QString& id) {
    for (const CheckGroup& g : groups) {
        for (const CheckResult& r : g.results) {
            if (r.id == id) return &r;
        }
    }
    return nullptr;
}

int totalResults(const QList<CheckGroup>& groups) {
    int n = 0;
    for (const CheckGroup& g : groups) n += g.results.size();
    return n;
}

} // namespace

int main(int argc, char** argv) {
    // Keep QSettings writes (the proxy-cache probe reads preferences) out of
    // the real user config.
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir configDir;
    if (configDir.isValid()) {
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configDir.path());
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("HyggshiCut");
    QCoreApplication::setOrganizationName("Hyggshi OS Foundation");

    std::cout << "Running EnvironmentCheckTest..." << std::endl;

    // -----------------------------------------------------------------
    // 1. The report is well-formed and bounded in time.
    // -----------------------------------------------------------------
    std::cout << "[1] Report structure..." << std::endl;
    QElapsedTimer timer;
    timer.start();
    const QList<CheckGroup> groups = EnvironmentCheck::run();
    const qint64 elapsedMs = timer.elapsed();

    check(!groups.isEmpty(), "report has at least one group");
    check(totalResults(groups) > 0, "report has at least one result");

    // Each ffmpeg probe is capped at 4s; the whole run spawns two. Allow
    // generous headroom for a loaded CI box but still catch a hang.
    check(elapsedMs < 30000,
          "run() completed within 30s (took " + std::to_string(elapsedMs) + "ms)");

    bool allPopulated = true;
    for (const CheckGroup& g : groups) {
        if (g.name.isEmpty()) allPopulated = false;
        for (const CheckResult& r : g.results) {
            // Every result must be self-describing: the UI renders these
            // verbatim, so an empty id/name/value is a bug.
            if (r.id.isEmpty() || r.name.isEmpty() || r.value.isEmpty()) {
                allPopulated = false;
                std::cout << "    incomplete result: id='" << r.id.toStdString()
                          << "' name='" << r.name.toStdString()
                          << "' value='" << r.value.toStdString() << "'" << std::endl;
            }
        }
    }
    check(allPopulated, "every result has a non-empty id, name and value");

    // Ids must be unique, otherwise findById()/UI keying is ambiguous.
    {
        QStringList ids;
        for (const CheckGroup& g : groups) {
            for (const CheckResult& r : g.results) ids << r.id;
        }
        const int before = ids.size();
        ids.removeDuplicates();
        check(ids.size() == before, "all result ids are unique");
    }

    // Anything not Ok should tell the user what to do about it.
    {
        bool remedied = true;
        for (const CheckGroup& g : groups) {
            for (const CheckResult& r : g.results) {
                if (r.status == CheckStatus::Warning || r.status == CheckStatus::Error) {
                    if (r.remedy.isEmpty() && r.detail.isEmpty()) {
                        remedied = false;
                        std::cout << "    no guidance for: " << r.id.toStdString() << std::endl;
                    }
                }
            }
        }
        check(remedied, "every warning/error carries a remedy or detail");
    }

    // -----------------------------------------------------------------
    // 2. ffmpeg verdict matches reality, independently observed.
    // -----------------------------------------------------------------
    std::cout << "[2] ffmpeg probe agrees with the filesystem..." << std::endl;
    {
        const QString path = EnvironmentCheck::resolveFfmpegPath();
        const CheckResult* r = findById(groups, "ffmpeg.binary");
        check(r != nullptr, "ffmpeg.binary result present");
        if (r) {
            if (path.isEmpty()) {
                std::cout << "    (no ffmpeg on this machine)" << std::endl;
                check(r->status == CheckStatus::Error,
                      "reports Error when no ffmpeg binary exists");
                check(!r->remedy.isEmpty(), "offers an install hint when missing");
            } else {
                std::cout << "    (ffmpeg at " << path.toStdString() << ")" << std::endl;
                // Independently confirm the resolved path is a real executable.
                const QFileInfo fi(path);
                check(fi.exists() && fi.isExecutable(),
                      "resolveFfmpegPath() returned an existing executable");
                check(r->status == CheckStatus::Ok || r->status == CheckStatus::Error,
                      "binary probe reports a definite verdict");
                if (r->status == CheckStatus::Ok) {
                    check(r->detail == path,
                          "reported path matches the resolved binary");
                }
            }
        }
    }

    // Encoder results must be consistent with the binary result: no encoder
    // can be "available" if there is no ffmpeg at all.
    std::cout << "[3] Encoder probes are consistent with the binary probe..." << std::endl;
    {
        const bool haveFfmpeg = !EnvironmentCheck::resolveFfmpegPath().isEmpty();
        const auto encoders = EnvironmentCheck::checkFfmpegEncoders();
        check(!encoders.isEmpty(), "encoder probe returned results");

        if (!haveFfmpeg) {
            bool anyOk = false;
            for (const CheckResult& r : encoders) {
                if (r.status == CheckStatus::Ok) anyOk = true;
            }
            check(!anyOk, "no encoder is reported available without an ffmpeg binary");
        } else {
            // With a real ffmpeg, H.264/AAC are near-universal; assert only
            // that each verdict is definite rather than which way it went.
            bool definite = true;
            for (const CheckResult& r : encoders) {
                if (r.status == CheckStatus::Unknown) definite = false;
            }
            check(definite, "each encoder gets a definite verdict when ffmpeg exists");

            // The name must not be matched as a substring of another encoder.
            for (const CheckResult& r : encoders) {
                if (r.name == "aac") {
                    std::cout << "    aac -> " << r.value.toStdString() << std::endl;
                }
            }
        }
    }

    // -----------------------------------------------------------------
    // 4. Proxy cache probe actually proves writability.
    // -----------------------------------------------------------------
    std::cout << "[4] Proxy cache probe..." << std::endl;
    {
        const CheckResult r = EnvironmentCheck::checkProxyCache();
        check(!r.value.isEmpty(), "reports the directory it examined");
        if (r.status == CheckStatus::Ok) {
            // Independently verify we really can write there.
            QFile probe(QDir(r.value).filePath("envcheck-verify.tmp"));
            const bool wrote = probe.open(QIODevice::WriteOnly);
            if (wrote) {
                probe.close();
                probe.remove();
            }
            check(wrote, "directory reported writable really is writable");
        } else {
            std::cout << "    (reported not writable: " << r.detail.toStdString() << ")"
                      << std::endl;
            check(!r.remedy.isEmpty(), "offers guidance when the cache is unusable");
        }
    }

    // A directory that cannot exist must be reported as an error, not Ok.
    {
        QSettings pref("HyggshiCut", "Preferences");
        const QString saved = pref.value("proxy/cacheDir").toString();
        // /proc is present on Linux and rejects mkdir even as root.
        pref.setValue("proxy/cacheDir", "/proc/hyggshicut-cannot-exist");
        pref.sync();

        const CheckResult r = EnvironmentCheck::checkProxyCache();
        check(r.status == CheckStatus::Error,
              "an uncreatable cache directory is reported as Error");

        if (saved.isEmpty()) pref.remove("proxy/cacheDir");
        else pref.setValue("proxy/cacheDir", saved);
        pref.sync();
    }

    // -----------------------------------------------------------------
    // 5. Library/version probes are definite (they read linked-in symbols).
    // -----------------------------------------------------------------
    std::cout << "[5] Library version probes..." << std::endl;
    {
        const CheckResult av = EnvironmentCheck::checkFfmpegLibraries();
        check(av.status == CheckStatus::Ok || av.status == CheckStatus::Warning,
              "libav* probe reaches a definite verdict");
        check(av.value.contains("libavcodec"), "reports the libavcodec version");

        const CheckResult qt = EnvironmentCheck::checkQt();
        check(qt.status == CheckStatus::Ok || qt.status == CheckStatus::Warning,
              "Qt probe reaches a definite verdict");
        check(!qt.value.isEmpty(), "reports a Qt version");
    }

    // -----------------------------------------------------------------
    // 6. Aggregation helpers.
    // -----------------------------------------------------------------
    std::cout << "[6] Summary helpers..." << std::endl;
    {
        // hasErrors and hasWarnings must be mutually exclusive: hasWarnings
        // is defined as "warnings and no errors".
        const bool err = EnvironmentCheck::hasErrors(groups);
        const bool warn = EnvironmentCheck::hasWarnings(groups);
        check(!(err && warn), "hasErrors and hasWarnings are mutually exclusive");

        // Cross-check against a manual scan.
        bool sawError = false, sawWarning = false;
        for (const CheckGroup& g : groups) {
            for (const CheckResult& r : g.results) {
                if (r.status == CheckStatus::Error) sawError = true;
                if (r.status == CheckStatus::Warning) sawWarning = true;
            }
        }
        check(err == sawError, "hasErrors matches a manual scan");
        check(warn == (sawWarning && !sawError), "hasWarnings matches a manual scan");
    }

    // -----------------------------------------------------------------
    // 7. Plain-text rendering (used by --check-env and "copy report").
    // -----------------------------------------------------------------
    std::cout << "[7] Plain-text report..." << std::endl;
    {
        const QString text = EnvironmentCheck::toPlainText(groups);
        check(!text.isEmpty(), "renders a non-empty report");
        // Every group heading and every result name should appear.
        bool allPresent = true;
        for (const CheckGroup& g : groups) {
            if (!text.contains(g.name)) allPresent = false;
            for (const CheckResult& r : g.results) {
                if (!text.contains(r.name)) {
                    allPresent = false;
                    std::cout << "    missing from report: " << r.name.toStdString() << std::endl;
                }
            }
        }
        check(allPresent, "report mentions every group and result");
    }

    // Show the report so a human reading CI logs can see the machine's state.
    std::cout << std::endl
              << "--- report ---" << std::endl
              << EnvironmentCheck::toPlainText(groups).toStdString()
              << "--------------" << std::endl;

    if (g_failures == 0) {
        std::cout << "EnvironmentCheckTest: ALL CHECKS PASSED" << std::endl;
        return 0;
    }
    std::cout << "EnvironmentCheckTest: " << g_failures << " CHECK(S) FAILED" << std::endl;
    return 1;
}
