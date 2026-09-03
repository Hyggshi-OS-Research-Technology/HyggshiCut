#pragma once
#include <QWidget>
#include <QString>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include "../core/TimeTypes.h"
#include "../core/Clip.h"

class QDoubleSpinBox;
class QPushButton;
class QComboBox;
class QCheckBox;

namespace hc {

class Project;

// Small properties dock for editing the selected clip's Free Transform
// (position / scale / rotation). This is the fix for tracks stacked above
// others always fully covering them: with the default transform a clip
// still fills the frame (identical to old behaviour), but shrinking and/or
// moving it here reveals the layers underneath, i.e. picture-in-picture.
// Disabled when nothing is selected or the selection isn't a video/image
// clip (audio/text clips have no visual transform).
//
// Also drives per-clip layer *keyframe* animation: "Thêm keyframe" stores
// the current spin box values as a keyframe at the playhead's position
// (relative to the clip), so position/scale/rotation can animate over the
// clip's lifetime instead of staying static. Once a clip has at least one
// keyframe, editing the spin boxes updates/creates a keyframe at the
// current playhead time rather than the old single static transform.
class TransformPanel : public QWidget {
    Q_OBJECT
public:
    explicit TransformPanel(QWidget* parent = nullptr);

    // Called by MainWindow whenever the timeline selection changes.
    void setSelectedClip(Project* project, const QString& trackId, const QString& clipId);

    // Re-reads the selected clip's transform without changing the
    // selection (e.g. after an undo/redo).
    void refreshFromClip();

    // Called by MainWindow whenever the playhead moves (seek, scrub, or
    // playback tick). Used to know WHERE a new keyframe should land, and to
    // show the interpolated transform for the selected clip at that instant.
    void setCurrentTime(Ticks t);

    // Called by MainWindow when the overlay bounding box changed the transform
    // externally. Updates spin boxes without triggering pushToClip() (to avoid
    // feedback loops); the clip itself is already updated by the caller.
    void setTransformExternal(const Transform& t);

public slots:
    void retranslateUi();

signals:
    // Emitted after the panel edits the clip's transform in place.
    // MainWindow should refresh the preview/timeline in response.
    void transformEdited();

private:
    void applyToSpinBoxes();
    void pushToClip();
    void updateKeyframeUi();
    Ticks relativeTimeForCurrentClip() const; // playhead time, clamped to [0, clip duration)

    Project* m_project = nullptr;
    QString m_trackId, m_clipId;
    Ticks m_currentTime = 0;

    QGroupBox* m_transformGroup = nullptr;
    QGroupBox* m_avGroup = nullptr;
    QGroupBox* m_kfGroup = nullptr;
    QFormLayout* m_transformForm = nullptr;
    QFormLayout* m_avForm = nullptr;

    QDoubleSpinBox* m_x = nullptr;
    QDoubleSpinBox* m_y = nullptr;
    QDoubleSpinBox* m_scaleX = nullptr;
    QDoubleSpinBox* m_scaleY = nullptr;
    QDoubleSpinBox* m_rotation = nullptr;
    QDoubleSpinBox* m_opacitySpin = nullptr;
    QComboBox* m_blendCombo = nullptr;
    QDoubleSpinBox* m_speedSpin = nullptr;
    QDoubleSpinBox* m_volumeSpin = nullptr;
    QCheckBox* m_muteCheck = nullptr;
    QPushButton* m_resetBtn = nullptr;
    QLabel* m_hintLabel = nullptr;

    QLabel* m_keyframeStatusLabel = nullptr;
    QPushButton* m_addKeyframeBtn = nullptr;
    QPushButton* m_removeKeyframeBtn = nullptr;

    bool m_updating = false; // guards against feedback loops while setting spin box values programmatically
};

} // namespace hc
