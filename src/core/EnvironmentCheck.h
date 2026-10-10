#pragma once

// Runtime environment diagnostics.
//
// HyggshiCut depends on a handful of things that are resolved at RUN time, not
// link time, and each of them fails in a way that is confusing when you hit it
// mid-edit rather than up front:
//
//   * the external `ffmpeg` binary   -> every export and every proxy silently
//                                       fails ("khong the chay ffmpeg")
//   * specific ffmpeg encoders       -> the export only fails once it has run
//                                       for a while, for the one codec you
//                                       picked
//   * an OpenGL 3.3 Core context     -> preview falls back to the slow CPU path
//   * an ALSA PCM device             -> timeline preview plays silently
//   * language / plugin packs        -> UI shows raw translation keys and an
//                                       empty Effects list
//   * a writable proxy cache         -> proxy generation fails per-asset
//
// This module probes all of them and reports a structured result, so the UI can
// show one page that says what works and what doesn't. Everything here is
// best-effort and non-throwing: a probe that cannot determine an answer reports
// Status::Unknown rather than guessing.
//
// Deliberately Qt Core + Gui only (no Widgets, no OpenGLWidgets): the headless
// CLI (`--check-env`) must be able to run this without constructing a GUI.

#include <QString>
#include <QStringList>
#include <QList>
#include <QObject>

namespace hc {

enum class CheckStatus {
    Ok,       // present and usable
    Warning,  // usable, but degraded or not the recommended configuration
    Error,    // missing/broken; a core feature will not work
    Unknown   // could not be determined (e.g. probe needs a GUI context)
};

QString checkStatusLabel(CheckStatus s);

struct CheckResult {
    QString id;        // stable machine-readable key, e.g. "ffmpeg.binary"
    QString name;      // human-readable label
    QString value;     // what was found ("6.1.1", "/usr/bin/ffmpeg", ...)
    QString detail;    // extra context, or the reason for a failure
    QString remedy;    // actionable hint when status != Ok
    CheckStatus status = CheckStatus::Unknown;
};

struct CheckGroup {
    QString name;
    QList<CheckResult> results;
};

// A full environment report. Cheap probes run inline; the ffmpeg probes spawn a
// short-lived process (bounded by a timeout), so this can take a moment.
class EnvironmentCheck {
public:
    // Runs every probe that does not require an OpenGL context.
    //
    // The OpenGL entry is reported as Unknown by this function, because a valid
    // context only exists inside a GL widget. Call the GUI-side helper (see
    // WindowSettingsDialog) or reportWithGpu() to fill it in.
    static QList<CheckGroup> run();

    // Same as run(), but substitutes a GPU/OpenGL result gathered by the
    // caller (which does have a live GL context).
    static QList<CheckGroup> runWithGpu(const CheckResult& gpu);

    // ---- individual probes (exposed for testing) ----

    // Resolves the ffmpeg binary the way Exporter does. Empty if not found.
    static QString resolveFfmpegPath();

    // `ffmpeg -version`, parsed. Status is Error when the binary is missing.
    static CheckResult checkFfmpegBinary();

    // Verifies the encoders the export presets actually reference are
    // compiled into the ffmpeg that was found.
    static QList<CheckResult> checkFfmpegEncoders();

    // ALSA PCM device availability (timeline preview audio).
    static CheckResult checkAudioOutput();

    // Bundled .langhc / .plhc discovery, mirroring the search order used by
    // main.cpp and LanguageManager.
    static CheckResult checkLanguagePacks();
    static CheckResult checkPluginPacks();

    // Proxy cache directory: resolved the same way ProxyManager does, then
    // probed for writability.
    static CheckResult checkProxyCache();

    // CPU / RAM, versus the thresholds the decoder and exporter use to switch
    // into low-memory mode.
    static CheckResult checkCpu();
    static CheckResult checkMemory();

    // Compile-time vs run-time library versions (libav*, Qt).
    static CheckResult checkFfmpegLibraries();
    static CheckResult checkQt();

    // Renders a report as plain text, for the CLI and for "copy to clipboard".
    static QString toPlainText(const QList<CheckGroup>& groups);

    // True if any result in the report is an Error.
    static bool hasErrors(const QList<CheckGroup>& groups);
    // True if any result is a Warning (and none is an Error).
    static bool hasWarnings(const QList<CheckGroup>& groups);
};

} // namespace hc
