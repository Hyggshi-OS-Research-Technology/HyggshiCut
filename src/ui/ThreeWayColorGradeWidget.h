#pragma once
#include <QWidget>
#include <QComboBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include "ColorWheelWidget.h"
#include "../core/Clip.h"

namespace hc {

// ThreeWayColorGradeWidget – Full 3-Way Color Wheels studio with Shadows (Lift),
// Midtones (Gamma), Highlights (Gain) and Preset management.
class ThreeWayColorGradeWidget : public QWidget {
    Q_OBJECT
public:
    explicit ThreeWayColorGradeWidget(QWidget* parent = nullptr);

    void loadFromEffect(const Effect& effect);
    void applyToEffect(Effect& effect) const;
    void resetAll();

public slots:
    void retranslateUi();

signals:
    void colorGradingChanged();

private slots:
    void onPresetSelected(int index);
    void onSavePreset();
    void onDeletePreset();
    void onWheelChanged();

private:
    void populatePresets();

    QComboBox* m_presetCombo = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_resetBtn = nullptr;

    ColorWheelWidget* m_liftWheel = nullptr;
    ColorWheelWidget* m_gammaWheel = nullptr;
    ColorWheelWidget* m_gainWheel = nullptr;

    bool m_updating = false;
};

} // namespace hc
