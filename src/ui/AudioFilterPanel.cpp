#include "AudioFilterPanel.h"
#include "../core/Project.h"
#include "../i18n/LanguageManager.h"
#include <QFormLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>

namespace hc {

namespace {
QDoubleSpinBox* makeSpin(double lo, double hi, double step, double decimals = 1) {
    auto* sb = new QDoubleSpinBox();
    sb->setRange(lo, hi);
    sb->setSingleStep(step);
    sb->setDecimals(decimals);
    return sb;
}
} // namespace

AudioFilterPanel::AudioFilterPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);

    m_volGroup = new QGroupBox(this);
    m_volForm = new QFormLayout(m_volGroup);

    m_speed = makeSpin(0.1, 10.0, 0.1);
    m_speed->setValue(1.0);
    m_speed->setSuffix("×");
    m_volForm->addRow(new QLabel(this), m_speed);

    auto* volRow = new QHBoxLayout();
    m_volume = makeSpin(0.0, 2.0, 0.05);
    m_volume->setValue(1.0);
    m_mute = new QCheckBox(m_volGroup);
    volRow->addWidget(m_volume, 1);
    volRow->addWidget(m_mute);
    m_volForm->addRow(new QLabel(this), volRow);
    outer->addWidget(m_volGroup);

    m_eqGroup = new QGroupBox(this);
    m_eqForm = new QFormLayout(m_eqGroup);
    m_eqLow = makeSpin(-12.0, 12.0, 0.5);
    m_eqForm->addRow(new QLabel(this), m_eqLow);
    m_eqMid = makeSpin(-12.0, 12.0, 0.5);
    m_eqForm->addRow(new QLabel(this), m_eqMid);
    m_eqHigh = makeSpin(-12.0, 12.0, 0.5);
    m_eqForm->addRow(new QLabel(this), m_eqHigh);
    outer->addWidget(m_eqGroup);

    m_denoiseGroup = new QGroupBox(this);
    m_denoiseForm = new QFormLayout(m_denoiseGroup);
    m_denoiseEnabled = new QCheckBox(this);
    m_denoiseForm->addRow(m_denoiseEnabled);
    m_denoiseAmount = makeSpin(0.0, 97.0, 1.0);
    m_denoiseForm->addRow(new QLabel(this), m_denoiseAmount);
    outer->addWidget(m_denoiseGroup);

    m_compGroup = new QGroupBox(this);
    m_compForm = new QFormLayout(m_compGroup);
    m_compressorEnabled = new QCheckBox(this);
    m_compForm->addRow(m_compressorEnabled);
    m_compressorThreshold = makeSpin(-60.0, 0.0, 1.0);
    m_compForm->addRow(new QLabel(this), m_compressorThreshold);
    m_compressorRatio = makeSpin(1.0, 20.0, 0.5);
    m_compForm->addRow(new QLabel(this), m_compressorRatio);
    outer->addWidget(m_compGroup);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet("color: #888;");
    outer->addWidget(m_hintLabel);
    outer->addStretch(1);

    retranslateUi();
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi();
    });

    for (auto* sb : {m_volume, m_speed, m_eqLow, m_eqMid, m_eqHigh, m_denoiseAmount, m_compressorThreshold, m_compressorRatio}) {
        connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
            if (m_updating) return;
            pushToClip();
        });
    }
    for (auto* cb : {m_mute, m_denoiseEnabled, m_compressorEnabled}) {
        connect(cb, &QCheckBox::toggled, this, [this](bool) {
            if (m_updating) return;
            pushToClip();
        });
    }

    setEnabled(false);
}

void AudioFilterPanel::setSelectedClip(Project* project, const QString& trackId, const QString& clipId) {
    m_project = project;
    m_trackId = trackId;
    m_clipId = clipId;

    Clip* clip = nullptr;
    if (m_project && !m_trackId.isEmpty() && !m_clipId.isEmpty()) {
        Track* track = m_project->timeline().findTrack(m_trackId);
        if (track) clip = track->findClip(m_clipId);
    }

    // Enabled for any clip whose source actually carries audio — video
    // clips with embedded sound included, matching what already gets
    // mixed by PlaybackController/Exporter (not restricted to Audio-track
    bool usable = false;
    if (clip) {
        if (clip->type == ClipType::Audio) {
            usable = true;
        } else {
            auto asset = m_project->findAsset(clip->assetId);
            usable = asset && asset->hasAudio();
        }
    }
    setEnabled(usable);
    if (!usable) {
        m_clipId.clear();
        return;
    }
    applyToWidgets();
}

void AudioFilterPanel::refreshFromClip() {
    if (m_clipId.isEmpty()) return;
    applyToWidgets();
}

void AudioFilterPanel::applyToWidgets() {
    if (!m_project || m_clipId.isEmpty()) return;
    Track* track = m_project->timeline().findTrack(m_trackId);
    Clip* clip = track ? track->findClip(m_clipId) : nullptr;
    if (!clip) return;

    const auto& f = clip->audioFilters;
    m_updating = true;
    m_volume->setValue(clip->volume);
    m_speed->setValue(clip->speed);
    m_mute->setChecked(clip->muted);
    m_eqLow->setValue(f.eqLowDb);
    m_eqMid->setValue(f.eqMidDb);
    m_eqHigh->setValue(f.eqHighDb);
    m_denoiseEnabled->setChecked(f.denoiseEnabled);
    m_denoiseAmount->setValue(f.denoiseAmountDb);
    m_compressorEnabled->setChecked(f.compressorEnabled);
    m_compressorThreshold->setValue(f.compressorThresholdDb);
    m_compressorRatio->setValue(f.compressorRatio);
    m_updating = false;
}

void AudioFilterPanel::pushToClip() {
    if (!m_project || m_clipId.isEmpty()) return;
    Track* track = m_project->timeline().findTrack(m_trackId);
    Clip* clip = track ? track->findClip(m_clipId) : nullptr;
    if (!clip) return;

    clip->volume = m_volume->value();
    clip->speed = m_speed->value();
    clip->muted = m_mute->isChecked();
    clip->audioFilters.eqLowDb = m_eqLow->value();
    clip->audioFilters.eqMidDb = m_eqMid->value();
    clip->audioFilters.eqHighDb = m_eqHigh->value();
    clip->audioFilters.denoiseEnabled = m_denoiseEnabled->isChecked();
    clip->audioFilters.denoiseAmountDb = m_denoiseAmount->value();
    clip->audioFilters.compressorEnabled = m_compressorEnabled->isChecked();
    clip->audioFilters.compressorThresholdDb = m_compressorThreshold->value();
    clip->audioFilters.compressorRatio = m_compressorRatio->value();

    m_project->timeline().notifyClipChanged(m_trackId);
    emit audioFiltersEdited();
}

void AudioFilterPanel::retranslateUi() {
    if (m_volGroup) m_volGroup->setTitle(LTR("audio.volSpeedGroup"));
    if (m_mute) m_mute->setText(LTR("audio.mute"));
    if (m_eqGroup) m_eqGroup->setTitle(LTR("audio.eqGroup"));
    if (m_denoiseGroup) m_denoiseGroup->setTitle(LTR("audio.denoiseGroup"));
    if (m_denoiseEnabled) m_denoiseEnabled->setText(LTR("audio.denoiseEnable"));
    if (m_compGroup) m_compGroup->setTitle(LTR("audio.compGroup"));
    if (m_compressorEnabled) m_compressorEnabled->setText(LTR("audio.compEnable"));

    if (m_volForm) {
        if (auto* l = qobject_cast<QLabel*>(m_volForm->labelForField(m_speed))) l->setText(LTR("audio.speed"));
    }
    if (m_eqForm) {
        if (auto* l = qobject_cast<QLabel*>(m_eqForm->labelForField(m_eqLow))) l->setText(LTR("audio.low"));
        if (auto* l = qobject_cast<QLabel*>(m_eqForm->labelForField(m_eqMid))) l->setText(LTR("audio.mid"));
        if (auto* l = qobject_cast<QLabel*>(m_eqForm->labelForField(m_eqHigh))) l->setText(LTR("audio.high"));
    }
    if (m_denoiseForm) {
        if (auto* l = qobject_cast<QLabel*>(m_denoiseForm->labelForField(m_denoiseAmount))) l->setText(LTR("audio.denoiseAmount"));
    }
    if (m_compForm) {
        if (auto* l = qobject_cast<QLabel*>(m_compForm->labelForField(m_compressorThreshold))) l->setText(LTR("audio.compThresh"));
        if (auto* l = qobject_cast<QLabel*>(m_compForm->labelForField(m_compressorRatio))) l->setText(LTR("audio.compRatio"));
    }
}

} // namespace hc
