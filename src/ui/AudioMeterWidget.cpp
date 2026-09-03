#include "AudioMeterWidget.h"
#include <QPainter>
#include <QLinearGradient>
#include <cmath>
#include <algorithm>

namespace hc {

AudioMeterWidget::AudioMeterWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(48, 80);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    m_decayTimer = new QTimer(this);
    m_decayTimer->setInterval(33); // ~30 fps
    connect(m_decayTimer, &QTimer::timeout, this, [this]() {
        bool needsRepaint = false;
        if (m_leftLevel > 0.001f) {
            m_leftLevel = std::max(0.0f, m_leftLevel - 0.04f);
            needsRepaint = true;
        }
        if (m_rightLevel > 0.001f) {
            m_rightLevel = std::max(0.0f, m_rightLevel - 0.04f);
            needsRepaint = true;
        }
        if (m_leftPeakHoldCounter > 0) {
            --m_leftPeakHoldCounter;
        } else if (m_leftPeakHold > m_leftLevel) {
            m_leftPeakHold = std::max(m_leftLevel, m_leftPeakHold - 0.02f);
            needsRepaint = true;
        }
        if (m_rightPeakHoldCounter > 0) {
            --m_rightPeakHoldCounter;
        } else if (m_rightPeakHold > m_rightLevel) {
            m_rightPeakHold = std::max(m_rightLevel, m_rightPeakHold - 0.02f);
            needsRepaint = true;
        }
        if (needsRepaint) update();
    });
    m_decayTimer->start();
}

void AudioMeterWidget::setLevels(float leftPeak, float rightPeak) {
    leftPeak = std::clamp(leftPeak, 0.0f, 1.5f);
    rightPeak = std::clamp(rightPeak, 0.0f, 1.5f);

    m_leftLevel = std::max(m_leftLevel, leftPeak);
    m_rightLevel = std::max(m_rightLevel, rightPeak);

    if (m_leftLevel > m_leftPeakHold) {
        m_leftPeakHold = m_leftLevel;
        m_leftPeakHoldCounter = 15;
    }
    if (m_rightLevel > m_rightPeakHold) {
        m_rightPeakHold = m_rightLevel;
        m_rightPeakHoldCounter = 15;
    }
    update();
}

void AudioMeterWidget::reset() {
    m_leftLevel = 0.0f;
    m_rightLevel = 0.0f;
    m_leftPeakHold = 0.0f;
    m_rightPeakHold = 0.0f;
    update();
}

void AudioMeterWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int w = width();
    const int h = height();

    // Background
    p.fillRect(rect(), QColor(20, 20, 24));
    p.setPen(QColor(45, 45, 52));
    p.drawRect(0, 0, w - 1, h - 1);

    const int headerH = 14;
    const int footerH = 12;
    const int barTop = headerH;
    const int barBottom = h - footerH;
    const int barH = barBottom - barTop;

    if (barH <= 4) return;

    // Header labels L and R
    p.setFont(QFont("Monospace", 8, QFont::Bold));
    p.setPen(QColor(180, 180, 190));
    p.drawText(QRect(2, 1, (w - 6) / 2, headerH), Qt::AlignCenter, "L");
    p.drawText(QRect(w / 2, 1, (w - 6) / 2, headerH), Qt::AlignCenter, "R");

    const int barWidth = std::max(4, (w - 10) / 2);
    const int leftX = 3;
    const int rightX = w - barWidth - 3;

    // Helper to draw a meter channel
    auto drawChannel = [&](int x, float level, float peakHold) {
        // Background track
        p.fillRect(x, barTop, barWidth, barH, QColor(12, 12, 15));

        // Draw segmented LED blocks
        const int numSegments = std::max(8, barH / 4);
        const float segH = static_cast<float>(barH) / static_cast<float>(numSegments);

        for (int i = 0; i < numSegments; ++i) {
            float segFrac = 1.0f - (static_cast<float>(i) + 0.5f) / static_cast<float>(numSegments);
            if (segFrac > level) continue;

            // Color based on level height: Green (< 70%), Yellow (70-90%), Red (> 90%)
            QColor col;
            if (segFrac < 0.70f) {
                col = QColor(40, 200, 80);
            } else if (segFrac < 0.90f) {
                col = QColor(255, 200, 40);
            } else {
                col = QColor(255, 50, 50);
            }

            int sy = barTop + static_cast<int>(i * segH);
            int sh = std::max(1, static_cast<int>(segH - 1));
            p.fillRect(x, sy, barWidth, sh, col);
        }

        // Draw peak hold line
        if (peakHold > 0.01f) {
            int peakY = barTop + static_cast<int>((1.0f - std::clamp(peakHold, 0.0f, 1.0f)) * (barH - 1));
            QColor peakCol = (peakHold > 0.90f) ? QColor(255, 80, 80) : QColor(255, 230, 100);
            p.setPen(peakCol);
            p.drawLine(x, peakY, x + barWidth - 1, peakY);
        }
    };

    drawChannel(leftX, m_leftLevel, m_leftPeakHold);
    drawChannel(rightX, m_rightLevel, m_rightPeakHold);

    // dB tick marks in center if wide enough
    if (w >= 50) {
        p.setPen(QColor(100, 100, 110));
        p.setFont(QFont("Sans", 6));
        const int midX = w / 2;
        p.drawLine(midX - 2, barTop, midX + 2, barTop); // 0dB
        p.drawLine(midX - 2, barTop + barH / 4, midX + 2, barTop + barH / 4); // -6dB
        p.drawLine(midX - 2, barTop + barH / 2, midX + 2, barTop + barH / 2); // -18dB
    }
}

} // namespace hc
