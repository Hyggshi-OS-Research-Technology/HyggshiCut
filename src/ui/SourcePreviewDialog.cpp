#include "SourcePreviewDialog.h"
#include "../playback/MpvPlayer.h"
#include "../render/MpvVideoWidget.h"
#include "../core/TimeTypes.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QMessageBox>

namespace hc {

SourcePreviewDialog::SourcePreviewDialog(const QString& filePath, const QString& displayName, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Xem trước nguồn — %1").arg(displayName));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(900, 560);

    try {
        m_player = std::make_unique<MpvPlayer>(this);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Lỗi mpv"),
            tr("Không khởi tạo được libmpv: %1").arg(QString::fromUtf8(e.what())));
        return;
    }

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_gl = new MpvVideoWidget(m_player.get(), this);
    layout->addWidget(m_gl, 1);

    auto* transport = new QHBoxLayout();
    m_playBtn = new QPushButton(tr("Phát"), this);
    m_playBtn->setFixedWidth(80);
    connect(m_playBtn, &QPushButton::clicked, this, [this]() { m_player->togglePause(); });
    transport->addWidget(m_playBtn);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 0);
    connect(m_slider, &QSlider::sliderPressed, this, [this]() { m_sliderBeingDragged = true; });
    connect(m_slider, &QSlider::sliderReleased, this, [this]() {
        m_sliderBeingDragged = false;
        m_player->seek(m_slider->value() / 1000.0);
    });
    transport->addWidget(m_slider, 1);

    m_timeLabel = new QLabel("00:00:00.000 / 00:00:00.000", this);
    m_timeLabel->setStyleSheet("color: #ccc; font-family: monospace;");
    transport->addWidget(m_timeLabel);

    layout->addLayout(transport);

    connect(m_player.get(), &MpvPlayer::positionChanged, this, &SourcePreviewDialog::onPositionChanged);
    connect(m_player.get(), &MpvPlayer::durationChanged, this, &SourcePreviewDialog::onDurationChanged);
    connect(m_player.get(), &MpvPlayer::pausedChanged, this, &SourcePreviewDialog::onPausedChanged);
    connect(m_player.get(), &MpvPlayer::endOfFile, this, [this]() { m_player->seek(0); m_player->pause(); });

    m_player->loadFile(filePath);
    m_player->play();
}

SourcePreviewDialog::~SourcePreviewDialog() {
    if (m_player) m_player->pause();
}

void SourcePreviewDialog::onPositionChanged(double seconds) {
    if (!m_sliderBeingDragged) {
        m_slider->blockSignals(true);
        m_slider->setValue(static_cast<int>(seconds * 1000.0));
        m_slider->blockSignals(false);
    }
    const Ticks curTicks = secondsToTicks(seconds);
    const Ticks durTicks = secondsToTicks(m_duration);
    m_timeLabel->setText(QString("%1 / %2").arg(formatTimecode(curTicks), formatTimecode(durTicks)));
}

void SourcePreviewDialog::onDurationChanged(double seconds) {
    m_duration = seconds;
    m_slider->setRange(0, static_cast<int>(seconds * 1000.0));
}

void SourcePreviewDialog::onPausedChanged(bool paused) {
    m_playBtn->setText(paused ? tr("Phát") : tr("Tạm dừng"));
}

} // namespace hc
