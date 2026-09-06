#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
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

// Left-dock "Explorer" panel: shows every imported MediaAsset as a thumbnail
// grid card (with name + type + metadata), offers a live search filter, lets
// the user trigger Import (file dialog handled by MainWindow), and acts as a
// drag source for adding clips to the timeline. Selecting a card also reports
// the asset id via assetSelected() so the Inspector can show its properties.
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

private:
    Project* m_project;
    ProxyManager* m_proxyManager; // not owned, may be nullptr
    QLabel* m_headerLabel = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_importBtn = nullptr;
    QPushButton* m_recordBtn = nullptr;
    AssetListWidget* m_list;
    QLabel* m_countLabel = nullptr;
    QString m_filter; // current search text (lowercased for matching)
};

} // namespace hc
