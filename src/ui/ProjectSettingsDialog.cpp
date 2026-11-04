#include "ProjectSettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPainter>

namespace hc {

namespace {

std::vector<AspectPreset> aspectPresets() {
    return {
        { LTR("legacy.ae33d57b946518dd"), "16:9", 1920, 1080, LTR("legacy.d28d512eb97f33cc") },
        { LTR("legacy.6439d66bc2124b2a"), "16:9", 3840, 2160, LTR("legacy.da0f33060878a75f") },
        { LTR("legacy.f1368a9ebdd29edd"), "16:9", 1280, 720, LTR("legacy.3524cf2d585fa786") },
        { LTR("legacy.99b215e965a16375"),   "9:16", 1080, 1920, LTR("legacy.7cba18e89827c913") },
        { LTR("legacy.9824381f6ca16720"),          "9:16", 720,  1280, LTR("legacy.a75c7f50b0add8ef") },
        { LTR("legacy.b6d479ec2d8f3709"),       "1:1",  1080, 1080, LTR("legacy.6d6610d8f35015ce") },
        { LTR("legacy.739bd0f4734f5ff8"),         "4:5",  1080, 1350, LTR("legacy.5ae38e833a0d7fd4") },
        { LTR("legacy.bf599eeaaa5155ee"),   "21:9", 2560, 1080, LTR("legacy.a70ef6f6eb00ca96") },
        { LTR("legacy.ee215b25e970fc3d"),     "4:3",  1440, 1080, LTR("legacy.0f51af58dfd1454f") },
        { LTR("legacy.59d13a48f16c67d0"),          "Custom", 1920, 1080, LTR("legacy.f4a97dc460028553") }
    };
}

} // namespace

ProjectSettingsDialog::ProjectSettingsDialog(Project* project, bool isNewProject, QWidget* parent)
    : QDialog(parent), m_project(project) {
    setWindowTitle(isNewProject ? LTR("legacy.b188851e5418750e") : LTR("legacy.1a4817e9c3324e6d"));
    setMinimumWidth(480);
    setStyleSheet(
        "QDialog { background-color: #1e1e22; color: #eee; }"
        "QLabel { color: #ddd; }"
        "QGroupBox { border: 1px solid #3d3d45; border-radius: 0px; margin-top: 1.2em; font-weight: bold; color: #ff9944; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background-color: #2b2b32; color: #fff; border: 1px solid #444; border-radius: 0px; padding: 4px 8px; }"
        "QPushButton { background-color: #3b3b44; color: white; border-radius: 0px; padding: 6px 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #4a4a56; }"
    );

    setupUi(isNewProject);

    // Initial state from project
    if (m_project) {
        m_nameEdit->setText(m_project->name.isEmpty() ? LTR("legacy.992d22b2a9573f3e") : m_project->name);
        m_widthSpin->setValue(m_project->timeline().videoWidth > 0 ? m_project->timeline().videoWidth : 1920);
        m_heightSpin->setValue(m_project->timeline().videoHeight > 0 ? m_project->timeline().videoHeight : 1080);
        m_fpsSpin->setValue(m_project->timeline().frameRate > 0 ? m_project->timeline().frameRate : 30.0);

        // Try to match preset
        int curW = m_widthSpin->value();
        int curH = m_heightSpin->value();
        int foundIdx = 0;
        const auto& list = aspectPresets();
        for (size_t i = 0; i < list.size() - 1; ++i) {
            if (list[i].width == curW && list[i].height == curH) {
                foundIdx = static_cast<int>(i);
                break;
            }
        }
        m_presetCombo->setCurrentIndex(foundIdx);
    }

    updateRatioVisual();
}

void ProjectSettingsDialog::setupUi(bool isNewProject) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(16, 16, 16, 16);

    // Project Name
    auto* infoGroup = new QGroupBox(LTR("legacy.22479becfc7a324c"), this);
    auto* infoLayout = new QFormLayout(infoGroup);
    infoLayout->setContentsMargins(12, 16, 12, 12);
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(LTR("legacy.e79340c52ca498f4"));
    infoLayout->addRow(LTR("legacy.711cc3ec22c526cf"), m_nameEdit);
    mainLayout->addWidget(infoGroup);

    // Aspect ratio & resolution group
    auto* ratioGroup = new QGroupBox(LTR("legacy.b149446642bfbf1b"), this);
    auto* ratioLayout = new QVBoxLayout(ratioGroup);
    ratioLayout->setContentsMargins(12, 16, 12, 12);
    ratioLayout->setSpacing(10);

    m_presetCombo = new QComboBox(this);
    for (const auto& p : aspectPresets()) {
        m_presetCombo->addItem(QString("%1  (%2)").arg(p.name, p.ratioText));
    }
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ProjectSettingsDialog::onPresetSelected);
    ratioLayout->addWidget(m_presetCombo);

    m_descLabel = new QLabel(this);
    m_descLabel->setStyleSheet("color: #aaa; font-style: italic; font-size: 11px;");
    m_descLabel->setWordWrap(true);
    ratioLayout->addWidget(m_descLabel);

    // Custom dimensions & FPS
    auto* dimLayout = new QHBoxLayout();
    m_widthSpin = new QSpinBox(this);
    m_widthSpin->setRange(128, 7680);
    m_widthSpin->setSingleStep(16);
    m_widthSpin->setValue(1920);

    m_heightSpin = new QSpinBox(this);
    m_heightSpin->setRange(128, 4320);
    m_heightSpin->setSingleStep(16);
    m_heightSpin->setValue(1080);

    m_fpsSpin = new QDoubleSpinBox(this);
    m_fpsSpin->setRange(10.0, 120.0);
    m_fpsSpin->setSingleStep(1.0);
    m_fpsSpin->setDecimals(2);
    m_fpsSpin->setValue(30.0);

    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProjectSettingsDialog::onCustomDimensionsChanged);
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ProjectSettingsDialog::onCustomDimensionsChanged);

    dimLayout->addWidget(new QLabel(LTR("legacy.d4ecbb0032626bc3"), this));
    dimLayout->addWidget(m_widthSpin);
    dimLayout->addWidget(new QLabel("×", this));
    dimLayout->addWidget(new QLabel(tr("Cao:"), this));
    dimLayout->addWidget(m_heightSpin);
    dimLayout->addSpacing(10);
    dimLayout->addWidget(new QLabel(tr("FPS:"), this));
    dimLayout->addWidget(m_fpsSpin);

    ratioLayout->addLayout(dimLayout);

    // Visual thumbnail box
    m_ratioPreviewLabel = new QLabel(this);
    m_ratioPreviewLabel->setAlignment(Qt::AlignCenter);
    m_ratioPreviewLabel->setFixedHeight(80);
    m_ratioPreviewLabel->setStyleSheet("background-color: #141417; border: 1px dashed #444; border-radius: 0px; padding: 6px;");
    ratioLayout->addWidget(m_ratioPreviewLabel);

    mainLayout->addWidget(ratioGroup);

    // Bottom action buttons
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch(1);

    m_cancelBtn = new QPushButton(LTR("legacy.74fcd3522be7e175"), this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_okBtn = new QPushButton(isNewProject ? LTR("legacy.f6276eb1f50efb63") : LTR("legacy.ff631576384c41cd"), this);
    m_okBtn->setStyleSheet("background-color: #ff6a00; color: white; font-weight: bold; border-radius: 0px; padding: 6px 20px;");
    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addWidget(m_okBtn);
    mainLayout->addLayout(btnLayout);
}

void ProjectSettingsDialog::onPresetSelected(int index) {
    const auto& list = aspectPresets();
    if (index >= 0 && index < static_cast<int>(list.size()) - 1) {
        m_widthSpin->blockSignals(true);
        m_heightSpin->blockSignals(true);
        m_widthSpin->setValue(list[index].width);
        m_heightSpin->setValue(list[index].height);
        m_descLabel->setText(list[index].description);
        m_widthSpin->blockSignals(false);
        m_heightSpin->blockSignals(false);
    } else if (index == static_cast<int>(list.size()) - 1) {
        m_descLabel->setText(list[index].description);
    }
    updateRatioVisual();
}

void ProjectSettingsDialog::onCustomDimensionsChanged() {
    updateRatioVisual();
}

void ProjectSettingsDialog::updateRatioVisual() {
    int w = m_widthSpin->value();
    int h = m_heightSpin->value();
    if (w <= 0 || h <= 0) return;

    double ratio = static_cast<double>(w) / static_cast<double>(h);
    QString ratioStr;
    if (std::abs(ratio - 16.0 / 9.0) < 0.02) ratioStr = "16:9";
    else if (std::abs(ratio - 9.0 / 16.0) < 0.02) ratioStr = LTR("legacy.474c844f26ec058c");
    else if (std::abs(ratio - 1.0) < 0.02) ratioStr = LTR("legacy.985fdb5c224eecd8");
    else if (std::abs(ratio - 4.0 / 5.0) < 0.02) ratioStr = LTR("legacy.332e825e18b74005");
    else if (std::abs(ratio - 21.0 / 9.0) < 0.05) ratioStr = LTR("legacy.6c672d9d62efa2b2");
    else if (std::abs(ratio - 4.0 / 3.0) < 0.02) ratioStr = "4:3";
    else ratioStr = QString("%1:%2").arg(w).arg(h);

    m_ratioPreviewLabel->setText(QString(LTR("legacy.3afc0eb4b07cd1d6"))
                                 .arg(w).arg(h).arg(ratioStr).arg(m_fpsSpin->value(), 0, 'f', 2));
}

QString ProjectSettingsDialog::projectName() const {
    return m_nameEdit ? m_nameEdit->text().trimmed() : QString();
}

int ProjectSettingsDialog::videoWidth() const {
    return m_widthSpin ? m_widthSpin->value() : 1920;
}

int ProjectSettingsDialog::videoHeight() const {
    return m_heightSpin ? m_heightSpin->value() : 1080;
}

double ProjectSettingsDialog::frameRate() const {
    return m_fpsSpin ? m_fpsSpin->value() : 30.0;
}

} // namespace hc
