#pragma once
#include <QDialog>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QStackedWidget>
#include <memory>
#include "../capture/ScreenRecorder.h"

namespace hc {

class ScreenRecordDialog : public QDialog {
    Q_OBJECT
public:
    explicit ScreenRecordDialog(QWidget* parent = nullptr);
    ~ScreenRecordDialog() override;

signals:
    void recordingCompleted(const QString& filePath, bool autoImport, bool insertTimeline);

private slots:
    void onStartClicked();
    void onStopClicked();
    void onBrowseFolderClicked();
    void onAreaModeChanged(int index);
    void onCountdownTick(int remaining);
    void onRecordingTick(int elapsedSeconds);
    void onRecordingFinished(const QString& filePath, int durationSec, qint64 fileSize);
    void onRecordingError(const QString& errorMsg);
    void onOpenFolderClicked();

private:
    void setupUi();
    void setupConfigPage();
    void setupRecordingPage();
    void setupSummaryPage();
    QString formatTime(int totalSeconds);
    QString formatFileSize(qint64 bytes);

    std::unique_ptr<ScreenRecorder> m_recorder;

    QStackedWidget* m_stackedWidget = nullptr;

    // Config Page widgets
    QWidget* m_configPage = nullptr;
    QComboBox* m_areaCombo = nullptr;
    QWidget* m_customAreaContainer = nullptr;
    QSpinBox* m_customXSpin = nullptr;
    QSpinBox* m_customYSpin = nullptr;
    QSpinBox* m_customWSpin = nullptr;
    QSpinBox* m_customHSpin = nullptr;
    QComboBox* m_audioCombo = nullptr;
    QComboBox* m_fpsCombo = nullptr;
    QComboBox* m_countdownCombo = nullptr;
    QLineEdit* m_outputFolderEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;
    QCheckBox* m_autoImportCheck = nullptr;
    QCheckBox* m_insertTimelineCheck = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;

    // Recording Page widgets
    QWidget* m_recordingPage = nullptr;
    QLabel* m_recordingStatusLabel = nullptr;
    QLabel* m_timerLabel = nullptr;
    QPushButton* m_stopBtn = nullptr;

    // Summary Page widgets
    QWidget* m_summaryPage = nullptr;
    QLabel* m_summaryTitleLabel = nullptr;
    QLabel* m_filePathLabel = nullptr;
    QLabel* m_fileInfoLabel = nullptr;
    QPushButton* m_openFolderBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    // Last recorded result
    QString m_lastRecordedPath;
};

} // namespace hc
