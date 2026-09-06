#include "ScreenRecorder.h"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusMessage>
#include <QDBusConnection>
#include <QVariantMap>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <QDebug>

namespace hc {

ScreenRecorder::ScreenRecorder(QObject* parent)
    : QObject(parent) {
    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout, this, &ScreenRecorder::onCountdownTimeout);

    m_elapsedTimer = new QTimer(this);
    m_elapsedTimer->setInterval(1000);
    connect(m_elapsedTimer, &QTimer::timeout, this, &ScreenRecorder::onElapsedTimerTimeout);
}

ScreenRecorder::~ScreenRecorder() {
    cancel();
}

void ScreenRecorder::setState(ScreenRecorderState newState) {
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(m_state);
    }
}

bool ScreenRecorder::isGnomeScreencastAvailable() {
    if (!QDBusConnection::sessionBus().isConnected()) return false;
    QDBusInterface iface("org.gnome.Shell.Screencast",
                         "/org/gnome/Shell/Screencast",
                         "org.gnome.Shell.Screencast",
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) return false;
    QVariant val = iface.property("ScreencastSupported");
    return val.isValid() && val.toBool();
}

bool ScreenRecorder::start(const ScreenRecordSettings& settings) {
    if (m_state != ScreenRecorderState::Idle) return false;

    m_settings = settings;
    if (m_settings.outputFolder.isEmpty()) {
        m_settings.outputFolder = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        if (m_settings.outputFolder.isEmpty()) {
            m_settings.outputFolder = QDir::homePath() + "/Videos";
        }
    }
    QDir().mkpath(m_settings.outputFolder);

    m_elapsedSeconds = 0;

    if (m_settings.countdownSeconds > 0) {
        setState(ScreenRecorderState::Countdown);
        m_countdownRemaining = m_settings.countdownSeconds;
        emit countdownTick(m_countdownRemaining);
        m_countdownTimer->start();
    } else {
        startActualRecording();
    }

    return true;
}

void ScreenRecorder::onCountdownTimeout() {
    m_countdownRemaining--;
    emit countdownTick(m_countdownRemaining);
    if (m_countdownRemaining <= 0) {
        m_countdownTimer->stop();
        startActualRecording();
    }
}

void ScreenRecorder::startActualRecording() {
    setState(ScreenRecorderState::Recording);

    const QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
    const QString baseName = QString("ScreenRecord_%1").arg(timestamp);
    m_finalOutputPath = m_settings.outputFolder + "/" + baseName + ".mp4";

    // 1. Determine Video Backend
    if (isGnomeScreencastAvailable()) {
        m_activeBackend = ScreenRecordBackendType::GnomeScreencast;
        QDBusInterface iface("org.gnome.Shell.Screencast",
                             "/org/gnome/Shell/Screencast",
                             "org.gnome.Shell.Screencast",
                             QDBusConnection::sessionBus());

        QVariantMap options;
        options.insert("draw-cursor", true);
        options.insert("framerate", m_settings.fps > 0 ? m_settings.fps : 30);

        const QString templateName = baseName + "_%d_%t";
        QDBusMessage reply;
        if (m_settings.areaMode == CaptureAreaMode::CustomArea && m_settings.customArea.isValid()) {
            reply = iface.call("ScreencastArea",
                               m_settings.customArea.x(),
                               m_settings.customArea.y(),
                               m_settings.customArea.width(),
                               m_settings.customArea.height(),
                               templateName,
                               options);
        } else {
            reply = iface.call("Screencast", templateName, options);
        }

        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty() && reply.arguments().at(0).toBool()) {
            m_gnomeRecordingActive = true;
            m_videoRawPath = reply.arguments().at(1).toString();
            qInfo() << "ScreenRecorder: GNOME Screencast started ->" << m_videoRawPath;
        } else {
            qWarning() << "ScreenRecorder: GNOME Screencast D-Bus call failed:" << reply.errorMessage();
            m_gnomeRecordingActive = false;
        }
    }

    if (!m_gnomeRecordingActive) {
        // Fallback: FFmpeg x11grab
        m_activeBackend = ScreenRecordBackendType::FFmpegX11;
        m_videoRawPath = QDir::tempPath() + QString("/hyggshicut_rec_video_%1.mp4").arg(QDateTime::currentMSecsSinceEpoch());

        m_videoProcess = std::make_unique<QProcess>();
        QString display = qgetenv("DISPLAY");
        if (display.isEmpty()) display = ":0.0";

        QStringList vArgs;
        vArgs << "-y"
              << "-f" << "x11grab"
              << "-framerate" << QString::number(m_settings.fps > 0 ? m_settings.fps : 30);

        if (m_settings.areaMode == CaptureAreaMode::CustomArea && m_settings.customArea.isValid()) {
            vArgs << "-video_size" << QString("%1x%2").arg(m_settings.customArea.width()).arg(m_settings.customArea.height());
            vArgs << "-i" << QString("%1+%2,%3").arg(display).arg(m_settings.customArea.x()).arg(m_settings.customArea.y());
        } else {
            vArgs << "-i" << display;
        }

        vArgs << "-c:v" << "libx264"
              << "-preset" << "ultrafast"
              << "-crf" << "22"
              << "-pix_fmt" << "yuv420p"
              << m_videoRawPath;

        m_videoProcess->start("ffmpeg", vArgs);
        if (!m_videoProcess->waitForStarted(2000)) {
            setState(ScreenRecorderState::Error);
            emit recordingError(tr("Không thể khởi chạy bộ ghi màn hình ffmpeg x11grab."));
            return;
        }
        qInfo() << "ScreenRecorder: FFmpeg x11grab started ->" << m_videoRawPath;
    }

    // 2. Start Synchronized Audio Capture if requested
    if (m_settings.audioMode != AudioRecordMode::None) {
        m_audioTempPath = QDir::tempPath() + QString("/hyggshicut_rec_audio_%1.wav").arg(QDateTime::currentMSecsSinceEpoch());
        m_audioProcess = std::make_unique<QProcess>();

        QStringList aArgs;
        aArgs << "-y"
              << "-f" << "pulse";

        if (m_settings.audioMode == AudioRecordMode::SystemAudio) {
            aArgs << "-i" << "@DEFAULT_MONITOR@";
        } else {
            aArgs << "-i" << "default";
        }

        aArgs << "-ac" << "2"
              << "-ar" << "48000"
              << "-c:a" << "pcm_s16le"
              << m_audioTempPath;

        m_audioProcess->start("ffmpeg", aArgs);
        if (!m_audioProcess->waitForStarted(2000)) {
            qWarning() << "ScreenRecorder: Failed to start pulse audio recording:" << m_audioProcess->errorString();
            m_audioTempPath.clear();
        } else {
            qInfo() << "ScreenRecorder: Pulse audio capture started ->" << m_audioTempPath;
        }
    }

    m_elapsedTimer->start();
    emit recordingStarted(m_videoRawPath);
}

void ScreenRecorder::onElapsedTimerTimeout() {
    m_elapsedSeconds++;
    emit recordingTick(m_elapsedSeconds);
}

void ScreenRecorder::stop() {
    if (m_state != ScreenRecorderState::Recording) {
        if (m_state == ScreenRecorderState::Countdown) {
            cancel();
        }
        return;
    }

    setState(ScreenRecorderState::Stopping);
    m_elapsedTimer->stop();

    // 1. Stop video recording
    if (m_gnomeRecordingActive) {
        QDBusInterface iface("org.gnome.Shell.Screencast",
                             "/org/gnome/Shell/Screencast",
                             "org.gnome.Shell.Screencast",
                             QDBusConnection::sessionBus());
        iface.call("StopScreencast");
        m_gnomeRecordingActive = false;
        // Allow GNOME Shell / GStreamer pipeline a short moment to flush and write mp4 moov atom
        QThread::msleep(800);
    }

    if (m_videoProcess && m_videoProcess->state() == QProcess::Running) {
        m_videoProcess->write("q\n");
        if (!m_videoProcess->waitForFinished(3000)) {
            m_videoProcess->terminate();
            m_videoProcess->waitForFinished(1500);
        }
    }

    // 2. Stop audio recording
    if (m_audioProcess && m_audioProcess->state() == QProcess::Running) {
        m_audioProcess->write("q\n");
        if (!m_audioProcess->waitForFinished(2000)) {
            m_audioProcess->terminate();
            m_audioProcess->waitForFinished(1000);
        }
    }

    finalizeRecording();
}

void ScreenRecorder::finalizeRecording() {
    // Wait briefly if file is still being flushed
    for (int retry = 0; retry < 15; ++retry) {
        if (QFileInfo::exists(m_videoRawPath) && QFileInfo(m_videoRawPath).size() > 0) {
            break;
        }
        QThread::msleep(200);
    }

    if (!QFileInfo::exists(m_videoRawPath) || QFileInfo(m_videoRawPath).size() == 0) {
        setState(ScreenRecorderState::Error);
        emit recordingError(tr("Không tìm thấy file video quay màn hình hoặc file trống."));
        cleanupTempFiles();
        return;
    }

    const bool hasAudio = !m_audioTempPath.isEmpty() && QFileInfo::exists(m_audioTempPath) && QFileInfo(m_audioTempPath).size() > 1024;

    if (hasAudio) {
        // Multiplex video + audio losslessly into m_finalOutputPath
        QProcess muxProc;
        QStringList muxArgs;
        muxArgs << "-y"
                << "-i" << m_videoRawPath
                << "-i" << m_audioTempPath
                << "-c:v" << "copy"
                << "-c:a" << "aac"
                << "-b:a" << "192k"
                << "-shortest"
                << m_finalOutputPath;

        muxProc.start("ffmpeg", muxArgs);
        if (muxProc.waitForFinished(10000) && muxProc.exitCode() == 0 && QFileInfo::exists(m_finalOutputPath)) {
            // Success muxing
            if (m_videoRawPath != m_finalOutputPath) {
                QFile::remove(m_videoRawPath);
            }
        } else {
            qWarning() << "ScreenRecorder: Audio multiplexing failed, using raw video file instead.";
            // Fall back to the raw video so the user still gets a recording.
            // Clean up the now-unused temp video afterwards (copy, don't move,
            // so a copy failure doesn't destroy the only usable file).
            if (!QFile::copy(m_videoRawPath, m_finalOutputPath)) {
                qWarning() << "ScreenRecorder: Failed to copy raw video to final output path:" << m_finalOutputPath;
            }
            QFile::remove(m_videoRawPath);
        }
    } else {
        // No audio: move or copy raw video to final destination
        if (m_videoRawPath != m_finalOutputPath) {
            QFile::remove(m_finalOutputPath);
            if (!QFile::rename(m_videoRawPath, m_finalOutputPath)) {
                QFile::copy(m_videoRawPath, m_finalOutputPath);
                QFile::remove(m_videoRawPath);
            }
        }
    }

    cleanupTempFiles();

    QFileInfo fi(m_finalOutputPath);
    const qint64 fileSize = fi.exists() ? fi.size() : 0;
    const int duration = m_elapsedSeconds;

    setState(ScreenRecorderState::Idle);
    emit recordingFinished(m_finalOutputPath, duration, fileSize);
}

void ScreenRecorder::cancel() {
    m_countdownTimer->stop();
    m_elapsedTimer->stop();

    if (m_gnomeRecordingActive) {
        QDBusInterface iface("org.gnome.Shell.Screencast",
                             "/org/gnome/Shell/Screencast",
                             "org.gnome.Shell.Screencast",
                             QDBusConnection::sessionBus());
        iface.call("StopScreencast");
        m_gnomeRecordingActive = false;
    }

    if (m_videoProcess && m_videoProcess->state() == QProcess::Running) {
        m_videoProcess->kill();
        m_videoProcess->waitForFinished(500);
    }

    if (m_audioProcess && m_audioProcess->state() == QProcess::Running) {
        m_audioProcess->kill();
        m_audioProcess->waitForFinished(500);
    }

    cleanupTempFiles();
    setState(ScreenRecorderState::Idle);
}

void ScreenRecorder::cleanupTempFiles() {
    if (!m_audioTempPath.isEmpty() && QFile::exists(m_audioTempPath)) {
        QFile::remove(m_audioTempPath);
        m_audioTempPath.clear();
    }
}

} // namespace hc
