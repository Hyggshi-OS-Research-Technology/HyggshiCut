#pragma once
#include <QWidget>
#include <QImage>
#include <QPainter>
#include <QMouseEvent>

namespace hc {

// ColorWheelWidget – A single 3-way color grading wheel (Hue/Saturation disk + Luminance bar).
// Used for Shadows (Lift), Midtones (Gamma), and Highlights (Gain).
class ColorWheelWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorWheelWidget(const QString& title, QWidget* parent = nullptr);

    // Getters
    QString title() const { return m_title; }
    void setTitle(const QString& title) { m_title = title; update(); }
    double wheelX() const { return m_wheelX; }
    double wheelY() const { return m_wheelY; }
    double luma() const { return m_luma; }
    double red() const { return m_r; }
    double green() const { return m_g; }
    double blue() const { return m_b; }

    // Setters (e.g. when loading from clip/preset)
    void setValues(double r, double g, double b, double luma, bool emitSignal = true);
    void setWheelAndLuma(double x, double y, double luma, bool emitSignal = true);
    void reset(bool emitSignal = true);

signals:
    void valueChanged(double r, double g, double b, double luma);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    QSize sizeHint() const override { return QSize(160, 200); }
    QSize minimumSizeHint() const override { return QSize(130, 160); }

private:
    void updateWheelImage();
    void computeRgb();
    QRect wheelRect() const;
    QRect lumaRect() const;
    void handleMouseInWheel(const QPointF& pos);
    void handleMouseInLuma(const QPointF& pos);

    QString m_title;
    double m_wheelX = 0.0; // [-1.0, 1.0] inside unit circle
    double m_wheelY = 0.0; // [-1.0, 1.0] inside unit circle
    double m_luma = 0.0;   // [-1.0, 1.0] (0.0 = neutral center)
    double m_r = 0.0;      // [-1.0, 1.0]
    double m_g = 0.0;      // [-1.0, 1.0]
    double m_b = 0.0;      // [-1.0, 1.0]

    enum class DragTarget { None, Wheel, Luma };
    DragTarget m_dragTarget = DragTarget::None;

    QImage m_wheelImage;
    int m_cachedDiameter = 0;
};

} // namespace hc
