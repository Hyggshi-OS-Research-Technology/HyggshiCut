#include "MediaPoolWidget.h"
#include "../i18n/LanguageManager.h"
#include <QVBoxLayout>
#include <QPushButton>
#include <QMimeData>
#include <QDrag>
#include <QLabel>
#include <QPainter>
#include <QPixmap>

namespace hc {

QMimeData* AssetListWidget::mimeData(const QList<QListWidgetItem*>& items) const {
    if (items.isEmpty()) return nullptr;
    auto* mime = new QMimeData();
    const QString assetId = items.first()->data(Qt::UserRole).toString();
    mime->setData("application/x-hyggshicut-asset", assetId.toUtf8());
    return mime;
}

MediaPoolWidget::MediaPoolWidget(Project* project, ProxyManager* proxyManager, QWidget* parent)
    : QWidget(parent), m_project(project), m_proxyManager(proxyManager) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setStyleSheet("font-weight: 600; padding: 2px;");
    layout->addWidget(m_headerLabel);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(6);

    m_importBtn = new QPushButton(this);
    connect(m_importBtn, &QPushButton::clicked, this, &MediaPoolWidget::importRequested);
    btnLayout->addWidget(m_importBtn);

    m_recordBtn = new QPushButton(this);
    m_recordBtn->setStyleSheet("QPushButton { background-color: #3b2525; color: #ff7777; font-weight: bold; border-radius: 4px; padding: 4px 8px; } QPushButton:hover { background-color: #4a2e2e; color: #ff9999; }");
    connect(m_recordBtn, &QPushButton::clicked, this, &MediaPoolWidget::recordScreenRequested);
    btnLayout->addWidget(m_recordBtn);

    layout->addLayout(btnLayout);

    m_list = new AssetListWidget(this);
    m_list->setIconSize(QSize(120, 68));
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit assetActivated(item->data(Qt::UserRole).toString());
    });
    layout->addWidget(m_list, 1);

    retranslateUi();
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi();
    });
}

void MediaPoolWidget::setProject(Project* project) {
    m_project = project;
    refresh();
}

void MediaPoolWidget::retranslateUi() {
    if (m_headerLabel) m_headerLabel->setText(LTR("media.pool.title"));
    if (m_importBtn) m_importBtn->setText(LTR("media.pool.import"));
    if (m_recordBtn) m_recordBtn->setText(LTR("screenRecord.btn"));
}

void MediaPoolWidget::refresh() {
    m_list->clear();
    if (!m_project) return;
    for (const auto& asset : m_project->assets()) {
        const QString durationStr = formatTimecode(asset->duration).left(8);
        QString proxyTag;
        if (m_proxyManager && asset->hasVideo()) {
            switch (m_proxyManager->statusForAsset(asset)) {
                case ProxyStatus::Ready:      proxyTag = "  [proxy]"; break;
                case ProxyStatus::Generating: proxyTag = "  [đang tạo proxy...]"; break;
                case ProxyStatus::Queued:     proxyTag = "  [chờ tạo proxy]"; break;
                case ProxyStatus::Failed:     proxyTag = "  [proxy lỗi]"; break;
                case ProxyStatus::NotGenerated: break;
            }
        }
        QString desc;
        if (asset->kind == MediaKind::Audio) {
            QString kbpsStr;
            if (asset->bitRate > 0) {
                const int kbps = qRound(asset->bitRate / 1000.0);
                kbpsStr = QString(" · %1 Kbps").arg(kbps);
            }
            const int hz = asset->sampleRate > 0 ? asset->sampleRate : 44100;
            desc = QString("%1\n%2  ·  %3 Hz%4").arg(asset->displayName, durationStr)
                .arg(hz).arg(kbpsStr);
        } else if (asset->kind == MediaKind::Image) {
            desc = QString("%1\nẢnh tĩnh  ·  %2x%3").arg(asset->displayName).arg(asset->width).arg(asset->height);
        } else {
            desc = QString("%1\n%2  ·  %3x%4%5")
                .arg(asset->displayName, durationStr).arg(asset->width).arg(asset->height).arg(proxyTag);
        }
        auto* item = new QListWidgetItem(desc);
        if (!asset->thumbnail.isNull()) {
            item->setIcon(QIcon(QPixmap::fromImage(asset->thumbnail)));
        } else if (asset->kind == MediaKind::Audio) {
            QPixmap audioIcon(120, 68);
            audioIcon.fill(QColor(35, 45, 40));
            QPainter p(&audioIcon);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(80, 175, 120));
            const int barWidth = 4;
            const int gap = 3;
            const int heights[] = { 12, 24, 38, 20, 48, 30, 42, 16, 34, 22, 40, 14, 28 };
            const int numBars = sizeof(heights) / sizeof(heights[0]);
            const int startX = (120 - (numBars * (barWidth + gap) - gap)) / 2;
            for (int i = 0; i < numBars; ++i) {
                const int h = heights[i];
                const int x = startX + i * (barWidth + gap);
                const int y = (68 - h) / 2;
                p.drawRoundedRect(x, y, barWidth, h, 2, 2);
            }
            p.end();
            item->setIcon(QIcon(audioIcon));
        }
        item->setData(Qt::UserRole, asset->id);
        item->setToolTip(asset->filePath);
        m_list->addItem(item);
    }
}

} // namespace hc
