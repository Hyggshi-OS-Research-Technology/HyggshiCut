#include "TransformOverlay.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QCursor>
#include <cmath>
#include <algorithm>

namespace hc {

static double deg2rad(double d) { return d * M_PI / 180.0; }
static double vecLen(QPointF v) { return std::sqrt(v.x()*v.x() + v.y()*v.y()); }

// ─── TransformOverlay ────────────────────────────────────────────────────────

TransformOverlay::TransformOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    hide();
}

// ── Public API ───────────────────────────────────────────────────────────────

void TransformOverlay::setSelectedClip(const Transform& transform, int srcW, int srcH) {
    m_transform = transform;
    m_srcW   = srcW;
    m_srcH   = srcH;
    m_active = (srcW > 0 && srcH > 0);
    if (m_active) {
        if (parentWidget()) {
            setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
            raise();
        }
        computeHandlePoints();
        show();
    } else {
        hide();
    }
    update();
}

void TransformOverlay::setTransformExternal(const Transform& t) {
    m_transform = t;
    if (parentWidget() && (width() != parentWidget()->width() || height() != parentWidget()->height())) {
        setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
        raise();
    }
    computeHandlePoints();
    update();
}

void TransformOverlay::clearSelection() {
    m_active   = false;
    m_dragging = HitZone::None;
    hide();
}

void TransformOverlay::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    computeHandlePoints();
    update();
}

// ── Coordinate mapping ───────────────────────────────────────────────────────

// ── Coordinate mapping ───────────────────────────────────────────────────────

void TransformOverlay::naturalFitPx(double& outW, double& outH) const {
    if (width() <= 0 || height() <= 0) { outW = outH = 1.0; return; }
    const double widgetAspect = static_cast<double>(width()) / static_cast<double>(height());
    const double sourceAspect = (m_srcW > 0 && m_srcH > 0) ? (static_cast<double>(m_srcW) / static_cast<double>(m_srcH)) : widgetAspect;
    if (sourceAspect > widgetAspect) {
        outW = width();
        outH = width() / sourceAspect;
    } else {
        outH = height();
        outW = height() * sourceAspect;
    }
}

QPointF TransformOverlay::toWidget(double nx, double ny) const {
    const double cx = width()  / 2.0;
    const double cy = height() / 2.0;
    const double ux = width()  / 2.0;
    const double uy = height() / 2.0;
    return { cx + nx * ux, cy + ny * uy };
}

QPointF TransformOverlay::fromWidget(QPointF wp) const {
    const double cx = width()  / 2.0;
    const double cy = height() / 2.0;
    const double ux = width()  / 2.0;
    const double uy = height() / 2.0;
    return { (wp.x() - cx) / ux, (wp.y() - cy) / uy };
}

QPointF TransformOverlay::cornerWidget(double nx, double ny) const {
    if (width() <= 0 || height() <= 0) return {0, 0};

    double fw, fh;
    naturalFitPx(fw, fh);

    const double cx = (width()  / 2.0) + m_transform.x * (width()  / 2.0);
    const double cy = (height() / 2.0) + m_transform.y * (height() / 2.0);

    const double halfW_px = (fw / 2.0) * m_transform.scaleX;
    const double halfH_px = (fh / 2.0) * m_transform.scaleY;

    const double px = nx * halfW_px;
    const double py = ny * halfH_px;

    const double rad = deg2rad(m_transform.rotationDeg);
    const double c = std::cos(rad), s = std::sin(rad);
    const double rot_x = px * c - py * s;
    const double rot_y = px * s + py * c;

    return QPointF(cx + rot_x, cy + rot_y);
}

void TransformOverlay::computeHandlePoints() {
    if (parentWidget() && (width() != parentWidget()->width() || height() != parentWidget()->height())) {
        setGeometry(0, 0, parentWidget()->width(), parentWidget()->height());
    }

    m_centre = QPointF((width()  / 2.0) + m_transform.x * (width()  / 2.0),
                       (height() / 2.0) + m_transform.y * (height() / 2.0));
    m_anchor = m_centre;

    m_corners[0] = cornerWidget(-1, -1);  // NW (left, top)
    m_corners[1] = cornerWidget( 1, -1);  // NE (right, top)
    m_corners[2] = cornerWidget( 1,  1);  // SE (right, bottom)
    m_corners[3] = cornerWidget(-1,  1);  // SW (left, bottom)

    // Mid-edges: average of adjacent corners.
    m_edges[0] = (m_corners[0] + m_corners[1]) / 2.0;  // N (top)
    m_edges[1] = (m_corners[1] + m_corners[2]) / 2.0;  // E (right)
    m_edges[2] = (m_corners[2] + m_corners[3]) / 2.0;  // S (bottom)
    m_edges[3] = (m_corners[3] + m_corners[0]) / 2.0;  // W (left)
}

// ── Hit-testing ──────────────────────────────────────────────────────────────

TransformOverlay::HitZone TransformOverlay::hitTest(QPointF pos) const {
    // Anchor
    if (vecLen(pos - m_anchor) < kAnchorR + 4) return HitZone::Anchor;

    // Corners (inner hit) and rotate zone (slightly outside).
    const HitZone cornerZones[4] = { HitZone::CornerNW, HitZone::CornerNE,
                                      HitZone::CornerSE, HitZone::CornerSW };
    const HitZone rotateZones[4] = { HitZone::RotateNW, HitZone::RotateNE,
                                      HitZone::RotateSE, HitZone::RotateSW };
    for (int i = 0; i < 4; ++i) {
        double d = vecLen(pos - m_corners[i]);
        if (d <= kHandleR + 2)                            return cornerZones[i];
        if (d <= kHandleR + kRotateZone)                  return rotateZones[i];
    }

    // Edge handles.
    const HitZone edgeZones[4] = { HitZone::EdgeN, HitZone::EdgeE,
                                    HitZone::EdgeS, HitZone::EdgeW };
    for (int i = 0; i < 4; ++i) {
        if (vecLen(pos - m_edges[i]) < kHandleR + 3)     return edgeZones[i];
    }

    // Inside bounding box? Un-rotate pos relative to m_centre in 2D pixel space:
    double fw, fh;
    naturalFitPx(fw, fh);
    const double halfW_px = (fw / 2.0) * std::abs(m_transform.scaleX);
    const double halfH_px = (fh / 2.0) * std::abs(m_transform.scaleY);

    const QPointF diff = pos - m_centre;
    // Un-rotate by -rotationDeg in Qt pixel space
    const double rad = deg2rad(-m_transform.rotationDeg);
    const double c = std::cos(rad), s = std::sin(rad);
    const double localX = diff.x() * c - diff.y() * s;
    const double localY = diff.x() * s + diff.y() * c;

    if (std::abs(localX) <= halfW_px && std::abs(localY) <= halfH_px)
        return HitZone::Inside;

    return HitZone::None;
}

Qt::CursorShape TransformOverlay::cursorForZone(HitZone z) const {
    switch (z) {
    case HitZone::Inside:     return Qt::SizeAllCursor;
    case HitZone::Anchor:     return Qt::CrossCursor;
    case HitZone::RotateNW:
    case HitZone::RotateNE:
    case HitZone::RotateSE:
    case HitZone::RotateSW:   return Qt::CrossCursor;
    case HitZone::CornerNW:
    case HitZone::CornerSE:   return Qt::SizeFDiagCursor;
    case HitZone::CornerNE:
    case HitZone::CornerSW:   return Qt::SizeBDiagCursor;
    case HitZone::EdgeN:
    case HitZone::EdgeS:      return Qt::SizeVerCursor;
    case HitZone::EdgeE:
    case HitZone::EdgeW:      return Qt::SizeHorCursor;
    default:                  return Qt::ArrowCursor;
    }
}

// ── Drag logic ───────────────────────────────────────────────────────────────

void TransformOverlay::applyDrag(QPointF currentPos, Qt::KeyboardModifiers mods) {
    const QPointF delta = currentPos - m_dragStart; // pixels
    Transform t = m_startTransform;

    const bool shiftHeld = mods.testFlag(Qt::ShiftModifier);

    switch (m_dragging) {

    case HitZone::Inside: {
        // Move: convert pixel delta to normalised half-frame units.
        t.x = m_startTransform.x + delta.x() / (width()  / 2.0);
        t.y = m_startTransform.y + delta.y() / (height() / 2.0);
        break;
    }

    case HitZone::Anchor: {
        break;
    }

    case HitZone::RotateNW:
    case HitZone::RotateNE:
    case HitZone::RotateSE:
    case HitZone::RotateSW: {
        // Rotate: angle in Qt pixel space from centre
        QPointF vStart = m_dragStart - m_centre;
        QPointF vCur   = currentPos  - m_centre;
        const double a0 = std::atan2(vStart.y(), vStart.x());
        const double a1 = std::atan2(vCur.y(),   vCur.x());
        double dDeg = (a1 - a0) * 180.0 / M_PI;
        t.rotationDeg = m_startTransform.rotationDeg + dDeg;
        // Snap to 45° increments when Shift is held.
        if (shiftHeld) {
            t.rotationDeg = std::round(t.rotationDeg / 45.0) * 45.0;
        }
        break;
    }

    case HitZone::CornerNW:
    case HitZone::CornerNE:
    case HitZone::CornerSE:
    case HitZone::CornerSW: {
        const int ci = (m_dragging == HitZone::CornerNW) ? 0
                      :(m_dragging == HitZone::CornerNE) ? 1
                      :(m_dragging == HitZone::CornerSE) ? 2 : 3;

        QPointF startCorner = m_startDragCorners[ci] - m_centre;
        QPointF curCorner   = startCorner + delta;

        double lenStart = vecLen(startCorner);
        double lenCur   = vecLen(curCorner);
        if (lenStart < 1.0) break;

        double ratio = lenCur / lenStart;

        if (shiftHeld) {
            // Proportional: scale both axes equally.
            t.scaleX = std::max(0.05, m_startTransform.scaleX * ratio);
            t.scaleY = std::max(0.05, m_startTransform.scaleY * ratio);
        } else {
            double fw, fh;
            naturalFitPx(fw, fh);
            const double unitW = fw / 2.0;
            const double unitH = fh / 2.0;

            // Un-rotate current vector in pixel space relative to centre
            const double rad = deg2rad(-m_startTransform.rotationDeg);
            const double c = std::cos(rad), s = std::sin(rad);
            const double localX = curCorner.x() * c - curCorner.y() * s;
            const double localY = curCorner.x() * s + curCorner.y() * c;

            // NW (0): nx=-1, ny=-1; NE (1): nx=+1, ny=-1; SE (2): nx=+1, ny=+1; SW (3): nx=-1, ny=+1
            const double signX = (ci == 0 || ci == 3) ? -1.0 : 1.0;
            const double signY = (ci == 0 || ci == 1) ? -1.0 : 1.0;

            if (unitW > 0.0) t.scaleX = std::max(0.05, (signX * localX) / unitW);
            if (unitH > 0.0) t.scaleY = std::max(0.05, (signY * localY) / unitH);
        }
        break;
    }

    case HitZone::EdgeN:
    case HitZone::EdgeS: {
        double fw, fh; naturalFitPx(fw, fh);
        const double unitH = fh / 2.0;
        const int ei = (m_dragging == HitZone::EdgeN) ? 0 : 2;
        const QPointF curPos = (m_edges[ei] - m_centre) + delta;
        const double rad = deg2rad(-m_startTransform.rotationDeg);
        const double c = std::cos(rad), s = std::sin(rad);
        const double localY = curPos.x() * s + curPos.y() * c;
        const double sign = (m_dragging == HitZone::EdgeS) ? 1.0 : -1.0;
        if (unitH > 0.0) t.scaleY = std::max(0.05, (sign * localY) / unitH);
        break;
    }

    case HitZone::EdgeE:
    case HitZone::EdgeW: {
        double fw, fh; naturalFitPx(fw, fh);
        const double unitW = fw / 2.0;
        const int ei = (m_dragging == HitZone::EdgeE) ? 1 : 3;
        const QPointF curPos = (m_edges[ei] - m_centre) + delta;
        const double rad = deg2rad(-m_startTransform.rotationDeg);
        const double c = std::cos(rad), s = std::sin(rad);
        const double localX = curPos.x() * c - curPos.y() * s;
        const double sign = (m_dragging == HitZone::EdgeE) ? 1.0 : -1.0;
        if (unitW > 0.0) t.scaleX = std::max(0.05, (sign * localX) / unitW);
        break;
    }

    default: break;
    }

    m_transform = t;
    update();
    emit transformChanged(t);
}

// ── Mouse events ─────────────────────────────────────────────────────────────

void TransformOverlay::mousePressEvent(QMouseEvent* event) {
    if (!m_active || event->button() != Qt::LeftButton) return;
    computeHandlePoints();
    HitZone z = hitTest(event->position());
    if (z == HitZone::None) { event->ignore(); return; }

    m_dragging = z;
    m_dragStart = event->position();
    m_dragStartAnchor = m_anchor;
    m_startTransform  = m_transform;
    // Cache corner positions at drag start for corner-scale calcs.
    for (int i = 0; i < 4; ++i) m_startDragCorners[i] = m_corners[i];
    emit dragStarted();
    event->accept();
}

void TransformOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (!m_active) return;
    computeHandlePoints();

    if (m_dragging != HitZone::None) {
        applyDrag(event->position(), event->modifiers());
    } else {
        HitZone z = hitTest(event->position());
        if (z != m_hovering) {
            m_hovering = z;
            setCursor(cursorForZone(z));
            update();
        }
    }
    event->accept();
}

void TransformOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_active || event->button() != Qt::LeftButton) return;
    if (m_dragging != HitZone::None) {
        applyDrag(event->position(), event->modifiers());
        m_dragging = HitZone::None;
        emit transformCommitted(m_transform);
    }
    event->accept();
}

void TransformOverlay::mouseDoubleClickEvent(QMouseEvent* event) {
    event->ignore();
}

void TransformOverlay::leaveEvent(QEvent*) {
    m_hovering = HitZone::None;
    unsetCursor();
    update();
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void TransformOverlay::paintEvent(QPaintEvent*) {
    if (!m_active) return;
    computeHandlePoints();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // ── Bounding box outline ─────────────────────────────────────────────────
    QPainterPath boxPath;
    boxPath.moveTo(m_corners[0]);
    for (int i = 1; i < 4; ++i) boxPath.lineTo(m_corners[i]);
    boxPath.closeSubpath();

    // Shadow
    p.setPen(QPen(QColor(0, 0, 0, 100), 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(boxPath);
    // White border
    p.setPen(QPen(QColor(255, 255, 255, 220), 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(boxPath);

    // ── Draw dashed lines from centre to corners (subtle guide) ─────────────
    p.setPen(QPen(QColor(255, 255, 255, 40), 0.8, Qt::DashLine));
    for (int i = 0; i < 4; ++i) p.drawLine(m_centre, m_corners[i]);

    // ── Corner handles ───────────────────────────────────────────────────────
    const QColor handleFill(240, 240, 255, 235);
    const QColor handleBorder(80, 120, 220, 240);
    p.setPen(QPen(handleBorder, 1.5));
    p.setBrush(handleFill);
    for (int i = 0; i < 4; ++i) {
        p.drawEllipse(m_corners[i], kHandleR, kHandleR);
    }

    // ── Edge handles ─────────────────────────────────────────────────────────
    const QColor edgeFill(220, 235, 255, 220);
    p.setPen(QPen(handleBorder, 1.2));
    p.setBrush(edgeFill);
    for (int i = 0; i < 4; ++i) {
        p.drawEllipse(m_edges[i], kHandleR - 2, kHandleR - 2);
    }

    // ── Rotate arc hints (small arc outside each corner) ─────────────────────
    {
        p.save();
        p.setPen(QPen(QColor(255, 210, 50, 180), 1.5, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        const double arcRad = kHandleR + 8.0;
        for (int i = 0; i < 4; ++i) {
            // Draw a small arc centred on each corner.
            const double startAngles[4] = { 135.0, 45.0, -45.0, -135.0 };
            QRectF rect(m_corners[i] - QPointF(arcRad, arcRad),
                        QSizeF(arcRad * 2, arcRad * 2));
            p.drawArc(rect, (int)(startAngles[i] * 16), (int)(70 * 16));
        }
        p.restore();
    }

    // ── Anchor / pivot diamond ───────────────────────────────────────────────
    {
        p.save();
        const double r = kAnchorR;
        QPainterPath diamond;
        diamond.moveTo(m_anchor + QPointF( 0, -r));
        diamond.lineTo(m_anchor + QPointF( r,  0));
        diamond.lineTo(m_anchor + QPointF( 0,  r));
        diamond.lineTo(m_anchor + QPointF(-r,  0));
        diamond.closeSubpath();
        p.setPen(QPen(QColor(60, 60, 60, 200), 1.2));
        p.setBrush(QColor(255, 255, 255, 200));
        p.drawPath(diamond);

        // Crosshair
        p.setPen(QPen(QColor(60, 60, 60, 180), 1.0));
        p.drawLine(m_anchor - QPointF(r + 4, 0), m_anchor + QPointF(r + 4, 0));
        p.drawLine(m_anchor - QPointF(0, r + 4), m_anchor + QPointF(0, r + 4));
        p.restore();
    }

    // ── Hover highlight ──────────────────────────────────────────────────────
    if (m_hovering != HitZone::None && m_dragging == HitZone::None) {
        p.save();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(100, 160, 255, 45));
        p.drawPath(boxPath);
        p.restore();
    }
}

} // namespace hc
