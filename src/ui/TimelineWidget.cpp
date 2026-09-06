#include "TimelineWidget.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QToolTip>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QWheelEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QColorDialog>
#include <algorithm>
#include <cmath>

namespace hc {

namespace {
constexpr Ticks kMinClipDuration = 33'333; // ~1 frame at 30fps, floor for trims
constexpr int kEdgeGrabPx = 6;
constexpr Ticks kDefaultTransitionTicks = 500'000; // 0.5s

// --- Sleek vector button rendering for the per-track control cards ---
void drawControlButton(QPainter& p, const QRect& r, int ctrlIndex, bool active, bool hovered) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    QColor bgColor;
    QColor borderColor;

    if (hovered) {
        if (ctrlIndex == 3) { // Delete
            bgColor = QColor(239, 68, 68, 55);
            borderColor = QColor(239, 68, 68, 140);
        } else {
            bgColor = QColor(255, 255, 255, 28);
            borderColor = QColor(255, 255, 255, 80);
        }
    } else if (active) {
        if (ctrlIndex == 2) { // Lock
            bgColor = QColor(245, 158, 11, 45);
            borderColor = QColor(245, 158, 11, 120);
        } else if (ctrlIndex == 0 || ctrlIndex == 1) { // Muted or Hidden
            bgColor = QColor(239, 68, 68, 35);
            borderColor = QColor(239, 68, 68, 100);
        } else {
            bgColor = QColor(255, 255, 255, 14);
            borderColor = QColor(255, 255, 255, 35);
        }
    } else {
        bgColor = QColor(255, 255, 255, 10);
        borderColor = QColor(255, 255, 255, 22);
    }

    p.setBrush(bgColor);
    p.setPen(QPen(borderColor, 1.0));
    p.drawRoundedRect(r, 4.0, 4.0);

    const int cx = r.center().x();
    const int cy = r.center().y();

    switch (ctrlIndex) {
    case 0: { // Mute (Speaker)
        const QColor iconColor = active ? QColor(248, 113, 113) : (hovered ? QColor(248, 250, 252) : QColor(148, 163, 184));
        // Speaker horn
        QPainterPath speaker;
        speaker.moveTo(cx - 5, cy - 2);
        speaker.lineTo(cx - 2, cy - 2);
        speaker.lineTo(cx + 1, cy - 5);
        speaker.lineTo(cx + 1, cy + 5);
        speaker.lineTo(cx - 2, cy + 2);
        speaker.lineTo(cx - 5, cy + 2);
        speaker.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(iconColor);
        p.drawPath(speaker);

        if (!active) {
            // Soundwaves
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(iconColor, 1.4, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(cx + 1, cy - 3, 5, 6, -45 * 16, 90 * 16);
            p.drawArc(cx + 1, cy - 5, 8, 10, -45 * 16, 90 * 16);
        } else {
            // Crisp mute 'x' marker next to cone (no ugly slash cutting the whole icon)
            p.setPen(QPen(QColor(239, 68, 68), 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(cx + 3, cy - 3, cx + 7, cy + 3);
            p.drawLine(cx + 7, cy - 3, cx + 3, cy + 3);
        }
        break;
    }
    case 1: { // Hidden (Eye)
        const QColor iconColor = active ? QColor(248, 113, 113) : (hovered ? QColor(248, 250, 252) : QColor(148, 163, 184));
        if (!active) {
            // Open eye
            QPainterPath eyePath;
            eyePath.moveTo(cx - 6, cy);
            eyePath.quadTo(cx, cy - 4.5, cx + 6, cy);
            eyePath.quadTo(cx, cy + 4.5, cx - 6, cy);
            p.setPen(QPen(iconColor, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(eyePath);
            p.setBrush(iconColor);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QPoint(cx, cy), 2, 2);
        } else {
            // Eye with clean diagonal slash
            QPainterPath eyePath;
            eyePath.moveTo(cx - 6, cy);
            eyePath.quadTo(cx, cy - 4, cx + 6, cy);
            eyePath.quadTo(cx, cy + 4, cx - 6, cy);
            p.setPen(QPen(QColor(148, 163, 184, 110), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(eyePath);
            p.setPen(QPen(QColor(239, 68, 68), 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(cx - 5, cy + 4, cx + 5, cy - 4);
        }
        break;
    }
    case 2: { // Lock
        const QColor bodyColor = active ? QColor(245, 158, 11) : (hovered ? QColor(248, 250, 252) : QColor(148, 163, 184));
        const QColor shackleColor = active ? QColor(251, 191, 36) : (hovered ? QColor(203, 213, 225) : QColor(100, 116, 139));

        p.setPen(QPen(shackleColor, 1.4, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        if (active) {
            // Closed shackle
            p.drawArc(cx - 4, cy - 6, 8, 8, 0, 180 * 16);
            p.drawLine(cx - 4, cy - 2, cx - 4, cy);
            p.drawLine(cx + 4, cy - 2, cx + 4, cy);
        } else {
            // Open shackle
            p.drawArc(cx - 5, cy - 7, 8, 8, 0, 180 * 16);
            p.drawLine(cx - 5, cy - 3, cx - 5, cy);
        }
        // Lock body
        p.setPen(QPen(bodyColor.darker(120), 0.8));
        p.setBrush(bodyColor);
        p.drawRoundedRect(QRect(cx - 5, cy - 1, 10, 8), 2.0, 2.0);
        // Keyhole dot
        p.setPen(Qt::NoPen);
        p.setBrush(active ? QColor(120, 53, 15) : QColor(30, 41, 59));
        p.drawEllipse(QPoint(cx, cy + 3), 1, 1);
        break;
    }
    case 3: { // Delete (Trash bin)
        const QColor color = hovered ? QColor(248, 113, 113) : QColor(148, 163, 184);
        p.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        // Lid handle
        p.drawLine(cx - 2, cy - 6, cx + 2, cy - 6);
        // Lid brim
        p.drawLine(cx - 5, cy - 4, cx + 5, cy - 4);
        // Bin body
        QPainterPath bin;
        bin.moveTo(cx - 4, cy - 3);
        bin.lineTo(cx - 3, cy + 4);
        bin.quadTo(cx - 3, cy + 5, cx - 2, cy + 5);
        bin.lineTo(cx + 2, cy + 5);
        bin.quadTo(cx + 3, cy + 5, cx + 3, cy + 4);
        bin.lineTo(cx + 4, cy - 3);
        p.drawPath(bin);
        // Slats
        p.drawLine(cx - 1, cy - 1, cx - 1, cy + 3);
        p.drawLine(cx + 1, cy - 1, cx + 1, cy + 3);
        break;
    }
    }

    p.restore();
}
}

TimelineWidget::TimelineWidget(Project* project, QWidget* parent)
    : QWidget(parent), m_project(project) {
    setMinimumHeight(m_rulerHeight + kMinTrackHeight);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    if (m_project) {
        recomputeTrackHeight();
    }
}

void TimelineWidget::setProject(Project* project) {
    m_project = project;
    if (m_project) {
        recomputeTrackHeight();
        updateGeometry();
        update();
    }
}

void TimelineWidget::recomputeTrackHeight()
{
    if (!m_project) return;
    const int count =
        std::max(1, static_cast<int>(m_project->timeline().tracks().size()));

    // m_availableHeight is the height supplied by the OUTER viewport.
    const int availableHeight =
        m_availableHeight > 0 ? m_availableHeight : height();

    const int usableHeight = std::max(0, availableHeight - m_rulerHeight);

    if (usableHeight > 0) {
        const int fitHeight = usableHeight / count;

        // Tracks fill the available viewport cleanly up and down.
        m_trackHeight = std::clamp(
            fitHeight,
            kMinTrackHeight,
            kMaxTrackHeight
        );
    } else {
        m_trackHeight = kMinTrackHeight;
    }

    const int contentHeight =
        m_rulerHeight + count * m_trackHeight;

    // Keep minimumHeight minimal so outer QScrollArea / QDockWidget can shrink freely
    setMinimumHeight(m_rulerHeight + kMinTrackHeight);

    const Ticks dur =
        std::max<Ticks>(m_project->timeline().totalDuration(),
                        secondsToTicks(30));
    const int w = timeToPixel(dur) + 200;
    const int actualH = std::max(contentHeight, availableHeight);
    resize(w, actualH);
}

void TimelineWidget::setAvailableHeight(int h) {
    const int newHeight = std::max(0, h);
    if (newHeight == m_availableHeight) return;

    m_availableHeight = newHeight;
    recomputeTrackHeight();
    updateGeometry();
    update();
}

void TimelineWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Only use the real widget height as a fallback until the outer
    // QScrollArea supplies its viewport height. Do not overwrite a viewport
    // height with our own content height, otherwise resizing can create a
    // feedback loop and clip the last track.
    if (m_availableHeight <= 0) {
        recomputeTrackHeight();
        updateGeometry();
    }

    update();
}

QRect TimelineWidget::trackControlRect(int row, TrackControl control) const {
    if (!m_project) return {};
    const int count = static_cast<int>(m_project->timeline().tracks().size());
    if (row < 0 || row >= count || control == TrackControl::None) return {};
    const int idx = static_cast<int>(control);
    const int y = m_rulerHeight + row * m_trackHeight;

    if (m_trackHeight >= 46) {
        // 2-row layout: buttons are placed on row 2, aligned under badge & track name
        constexpr int btnW = 28;
        constexpr int btnH = 20;
        constexpr int gap = 6;
        constexpr int startX = 22;
        const int btnY = y + m_trackHeight - btnH - 6;
        return QRect(startX + idx * (btnW + gap), btnY, btnW, btnH);
    } else {
        // 1-row compact layout: buttons are right-aligned within header
        constexpr int btnW = 22;
        const int btnH = std::min(m_trackHeight - 6, 20);
        constexpr int gap = 4;
        constexpr int totalW = 4 * btnW + 3 * gap;
        const int startX = m_headerWidth - totalW - 8;
        const int btnY = y + (m_trackHeight - btnH) / 2;
        return QRect(startX + idx * (btnW + gap), btnY, btnW, btnH);
    }
}

TimelineWidget::TrackControl TimelineWidget::trackControlAtPosition(const QPoint& pos, int* outRow) const {
    if (!m_project || pos.x() >= m_headerWidth || pos.y() < m_rulerHeight) return TrackControl::None;
    const int row = trackRowAtY(pos.y());
    const int count = static_cast<int>(m_project->timeline().tracks().size());
    if (row < 0 || row >= count) return TrackControl::None;
    for (int i = 0; i < 4; ++i) {
        if (trackControlRect(row, static_cast<TrackControl>(i)).adjusted(-1, -1, 1, 1).contains(pos)) {
            if (outRow) *outRow = row;
            return static_cast<TrackControl>(i);
        }
    }
    return TrackControl::None;
}

void TimelineWidget::toggleTrackControl(int row, TrackControl control) {
    if (!m_project) return;
    const int count = static_cast<int>(m_project->timeline().tracks().size());
    if (row < 0 || row >= count || control == TrackControl::None) return;
    Track& track = m_project->timeline().tracks()[trackVectorIndexForRow(row)];
    if (control == TrackControl::Delete) {
        deleteTrack(track.id);
        return;
    }
    pushUndo();
    switch (control) {
        case TrackControl::Mute:   track.muted = !track.muted; break;
        case TrackControl::Hidden: track.hidden = !track.hidden; break;
        case TrackControl::Lock:   track.locked = !track.locked; break;
        case TrackControl::Delete: break;
        case TrackControl::None:   break;
    }
    emit timelineEdited();
    update();
}

void TimelineWidget::pushUndo() {
    if (m_project) m_project->pushUndoSnapshot();
}

// Minimal-region repaint: only update the row being dragged + the snap line
// columns (old and new position). This replaces the full-widget update() call
// in the hot drag path, dramatically reducing repaint cost.
void TimelineWidget::invalidateDragRegion(int trackRow, int oldSnapX, int newSnapX) {
    // Full-width dirty rect for the affected track row (clips may shift far)
    if (trackRow >= 0) {
        const int y = m_rulerHeight + trackRow * m_trackHeight;
        update(QRect(m_headerWidth, y - 2, width() - m_headerWidth, m_trackHeight + 4));
    }
    // Old snap line column (erase it)
    if (oldSnapX >= m_headerWidth)
        update(QRect(oldSnapX - 2, m_rulerHeight, 5, height() - m_rulerHeight));
    // New snap line column (draw it)
    if (newSnapX >= m_headerWidth && newSnapX != oldSnapX)
        update(QRect(newSnapX - 2, m_rulerHeight, 5, height() - m_rulerHeight));
    // Always repaint the playhead strip in case scrub is happening
    const int phx = timeToPixel(m_playheadTime);
    update(QRect(phx - 10, 0, 20, height()));
}

// --------------------------------------------------------------------------
// Snap helpers
// --------------------------------------------------------------------------
QList<Ticks> TimelineWidget::computeSnapPoints(const QString& excludeClipId) const {
    QList<Ticks> pts;
    pts.append(0); // start of timeline
    pts.append(m_playheadTime); // playhead
    if (!m_project) return pts;
    for (const auto& track : m_project->timeline().tracks()) {
        for (const auto& clip : track.clips()) {
            if (clip.id == excludeClipId) continue;
            pts.append(clip.timelineStart);
            pts.append(clip.timelineEnd());
            for (const auto& kf : clip.transformKeyframes) {
                pts.append(clip.timelineStart + kf.time);
            }
        }
    }
    return pts;
}

Ticks TimelineWidget::snapTime(Ticks t, const QList<Ticks>& pts, bool enabled) const {
    if (!enabled) return t;
    const int curPx = timeToPixel(t);
    Ticks best = t;
    int bestDist = kSnapThresholdPx + 1;
    for (Ticks snap : pts) {
        const int d = std::abs(timeToPixel(snap) - curPx);
        if (d < bestDist) { bestDist = d; best = snap; }
    }
    return best;
}


void TimelineWidget::setPlayheadTime(Ticks t) {
    const Ticks oldT = m_playheadTime;
    m_playheadTime = std::max<Ticks>(0, t);
    if (oldT != m_playheadTime) {
        const int oldPx = timeToPixel(oldT);
        const int newPx = timeToPixel(m_playheadTime);
        update(QRect(oldPx - 12, 0, 24, height()));
        update(QRect(newPx - 12, 0, 24, height()));
        // Follow the playhead: when it moves off the visible area (during
        // playback or edge scrubbing), scroll the minimum amount to reveal it.
        ensurePlayheadVisible();
    }
}

void TimelineWidget::setZoom(double pixelsPerSecond) {
    const double clamped = std::clamp(pixelsPerSecond, kMinZoomPxPerSec, kMaxZoomPxPerSec);
    if (qFuzzyCompare(clamped, m_pixelsPerSecond)) return;
    m_pixelsPerSecond = clamped;
    recomputeTrackHeight(); // resize the widget so the scrollbar range follows the zoom
    updateGeometry();
    update();
    emit zoomChanged(m_pixelsPerSecond);
}

QScrollArea* TimelineWidget::outerScrollArea() const {
    // After QScrollArea::setWidget() the timeline is reparented to the
    // scroll area's viewport, so walk up until we hit the QScrollArea.
    for (QWidget* w = parentWidget(); w; w = w->parentWidget()) {
        if (auto* sa = qobject_cast<QScrollArea*>(w)) return sa;
    }
    return nullptr;
}

void TimelineWidget::zoomAt(Ticks anchorTime, double factor, int cursorWidgetX) {
    const double newZoom = std::clamp(m_pixelsPerSecond * factor, kMinZoomPxPerSec, kMaxZoomPxPerSec);
    if (qFuzzyCompare(newZoom, m_pixelsPerSecond)) return;

    QScrollArea* sa = outerScrollArea();
    const int oldScroll = sa ? sa->horizontalScrollBar()->value() : 0;

    m_pixelsPerSecond = newZoom;
    recomputeTrackHeight(); // resize the widget so the scrollbar range follows the zoom
    updateGeometry();

    if (sa) {
        QScrollBar* hbar = sa->horizontalScrollBar();
        if (cursorWidgetX >= 0) {
            // Keep the timeline *time* under the cursor fixed on screen:
            // newScroll = newWidgetX - cursorWidgetX + oldScroll.
            hbar->setValue(timeToPixel(anchorTime) - cursorWidgetX + oldScroll);
        } else {
            hbar->setValue(oldScroll); // left-anchored (menu zoom)
        }
    }
    update();
    emit zoomChanged(m_pixelsPerSecond);
}

void TimelineWidget::zoomBy(double factor) {
    QScrollArea* sa = outerScrollArea();
    if (!sa) {
        zoomAt(m_playheadTime, factor, -1);
        return;
    }
    const int scroll = sa->horizontalScrollBar()->value();
    const int vw = sa->viewport()->width();
    const int centerX = scroll + std::max(0, vw / 2);
    const Ticks centerTime = pixelToTime(centerX);
    zoomAt(centerTime, factor, centerX);
}

void TimelineWidget::zoomToFit() {
    if (!m_project) return;
    QScrollArea* sa = outerScrollArea();
    const int viewW = sa ? sa->viewport()->width() : width();
    const int usable = std::max(100, viewW - m_headerWidth);
    const Ticks dur = std::max<Ticks>(m_project->timeline().totalDuration(), secondsToTicks(1));
    const double seconds = std::max(0.001, ticksToSeconds(dur));
    const double newZoom = std::clamp(static_cast<double>(usable) / seconds,
                                      kMinZoomPxPerSec, kMaxZoomPxPerSec);
    m_pixelsPerSecond = newZoom;
    recomputeTrackHeight();
    updateGeometry();
    if (sa) sa->horizontalScrollBar()->setValue(0);
    update();
    emit zoomChanged(m_pixelsPerSecond);
}

void TimelineWidget::ensurePlayheadVisible() {
    QScrollArea* sa = outerScrollArea();
    if (!sa) return;
    QScrollBar* hbar = sa->horizontalScrollBar();
    const int scroll = hbar->value();
    const int vw = sa->viewport()->width();
    const int px = timeToPixel(m_playheadTime);
    constexpr int margin = 12;
    if (px < scroll + margin) {
        hbar->setValue(std::max(0, px - margin));
    } else if (px > scroll + vw - margin) {
        hbar->setValue(px - vw + margin);
    }
}

Ticks TimelineWidget::pixelToTime(int x) const {
    return secondsToTicks(std::max(0, x - m_headerWidth) / m_pixelsPerSecond);
}
int TimelineWidget::timeToPixel(Ticks t) const {
    return m_headerWidth + static_cast<int>(ticksToSeconds(t) * m_pixelsPerSecond);
}

int TimelineWidget::trackRowAtY(int y) const {
    if (!m_project || y < m_rulerHeight) return -1;
    const int row = (y - m_rulerHeight) / m_trackHeight;
    const int count = static_cast<int>(m_project->timeline().tracks().size());
    if (row < 0 || row >= count) return -1;
    return row;
}

int TimelineWidget::trackVectorIndexForRow(int row) const {
    if (!m_project) return -1;
    const int count = static_cast<int>(m_project->timeline().tracks().size());
    return count - 1 - row;
}

QRect TimelineWidget::clipRect(int trackVectorIndex, const Clip& clip) const {
    if (!m_project) return QRect();
    const int count = static_cast<int>(m_project->timeline().tracks().size());
    const int row = count - 1 - trackVectorIndex;
    const int y = m_rulerHeight + row * m_trackHeight + 2;
    const int x = timeToPixel(clip.timelineStart);
    const int w = std::max(2, timeToPixel(clip.timelineEnd()) - x);
    return QRect(x, y, w, m_trackHeight - 4);
}

QSize TimelineWidget::sizeHint() const {
    if (!m_project) return QSize(200, m_rulerHeight + kMinTrackHeight);
    const Ticks dur =
        std::max<Ticks>(m_project->timeline().totalDuration(),
                        secondsToTicks(30));

    const int w = timeToPixel(dur) + 200;
    const int trackCount =
        std::max(1, static_cast<int>(m_project->timeline().tracks().size()));

    const int contentHeight =
        m_rulerHeight + trackCount * m_trackHeight;

    return QSize(w, contentHeight);
}

QString TimelineWidget::formatDurationShort(Ticks t) const {
    const double sec = ticksToSeconds(t);
    return QString::number(sec, 'f', 2) + "s";
}

void TimelineWidget::refresh()
{
    if (!m_project) return;
    recomputeTrackHeight();
    updateGeometry();
    update();
}

void TimelineWidget::setCutToolActive(bool active) {
    m_cutToolActive = active;
    setCursor(active ? Qt::SplitHCursor : Qt::ArrowCursor);
}

void TimelineWidget::hitTest(const QPoint& pos, QString* outTrackId, QString* outClipId, DragMode* outMode) const {
    *outTrackId = QString();
    *outClipId = QString();
    *outMode = DragMode::None;
    if (!m_project) return;

    // 1. Check if clicking near the playhead shield in ruler OR needle across the timeline
    const int phX = timeToPixel(m_playheadTime);
    if (pos.y() < m_rulerHeight && std::abs(pos.x() - phX) <= 12) {
        *outMode = DragMode::ScrubPlayhead;
        return;
    }
    if (std::abs(pos.x() - phX) <= 6 && pos.x() >= m_headerWidth - 6) {
        *outMode = DragMode::ScrubPlayhead;
        return;
    }

    // 2. Check click anywhere in the ruler (to the right of header column)
    if (pos.y() < m_rulerHeight) {
        if (pos.x() >= m_headerWidth - 6) *outMode = DragMode::ScrubPlayhead;
        return;
    }

    const int row = trackRowAtY(pos.y());
    if (row < 0) {
        if (pos.x() >= m_headerWidth - 6) *outMode = DragMode::ScrubPlayhead;
        return;
    }
    const int idx = trackVectorIndexForRow(row);
    const auto& tracks = m_project->timeline().tracks();
    if (idx < 0 || idx >= static_cast<int>(tracks.size())) return;
    const Track& track = tracks[idx];

    if (pos.x() < m_headerWidth) {
        // The right strip of the layers column holds the per-track controls
        // (mute / hide / lock / delete); the rest of the column reorders the track.
        if (trackControlAtPosition(pos, nullptr) != TrackControl::None) {
            *outTrackId = track.id;
            *outMode = DragMode::ToggleTrackControl;
            return;
        }
        // Left "layers" column: grabbing here reorders the whole track (layer),
        // not an individual clip.
        *outTrackId = track.id;
        *outMode = DragMode::ReorderTrack;
        return;
    }

    for (const auto& clip : track.clips()) {
        const QRect r = clipRect(idx, clip);
        if (r.contains(pos)) {
            *outTrackId = track.id;
            *outClipId = clip.id;
            // Fade-in handle: left portion of the clip (first 16px), top half
            if (pos.x() - r.left() <= kEdgeGrabPx) {
                *outMode = DragMode::TrimLeft;
            } else if (r.right() - pos.x() <= kEdgeGrabPx) {
                *outMode = DragMode::TrimRight;
            } else {
                // Check fade handles: small triangles at top corners of clip
                const int fadeHandleW = std::min(r.width() / 3, 20);
                const int handleH = std::min(m_trackHeight / 2, 14);
                const QRect fadeInHandle(r.left() + kEdgeGrabPx, r.top(), fadeHandleW, handleH);
                const QRect fadeOutHandle(r.right() - kEdgeGrabPx - fadeHandleW, r.top(), fadeHandleW, handleH);
                if (clip.fadeInDuration > 0 && fadeInHandle.contains(pos)) {
                    *outMode = DragMode::FadeIn;
                } else if (clip.fadeOutDuration > 0 && fadeOutHandle.contains(pos)) {
                    *outMode = DragMode::FadeOut;
                } else {
                    *outMode = DragMode::MoveClip;
                }
            }
            return;
        }
    }

    // 3. Clicked in empty track area: scrub playhead!
    *outMode = DragMode::ScrubPlayhead;
}

bool TimelineWidget::transitionMarkerAt(const QPoint& pos, QString* outTrackId,
                                         QString* outPrevClipId, QString* outCurClipId) const {
    *outTrackId = QString();
    *outPrevClipId = QString();
    *outCurClipId = QString();
    if (pos.y() < m_rulerHeight || pos.x() < m_headerWidth) return false;

    const int row = trackRowAtY(pos.y());
    if (row < 0) return false;
    const int idx = trackVectorIndexForRow(row);
    const auto& tracks = m_project->timeline().tracks();
    if (idx < 0 || idx >= static_cast<int>(tracks.size())) return false;
    const Track& track = tracks[idx];
    if (track.type != TrackType::Visual) return false;

    const int y = m_rulerHeight + row * m_trackHeight;
    const int my = y + m_trackHeight / 2;
    const auto& clips = track.clips();
    // Same eligibility/marker-position rules as the drawing pass in
    // paintEvent — keep these two in sync if either changes.
    for (size_t i = 1; i < clips.size(); ++i) {
        const Clip& prevClip = clips[i - 1];
        const Clip& curClip = clips[i];
        if (prevClip.type == ClipType::Text || curClip.type == ClipType::Text) continue;
        const bool touching = curClip.timelineStart == prevClip.timelineEnd();
        const bool hasTransition = curClip.transitionInDuration > 0 &&
            curClip.timelineStart == prevClip.timelineEnd() - curClip.transitionInDuration;
        if (!touching && !hasTransition) continue;

        const Ticks markerTime = hasTransition
            ? (curClip.timelineStart + prevClip.timelineEnd()) / 2
            : prevClip.timelineEnd();
        const int mx = timeToPixel(markerTime);
        if ((pos - QPoint(mx, my)).manhattanLength() > 9) continue;

        *outTrackId = track.id;
        *outPrevClipId = prevClip.id;
        *outCurClipId = curClip.id;
        return true;
    }
    return false;
}

void TimelineWidget::toggleTransitionAt(Track* track, Clip* prevClip, Clip* curClip) {
    if (!track || !prevClip || !curClip || track->locked) return;
    pushUndo();
    if (curClip->transitionInDuration > 0) {
        // Remove: push the incoming clip back so it just touches the
        // outgoing one again, like it did before the transition existed.
        curClip->timelineStart += curClip->transitionInDuration;
        curClip->transitionInDuration = 0;
    } else {
        // Create: a default 0.5s crossfade, clamped so it never eats more
        // than half of either clip's own on-screen duration (keeps a
        // sliver of each clip visible on its own either side of the fade).
        const Ticks maxByPrev = prevClip->timelineDuration() / 2;
        const Ticks maxByCur = curClip->timelineDuration() / 2;
        const Ticks duration = std::clamp<Ticks>(kDefaultTransitionTicks, 1, std::max<Ticks>(1, std::min(maxByPrev, maxByCur)));
        curClip->transitionInDuration = duration;
        curClip->timelineStart -= duration;
    }
    emit timelineEdited();
    update();
}

void TimelineWidget::buildTransitionMenu(QMenu* menu, Track* track,
                                         const QString& prevClipId, const QString& incomingClipId) {
    if (!menu || !track || track->locked) return;
    Clip* prev = track->findClip(prevClipId);
    Clip* cur = track->findClip(incomingClipId);
    if (!cur) return;

    const TransitionType types[4] = { TransitionType::Dissolve, TransitionType::Wipe,
                                      TransitionType::Slide, TransitionType::DipToColor };
    const QString typeNames[4] = { tr("Cross Dissolve"), tr("Wipe"), tr("Slide"), tr("Dip to Color") };
    const QString dirNames[4] = { tr("Left → Right"), tr("Right → Left"),
                                  tr("Top → Bottom"), tr("Bottom → Top") };

    menu->addSection(tr("Transition"));

    if (cur->transitionInDuration <= 0) {
        // No transition yet: offer to add one of the chosen type.
        for (int i = 0; i < 4; ++i) {
            QAction* a = menu->addAction(tr("Add %1").arg(typeNames[i]));
            connect(a, &QAction::triggered, this, [this, track, prev, cur, t = types[i]]() {
                const Ticks maxByPrev = prev ? prev->timelineDuration() / 2 : kDefaultTransitionTicks;
                const Ticks maxByCur = cur->timelineDuration() / 2;
                const Ticks duration = std::clamp<Ticks>(kDefaultTransitionTicks, 1,
                    std::max<Ticks>(1, std::min(maxByPrev, maxByCur)));
                pushUndo();
                cur->transitionInDuration = duration;
                cur->transitionType = t;
                cur->timelineStart -= duration;
                emit timelineEdited();
                update();
            });
        }
        return;
    }

    QMenu* typeMenu = menu->addMenu(tr("Type"));
    for (int i = 0; i < 4; ++i) {
        QAction* a = typeMenu->addAction(typeNames[i]);
        a->setCheckable(true);
        a->setChecked(cur->transitionType == types[i]);
        connect(a, &QAction::triggered, this, [this, cur, t = types[i]]() {
            if (cur->transitionType == t) return;
            pushUndo();
            cur->transitionType = t;
            emit timelineEdited();
            update();
        });
    }

    QMenu* dirMenu = menu->addMenu(tr("Direction"));
    for (int d = 0; d < 4; ++d) {
        QAction* a = dirMenu->addAction(dirNames[d]);
        a->setCheckable(true);
        a->setChecked(cur->transitionDirection == d);
        connect(a, &QAction::triggered, this, [this, cur, d]() {
            if (cur->transitionDirection == d) return;
            pushUndo();
            cur->transitionDirection = d;
            emit timelineEdited();
            update();
        });
    }

    if (cur->transitionType == TransitionType::DipToColor) {
        QAction* colorAction = menu->addAction(tr("Dip Color…"));
        connect(colorAction, &QAction::triggered, this, [this, cur]() {
            const QColor chosen = QColorDialog::getColor(
                cur->transitionColor.isValid() ? cur->transitionColor : QColor(0, 0, 0),
                this, tr("Dip to Color"));
            if (!chosen.isValid()) return;
            pushUndo();
            cur->transitionColor = chosen;
            emit timelineEdited();
            update();
        });
    }

    QMenu* durMenu = menu->addMenu(tr("Duration"));
    const Ticks options[3] = { 250'000, 500'000, 1'000'000 }; // 0.25s / 0.5s / 1s
    const QString optionNames[3] = { tr("0.25 s"), tr("0.5 s"), tr("1.0 s") };
    for (int i = 0; i < 3; ++i) {
        QAction* a = durMenu->addAction(optionNames[i]);
        a->setCheckable(true);
        a->setChecked(cur->transitionInDuration == options[i]);
        connect(a, &QAction::triggered, this, [this, cur, newDur = options[i]]() {
            if (cur->transitionInDuration == newDur) return;
            pushUndo();
            cur->timelineStart += cur->transitionInDuration - newDur;
            cur->transitionInDuration = newDur;
            emit timelineEdited();
            update();
        });
    }

    menu->addSeparator();
    QAction* remove = menu->addAction(tr("Remove Transition"));
    connect(remove, &QAction::triggered, this, [this, cur]() {
        pushUndo();
        cur->timelineStart += cur->transitionInDuration;
        cur->transitionInDuration = 0;
        emit timelineEdited();
        update();
    });
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    const QRect dirty = event ? event->rect() : rect();
    p.fillRect(dirty, QColor(22, 23, 30));
    if (!m_project) return;

    const auto& tracks = m_project->timeline().tracks();
    const int trackCount = static_cast<int>(tracks.size());

    // --- ruler (top bar) ---
    if (dirty.top() < m_rulerHeight) {
        // Left corner box above layers column
        if (dirty.left() < m_headerWidth) {
            const QRect cornerRect(0, 0, m_headerWidth, m_rulerHeight);
            p.fillRect(cornerRect, QColor(20, 21, 28));
            p.setPen(QColor(42, 44, 56));
            p.drawLine(0, m_rulerHeight - 1, m_headerWidth, m_rulerHeight - 1);
            p.drawLine(m_headerWidth - 1, 0, m_headerWidth - 1, m_rulerHeight);

            // Section title: TIMELINE
            p.setPen(QColor(160, 165, 180));
            QFont titleFont = p.font();
            titleFont.setPointSize(8);
            titleFont.setBold(true);
            p.setFont(titleFont);
            p.drawText(QRect(12, 0, 100, m_rulerHeight), Qt::AlignVCenter | Qt::AlignLeft, "TIMELINE");

            // Subtle track count badge
            p.save();
            p.setRenderHint(QPainter::Antialiasing, true);
            const QRect badgeRect(m_headerWidth - 66, (m_rulerHeight - 18) / 2, 56, 18);
            p.setBrush(QColor(255, 255, 255, 12));
            p.setPen(QPen(QColor(255, 255, 255, 25), 1.0));
            p.drawRoundedRect(badgeRect, 4.0, 4.0);
            QFont badgeFont = p.font();
            badgeFont.setPointSize(7);
            badgeFont.setBold(false);
            p.setFont(badgeFont);
            p.setPen(QColor(148, 163, 184));
            p.drawText(badgeRect, Qt::AlignCenter, QString("%1 %2").arg(trackCount).arg(tr("Lớp")));
            p.restore();
        }

        // Ruler surface from m_headerWidth
        const int rL = std::max(m_headerWidth, dirty.left());
        const int rW = std::min(width(), dirty.right() + 1);
        if (rW > rL) {
            QLinearGradient rGrad(0, 0, 0, m_rulerHeight);
            rGrad.setColorAt(0.0, QColor(26, 28, 38));
            rGrad.setColorAt(1.0, QColor(18, 19, 26));
            p.fillRect(QRect(rL, 0, rW - rL, m_rulerHeight), rGrad);
            p.setPen(QColor(42, 44, 56));
            p.drawLine(rL, m_rulerHeight - 1, rW, m_rulerHeight - 1);
        }

        // Calculate tick intervals dynamically based on zoom (pixelsPerSecond)
        // Ensure major tick labels (which need ~60px) have plenty of breathing room (at least ~120px)
        double majorSec = 5.0;
        int subTickCount = 5;
        if (m_pixelsPerSecond >= 180.0) {
            majorSec = 1.0;
            subTickCount = 4;
        } else if (m_pixelsPerSecond >= 80.0) {
            majorSec = 2.0;
            subTickCount = 4;
        } else if (m_pixelsPerSecond >= 30.0) {
            majorSec = 5.0;
            subTickCount = 5;
        } else if (m_pixelsPerSecond >= 12.0) {
            majorSec = 10.0;
            subTickCount = 5;
        } else if (m_pixelsPerSecond >= 4.0) {
            majorSec = 30.0;
            subTickCount = 6;
        } else {
            majorSec = 60.0;
            subTickCount = 6;
        }

        const double majorPx = majorSec * m_pixelsPerSecond;
        const int startIdx = std::max(0, static_cast<int>((dirty.left() - m_headerWidth) / majorPx));
        const int endIdx = static_cast<int>((dirty.right() - m_headerWidth) / majorPx) + 2;

        p.setFont(QFont("monospace", 8, QFont::DemiBold));
        for (int i = startIdx; i <= endIdx; ++i) {
            const int majorX = m_headerWidth + static_cast<int>(i * majorPx);
            if (majorX < m_headerWidth) continue;
            if (majorX > width()) break;

            // Major tick line
            p.setPen(QPen(QColor(100, 116, 139), 1.2));
            p.drawLine(majorX, m_rulerHeight - 11, majorX, m_rulerHeight - 1);

            // Timecode label
            const Ticks t = pixelToTime(majorX);
            p.setPen(QColor(203, 213, 225));
            p.drawText(majorX + 4, m_rulerHeight - 13, formatTimecode(t).left(8));

            // Sub-ticks
            for (int s = 1; s < subTickCount; ++s) {
                const int subX = majorX + static_cast<int>((s * majorPx) / subTickCount);
                if (subX >= m_headerWidth && subX <= width()) {
                    const bool isMid = (subTickCount % 2 == 0 && s == subTickCount / 2);
                    p.setPen(QPen(isMid ? QColor(71, 85, 105) : QColor(51, 65, 85), 1.0));
                    p.drawLine(subX, isMid ? (m_rulerHeight - 7) : (m_rulerHeight - 4), subX, m_rulerHeight - 1);
                }
            }
        }
    }

    // --- track rows ---
    for (int row = 0; row < trackCount; ++row) {
        const int idx = trackVectorIndexForRow(row);
        const Track& track = tracks[idx];
        const int y = m_rulerHeight + row * m_trackHeight;
        if (y + m_trackHeight < dirty.top() || y > dirty.bottom()) continue;

        const bool beingReordered = (m_dragMode == DragMode::ReorderTrack && track.id == m_dragTrackId);

        const int trackDrawLeft = std::max(m_headerWidth, dirty.left());
        const int trackDrawRight = std::min(width(), dirty.right() + 1);
        if (trackDrawRight > trackDrawLeft) {
            p.fillRect(trackDrawLeft, y, trackDrawRight - trackDrawLeft, m_trackHeight,
                       (row % 2 == 0) ? QColor(24, 25, 33) : QColor(20, 21, 28));
            p.setPen(QColor(36, 38, 48));
            p.drawLine(trackDrawLeft, y + m_trackHeight - 1, trackDrawRight, y + m_trackHeight - 1);
        }

        // Left Header Column (Layers)
        if (dirty.left() < m_headerWidth) {
            p.fillRect(0, y, m_headerWidth, m_trackHeight, beingReordered ? QColor(49, 53, 74) : QColor(22, 23, 30));
            p.setPen(QColor(36, 38, 48));
            p.drawLine(0, y + m_trackHeight - 1, m_headerWidth, y + m_trackHeight - 1);
            p.drawLine(m_headerWidth - 1, y, m_headerWidth - 1, y + m_trackHeight);

            // Left Accent Strip (4px wide)
            const QColor accentColor = (track.type == TrackType::Visual)
                ? QColor(59, 130, 246)
                : QColor(16, 185, 129);
            p.fillRect(0, y, 4, m_trackHeight, accentColor);

            p.save();
            p.setRenderHint(QPainter::Antialiasing, true);

            if (m_trackHeight >= 46) {
                // --- 2-ROW HEADER LAYOUT ---
                // Row 1 (Top): Grip + Badge + Name
                p.setPen(QColor(75, 85, 99));
                QFont gripFont = p.font();
                gripFont.setPointSize(10);
                p.setFont(gripFont);
                p.drawText(QRect(7, y + 4, 12, 20), Qt::AlignCenter, "⠿");

                // Badge pill: [V1] or [A1]
                const QString typeChar = (track.type == TrackType::Visual) ? "V" : "A";
                int typeIdx = 1;
                for (int ti = 0; ti < idx; ++ti) {
                    if (tracks[ti].type == track.type) typeIdx++;
                }
                const QString badgeText = QString("%1%2").arg(typeChar).arg(typeIdx);
                const QRect badgeRect(22, y + 5, 32, 18);
                const QColor badgeBg = (track.type == TrackType::Visual) ? QColor(59, 130, 246, 45) : QColor(16, 185, 129, 45);
                const QColor badgeBorder = (track.type == TrackType::Visual) ? QColor(59, 130, 246, 110) : QColor(16, 185, 129, 110);
                const QColor badgeFg = (track.type == TrackType::Visual) ? QColor(147, 197, 253) : QColor(110, 231, 183);
                p.setBrush(badgeBg);
                p.setPen(QPen(badgeBorder, 1.0));
                p.drawRoundedRect(badgeRect, 4.0, 4.0);
                QFont bFont = p.font();
                bFont.setPointSize(8);
                bFont.setBold(true);
                p.setFont(bFont);
                p.setPen(badgeFg);
                p.drawText(badgeRect, Qt::AlignCenter, badgeText);

                // Full Track Name
                const int nameX = 60;
                const int nameW = m_headerWidth - nameX - 8;
                QFont nameFont = p.font();
                nameFont.setPointSize(9);
                nameFont.setBold(false);
                p.setFont(nameFont);
                p.setPen((track.locked || track.hidden) ? QColor(148, 163, 184) : QColor(241, 245, 249));
                p.drawText(QRect(nameX, y + 4, nameW, 20), Qt::AlignVCenter | Qt::AlignLeft,
                           p.fontMetrics().elidedText(track.name, Qt::ElideRight, nameW));

                // Row 2 (Bottom): 4 Button Cards
                for (int c = 0; c < 4; ++c) {
                    const auto ctrl = static_cast<TrackControl>(c);
                    const QRect r = trackControlRect(row, ctrl);
                    const bool hovered = (m_hoverTrackRow == row && m_hoverControl == ctrl);
                    bool active = false;
                    if (ctrl == TrackControl::Mute) active = track.muted;
                    else if (ctrl == TrackControl::Hidden) active = track.hidden;
                    else if (ctrl == TrackControl::Lock) active = track.locked;
                    drawControlButton(p, r, c, active, hovered);
                }
            } else {
                // --- 1-ROW COMPACT HEADER LAYOUT ---
                p.setPen(QColor(75, 85, 99));
                QFont gripFont = p.font();
                gripFont.setPointSize(9);
                p.setFont(gripFont);
                p.drawText(QRect(6, y, 10, m_trackHeight), Qt::AlignCenter, "⠿");

                const QString typeChar = (track.type == TrackType::Visual) ? "V" : "A";
                int typeIdx = 1;
                for (int ti = 0; ti < idx; ++ti) {
                    if (tracks[ti].type == track.type) typeIdx++;
                }
                const QString badgeText = QString("%1%2").arg(typeChar).arg(typeIdx);
                const QRect badgeRect(18, y + (m_trackHeight - 16) / 2, 28, 16);
                const QColor badgeBg = (track.type == TrackType::Visual) ? QColor(59, 130, 246, 45) : QColor(16, 185, 129, 45);
                const QColor badgeBorder = (track.type == TrackType::Visual) ? QColor(59, 130, 246, 110) : QColor(16, 185, 129, 110);
                const QColor badgeFg = (track.type == TrackType::Visual) ? QColor(147, 197, 253) : QColor(110, 231, 183);
                p.setBrush(badgeBg);
                p.setPen(QPen(badgeBorder, 1.0));
                p.drawRoundedRect(badgeRect, 3.0, 3.0);
                QFont bFont = p.font();
                bFont.setPointSize(7);
                bFont.setBold(true);
                p.setFont(bFont);
                p.setPen(badgeFg);
                p.drawText(badgeRect, Qt::AlignCenter, badgeText);

                for (int c = 0; c < 4; ++c) {
                    const auto ctrl = static_cast<TrackControl>(c);
                    const QRect r = trackControlRect(row, ctrl);
                    const bool hovered = (m_hoverTrackRow == row && m_hoverControl == ctrl);
                    bool active = false;
                    if (ctrl == TrackControl::Mute) active = track.muted;
                    else if (ctrl == TrackControl::Hidden) active = track.hidden;
                    else if (ctrl == TrackControl::Lock) active = track.locked;
                    drawControlButton(p, r, c, active, hovered);
                }

                const int nameX = 50;
                const int rightBound = trackControlRect(row, TrackControl::Mute).left() - 4;
                const int nameW = std::max(20, rightBound - nameX);
                QFont nameFont = p.font();
                nameFont.setPointSize(8);
                p.setFont(nameFont);
                p.setPen((track.locked || track.hidden) ? QColor(148, 163, 184) : QColor(241, 245, 249));
                p.drawText(QRect(nameX, y, nameW, m_trackHeight), Qt::AlignVCenter | Qt::AlignLeft,
                           p.fontMetrics().elidedText(track.name, Qt::ElideRight, nameW));
            }

            p.restore();
        }


        // Clips
        for (const auto& clip : track.clips()) {
            const QRect r = clipRect(idx, clip);
            if (!r.intersects(dirty)) continue;

            const bool selected = (clip.id == m_selectedClipId);
            p.save();
            p.setRenderHint(QPainter::Antialiasing, true);

            QLinearGradient clipGrad(0, r.top(), 0, r.bottom());
            if (clip.type == ClipType::Audio) {
                clipGrad.setColorAt(0.0, QColor(16, 145, 105));
                clipGrad.setColorAt(1.0, QColor(10, 100, 72));
            } else if (clip.type == ClipType::Text) {
                clipGrad.setColorAt(0.0, QColor(139, 92, 246));
                clipGrad.setColorAt(1.0, QColor(109, 40, 217));
            } else {
                clipGrad.setColorAt(0.0, QColor(49, 115, 222));
                clipGrad.setColorAt(1.0, QColor(29, 78, 175));
            }

            if (track.locked || track.hidden) {
                p.setOpacity(0.6);
            }

            p.setBrush(clipGrad);
            if (selected) {
                p.setPen(QPen(QColor(251, 191, 36), 1.8));
            } else {
                p.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
            }
            p.drawRoundedRect(r.adjusted(0, 0, -1, -1), 4.0, 4.0);

            if (r.width() > 6 && r.height() > 8) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(255, 255, 255, 45));
                p.drawRoundedRect(QRect(r.left() + 2, r.top() + 1, r.width() - 4, 2), 1.0, 1.0);
            }

            // Audio waveform
            if (clip.type == ClipType::Audio) {
                const auto asset = m_project->findAsset(clip.assetId);
                if (asset && !asset->waveformPeaks.empty() && r.width() > 1) {
                    const auto& peaks = asset->waveformPeaks;
                    const double bucketsPerSec = MediaAsset::kWaveformBucketsPerSecond;
                    const double srcInSec = ticksToSeconds(clip.sourceIn);
                    const double srcOutSec = ticksToSeconds(clip.sourceOut);
                    const int midY = r.center().y();
                    const int halfH = std::max(2, r.height() / 2 - 4);
                    p.setPen(QPen(QColor(210, 245, 230, 220), 1.0));
                    const int xStart = std::max(r.left() + 2, dirty.left());
                    const int xEnd = std::min(r.right() - 2, dirty.right());
                    for (int x = xStart; x <= xEnd; ++x) {
                        const double f0 = double(x - r.left()) / double(r.width());
                        const double f1 = double(x - r.left() + 1) / double(r.width());
                        const double s0 = srcInSec + f0 * (srcOutSec - srcInSec);
                        const double s1 = srcInSec + f1 * (srcOutSec - srcInSec);
                        auto b0 = static_cast<size_t>(std::max(0.0, s0) * bucketsPerSec);
                        auto b1 = std::max(b0 + 1, static_cast<size_t>(std::max(0.0, s1) * bucketsPerSec));
                        const size_t stride = std::max<size_t>(1, (b1 - b0) / 64);
                        float peak = 0.0f;
                        for (size_t b = b0; b < b1 && b < peaks.size(); b += stride) {
                            peak = std::max(peak, peaks[b]);
                        }
                        const int barH = static_cast<int>(peak * halfH);
                        if (barH > 0) p.drawLine(x, midY - barH, x, midY + barH);
                    }
                }
            }

            // Fade-in / Fade-out
            if (clip.fadeInDuration > 0) {
                const int fadeInW = std::min(r.width(), timeToPixel(clip.timelineStart + clip.fadeInDuration) - r.left());
                if (fadeInW > 0) {
                    const QRect fRect(r.left(), r.top(), fadeInW, r.height());
                    if (fRect.intersects(dirty)) {
                        QLinearGradient g(r.left(), 0, r.left() + fadeInW, 0);
                        g.setColorAt(0, QColor(0, 0, 0, 180));
                        g.setColorAt(1, QColor(0, 0, 0, 0));
                        p.fillRect(fRect, g);
                        const int hx = r.left() + fadeInW;
                        const int hy = r.top();
                        p.setBrush(QColor(255, 220, 80, 220));
                        p.setPen(Qt::NoPen);
                        p.drawPolygon(QPolygon({QPoint(r.left(), hy), QPoint(hx, hy), QPoint(r.left(), hy + 10)}));
                    }
                }
            }
            if (clip.fadeOutDuration > 0) {
                const int fadeOutEnd = r.right();
                const int fadeOutW = std::min(r.width(), fadeOutEnd - timeToPixel(clip.timelineEnd() - clip.fadeOutDuration));
                if (fadeOutW > 0) {
                    const QRect fRect(fadeOutEnd - fadeOutW, r.top(), fadeOutW, r.height());
                    if (fRect.intersects(dirty)) {
                        QLinearGradient g(fadeOutEnd - fadeOutW, 0, fadeOutEnd, 0);
                        g.setColorAt(0, QColor(0, 0, 0, 0));
                        g.setColorAt(1, QColor(0, 0, 0, 180));
                        p.fillRect(fRect, g);
                        const int hx = fadeOutEnd - fadeOutW;
                        const int hy = r.top();
                        p.setBrush(QColor(255, 220, 80, 220));
                        p.setPen(Qt::NoPen);
                        p.drawPolygon(QPolygon({QPoint(hx, hy), QPoint(fadeOutEnd, hy), QPoint(fadeOutEnd, hy + 10)}));
                    }
                }
            }

            QString label = clip.displayLabel;
            if (label.isEmpty()) {
                const auto asset = m_project->findAsset(clip.assetId);
                if (asset && asset->kind != MediaKind::Unknown) {
                    label = asset->displayName;
                } else {
                    label = tr("(media bị mất)");
                }
            }
            const QString fullLabel = label;

            QFont clipFont = p.font();
            clipFont.setPointSize(8);
            clipFont.setBold(true);
            p.setFont(clipFont);
            p.setPen(QColor(0, 0, 0, 160));
            p.drawText(r.adjusted(7, 1, -5, 1), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(fullLabel, Qt::ElideRight, r.width() - 12));
            p.setPen(Qt::white);
            p.drawText(r.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(fullLabel, Qt::ElideRight, r.width() - 12));

            // Keyframes
            if (clip.hasTransformKeyframes()) {
                const int kfSize = 5;
                const int kfY = r.bottom() - 6;
                for (const auto& kf : clip.transformKeyframes) {
                    const Ticks absTime = clip.timelineStart + kf.time;
                    const int kfX = timeToPixel(absTime);
                    if (kfX < r.left() || kfX > r.right()) continue;
                    if (kfX < dirty.left() - kfSize || kfX > dirty.right() + kfSize) continue;

                    QPolygon diamond;
                    diamond << QPoint(kfX, kfY - kfSize)
                            << QPoint(kfX + kfSize, kfY)
                            << QPoint(kfX, kfY + kfSize)
                            << QPoint(kfX - kfSize, kfY);

                    const bool isCurrent = std::abs(timeToPixel(m_playheadTime) - kfX) <= 3;
                    if (isCurrent) {
                        p.setPen(QPen(QColor(255, 255, 255), 1.5));
                        p.setBrush(QColor(255, 220, 50));
                    } else {
                        p.setPen(QPen(QColor(20, 30, 40, 200), 1.0));
                        p.setBrush(QColor(251, 191, 36, 230));
                    }
                    p.drawPolygon(diamond);
                }
            }

            p.restore();
        }

        // Crossfade transition markers
        if (track.type == TrackType::Visual) {
            const auto& clips = track.clips();
            for (size_t i = 1; i < clips.size(); ++i) {
                const Clip& prevClip = clips[i - 1];
                const Clip& curClip = clips[i];
                if (prevClip.type == ClipType::Text || curClip.type == ClipType::Text) continue;
                const bool touching = curClip.timelineStart == prevClip.timelineEnd();
                const bool hasTransition = curClip.transitionInDuration > 0 &&
                    curClip.timelineStart == prevClip.timelineEnd() - curClip.transitionInDuration;
                if (!touching && !hasTransition) continue;

                Ticks markerTime;
                if (hasTransition) {
                    const int xStart = timeToPixel(curClip.timelineStart);
                    const int xEnd = timeToPixel(prevClip.timelineEnd());
                    if (xEnd >= dirty.left() && xStart <= dirty.right()) {
                        p.fillRect(QRect(xStart, y + 2, std::max(2, xEnd - xStart), m_trackHeight - 4),
                                   QColor(255, 255, 255, 40));
                    }
                    markerTime = (curClip.timelineStart + prevClip.timelineEnd()) / 2;
                } else {
                    markerTime = prevClip.timelineEnd();
                }
                const int mx = timeToPixel(markerTime);
                if (mx < dirty.left() - 8 || mx > dirty.right() + 8) continue;
                const int my = y + m_trackHeight / 2;
                QColor markerColor(255, 190, 80); // dissolve: amber
                if (hasTransition) {
                    switch (curClip.transitionType) {
                        case TransitionType::Dissolve:   markerColor = QColor(255, 190, 80); break;
                        case TransitionType::Wipe:       markerColor = QColor(90, 190, 255); break;
                        case TransitionType::Slide:      markerColor = QColor(120, 220, 140); break;
                        case TransitionType::DipToColor: markerColor = QColor(215, 130, 245); break;
                    }
                }
                p.setPen(QColor(20, 20, 24));
                p.setBrush(hasTransition ? markerColor : QColor(150, 150, 158, 160));
                p.drawEllipse(QPoint(mx, my), 7, 7);
                p.setPen(Qt::white);
                p.drawText(QRect(mx - 8, my - 8, 16, 16), Qt::AlignCenter, hasTransition ? tr("×") : tr("+"));
            }
        }
    }

    // Snap line
    if (m_lastSnapTarget >= 0) {
        const int sx = timeToPixel(m_lastSnapTarget);
        if (sx >= dirty.left() - 2 && sx <= dirty.right() + 2) {
            p.setPen(QPen(QColor(251, 191, 36, 220), 1.2, Qt::DashLine));
            p.drawLine(sx, m_rulerHeight, sx, height());
        }
    }

    // Playhead
    const int px = timeToPixel(m_playheadTime);
    if (px >= dirty.left() - 10 && px <= dirty.right() + 10) {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);

        // Vertical needle line across tracks with subtle drop shadow
        p.setPen(QPen(QColor(0, 0, 0, 80), 3.0));
        p.drawLine(px, 18, px, height());
        p.setPen(QPen(QColor(248, 113, 113), 1.6));
        p.drawLine(px, 18, px, height());

        // Shield head in ruler
        QPainterPath headPath;
        headPath.moveTo(px - 7, 1);
        headPath.lineTo(px + 7, 1);
        headPath.lineTo(px + 7, 14);
        headPath.lineTo(px, 22);
        headPath.lineTo(px - 7, 14);
        headPath.closeSubpath();

        // Drop shadow
        p.fillPath(headPath.translated(0, 1), QColor(0, 0, 0, 90));

        // Fill with gradient
        QLinearGradient phGrad(0, 1, 0, 22);
        phGrad.setColorAt(0.0, QColor(255, 100, 90));
        phGrad.setColorAt(1.0, QColor(220, 38, 38));
        p.fillPath(headPath, phGrad);

        // Border
        p.setPen(QPen(QColor(255, 255, 255, 180), 1.0));
        p.drawPath(headPath);

        // Center notch
        p.setPen(QPen(QColor(255, 255, 255, 220), 1.2));
        p.drawLine(px, 3, px, 11);

        p.restore();
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
    if (!m_project) return;
    QString trackId, clipId;
    DragMode mode;
    hitTest(event->pos(), &trackId, &clipId, &mode);

    // Cut/blade tool: click anywhere on a clip to split it right there,
    // instead of starting a move/trim drag.
    if (m_cutToolActive) {
        if (!clipId.isEmpty()) {
            pushUndo();
            const Ticks t = pixelToTime(event->pos().x());
            if (m_project->timeline().splitClip(trackId, clipId, t)) {
                emit timelineEdited();
                update();
            }
        }
        return;
    }

    if (mode == DragMode::ToggleTrackControl) {
        int row = -1;
        const TrackControl ctrl = trackControlAtPosition(event->pos(), &row);
        if (ctrl != TrackControl::None) {
            toggleTrackControl(row, ctrl);
            return;
        }
    }

    if (mode == DragMode::ScrubPlayhead) {
        m_selectedClipId.clear();
        m_selectedTrackId.clear();
        emit selectionChanged(m_selectedClipId, m_selectedTrackId);
        const Ticks t = pixelToTime(event->pos().x());
        setPlayheadTime(t);
        m_scrubThrottleTimer.restart();
        emit seekRequested(t);
        m_dragMode = DragMode::ScrubPlayhead;
        return;
    }

    if (mode == DragMode::ReorderTrack) {
        m_selectedClipId.clear();
        m_selectedTrackId.clear();
        emit selectionChanged(m_selectedClipId, m_selectedTrackId);
        m_dragMode = DragMode::ReorderTrack;
        m_dragTrackId = trackId;
        const auto& tracks = m_project->timeline().tracks();
        for (int i = 0; i < static_cast<int>(tracks.size()); ++i) {
            if (tracks[i].id == trackId) { m_dragTrackCurrentIndex = i; break; }
        }
        update();
        return;
    }

    if (mode == DragMode::None) {
        m_selectedClipId.clear();
        m_selectedTrackId.clear();
        emit selectionChanged(m_selectedClipId, m_selectedTrackId);
        update();
        return;
    }

    m_selectedClipId = clipId;
    m_selectedTrackId = trackId;
    emit selectionChanged(clipId, trackId);

    Track* track = m_project->timeline().findTrack(trackId);
    Clip* clip = track ? track->findClip(clipId) : nullptr;
    if (clip && !track->locked) {
        m_dragMode = mode;
        m_dragTrackId = trackId;
        m_dragClipId = clipId;
        m_dragAnchorTime = pixelToTime(event->pos().x());
        m_dragClipOrigStart = clip->timelineStart;
        m_dragClipOrigIn = clip->sourceIn;
        m_dragClipOrigOut = clip->sourceOut;
        m_dragUndoSnapshotted = false;
        m_dragModifier = event->modifiers(); // Ctrl/Shift modifier

        // Store fade durations for fade-drag
        m_dragFadeOrigDuration = (mode == DragMode::FadeIn) ? clip->fadeInDuration : clip->fadeOutDuration;

        // Compute the neighbor clamp once, from the clips currently on this
        // track other than the one being dragged. track->clips() is kept
        // sorted by timelineStart, so the nearest left/right neighbor is
        // just the closest clip whose range doesn't include this one.
        m_dragLeftBound = 0;
        m_dragRightBound = std::numeric_limits<Ticks>::max();
        for (const auto& other : track->clips()) {
            if (other.id == clipId) continue;
            if (other.timelineEnd() <= m_dragClipOrigStart) {
                m_dragLeftBound = std::max(m_dragLeftBound, other.timelineEnd());
            } else if (other.timelineStart >= clip->timelineEnd()) {
                m_dragRightBound = std::min(m_dragRightBound, other.timelineStart);
            }
        }

        // Pre-compute snap points once here — avoids O(N) traversal on every
        // mouseMoveEvent. Only needed for move/trim modes; fade drags don't snap.
        if (mode == DragMode::MoveClip || mode == DragMode::TrimLeft || mode == DragMode::TrimRight) {
            m_dragSnapPoints = computeSnapPoints(clipId);
        } else {
            m_dragSnapPoints.clear();
        }
    }
    update();
}


void TimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!m_project || m_cutToolActive) return; // no dragging while the blade tool is active

    if (m_dragMode == DragMode::ScrubPlayhead && (event->buttons() & Qt::LeftButton)) {
        setCursor(Qt::SizeHorCursor);
        const Ticks t = pixelToTime(event->pos().x());
        setPlayheadTime(t);
        // Throttle FFmpeg seeking to max ~35-40fps so continuous rapid scrubbing
        // doesn't block the UI thread on heavy synchronous frame decode.
        if (!m_scrubThrottleTimer.isValid() || m_scrubThrottleTimer.elapsed() >= 25) {
            m_scrubThrottleTimer.restart();
            emit seekRequested(t);
        }
        return;
    }

    if (m_dragMode == DragMode::ReorderTrack) {
        if (!m_dragUndoSnapshotted) { pushUndo(); m_dragUndoSnapshotted = true; }
        const int trackCount = static_cast<int>(m_project->timeline().tracks().size());
        int row = trackRowAtY(event->pos().y());
        if (row < 0) row = (event->pos().y() < m_rulerHeight) ? 0 : trackCount - 1;
        row = std::clamp(row, 0, trackCount - 1);
        const int newIndex = trackVectorIndexForRow(row);
        if (newIndex != m_dragTrackCurrentIndex && m_dragTrackCurrentIndex >= 0) {
            auto& tracks = m_project->timeline().tracks();
            Track moved = std::move(tracks[m_dragTrackCurrentIndex]);
            tracks.erase(tracks.begin() + m_dragTrackCurrentIndex);
            tracks.insert(tracks.begin() + newIndex, std::move(moved));
            m_dragTrackCurrentIndex = newIndex;
            update();
        }
        return;
    }

    if (m_dragMode == DragMode::None) {
        int hoverRow = -1;
        TrackControl ctrl = TrackControl::None;
        if (event->pos().x() < m_headerWidth && event->pos().y() >= m_rulerHeight) {
            ctrl = trackControlAtPosition(event->pos(), &hoverRow);
            if (ctrl == TrackControl::Delete) {
                setCursor(Qt::PointingHandCursor);
                setToolTip(tr("Xóa layer"));
            } else if (ctrl == TrackControl::Lock) {
                setCursor(Qt::PointingHandCursor);
                setToolTip(tr("Khóa / Mở khóa layer"));
            } else if (ctrl == TrackControl::Hidden) {
                setCursor(Qt::PointingHandCursor);
                setToolTip(tr("Ẩn / Hiện layer"));
            } else if (ctrl == TrackControl::Mute) {
                setCursor(Qt::PointingHandCursor);
                setToolTip(tr("Tắt / Bật tiếng layer"));
            } else {
                setCursor(Qt::ArrowCursor);
                setToolTip(QString());
            }
        } else {
            const int phX = timeToPixel(m_playheadTime);
            const bool overPlayheadShield = (event->pos().y() < m_rulerHeight && std::abs(event->pos().x() - phX) <= 12);
            const bool overPlayheadNeedle = (std::abs(event->pos().x() - phX) <= 6 && event->pos().x() >= m_headerWidth - 6);

            if (overPlayheadShield || overPlayheadNeedle) {
                setCursor(Qt::SizeHorCursor);
                setToolTip(tr("Kéo để tua video (Playhead)"));
            } else if (event->pos().y() < m_rulerHeight && event->pos().x() >= m_headerWidth - 6) {
                setCursor(Qt::PointingHandCursor);
                setToolTip(tr("Nhấp để di chuyển con trỏ phát"));
            } else {
                QString trackId, clipId;
                DragMode hitMode = DragMode::None;
                hitTest(event->pos(), &trackId, &clipId, &hitMode);
                if (hitMode == DragMode::TrimLeft || hitMode == DragMode::TrimRight) {
                    setCursor(Qt::SizeHorCursor);
                    setToolTip(tr("Kéo để cắt / mở rộng clip"));
                } else if (hitMode == DragMode::FadeIn || hitMode == DragMode::FadeOut) {
                    setCursor(Qt::PointingHandCursor);
                    setToolTip(tr("Kéo để chỉnh Fade"));
                } else if (hitMode == DragMode::MoveClip) {
                    setCursor(Qt::SizeAllCursor);
                    setToolTip(QString());
                } else {
                    setCursor(Qt::ArrowCursor);
                    setToolTip(QString());
                }
            }
        }

        if (hoverRow != m_hoverTrackRow || ctrl != m_hoverControl) {
            m_hoverTrackRow = hoverRow;
            m_hoverControl = ctrl;
            update(QRect(0, m_rulerHeight, m_headerWidth, height() - m_rulerHeight));
        }
        return;
    }

    Track* track = m_project->timeline().findTrack(m_dragTrackId);
    Clip* clip = track ? track->findClip(m_dragClipId) : nullptr;
    if (!clip) return;

    if (!m_dragUndoSnapshotted) { pushUndo(); m_dragUndoSnapshotted = true; }

    const Ticks now = pixelToTime(event->pos().x());
    const Ticks delta = now - m_dragAnchorTime;
    // Alt disables snapping (in addition to the global snap toggle)
    const bool snapEnabled = m_snapEnabled && !(event->modifiers() & Qt::AltModifier);
    // Use the snap-point cache computed once at drag-start (mousePressEvent).
    // Avoids an O(N) timeline traversal on every mouse move event.
    const QList<Ticks>& snapPts = m_dragSnapPoints;

    // Capture the old snap line position BEFORE the drag branches mutate
    // m_lastSnapTarget, so we can erase it correctly in the dirty-rect call.
    const int prevSnapX = (m_lastSnapTarget >= 0) ? timeToPixel(m_lastSnapTarget) : -1;
    // Resolve the visual row of the track being dragged (used for dirty-rect).
    const int dragRow = [&]() -> int {
        const auto& tracks = m_project->timeline().tracks();
        for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
            if (tracks[i].id == m_dragTrackId) return static_cast<int>(tracks.size()) - 1 - i;
        return -1;
    }();

    QString tooltip;
    if (m_dragMode == DragMode::FadeIn) {
        // Dragging right/left adjusts fade-in duration
        const Ticks rawFadeDur = m_dragFadeOrigDuration + delta;
        const Ticks maxFade = clip->timelineDuration() / 2;
        clip->fadeInDuration = std::clamp<Ticks>(rawFadeDur, 0, maxFade);
        tooltip = tr("Fade in: %1").arg(formatDurationShort(clip->fadeInDuration));
        m_lastSnapTarget = -1;
    } else if (m_dragMode == DragMode::FadeOut) {
        // Dragging left increases fade-out (delta is negative when moving left)
        const Ticks rawFadeDur = m_dragFadeOrigDuration - delta;
        const Ticks maxFade = clip->timelineDuration() / 2;
        clip->fadeOutDuration = std::clamp<Ticks>(rawFadeDur, 0, maxFade);
        tooltip = tr("Fade out: %1").arg(formatDurationShort(clip->fadeOutDuration));
        m_lastSnapTarget = -1;
    } else if (m_dragMode == DragMode::MoveClip) {
        // Apply modifier-based constraints
        Ticks newStart = m_dragClipOrigStart + delta;
        if (m_dragModifier & Qt::ControlModifier) {
            // Ctrl: snap to nearest frame (at project frame rate)
            double fps = m_project->timeline().frameRate > 0 ? m_project->timeline().frameRate : 30.0;
            newStart = secondsToTicks(std::round(ticksToSeconds(newStart) * fps) / fps);
        }
        // Auto-snap clip start
        Ticks snappedStart = snapTime(newStart, snapPts, snapEnabled);
        Ticks snappedEnd = snapTime(newStart + clip->timelineDuration(), snapPts, snapEnabled);
        if (std::abs(timeToPixel(snappedEnd) - timeToPixel(newStart + clip->timelineDuration())) <
            std::abs(timeToPixel(snappedStart) - timeToPixel(newStart))) {
            newStart = snappedEnd - clip->timelineDuration();
            m_lastSnapTarget = snappedEnd;
        } else if (snappedStart != newStart) {
            newStart = snappedStart;
            m_lastSnapTarget = snappedStart;
        } else {
            m_lastSnapTarget = -1;
        }
        // Never allow negative timeline position, and never let this clip
        // overlap its nearest neighbors on the same track (computed once at
        // mouse-press in m_dragLeftBound/m_dragRightBound).
        const Ticks duration = clip->timelineDuration();
        Ticks maxStart = (m_dragRightBound == std::numeric_limits<Ticks>::max())
                              ? std::numeric_limits<Ticks>::max()
                              : m_dragRightBound - duration;
        newStart = std::clamp<Ticks>(newStart, m_dragLeftBound, std::max(m_dragLeftBound, maxStart));
        clip->timelineStart = newStart;
        tooltip = tr("Bắt đầu: %1").arg(formatTimecode(clip->timelineStart).left(11));
    } else if (m_dragMode == DragMode::TrimLeft) {
        Ticks newIn = std::clamp<Ticks>(m_dragClipOrigIn + delta, 0, m_dragClipOrigOut - kMinClipDuration);
        if (m_dragModifier & Qt::ControlModifier) {
            // Ctrl: snap to nearest frame
            double fps = m_project->timeline().frameRate > 0 ? m_project->timeline().frameRate : 30.0;
            newIn = secondsToTicks(std::round(ticksToSeconds(newIn) * fps) / fps);
        }
        Ticks appliedDelta = newIn - m_dragClipOrigIn;
        Ticks newStart = std::max<Ticks>(m_dragLeftBound, m_dragClipOrigStart + appliedDelta);
        // Snap left edge
        Ticks snappedStart = snapTime(newStart, snapPts, snapEnabled);
        if (snappedStart != newStart) { newStart = snappedStart; m_lastSnapTarget = snappedStart; }
        else m_lastSnapTarget = -1;
        appliedDelta = newStart - m_dragClipOrigStart;
        clip->sourceIn = m_dragClipOrigIn + appliedDelta;
        clip->timelineStart = newStart;
        tooltip = tr("Thời lượng: %1").arg(formatDurationShort(clip->timelineDuration()));
    } else if (m_dragMode == DragMode::TrimRight) {
        // Don't let a trim run past the *source media* length (for video/audio),
        // or past the start of the next clip on this track.
        // Static clips (image, text) have no source duration limit.
        const auto asset = m_project->findAsset(clip->assetId);
        const bool isStatic = clip->isStaticVisual() || (asset && asset->kind == MediaKind::Image);
        const Ticks sourceMax = isStatic ? std::numeric_limits<Ticks>::max()
                               : (asset && asset->duration > 0 ? asset->duration
                                                               : m_dragClipOrigOut + secondsToTicks(60));
        Ticks newOut = m_dragClipOrigOut + delta;
        if (m_dragModifier & Qt::ControlModifier) {
            // Ctrl: snap to nearest frame
            double fps = m_project->timeline().frameRate > 0 ? m_project->timeline().frameRate : 30.0;
            newOut = secondsToTicks(std::round(ticksToSeconds(newOut) * fps) / fps);
        }
        const Ticks neighborCap = (m_dragRightBound == std::numeric_limits<Ticks>::max())
                                       ? sourceMax
                                       : m_dragClipOrigIn + (m_dragRightBound - m_dragClipOrigStart);
        newOut = std::clamp<Ticks>(newOut, m_dragClipOrigIn + kMinClipDuration, std::min(sourceMax, neighborCap));
        // Snap right edge
        const Ticks snappedOut = snapTime(m_dragClipOrigStart + (newOut - m_dragClipOrigIn), snapPts, snapEnabled);
        if (snappedOut != m_dragClipOrigStart + (newOut - m_dragClipOrigIn)) {
            m_lastSnapTarget = snappedOut;
        } else {
            m_lastSnapTarget = -1;
        }
        clip->sourceOut = newOut;
        tooltip = tr("Thời lượng: %1").arg(formatDurationShort(clip->timelineDuration()));
    }
    // sortClips() is deferred to mouseReleaseEvent — clips don't truly change
    // order during a single-track drag, so sorting mid-drag is unnecessary work.
    if (!tooltip.isEmpty()) {
        QToolTip::showText(event->globalPosition().toPoint(), tooltip, this);
    }
    // Dirty-rect repaint: only invalidate the affected track row + snap line
    // columns (old position to erase, new position to draw).
    invalidateDragRegion(dragRow, prevSnapX, (m_lastSnapTarget >= 0) ? timeToPixel(m_lastSnapTarget) : -1);
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent*) {
    if (!m_project) {
        m_dragMode = DragMode::None;
        return;
    }
    if (m_dragMode == DragMode::ScrubPlayhead) {
        emit seekRequested(m_playheadTime);
    } else if (m_dragMode == DragMode::MoveClip || m_dragMode == DragMode::TrimLeft ||
        m_dragMode == DragMode::TrimRight || m_dragMode == DragMode::FadeIn ||
        m_dragMode == DragMode::FadeOut) {
        // Sort was deferred from the drag loop; do it once here at release.
        Track* track = m_project->timeline().findTrack(m_dragTrackId);
        if (track) track->sortClips();
        emit timelineEdited();
    } else if (m_dragMode == DragMode::ReorderTrack) {
        emit timelineEdited();
        update();
    }
    m_dragMode = DragMode::None;
    m_dragTrackCurrentIndex = -1;
    m_dragUndoSnapshotted = false;
    m_lastSnapTarget = -1;
    m_dragSnapPoints.clear(); // release the cached snap-point list
    unsetCursor();
    update();
}

void TimelineWidget::wheelEvent(QWheelEvent* event) {
    if (!m_project) {
        QWidget::wheelEvent(event);
        return;
    }

    const Qt::KeyboardModifiers mods = event->modifiers();
    // Ctrl/Alt + wheel zooms around the cursor (the standard NLE gesture).
    if (mods & (Qt::ControlModifier | Qt::AltModifier)) {
        const int deltaY = event->angleDelta().y();
        if (deltaY == 0) return;
        // Accumulate across high-resolution trackpads: one notch ≈ 1.25×.
        const double factor = std::pow(1.25, static_cast<double>(deltaY) / 120.0);
        zoomAt(pixelToTime(event->position().x()), factor,
               static_cast<int>(event->position().x()));
        event->accept();
        return;
    }

    QScrollArea* sa = outerScrollArea();
    if (!sa) {
        QWidget::wheelEvent(event);
        return;
    }

    int dy = event->angleDelta().y();
    int dx = event->angleDelta().x();
    // Prefer pixel deltas (high-res trackpads) when available.
    const QPoint pixelDelta = event->pixelDelta();
    if (!pixelDelta.isNull()) {
        dx = pixelDelta.x();
        dy = pixelDelta.y();
    }

    if (mods & Qt::ShiftModifier) {
        // Shift + wheel = vertical scroll (pass through).
        QScrollBar* vbar = sa->verticalScrollBar();
        vbar->setValue(vbar->value() - (dy != 0 ? dy : dx));
    } else {
        // Plain wheel = horizontal pan of the timeline (the standard NLE
        // convention where vertical wheel scrolls the timeline sideways).
        QScrollBar* hbar = sa->horizontalScrollBar();
        hbar->setValue(hbar->value() - (dy != 0 ? dy : dx));
    }
    event->accept();
}

void TimelineWidget::leaveEvent(QEvent* event) {
    QWidget::leaveEvent(event);
    if (m_hoverTrackRow != -1 || m_hoverControl != TrackControl::None) {
        m_hoverTrackRow = -1;
        m_hoverControl = TrackControl::None;
        update(QRect(0, m_rulerHeight, m_headerWidth, height() - m_rulerHeight));
    }
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    // Double-clicking a crossfade marker (see paintEvent) toggles the
    // transition between the two clips it sits between on/off.
    QString txTrackId, prevClipId, curClipId;
    if (transitionMarkerAt(event->pos(), &txTrackId, &prevClipId, &curClipId)) {
        Track* txTrack = m_project->timeline().findTrack(txTrackId);
        Clip* prevClip = txTrack ? txTrack->findClip(prevClipId) : nullptr;
        Clip* curClip = txTrack ? txTrack->findClip(curClipId) : nullptr;
        toggleTransitionAt(txTrack, prevClip, curClip);
        QWidget::mouseDoubleClickEvent(event);
        return;
    }

    // Double-clicking a Text clip renames its label (the actual title).
    QString trackId, clipId;
    DragMode mode;
    hitTest(event->pos(), &trackId, &clipId, &mode);
    Track* track = m_project->timeline().findTrack(trackId);
    Clip* clip = track ? track->findClip(clipId) : nullptr;
    if (clip && clip->type == ClipType::Text && !track->locked) {
        bool ok = false;
        const QString newLabel = QInputDialog::getText(this, tr("Tiêu đề"),
            tr("Nội dung văn bản:"), QLineEdit::Normal, clip->displayLabel, &ok);
        if (ok && !newLabel.isEmpty()) {
            pushUndo();
            clip->displayLabel = newLabel;
            emit timelineEdited();
            update();
        }
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TimelineWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) {
        deleteSelectedClip();
    } else if (event->key() == Qt::Key_S) {
        splitAtPlayhead();
    } else if (event->key() == Qt::Key_Space) {
        emit togglePlaybackRequested();
    } else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
               event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        const double fps = m_project->timeline().frameRate > 0 ? m_project->timeline().frameRate : 30.0;
        Ticks step = secondsToTicks(1.0 / fps);
        if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
            step = secondsToTicks(5.0);
        }
        const Ticks target = (event->key() == Qt::Key_Left || event->key() == Qt::Key_Down)
                                 ? m_playheadTime - step : m_playheadTime + step;
        setPlayheadTime(std::clamp<Ticks>(target, 0, m_project->timeline().totalDuration()));
        emit seekRequested(m_playheadTime);
    } else if (event->key() == Qt::Key_Comma || event->key() == Qt::Key_Period) {
        // Nudge the selected clip by one frame (comma = left, period = right).
        const Ticks frame = frameStepTicks();
        nudgeSelectedClip(event->key() == Qt::Key_Comma ? -frame : frame);
    } else if (event->key() == Qt::Key_Home) {
        // Jump to the start of the timeline (standard NLE navigation).
        setPlayheadTime(0);
        emit seekRequested(m_playheadTime);
    } else if (event->key() == Qt::Key_End) {
        // Jump to the end of the timeline.
        const Ticks end = m_project ? m_project->timeline().totalDuration() : 0;
        setPlayheadTime(end);
        emit seekRequested(m_playheadTime);
    } else {
        QWidget::keyPressEvent(event);
    }
}

void TimelineWidget::clearSelection() {
    if (!m_selectedClipId.isEmpty() || !m_selectedTrackId.isEmpty()) {
        m_selectedClipId.clear();
        m_selectedTrackId.clear();
        emit selectionChanged(QString(), QString());
        update();
    }
}

void TimelineWidget::selectClip(const QString& trackId, const QString& clipId) {
    m_selectedTrackId = trackId;
    m_selectedClipId = clipId;
    emit selectionChanged(clipId, trackId);
    update();
}

void TimelineWidget::deleteSelectedClip() {
    if (!m_project || m_selectedClipId.isEmpty()) return;
    Track* track = m_project->timeline().findTrack(m_selectedTrackId);
    if (track && track->locked) return; // locked tracks can not be edited
    pushUndo(); // snapshot BEFORE the destructive change
    if (track && track->removeClip(m_selectedClipId)) {
        m_selectedClipId.clear();
        m_selectedTrackId.clear();
        emit selectionChanged(QString(), QString());
        emit timelineEdited();
        update();
    }
}

Ticks TimelineWidget::frameStepTicks() const {
    const double fps = (m_project && m_project->timeline().frameRate > 0)
                           ? m_project->timeline().frameRate : 30.0;
    return secondsToTicks(1.0 / fps);
}

void TimelineWidget::setSnapEnabled(bool enabled) {
    if (m_snapEnabled == enabled) return;
    m_snapEnabled = enabled;
    update();
}

// ── Clipboard: copy / paste / duplicate ─────────────────────────────
// Copies are deep (Clip is plain data), placed at the playhead on the first
// compatible unlocked track, nudged past any overlap exactly like a media
// drop, and always get a fresh id so undo history and selection stay sane.

void TimelineWidget::copySelectedClip() {
    if (!m_project || m_selectedClipId.isEmpty()) return;
    Track* track = m_project->timeline().findTrack(m_selectedTrackId);
    if (!track) return;
    Clip* clip = track->findClip(m_selectedClipId);
    if (!clip) return;
    m_clipboard = *clip;
}

void TimelineWidget::pasteClip() {
    if (!m_project || !m_clipboard) return;
    Clip copy = *m_clipboard;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    const bool wantsAudio = (copy.type == ClipType::Audio);
    auto compatible = [&](const Track& t) {
        return !t.locked && (wantsAudio ? (t.type == TrackType::Audio)
                                        : (t.type == TrackType::Visual));
    };

    // Prefer the currently selected track when it's compatible and unlocked,
    // otherwise the first compatible unlocked track in the timeline.
    Track* target = m_project->timeline().findTrack(m_selectedTrackId);
    if (!target || !compatible(*target)) {
        target = nullptr;
        for (auto& t : m_project->timeline().tracks()) {
            if (compatible(t)) { target = &t; break; }
        }
    }

    pushUndo();
    if (!target) {
        const TrackType wantType = wantsAudio ? TrackType::Audio : TrackType::Visual;
        const QString name = QString("%1 %2")
            .arg(wantType == TrackType::Audio ? "Audio" : "Visual")
            .arg(m_project->timeline().tracks().size() + 1);
        target = &m_project->timeline().addTrack(wantType, name);
    }

    // Place at the playhead, pushing forward past any overlap on the target
    // track (bounded by clip count so a dense track can't loop forever).
    Ticks placedStart = std::max<Ticks>(0, m_playheadTime);
    const Ticks duration = copy.timelineDuration();
    const auto& existing = target->clips();
    for (size_t guard = 0; guard < existing.size() + 1; ++guard) {
        const Clip* blocker = nullptr;
        for (const auto& other : existing) {
            if (placedStart < other.timelineEnd() && placedStart + duration > other.timelineStart) {
                blocker = &other;
                break;
            }
        }
        if (!blocker) break;
        placedStart = blocker->timelineEnd();
    }
    copy.timelineStart = placedStart;

    Clip* added = target->addClip(std::move(copy));
    if (added) {
        m_selectedClipId = added->id;
        m_selectedTrackId = target->id;
        emit selectionChanged(m_selectedClipId, m_selectedTrackId);
    }
    emit timelineEdited();
    emit seekRequested(m_playheadTime); // re-render the frame under the playhead
    update();
}

void TimelineWidget::duplicateSelectedClip() {
    if (!m_project || m_selectedClipId.isEmpty()) return;
    Track* track = m_project->timeline().findTrack(m_selectedTrackId);
    if (!track || track->locked) return;
    Clip* clip = track->findClip(m_selectedClipId);
    if (!clip) return;

    Clip copy = *clip;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    pushUndo();

    // Place immediately after the source clip, nudged past any overlap.
    Ticks placedStart = clip->timelineEnd();
    const Ticks duration = copy.timelineDuration();
    const auto& existing = track->clips();
    for (size_t guard = 0; guard < existing.size() + 1; ++guard) {
        const Clip* blocker = nullptr;
        for (const auto& other : existing) {
            if (other.id == clip->id) continue;
            if (placedStart < other.timelineEnd() && placedStart + duration > other.timelineStart) {
                blocker = &other;
                break;
            }
        }
        if (!blocker) break;
        placedStart = blocker->timelineEnd();
    }
    copy.timelineStart = placedStart;

    Clip* added = track->addClip(std::move(copy));
    if (added) {
        m_selectedClipId = added->id;
        m_selectedTrackId = track->id;
        emit selectionChanged(m_selectedClipId, m_selectedTrackId);
    }
    emit timelineEdited();
    emit seekRequested(m_playheadTime); // re-render the frame under the playhead
    update();
}

void TimelineWidget::rippleDeleteSelectedClip() {
    if (!m_project || m_selectedClipId.isEmpty()) return;
    Track* track = m_project->timeline().findTrack(m_selectedTrackId);
    if (track && track->locked) return; // locked tracks can not be edited
    pushUndo(); // snapshot BEFORE the destructive change
    if (m_project->timeline().rippleDeleteClip(m_selectedTrackId, m_selectedClipId)) {
        m_selectedClipId.clear();
        m_selectedTrackId.clear();
        emit selectionChanged(QString(), QString());
        emit timelineEdited();
        emit seekRequested(m_playheadTime); // re-render the frame under the playhead
        update();
    }
}

void TimelineWidget::nudgeSelectedClip(Ticks deltaTicks) {
    if (!m_project || m_selectedClipId.isEmpty() || deltaTicks == 0) return;
    Track* track = m_project->timeline().findTrack(m_selectedTrackId);
    if (!track || track->locked) return;
    Clip* clip = track->findClip(m_selectedClipId);
    if (!clip) return;
    const Ticks newStart = std::max<Ticks>(0, clip->timelineStart + deltaTicks);
    if (newStart == clip->timelineStart) return;
    pushUndo();
    clip->timelineStart = newStart;
    emit timelineEdited();
    emit seekRequested(m_playheadTime); // re-render the frame under the playhead
    update();
}

void TimelineWidget::deleteTrack(const QString& trackId) {
    if (!m_project || trackId.isEmpty()) return;
    Track* track = m_project->timeline().findTrack(trackId);
    if (!track) return;

    if (!track->clips().empty()) {
        const auto res = QMessageBox::question(
            this,
            tr("Xóa layer"),
            tr("Layer \"%1\" đang chứa %2 clip.\nBạn có chắc chắn muốn xóa layer này không?")
                .arg(track->name)
                .arg(track->clips().size()),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No
        );
        if (res != QMessageBox::Yes) {
            return;
        }
    }

    pushUndo();
    if (m_selectedTrackId == trackId) {
        m_selectedClipId.clear();
        m_selectedTrackId.clear();
        emit selectionChanged(QString(), QString());
    }

    if (m_project->timeline().removeTrack(trackId)) {
        refresh();
        emit timelineEdited();
    }
}

void TimelineWidget::deleteSelectedTrack() {
    if (!m_project) return;
    QString trackId = m_selectedTrackId;
    if (trackId.isEmpty()) {
        const auto& tracks = m_project->timeline().tracks();
        if (!tracks.empty()) {
            trackId = tracks.back().id;
        }
    }
    if (!trackId.isEmpty()) {
        deleteTrack(trackId);
    }
}

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event) {
    const QPoint pos = event->pos();

    // Right-clicking a transition marker opens the transition editor
    // (type / direction / dip colour / duration / remove).
    {
        QString txTrackId, prevClipId, curClipId;
        if (transitionMarkerAt(pos, &txTrackId, &prevClipId, &curClipId)) {
            Track* txTrack = m_project->timeline().findTrack(txTrackId);
            QMenu menu(this);
            buildTransitionMenu(&menu, txTrack, prevClipId, curClipId);
            menu.exec(event->globalPos());
            return;
        }
    }

    const int row = trackRowAtY(pos.y());
    const int count = static_cast<int>(m_project->timeline().tracks().size());

    QMenu menu(this);

    if (row >= 0 && row < count) {
        const int idx = trackVectorIndexForRow(row);
        Track& track = m_project->timeline().tracks()[idx];

        // Right-clicked in the header (layer controls) area
        if (pos.x() < m_headerWidth) {
            auto* titleAct = menu.addAction(track.name);
            QFont font = titleAct->font();
            font.setBold(true);
            titleAct->setFont(font);
            titleAct->setEnabled(false);
            menu.addSeparator();

            menu.addAction(tr("Xóa layer \"%1\"").arg(track.name), [this, trackId = track.id]() {
                deleteTrack(trackId);
            });

            menu.addAction(tr("Đổi tên layer..."), [this, trackId = track.id, curName = track.name]() {
                bool ok = false;
                QString newName = QInputDialog::getText(this, tr("Đổi tên layer"), tr("Tên layer mới:"), QLineEdit::Normal, curName, &ok);
                if (ok && !newName.trimmed().isEmpty()) {
                    Track* t = m_project->timeline().findTrack(trackId);
                    if (t) {
                        pushUndo();
                        t->name = newName.trimmed();
                        emit timelineEdited();
                        update();
                    }
                }
            });

            menu.addSeparator();

            menu.addAction(track.locked ? tr("Mở khóa layer") : tr("Khóa layer"), [this, row]() {
                toggleTrackControl(row, TrackControl::Lock);
            });

            menu.addAction(track.hidden ? tr("Hiện layer") : tr("Ẩn layer"), [this, row]() {
                toggleTrackControl(row, TrackControl::Hidden);
            });

            menu.addAction(track.muted ? tr("Bật tiếng layer") : tr("Tắt tiếng layer"), [this, row]() {
                toggleTrackControl(row, TrackControl::Mute);
            });

            menu.addSeparator();
            menu.addAction(tr("Thêm layer video/ảnh"), [this]() {
                pushUndo();
                m_project->timeline().addTrack(TrackType::Visual, QString("Visual %1").arg(m_project->timeline().tracks().size() + 1));
                refresh();
                emit timelineEdited();
            });
            menu.addAction(tr("Thêm layer audio"), [this]() {
                pushUndo();
                m_project->timeline().addTrack(TrackType::Audio, QString("Audio %1").arg(m_project->timeline().tracks().size() + 1));
                refresh();
                emit timelineEdited();
            });

            menu.exec(event->globalPos());
            return;
        }

        // Check if right-clicked on a clip
        QString trackId, clipId;
        DragMode mode;
        hitTest(pos, &trackId, &clipId, &mode);
        if (!clipId.isEmpty()) {
            m_selectedClipId = clipId;
            m_selectedTrackId = trackId;
            emit selectionChanged(clipId, trackId);
            update();

            Clip* clip = track.findClip(clipId);
            auto* titleAct = menu.addAction(clip ? (clip->displayLabel.isEmpty() ? tr("Clip") : clip->displayLabel) : tr("Clip"));
            QFont font = titleAct->font();
            font.setBold(true);
            titleAct->setFont(font);
            titleAct->setEnabled(false);
            menu.addSeparator();

            menu.addAction(tr("Cắt tại playhead (S)"), this, &TimelineWidget::splitAtPlayhead);
            menu.addAction(tr("Xóa clip này (Delete)"), this, &TimelineWidget::deleteSelectedClip);
            menu.addAction(tr("Xóa & dồn clip sau lại (Ripple delete)"), this, &TimelineWidget::rippleDeleteSelectedClip);
            menu.addSeparator();
            menu.addAction(tr("Sao chép clip (Ctrl+C)"), this, &TimelineWidget::copySelectedClip);
            menu.addAction(tr("Dán clip (Ctrl+V)"), this, &TimelineWidget::pasteClip);
            menu.addAction(tr("Nhân đôi clip (Ctrl+D)"), this, &TimelineWidget::duplicateSelectedClip);
            menu.addSeparator();
            menu.addAction(tr("Dịch clip trái 1 khung hình (,)"), this, [this]() {
                nudgeSelectedClip(-frameStepTicks());
            });
            menu.addAction(tr("Dịch clip phải 1 khung hình (.)"), this, [this]() {
                nudgeSelectedClip(frameStepTicks());
            });
            menu.addSeparator();
            menu.addAction(tr("Xóa layer chứa clip này"), [this, trackId]() {
                deleteTrack(trackId);
            });

            menu.exec(event->globalPos());
            return;
        }
    }

    // Default right-click menu for empty timeline area
    menu.addAction(tr("Thêm layer video/ảnh"), [this]() {
        pushUndo();
        m_project->timeline().addTrack(TrackType::Visual, QString("Visual %1").arg(m_project->timeline().tracks().size() + 1));
        refresh();
        emit timelineEdited();
    });
    menu.addAction(tr("Thêm layer audio"), [this]() {
        pushUndo();
        m_project->timeline().addTrack(TrackType::Audio, QString("Audio %1").arg(m_project->timeline().tracks().size() + 1));
        refresh();
        emit timelineEdited();
    });
    if (!m_selectedTrackId.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("Xóa layer đang chọn"), this, &TimelineWidget::deleteSelectedTrack);
    }
    menu.exec(event->globalPos());
}

void TimelineWidget::splitAtPlayhead() {
    if (!m_project || m_selectedClipId.isEmpty()) return;
    Track* track = m_project->timeline().findTrack(m_selectedTrackId);
    if (track && track->locked) return;
    pushUndo();
    if (m_project->timeline().splitClip(m_selectedTrackId, m_selectedClipId, m_playheadTime)) {
        emit timelineEdited();
        update();
    }
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasFormat("application/x-hyggshicut-asset")) event->acceptProposedAction();
}
void TimelineWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (event->mimeData()->hasFormat("application/x-hyggshicut-asset")) event->acceptProposedAction();
}

void TimelineWidget::dropEvent(QDropEvent* event) {
    if (!m_project) return;
    const QString assetId = QString::fromUtf8(event->mimeData()->data("application/x-hyggshicut-asset"));
    auto asset = m_project->findAsset(assetId);
    if (!asset) return;

    const QPoint pos = event->position().toPoint();
    const Ticks dropTime = pixelToTime(pos.x());

    Track* targetTrack = nullptr;
    const int row = trackRowAtY(pos.y());
    if (row >= 0) {
        const int idx = trackVectorIndexForRow(row);
        Track& candidate = m_project->timeline().tracks()[idx];
        const bool compatible = (asset->kind == MediaKind::Audio && candidate.type == TrackType::Audio) ||
                                 (asset->kind != MediaKind::Audio && candidate.type == TrackType::Visual);
        if (compatible && !candidate.locked) targetTrack = &candidate;
    }
    pushUndo(); // snapshot BEFORE adding the clip / track
    if (!targetTrack) {
        const TrackType wantType = (asset->kind == MediaKind::Audio) ? TrackType::Audio : TrackType::Visual;
        const QString name = QString("%1 %2").arg(wantType == TrackType::Audio ? "Audio" : "Visual")
                                              .arg(m_project->timeline().tracks().size() + 1);
        targetTrack = &m_project->timeline().addTrack(wantType, name);
    }

    Clip clip;
    clip.assetId = asset->id;
    clip.type = asset->kind == MediaKind::Audio ? ClipType::Audio
              : asset->kind == MediaKind::Image ? ClipType::Image
                                                 : ClipType::Video;
    clip.sourceIn = 0;
    clip.sourceOut = asset->duration > 0 ? asset->duration : secondsToTicks(5.0);

    // Don't drop the new clip on top of an existing one on this track: if
    // [dropTime, dropTime+duration) overlaps a clip, push the drop point to
    // just after that clip and re-check (bounded by clip count so a dense
    // track can't loop forever).
    Ticks placedStart = std::max<Ticks>(0, dropTime);
    const Ticks duration = clip.timelineDuration();
    const auto& existing = targetTrack->clips();
    for (size_t guard = 0; guard < existing.size() + 1; ++guard) {
        const Clip* blocker = nullptr;
        for (const auto& other : existing) {
            if (placedStart < other.timelineEnd() && placedStart + duration > other.timelineStart) {
                blocker = &other;
                break;
            }
        }
        if (!blocker) break;
        placedStart = blocker->timelineEnd();
    }
    clip.timelineStart = placedStart;
    Clip* added = targetTrack->addClip(std::move(clip));

    // Select the clip we just dropped so the Transform panel binds to it
    // immediately — without this, adjusting "Tỉ lệ X/Y" right after adding
    // an image layer edits whatever was selected before (or nothing), and
    // the new layer keeps rendering at its default full-frame scale even
    // though the panel looks like it's editing it.
    if (added) {
        m_selectedClipId = added->id;
        m_selectedTrackId = targetTrack->id;
        emit selectionChanged(m_selectedClipId, m_selectedTrackId);
    }

    // A media drop can create a brand-new Player/Track. Recalculate the
    // vertical content size immediately so every Player stays on its own row.
    emit timelineEdited();
    refresh();

    event->acceptProposedAction();
}

} // namespace hc
