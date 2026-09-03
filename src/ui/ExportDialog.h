#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QRadioButton>
#include <QSlider>
#include <QCloseEvent>
#include <QElapsedTimer>
#include "../core/Project.h"
#include "../export/Exporter.h"

namespace hc {

class ExportDialog : public QDialog {
    Q_OBJECT
public:
    explicit ExportDialog(Project* project, QWidget* parent = nullptr);

private slots:
    void onPresetChanged(int index);
    void onFormatChanged(int index);
    void onVideoCodecChanged(int index);
    void onRateControlToggled();
    void onBrowse();
    void onStartClicked();
    void onProgress(double fraction, QString etaText);
    void onFinished(bool success, QString message);
    void updateEstimatedSize();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void applyPreset(int index);
    void setControlsEnabled(bool enabled);

    Project* m_project;
    Exporter* m_exporter = nullptr;

    // Presets & destination
    QComboBox* m_presetCombo = nullptr;
    QLineEdit* m_pathEdit = nullptr;
    QPushButton* m_browseBtn = nullptr;

    // Resolution & Framerate
    QComboBox* m_resolutionPresetCombo = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QDoubleSpinBox* m_fpsSpin = nullptr;

    // Video settings
    QComboBox* m_formatCombo = nullptr;
    QComboBox* m_vCodecCombo = nullptr;
    QRadioButton* m_bitrateRadio = nullptr;
    QRadioButton* m_crfRadio = nullptr;
    QSpinBox* m_vBitrateSpin = nullptr;
    QSlider* m_crfSlider = nullptr;
    QLabel* m_crfLabel = nullptr;
    QComboBox* m_speedPresetCombo = nullptr;
    QComboBox* m_pixFmtCombo = nullptr;

    // Audio settings
    QComboBox* m_aCodecCombo = nullptr;
    QComboBox* m_aBitrateCombo = nullptr;
    QComboBox* m_aSampleRateCombo = nullptr;
    QComboBox* m_aChannelsCombo = nullptr;

    // Output info & progress
    QLabel* m_estimateLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_startBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;

    // Export timing for ETA display
    QElapsedTimer m_exportTimer;
};

} // namespace hc
