#include "ScreenRecordDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QScreen>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include "../i18n/LanguageManager.h"

namespace hc {

ScreenRecordDialog::ScreenRecordDialog(QWidget* parent)
    : QDialog(parent)
    , m_recorder(std::make_unique<ScreenRecorder>(this)) {
    setWindowTitle(LTR("screenRecord.title"));
    setMinimumWidth(500);

    setStyleSheet(
        "QDialog { background-color: #1e1e22; color: #eee; }"
        "QLabel { color: #ddd; }"
        "QGroupBox { border: 1px solid #3d3d45; border-radius: 6px; margin-top: 1.2em; font-weight: bold; color: #ff9944; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QLineEdit, QSpinBox, QComboBox { background-color: #2b2b32; color: #fff; border: 1px solid #444; border-radius: 4px; padding: 5px 8px; }"
        "QCheckBox { color: #eee; spacing: 8px; }"
        "QPushButton { background-color: #3b3b44; color: white; border-radius: 4px; padding: 6px 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #4a4a56; }"
    );

    setupUi();

    connect(m_recorder.get(), &ScreenRecorder::countdownTick, this, &ScreenRecordDialog::onCountdownTick);
    connect(m_recorder.get(), &ScreenRecorder::recordingTick, this, &ScreenRecordDialog::onRecordingTick);
    connect(m_recorder.get(), &ScreenRecorder::recordingFinished, this, &ScreenRecordDialog::onRecordingFinished);
    connect(m_recorder.get(), &ScreenRecorder::recordingError, this, &ScreenRecordDialog::onRecordingError);
}

ScreenRecordDialog::~ScreenRecordDialog() = default;

void ScreenRecordDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    m_stackedWidget = new QStackedWidget(this);
    setupConfigPage();
    setupRecordingPage();
    setupSummaryPage();

    m_stackedWidget->addWidget(m_configPage);     // Index 0
    m_stackedWidget->addWidget(m_recordingPage);  // Index 1
    m_stackedWidget->addWidget(m_summaryPage);    // Index 2

    mainLayout->addWidget(m_stackedWidget);
    m_stackedWidget->setCurrentIndex(0);
}

void ScreenRecordDialog::setupConfigPage() {
    m_configPage = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(m_configPage);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(14);

    // Group 1: Source & Settings
    auto* settingsGroup = new QGroupBox(LTR("menu.settings"), m_configPage);
    auto* formLayout = new QFormLayout(settingsGroup);
    formLayout->setSpacing(10);
    formLayout->setContentsMargins(12, 16, 12, 12);

    // Area Mode
    m_areaCombo = new QComboBox(settingsGroup);
    m_areaCombo->addItem(LTR("screenRecord.fullScreen"));
    m_areaCombo->addItem(LTR("screenRecord.customArea"));
    formLayout->addRow(LTR("screenRecord.mode"), m_areaCombo);

    // Custom Area Container
    m_customAreaContainer = new QWidget(settingsGroup);
    auto* customAreaLayout = new QHBoxLayout(m_customAreaContainer);
    customAreaLayout->setContentsMargins(0, 0, 0, 0);
    customAreaLayout->setSpacing(6);

    QRect screenGeom(0, 0, 1920, 1080);
    if (auto* screen = QGuiApplication::primaryScreen()) {
        screenGeom = screen->geometry();
    }

    m_customXSpin = new QSpinBox(m_customAreaContainer);
    m_customXSpin->setRange(0, 7680);
    m_customXSpin->setValue(0);
    m_customXSpin->setPrefix("X: ");

    m_customYSpin = new QSpinBox(m_customAreaContainer);
    m_customYSpin->setRange(0, 4320);
    m_customYSpin->setValue(0);
    m_customYSpin->setPrefix("Y: ");

    m_customWSpin = new QSpinBox(m_customAreaContainer);
    m_customWSpin->setRange(128, 7680);
    m_customWSpin->setValue(screenGeom.width());
    m_customWSpin->setPrefix("W: ");

    m_customHSpin = new QSpinBox(m_customAreaContainer);
    m_customHSpin->setRange(128, 4320);
    m_customHSpin->setValue(screenGeom.height());
    m_customHSpin->setPrefix("H: ");

    customAreaLayout->addWidget(m_customXSpin);
    customAreaLayout->addWidget(m_customYSpin);
    customAreaLayout->addWidget(m_customWSpin);
    customAreaLayout->addWidget(m_customHSpin);
    m_customAreaContainer->setVisible(false);
    formLayout->addRow("", m_customAreaContainer);

    connect(m_areaCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ScreenRecordDialog::onAreaModeChanged);

    // Audio Mode
    m_audioCombo = new QComboBox(settingsGroup);
    m_audioCombo->addItem(LTR("screenRecord.audioNone"), static_cast<int>(AudioRecordMode::None));
    m_audioCombo->addItem(LTR("screenRecord.audioMic"), static_cast<int>(AudioRecordMode::Microphone));
    m_audioCombo->addItem(LTR("screenRecord.audioSystem"), static_cast<int>(AudioRecordMode::SystemAudio));
    m_audioCombo->setCurrentIndex(1); // Default to Microphone
    formLayout->addRow(LTR("screenRecord.audio"), m_audioCombo);

    // FPS
    m_fpsCombo = new QComboBox(settingsGroup);
    m_fpsCombo->addItem("30 FPS", 30);
    m_fpsCombo->addItem("60 FPS", 60);
    formLayout->addRow(LTR("screenRecord.fps"), m_fpsCombo);

    // Countdown
    m_countdownCombo = new QComboBox(settingsGroup);
    m_countdownCombo->addItem(LTR("screenRecord.countdownNone"), 0);
    m_countdownCombo->addItem(LTR("screenRecord.countdownSec").arg(3), 3);
    m_countdownCombo->addItem(LTR("screenRecord.countdownSec").arg(5), 5);
    m_countdownCombo->setCurrentIndex(1); // Default to 3s
    formLayout->addRow(LTR("screenRecord.countdown"), m_countdownCombo);

    // Save Folder
    auto* folderContainer = new QWidget(settingsGroup);
    auto* folderLayout = new QHBoxLayout(folderContainer);
    folderLayout->setContentsMargins(0, 0, 0, 0);
    folderLayout->setSpacing(6);

    QString defaultVideos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (defaultVideos.isEmpty()) defaultVideos = QDir::homePath() + "/Videos";

    m_outputFolderEdit = new QLineEdit(defaultVideos, folderContainer);
    m_browseBtn = new QPushButton(LTR("btn.browse"), folderContainer);
    folderLayout->addWidget(m_outputFolderEdit, 1);
    folderLayout->addWidget(m_browseBtn);
    formLayout->addRow(LTR("screenRecord.saveFolder"), folderContainer);

    connect(m_browseBtn, &QPushButton::clicked, this, &ScreenRecordDialog::onBrowseFolderClicked);

    pageLayout->addWidget(settingsGroup);

    // Group 2: Integration Checkboxes
    auto* actionGroup = new QGroupBox(LTR("dock.timeline"), m_configPage);
    auto* actionLayout = new QVBoxLayout(actionGroup);
    actionLayout->setContentsMargins(12, 16, 12, 12);
    actionLayout->setSpacing(8);

    m_autoImportCheck = new QCheckBox(LTR("screenRecord.autoImport"), actionGroup);
    m_autoImportCheck->setChecked(true);
    actionLayout->addWidget(m_autoImportCheck);

    m_insertTimelineCheck = new QCheckBox(LTR("screenRecord.insertTimeline"), actionGroup);
    m_insertTimelineCheck->setChecked(true);
    actionLayout->addWidget(m_insertTimelineCheck);

    pageLayout->addWidget(actionGroup);

    // Buttons
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_cancelBtn = new QPushButton(LTR("btn.cancel"), m_configPage);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_cancelBtn);

    m_startBtn = new QPushButton(QString("🔴 %1").arg(LTR("screenRecord.start")), m_configPage);
    m_startBtn->setStyleSheet(
        "QPushButton { background-color: #d32f2f; color: white; border-radius: 4px; padding: 8px 24px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background-color: #f44336; }"
    );
    connect(m_startBtn, &QPushButton::clicked, this, &ScreenRecordDialog::onStartClicked);
    btnLayout->addWidget(m_startBtn);

    pageLayout->addLayout(btnLayout);
}

void ScreenRecordDialog::setupRecordingPage() {
    m_recordingPage = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(m_recordingPage);
    pageLayout->setContentsMargins(20, 24, 20, 24);
    pageLayout->setSpacing(16);
    pageLayout->setAlignment(Qt::AlignCenter);

    m_recordingStatusLabel = new QLabel(LTR("screenRecord.recording"), m_recordingPage);
    m_recordingStatusLabel->setAlignment(Qt::AlignCenter);
    m_recordingStatusLabel->setStyleSheet("font-size: 15px; font-weight: bold; color: #ff9944;");
    pageLayout->addWidget(m_recordingStatusLabel);

    m_timerLabel = new QLabel("00:00:00", m_recordingPage);
    m_timerLabel->setAlignment(Qt::AlignCenter);
    m_timerLabel->setStyleSheet("font-size: 32px; font-weight: bold; font-family: monospace; color: #ffffff;");
    pageLayout->addWidget(m_timerLabel);

    m_stopBtn = new QPushButton(QString("⏹ %1").arg(LTR("screenRecord.stop")), m_recordingPage);
    m_stopBtn->setMinimumWidth(180);
    m_stopBtn->setStyleSheet(
        "QPushButton { background-color: #e53935; color: white; border-radius: 6px; padding: 10px 24px; font-weight: bold; font-size: 14px; }"
        "QPushButton:hover { background-color: #ef5350; }"
    );
    connect(m_stopBtn, &QPushButton::clicked, this, &ScreenRecordDialog::onStopClicked);
    pageLayout->addWidget(m_stopBtn, 0, Qt::AlignCenter);
}

void ScreenRecordDialog::setupSummaryPage() {
    m_summaryPage = new QWidget(this);
    auto* pageLayout = new QVBoxLayout(m_summaryPage);
    pageLayout->setContentsMargins(20, 20, 20, 20);
    pageLayout->setSpacing(14);

    m_summaryTitleLabel = new QLabel(QString("✅ %1").arg(LTR("screenRecord.successTitle")), m_summaryPage);
    m_summaryTitleLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #4caf50;");
    pageLayout->addWidget(m_summaryTitleLabel);

    auto* infoBox = new QGroupBox(LTR("screenRecord.saved"), m_summaryPage);
    auto* infoLayout = new QVBoxLayout(infoBox);
    infoLayout->setContentsMargins(12, 14, 12, 12);
    infoLayout->setSpacing(8);

    m_filePathLabel = new QLabel("", infoBox);
    m_filePathLabel->setWordWrap(true);
    m_filePathLabel->setStyleSheet("color: #90caf9; font-weight: bold;");
    infoLayout->addWidget(m_filePathLabel);

    m_fileInfoLabel = new QLabel("", infoBox);
    m_fileInfoLabel->setStyleSheet("color: #aaa;");
    infoLayout->addWidget(m_fileInfoLabel);

    pageLayout->addWidget(infoBox);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_openFolderBtn = new QPushButton(LTR("screenRecord.openFolder"), m_summaryPage);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &ScreenRecordDialog::onOpenFolderClicked);
    btnLayout->addWidget(m_openFolderBtn);

    m_closeBtn = new QPushButton(LTR("btn.close"), m_summaryPage);
    m_closeBtn->setStyleSheet(
        "QPushButton { background-color: #2e7d32; color: white; border-radius: 4px; padding: 6px 20px; font-weight: bold; }"
        "QPushButton:hover { background-color: #388e3c; }"
    );
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(m_closeBtn);

    pageLayout->addLayout(btnLayout);
}

void ScreenRecordDialog::onAreaModeChanged(int index) {
    m_customAreaContainer->setVisible(index == 1);
}

void ScreenRecordDialog::onBrowseFolderClicked() {
    QString folder = QFileDialog::getExistingDirectory(this, LTR("screenRecord.saveFolder"), m_outputFolderEdit->text());
    if (!folder.isEmpty()) {
        m_outputFolderEdit->setText(folder);
    }
}

void ScreenRecordDialog::onStartClicked() {
    ScreenRecordSettings settings;
    settings.areaMode = m_areaCombo->currentIndex() == 1 ? CaptureAreaMode::CustomArea : CaptureAreaMode::FullScreen;
    if (settings.areaMode == CaptureAreaMode::CustomArea) {
        settings.customArea = QRect(m_customXSpin->value(), m_customYSpin->value(),
                                    m_customWSpin->value(), m_customHSpin->value());
    }
    settings.audioMode = static_cast<AudioRecordMode>(m_audioCombo->currentData().toInt());
    settings.fps = m_fpsCombo->currentData().toInt();
    settings.countdownSeconds = m_countdownCombo->currentData().toInt();
    settings.outputFolder = m_outputFolderEdit->text().trimmed();
    settings.autoImportToMediaPool = m_autoImportCheck->isChecked();
    settings.insertIntoTimeline = m_insertTimelineCheck->isChecked();

    if (!m_recorder->start(settings)) {
        QMessageBox::warning(this, LTR("screenRecord.title"), LTR("screenRecord.error").arg("Cannot start recorder"));
        return;
    }

    m_stackedWidget->setCurrentIndex(1);
    if (settings.countdownSeconds > 0) {
        m_recordingStatusLabel->setText(QString("⏱ %1...").arg(settings.countdownSeconds));
        m_timerLabel->setText(QString::number(settings.countdownSeconds));
    } else {
        m_recordingStatusLabel->setText(LTR("screenRecord.recording"));
        m_timerLabel->setText("00:00:00");
    }
}

void ScreenRecordDialog::onCountdownTick(int remaining) {
    if (remaining > 0) {
        m_recordingStatusLabel->setText(QString("⏱ %1...").arg(remaining));
        m_timerLabel->setText(QString::number(remaining));
    } else {
        m_recordingStatusLabel->setText(LTR("screenRecord.recording"));
        m_timerLabel->setText("00:00:00");
    }
}

void ScreenRecordDialog::onRecordingTick(int elapsedSeconds) {
    m_recordingStatusLabel->setText(LTR("screenRecord.recording"));
    m_timerLabel->setText(formatTime(elapsedSeconds));
}

void ScreenRecordDialog::onStopClicked() {
    m_recordingStatusLabel->setText("Finalizing recording...");
    m_stopBtn->setEnabled(false);
    m_recorder->stop();
}

void ScreenRecordDialog::onRecordingFinished(const QString& filePath, int durationSec, qint64 fileSize) {
    m_lastRecordedPath = filePath;
    m_stopBtn->setEnabled(true);

    m_filePathLabel->setText(filePath);
    m_fileInfoLabel->setText(LTR("screenRecord.duration")
                                 .arg(formatTime(durationSec))
                                 .arg(formatFileSize(fileSize)));

    m_stackedWidget->setCurrentIndex(2);

    emit recordingCompleted(filePath, m_autoImportCheck->isChecked(), m_insertTimelineCheck->isChecked());
}

void ScreenRecordDialog::onRecordingError(const QString& errorMsg) {
    m_stopBtn->setEnabled(true);
    m_stackedWidget->setCurrentIndex(0);
    QMessageBox::critical(this, LTR("screenRecord.title"), LTR("screenRecord.error").arg(errorMsg));
}

void ScreenRecordDialog::onOpenFolderClicked() {
    if (!m_lastRecordedPath.isEmpty()) {
        QFileInfo fi(m_lastRecordedPath);
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    }
}

QString ScreenRecordDialog::formatTime(int totalSeconds) {
    int h = totalSeconds / 3600;
    int m = (totalSeconds % 3600) / 60;
    int s = totalSeconds % 60;
    if (h > 0) {
        return QString("%1:%2:%3")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'))
            .arg(s, 2, 10, QChar('0'));
    }
    return QString("%1:%2")
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

QString ScreenRecordDialog::formatFileSize(qint64 bytes) {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

} // namespace hc
