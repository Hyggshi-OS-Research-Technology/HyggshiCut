#pragma once
#include <QWidget>
#include <QElapsedTimer>
#include <limits>
#include <optional>
#include "../core/Project.h"

class QMenu;

class QScrollArea;
class QWheelEvent;

namespace hc {

// The multi-track editing surface. Owns no data itself — it reads/mutates
// the Project's Timeline directly (this widget IS the timeline editor, so
// direct mutation with an `update()` + a single `timelineEdited()` signal
// is simpler than round-tripping every drag through command objects for
// a v1). Horizontal scrolling is delegated to an outer QScrollArea: this
// widget reports its true pixel width via sizeHint() and never scrolls
// itself.
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(Project* project, QWidget* parent = nullptr);
    void setProject(Project* project);

    void setPlayheadTime(Ticks t);
    Ticks playheadTime() const { return m_playheadTime; }

    void setZoom(double pixelsPerSecond);
    double zoom() const { return m_pixelsPerSecond; }

    // Zoom anchored to a specific timeline time, keeping `cursorWidgetX`
    // (a widget x coordinate, as reported by mouse events) fixed on screen.
    // Pass -1 for cursorWidgetX to left-anchor instead (plain zoom-in/out).
    void zoomAt(Ticks anchorTime, double factor, int cursorWidgetX = -1);
    // Zoom around the centre of the visible viewport (used by the View menu
    // and toolbar zoom buttons).
    void zoomBy(double factor);
    // Fit the whole timeline duration into the visible viewport width.
    void zoomToFit();
    // Scroll the viewport (if any) so the playhead is on screen.
    void ensurePlayheadVisible();

    QString selectedClipId() const { return m_selectedClipId; }
    QString selectedTrackId() const { return m_selectedTrackId; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return QSize(100, 50); }

    bool cutToolActive() const { return m_cutToolActive; }

    // Frame-step size at the project frame rate (used by nudge actions).
    Ticks frameStepTicks() const;

    // Toggle edge/playhead snapping during drag operations (default on).
    void setSnapEnabled(bool enabled);
    bool snapEnabled() const { return m_snapEnabled; }

    // Called by the outer scroll area whenever its viewport is resized.
    // Tracks fill the available height smoothly when resized up or down,
    // and when there are too many tracks to fit below kMinTrackHeight,
    // the outer QScrollArea provides vertical scrolling.
    void setAvailableHeight(int h);

public slots:
    void refresh(); // call after external timeline mutation (e.g. split via toolbar)
    void deleteSelectedClip();
    void deleteSelectedTrack();
    void deleteTrack(const QString& trackId);
    void splitAtPlayhead();
    void copySelectedClip();
    void pasteClip();
    void duplicateSelectedClip();
    void rippleDeleteSelectedClip();
    void nudgeSelectedClip(Ticks deltaTicks);
    void setCutToolActive(bool active);
    void clearSelection();
    Ticks pixelToTime(int x) const;
    int timeToPixel(Ticks t) const;

signals:
    void seekRequested(hc::Ticks t);
    void timelineEdited();          // clip moved/trimmed/added/removed
    void selectionChanged(QString clipId, QString trackId);
    void togglePlaybackRequested(); // e.g. Space pressed while timeline focused
    void zoomChanged(double pixelsPerSecond); // zoom level changed (wheel/menu/fit)

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class DragMode { None, MoveClip, TrimLeft, TrimRight, FadeIn, FadeOut, ScrubPlayhead, ReorderTrack, ToggleTrackControl };

    enum class TrackControl { Mute = 0, Hidden = 1, Lock = 2, Delete = 3, None = -1 };

    int trackRowAtY(int y) const;             // visual row, 0 = top
    int trackVectorIndexForRow(int row) const; // maps visual row -> Track::tracks() index
    QRect clipRect(int trackVectorIndex, const Clip& clip) const;
    void hitTest(const QPoint& pos, QString* outTrackId, QString* outClipId, DragMode* outMode) const;
    // Hit-tests the small crossfade marker drawn at the boundary between two
    // adjacent clips on a Visual track (see paintEvent). Returns true and
    // fills the two clip ids if `pos` lands on one; `curClipId` is always
    // the later (right-hand / incoming) clip of the pair, since that's the
    // one whose transitionInDuration/timelineStart actually get edited.
    bool transitionMarkerAt(const QPoint& pos, QString* outTrackId,
                             QString* outPrevClipId, QString* outCurClipId) const;
    void toggleTransitionAt(Track* track, Clip* prevClip, Clip* curClip);
    // Populates `menu` with the transition editor for the pair
    // (prevClipId → incomingClipId): type, direction, dip colour, duration,
    // and removal. Used by the right-click-on-marker context menu.
    void buildTransitionMenu(QMenu* menu, Track* track,
                             const QString& prevClipId, const QString& incomingClipId);
    QString formatDurationShort(Ticks t) const;
    void recomputeTrackHeight();
    TrackControl trackControlAtPosition(const QPoint& pos, int* outRow) const;
    QRect trackControlRect(int row, TrackControl control) const;
    void toggleTrackControl(int row, TrackControl control);
    void pushUndo();
    // The outer QScrollArea (found by walking up from the viewport), used to
    // drive horizontal/vertical scrolling and cursor-anchored zooming.
    QScrollArea* outerScrollArea() const;
    // Trigger a minimal repaint covering only the regions dirtied by a drag
    // event (the affected track row + the old/new snap line columns). Avoids a
    // full widget repaint on every mouseMoveEvent.
    void invalidateDragRegion(int trackRow, int oldSnapX, int newSnapX);

    // --- Snap helpers ---
    QList<Ticks> computeSnapPoints(const QString& excludeClipId = {}) const;
    Ticks snapTime(Ticks t, const QList<Ticks>& pts, bool enabled) const;
    static constexpr int kSnapThresholdPx = 8;
    static constexpr double kMinZoomPxPerSec = 5.0;
    static constexpr double kMaxZoomPxPerSec = 2000.0;

    Project* m_project;
    Ticks m_playheadTime = 0;
    double m_pixelsPerSecond = 60.0;
    int m_trackHeight = 56;         // effective per-track row height, recomputed by recomputeTrackHeight()
    static constexpr int kMinTrackHeight = 32; // floor: below this we scroll instead of shrinking further
    static constexpr int kMaxTrackHeight = 800; // allow tracks to scale up freely when timeline is expanded
    int m_availableHeight = 0;      // last known viewport height from the outer scroll area, 0 = unknown yet
    int m_rulerHeight = 32;
    int m_headerWidth = 220; // left "layers" column: track badge, name + sleek control buttons

    bool m_cutToolActive = false;

    DragMode m_dragMode = DragMode::None;
    QString m_dragTrackId, m_dragClipId;
    Ticks m_dragAnchorTime = 0;   // playhead time under the cursor when drag started
    Ticks m_dragClipOrigStart = 0;
    Ticks m_dragClipOrigIn = 0, m_dragClipOrigOut = 0;
    // Neighbor clamp for the clip being dragged/trimmed, computed once at
    // mouse-press time from the *other* clips on the same track (excludes
    // the dragged clip itself). Prevents Move/TrimLeft/TrimRight from ever
    // producing an overlap within a single track.
    Ticks m_dragLeftBound = 0;                    // end of nearest clip to the left, or 0
    Ticks m_dragRightBound = std::numeric_limits<Ticks>::max(); // start of nearest clip to the right, or +inf
    int m_dragTrackCurrentIndex = -1; // live vector index of track being reordered
    bool m_dragUndoSnapshotted = false; // one line-item snapshot per drag gesture
    Qt::KeyboardModifiers m_dragModifier = Qt::NoModifier; // Ctrl/Shift modifier

    // --- Snap state ---
    Ticks m_lastSnapTarget = -1;  // <0 = no active snap, >=0 = draw yellow snap line at this tick

    // --- Snap cache: computed once at drag-start, reused every mouseMoveEvent ---
    QList<Ticks> m_dragSnapPoints; // populated at press, cleared at release

    // --- Scrub playhead throttle timer ---
    QElapsedTimer m_scrubThrottleTimer;

    // --- Fade drag state ---
    Ticks m_dragFadeOrigDuration = 0; // original fadeIn/fadeOut when drag started

    // --- Hover tracking for track controls ---
    int m_hoverTrackRow = -1;
    TrackControl m_hoverControl = TrackControl::None;

    QString m_selectedClipId, m_selectedTrackId;

    // --- Clip clipboard & snap preference ---
    // A deep copy of the last copied clip (id regenerated on paste).
    std::optional<Clip> m_clipboard;
    bool m_snapEnabled = true;
};

} // namespace hc
