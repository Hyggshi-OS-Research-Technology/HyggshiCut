#pragma once
#include <QWidget>
#include <QString>
#include <QGroupBox>
#include <QFormLayout>

class QDoubleSpinBox;
class QCheckBox;
class QLabel;

namespace hc {

class Project;

// Small properties dock for editing the selected clip's audio processing
// chain (EQ / noise reduction / compressor) — see
// Clip::AudioFilterSettings and audio/AudioFilterDesc.h. Enabled whenever
// the selected clip's source asset actually has an audio stream (this
// includes video clips with embedded audio, not just Audio-track clips —
// matches what PlaybackController/Exporter already mix).
class AudioFilterPanel : public QWidget {
    Q_OBJECT
public:
    explicit AudioFilterPanel(QWidget* parent = nullptr);

    // Called by MainWindow whenever the timeline selection changes.
    void setSelectedClip(Project* project, const QString& trackId, const QString& clipId);

    // Re-reads the selected clip's filter settings without changing the
    // selection (e.g. after an undo/redo).
    void refreshFromClip();

public slots:
    void retranslateUi();

signals:
    // Emitted after the panel edits the clip's AudioFilterSettings in
    // place. MainWindow should mark the project modified in response.
    void audioFiltersEdited();

private:
    void applyToWidgets();
    void pushToClip();

    Project* m_project = nullptr;
    QString m_trackId, m_clipId;

    QGroupBox* m_volGroup = nullptr;
    QGroupBox* m_eqGroup = nullptr;
    QGroupBox* m_denoiseGroup = nullptr;
    QGroupBox* m_compGroup = nullptr;
    QFormLayout* m_volForm = nullptr;
    QFormLayout* m_eqForm = nullptr;
    QFormLayout* m_denoiseForm = nullptr;
    QFormLayout* m_compForm = nullptr;

    QDoubleSpinBox* m_volume = nullptr;
    QCheckBox* m_mute = nullptr;
    QDoubleSpinBox* m_speed = nullptr;

    QDoubleSpinBox* m_eqLow = nullptr;
    QDoubleSpinBox* m_eqMid = nullptr;
    QDoubleSpinBox* m_eqHigh = nullptr;

    QCheckBox* m_denoiseEnabled = nullptr;
    QDoubleSpinBox* m_denoiseAmount = nullptr;

    QCheckBox* m_compressorEnabled = nullptr;
    QDoubleSpinBox* m_compressorThreshold = nullptr;
    QDoubleSpinBox* m_compressorRatio = nullptr;

    QLabel* m_hintLabel = nullptr;

    bool m_updating = false; // guards against feedback loops while setting widget values programmatically
};

} // namespace hc
