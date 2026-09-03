#include "ColorWheelWidget.h"
#include <cmath>
#include <QPainterPath>
#include <QToolTip>

namespace hc {

ColorWheelWidget::ColorWheelWidget(const QString& title, QWidget* parent)
    : QWidget(parent), m_title(title) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

QRect ColorWheelWidget::wheelRect() const {
    const int topMargin = 28;
    const int bottomMargin = 24;
    const int rightMargin = 30; // space for vertical luma slider
    const int leftMargin = 6;

    const int availW = width() - leftMargin - rightMargin;
    const int availH = height() - topMargin - bottomMargin;
    const int diameter = std::max(20, std::min(availW, availH));

    const int x = leftMargin + (availW - diameter) / 2;
    const int y = topMargin + (availH - diameter) / 2;
    return QRect(x, y, diameter, diameter);
}

QRect ColorWheelWidget::lumaRect() const {
    const QRect wr = wheelRect();
    const int barW = 14;
    const int barX = wr.right() + 10;
    return QRect(barX, wr.top(), barW, wr.height());
}

void ColorWheelWidget::updateWheelImage() {
    const int d = wheelRect().width();
    if (d <= 0 || (m_cachedDiameter == d && !m_wheelImage.isNull())) return;

    m_cachedDiameter = d;
    m_wheelImage = QImage(d, d, QImage::Format_ARGB32_Premultiplied);
    m_wheelImage.fill(Qt::transparent);

    const double radius = d / 2.0;

    for (int y = 0; y < d; ++y) {
        auto* scanLine = reinterpret_cast<QRgb*>(m_wheelImage.scanLine(y));
        const double dy = (y + 0.5 - radius) / radius;
        for (int x = 0; x < d; ++x) {
            const double dx = (x + 0.5 - radius) / radius;
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (dist > 1.02) {
                scanLine[x] = 0;
                continue;
            }

            const double angleRad = std::atan2(-dy, dx);
            double angleDeg = angleRad * 180.0 / M_PI;
            if (angleDeg < 0.0) angleDeg += 360.0;

            const double sat = std::clamp(dist, 0.0, 1.0);
            QColor col = QColor::fromHsvF(angleDeg / 360.0, sat, 1.0);

            // Antialiased edge
            double alpha = 1.0;
            if (dist > 0.98) {
                alpha = std::clamp((1.0 - dist) / 0.04, 0.0, 1.0);
            }

            col.setAlphaF(alpha);
            scanLine[x] = col.rgba();
        }
    }
}

void ColorWheelWidget::computeRgb() {
    const double dist = std::sqrt(m_wheelX * m_wheelX + m_wheelY * m_wheelY);
    if (dist > 0.0001) {
        const double angleRad = std::atan2(-m_wheelY, m_wheelX);
        double angleDeg = angleRad * 180.0 / M_PI;
        if (angleDeg < 0.0) angleDeg += 360.0;

        const double sat = std::clamp(dist, 0.0, 1.0);
        QColor col = QColor::fromHsvF(angleDeg / 360.0, sat, 1.0);

        const double tint_r = (col.redF() - 0.5) * 2.0 * sat;
        const double tint_g = (col.greenF() - 0.5) * 2.0 * sat;
        const double tint_b = (col.blueF() - 0.5) * 2.0 * sat;

        m_r = std::clamp(tint_r + m_luma, -1.0, 1.0);
        m_g = std::clamp(tint_g + m_luma, -1.0, 1.0);
        m_b = std::clamp(tint_b + m_luma, -1.0, 1.0);
    } else {
        m_r = std::clamp(m_luma, -1.0, 1.0);
        m_g = std::clamp(m_luma, -1.0, 1.0);
        m_b = std::clamp(m_luma, -1.0, 1.0);
    }
}

void ColorWheelWidget::setValues(double r, double g, double b, double luma, bool emitSignal) {
    m_r = std::clamp(r, -1.0, 1.0);
    m_g = std::clamp(g, -1.0, 1.0);
    m_b = std::clamp(b, -1.0, 1.0);
    m_luma = std::clamp(luma, -1.0, 1.0);

    // Approximate back to wheelX / wheelY
    const double tr = m_r - m_luma;
    const double tg = m_g - m_luma;
    const double tb = m_b - m_luma;

    // Convert RGB delta back to HSV
    const double maxVal = std::max({tr, tg, tb, 0.0});
    const double minVal = std::min({tr, tg, tb, 0.0});
    const double delta = maxVal - minVal;

    if (delta > 0.001) {
        double hueDeg = 0.0;
        if (maxVal == tr) {
            hueDeg = 60.0 * std::fmod(((tg - tb) / delta), 6.0);
        } else if (maxVal == tg) {
            hueDeg = 60.0 * (((tb - tr) / delta) + 2.0);
        } else {
            hueDeg = 60.0 * (((tr - tg) / delta) + 4.0);
        }
        if (hueDeg < 0.0) hueDeg += 360.0;
        const double sat = std::clamp(delta, 0.0, 1.0);
        const double rad = hueDeg * M_PI / 180.0;
        m_wheelX = sat * std::cos(rad);
        m_wheelY = -sat * std::sin(rad);
    } else {
        m_wheelX = 0.0;
        m_wheelY = 0.0;
    }

    update();
    if (emitSignal) emit valueChanged(m_r, m_g, m_b, m_luma);
}

void ColorWheelWidget::setWheelAndLuma(double x, double y, double luma, bool emitSignal) {
    const double dist = std::sqrt(x * x + y * y);
    if (dist > 1.0) {
        m_wheelX = x / dist;
        m_wheelY = y / dist;
    } else {
        m_wheelX = x;
        m_wheelY = y;
    }
    m_luma = std::clamp(luma, -1.0, 1.0);
    computeRgb();
    update();
    if (emitSignal) emit valueChanged(m_r, m_g, m_b, m_luma);
}

void ColorWheelWidget::reset(bool emitSignal) {
    m_wheelX = 0.0;
    m_wheelY = 0.0;
    m_luma = 0.0;
    m_r = 0.0;
    m_g = 0.0;
    m_b = 0.0;
    update();
    if (emitSignal) emit valueChanged(0.0, 0.0, 0.0, 0.0);
}

void ColorWheelWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    m_cachedDiameter = 0;
    updateWheelImage();
}

void ColorWheelWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // ── 1. Title ─────────────────────────────────────────────────────────────
    p.setPen(QColor(230, 230, 240));
    QFont font = p.font();
    font.setBold(true);
    font.setPointSize(9);
    p.setFont(font);
    p.drawText(QRect(0, 4, width(), 20), Qt::AlignCenter, m_title);

    // ── 2. Color Wheel ───────────────────────────────────────────────────────
    const QRect wr = wheelRect();
    updateWheelImage();
    if (!m_wheelImage.isNull()) {
        p.drawImage(wr.topLeft(), m_wheelImage);
    }

    // Outer ring border
    p.setPen(QPen(QColor(60, 60, 68), 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(wr);

    // Inner crosshairs guide (subtle 50% gray lines)
    const QPoint center = wr.center();
    p.setPen(QPen(QColor(255, 255, 255, 25), 1.0, Qt::DashLine));
    p.drawLine(center.x() - wr.width() / 4, center.y(), center.x() + wr.width() / 4, center.y());
    p.drawLine(center.x(), center.y() - wr.height() / 4, center.x(), center.y() + wr.height() / 4);

    // Color dot handle (current selected tint)
    const double radius = wr.width() / 2.0;
    const double hx = center.x() + m_wheelX * radius;
    const double hy = center.y() + m_wheelY * radius;
    const QPointF handlePos(hx, hy);

    // Handle outer shadow
    p.setPen(QPen(QColor(0, 0, 0, 180), 2.5));
    p.setBrush(QColor(255, 255, 255, 240));
    p.drawEllipse(handlePos, 4.5, 4.5);
    p.setPen(QPen(QColor(20, 20, 25), 1.0));
    p.setBrush(QColor(0, 0, 0, 220));
    p.drawEllipse(handlePos, 1.5, 1.5);

    // ── 3. Vertical Luminance Bar ────────────────────────────────────────────
    const QRect lr = lumaRect();

    // Background gradient (White at top -> Mid-gray at center -> Black at bottom)
    QLinearGradient grad(lr.topLeft(), lr.bottomLeft());
    grad.setColorAt(0.0, QColor(255, 255, 255));
    grad.setColorAt(0.5, QColor(128, 128, 128));
    grad.setColorAt(1.0, QColor(10, 10, 10));

    p.setPen(QPen(QColor(50, 50, 58), 1.0));
    p.setBrush(grad);
    p.drawRoundedRect(lr, 3, 3);

    // Midpoint zero tick
    const int midY = lr.center().y();
    p.setPen(QPen(QColor(0, 0, 0, 160), 1.5));
    p.drawLine(lr.left() - 2, midY, lr.right() + 2, midY);

    // Slider thumb (horizontal rounded slider handle)
    // m_luma: +1.0 is top (lr.top()), -1.0 is bottom (lr.bottom()), 0.0 is center
    const double normLuma = (1.0 - m_luma) / 2.0; // 0.0 at top, 1.0 at bottom
    const int thumbY = lr.top() + static_cast<int>(normLuma * lr.height());
    const QRect thumbRect(lr.left() - 3, thumbY - 3, lr.width() + 6, 7);

    p.setPen(QPen(QColor(20, 20, 25), 1.0));
    p.setBrush(QColor(240, 240, 248));
    p.drawRoundedRect(thumbRect, 2, 2);

    // ── 4. Value label & Reset button at bottom ──────────────────────────────
    font.setBold(false);
    font.setPointSize(8);
    p.setFont(font);
    p.setPen(QColor(160, 160, 170));

    const QString valText = QString("R:%1 G:%2 B:%3")
        .arg(m_r >= 0 ? QString("+%1").arg(m_r, 0, 'f', 2) : QString::number(m_r, 'f', 2))
        .arg(m_g >= 0 ? QString("+%1").arg(m_g, 0, 'f', 2) : QString::number(m_g, 'f', 2))
        .arg(m_b >= 0 ? QString("+%1").arg(m_b, 0, 'f', 2) : QString::number(m_b, 'f', 2));
    p.drawText(QRect(0, height() - 20, width(), 18), Qt::AlignCenter, valText);
}

void ColorWheelWidget::handleMouseInWheel(const QPointF& pos) {
    const QRect wr = wheelRect();
    const QPointF center = wr.center();
    const double radius = wr.width() / 2.0;
    if (radius <= 0) return;

    double dx = (pos.x() - center.x()) / radius;
    double dy = (pos.y() - center.y()) / radius;

    const double dist = std::sqrt(dx * dx + dy * dy);
    if (dist > 1.0) {
        dx /= dist;
        dy /= dist;
    }

    m_wheelX = dx;
    m_wheelY = dy;
    computeRgb();
    update();
    emit valueChanged(m_r, m_g, m_b, m_luma);
}

void ColorWheelWidget::handleMouseInLuma(const QPointF& pos) {
    const QRect lr = lumaRect();
    if (lr.height() <= 0) return;

    const double norm = std::clamp((pos.y() - lr.top()) / static_cast<double>(lr.height()), 0.0, 1.0);
    // norm = 0.0 -> luma = +1.0 (top), norm = 1.0 -> luma = -1.0 (bottom)
    m_luma = 1.0 - norm * 2.0;

    // Snap to 0.0 near center
    if (std::abs(m_luma) < 0.04) m_luma = 0.0;

    computeRgb();
    update();
    emit valueChanged(m_r, m_g, m_b, m_luma);
}

void ColorWheelWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;

    const QRect wr = wheelRect();
    const QRect lr = lumaRect().adjusted(-4, -4, 4, 4);

    if (lr.contains(event->pos())) {
        m_dragTarget = DragTarget::Luma;
        handleMouseInLuma(event->position());
    } else if (wr.contains(event->pos())) {
        m_dragTarget = DragTarget::Wheel;
        handleMouseInWheel(event->position());
    }
}

void ColorWheelWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragTarget == DragTarget::Wheel) {
        handleMouseInWheel(event->position());
    } else if (m_dragTarget == DragTarget::Luma) {
        handleMouseInLuma(event->position());
    } else {
        // Cursor hover hints
        const QRect wr = wheelRect();
        const QRect lr = lumaRect().adjusted(-2, -2, 2, 2);
        if (wr.contains(event->pos()) || lr.contains(event->pos())) {
            setCursor(Qt::CrossCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }
    }
}

void ColorWheelWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragTarget = DragTarget::None;
    }
}

void ColorWheelWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    const QRect wr = wheelRect();
    const QRect lr = lumaRect().adjusted(-4, -4, 4, 4);

    if (lr.contains(event->pos())) {
        m_luma = 0.0;
        computeRgb();
        update();
        emit valueChanged(m_r, m_g, m_b, m_luma);
    } else if (wr.contains(event->pos())) {
        m_wheelX = 0.0;
        m_wheelY = 0.0;
        computeRgb();
        update();
        emit valueChanged(m_r, m_g, m_b, m_luma);
    } else {
        reset(true);
    }
}

} // namespace hc
