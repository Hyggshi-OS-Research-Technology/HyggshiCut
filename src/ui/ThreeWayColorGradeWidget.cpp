#include "ThreeWayColorGradeWidget.h"
#include "../i18n/LanguageManager.h"
#include <QSettings>
#include <QInputDialog>
#include <QMessageBox>

namespace hc {

namespace {

struct ColorPreset {
    QString id;
    QString name;
    double liftR, liftG, liftB, liftLuma;
    double gammaR, gammaG, gammaB, gammaLuma;
    double gainR, gainG, gainB, gainLuma;
};

const std::vector<ColorPreset>& builtinPresets() {
    static const std::vector<ColorPreset> presets = {
        { "neutral", "Neutral (Default)",
          0.0, 0.0, 0.0, 0.0,
          0.0, 0.0, 0.0, 0.0,
          0.0, 0.0, 0.0, 0.0 },
        { "cinematic", "Cinematic Teal & Orange",
          -0.05, 0.02, 0.15, -0.05,
          0.0, 0.0, 0.0, 0.0,
          0.15, 0.08, -0.10, 0.05 },
        { "warm", "Warm Sunset",
          0.05, 0.02, -0.05, 0.0,
          0.10, 0.05, -0.08, 0.0,
          0.20, 0.10, -0.15, 0.05 },
        { "cool", "Cool Nordic",
          -0.08, 0.0, 0.12, -0.02,
          -0.05, 0.0, 0.10, 0.0,
          -0.05, 0.05, 0.18, 0.02 },
        { "vintage", "Vintage Film 70s",
          0.08, 0.05, 0.02, 0.05,
          0.05, 0.02, -0.02, -0.02,
          -0.05, 0.05, -0.10, -0.05 },
        { "cyberpunk", "Cyberpunk Neon (Neo-Tokyo)",
          -0.10, 0.05, 0.25, -0.08,
          0.15, -0.10, 0.15, 0.02,
          0.25, -0.05, 0.20, 0.08 },
        { "bleach", "Bleach Bypass",
          0.05, 0.05, 0.05, 0.10,
          -0.05, -0.05, -0.05, -0.05,
          -0.08, -0.08, -0.08, -0.10 },
        { "golden", "Golden Hour",
          0.02, 0.04, -0.08, -0.02,
          0.12, 0.08, -0.10, 0.04,
          0.22, 0.15, -0.20, 0.06 },
        { "horror", "Horror Green",
          -0.15, 0.10, -0.10, -0.10,
          -0.10, 0.18, -0.08, -0.05,
          -0.05, 0.12, -0.15, 0.0 }
    };
    return presets;
}

} // namespace

ThreeWayColorGradeWidget::ThreeWayColorGradeWidget(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(8);

    // ── Top Bar: Preset Selector + Save + Delete + Reset All ─────────────────
    auto* topBar = new QHBoxLayout();
    topBar->setSpacing(6);

    auto* presetLabel = new QLabel(LTR("effects.preset"), this);
    presetLabel->setStyleSheet("color: #ccc; font-weight: bold;");
    topBar->addWidget(presetLabel);

    m_presetCombo = new QComboBox(this);
    m_presetCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    topBar->addWidget(m_presetCombo, 1);

    m_saveBtn = new QPushButton(LTR("effects.savePreset"), this);
    m_saveBtn->setStyleSheet("background-color: #3b3b44; color: white; padding: 3px 8px; border-radius: 4px;");
    connect(m_saveBtn, &QPushButton::clicked, this, &ThreeWayColorGradeWidget::onSavePreset);
    topBar->addWidget(m_saveBtn);

    m_deleteBtn = new QPushButton(LTR("effects.deletePreset"), this);
    m_deleteBtn->setStyleSheet("background-color: #4a2a2a; color: white; padding: 3px 8px; border-radius: 4px;");
    connect(m_deleteBtn, &QPushButton::clicked, this, &ThreeWayColorGradeWidget::onDeletePreset);
    topBar->addWidget(m_deleteBtn);

    m_resetBtn = new QPushButton(LTR("effects.resetAll"), this);
    m_resetBtn->setStyleSheet("background-color: #2b3b55; color: white; padding: 3px 8px; border-radius: 4px; font-weight: bold;");
    connect(m_resetBtn, &QPushButton::clicked, this, &ThreeWayColorGradeWidget::resetAll);
    topBar->addWidget(m_resetBtn);

    mainLayout->addLayout(topBar);

    // ── Center: 3 Color Wheels (Shadows / Midtones / Highlights) ─────────────
    auto* wheelsLayout = new QHBoxLayout();
    wheelsLayout->setContentsMargins(0, 4, 0, 4);
    wheelsLayout->setSpacing(8);

    m_liftWheel = new ColorWheelWidget(LTR("colorgrade.shadows"), this);
    m_gammaWheel = new ColorWheelWidget(LTR("colorgrade.midtones"), this);
    m_gainWheel = new ColorWheelWidget(LTR("colorgrade.highlights"), this);

    connect(m_liftWheel, &ColorWheelWidget::valueChanged, this, &ThreeWayColorGradeWidget::onWheelChanged);
    connect(m_gammaWheel, &ColorWheelWidget::valueChanged, this, &ThreeWayColorGradeWidget::onWheelChanged);
    connect(m_gainWheel, &ColorWheelWidget::valueChanged, this, &ThreeWayColorGradeWidget::onWheelChanged);

    wheelsLayout->addWidget(m_liftWheel);
    wheelsLayout->addWidget(m_gammaWheel);
    wheelsLayout->addWidget(m_gainWheel);

    mainLayout->addLayout(wheelsLayout);

    populatePresets();
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ThreeWayColorGradeWidget::onPresetSelected);

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi();
    });
}

void ThreeWayColorGradeWidget::retranslateUi() {
    if (m_saveBtn) m_saveBtn->setText(LTR("effects.savePreset"));
    if (m_deleteBtn) m_deleteBtn->setText(LTR("effects.deletePreset"));
    if (m_resetBtn) m_resetBtn->setText(LTR("effects.resetAll"));

    if (m_liftWheel) m_liftWheel->setTitle(LTR("colorgrade.shadows"));
    if (m_gammaWheel) m_gammaWheel->setTitle(LTR("colorgrade.midtones"));
    if (m_gainWheel) m_gainWheel->setTitle(LTR("colorgrade.highlights"));

    populatePresets();
}

void ThreeWayColorGradeWidget::populatePresets() {
    m_presetCombo->blockSignals(true);
    m_presetCombo->clear();
    m_presetCombo->addItem(LTR("effects.custom"));

    for (const auto& p : builtinPresets()) {
        m_presetCombo->addItem(p.name);
    }

    // Load custom presets from QSettings
    QSettings settings("HyggshiCut", "ColorGradePresets");
    const QStringList keys = settings.childKeys();
    for (const QString& k : keys) {
        m_presetCombo->addItem("⭐ " + k, k);
    }

    m_presetCombo->blockSignals(false);
}

void ThreeWayColorGradeWidget::loadFromEffect(const Effect& effect) {
    m_updating = true;

    // Load lift
    const double lr = effect.paramValue("lift_r", effect.paramValue("lift", 0.0));
    const double lg = effect.paramValue("lift_g", effect.paramValue("lift", 0.0));
    const double lb = effect.paramValue("lift_b", effect.paramValue("lift", 0.0));
    const double ll = effect.paramValue("lift_luma", 0.0);
    m_liftWheel->setValues(lr, lg, lb, ll, false);

    // Load gamma
    const double gr = effect.paramValue("gamma_r", effect.paramValue("gamma", 0.0));
    const double gg = effect.paramValue("gamma_g", effect.paramValue("gamma", 0.0));
    const double gb = effect.paramValue("gamma_b", effect.paramValue("gamma", 0.0));
    const double gl = effect.paramValue("gamma_luma", 0.0);
    m_gammaWheel->setValues(gr, gg, gb, gl, false);

    // Load gain
    const double gar = effect.paramValue("gain_r", effect.paramValue("gain", 0.0));
    const double gag = effect.paramValue("gain_g", effect.paramValue("gain", 0.0));
    const double gab = effect.paramValue("gain_b", effect.paramValue("gain", 0.0));
    const double gal = effect.paramValue("gain_luma", 0.0);
    m_gainWheel->setValues(gar, gag, gab, gal, false);

    m_presetCombo->setCurrentIndex(0); // Custom
    m_updating = false;
}

void ThreeWayColorGradeWidget::applyToEffect(Effect& effect) const {
    effect.type = "color_grade";
    effect.params.clear();

    auto addP = [&](const QString& name, double val) {
        effect.params.push_back(EffectParameter{name, val});
    };

    addP("lift_r", m_liftWheel->red());
    addP("lift_g", m_liftWheel->green());
    addP("lift_b", m_liftWheel->blue());
    addP("lift_luma", m_liftWheel->luma());
    addP("lift", m_liftWheel->luma());

    addP("gamma_r", m_gammaWheel->red());
    addP("gamma_g", m_gammaWheel->green());
    addP("gamma_b", m_gammaWheel->blue());
    addP("gamma_luma", m_gammaWheel->luma());
    addP("gamma", m_gammaWheel->luma());

    addP("gain_r", m_gainWheel->red());
    addP("gain_g", m_gainWheel->green());
    addP("gain_b", m_gainWheel->blue());
    addP("gain_luma", m_gainWheel->luma());
    addP("gain", m_gainWheel->luma());
}

void ThreeWayColorGradeWidget::resetAll() {
    m_updating = true;
    m_liftWheel->reset(false);
    m_gammaWheel->reset(false);
    m_gainWheel->reset(false);
    m_presetCombo->setCurrentIndex(1); // Neutral
    m_updating = false;
    emit colorGradingChanged();
}

void ThreeWayColorGradeWidget::onWheelChanged() {
    if (m_updating) return;
    if (m_presetCombo->currentIndex() != 0) {
        m_presetCombo->blockSignals(true);
        m_presetCombo->setCurrentIndex(0); // Custom
        m_presetCombo->blockSignals(false);
    }
    emit colorGradingChanged();
}

void ThreeWayColorGradeWidget::onPresetSelected(int index) {
    if (index <= 0 || m_updating) return;

    const auto& bPresets = builtinPresets();
    if (index - 1 < static_cast<int>(bPresets.size())) {
        const auto& p = bPresets[index - 1];
        m_updating = true;
        m_liftWheel->setValues(p.liftR, p.liftG, p.liftB, p.liftLuma, false);
        m_gammaWheel->setValues(p.gammaR, p.gammaG, p.gammaB, p.gammaLuma, false);
        m_gainWheel->setValues(p.gainR, p.gainG, p.gainB, p.gainLuma, false);
        m_updating = false;
        emit colorGradingChanged();
        return;
    }

    // Custom saved preset
    const QString customKey = m_presetCombo->itemData(index).toString();
    if (!customKey.isEmpty()) {
        QSettings settings("HyggshiCut", "ColorGradePresets");
        const QString valStr = settings.value(customKey).toString();
        const QStringList parts = valStr.split(',');
        if (parts.size() == 12) {
            m_updating = true;
            m_liftWheel->setValues(parts[0].toDouble(), parts[1].toDouble(), parts[2].toDouble(), parts[3].toDouble(), false);
            m_gammaWheel->setValues(parts[4].toDouble(), parts[5].toDouble(), parts[6].toDouble(), parts[7].toDouble(), false);
            m_gainWheel->setValues(parts[8].toDouble(), parts[9].toDouble(), parts[10].toDouble(), parts[11].toDouble(), false);
            m_updating = false;
            emit colorGradingChanged();
        }
    }
}

void ThreeWayColorGradeWidget::onSavePreset() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Lưu Color Grading Preset"),
                                              tr("Nhập tên Preset:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    const QString key = name.trimmed();
    const QString valStr = QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12")
        .arg(m_liftWheel->red())
        .arg(m_liftWheel->green())
        .arg(m_liftWheel->blue())
        .arg(m_liftWheel->luma())
        .arg(m_gammaWheel->red())
        .arg(m_gammaWheel->green())
        .arg(m_gammaWheel->blue())
        .arg(m_gammaWheel->luma())
        .arg(m_gainWheel->red())
        .arg(m_gainWheel->green())
        .arg(m_gainWheel->blue())
        .arg(m_gainWheel->luma());

    QSettings settings("HyggshiCut", "ColorGradePresets");
    settings.setValue(key, valStr);

    populatePresets();
    for (int i = 0; i < m_presetCombo->count(); ++i) {
        if (m_presetCombo->itemData(i).toString() == key) {
            m_presetCombo->setCurrentIndex(i);
            break;
        }
    }
}

void ThreeWayColorGradeWidget::onDeletePreset() {
    const int idx = m_presetCombo->currentIndex();
    if (idx <= static_cast<int>(builtinPresets().size())) {
        QMessageBox::information(this, tr("Thông báo"), tr("Không thể xóa các preset mặc định của hệ thống."));
        return;
    }

    const QString customKey = m_presetCombo->itemData(idx).toString();
    if (customKey.isEmpty()) return;

    if (QMessageBox::question(this, tr("Xác nhận xóa"),
                              tr("Bạn có chắc muốn xóa preset \"%1\" không?").arg(customKey)) == QMessageBox::Yes) {
        QSettings settings("HyggshiCut", "ColorGradePresets");
        settings.remove(customKey);
        populatePresets();
        m_presetCombo->setCurrentIndex(0);
    }
}

} // namespace hc
