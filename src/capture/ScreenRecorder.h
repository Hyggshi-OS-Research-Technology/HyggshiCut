#pragma once
#include <QObject>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QProcess>
#include <memory>

namespace hc {

enum class CaptureAreaMode {
    FullScreen = 0,
    CustomArea = 1
};

enum class AudioRecordMode {
    None = 0,
    Microphone = 1,
    SystemAudio = 2
};

enum class ScreenRecorderState {
    Idle,
    Countdown,
    Recording,
    Stopping,
    Error
};

enum class ScreenRecordBackendType {
    Auto,
    GnomeScreencast,
    FFmpegX11
};

struct ScreenRecordSettings {
    CaptureAreaMode areaMode = CaptureAreaMode::FullScreen;
    QRect customArea = QRect(0, 0, 1920, 1080);
    AudioRecordMode audioMode = AudioRecordMode::None;
    int fps = 30;
    int countdownSeconds = 3;
    QString outputFolder;
    bool autoImportToMediaPool = true;
    bool insertIntoTimeline = true;
};

class ScreenRecorder : public QObject {
    Q_OBJECT
public:
    explicit ScreenRecorder(QObject* parent = nullptr);
    ~ScreenRecorder() override;

    ScreenRecorderState state() const { return m_state; }
    ScreenRecordBackendType activeBackend() const { return m_activeBackend; }
    static bool isGnomeScreencastAvailable();

    bool start(const ScreenRecordSettings& settings);
    void stop();
    void cancel();

signals:
    void stateChanged(ScreenRecorderState newState);
    void countdownTick(int secondsRemaining);
    void recordingStarted(const QString& rawVideoPath);
    void recordingTick(int elapsedSeconds);
    void recordingFinished(const QString& finalFilePath, int durationSec, qint64 fileSizeBytes);
    void recordingError(const QString& errorMessage);

private slots:
    void onCountdownTimeout();
    void onElapsedTimerTimeout();

private:
    void startActualRecording();
    void finalizeRecording();
    void cleanupTempFiles();
    void setState(ScreenRecorderState newState);

    ScreenRecorderState m_state = ScreenRecorderState::Idle;
    ScreenRecordSettings m_settings;
    ScreenRecordBackendType m_activeBackend = ScreenRecordBackendType::Auto;

    // Timers
    QTimer* m_countdownTimer = nullptr;
    QTimer* m_elapsedTimer = nullptr;
    int m_countdownRemaining = 0;
    int m_elapsedSeconds = 0;

    // Paths
    QString m_videoRawPath;
    QString m_audioTempPath;
    QString m_finalOutputPath;

    // Subprocesses
    std::unique_ptr<QProcess> m_audioProcess;
    std::unique_ptr<QProcess> m_videoProcess;

    // GNOME Shell Screencast
    bool m_gnomeRecordingActive = false;
};

} // namespace hc
