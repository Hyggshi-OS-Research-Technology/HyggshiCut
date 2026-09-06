#include "EffectsPanel.h"
#include "../plugin/PluginManager.h"
#include "../i18n/LanguageManager.h"
#include <QMenu>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>

namespace hc {

namespace {

struct EffectTypeInfo {
    QString id;
    QString nameKey;
    std::vector<std::pair<QString, QString>> params; // param name, display label
    std::vector<std::tuple<double, double, double, double>> paramRanges; // min, max, default, step
};

const std::vector<EffectTypeInfo>& availableEffectTypes() {
    static const std::vector<EffectTypeInfo> types = {
        { "color_grade", "effects.type.color_grade",
          {}, {} },
        { "crop", "effects.type.crop",
          { {"left", "Left"},
            {"top", "Top"},
            {"right", "Right"},
            {"bottom", "Bottom"} },
          { {0.0, 0.9, 0.0, 0.01}, {0.0, 0.9, 0.0, 0.01}, {0.0, 0.9, 0.0, 0.01}, {0.0, 0.9, 0.0, 0.01} } },
        { "brightness", "effects.type.brightness",
          { {"amount", "Amount"} },
          { {-1.0, 1.0, 0.0, 0.05} } },
        { "contrast", "effects.type.contrast",
          { {"amount", "Amount"} },
          { {0.0, 3.0, 1.0, 0.05} } },
        { "saturation", "effects.type.saturation",
          { {"amount", "Amount"} },
          { {0.0, 3.0, 1.0, 0.05} } },
        { "hue_rotate", "effects.type.hue_rotate",
          { {"degrees", "Degrees"} },
          { {0.0, 360.0, 0.0, 1.0} } },
        { "blur", "effects.type.blur",
          { {"radius", "Radius"} },
          { {0.0, 30.0, 5.0, 1.0} } },
        { "sharpen", "effects.type.sharpen",
          { {"amount", "Amount"} },
          { {0.0, 3.0, 1.0, 0.1} } },
        { "vignette", "effects.type.vignette",
          { {"strength", "Strength"}, {"radius", "Radius"} },
          { {0.0, 1.0, 0.5, 0.05}, {0.1, 1.0, 0.75, 0.05} } },
        { "invert", "effects.type.invert",
          {}, {} },
        { "sepia", "effects.type.sepia",
          { {"amount", "Amount"} },
          { {0.0, 1.0, 0.8, 0.05} } }
    };
    return types;
}

const EffectTypeInfo* findEffectType(const QString& id) {
    for (const auto& t : availableEffectTypes()) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

QString getEffectTypeName(const QString& id) {
    QString key = "effects.type." + id;
    QString val = LTR(key);
    if (val != key) return val;
    const auto* info = findEffectType(id);
    return info ? LTR(info->nameKey) : id;
}

} // namespace

EffectsPanel::EffectsPanel(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(4);

    // ── 1. Left Pane: Filters List ───────────────────────────────────────────
    auto* leftWidget = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(4);

    auto* headerLabel = new QLabel(LTR("effects.filtersLabel"), leftWidget);
    headerLabel->setStyleSheet("font-weight: bold; color: #ff9944; padding: 2px 4px;");
    leftLayout->addWidget(headerLabel);

    m_filterList = new QListWidget(leftWidget);
    m_filterList->setStyleSheet(
        "QListWidget {"
        "  background-color: #1a1a1e;"
        "  border: 1px solid #33333a;"
        "  border-radius: 4px;"
        "  padding: 2px;"
        "}"
        "QListWidget::item {"
        "  padding: 5px 6px;"
        "  border-radius: 3px;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: #2b5b84;"
        "  color: white;"
        "  font-weight: bold;"
        "}"
    );
    connect(m_filterList, &QListWidget::currentRowChanged, this, &EffectsPanel::onFilterListSelectionChanged);
    connect(m_filterList, &QListWidget::itemChanged, this, &EffectsPanel::onFilterItemChanged);
    leftLayout->addWidget(m_filterList, 1);

    // Bottom Action Bar (+ and - buttons)
    auto* btnBar = new QHBoxLayout();
    btnBar->setContentsMargins(0, 0, 0, 0);
    btnBar->setSpacing(4);

    m_addBtn = new QPushButton(LTR("effects.addBtn"), leftWidget);
    m_addBtn->setStyleSheet("background-color: #255d36; color: white; font-weight: bold; padding: 4px 8px; border-radius: 3px;");
    connect(m_addBtn, &QPushButton::clicked, this, &EffectsPanel::onAddEffectMenuRequested);

    m_removeBtn = new QPushButton(LTR("effects.removeBtn"), leftWidget);
    m_removeBtn->setStyleSheet("background-color: #5d2525; color: white; font-weight: bold; padding: 4px 8px; border-radius: 3px;");
    connect(m_removeBtn, &QPushButton::clicked, this, &EffectsPanel::onRemoveSelectedEffect);

    btnBar->addWidget(m_addBtn);
    btnBar->addWidget(m_removeBtn);
    btnBar->addStretch(1);
    leftLayout->addLayout(btnBar);

    splitter->addWidget(leftWidget);

    // ── 2. Right Pane: Stacked Editor ────────────────────────────────────────
    m_editorStack = new QStackedWidget(splitter);

    // Page 0: Empty Placeholder
    m_noSelectionLabel = new QLabel(LTR("effects.noSelection"), m_editorStack);
    m_noSelectionLabel->setAlignment(Qt::AlignCenter);
    m_noSelectionLabel->setStyleSheet("color: #777; font-style: italic; padding: 20px;");
    m_editorStack->addWidget(m_noSelectionLabel);

    // Page 1: 3-Way Color Wheels Studio
    m_colorGradeWidget = new ThreeWayColorGradeWidget(m_editorStack);
    connect(m_colorGradeWidget, &ThreeWayColorGradeWidget::colorGradingChanged, this, &EffectsPanel::onColorGradingChanged);
    m_editorStack->addWidget(m_colorGradeWidget);

    // Page 2: Generic Effect Parameter Inspector
    m_genericEditorScroll = new QScrollArea(m_editorStack);
    m_genericEditorScroll->setWidgetResizable(true);
    m_genericEditorScroll->setFrameShape(QFrame::NoFrame);
    m_genericEditorContainer = new QWidget(m_genericEditorScroll);
    m_genericEditorScroll->setWidget(m_genericEditorContainer);
    m_editorStack->addWidget(m_genericEditorScroll);

    splitter->addWidget(m_editorStack);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    mainLayout->addWidget(splitter);

    retranslateUi();
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi();
    });

    setEnabled(false);
}

void EffectsPanel::retranslateUi() {
    if (m_addBtn) m_addBtn->setText(LTR("effects.addBtn"));
    if (m_removeBtn) m_removeBtn->setText(LTR("effects.removeBtn"));
    if (m_noSelectionLabel) m_noSelectionLabel->setText(LTR("effects.noSelection"));
    refreshFilterList(m_filterList ? m_filterList->currentRow() : -1);
}

void EffectsPanel::setClip(Clip* clip) {
    m_clip = clip;
    const bool usable = (clip != nullptr && (clip->type == ClipType::Video || clip->type == ClipType::Image || clip->type == ClipType::Text));
    setEnabled(usable);
    refreshFilterList(0);
}

void EffectsPanel::refreshFilterList(int selectIndex) {
    m_updating = true;
    m_filterList->clear();

    if (!m_clip || m_clip->effects.empty()) {
        m_editorStack->setCurrentWidget(m_noSelectionLabel);
        m_removeBtn->setEnabled(false);
        m_updating = false;
        return;
    }

    m_removeBtn->setEnabled(true);

    for (size_t i = 0; i < m_clip->effects.size(); ++i) {
        const auto& eff = m_clip->effects[i];
        const QString displayName = getEffectTypeName(eff.type);

        auto* item = new QListWidgetItem(displayName, m_filterList);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(eff.enabled ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole, static_cast<int>(i));
    }

    m_updating = false;

    if (selectIndex >= 0 && selectIndex < m_filterList->count()) {
        m_filterList->setCurrentRow(selectIndex);
    } else if (m_filterList->count() > 0) {
        m_filterList->setCurrentRow(0);
    } else {
        m_editorStack->setCurrentWidget(m_noSelectionLabel);
    }
}

void EffectsPanel::onFilterListSelectionChanged(int currentRow) {
    if (m_updating) return;
    showEditorForCurrentFilter();
}

void EffectsPanel::onFilterItemChanged(QListWidgetItem* item) {
    if (m_updating || !m_clip) return;
    const int idx = item->data(Qt::UserRole).toInt();
    if (idx >= 0 && idx < static_cast<int>(m_clip->effects.size())) {
        m_clip->effects[idx].enabled = (item->checkState() == Qt::Checked);
        emit effectsEdited();
    }
}

void EffectsPanel::showEditorForCurrentFilter() {
    const int row = m_filterList->currentRow();
    if (!m_clip || row < 0 || row >= static_cast<int>(m_clip->effects.size())) {
        m_editorStack->setCurrentWidget(m_noSelectionLabel);
        return;
    }

    auto& eff = m_clip->effects[row];

    if (eff.type == "color_grade") {
        m_colorGradeWidget->loadFromEffect(eff);
        m_editorStack->setCurrentWidget(m_colorGradeWidget);
    } else {
        // Build generic editor
        QWidget* newContainer = buildGenericEditor(eff);
        m_genericEditorScroll->setWidget(newContainer);
        m_genericEditorContainer = newContainer;
        m_editorStack->setCurrentWidget(m_genericEditorScroll);
    }
}

QWidget* EffectsPanel::buildGenericEditor(Effect& effect) {
    auto* container = new QWidget(m_genericEditorScroll);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    const EffectTypeInfo* info = findEffectType(effect.type);

    auto* titleLabel = new QLabel(getEffectTypeName(effect.type), container);
    titleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #ffaa55; margin-bottom: 4px;");
    layout->addWidget(titleLabel);

    if (info && !info->params.empty()) {
        auto* form = new QFormLayout();
        form->setContentsMargins(0, 0, 0, 0);
        form->setSpacing(8);

        for (size_t p = 0; p < info->params.size(); ++p) {
            const QString pName = info->params[p].first;
            const QString pLabel = info->params[p].second;
            const auto [minVal, maxVal, defVal, stepVal] = info->paramRanges[p];

            const double curVal = effect.paramValue(pName, defVal);

            auto* rowWidget = new QWidget(container);
            auto* rowLayout = new QHBoxLayout(rowWidget);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(6);

            auto* spin = new QDoubleSpinBox(rowWidget);
            spin->setRange(minVal, maxVal);
            spin->setSingleStep(stepVal);
            spin->setValue(curVal);
            spin->setDecimals(stepVal < 0.1 ? 2 : 1);
            spin->setFixedWidth(68);

            auto* slider = new QSlider(Qt::Horizontal, rowWidget);
            slider->setRange(0, 1000);
            const int sliderVal = static_cast<int>((curVal - minVal) / (maxVal - minVal) * 1000.0);
            slider->setValue(sliderVal);

            connect(slider, &QSlider::valueChanged, this, [this, spin, minVal, maxVal, &effect, pName](int val) {
                if (m_updating) return;
                m_updating = true;
                const double dVal = minVal + (val / 1000.0) * (maxVal - minVal);
                spin->setValue(dVal);
                bool found = false;
                for (auto& param : effect.params) {
                    if (param.name == pName) { param.value = dVal; found = true; break; }
                }
                if (!found) effect.params.push_back(EffectParameter{pName, dVal});
                m_updating = false;
                emit effectsEdited();
            });

            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, slider, minVal, maxVal, &effect, pName](double dVal) {
                if (m_updating) return;
                m_updating = true;
                const int val = static_cast<int>((dVal - minVal) / (maxVal - minVal) * 1000.0);
                slider->setValue(val);
                bool found = false;
                for (auto& param : effect.params) {
                    if (param.name == pName) { param.value = dVal; found = true; break; }
                }
                if (!found) effect.params.push_back(EffectParameter{pName, dVal});
                m_updating = false;
                emit effectsEdited();
            });

            rowLayout->addWidget(slider, 1);
            rowLayout->addWidget(spin);

            form->addRow(pLabel, rowWidget);
        }

        layout->addLayout(form);
    } else {
        auto* noParamLabel = new QLabel(tr("Hiệu ứng này không cần điều chỉnh tham số."), container);
        noParamLabel->setStyleSheet("color: #888; font-style: italic;");
        layout->addWidget(noParamLabel);
    }

    // Reset button
    auto* resetBtn = new QPushButton(tr("Đặt lại mặc định"), container);
    resetBtn->setStyleSheet("background-color: #33333c; color: #ddd; padding: 4px 10px; border-radius: 4px;");
    connect(resetBtn, &QPushButton::clicked, this, [this, &effect, info]() {
        if (!info) return;
        effect.params.clear();
        for (size_t p = 0; p < info->params.size(); ++p) {
            const QString pName = info->params[p].first;
            const auto [minVal, maxVal, defVal, stepVal] = info->paramRanges[p];
            effect.params.push_back(EffectParameter{pName, defVal});
        }
        showEditorForCurrentFilter();
        emit effectsEdited();
    });
    layout->addWidget(resetBtn, 0, Qt::AlignLeft);

    layout->addStretch(1);
    return container;
}

void EffectsPanel::onColorGradingChanged() {
    const int row = m_filterList->currentRow();
    if (!m_clip || row < 0 || row >= static_cast<int>(m_clip->effects.size())) return;

    m_colorGradeWidget->applyToEffect(m_clip->effects[row]);
    emit effectsEdited();
}

void EffectsPanel::onGenericParamChanged() {
    emit effectsEdited();
}

void EffectsPanel::onAddEffectMenuRequested() {
    if (!m_clip) return;

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu {"
        "  background-color: #222228;"
        "  border: 1px solid #444450;"
        "  padding: 4px;"
        "}"
        "QMenu::item {"
        "  padding: 6px 18px;"
        "  color: #eee;"
        "  border-radius: 3px;"
        "}"
        "QMenu::item:selected {"
        "  background-color: #2b5b84;"
        "}"
    );

    for (const auto& t : availableEffectTypes()) {
        auto* action = menu.addAction(getEffectTypeName(t.id));
        connect(action, &QAction::triggered, this, [this, t]() {
            onAddEffectType(t.id);
        });
    }

    // Plugin effects submenu
    const auto pluginEffects = PluginManager::instance().allEffects();
    if (!pluginEffects.isEmpty()) {
        menu.addSeparator();
        auto* pluginMenu = menu.addMenu(LTR("effects.pluginMenu"));
        for (const auto& def : pluginEffects) {
            auto* action = pluginMenu->addAction(def.name);
            connect(action, &QAction::triggered, this, [this, def]() {
                if (!m_clip) return;
                Effect eff = PluginManager::buildEffect(def);
                m_clip->effects.push_back(eff);
                refreshFilterList(static_cast<int>(m_clip->effects.size()) - 1);
                emit effectsEdited();
            });
        }
    }

    menu.exec(m_addBtn->mapToGlobal(QPoint(0, m_addBtn->height())));
}

void EffectsPanel::onAddEffectType(const QString& typeId) {
    if (!m_clip) return;

    Effect eff;
    eff.type = typeId;
    eff.enabled = true;

    const EffectTypeInfo* info = findEffectType(typeId);
    if (info) {
        for (size_t p = 0; p < info->params.size(); ++p) {
            const auto [minVal, maxVal, defVal, stepVal] = info->paramRanges[p];
            eff.params.push_back(EffectParameter{info->params[p].first, defVal});
        }
    }

    m_clip->effects.push_back(eff);
    refreshFilterList(static_cast<int>(m_clip->effects.size()) - 1);
    emit effectsEdited();
}

void EffectsPanel::onRemoveSelectedEffect() {
    const int row = m_filterList->currentRow();
    if (!m_clip || row < 0 || row >= static_cast<int>(m_clip->effects.size())) return;

    m_clip->effects.erase(m_clip->effects.begin() + row);
    refreshFilterList(std::min(row, static_cast<int>(m_clip->effects.size()) - 1));
    emit effectsEdited();
}

} // namespace hc
