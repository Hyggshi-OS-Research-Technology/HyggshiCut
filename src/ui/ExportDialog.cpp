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

const std::vector<ExportPresetItem>& exportPresets() {
    static const std::vector<ExportPresetItem> presets = {
        { "YouTube 1080p FHD (H.264, 12 Mbps, AAC 192k)", "mp4", 1920, 1080, 30.0, "libx264", "bitrate", 12000, 21, "medium", "aac", 192, 48000, 2 },
        { "YouTube 4K UHD (H.264, 40 Mbps, AAC 320k)",   "mp4", 3840, 2160, 60.0, "libx264", "bitrate", 40000, 21, "medium", "aac", 320, 48000, 2 },
        { "TikTok / Reels / Shorts 9:16 (1080x1920 Dọc)", "mp4", 1080, 1920, 30.0, "libx264", "bitrate", 10000, 21, "medium", "aac", 192, 48000, 2 },
        { "Instagram Post 1:1 (1080x1080 Vuông)",         "mp4", 1080, 1080, 30.0, "libx264", "bitrate", 8000,  21, "medium", "aac", 192, 48000, 2 },
        { "H.264 Chất lượng cao (CRF 18, Lossless-like)", "mp4", 1920, 1080, 30.0, "libx264", "crf",     8000,  18, "slow",   "aac", 256, 48000, 2 },
        { "H.265 / HEVC Siêu nhẹ (CRF 24, AAC 128k)",     "mp4", 1920, 1080, 30.0, "libx265", "crf",     4000,  24, "medium", "aac", 128, 48000, 2 },
        { "Apple ProRes 422 HQ (Chuyên nghiệp MOV)",      "mov", 1920, 1080, 30.0, "prores_ks", "bitrate", 0,     0,  "",       "pcm_s16le", 0, 48000, 2 },
        { "WebM / VP9 Video cho Web (VP9 + Opus)",        "webm",1920, 1080, 30.0, "libvpx-vp9", "crf",   0,     28, "medium", "libopus", 128, 48000, 2 },
        { "Chỉ xuất Âm thanh MP3 (320 kbps High Quality)","mp3", 0,    0,    0.0,  "none",    "bitrate", 0,     0,  "",       "libmp3lame", 320, 48000, 2 },
        { "Chỉ xuất Âm thanh WAV (Lossless PCM 16-bit)",  "wav", 0,    0,    0.0,  "none",    "bitrate", 0,     0,  "",       "pcm_s16le", 0, 48000, 2 },
        // Low-spec PC preset: ultrafast encode + low bitrate for stability on weak machines
        { "⚡ Máy yếu / Nhanh (H.264 Ultrafast, 4 Mbps, 720p)", "mp4", 1280, 720, 30.0, "libx264", "bitrate", 4000, 28, "ultrafast", "aac", 128, 48000, 2 },
        { "Tùy chỉnh (Custom)",                           "mp4", 1920, 1080, 30.0, "libx264", "bitrate", 8000,  21, "medium", "aac", 192, 48000, 2 }
    };
    return presets;
}

} // namespace

ExportDialog::ExportDialog(Project* project, QWidget* parent)
    : QDialog(parent), m_project(project) {
    setWindowTitle(tr("Xuất video & âm thanh (Export Media)"));
    resize(580, 680);
    setStyleSheet(
        "QDialog { background-color: #1e1e22; color: #eee; }"
        "QLabel { color: #ddd; }"
        "QGroupBox { border: 1px solid #3d3d45; border-radius: 6px; margin-top: 1.2em; font-weight: bold; color: #ff9944; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background-color: #2b2b32; color: #fff; border: 1px solid #444; border-radius: 4px; padding: 4px 8px; }"
        "QTabWidget::pane { border: 1px solid #3d3d45; background-color: #24242a; border-radius: 6px; }"
        "QTabBar::tab { background-color: #1a1a1e; color: #aaa; padding: 8px 16px; margin-right: 2px; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
        "QTabBar::tab:selected { background-color: #24242a; color: #ff9944; font-weight: bold; }"
        "QPushButton { background-color: #3b3b44; color: white; border-radius: 4px; padding: 6px 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #4a4a56; }"
        "QProgressBar { border: 1px solid #444; border-radius: 4px; text-align: center; background-color: #141418; color: white; }"
        "QProgressBar::chunk { background-color: #ff6a00; border-radius: 3px; }"
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
    auto* topGroup = new QGroupBox(tr("Mẫu xuất & Nơi lưu"), this);
    auto* topLayout = new QFormLayout(topGroup);
    topLayout->setContentsMargins(12, 16, 12, 12);
    topLayout->setSpacing(8);

    m_presetCombo = new QComboBox(this);
    for (const auto& p : exportPresets()) {
        m_presetCombo->addItem(p.name);
    }
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onPresetChanged);
    topLayout->addRow(tr("Mẫu định sẵn (Preset):"), m_presetCombo);

    auto* pathRow = new QHBoxLayout();
    m_pathEdit = new QLineEdit(this);
    m_browseBtn = new QPushButton(tr("Chọn nơi lưu..."), this);
    connect(m_browseBtn, &QPushButton::clicked, this, &ExportDialog::onBrowse);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(m_browseBtn);
    topLayout->addRow(tr("Đường dẫn lưu:"), pathRow);

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
    vLayout->addRow(tr("Định dạng file (Format):"), m_formatCombo);

    m_vCodecCombo = new QComboBox(videoTab);
    m_vCodecCombo->addItem("H.264 (libx264 - Tương thích nhất)", "libx264");
    m_vCodecCombo->addItem("H.265 / HEVC (libx265 - Dung lượng nhẹ)", "libx265");
    m_vCodecCombo->addItem("VP9 (libvpx-vp9 - Dành cho web)", "libvpx-vp9");
    m_vCodecCombo->addItem("AV1 (libsvtav1 - Thế hệ mới)", "libsvtav1");
    m_vCodecCombo->addItem("Apple ProRes (prores_ks - Dựng phim)", "prores_ks");
    m_vCodecCombo->addItem("Tắt Video (Chỉ xuất âm thanh)", "none");
    connect(m_vCodecCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::onVideoCodecChanged);
    vLayout->addRow(tr("Bộ mã hóa Video (Codec):"), m_vCodecCombo);

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
    vLayout->addRow(tr("Độ phân giải & FPS:"), resRow);

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
    vLayout->addRow(tr("Chế độ chất lượng:"), rateBox);

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
    m_crfLabel = new QLabel("21 (Mặc định)", videoTab);
    m_crfLabel->setFixedWidth(90);
    connect(m_crfSlider, &QSlider::valueChanged, this, [this](int val) {
        QString desc = (val <= 18) ? tr("%1 (Cực nét)").arg(val)
                     : (val <= 23) ? tr("%1 (Cân bằng)").arg(val)
                     : tr("%1 (Nhẹ)").arg(val);
        m_crfLabel->setText(desc);
    });
    crfRow->addWidget(m_crfSlider, 1);
    crfRow->addWidget(m_crfLabel);
    vLayout->addRow(tr("Mức CRF (0-51):"), crfRow);
    m_crfSlider->setEnabled(false);

    // Speed / Preset
    m_speedPresetCombo = new QComboBox(videoTab);
    m_speedPresetCombo->addItem("Ultrafast (Xuất cực nhanh)", "ultrafast");
    m_speedPresetCombo->addItem("Veryfast (Nhanh)", "veryfast");
    m_speedPresetCombo->addItem("Fast", "fast");
    m_speedPresetCombo->addItem("Medium (Cân bằng mặc định)", "medium");
    m_speedPresetCombo->addItem("Slow (Nén tốt hơn)", "slow");
    m_speedPresetCombo->addItem("Slower (Chất lượng tối ưu)", "slower");
    m_speedPresetCombo->setCurrentIndex(3);
    vLayout->addRow(tr("Tốc độ nén (Speed):"), m_speedPresetCombo);

    // Pixel Format
    m_pixFmtCombo = new QComboBox(videoTab);
    m_pixFmtCombo->addItem("YUV 4:2:0 8-bit (Tiêu chuẩn mọi thiết bị)", "yuv420p");
    m_pixFmtCombo->addItem("YUV 4:2:0 10-bit (Màu sắc mượt mà)", "yuv420p10le");
    m_pixFmtCombo->addItem("YUV 4:2:2 8-bit (Chất lượng cao)", "yuv422p");
    m_pixFmtCombo->addItem("YUV 4:4:4 8-bit (Không nén chroma)", "yuv444p");
    vLayout->addRow(tr("Định dạng màu (Pixel):"), m_pixFmtCombo);

    tabs->addTab(videoTab, tr("Video"));

    // Tab 2: Audio Settings
    auto* audioTab = new QWidget(tabs);
    auto* aLayout = new QFormLayout(audioTab);
    aLayout->setContentsMargins(12, 16, 12, 12);
    aLayout->setSpacing(8);

    m_aCodecCombo = new QComboBox(audioTab);
    m_aCodecCombo->addItem("AAC (Mặc định chuẩn)", "aac");
    m_aCodecCombo->addItem("MP3 (libmp3lame)", "libmp3lame");
    m_aCodecCombo->addItem("Opus (libopus - Nén hiện đại)", "libopus");
    m_aCodecCombo->addItem("PCM 16-bit (Lossless WAV)", "pcm_s16le");
    m_aCodecCombo->addItem("FLAC (Lossless)", "flac");
    m_aCodecCombo->addItem("Tắt âm thanh (Mute)", "none");
    aLayout->addRow(tr("Bộ mã hóa Audio:"), m_aCodecCombo);

    m_aBitrateCombo = new QComboBox(audioTab);
    m_aBitrateCombo->addItem("320 kbps (Chất lượng cao nhất)", 320);
    m_aBitrateCombo->addItem("256 kbps (Rất tốt)", 256);
    m_aBitrateCombo->addItem("192 kbps (Chuẩn phát sóng)", 192);
    m_aBitrateCombo->addItem("128 kbps (Cân bằng)", 128);
    m_aBitrateCombo->addItem("96 kbps (Tiết kiệm)", 96);
    m_aBitrateCombo->setCurrentIndex(2);
    connect(m_aBitrateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportDialog::updateEstimatedSize);
    aLayout->addRow(tr("Bitrate Audio:"), m_aBitrateCombo);

    m_aSampleRateCombo = new QComboBox(audioTab);
    m_aSampleRateCombo->addItem("48000 Hz (Chuẩn Video & Phim ảnh)", 48000);
    m_aSampleRateCombo->addItem("44100 Hz (Chuẩn CD Audio)", 44100);
    m_aSampleRateCombo->addItem("96000 Hz (High Resolution Audio)", 96000);
    aLayout->addRow(tr("Tần số lấy mẫu:"), m_aSampleRateCombo);

    m_aChannelsCombo = new QComboBox(audioTab);
    m_aChannelsCombo->addItem("Stereo (2 kênh)", 2);
    m_aChannelsCombo->addItem("5.1 Surround (6 kênh)", 6);
    m_aChannelsCombo->addItem("Mono (1 kênh)", 1);
    aLayout->addRow(tr("Kênh âm thanh:"), m_aChannelsCombo);

    tabs->addTab(audioTab, tr("Audio"));
    mainLayout->addWidget(tabs);

    // File size estimate & Status
    auto* infoGroup = new QGroupBox(tr("Dự tính xuất"), this);
    auto* infoLayout = new QVBoxLayout(infoGroup);
    infoLayout->setContentsMargins(12, 14, 12, 12);
    infoLayout->setSpacing(6);

    m_estimateLabel = new QLabel(this);
    m_estimateLabel->setStyleSheet("color: #ff9944; font-weight: bold;");
    infoLayout->addWidget(m_estimateLabel);

    m_statusLabel = new QLabel(tr("Sẵn sàng xuất."), this);
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

    m_closeBtn = new QPushButton(tr("Đóng"), this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_startBtn = new QPushButton(tr("Bắt đầu xuất video"), this);
    m_startBtn->setStyleSheet("background-color: #ff6a00; color: white; font-weight: bold; font-size: 13px; border-radius: 4px; padding: 8px 24px;");
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
    if (p.name == "Tùy chỉnh (Custom)") return;

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

    m_estimateLabel->setText(QString("Thời lượng: %1s &nbsp;|&nbsp; Tổng bitrate: ~%2 kbps &nbsp;|&nbsp; <b>Dung lượng ước tính:</b> ~%3 MB")
                             .arg(durationSec, 0, 'f', 1)
                             .arg(static_cast<int>(totalKbps))
                             .arg(sizeMB, 0, 'f', 1));
}

void ExportDialog::onBrowse() {
    QString ext = m_formatCombo->currentData().toString();
    QString filter;
    if (ext == "mp4") filter = tr("MP4 Video (*.mp4);;Tất cả (*.*)");
    else if (ext == "mov") filter = tr("QuickTime MOV (*.mov);;Tất cả (*.*)");
    else if (ext == "mkv") filter = tr("Matroska MKV (*.mkv);;Tất cả (*.*)");
    else if (ext == "webm") filter = tr("WebM Video (*.webm);;Tất cả (*.*)");
    else if (ext == "mp3") filter = tr("MP3 Audio (*.mp3);;Tất cả (*.*)");
    else if (ext == "wav") filter = tr("WAV Audio (*.wav);;Tất cả (*.*)");
    else filter = tr("Tất cả (*.*)");

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Chọn nơi lưu video"), m_pathEdit->text(), filter);
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
        m_startBtn->setText(tr("Bắt đầu xuất video"));
        m_startBtn->setStyleSheet("background-color: #ff6a00; color: white; font-weight: bold; border-radius: 4px; padding: 8px 24px;");
        m_statusLabel->setText(tr("Đã hủy xuất video."));
        setControlsEnabled(true);
        return;
    }

    const QString outPath = m_pathEdit->text().trimmed();
    if (outPath.isEmpty()) {
        m_statusLabel->setText(tr("Lỗi: Vui lòng chọn đường dẫn lưu!"));
        return;
    }

    const QString outDir = QFileInfo(outPath).absolutePath();
    if (!outDir.isEmpty() && !QDir().mkpath(outDir)) {
        m_statusLabel->setText(tr("Lỗi: Không thể tạo hoặc truy cập thư mục lưu (%1)! Vui lòng kiểm tra đường dẫn.").arg(outDir));
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
    m_statusLabel->setText(tr("Đang xuất video qua FFmpeg..."));
    m_startBtn->setText(tr("Hủy xuất"));
    m_startBtn->setStyleSheet("background-color: #8b2525; color: white; font-weight: bold; border-radius: 4px; padding: 8px 24px;");
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
        m_statusLabel->setText(tr("Đang kết xuất: %1% | Đã qua: %2 | Còn lại: ~%3%4")
                               .arg(pct).arg(elapsedStr).arg(etaStr).arg(speedPart));
    } else {
        m_statusLabel->setText(tr("Đang kết xuất: %1% | Đã qua: %2%3")
                               .arg(pct).arg(elapsedStr).arg(speedPart));
    }
}

void ExportDialog::onFinished(bool success, QString message) {
    m_startBtn->setText(tr("Bắt đầu xuất video"));
    m_startBtn->setStyleSheet("background-color: #ff6a00; color: white; font-weight: bold; border-radius: 4px; padding: 8px 24px;");
    m_progressBar->setValue(success ? 100 : 0);
    setControlsEnabled(true);

    if (success) {
        const qint64 elapsedMs = m_exportTimer.elapsed();
        const int totalSec = static_cast<int>(elapsedMs / 1000);
        const QString timeStr = QString("%1:%2")
            .arg(totalSec / 60, 2, 10, QChar('0'))
            .arg(totalSec % 60, 2, 10, QChar('0'));
        m_statusLabel->setText(tr("%1 (Thời gian xuất: %2)").arg(message).arg(timeStr));
    } else {
        m_statusLabel->setText(message);
    }
}

void ExportDialog::closeEvent(QCloseEvent* event) {
    if (m_exporter && m_exporter->isRunning()) {
        const auto btn = QMessageBox::question(
            this,
            tr("Đang xuất video"),
            tr("Quá trình xuất video đang chạy. Bạn có chắc muốn hủy và đóng?"),
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
