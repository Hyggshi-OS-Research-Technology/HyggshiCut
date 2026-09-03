#include "TimelineWidget.h"
#include <QPainter>
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
#include <algorithm>

namespace hc {

namespace {
constexpr Ticks kMinClipDuration = 33'333; // ~1 frame at 30fps, floor for trims
constexpr int kEdgeGrabPx = 6;
constexpr int kControlBtnW = 18;            // width of each per-track control button
constexpr int kControlGap = 3;

// --- tiny geometric icons for the per-track control strip (no emoji/font
// dependency: these are drawn with QPainter primitives) ---

void drawSpeakerIcon(QPainter& p, QRect r, bool muted) {
    const QPoint body(r.left() + 2, r.center().y());
    const QPolygon cone{QPoint(body.x(), body.y()),
                        QPoint(body.x() + 5, body.y() - 4),
                        QPoint(body.x() + 5, body.y() + 4)};
    QPen pen(QColor(160, 160, 168), 1.5);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(cone);
    p.drawLine(body.x() - 2, body.y() - 1, body.x() + 1, body.y() - 3);
    p.drawArc(body.x() + 6, body.y() - 5, 8, 10, -60 * 16, 120 * 16);
    if (muted) {
        // slash across the speaker
        QPen slash(QColor(235, 80, 80), 2);
        p.setPen(slash);
        p.drawLine(r.left() + 2, r.top() + 2, r.right() - 2, r.bottom() - 2);
    }
}

void drawEyeIcon(QPainter& p, QRect r, bool hidden) {
    QPen pen(QColor(160, 160, 175), 1.5);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const QRect eye(r.left() + 1, r.top() + r.height() / 2 - 4, 16, 8);
    p.drawEllipse(eye);
    p.setBrush(QColor(160, 160, 175));
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPoint(r.center().x(), r.center().y()), 2, 2);
    if (hidden) {
        QPen slash(QColor(235, 80, 80), 2);
        p.setPen(slash);
        p.drawLine(r.left() + 2, r.top() + 2, r.right() - 2, r.bottom() - 2);
    }
}

void drawLockIcon(QPainter& p, QRect r, bool locked) {
    QPen pen(locked ? QColor(235, 170, 60) : QColor(120, 120, 135), 1.5);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    // shackle (open vs closed)
    p.drawArc(r.left() + 5, r.top() + 2, 7, 7, 0, 180 * 16);
    // body
    p.drawRoundedRect(r.left() + 3, r.top() + 7, 12, 9, 2, 2);
}

void drawTrashIcon(QPainter& p, QRect r) {
    QPen pen(QColor(170, 140, 140), 1.5);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const int cx = r.center().x();
    const int cy = r.center().y();
    // Lid
    p.drawLine(cx - 5, cy - 4, cx + 5, cy - 4);
    p.drawLine(cx - 2, cy - 6, cx + 2, cy - 6);
    // Bin body
    p.drawLine(cx - 4, cy - 3, cx - 3, cy + 5);
    p.drawLine(cx + 4, cy - 3, cx + 3, cy + 5);
    p.drawLine(cx - 3, cy + 5, cx + 3, cy + 5);
    // Slats inside bin
    p.drawLine(cx - 1, cy - 1, cx - 1, cy + 3);
    p.drawLine(cx + 1, cy - 1, cx + 1, cy + 3);
}
}

TimelineWidget::TimelineWidget(Project* project, QWidget* parent)
    : QWidget(parent), m_project(project) {
    setMinimumHeight(m_rulerHeight + kMinTrackHeight);
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    recomputeTrackHeight();
}

void TimelineWidget::recomputeTrackHeight()
{
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
    const int count = static_cast<int>(m_project->timeline().tracks().size());
    if (row < 0 || row >= count || control == TrackControl::None) return {};
    const int idx = static_cast<int>(control);
    const int x = m_headerWidth - (kControlBtnW + kControlGap) * 4 + (kControlBtnW + kControlGap) * idx;
    // Account for a small right margin so the buttons sit inside the header.
    const int xAdjusted = x - kControlGap;
    const int y = m_rulerHeight + row * m_trackHeight;
    return QRect(xAdjusted, y, kControlBtnW, m_trackHeight);
}

TimelineWidget::TrackControl TimelineWidget::trackControlAtPosition(const QPoint& pos, int* outRow) const {
    if (pos.x() >= m_headerWidth || pos.y() < m_rulerHeight) return TrackControl::None;
    const int row = trackRowAtY(pos.y());
    const int count = static_cast<int>(m_project->timeline().tracks().size());
    if (row < 0 || row >= count) return TrackControl::None;
    for (int i = 0; i < 4; ++i) {
        if (trackControlRect(row, static_cast<TrackControl>(i)).adjusted(-2, 0, 2, 0).contains(pos)) {
            if (outRow) *outRow = row;
            return static_cast<TrackControl>(i);
        }
    }
    return TrackControl::None;
}

void TimelineWidget::toggleTrackControl(int row, TrackControl control) {
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
    m_project->pushUndoSnapshot();
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
    update(QRect(phx - 3, 0, 7, height()));
}

// --------------------------------------------------------------------------
// Snap helpers
// --------------------------------------------------------------------------
QList<Ticks> TimelineWidget::computeSnapPoints(const QString& excludeClipId) const {
    QList<Ticks> pts;
    pts.append(0); // start of timeline
    pts.append(m_playheadTime); // playhead
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
        update(QRect(oldPx - 6, 0, 14, height()));
        update(QRect(newPx - 6, 0, 14, height()));
    }
}

void TimelineWidget::setZoom(double pixelsPerSecond) {
    m_pixelsPerSecond = std::clamp(pixelsPerSecond, 5.0, 2000.0);
    updateGeometry();
    update();
}

Ticks TimelineWidget::pixelToTime(int x) const {
    return secondsToTicks(std::max(0, x - m_headerWidth) / m_pixelsPerSecond);
}
int TimelineWidget::timeToPixel(Ticks t) const {
    return m_headerWidth + static_cast<int>(ticksToSeconds(t) * m_pixelsPerSecond);
}

int TimelineWidget::trackRowAtY(int y) const {
    if (y < m_rulerHeight) return -1;
    const int row = (y - m_rulerHeight) / m_trackHeight;
    const int count = static_cast<int>(m_project->timeline().tracks().size());
    if (row < 0 || row >= count) return -1;
    return row;
}

int TimelineWidget::trackVectorIndexForRow(int row) const {
    const int count = static_cast<int>(m_project->timeline().tracks().size());
    return count - 1 - row;
}

QRect TimelineWidget::clipRect(int trackVectorIndex, const Clip& clip) const {
    const int count = static_cast<int>(m_project->timeline().tracks().size());
    const int row = count - 1 - trackVectorIndex;
    const int y = m_rulerHeight + row * m_trackHeight + 2;
    const int x = timeToPixel(clip.timelineStart);
    const int w = std::max(2, timeToPixel(clip.timelineEnd()) - x);
    return QRect(x, y, w, m_trackHeight - 4);
}

QSize TimelineWidget::sizeHint() const {
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
    // Track count may change without a QWidget resize event, e.g. when a new
    // Player/Track is created by dropping media.
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

    if (pos.y() < m_rulerHeight) {
        if (pos.x() >= m_headerWidth) *outMode = DragMode::ScrubPlayhead;
        return;
    }
    const int row = trackRowAtY(pos.y());
    if (row < 0) return;
    const int idx = trackVectorIndexForRow(row);
    const auto& tracks = m_project->timeline().tracks();
    if (idx < 0 || idx >= static_cast<int>(tracks.size())) return;
    const Track& track = tracks[idx];

    if (pos.x() < m_headerWidth) {
        // The right strip of the layers column holds the per-track controls
        // (mute / hide / lock); the rest of the column reorders the track.
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
        constexpr Ticks kDefaultTransition = 500'000; // 0.5s, in ticks (µs)
        const Ticks maxByPrev = prevClip->timelineDuration() / 2;
        const Ticks maxByCur = curClip->timelineDuration() / 2;
        const Ticks duration = std::clamp<Ticks>(kDefaultTransition, 1, std::max<Ticks>(1, std::min(maxByPrev, maxByCur)));
        curClip->transitionInDuration = duration;
        curClip->timelineStart -= duration;
    }
    emit timelineEdited();
    update();
}

void TimelineWidget::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    const QRect dirty = event ? event->rect() : rect();
    p.fillRect(dirty, QColor(30, 30, 34));

    const auto& tracks = m_project->timeline().tracks();
    const int trackCount = static_cast<int>(tracks.size());

    // --- ruler (starts after the layers column) ---
    if (dirty.top() < m_rulerHeight) {
        const int rW = std::min(width(), dirty.right() + 1);
        const int rL = dirty.left();
        p.fillRect(rL, 0, std::max(0, rW - rL), m_rulerHeight, QColor(24, 24, 27));
        if (rL < m_headerWidth) {
            p.fillRect(0, 0, m_headerWidth, m_rulerHeight, QColor(20, 20, 23));
            p.setPen(QColor(150, 150, 158));
            p.drawText(QRect(0, 0, m_headerWidth, m_rulerHeight), Qt::AlignCenter, tr("Layer"));
        }
        p.setPen(QColor(120, 120, 128));
        const int pxPerSecondTick = std::max(1, static_cast<int>(m_pixelsPerSecond));
        const int step = std::max(20, pxPerSecondTick);
        const int startX = m_headerWidth + std::max(0, ((dirty.left() - m_headerWidth) / step) * step);
        const int endX = std::min(width(), dirty.right() + step);
        for (int x = startX; x < endX; x += step) {
            if (x < m_headerWidth) continue;
            const Ticks t = pixelToTime(x);
            p.drawLine(x, m_rulerHeight - 6, x, m_rulerHeight);
            if (x == m_headerWidth || ((x - m_headerWidth) / step) % 5 == 0) {
                p.drawText(x + 2, m_rulerHeight - 8, formatTimecode(t).left(8));
            }
        }
    }

    // --- track rows ---
    for (int row = 0; row < trackCount; ++row) {
        const int idx = trackVectorIndexForRow(row);
        const Track& track = tracks[idx];
        const int y = m_rulerHeight + row * m_trackHeight;
        if (y + m_trackHeight < dirty.top() || y > dirty.bottom()) continue; // Skip rows outside dirty rect

        const bool beingReordered = (m_dragMode == DragMode::ReorderTrack && track.id == m_dragTrackId);

        const int trackDrawLeft = std::max(m_headerWidth, dirty.left());
        const int trackDrawRight = std::min(width(), dirty.right() + 1);
        if (trackDrawRight > trackDrawLeft) {
            p.fillRect(trackDrawLeft, y, trackDrawRight - trackDrawLeft, m_trackHeight,
                       (row % 2 == 0) ? QColor(38, 38, 43) : QColor(34, 34, 38));
            p.setPen(QColor(70, 70, 76));
            p.drawLine(trackDrawLeft, y, trackDrawRight, y);
        }

        // --- layers column: name + drag handle ---
        if (dirty.left() < m_headerWidth) {
            p.fillRect(0, y, m_headerWidth, m_trackHeight, beingReordered ? QColor(70, 70, 90) : QColor(26, 26, 30));
            p.setPen(QColor(90, 90, 98));
            p.drawLine(0, y, m_headerWidth, y);

            p.setPen(QColor(150, 150, 158));
            p.drawText(QRect(6, y, 16, m_trackHeight), Qt::AlignVCenter | Qt::AlignLeft, "⋮⋮");
            const QString typeLabel = track.type == TrackType::Visual ? "V" : "A";
            const int controlsWidth = (kControlBtnW + kControlGap) * 4 + 4;
            const int textWidth = std::max(20, m_headerWidth - controlsWidth - 24);
            p.drawText(QRect(24, y, textWidth, m_trackHeight), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(QString("%1 %2").arg(typeLabel, track.name),
                                                   Qt::ElideRight, textWidth));

            // Per-track control strip (mute / hide / lock / delete)
            drawSpeakerIcon(p, trackControlRect(row, TrackControl::Mute).translated(0, (m_trackHeight - 18) / 2),
                            track.muted);
            drawEyeIcon(p, trackControlRect(row, TrackControl::Hidden).translated(0, (m_trackHeight - 18) / 2),
                        track.hidden);
            drawLockIcon(p, trackControlRect(row, TrackControl::Lock).translated(0, (m_trackHeight - 18) / 2),
                         track.locked);
            drawTrashIcon(p, trackControlRect(row, TrackControl::Delete).translated(0, (m_trackHeight - 18) / 2));
        }

        for (const auto& clip : track.clips()) {
            const QRect r = clipRect(idx, clip);
            if (!r.intersects(dirty)) continue; // Skip clips outside dirty region!

            const bool selected = (clip.id == m_selectedClipId);
            QColor base = track.type == TrackType::Visual ? QColor(70, 110, 170)
                         : QColor(90, 150, 110);
            if (selected) base = base.lighter(140);
            if (track.locked) base = base.darker(125);
            if (track.hidden) base = base.darker(135);
            p.fillRect(r, base);
            p.setPen(selected ? QColor(255, 220, 120) : QColor(15, 15, 18));
            p.drawRect(r.adjusted(0, 0, -1, -1));

            // --- audio waveform: only for audio clips, clipped to dirty region ---
            if (clip.type == ClipType::Audio) {
                const auto asset = m_project->findAsset(clip.assetId);
                if (asset && !asset->waveformPeaks.empty() && r.width() > 1) {
                    const auto& peaks = asset->waveformPeaks;
                    const double bucketsPerSec = MediaAsset::kWaveformBucketsPerSecond;
                    const double srcInSec = ticksToSeconds(clip.sourceIn);
                    const double srcOutSec = ticksToSeconds(clip.sourceOut);
                    const int midY = r.center().y();
                    const int halfH = std::max(2, r.height() / 2 - 3);
                    p.setPen(QColor(210, 235, 220, 210));
                    const int xStart = std::max(r.left(), dirty.left());
                    const int xEnd = std::min(r.right(), dirty.right());
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

            // --- fade-in/out gradient overlays ---
            if (clip.fadeInDuration > 0) {
                const int fadeInW = std::min(r.width(), timeToPixel(clip.timelineStart + clip.fadeInDuration) - r.left());
                if (fadeInW > 0) {
                    const QRect fRect(r.left(), r.top(), fadeInW, r.height());
                    if (fRect.intersects(dirty)) {
                        QLinearGradient g(r.left(), 0, r.left() + fadeInW, 0);
                        g.setColorAt(0, QColor(0, 0, 0, 180));
                        g.setColorAt(1, QColor(0, 0, 0, 0));
                        p.fillRect(fRect, g);
                        // Fade-in drag handle triangle
                        const int hx = r.left() + fadeInW;
                        const int hy = r.top();
                        p.setBrush(QColor(255, 220, 80, 200));
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
                        // Fade-out drag handle triangle
                        const int hx = fadeOutEnd - fadeOutW;
                        const int hy = r.top();
                        p.setBrush(QColor(255, 220, 80, 200));
                        p.setPen(Qt::NoPen);
                        p.drawPolygon(QPolygon({QPoint(hx, hy), QPoint(fadeOutEnd, hy), QPoint(fadeOutEnd, hy + 10)}));
                    }
                }
            }

            p.setPen(Qt::white);
            QString label = clip.displayLabel;
            if (label.isEmpty()) {
                const auto asset = m_project->findAsset(clip.assetId);
                if (asset && asset->kind != MediaKind::Unknown) {
                    label = asset->displayName;
                } else {
                    label = tr("(media bị mất)");
                }
            }
            p.drawText(r.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(label, Qt::ElideRight, r.width() - 8));

            // --- keyframe diamond markers (hình thoi ◆) ---
            if (clip.hasTransformKeyframes()) {
                p.save();
                p.setRenderHint(QPainter::Antialiasing, true);
                const int kfSize = 5; // half-diagonal of diamond
                const int kfY = r.bottom() - 6;

                for (const auto& kf : clip.transformKeyframes) {
                    const Ticks absTime = clip.timelineStart + kf.time;
                    const int kfX = timeToPixel(absTime);
                    if (kfX < r.left() || kfX > r.right()) continue;
                    if (kfX < dirty.left() - kfSize || kfX > dirty.right() + kfSize) continue;

                    // Diamond polygon (hình thoi)
                    QPolygon diamond;
                    diamond << QPoint(kfX, kfY - kfSize)
                            << QPoint(kfX + kfSize, kfY)
                            << QPoint(kfX, kfY + kfSize)
                            << QPoint(kfX - kfSize, kfY);

                    const bool isCurrent = std::abs(timeToPixel(m_playheadTime) - kfX) <= 3;
                    if (isCurrent) {
                        p.setPen(QPen(QColor(255, 255, 255, 255), 1.5));
                        p.setBrush(QColor(255, 220, 50, 255));
                    } else {
                        p.setPen(QPen(QColor(20, 30, 40, 220), 1.0));
                        p.setBrush(QColor(255, 200, 40, 230));
                    }
                    p.drawPolygon(diamond);
                }
                p.restore();
            }
        }

        // --- crossfade transition markers ---
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
                p.setPen(QColor(20, 20, 24));
                p.setBrush(hasTransition ? QColor(255, 190, 80) : QColor(150, 150, 158, 160));
                p.drawEllipse(QPoint(mx, my), 7, 7);
                p.setPen(Qt::white);
                p.drawText(QRect(mx - 8, my - 8, 16, 16), Qt::AlignCenter, hasTransition ? tr("×") : tr("+"));
            }
        }
    }

    // --- snap line (yellow) shown while dragging ---
    if (m_lastSnapTarget >= 0) {
        const int sx = timeToPixel(m_lastSnapTarget);
        if (sx >= dirty.left() - 2 && sx <= dirty.right() + 2) {
            p.setPen(QPen(QColor(255, 220, 40, 200), 1, Qt::DashLine));
            p.drawLine(sx, m_rulerHeight, sx, height());
        }
    }

    // --- playhead ---
    const int px = timeToPixel(m_playheadTime);
    if (px >= dirty.left() - 6 && px <= dirty.right() + 6) {
        p.setPen(QPen(QColor(230, 60, 60), 2));
        p.drawLine(px, 0, px, height());
        p.setBrush(QColor(230, 60, 60));
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygon({QPoint(px - 5, 0), QPoint(px + 5, 0), QPoint(px, 8)}));
    }
}

void TimelineWidget::mousePressEvent(QMouseEvent* event) {
    setFocus();
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
    if (m_cutToolActive) return; // no dragging while the blade tool is active

    if (m_dragMode == DragMode::ScrubPlayhead && (event->buttons() & Qt::LeftButton)) {
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
        if (event->pos().x() < m_headerWidth && event->pos().y() >= m_rulerHeight) {
            int hoverRow = -1;
            TrackControl ctrl = trackControlAtPosition(event->pos(), &hoverRow);
            if (ctrl == TrackControl::Delete) {
                setCursor(Qt::PointingHandCursor);
                setToolTip(tr("Xóa layer này"));
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
            setCursor(Qt::ArrowCursor);
            setToolTip(QString());
        }
        return;
    }

    Track* track = m_project->timeline().findTrack(m_dragTrackId);
    Clip* clip = track ? track->findClip(m_dragClipId) : nullptr;
    if (!clip) return;

    if (!m_dragUndoSnapshotted) { pushUndo(); m_dragUndoSnapshotted = true; }

    const Ticks now = pixelToTime(event->pos().x());
    const Ticks delta = now - m_dragAnchorTime;
    // Alt disables snapping
    const bool snapEnabled = !(event->modifiers() & Qt::AltModifier);
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
    update();
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

void TimelineWidget::deleteSelectedClip() {
    if (m_selectedClipId.isEmpty()) return;
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

void TimelineWidget::deleteTrack(const QString& trackId) {
    if (trackId.isEmpty()) return;
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
    const int row = trackRowAtY(pos.y());
    const int count = static_cast<int>(m_project->timeline().tracks().size());

    QMenu menu(this);

    if (row >= 0 && row < count) {
        const int idx = trackVectorIndexForRow(row);
        Track& track = m_project->timeline().tracks()[idx];

        // Right-clicked in the header (layer controls) area
        if (pos.x() < m_headerWidth) {
            auto* titleAct = menu.addAction(QString("📁 %1").arg(track.name));
            QFont font = titleAct->font();
            font.setBold(true);
            titleAct->setFont(font);
            titleAct->setEnabled(false);
            menu.addSeparator();

            menu.addAction(tr("🗑 Xóa layer \"%1\"").arg(track.name), [this, trackId = track.id]() {
                deleteTrack(trackId);
            });

            menu.addAction(tr("✏ Đổi tên layer..."), [this, trackId = track.id, curName = track.name]() {
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

            menu.addAction(track.locked ? tr("🔓 Mở khóa layer") : tr("🔒 Khóa layer"), [this, row]() {
                toggleTrackControl(row, TrackControl::Lock);
            });

            menu.addAction(track.hidden ? tr("👁 Hiện layer") : tr("🚫 Ẩn layer"), [this, row]() {
                toggleTrackControl(row, TrackControl::Hidden);
            });

            menu.addAction(track.muted ? tr("🔊 Bật tiếng layer") : tr("🔇 Tắt tiếng layer"), [this, row]() {
                toggleTrackControl(row, TrackControl::Mute);
            });

            menu.addSeparator();
            menu.addAction(tr("➕ Thêm layer video/ảnh"), [this]() {
                pushUndo();
                m_project->timeline().addTrack(TrackType::Visual, QString("Visual %1").arg(m_project->timeline().tracks().size() + 1));
                refresh();
                emit timelineEdited();
            });
            menu.addAction(tr("➕ Thêm layer audio"), [this]() {
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
            auto* titleAct = menu.addAction(clip ? QString("🎬 %1").arg(clip->displayLabel) : tr("Clip"));
            QFont font = titleAct->font();
            font.setBold(true);
            titleAct->setFont(font);
            titleAct->setEnabled(false);
            menu.addSeparator();

            menu.addAction(tr("✂ Cắt tại playhead (S)"), this, &TimelineWidget::splitAtPlayhead);
            menu.addAction(tr("🗑 Xóa clip này (Delete)"), this, &TimelineWidget::deleteSelectedClip);
            menu.addSeparator();
            menu.addAction(tr("🗑 Xóa layer chứa clip này"), [this, trackId]() {
                deleteTrack(trackId);
            });

            menu.exec(event->globalPos());
            return;
        }
    }

    // Default right-click menu for empty timeline area
    menu.addAction(tr("➕ Thêm layer video/ảnh"), [this]() {
        pushUndo();
        m_project->timeline().addTrack(TrackType::Visual, QString("Visual %1").arg(m_project->timeline().tracks().size() + 1));
        refresh();
        emit timelineEdited();
    });
    menu.addAction(tr("➕ Thêm layer audio"), [this]() {
        pushUndo();
        m_project->timeline().addTrack(TrackType::Audio, QString("Audio %1").arg(m_project->timeline().tracks().size() + 1));
        refresh();
        emit timelineEdited();
    });
    if (!m_selectedTrackId.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("🗑 Xóa layer đang chọn"), this, &TimelineWidget::deleteSelectedTrack);
    }
    menu.exec(event->globalPos());
}

void TimelineWidget::splitAtPlayhead() {
    if (m_selectedClipId.isEmpty()) return;
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
