#pragma once
#include <QWidget>
#include "../core/TimeTypes.h"
#include "../core/Clip.h"

class QPushButton;
class QSlider;
class QLabel;
class QResizeEvent;

namespace hc {

class GLVideoWidget;
class TransformOverlay;

class AudioMeterWidget;

// Preview pane: the OpenGL video surface plus a standard transport bar
// (play/pause, scrub slider, current/total timecode) and digital audio meter.
class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    GLVideoWidget* glWidget() const { return m_gl; }
    AudioMeterWidget* audioMeter() const { return m_audioMeter; }

    QSize sizeHint() const override;

    void setAudioMeterVisible(bool visible);
    bool isAudioMeterVisible() const;

    // Show/hide the bounding-box overlay for the currently selected clip.
    // srcW/srcH = source media pixel dimensions.
    void setSelectedTransform(const Transform& t, int srcW, int srcH);
    // Update overlay transform without changing clip selection.
    void updateOverlayTransform(const Transform& t);
    void clearTransformOverlay();

public slots:
    void setPlaying(bool playing);
    void setPosition(hc::Ticks t);
    void setDuration(hc::Ticks d);
    void setTextOverlay(const QString& text);
    void setAudioLevels(float left, float right);

signals:
    void playPauseClicked();
    void seekRequested(hc::Ticks t);
    // Emitted while user drags a bounding-box handle (real-time).
    void previewTransformChanged(hc::Transform transform);
    // Emitted when user releases (undo boundary).
    void previewTransformCommitted(hc::Transform transform);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void relayoutOverlays();

    GLVideoWidget*    m_gl;
    TransformOverlay* m_transformOverlay = nullptr;
    AudioMeterWidget* m_audioMeter = nullptr;
    QPushButton*      m_playBtn;
    QPushButton*      m_meterToggleBtn = nullptr;
    QSlider*          m_slider;
    QLabel*           m_timeLabel;
    QLabel*           m_textLabel = nullptr;
    Ticks             m_duration = 0;
    bool              m_sliderBeingDragged = false;
};

} // namespace hc
