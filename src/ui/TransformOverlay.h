#pragma once
#include <QWidget>
#include <QPointF>
#include <QRectF>
#include "../core/Clip.h"

namespace hc {

// TransformOverlay renders an interactive bounding-box over the preview area.
//
// It sits transparently on top of the GLVideoWidget (same parent, raised above
// it) and translates pointer gestures into Transform changes that are emitted
// back to MainWindow via transformChanged().
//
// Coordinate mapping:
//   "widget space"  — raw pixel coordinates inside this overlay widget.
//   "clip space"    — normalised unit square centred at clip centre AFTER
//                     applying the clip's current transform.
//
// The overlay converts between the two using the same matrix the GL shader
// uses, so the drawn handles line up exactly with the rendered pixels.
class TransformOverlay : public QWidget {
    Q_OBJECT
public:
    explicit TransformOverlay(QWidget* parent = nullptr);

    // Call when a clip is selected. srcW/srcH are the source media dimensions
    // (used to compute the natural aspect ratio inside the frame, matching the
    // GL renderer's letterboxed-fit behaviour). Pass 0/0 to hide the overlay.
    void setSelectedClip(const Transform& transform, int srcW, int srcH);

    // Update the transform without changing the selected clip (e.g. spinbox
    // changed while this overlay is visible). Does NOT emit transformChanged.
    void setTransformExternal(const Transform& t);

    void clearSelection();

    bool hasSelection() const { return m_active; }

signals:
    // Emitted on the first mouse press that grabs a handle/box. Caller uses
    // it as the undo-snapshot boundary for the whole drag gesture (pushing the
    // snapshot here, rather than on release, so Undo restores the pre-drag
    // state instead of the post-drag state).
    void dragStarted();
    // Emitted while dragging — caller should write to clip + seek to re-render.
    void transformChanged(hc::Transform transform);
    // Emitted on mouse release — caller should do a final re-render.
    void transformCommitted(hc::Transform transform);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Which handle (if any) the pointer is hovering over / dragging.
    enum class HitZone {
        None,
        Inside,     // drag to translate
        Anchor,     // drag to move pivot
        RotateNW, RotateNE, RotateSE, RotateSW,  // just outside corners → rotate
        CornerNW, CornerNE, CornerSE, CornerSW,  // corner handles → scale
        EdgeN, EdgeE, EdgeS, EdgeW,               // mid-edge handles → scale one axis
    };

    // Convert normalised clip offset (-1..+1 in half-frame units) → pixel pos.
    QPointF toWidget(double nx, double ny) const;
    // Convert pixel pos → normalised clip offset.
    QPointF fromWidget(QPointF wp) const;

    // Compute the 4 corners and 4 mid-edge points of the bounding box (in
    // widget pixels), already rotated and scaled.
    void computeHandlePoints();

    HitZone hitTest(QPointF pos) const;
    Qt::CursorShape cursorForZone(HitZone z) const;
    void applyDrag(QPointF currentPos, Qt::KeyboardModifiers mods);

    // Maps a unit-square corner (±1, ±1) through the clip transform.
    QPointF cornerWidget(double nx, double ny) const;

    // --- state ---
    bool     m_active = false;
    Transform m_transform;          // current transform being edited
    Transform m_startTransform;     // transform at drag start
    int      m_srcW = 0, m_srcH = 0;
    int      m_frameW = 1, m_frameH = 1;

    // Corners and edge mid-points in widget pixels (recomputed each paint).
    QPointF m_corners[4];   // NW, NE, SE, SW  (rotated)
    QPointF m_edges[4];     // N, E, S, W  (mid-edges, rotated)
    QPointF m_anchor;       // pivot / anchor in widget pixels
    QPointF m_centre;       // bounding-box centre in widget pixels

    // Drag state
    HitZone  m_dragging   = HitZone::None;
    HitZone  m_hovering   = HitZone::None;
    QPointF  m_dragStart;           // widget pos at mouse press
    QPointF  m_dragStartAnchor;     // anchor pos at mouse press (for anchor drag)
    QPointF  m_startDragCorners[4]; // corner widget positions at drag start

    // Handle geometry constants
    static constexpr double kHandleR     = 7.0;   // corner/edge handle radius (px)
    static constexpr double kAnchorR     = 6.0;   // anchor diamond half-size
    static constexpr double kRotateZone  = 20.0;  // outer ring width for rotate hit
    static constexpr double kBorderPad   = 1.0;   // line width

    // Aspect ratio of natural letterboxed fit (srcW*frameH vs srcH*frameW).
    double naturalAspect() const;
    // Pixel size of one "unit" in each axis at identity transform.
    void naturalFitPx(double& outW, double& outH) const;
};

} // namespace hc
