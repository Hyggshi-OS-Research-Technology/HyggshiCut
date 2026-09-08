#include "EnvironmentCheck.h"
#include "SystemInfo.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QStringList>
#include <QSysInfo>
#include <QtGlobal>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#if defined(__has_include)
#  if __has_include(<alsa/asoundlib.h>)
#    define HC_HAVE_ALSA 1
#    include <alsa/asoundlib.h>
#  endif
#endif

namespace hc {

namespace {

// ffmpeg probes must never hang the UI thread. Every QProcess call below is
// bounded by this.
constexpr int kProcessTimeoutMs = 4000;

// Runs a program and returns its combined output, or an empty QString if it
// could not be started / timed out. `ok` distinguishes "ran, printed nothing"
// from "did not run".
QString runProcess(const QString& program, const QStringList& args, bool* ok = nullptr) {
    if (ok) *ok = false;
    if (program.isEmpty()) return {};

    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start();
    if (!p.waitForStarted(kProcessTimeoutMs)) return {};
    if (!p.waitForFinished(kProcessTimeoutMs)) {
        p.kill();
        p.waitForFinished(500);
        return {};
    }
    if (ok) *ok = true;
    return QString::fromLocal8Bit(p.readAll());
}

// Human-readable byte size.
QString formatBytes(uint64_t bytes) {
    if (bytes == 0) return QStringLiteral("unknown");
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    constexpr double kMiB = 1024.0 * 1024.0;
    if (bytes >= static_cast<uint64_t>(kGiB)) {
        return QString::number(bytes / kGiB, 'f', 1) + QStringLiteral(" GiB");
    }
    return QString::number(bytes / kMiB, 'f', 0) + QStringLiteral(" MiB");
}

QString versionTriplet(unsigned packed) {
    return QStringLiteral("%1.%2.%3")
        .arg(AV_VERSION_MAJOR(packed))
        .arg(AV_VERSION_MINOR(packed))
        .arg(AV_VERSION_MICRO(packed));
}

// The directory search order used by main.cpp::loadBundledAssets() and
// LanguageManager::discoverBundledLanguages(). Kept in sync with both.
QStringList bundledAssetDirs(const QString& leaf) {
    const QString appDir = QCoreApplication::instance()
                               ? QCoreApplication::applicationDirPath()
                               : QDir::currentPath();
    return {
        QDir(appDir).filePath(leaf),
        QDir(appDir).filePath("../" + leaf),
        QDir(appDir).filePath("../share/hyggshicut/" + leaf),
        QDir::current().filePath(leaf),
        "/usr/local/share/hyggshicut/" + leaf,
        "/usr/share/hyggshicut/" + leaf,
    };
}

CheckResult scanAssetDir(const QString& id, const QString& name, const QString& leaf,
                         const QString& pattern, const QString& remedy) {
    CheckResult r;
    r.id = id;
    r.name = name;

    const QStringList dirs = bundledAssetDirs(leaf);
    for (const QString& dPath : dirs) {
        QDir dir(dPath);
        if (!dir.exists()) continue;
        const auto files = dir.entryInfoList(QStringList() << pattern, QDir::Files);
        if (files.isEmpty()) continue;

        QStringList names;
        names.reserve(files.size());
        for (const auto& fi : files) names << fi.completeBaseName();
        names.sort();

        r.status = CheckStatus::Ok;
        r.value = QStringLiteral("%1 found").arg(files.size());
        r.detail = QStringLiteral("%1\n%2").arg(dir.absolutePath(), names.join(QStringLiteral(", ")));
        return r;
    }

    r.status = CheckStatus::Error;
    r.value = QStringLiteral("none found");
    r.detail = QStringLiteral("Searched:\n") + dirs.join(QChar('\n'));
    r.remedy = remedy;
    return r;
}

} // namespace

QString checkStatusLabel(CheckStatus s) {
    switch (s) {
        case CheckStatus::Ok:      return QStringLiteral("OK");
        case CheckStatus::Warning: return QStringLiteral("WARN");
        case CheckStatus::Error:   return QStringLiteral("FAIL");
        case CheckStatus::Unknown: break;
    }
    return QStringLiteral("? ");
}

QString EnvironmentCheck::resolveFfmpegPath() {
    // Identical resolution order to Exporter::start(), so this reports the
    // binary that would actually be used rather than merely what is in PATH.
    QString prog = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!prog.isEmpty()) return prog;
    for (const char* candidate : {"/usr/bin/ffmpeg", "/usr/local/bin/ffmpeg",
                                  "/snap/bin/ffmpeg", "/bin/ffmpeg"}) {
        const QFileInfo fi{QString::fromLatin1(candidate)};
        if (fi.exists() && fi.isExecutable()) return fi.absoluteFilePath();
    }
    return {};
}

CheckResult EnvironmentCheck::checkFfmpegBinary() {
    CheckResult r;
    r.id = QStringLiteral("ffmpeg.binary");
    r.name = QStringLiteral("ffmpeg executable");

    const QString prog = resolveFfmpegPath();
    if (prog.isEmpty()) {
        r.status = CheckStatus::Error;
        r.value = QStringLiteral("not found");
        r.detail = QStringLiteral(
            "Export and proxy generation run the external ffmpeg binary and "
            "will fail without it.");
        r.remedy = QStringLiteral("Install it, e.g.  sudo apt install ffmpeg");
        return r;
    }

    bool ok = false;
    const QString out = runProcess(prog, {QStringLiteral("-version")}, &ok);
    if (!ok || out.isEmpty()) {
        r.status = CheckStatus::Error;
        r.value = prog;
        r.detail = QStringLiteral("Found the binary but could not run it (`ffmpeg -version` failed).");
        r.remedy = QStringLiteral("Check the file's permissions and that it is not a broken symlink.");
        return r;
    }

    static const QRegularExpression re(QStringLiteral("^ffmpeg version (\\S+)"));
    const auto m = re.match(out);
    r.status = CheckStatus::Ok;
    r.value = m.hasMatch() ? m.captured(1) : QStringLiteral("unknown version");
    r.detail = prog;
    return r;
}

QList<CheckResult> EnvironmentCheck::checkFfmpegEncoders() {
    // The encoders referenced by the export presets and the --codec CLI flag.
    // A missing one only surfaces today as a mid-export failure.
    struct Wanted { const char* encoder; const char* what; bool essential; };
    static const Wanted wanted[] = {
        { "libx264",     "H.264 (default video)",  true  },
        { "aac",         "AAC (default audio)",    true  },
        { "libx265",     "H.265 / HEVC",           false },
        { "libvpx-vp9",  "VP9 (WebM)",             false },
        { "libsvtav1",   "AV1",                    false },
        { "prores_ks",   "Apple ProRes",           false },
        { "libmp3lame",  "MP3",                    false },
        { "pcm_s16le",   "WAV / PCM",              false },
    };

    QList<CheckResult> out;
    const QString prog = resolveFfmpegPath();
    if (prog.isEmpty()) {
        CheckResult r;
        r.id = QStringLiteral("ffmpeg.encoders");
        r.name = QStringLiteral("ffmpeg encoders");
        r.status = CheckStatus::Unknown;
        r.value = QStringLiteral("skipped");
        r.detail = QStringLiteral("Cannot enumerate encoders: no ffmpeg binary.");
        out.append(r);
        return out;
    }

    bool ok = false;
    const QString listing = runProcess(prog, {QStringLiteral("-hide_banner"),
                                              QStringLiteral("-encoders")}, &ok);
    if (!ok) {
        CheckResult r;
        r.id = QStringLiteral("ffmpeg.encoders");
        r.name = QStringLiteral("ffmpeg encoders");
        r.status = CheckStatus::Unknown;
        r.value = QStringLiteral("unavailable");
        r.detail = QStringLiteral("`ffmpeg -encoders` did not complete.");
        out.append(r);
        return out;
    }

    // Lines look like: " V....D libx264   libx264 H.264 ..." — match the name
    // as a standalone token so "aac" does not match "aac_at"/"libfdk_aac".
    for (const auto& w : wanted) {
        const QString name = QString::fromLatin1(w.encoder);
        const QRegularExpression re(
            QStringLiteral("^\\s*\\S+\\s+%1\\s").arg(QRegularExpression::escape(name)),
            QRegularExpression::MultilineOption);
        const bool present = re.match(listing).hasMatch();

        CheckResult r;
        r.id = QStringLiteral("ffmpeg.encoder.") + name;
        r.name = name;
        r.value = present ? QStringLiteral("available") : QStringLiteral("missing");
        r.detail = QString::fromLatin1(w.what);
        if (present) {
            r.status = CheckStatus::Ok;
        } else if (w.essential) {
            r.status = CheckStatus::Error;
            r.remedy = QStringLiteral(
                "Exports using this codec will fail. Install a full ffmpeg build "
                "(Debian/Ubuntu: the `ffmpeg` package).");
        } else {
            r.status = CheckStatus::Warning;
            r.remedy = QStringLiteral("Presets using this codec will be unavailable.");
        }
        out.append(r);
    }
    return out;
}

CheckResult EnvironmentCheck::checkAudioOutput() {
    CheckResult r;
    r.id = QStringLiteral("audio.alsa");
    r.name = QStringLiteral("Audio output (ALSA PCM)");

#if defined(HC_HAVE_ALSA)
    // Open the same device AlsaAudioOutput uses, in non-blocking mode, then
    // close it immediately. This is the only reliable way to tell whether
    // playback will actually work; enumerating cards can succeed while the
    // default device is unusable (e.g. no PipeWire/Pulse bridge in a
    // container).
    snd_pcm_t* pcm = nullptr;
    const int err = snd_pcm_open(&pcm, "default", SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (err == 0 && pcm) {
        snd_pcm_close(pcm);
        r.status = CheckStatus::Ok;
        r.value = QStringLiteral("default device available");
        r.detail = QStringLiteral("Timeline preview audio will play through ALSA.");
    } else {
        r.status = CheckStatus::Warning;
        r.value = QStringLiteral("unavailable");
        r.detail = QStringLiteral("snd_pcm_open(\"default\"): %1")
                       .arg(QString::fromLocal8Bit(snd_strerror(err)));
        r.remedy = QStringLiteral(
            "Preview will be silent; editing and export are unaffected. In a "
            "container, pass through /dev/snd or the PulseAudio/PipeWire socket.");
    }
#else
    r.status = CheckStatus::Unknown;
    r.value = QStringLiteral("not probed");
    r.detail = QStringLiteral("Built without ALSA headers.");
#endif
    return r;
}

CheckResult EnvironmentCheck::checkLanguagePacks() {
    return scanAssetDir(
        QStringLiteral("assets.languages"),
        QStringLiteral("Language packs (.langhc)"),
        QStringLiteral("languages"),
        QStringLiteral("*.langhc"),
        QStringLiteral("The UI will fall back to raw translation keys. Reinstall the "
                       "package, or run from the source tree."));
}

CheckResult EnvironmentCheck::checkPluginPacks() {
    CheckResult r = scanAssetDir(
        QStringLiteral("assets.plugins"),
        QStringLiteral("Effect packs (.plhc)"),
        QStringLiteral("plugins"),
        QStringLiteral("*.plhc"),
        QStringLiteral("The Effects panel will be empty. Reinstall the package, or "
                       "run from the source tree."));
    // Plugins are optional in a way language packs are not: the editor is fully
    // usable without the bundled effect presets.
    if (r.status == CheckStatus::Error) r.status = CheckStatus::Warning;
    return r;
}

CheckResult EnvironmentCheck::checkProxyCache() {
    CheckResult r;
    r.id = QStringLiteral("proxy.cache");
    r.name = QStringLiteral("Proxy cache directory");

    // Mirrors ProxyManager's constructor.
    QSettings pref(QStringLiteral("HyggshiCut"), QStringLiteral("Preferences"));
    const QString custom = pref.value(QStringLiteral("proxy/cacheDir")).toString();
    QString dirPath;
    if (!custom.isEmpty()) {
        dirPath = custom;
    } else {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        dirPath = (base.isEmpty() ? QDir::tempPath() : base) + QStringLiteral("/HyggshiCut/proxies");
    }
    r.value = dirPath;

    if (!QDir().mkpath(dirPath)) {
        r.status = CheckStatus::Error;
        r.detail = QStringLiteral("Could not create the directory.");
        r.remedy = QStringLiteral("Pick a writable location in Settings > Proxy.");
        return r;
    }

    // mkpath succeeding does not prove writability (read-only mount, quota),
    // so actually create a file.
    QTemporaryFile probe(QDir(dirPath).filePath(QStringLiteral("envcheck-XXXXXX.tmp")));
    if (!probe.open()) {
        r.status = CheckStatus::Error;
        r.detail = QStringLiteral("Directory exists but is not writable.");
        r.remedy = QStringLiteral("Pick a writable location in Settings > Proxy.");
        return r;
    }
    probe.close();

    r.status = CheckStatus::Ok;
    r.detail = QStringLiteral("Writable.");
    return r;
}

CheckResult EnvironmentCheck::checkCpu() {
    CheckResult r;
    r.id = QStringLiteral("system.cpu");
    r.name = QStringLiteral("CPU");

    const int cores = systeminfo::cpuCoreCount();
    r.value = QStringLiteral("%1 core%2 (%3)")
                  .arg(cores)
                  .arg(cores == 1 ? QString() : QStringLiteral("s"),
                       QSysInfo::currentCpuArchitecture());
    if (cores >= 4) {
        r.status = CheckStatus::Ok;
    } else {
        r.status = CheckStatus::Warning;
        r.detail = QStringLiteral(
            "Fewer than 4 cores: decoding uses slice-only threading and exports "
            "will be slow.");
        r.remedy = QStringLiteral("Enable proxies (Settings > Proxy) for smoother editing.");
    }
    return r;
}

CheckResult EnvironmentCheck::checkMemory() {
    CheckResult r;
    r.id = QStringLiteral("system.memory");
    r.name = QStringLiteral("Memory");

    const uint64_t total = systeminfo::totalMemoryBytes();
    const uint64_t avail = systeminfo::availableMemoryBytes();
    if (total == 0) {
        r.status = CheckStatus::Unknown;
        r.value = QStringLiteral("unknown");
        return r;
    }

    r.value = avail > 0
                  ? QStringLiteral("%1 total, %2 available").arg(formatBytes(total), formatBytes(avail))
                  : formatBytes(total);

    // 3 GiB is the threshold Decoder uses to drop into slice-only threading.
    constexpr uint64_t kLowMemory = 3ULL * 1024 * 1024 * 1024;
    if (total < kLowMemory) {
        r.status = CheckStatus::Warning;
        r.detail = QStringLiteral(
            "Under 3 GiB: the decoder and exporter switch to low-memory mode "
            "(smaller caches, fewer threads).");
        r.remedy = QStringLiteral("Enable proxies and close other applications while exporting.");
    } else {
        r.status = CheckStatus::Ok;
    }
    return r;
}

CheckResult EnvironmentCheck::checkFfmpegLibraries() {
    CheckResult r;
    r.id = QStringLiteral("ffmpeg.libraries");
    r.name = QStringLiteral("FFmpeg libraries (libav*)");

    // Compiled-against versions come from the headers; running versions from
    // the shared objects actually loaded. A mismatch in the major component is
    // a genuine ABI hazard and explains otherwise-inexplicable decode crashes.
    const unsigned runtimeCodec = avcodec_version();
    const unsigned runtimeFormat = avformat_version();
    const unsigned runtimeUtil = avutil_version();

    r.value = QStringLiteral("libavcodec %1, libavformat %2, libavutil %3")
                  .arg(versionTriplet(runtimeCodec),
                       versionTriplet(runtimeFormat),
                       versionTriplet(runtimeUtil));

    const bool codecMajorDiffers =
        AV_VERSION_MAJOR(runtimeCodec) != AV_VERSION_MAJOR(LIBAVCODEC_VERSION_INT);
    const bool formatMajorDiffers =
        AV_VERSION_MAJOR(runtimeFormat) != AV_VERSION_MAJOR(LIBAVFORMAT_VERSION_INT);
    const bool utilMajorDiffers =
        AV_VERSION_MAJOR(runtimeUtil) != AV_VERSION_MAJOR(LIBAVUTIL_VERSION_INT);

    if (codecMajorDiffers || formatMajorDiffers || utilMajorDiffers) {
        r.status = CheckStatus::Warning;
        r.detail = QStringLiteral("Built against libavcodec %1 / libavformat %2 / libavutil %3.")
                       .arg(versionTriplet(LIBAVCODEC_VERSION_INT),
                            versionTriplet(LIBAVFORMAT_VERSION_INT),
                            versionTriplet(LIBAVUTIL_VERSION_INT));
        r.remedy = QStringLiteral(
            "Major version mismatch between build and runtime libraries; decoding "
            "may misbehave. Rebuild against the installed FFmpeg.");
    } else {
        r.status = CheckStatus::Ok;
        r.detail = QStringLiteral("Matches the versions this build was compiled against.");
    }
    return r;
}

CheckResult EnvironmentCheck::checkQt() {
    CheckResult r;
    r.id = QStringLiteral("qt.version");
    r.name = QStringLiteral("Qt");
    r.value = QString::fromLatin1(qVersion());

    const QString compiled = QStringLiteral(QT_VERSION_STR);
    if (r.value != compiled) {
        r.status = CheckStatus::Warning;
        r.detail = QStringLiteral("Built against Qt %1.").arg(compiled);
        r.remedy = QStringLiteral("Usually harmless within the same major version.");
    } else {
        r.status = CheckStatus::Ok;
    }
    return r;
}

QList<CheckGroup> EnvironmentCheck::run() {
    CheckResult gpu;
    gpu.id = QStringLiteral("gpu.opengl");
    gpu.name = QStringLiteral("OpenGL renderer");
    gpu.status = CheckStatus::Unknown;
    gpu.value = QStringLiteral("not probed");
    gpu.detail = QStringLiteral("Requires a live OpenGL context (GUI only).");
    return runWithGpu(gpu);
}

QList<CheckGroup> EnvironmentCheck::runWithGpu(const CheckResult& gpu) {
    QList<CheckGroup> groups;

    CheckGroup media;
    media.name = QStringLiteral("Media pipeline");
    media.results << checkFfmpegBinary();
    media.results << checkFfmpegLibraries();
    media.results.append(checkFfmpegEncoders());
    groups << media;

    CheckGroup output;
    output.name = QStringLiteral("Rendering & playback");
    output.results << gpu;
    output.results << checkAudioOutput();
    groups << output;

    CheckGroup assets;
    assets.name = QStringLiteral("Bundled assets");
    assets.results << checkLanguagePacks();
    assets.results << checkPluginPacks();
    groups << assets;

    CheckGroup system;
    system.name = QStringLiteral("System");
    system.results << checkQt();
    system.results << checkCpu();
    system.results << checkMemory();
    system.results << checkProxyCache();
    groups << system;

    return groups;
}

QString EnvironmentCheck::toPlainText(const QList<CheckGroup>& groups) {
    QString out;
    out += QStringLiteral("HyggshiCut environment report\n");
    out += QStringLiteral("%1 %2 / Qt %3\n\n")
               .arg(QSysInfo::prettyProductName(),
                    QSysInfo::currentCpuArchitecture(),
                    QString::fromLatin1(qVersion()));

    for (const CheckGroup& g : groups) {
        out += QStringLiteral("== %1 ==\n").arg(g.name);
        for (const CheckResult& r : g.results) {
            out += QStringLiteral("[%1] %2: %3\n")
                       .arg(checkStatusLabel(r.status), r.name, r.value);
            if (!r.detail.isEmpty()) {
                // Indent multi-line details so the report stays readable.
                QString d = r.detail;
                d.replace(QChar('\n'), QStringLiteral("\n       "));
                out += QStringLiteral("       %1\n").arg(d);
            }
            if (!r.remedy.isEmpty() && r.status != CheckStatus::Ok) {
                out += QStringLiteral("       -> %1\n").arg(r.remedy);
            }
        }
        out += QChar('\n');
    }
    return out;
}

bool EnvironmentCheck::hasErrors(const QList<CheckGroup>& groups) {
    for (const CheckGroup& g : groups) {
        for (const CheckResult& r : g.results) {
            if (r.status == CheckStatus::Error) return true;
        }
    }
    return false;
}

bool EnvironmentCheck::hasWarnings(const QList<CheckGroup>& groups) {
    bool warn = false;
    for (const CheckGroup& g : groups) {
        for (const CheckResult& r : g.results) {
            if (r.status == CheckStatus::Error) return false;
            if (r.status == CheckStatus::Warning) warn = true;
        }
    }
    return warn;
}

} // namespace hc
