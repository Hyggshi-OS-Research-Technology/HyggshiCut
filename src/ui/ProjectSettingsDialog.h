#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include "../core/Project.h"

namespace hc {

// Preset definition for aspect ratios and resolutions
struct AspectPreset {
    QString name;
    QString ratioText;
    int width;
    int height;
    QString description;
};

class ProjectSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProjectSettingsDialog(Project* project, bool isNewProject = true, QWidget* parent = nullptr);

    QString projectName() const;
    int videoWidth() const;
    int videoHeight() const;
    double frameRate() const;

private slots:
    void onPresetSelected(int index);
    void onCustomDimensionsChanged();

private:
    void setupUi(bool isNewProject);
    void updateRatioVisual();

    Project* m_project;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_presetCombo = nullptr;
    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QDoubleSpinBox* m_fpsSpin = nullptr;
    QLabel* m_ratioPreviewLabel = nullptr;
    QLabel* m_descLabel = nullptr;
    QPushButton* m_okBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
};

} // namespace hc
