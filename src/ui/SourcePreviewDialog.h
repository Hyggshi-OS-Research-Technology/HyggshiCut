#pragma once
#include <QDialog>
#include <memory>

class QPushButton;
class QSlider;
class QLabel;

namespace hc {

class MpvPlayer;
class MpvVideoWidget;

// Non-modal "source monitor": double-click an asset in the Media Pool to
// play the raw file here with real audio+video playback via libmpv, instead
// of only the silent frame-by-frame scrub the timeline preview offers.
class SourcePreviewDialog : public QDialog {
    Q_OBJECT
public:
    explicit SourcePreviewDialog(const QString& filePath, const QString& displayName, QWidget* parent = nullptr);
    ~SourcePreviewDialog() override;

private slots:
    void onPositionChanged(double seconds);
    void onDurationChanged(double seconds);
    void onPausedChanged(bool paused);

private:
    std::unique_ptr<MpvPlayer> m_player;
    MpvVideoWidget* m_gl = nullptr;
    QPushButton* m_playBtn = nullptr;
    QSlider* m_slider = nullptr;
    QLabel* m_timeLabel = nullptr;
    double m_duration = 0.0;
    bool m_sliderBeingDragged = false;
};

} // namespace hc
