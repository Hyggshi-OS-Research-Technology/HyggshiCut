#include "PreviewWidget.h"
#include "TransformOverlay.h"
#include "AudioMeterWidget.h"
#include "../render/GLVideoWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QFont>
#include <QResizeEvent>

namespace hc {

PreviewWidget::PreviewWidget(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* glRow = new QHBoxLayout();
    glRow->setContentsMargins(0, 0, 0, 0);
    glRow->setSpacing(4);

    m_gl = new GLVideoWidget(this);
    m_gl->installEventFilter(this);
    m_audioMeter = new AudioMeterWidget(this);
    m_audioMeter->setFixedWidth(32);
    m_audioMeter->setVisible(false); // Hidden by default

    glRow->addWidget(m_gl, 1);
    glRow->addWidget(m_audioMeter);
    layout->addLayout(glRow, 1);

    // Transform bounding-box overlay (on top of GL surface).
    m_transformOverlay = new TransformOverlay(m_gl);
    connect(m_transformOverlay, &TransformOverlay::transformChanged,
            this, &PreviewWidget::previewTransformChanged);
    connect(m_transformOverlay, &TransformOverlay::transformCommitted,
            this, &PreviewWidget::previewTransformCommitted);

    relayoutOverlays();

    auto* transport = new QHBoxLayout();
    m_playBtn = new QPushButton(tr("▶"), this);
    m_playBtn->setFixedWidth(36);
    connect(m_playBtn, &QPushButton::clicked, this, &PreviewWidget::playPauseClicked);
    transport->addWidget(m_playBtn);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, 0);
    connect(m_slider, &QSlider::sliderPressed,  this, [this]() { m_sliderBeingDragged = true; });
    connect(m_slider, &QSlider::sliderReleased, this, [this]() {
        m_sliderBeingDragged = false;
        emit seekRequested(static_cast<Ticks>(m_slider->value()) * 1000);
    });
    connect(m_slider, &QSlider::sliderMoved, this, [this](int ms) {
        emit seekRequested(static_cast<Ticks>(ms) * 1000);
    });
    transport->addWidget(m_slider, 1);

    m_timeLabel = new QLabel("00:00:00.000 / 00:00:00.000", this);
    m_timeLabel->setStyleSheet("color: #ccc; font-family: monospace;");
    transport->addWidget(m_timeLabel);

    m_meterToggleBtn = new QPushButton(tr("VU"), this);
    m_meterToggleBtn->setCheckable(true);
    m_meterToggleBtn->setChecked(false);
    m_meterToggleBtn->setToolTip(tr("Bật/tắt thanh đo âm lượng (Digital Audio Volume Meter)"));
    m_meterToggleBtn->setFixedWidth(56);
    connect(m_meterToggleBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (m_audioMeter) m_audioMeter->setVisible(checked);
    });
    transport->addWidget(m_meterToggleBtn);

    layout->addLayout(transport);
}

QSize PreviewWidget::sizeHint() const {
    return QSize(720, 420);
}

void PreviewWidget::setAudioMeterVisible(bool visible) {
    if (m_meterToggleBtn) m_meterToggleBtn->setChecked(visible);
    if (m_audioMeter) m_audioMeter->setVisible(visible);
}

bool PreviewWidget::isAudioMeterVisible() const {
    return m_audioMeter && m_audioMeter->isVisible();
}

void PreviewWidget::setSelectedTransform(const Transform& t, int srcW, int srcH) {
    relayoutOverlays();
    if (m_transformOverlay)
        m_transformOverlay->setSelectedClip(t, srcW, srcH);
}

void PreviewWidget::updateOverlayTransform(const Transform& t) {
    relayoutOverlays();
    if (m_transformOverlay)
        m_transformOverlay->setTransformExternal(t);
}

void PreviewWidget::clearTransformOverlay() {
    if (m_transformOverlay)
        m_transformOverlay->clearSelection();
}

void PreviewWidget::setPlaying(bool playing) {
    m_playBtn->setText(playing ? tr("⏸") : tr("▶"));
}

void PreviewWidget::setAudioLevels(float left, float right) {
    if (m_audioMeter) m_audioMeter->setLevels(left, right);
}

void PreviewWidget::setPosition(Ticks t) {
    if (!m_sliderBeingDragged) {
        m_slider->blockSignals(true);
        m_slider->setValue(static_cast<int>(t / 1000));
        m_slider->blockSignals(false);
    }
    m_timeLabel->setText(QString("%1 / %2").arg(formatTimecode(t), formatTimecode(m_duration)));
}

void PreviewWidget::setDuration(Ticks d) {
    m_duration = d;
    m_slider->setRange(0, static_cast<int>(d / 1000));
}

void PreviewWidget::setTextOverlay(const QString&) {
    // Text layers are now rendered directly via the GL video pipeline
}

void PreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayoutOverlays();
}

bool PreviewWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_gl && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        relayoutOverlays();
    }
    return QWidget::eventFilter(obj, event);
}

void PreviewWidget::relayoutOverlays() {
    if (!m_gl || !m_transformOverlay) return;
    m_transformOverlay->setGeometry(0, 0, m_gl->width(), m_gl->height());
    m_transformOverlay->raise();
}

} // namespace hc
