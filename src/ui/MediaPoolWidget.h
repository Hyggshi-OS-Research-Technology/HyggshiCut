#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
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

// Left-dock panel: shows every imported MediaAsset with a thumbnail, lets
// the user trigger Import (file dialog handled by MainWindow), and acts as
// a drag source for adding clips to the timeline.
class MediaPoolWidget : public QWidget {
    Q_OBJECT
public:
    // `proxyManager` is optional (nullptr = no proxy status shown) and not
    // owned by this widget — see MainWindow, which owns the one shared
    // instance for the whole app session.
    explicit MediaPoolWidget(Project* project, ProxyManager* proxyManager = nullptr, QWidget* parent = nullptr);

public slots:
    void refresh();
    void retranslateUi();

signals:
    void importRequested();
    void assetActivated(QString assetId); // double-click, e.g. to load into preview

private:
    Project* m_project;
    ProxyManager* m_proxyManager; // not owned, may be nullptr
    QLabel* m_headerLabel = nullptr;
    QPushButton* m_importBtn = nullptr;
    AssetListWidget* m_list;
};

} // namespace hc
