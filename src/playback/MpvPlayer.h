#pragma once
#include <QObject>
#include <QString>
#include <mpv/client.h>
#include <mpv/render_gl.h>

namespace hc {

// Thin Qt wrapper around a libmpv client handle. This owns the mpv core
// (demux + decode + audio output) and is used for **source preview**: play
// a single raw media file (from the Media Pool) with real audio+video sync,
// something the custom FFmpeg/OpenGL/QAudioSink pipeline in
// PlaybackController does not do (that one only ever draws video frames and
// mixes *timeline* audio, it never plays back a lone source file).
//
// MpvPlayer only talks to the mpv *client* API (mpv_command, properties,
// events) — it does not touch OpenGL at all. Actual frame rendering is done
// by MpvVideoWidget via the separate libmpv *render* API, which needs a
// live GL context and therefore lives in the widget instead.
class MpvPlayer : public QObject {
    Q_OBJECT
public:
    explicit MpvPlayer(QObject* parent = nullptr);
    ~MpvPlayer() override;

    // Not copyable/movable: wraps a raw mpv_handle*.
    MpvPlayer(const MpvPlayer&) = delete;
    MpvPlayer& operator=(const MpvPlayer&) = delete;

    mpv_handle* handle() const { return m_mpv; }

    void loadFile(const QString& path);
    void play();
    void pause();
    void togglePause();
    bool isPaused() const { return m_paused; }

    // seconds, absolute, clamped by mpv itself to [0, duration]
    void seek(double seconds);
    void setVolume(int volume0to100);
    void setMuted(bool muted);

signals:
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pausedChanged(bool paused);
    void endOfFile();
    void mpvError(QString message);

private slots:
    void onMpvEvents();

private:
    static void wakeupCallback(void* ctx);
    void handleEvent(mpv_event* event);

    mpv_handle* m_mpv = nullptr;
    bool m_paused = true;
};

} // namespace hc
