#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QStackedWidget>
#include <QVector>
#include "../core/Project.h"
#include "../cache/ProxyManager.h"

namespace hc {

// Internal list widget that knows how to package a selected asset as drag
// payload so TimelineWidget can accept it as a drop (see
// TimelineWidget::dropEvent, which looks for this exact MIME type).
class AssetListWidget : public QListWidget {
    Q_OBJECT
public:
    explicit AssetListWidget(QWidget* parent = nullptr) : QListWidget(parent) {}

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
};

// Left-dock "Explorer" panel, laid out CapCut-style: a vertical category rail
// (Media / Sounds / Text / Effects / Transitions) next to a stacked content
// area. Media shows the imported assets as a searchable thumbnail grid; the
// other categories are preset libraries:
//   - Sounds: synthesized sound effects + already-imported audio assets.
//   - Text:    styled text presets (double-click adds a Text clip).
//   - Effects: visual effect presets (double-click applies to the selection).
//   - Transitions: transition presets (double-click applies to the selection).
// Selection of an asset still reports assetSelected() so the Inspector can
// show its file properties.
class MediaPoolWidget : public QWidget {
    Q_OBJECT
public:
    // `proxyManager` is optional (nullptr = no proxy status shown) and not
    // owned by this widget — see MainWindow, which owns the one shared
    // instance for the whole app session.
    explicit MediaPoolWidget(Project* project, ProxyManager* proxyManager = nullptr, QWidget* parent = nullptr);

public slots:
    void setProject(Project* project);
    void refresh();
    void retranslateUi();

signals:
    void importRequested();
    void recordScreenRequested();
    void assetActivated(QString assetId); // double-click, e.g. to load into preview
    void assetSelected(QString assetId);  // single-click selection (empty = none)

    // Preset library requests — MainWindow performs the actual edit.
    void soundPresetRequested(QString sfxId);
    void textPresetRequested(QString presetId);
    void effectPresetRequested(QString effectTypeId);
    void transitionPresetRequested(QString transitionId);

private:
    void buildCategoryRail();
    void buildMediaPage();
    void buildSoundsPage();
    void populateSoundsPage();
    void populatePresetPages();
    QWidget* makeCardList(QListWidget** outList);

    Project* m_project;
    ProxyManager* m_proxyManager; // not owned, may be nullptr

    QListWidget* m_categories = nullptr;
    QStackedWidget* m_stack = nullptr;

    // Media page
    QLabel* m_mediaHeader = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_importBtn = nullptr;
    QPushButton* m_recordBtn = nullptr;
    AssetListWidget* m_mediaList = nullptr;
    QLabel* m_mediaCount = nullptr;

    // Sounds page
    QPushButton* m_importAudioBtn = nullptr;
    QLabel* m_soundHint = nullptr;
    AssetListWidget* m_soundsList = nullptr;

    // Preset pages
    QVector<QLabel*> m_presetHints; // one hint label per preset page, in page order
    QListWidget* m_textList = nullptr;
    QListWidget* m_effectsList = nullptr;
    QListWidget* m_transitionsList = nullptr;

    QString m_filter; // current search text (lowercased for matching)
};

} // namespace hc
