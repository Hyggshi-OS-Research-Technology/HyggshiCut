#pragma once
#include <QWidget>
#include <QStringList>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QListWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QStackedWidget>
#include "../core/Clip.h"
#include "ThreeWayColorGradeWidget.h"

namespace hc {

// EffectsPanel – Studio dock widget for adding, managing, and tuning
// video filters & effects with 3-Way Color Wheels and custom parameter inspectors.
class EffectsPanel : public QWidget {
    Q_OBJECT
public:
    explicit EffectsPanel(QWidget* parent = nullptr);

    // Load the effects from a clip into the UI.
    // Pass nullptr to clear/disable the panel.
    void setClip(Clip* clip);

    // --- Static preset helpers (shared with the Explorer's Effects page
    // and MainWindow so there is exactly ONE list of effect types and ONE
    // way to build an effect with its default parameters). ---
    static QStringList effectTypeIds();
    static QString effectTypeName(const QString& typeId);
    static Effect buildEffect(const QString& typeId);

    // Id of the Explorer's "Effect Layer" card. It is NOT an effect type:
    // instead of pushing an effect onto the selected clip, it asks the
    // timeline to create a media-less Effect Layer (adjustment layer) whose
    // stack is inherited by every clip below it. Kept here so the Explorer,
    // MainWindow and TimelineWidget all agree on the one magic id.
    static QString effectLayerCardId();

public slots:
    void retranslateUi();

signals:
    void effectsEdited();

private slots:
    void onAddEffectMenuRequested();
    void onAddEffectType(const QString& typeId);
    void onRemoveSelectedEffect();
    void onFilterListSelectionChanged(int currentRow);
    void onFilterItemChanged(QListWidgetItem* item);
    void onColorGradingChanged();
    void onGenericParamChanged();

private:
    void refreshFilterList(int selectIndex = -1);
    void showEditorForCurrentFilter();
    QWidget* buildGenericEditor(Effect& effect);

    Clip* m_clip = nullptr;
    bool m_updating = false;

    // Left pane: Filter list
    QListWidget* m_filterList = nullptr;
    QPushButton* m_addBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;

    // Right pane: Editor stack
    QStackedWidget* m_editorStack = nullptr;
    QLabel* m_noSelectionLabel = nullptr;
    ThreeWayColorGradeWidget* m_colorGradeWidget = nullptr;
    QScrollArea* m_genericEditorScroll = nullptr;
    QWidget* m_genericEditorContainer = nullptr;
};

} // namespace hc
