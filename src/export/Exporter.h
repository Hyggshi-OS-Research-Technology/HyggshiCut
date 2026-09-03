#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QList>
#include <QPair>
#include "../core/Project.h"

namespace hc {

// Exporter renders the current Timeline to a single output file.
//
// Design choice: rather than hand-rolling an encoder/muxer with libavcodec,
// Exporter builds an `ffmpeg -filter_complex ...` command line and runs the
// system ffmpeg binary via QProcess. This is the same approach several
// well-known lightweight/open-source editors use under the hood — it gets
// battle-tested encoding (x264/x265/aac, threading, rate control) for free
// and keeps this codebase small. The video composition math (which clips
// are on screen at time t) reuses Timeline::activeVideoClipsAt/
// videoBoundaryTimes, so what you see in the live preview is exactly what
// gets exported.
//
// Current scope: every visible video-track clip active at a given moment is
// composited bottom-to-top with its own Free Transform (position/scale/
// rotation) and opacity via ffmpeg's `overlay`, so a clip shrunk or moved
// off-center lets the layers beneath it show through (picture-in-picture)
// instead of blotting them out; audio is mixed per-clip with volume +
// position + fades — this includes the audio embedded in video/image files,
// not just clips sitting on an Audio track. Text overlays are rendered to
// transparent PNGs (via Qt, so no fontconfig dependency) and composited with
// the ffmpeg `overlay` filter using `enable='between(t,...)'`.
class Exporter : public QObject {
    Q_OBJECT
public:
    struct Settings {
        QString outputPath;
        int width = 1920;
        int height = 1080;
        double frameRate = 30.0;

        // Video options
        QString videoCodec = "libx264";        // "libx264", "libx265", "libvpx-vp9", "libsvtav1", "prores_ks", "copy", "none"
        QString rateControlMode = "bitrate";   // "bitrate" or "crf"
        int videoBitrateKbps = 8000;
        int crf = 21;                          // 0..51
        QString preset = "medium";             // "ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"
        QString pixelFormat = "yuv420p";

        // Audio options
        QString audioCodec = "aac";            // "aac", "libmp3lame", "libopus", "pcm_s16le", "flac", "none"
        int audioBitrateKbps = 192;
        int audioSampleRate = 48000;           // 44100, 48000, 96000
        int audioChannels = 2;                 // 1 (mono), 2 (stereo), 6 (5.1)
    };

    explicit Exporter(Project* project, QObject* parent = nullptr);
    ~Exporter() override;

    void start(const Settings& settings);
    void cancel();
    bool isRunning() const { return m_process != nullptr; }

    // Builds the ffmpeg argv (and, via filterGraphDebugOut, the raw
    // -filter_complex string) without spawning ffmpeg. Public so tests can
    // inspect filter-graph size/shape (e.g. segment count bounding) offline,
    // without needing real media files or a running ffmpeg — start() is
    // still the normal entry point for an actual export.
    QStringList buildFfmpegArgs(const Settings& settings, QString* filterGraphDebugOut = nullptr);

signals:
    void progress(double fraction, QString etaText);
    void finished(bool success, QString message);

private slots:
    void onReadyReadStandardOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError error);

private:
    Project* m_project;
    QProcess* m_process = nullptr;
    Ticks m_totalDurationTicks = 0;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QTemporaryDir m_tempDir; // PNG rasters for text overlays
    bool m_finishedEmitted = false; // guards against duplicate finished() emissions
    // Accumulated from the most-recent ffmpeg -progress block for UI display.
    float m_lastFps = 0.0f;
    float m_lastSpeed = 0.0f;
};

} // namespace hc
