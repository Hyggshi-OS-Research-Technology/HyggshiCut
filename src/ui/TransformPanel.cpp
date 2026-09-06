#include "TransformPanel.h"
#include "../core/Project.h"
#include "../i18n/LanguageManager.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <algorithm>

namespace hc {

namespace {
QDoubleSpinBox* makeSpin(double lo, double hi, double step, double decimals = 2) {
    auto* sb = new QDoubleSpinBox();
    sb->setRange(lo, hi);
    sb->setSingleStep(step);
    sb->setDecimals(decimals);
    return sb;
}
} // namespace

TransformPanel::TransformPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);

    m_transformGroup = new QGroupBox(this);
    m_transformForm = new QFormLayout(m_transformGroup);

    m_x = makeSpin(-2.0, 2.0, 0.05);
    m_transformForm->addRow(new QLabel(this), m_x);
    m_y = makeSpin(-2.0, 2.0, 0.05);
    m_transformForm->addRow(new QLabel(this), m_y);
    m_scaleX = makeSpin(0.05, 5.0, 0.05);
    m_transformForm->addRow(new QLabel(this), m_scaleX);
    m_scaleY = makeSpin(0.05, 5.0, 0.05);
    m_transformForm->addRow(new QLabel(this), m_scaleY);
    m_rotation = makeSpin(-360.0, 360.0, 1.0, 1);
    m_transformForm->addRow(new QLabel(this), m_rotation);

    m_opacitySpin = makeSpin(0.0, 1.0, 0.05, 2);
    m_opacitySpin->setValue(1.0);
    m_transformForm->addRow(new QLabel(this), m_opacitySpin);

    m_blendCombo = new QComboBox(m_transformGroup);
    const QStringList blendNames = {
        "Normal", "Multiply", "Screen", "Overlay",
        "Add", "Subtract", "Darken", "Lighten",
        "Hard Light", "Soft Light", "Difference", "Exclusion",
        "Dodge", "Burn", "Saturate", "HSL Hue",
        "HSL Saturation", "HSL Color", "HSL Luminosity"
    };
    m_blendCombo->addItems(blendNames);
    m_transformForm->addRow(new QLabel(this), m_blendCombo);

    m_resetBtn = new QPushButton(m_transformGroup);
    m_transformForm->addRow(m_resetBtn);

    outer->addWidget(m_transformGroup);

    // --- Speed & Volume ---
    m_avGroup = new QGroupBox(this);
    m_avForm = new QFormLayout(m_avGroup);

    m_speedSpin = makeSpin(0.1, 10.0, 0.1);
    m_speedSpin->setValue(1.0);
    m_speedSpin->setSuffix("×");
    m_avForm->addRow(new QLabel(this), m_speedSpin);

    auto* volRow = new QHBoxLayout();
    m_volumeSpin = makeSpin(0.0, 2.0, 0.05);
    m_volumeSpin->setValue(1.0);
    m_muteCheck = new QCheckBox(m_avGroup);
    volRow->addWidget(m_volumeSpin, 1);
    volRow->addWidget(m_muteCheck);
    m_avForm->addRow(new QLabel(this), volRow);

    outer->addWidget(m_avGroup);

    // --- Keyframe controls ---
    m_kfGroup = new QGroupBox(this);
    auto* kfLayout = new QVBoxLayout(m_kfGroup);

    m_keyframeStatusLabel = new QLabel(m_kfGroup);
    m_keyframeStatusLabel->setWordWrap(true);
    m_keyframeStatusLabel->setStyleSheet("color: #aaa;");
    kfLayout->addWidget(m_keyframeStatusLabel);

    auto* kfBtnRow = new QHBoxLayout();
    m_addKeyframeBtn = new QPushButton(m_kfGroup);
    // Make the "Keyframe" button the primary action of the group: amber
    // accent to match the keyframe diamonds drawn on the timeline clips.
    m_addKeyframeBtn->setStyleSheet(
        "QPushButton { background-color: #b45309; color: #ffffff; font-weight: bold; "
        "border-radius: 4px; padding: 6px 10px; border: none; }"
        "QPushButton:hover { background-color: #d97706; }"
        "QPushButton:disabled { background-color: #3a3a40; color: #707070; }");
    m_removeKeyframeBtn = new QPushButton(m_kfGroup);
    kfBtnRow->addWidget(m_addKeyframeBtn);
    kfBtnRow->addWidget(m_removeKeyframeBtn);
    kfLayout->addLayout(kfBtnRow);

    outer->addWidget(m_kfGroup);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet("color: #888;");
    outer->addWidget(m_hintLabel);
    outer->addStretch(1);

    retranslateUi();
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi();
    });

    for (auto* sb : {m_x, m_y, m_scaleX, m_scaleY, m_rotation, m_opacitySpin, m_speedSpin, m_volumeSpin}) {
        connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
            if (m_updating) return;
            pushToClip();
        });
    }
    connect(m_blendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_updating) return;
        pushToClip();
    });
    connect(m_muteCheck, &QCheckBox::toggled, this, [this](bool) {
        if (m_updating) return;
        pushToClip();
    });
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        if (!m_project || m_clipId.isEmpty()) return;
        Track* track = m_project->timeline().findTrack(m_trackId);
        Clip* clip = track ? track->findClip(m_clipId) : nullptr;
        if (!clip) return;
        clip->transform = Transform();
        clip->transformKeyframes.clear();
        clip->opacity = 1.0;
        clip->blendMode = BlendMode::Normal;
        applyToSpinBoxes();
        updateKeyframeUi();
        m_project->timeline().notifyClipChanged(m_trackId);
        emit transformEdited();
    });

    connect(m_addKeyframeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_project || m_clipId.isEmpty()) return;
        Track* track = m_project->timeline().findTrack(m_trackId);
        Clip* clip = track ? track->findClip(m_clipId) : nullptr;
        if (!clip) return;

        Transform value;
        value.x = m_x->value();
        value.y = m_y->value();
        value.scaleX = m_scaleX->value();
        value.scaleY = m_scaleY->value();
        value.rotationDeg = m_rotation->value();
        value.opacity = m_opacitySpin->value();

        clip->setTransformKeyframe(relativeTimeForCurrentClip(), value);
        updateKeyframeUi();
        m_project->timeline().notifyClipChanged(m_trackId);
        emit transformEdited();
    });

    connect(m_removeKeyframeBtn, &QPushButton::clicked, this, [this]() {
        if (!m_project || m_clipId.isEmpty()) return;
        Track* track = m_project->timeline().findTrack(m_trackId);
        Clip* clip = track ? track->findClip(m_clipId) : nullptr;
        if (!clip) return;

        if (clip->removeTransformKeyframeNear(relativeTimeForCurrentClip())) {
            applyToSpinBoxes();
            updateKeyframeUi();
            m_project->timeline().notifyClipChanged(m_trackId);
            emit transformEdited();
        }
    });

    updateKeyframeUi();
    setEnabled(false);
}

void TransformPanel::setSelectedClip(Project* project, const QString& trackId, const QString& clipId) {
    m_project = project;
    m_trackId = trackId;
    m_clipId = clipId;

    Clip* clip = nullptr;
    if (m_project && !m_trackId.isEmpty() && !m_clipId.isEmpty()) {
        Track* track = m_project->timeline().findTrack(m_trackId);
        if (track) clip = track->findClip(m_clipId);
    }

    const bool usable = (clip != nullptr);
    setEnabled(usable);
    if (!usable) {
        m_clipId.clear();
        return;
    }

    const bool isVisual = (clip->type == ClipType::Video || clip->type == ClipType::Image || clip->type == ClipType::Text);
    for (auto* w : {static_cast<QWidget*>(m_x), static_cast<QWidget*>(m_y), static_cast<QWidget*>(m_scaleX),
                    static_cast<QWidget*>(m_scaleY), static_cast<QWidget*>(m_rotation), static_cast<QWidget*>(m_opacitySpin),
                    static_cast<QWidget*>(m_blendCombo), static_cast<QWidget*>(m_resetBtn),
                    static_cast<QWidget*>(m_addKeyframeBtn), static_cast<QWidget*>(m_removeKeyframeBtn)}) {
        if (w) w->setEnabled(isVisual);
    }

    if (clip->type == ClipType::Audio) {
        m_hintLabel->setText(tr("Đang chọn clip âm thanh. Bạn có thể chỉnh Tốc độ phát (Speed), Âm lượng (Volume) và Tắt tiếng (Mute)."));
    } else {
        m_hintLabel->setText(tr("Chọn một clip video/ảnh/chữ trên timeline để chỉnh vị trí, kích thước, góc xoay, tốc độ và âm lượng."));
    }

    applyToSpinBoxes();
    updateKeyframeUi();
}

void TransformPanel::refreshFromClip() {
    if (m_clipId.isEmpty()) return;
    applyToSpinBoxes();
    updateKeyframeUi();
}

void TransformPanel::setCurrentTime(Ticks t) {
    m_currentTime = t;
    if (m_clipId.isEmpty()) return;
    // Only clips with keyframes actually change over time; re-reading for
    // static-transform clips is harmless (same value every time) but cheap
    // enough to just always do, so scrubbing always shows the true frame.
    applyToSpinBoxes();
    updateKeyframeUi();
}

void TransformPanel::applyToSpinBoxes() {
    if (!m_project || m_clipId.isEmpty()) return;
    Track* track = m_project->timeline().findTrack(m_trackId);
    Clip* clip = track ? track->findClip(m_clipId) : nullptr;
    if (!clip) return;

    const Transform tf = clip->transformAt(m_currentTime);

    m_updating = true;
    m_x->setValue(tf.x);
    m_y->setValue(tf.y);
    m_scaleX->setValue(tf.scaleX);
    m_scaleY->setValue(tf.scaleY);
    m_rotation->setValue(tf.rotationDeg);
    m_opacitySpin->setValue(clip->hasTransformKeyframes() ? tf.opacity : clip->opacity);
    m_blendCombo->setCurrentIndex(static_cast<int>(clip->blendMode));
    m_speedSpin->setValue(clip->speed);
    m_volumeSpin->setValue(clip->volume);
    m_muteCheck->setChecked(clip->muted);
    m_updating = false;
}

void TransformPanel::pushToClip() {
    if (!m_project || m_clipId.isEmpty()) return;
    Track* track = m_project->timeline().findTrack(m_trackId);
    Clip* clip = track ? track->findClip(m_clipId) : nullptr;
    if (!clip) return;

    Transform value;
    value.x = m_x->value();
    value.y = m_y->value();
    value.scaleX = m_scaleX->value();
    value.scaleY = m_scaleY->value();
    value.rotationDeg = m_rotation->value();
    value.opacity = m_opacitySpin->value();

    if (clip->hasTransformKeyframes()) {
        // Already animated: dragging a spin box updates/creates the
        // keyframe at the playhead instead of touching the (now-unused)
        // static transform, so the animation keeps working.
        clip->setTransformKeyframe(relativeTimeForCurrentClip(), value);
        updateKeyframeUi();
    } else {
        clip->transform = value;
        clip->opacity = m_opacitySpin->value();
    }

    clip->blendMode = static_cast<BlendMode>(m_blendCombo->currentIndex());
    clip->speed = m_speedSpin->value();
    clip->volume = m_volumeSpin->value();
    clip->muted = m_muteCheck->isChecked();

    m_project->timeline().notifyClipChanged(m_trackId);
    emit transformEdited();
}

void TransformPanel::setTransformExternal(const Transform& t) {
    m_updating = true;
    m_x->setValue(t.x);
    m_y->setValue(t.y);
    m_scaleX->setValue(t.scaleX);
    m_scaleY->setValue(t.scaleY);
    m_rotation->setValue(t.rotationDeg);
    m_updating = false;
}

void TransformPanel::updateKeyframeUi() {
    if (!m_project || m_clipId.isEmpty()) {
        m_keyframeStatusLabel->setText(tr("Chưa có clip nào được chọn."));
        m_addKeyframeBtn->setEnabled(false);
        m_removeKeyframeBtn->setEnabled(false);
        return;
    }
    Track* track = m_project->timeline().findTrack(m_trackId);
    Clip* clip = track ? track->findClip(m_clipId) : nullptr;
    if (!clip) {
        m_addKeyframeBtn->setEnabled(false);
        m_removeKeyframeBtn->setEnabled(false);
        return;
    }

    m_addKeyframeBtn->setEnabled(true);
    const Ticks relTime = relativeTimeForCurrentClip();
    const bool onKeyframe = clip->hasTransformKeyframeNear(relTime);
    m_removeKeyframeBtn->setEnabled(onKeyframe);

    if (!clip->hasTransformKeyframes()) {
        m_keyframeStatusLabel->setText(tr("Chưa có keyframe — layer đang tĩnh (không hoạt ảnh)."));
    } else if (onKeyframe) {
        m_keyframeStatusLabel->setText(tr("◆ %1 keyframe — playhead đang ở đúng một keyframe.")
            .arg(clip->transformKeyframes.size()));
    } else {
        m_keyframeStatusLabel->setText(tr("%1 keyframe — giá trị hiện tại là nội suy giữa hai keyframe.")
            .arg(clip->transformKeyframes.size()));
    }
}

Ticks TransformPanel::relativeTimeForCurrentClip() const {
    if (!m_project || m_clipId.isEmpty()) return 0;
    Track* track = m_project->timeline().findTrack(m_trackId);
    Clip* clip = track ? track->findClip(m_clipId) : nullptr;
    if (!clip) return 0;

    const Ticks dur = clip->timelineDuration();
    Ticks rel = m_currentTime - clip->timelineStart;
    rel = std::clamp<Ticks>(rel, 0, std::max<Ticks>(0, dur));
    return rel;
}

void TransformPanel::retranslateUi() {
    if (m_transformGroup) m_transformGroup->setTitle(LTR("transform.group"));
    if (m_resetBtn) m_resetBtn->setText(LTR("transform.reset"));
    if (m_avGroup) m_avGroup->setTitle(LTR("transform.speedVol"));
    if (m_muteCheck) m_muteCheck->setText(LTR("transform.mute"));
    if (m_kfGroup) m_kfGroup->setTitle(LTR("transform.keyframeGroup"));
    if (m_addKeyframeBtn) m_addKeyframeBtn->setText("◆ " + LTR("transform.addKf"));
    if (m_removeKeyframeBtn) m_removeKeyframeBtn->setText(LTR("transform.resetKf"));

    // Form labels
    if (m_transformForm) {
        if (auto* l = qobject_cast<QLabel*>(m_transformForm->labelForField(m_x))) l->setText(LTR("transform.posX"));
        if (auto* l = qobject_cast<QLabel*>(m_transformForm->labelForField(m_y))) l->setText(LTR("transform.posY"));
        if (auto* l = qobject_cast<QLabel*>(m_transformForm->labelForField(m_scaleX))) l->setText(LTR("transform.scaleX"));
        if (auto* l = qobject_cast<QLabel*>(m_transformForm->labelForField(m_scaleY))) l->setText(LTR("transform.scaleY"));
        if (auto* l = qobject_cast<QLabel*>(m_transformForm->labelForField(m_rotation))) l->setText(LTR("transform.rotation"));
        if (auto* l = qobject_cast<QLabel*>(m_transformForm->labelForField(m_opacitySpin))) l->setText(LTR("transform.opacity"));
        if (auto* l = qobject_cast<QLabel*>(m_transformForm->labelForField(m_blendCombo))) l->setText(LTR("transform.blendMode"));
    }

    if (m_avForm) {
        if (auto* l = qobject_cast<QLabel*>(m_avForm->labelForField(m_speedSpin))) l->setText(LTR("transform.speed"));
    }

    updateKeyframeUi();
}

} // namespace hc
