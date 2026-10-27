#include "ExportDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QMessageBox>

namespace hc {

namespace {

struct ExportPresetItem {
    QString name;
    QString format;
    int width;
    int height;
    double fps;
    QString vcodec;
    QString rateMode;
    int vBitrate;
    int crf;
    QString speedPreset;
    QString acodec;
    int aBitrate;
    int aSampleRate;
    int aChannels;
};

std::vector<ExportPresetItem> exportPresets() {
    return {
        { "YouTube 1080p FHD (H.264, 12 Mbps, AAC 192k)", "mp4", 1920, 1080, 30.0, "libx264", "bitrate", 12000, 21, "medium", "aac", 192, 48000, 2 },
        { "YouTube 4K UHD (H.264, 40 Mbps, AAC 320k)",   "mp4", 3840, 2160, 60.0, "libx264", "bitrate", 40000, 21, "medium", "aac", 320, 48000, 2 },
        { LTR("legacy.913fbca979ce2b82"), "mp4", 1080, 1920, 30.0, "libx264", "bitrate", 10000, 21, "medium", "aac", 192, 48000, 2 },
        { LTR("legacy.9c52bd22921858cc"),         "mp4", 1080, 1080, 30.0, "libx264", "bitrate", 8000,  21, "medium", "aac", 192, 48000, 2 },
        { LTR("legacy.478bd97c8a92860c"), "mp4", 1920, 1080, 30.0, "libx264", "crf",     8000,  18, "slow",   "aac", 256, 48000, 2 },
        { LTR("legacy.97a9c327e0a4ce00"),     "mp4", 1920, 1080, 30.0, "libx265", "crf",     4000,  24, "medium", "aac", 128, 48000, 2 },
        { LTR("legacy.9e63ac65324e63d2"),      "mov", 1920, 1080, 30.0, "prores_ks", "bitrate", 0,     0,  "",       "pcm_s16le", 0, 48000, 2 },
        { LTR("legacy.38c32c5d6b75d890"), "webm", 1920, 1080, 30.0, "libvpx-vp9", "crf", 0, 28, "medium", "libopus", 128, 48000, 2 },
        { LTR("legacy.1e09f3243360e2bf"),"mp3", 0,    0,    0.0,  "none",    "bitrate", 0,     0,  "",       "libmp3lame", 320, 48000, 2 },
        { LTR("legacy.1ffb03ae1949db82"),  "wav", 0,    0,    0.0,  "none",    "bitrate", 0,     0,  "",       "pcm_s16le", 0, 48000, 2 },
        // Low-spec PC preset: ultrafast encode + low bitrate for stability on weak machines
        { LTR("legacy.619e783a7fe8cb33"), "mp4", 1280, 720, 30.0, "libx264", "bitrate", 4000, 28, "ultrafast", "aac", 128, 48000, 2 },
        { LTR("legacy.59d13a48f16c67d0"),                           "mp4", 1920, 1080, 30.0, "libx264", "bitrate", 8000,  21, "medium", "aac", 192, 48000, 2 }
    };
}

} // namespace

ExportDialog::ExportDialog(Project* project, QWidget* parent)
    : QDialog(parent), m_project(project) {
    setWindowTitle(LTR("legacy.6bf39d399fbdd349"));
    resize(580, 680);
    setStyleSheet(
        "QDialog { background-color: #1e1e22; color: #eee; }"
        "QLabel { color: #ddd; }"
        "QGroupBox { border: 1px solid #3d3d45; border-radius: 0px; margin-top: 1.2em; font-weight: bold; color: #ff9944; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background-color: #2b2b32; color: #fff; border: 1px solid #444; border-radius: 0px; padding: 4px 8px; }"
        "QTabWidget::pane { border: 1px solid #3d3d45; background-color: #24242a; border-radius: 0px; }"
        "QTabBar::tab { background-color: #1a1a1e; color: #aaa; padding: 8px 16px; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
        "QTabBar::tab:selected { background-color: #24242a; color: #ff9944; font-weight: bold; }"
        "QPushButton { background-color: #3b3b44; color: white; border-radius: 0px; padding: 6px 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #4a4a56; }"
        "QProgressBar { border: 1px solid #444; border-radius: 0px; text-align: center; background-color: #141418; color: white; }"
        "QProgressBar::chunk { background-color: #ff6a00; border-radius: 0px; }"
    );

    setupUi();

    // Default output path in Movies/Videos or Home directory
    QString outDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (outDir.isEmpty()) outDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString defaultName = m_project && !m_project->name.isEmpty() ? m_project->name : tr("Untitled Project");
    m_pathEdit->setText(QString("%1/%2.mp4").arg(outDir, defaultName));

    // Inherit resolution & fps from timeline
    if (m_project) {
        if (m_project->timeline().videoWidth > 0) m_widthSpin->setValue(m_project->timeline().videoWidth);
        if (m_project->timeline().videoHeight > 0) m_heightSpin->setValue(m_project->timeline().videoHeight);
        if (m_project->timeline().frameRate > 0) m_fpsSpin->setValue(m_project->timeline().frameRate);
    }

    updateEstimatedSize();
}

void ExportDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // Preset & Destination
    auto* topGroup = new QGroupBox(LTR("legacy.04a00ce9a5d7228b"), this);
    auto* topLayout = new QFormLayout(topGroup);
    topLayout->setContentsMargins(12, 16, 12, 12);
    topLayout->setSpacing(8);

    m_presetCombo = new QComboBox(this);
    for (const auto& p : exportPresets()) {
        m_presetCombo->addItem(p.name);
    }
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onPresetChanged);
    topLayout->addRow(LTR("legacy.e7b3bcf01bab8503"), m_presetCombo);

    auto* pathRow = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_browseBtn = new QPushButton(LTR("legacy.8eef8863fe637c6a"), this);
    connect(m_browseBtn, &QPushButton::clicked, this, &ExportDialog::onBrowse);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(m_browseBtn);
    topLayout->addRow(LTR("legacy.c46b7177dbb9d273"), pathRow);

    mainLayout->addWidget(topGroup);

    // Tabs for Video & Audio settings
    auto* tabs = new QTabWidget(this);

    // Tab 1: Video Settings
    auto* videoTab = new QWidget(tabs);
    auto* vLayout = new QFormLayout(videoTab);
    vLayout->setContentsMargins(12, 16, 12, 12);
    vLayout->setSpacing(8);

    m_formatCombo = new QComboBox(videoTab);
    m_formatCombo->addItem("MP4 (.mp4)", "mp4");
    m_formatCombo->addItem("QuickTime MOV (.mov)", "mov");
    m_formatCombo->addItem("Matroska MKV (.mkv)", "mkv");
    m_formatCombo->addItem("WebM (.webm)", "webm");
    m_formatCombo->addItem("Audio MP3 (.mp3)", "mp3");
    m_formatCombo->addItem("Audio WAV (.wav)", "wav");
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onFormatChanged);
    vLayout->addRow(LTR("legacy.f71d7e82c70e5203"), m_formatCombo);

    m_vCodecCombo = new QComboBox(videoTab);
    m_vCodecCombo->addItem(LTR("legacy.cc10035475246fb7"), "libx264");
    m_vCodecCombo->addItem(LTR("legacy.6efda910237c148e"), "libx265");
    m_vCodecCombo->addItem(LTR("legacy.baaf442d34dcbf80"), "libvpx-vp9");
    m_vCodecCombo->addItem(LTR("legacy.c85b57b295b2ca6a"), "libsvtav1");
    m_vCodecCombo->addItem(LTR("legacy.3ece12bf0b78edf6"), "prores_ks");
    m_vCodecCombo->addItem(LTR("legacy.1f7d5bc128994a07"), "none");
    connect(m_vCodecCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onVideoCodecChanged);
    vLayout->addRow(LTR("legacy.133fbd97b7f2bfa2"), m_vCodecCombo);

    // Resolution & FPS row
    auto* resRow = new QHBoxLayout();
    m_widthSpin = new QSpinBox(videoTab);
    m_widthSpin->setRange(128, 7680);
    m_widthSpin->setSingleStep(16);
    m_widthSpin->setValue(1920);
    m_heightSpin = new QSpinBox(videoTab);
    m_heightSpin->setRange(128, 4320);
    m_heightSpin->setSingleStep(16);
    m_heightSpin->setValue(1080);
    m_fpsSpin = new QDoubleSpinBox(videoTab);
    m_fpsSpin->setRange(1.0, 120.0);
    m_fpsSpin->setSingleStep(1.0);
    m_fpsSpin->setDecimals(2);
    m_fpsSpin->setValue(30.0);

    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ExportDialog::updateEstimatedSize);
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ExportDialog::updateEstimatedSize);
    connect(m_fpsSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ExportDialog::updateEstimatedSize);

    resRow->addWidget(m_widthSpin);
    resRow->addWidget(new QLabel("×", videoTab));
    resRow->addWidget(m_heightSpin);
    resRow->addSpacing(10);
    resRow->addWidget(new QLabel(tr("FPS:"), videoTab));
    resRow->addWidget(m_fpsSpin);
    vLayout->addRow(LTR("legacy.3923cb94f033fed7"), resRow);

    // Quality Rate Control Mode
    auto* rateBox = new QHBoxLayout();
    m_bitrateRadio = new QRadioButton(tr("Target Bitrate (kbps)"), videoTab);
    m_crfRadio = new QRadioButton(tr("Constant Quality (CRF)"), videoTab);
    m_bitrateRadio->setChecked(true);
    auto* rateGroup = new QButtonGroup(videoTab);
    rateGroup->addButton(m_bitrateRadio);
    rateGroup->addButton(m_crfRadio);
    connect(m_bitrateRadio, &QRadioButton::toggled, this, &ExportDialog::onRateControlToggled);
    rateBox->addWidget(m_bitrateRadio);
    rateBox->addWidget(m_crfRadio);
    vLayout->addRow(LTR("legacy.86a55c9b41fb7bd3"), rateBox);

    // Video Bitrate
    m_vBitrateSpin = new QSpinBox(videoTab);
    m_vBitrateSpin->setRange(500, 200000);
    m_vBitrateSpin->setSingleStep(500);
    m_vBitrateSpin->setValue(12000);
    m_vBitrateSpin->setSuffix(" kbps");
    connect(m_vBitrateSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ExportDialog::updateEstimatedSize);
    vLayout->addRow(tr("Bitrate Video:"), m_vBitrateSpin);

    // CRF Slider
    auto* crfRow = new QHBoxLayout();
    m_crfSlider = new QSlider(Qt::Horizontal, videoTab);
    m_crfSlider->setRange(0, 51);
    m_crfSlider->setValue(21);
    m_crfLabel = new QLabel(LTR("legacy.5436e302bca22a3f"), videoTab);
    m_crfLabel->setFixedWidth(90);
    connect(m_crfSlider, &QSlider::valueChanged, this, [this](int val) {
        QString desc = (val <= 18) ? LTR("legacy.3c86e727f10020eb").arg(val)
                     : (val <= 23) ? LTR("legacy.80c74d1da16bb3f1").arg(val)
                     : LTR("legacy.20be1e99f8ae2b44").arg(val);
        m_crfLabel->setText(desc);
    });
    crfRow->addWidget(m_crfSlider, 1);
    crfRow->addWidget(m_crfLabel);
    vLayout->addRow(LTR("legacy.38ad5dadeb9a425e"), crfRow);
    m_crfSlider->setEnabled(false);

    // Speed / Preset
    m_speedPresetCombo = new QComboBox(videoTab);
    m_speedPresetCombo->addItem(LTR("legacy.15394b24b863f715"), "ultrafast");
    m_speedPresetCombo->addItem("Veryfast (Nhanh)", "veryfast");
    m_speedPresetCombo->addItem("Fast", "fast");
    m_speedPresetCombo->addItem(LTR("legacy.82db88b2840db33d"), "medium");
    m_speedPresetCombo->addItem(LTR("legacy.339b027117a70c69"), "slow");
    m_speedPresetCombo->addItem(LTR("legacy.ac29b54c9d0a8030"), "slower");
    m_speedPresetCombo->setCurrentIndex(3);
    vLayout->addRow(LTR("legacy.558a06b06d496289"), m_speedPresetCombo);

    // Pixel Format
    m_pixFmtCombo = new QComboBox(videoTab);
    m_pixFmtCombo->addItem(LTR("legacy.7da340d46dd1d901"), "yuv420p");
    m_pixFmtCombo->addItem(LTR("legacy.f05d95b296e80e1c"), "yuv420p10le");
    m_pixFmtCombo->addItem(LTR("legacy.8980a7f960e6749b"), "yuv422p");
    m_pixFmtCombo->addItem(LTR("legacy.f4ca9957d2cad6ba"), "yuv444p");
    vLayout->addRow(LTR("legacy.1f20165aa81ee324"), m_pixFmtCombo);

    tabs->addTab(videoTab, tr("Video"));

    // Tab 2: Audio Settings
    auto* audioTab = new QWidget(tabs);
    auto* aLayout = new QFormLayout(audioTab);
    aLayout->setContentsMargins(12, 16, 12, 12);
    aLayout->setSpacing(8);

    m_aCodecCombo = new QComboBox(audioTab);
    m_aCodecCombo->addItem(LTR("legacy.79e46e890fcafa3d"), "aac");
    m_aCodecCombo->addItem("MP3 (libmp3lame)", "libmp3lame");
    m_aCodecCombo->addItem(LTR("legacy.c9e070f9389033b4"), "libopus");
    m_aCodecCombo->addItem("PCM 16-bit (Lossless WAV)", "pcm_s16le");
    m_aCodecCombo->addItem("FLAC (Lossless)", "flac");
    m_aCodecCombo->addItem(LTR("legacy.946966460132bfbf"), "none");
    aLayout->addRow(LTR("legacy.42519d04b0ba2bb2"), m_aCodecCombo);

    m_aBitrateCombo = new QComboBox(audioTab);
    m_aBitrateCombo->addItem(LTR("legacy.df0bfcfd7b2cf9c2"), 320);
    m_aBitrateCombo->addItem(LTR("legacy.ebf95e337d6200d0"), 256);
    m_aBitrateCombo->addItem(LTR("legacy.1eaf3f6f3c34d587"), 192);
    m_aBitrateCombo->addItem(LTR("legacy.31de6e77f6e66e06"), 128);
    m_aBitrateCombo->addItem(LTR("legacy.b6101eeb39384ae4"), 96);
    m_aBitrateCombo->setCurrentIndex(2);
    connect(m_aBitrateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::updateEstimatedSize);
    aLayout->addRow(tr("Bitrate Audio:"), m_aBitrateCombo);

    m_aSampleRateCombo = new QComboBox(audioTab);
    m_aSampleRateCombo->addItem(LTR("legacy.589a57f150eb8919"), 48000);
    m_aSampleRateCombo->addItem(LTR("legacy.898a1e9921f51b46"), 44100);
    m_aSampleRateCombo->addItem("96000 Hz (High Resolution Audio)", 96000);
    aLayout->addRow(LTR("legacy.4fae295002dde3ef"), m_aSampleRateCombo);

    m_aChannelsCombo = new QComboBox(audioTab);
    m_aChannelsCombo->addItem(LTR("legacy.21d7d85f1d6b81b4"), 2);
    m_aChannelsCombo->addItem(LTR("legacy.18201823ae74e2d2"), 6);
    m_aChannelsCombo->addItem(LTR("legacy.9c72e0a603a67954"), 1);
    aLayout->addRow(LTR("legacy.b81efbfb8ae56580"), m_aChannelsCombo);

    tabs->addTab(audioTab, tr("Audio"));
    mainLayout->addWidget(tabs);

    // File size estimate & Status
    auto* infoGroup = new QGroupBox(LTR("legacy.5c64ba8568f9cc14"), this);
    auto* infoLayout = new QVBoxLayout(infoGroup);
    infoLayout->setContentsMargins(12, 14, 12, 12);
    infoLayout->setSpacing(6);

    m_estimateLabel = new QLabel(this);
    m_estimateLabel->setStyleSheet("color: #ff9944; font-weight: bold;");
    infoLayout->addWidget(m_estimateLabel);

    m_statusLabel = new QLabel(LTR("legacy.54a2ef4a9ef6cf45"), this);
    m_statusLabel->setStyleSheet("color: #aaa; font-style: italic;");
    infoLayout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    infoLayout->addWidget(m_progressBar);

    mainLayout->addWidget(infoGroup);

    // Bottom action buttons
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch(1);

    m_closeBtn = new QPushButton(LTR("legacy.5d54c2a1a341732e"), this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_startBtn = new QPushButton(LTR("legacy.2573e4c98a127133"), this);
    m_startBtn->setStyleSheet("background-color: #ff6a00; color: white; font-weight: bold; font-size: 13px; border-radius: 0px; padding: 8px 24px;");
    connect(m_startBtn, &QPushButton::clicked, this, &ExportDialog::onStartClicked);

    btnLayout->addWidget(m_closeBtn);
    btnLayout->addWidget(m_startBtn);
    mainLayout->addLayout(btnLayout);
}

void ExportDialog::onPresetChanged(int index) {
    applyPreset(index);
    updateEstimatedSize();
}

void ExportDialog::applyPreset(int index) {
    const auto& presets = exportPresets();
    if (index < 0 || index >= static_cast<int>(presets.size())) return;
    const auto& p = presets[index];
    if (p.name == LTR("legacy.59d13a48f16c67d0")) return;

    // Adjust path extension
    QString currentPath = m_pathEdit->text();
    if (!currentPath.isEmpty()) {
        QFileInfo fi(currentPath);
        m_pathEdit->setText(QString("%1/%2.%3").arg(fi.path(), fi.completeBaseName(), p.format));
    }

    if (p.width > 0) m_widthSpin->setValue(p.width);
    if (p.height > 0) m_heightSpin->setValue(p.height);
    if (p.fps > 0) m_fpsSpin->setValue(p.fps);

    // Format
    int fmtIdx = m_formatCombo->findData(p.format);
    if (fmtIdx >= 0) m_formatCombo->setCurrentIndex(fmtIdx);

    // Video Codec
    int vcIdx = m_vCodecCombo->findData(p.vcodec);
    if (vcIdx >= 0) m_vCodecCombo->setCurrentIndex(vcIdx);

    // Rate Control
    if (p.rateMode == "crf") {
        m_crfRadio->setChecked(true);
        m_crfSlider->setValue(p.crf);
    } else {
        m_bitrateRadio->setChecked(true);
        if (p.vBitrate > 0) m_vBitrateSpin->setValue(p.vBitrate);
    }

    // Audio Codec
    int acIdx = m_aCodecCombo->findData(p.acodec);
    if (acIdx >= 0) m_aCodecCombo->setCurrentIndex(acIdx);

    // Audio Bitrate
    int abIdx = m_aBitrateCombo->findData(p.aBitrate);
    if (abIdx >= 0) m_aBitrateCombo->setCurrentIndex(abIdx);
}

void ExportDialog::onFormatChanged(int index) {
    QString ext = m_formatCombo->itemData(index).toString();
    QString currentPath = m_pathEdit->text();
    if (!currentPath.isEmpty()) {
        QFileInfo fi(currentPath);
        m_pathEdit->setText(QString("%1/%2.%3").arg(fi.path(), fi.completeBaseName(), ext));
    }
}

void ExportDialog::onVideoCodecChanged(int index) {
    QString vcodec = m_vCodecCombo->itemData(index).toString();
    bool isNone = (vcodec == "none");
    m_widthSpin->setEnabled(!isNone);
    m_heightSpin->setEnabled(!isNone);
    m_fpsSpin->setEnabled(!isNone);
    m_vBitrateSpin->setEnabled(!isNone && m_bitrateRadio->isChecked());
    m_crfSlider->setEnabled(!isNone && m_crfRadio->isChecked());
    m_speedPresetCombo->setEnabled(!isNone && vcodec != "prores_ks");
    m_pixFmtCombo->setEnabled(!isNone);
    updateEstimatedSize();
}

void ExportDialog::onRateControlToggled() {
    bool isBitrate = m_bitrateRadio->isChecked();
    m_vBitrateSpin->setEnabled(isBitrate);
    m_crfSlider->setEnabled(!isBitrate);
}

void ExportDialog::updateEstimatedSize() {
    if (!m_project) return;
    double durationSec = ticksToSeconds(m_project->timeline().totalDuration());
    if (durationSec <= 0.0) durationSec = 60.0;

    int vBitrate = m_vBitrateSpin->value();
    if (m_vCodecCombo->currentData().toString() == "none") vBitrate = 0;
    else if (m_crfRadio->isChecked()) {
        // Approximate bitrate for CRF display
        int crf = m_crfSlider->value();
        vBitrate = static_cast<int>(15000.0 * std::pow(0.88, std::clamp(crf - 18, -10, 20)));
    }

    int aBitrate = m_aBitrateCombo->currentData().toInt();
    if (m_aCodecCombo->currentData().toString() == "none") aBitrate = 0;

    double totalKbps = vBitrate + aBitrate;
    double sizeMB = (totalKbps * durationSec) / (8.0 * 1024.0);

    m_estimateLabel->setText(QString(LTR("legacy.3e3f6f59440b20bf"))
                             .arg(durationSec, 0, 'f', 1)
                             .arg(static_cast<int>(totalKbps))
                             .arg(sizeMB, 0, 'f', 1));
}

void ExportDialog::onBrowse() {
    QString ext = m_formatCombo->currentData().toString();
    QString filter;
    if (ext == "mp4") filter = LTR("legacy.0a0c4f9186e62f97");
    else if (ext == "mov") filter = LTR("legacy.e819c472fb8046ca");
    else if (ext == "mkv") filter = LTR("legacy.dce0db7679364eb8");
    else if (ext == "webm") filter = LTR("legacy.57f4b066c8c2735e");
    else if (ext == "mp3") filter = LTR("legacy.2a7c6ae9ba24d472");
    else if (ext == "wav") filter = LTR("legacy.20d997da7fb8902c");
    else filter = LTR("legacy.15c72874bf4dafcb");

    const QString path = QFileDialog::getSaveFileName(
        this, LTR("legacy.48b92c4574ecd892"), m_pathEdit->text(), filter);
    if (!path.isEmpty()) {
        m_pathEdit->setText(path);
    }
}

void ExportDialog::setControlsEnabled(bool enabled) {
    m_presetCombo->setEnabled(enabled);
    m_pathEdit->setEnabled(enabled);
    m_browseBtn->setEnabled(enabled);
    m_formatCombo->setEnabled(enabled);
    m_vCodecCombo->setEnabled(enabled);
    m_widthSpin->setEnabled(enabled);
    m_heightSpin->setEnabled(enabled);
    m_fpsSpin->setEnabled(enabled);
    m_bitrateRadio->setEnabled(enabled);
    m_crfRadio->setEnabled(enabled);
    m_speedPresetCombo->setEnabled(enabled);
    m_pixFmtCombo->setEnabled(enabled);
    m_aCodecCombo->setEnabled(enabled);
    m_aBitrateCombo->setEnabled(enabled);
    m_aSampleRateCombo->setEnabled(enabled);
    m_aChannelsCombo->setEnabled(enabled);
    m_closeBtn->setEnabled(enabled);
    // Update bitrate/CRF enable state based on current radio selection
    if (enabled) {
        m_vBitrateSpin->setEnabled(m_bitrateRadio->isChecked());
        m_crfSlider->setEnabled(m_crfRadio->isChecked());
    } else {
        m_vBitrateSpin->setEnabled(false);
        m_crfSlider->setEnabled(false);
    }
}

void ExportDialog::onStartClicked() {
    if (m_exporter && m_exporter->isRunning()) {
        m_exporter->cancel();
        m_startBtn->setText(LTR("legacy.2573e4c98a127133"));
        m_startBtn->setStyleSheet("background-color: #ff6a00; color: white; font-weight: bold; border-radius: 0px; padding: 8px 24px;");
        m_statusLabel->setText(LTR("legacy.2baa53f1805f921f"));
        setControlsEnabled(true);
        return;
    }

    const QString outPath = m_pathEdit->text().trimmed();
    if (outPath.isEmpty()) {
        m_statusLabel->setText(LTR("legacy.f68d098873e374d5"));
        return;
    }

    const QString outDir = QFileInfo(outPath).absolutePath();
    if (!outDir.isEmpty() && !QDir().mkpath(outDir)) {
        m_statusLabel->setText(LTR("legacy.8bf88fce9b4babd5").arg(outDir));
        return;
    }

    Exporter::Settings s;
    s.outputPath = outPath;
    s.width = m_widthSpin->value();
    s.height = m_heightSpin->value();
    s.frameRate = m_fpsSpin->value();
    s.videoCodec = m_vCodecCombo->currentData().toString();
    s.rateControlMode = m_bitrateRadio->isChecked() ? "bitrate" : "crf";
    s.videoBitrateKbps = m_vBitrateSpin->value();
    s.crf = m_crfSlider->value();
    s.preset = m_speedPresetCombo->currentData().toString();
    s.pixelFormat = m_pixFmtCombo->currentData().toString();

    s.audioCodec = m_aCodecCombo->currentData().toString();
    s.audioBitrateKbps = m_aBitrateCombo->currentData().toInt();
    s.audioSampleRate = m_aSampleRateCombo->currentData().toInt();
    s.audioChannels = m_aChannelsCombo->currentData().toInt();

    // Cancel any previous export cleanly before creating a new one.
    // Calling delete on a running Exporter would destroy the QProcess
    // while ffmpeg is still alive (causing the "Destroyed while running" warning).
    if (m_exporter) {
        if (m_exporter->isRunning()) m_exporter->cancel();
        delete m_exporter;
        m_exporter = nullptr;
    }
    m_exporter = new Exporter(m_project, this);
    connect(m_exporter, &Exporter::progress, this, &ExportDialog::onProgress);
    connect(m_exporter, &Exporter::finished, this, &ExportDialog::onFinished);

    m_progressBar->setValue(0);
    m_statusLabel->setText(LTR("legacy.85d453c72dce4474"));
    m_startBtn->setText(LTR("legacy.9b98f9fcc9ad08f4"));
    m_startBtn->setStyleSheet("background-color: #8b2525; color: white; font-weight: bold; border-radius: 0px; padding: 8px 24px;");
    setControlsEnabled(false);

    m_exportTimer.restart();
    m_exporter->start(s);
}

void ExportDialog::onProgress(double fraction, QString etaText) {
    const int pct = static_cast<int>(fraction * 100.0);
    m_progressBar->setValue(pct);

    // Compute elapsed and ETA.
    const qint64 elapsedMs = m_exportTimer.elapsed();
    const int elapsedSec = static_cast<int>(elapsedMs / 1000);
    const QString elapsedStr = QString("%1:%2")
        .arg(elapsedSec / 60, 2, 10, QChar('0'))
        .arg(elapsedSec % 60, 2, 10, QChar('0'));

    // etaText carries "45 fps | 1.50x" from Exporter::onReadyReadStandardOutput.
    const QString speedPart = etaText.isEmpty() ? QString() : QStringLiteral(" | %1").arg(etaText);

    if (fraction > 0.02) {
        const qint64 totalEstMs = static_cast<qint64>(elapsedMs / fraction);
        const int etaSec = static_cast<int>((totalEstMs - elapsedMs) / 1000);
        const QString etaStr = QString("%1:%2")
            .arg(etaSec / 60, 2, 10, QChar('0'))
            .arg(etaSec % 60, 2, 10, QChar('0'));
        m_statusLabel->setText(LTR("legacy.90df978cc54a84c9")
                               .arg(pct).arg(elapsedStr).arg(etaStr).arg(speedPart));
    } else {
        m_statusLabel->setText(LTR("legacy.a95be2034411c84d")
                               .arg(pct).arg(elapsedStr).arg(speedPart));
    }
}

void ExportDialog::onFinished(bool success, QString message) {
    m_startBtn->setText(LTR("legacy.2573e4c98a127133"));
    m_startBtn->setStyleSheet("background-color: #ff6a00; color: white; font-weight: bold; border-radius: 0px; padding: 8px 24px;");
    m_progressBar->setValue(success ? 100 : 0);
    setControlsEnabled(true);

    if (success) {
        const qint64 elapsedMs = m_exportTimer.elapsed();
        const int totalSec = static_cast<int>(elapsedMs / 1000);
        const QString timeStr = QString("%1:%2")
            .arg(totalSec / 60, 2, 10, QChar('0'))
            .arg(totalSec % 60, 2, 10, QChar('0'));
        m_statusLabel->setText(LTR("legacy.96601e3657279b48").arg(message).arg(timeStr));
    } else {
        m_statusLabel->setText(message);
    }
}

void ExportDialog::closeEvent(QCloseEvent* event) {
    if (m_exporter && m_exporter->isRunning()) {
        const auto btn = QMessageBox::question(
            this,
            LTR("legacy.5cc94699ffbaec06"),
            LTR("legacy.617835e946da6bd4"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (btn != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_exporter->cancel();
    }
    QDialog::closeEvent(event);
}

} // namespace hc
