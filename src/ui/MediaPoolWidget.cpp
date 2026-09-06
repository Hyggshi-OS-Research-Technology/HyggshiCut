#include "MediaPoolWidget.h"
#include "../i18n/LanguageManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMimeData>
#include <QDrag>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QFileInfo>

namespace hc {

namespace {

// Grid geometry: each card holds a 160x90 preview plus two lines of text.
constexpr int kCardWidth = 172;
constexpr int kCardHeight = 136;
constexpr int kThumbWidth = 160;
constexpr int kThumbHeight = 90;

QString kindLabel(MediaKind kind) {
    switch (kind) {
        case MediaKind::Video: return QStringLiteral("VIDEO");
        case MediaKind::Audio: return QStringLiteral("AUDIO");
        case MediaKind::Image: return QStringLiteral("IMAGE");
        default: return QStringLiteral("MEDIA");
    }
}

// Short second-line metadata: resolution for video/image, sample rate +
// bitrate for audio.
QString metaLine(const MediaAssetPtr& a) {
    if (a->kind == MediaKind::Image) {
        return QStringLiteral("%1×%2").arg(a->width).arg(a->height);
    }
    if (a->kind == MediaKind::Audio) {
        QString bits;
        if (a->bitRate > 0) {
            bits = QStringLiteral(" · %1 kbps").arg(qRound(a->bitRate / 1000.0));
        }
        return QStringLiteral("%1 Hz%2").arg(a->sampleRate > 0 ? a->sampleRate : 44100).arg(bits);
    }
    return QStringLiteral("%1 · %2×%3")
        .arg(formatTimecode(a->duration).left(8))
        .arg(a->width)
        .arg(a->height);
}

// Paints the equalizer-style fallback icon for audio assets.
QPixmap audioFallbackIcon(int w, int h) {
    QPixmap pm(w, h);
    pm.fill(QColor(30, 42, 38));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(80, 175, 120));
    const int barWidth = 5;
    const int gap = 4;
    const int heights[] = { 14, 30, 48, 26, 62, 38, 54, 20, 44, 28, 52, 18, 36, 24, 46, 16 };
    const int numBars = static_cast<int>(sizeof(heights) / sizeof(heights[0]));
    const int totalW = numBars * (barWidth + gap) - gap;
    const int startX = (w - totalW) / 2;
    for (int i = 0; i < numBars; ++i) {
        const int bh = heights[i];
        const int x = startX + i * (barWidth + gap);
        const int y = (h - bh) / 2;
        p.drawRoundedRect(x, y, barWidth, bh, 2, 2);
    }
    p.end();
    return pm;
}

// Paints a placeholder card for video/image assets that failed to produce a
// thumbnail (e.g. relinked media that could not be re-probed).
QPixmap placeholderIcon(int w, int h, const QString& label) {
    QPixmap pm(w, h);
    pm.fill(QColor(40, 46, 58));
    QPainter p(&pm);
    p.setPen(QColor(120, 130, 148));
    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(12);
    p.setFont(f);
    p.drawText(pm.rect(), Qt::AlignCenter, label);
    p.end();
    return pm;
}

} // namespace

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
    layout->setSpacing(6);

    m_headerLabel = new QLabel(this);
    m_headerLabel->setStyleSheet("font-weight: 600; padding: 2px;");
    layout->addWidget(m_headerLabel);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setPlaceholderText(LTR("media.pool.search"));
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_filter = text.trimmed().toLower();
        refresh();
    });
    layout->addWidget(m_searchEdit);

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
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWrapping(true);
    m_list->setWordWrap(true);
    m_list->setSpacing(8);
    m_list->setUniformItemSizes(true);
    m_list->setGridSize(QSize(kCardWidth, kCardHeight));
    m_list->setIconSize(QSize(kThumbWidth, kThumbHeight));
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit assetActivated(item->data(Qt::UserRole).toString());
    });
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
        emit assetSelected(current ? current->data(Qt::UserRole).toString() : QString());
    });
    layout->addWidget(m_list, 1);

    m_countLabel = new QLabel(this);
    m_countLabel->setStyleSheet("color: #888;");
    layout->addWidget(m_countLabel);

    retranslateUi();
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi();
    });
}

void MediaPoolWidget::setProject(Project* project) {
    m_project = project;
    m_filter.clear();
    if (m_searchEdit) m_searchEdit->clear();
    refresh();
}

void MediaPoolWidget::retranslateUi() {
    if (m_headerLabel) m_headerLabel->setText(LTR("media.pool.title"));
    if (m_searchEdit) m_searchEdit->setPlaceholderText(LTR("media.pool.search"));
    if (m_importBtn) m_importBtn->setText(LTR("media.pool.import"));
    if (m_recordBtn) m_recordBtn->setText(LTR("screenRecord.btn"));
}

void MediaPoolWidget::refresh() {
    // Remember the current card so refreshes (search typing, proxy status
    // changes) keep it selected and — crucially — don't emit a transient
    // "selection cleared" that would yank the Inspector's tab away while the
    // user is mid-edit on a clip.
    const QString previousId = m_list->currentItem()
        ? m_list->currentItem()->data(Qt::UserRole).toString()
        : QString();

    if (!m_project) {
        m_list->clear();
        m_countLabel->clear();
        return;
    }

    int shown = 0;
    const int total = static_cast<int>(m_project->assets().size());
    QListWidgetItem* toSelect = nullptr;

    // Block the list's own currentItemChanged while we tear down and rebuild,
    // then report the *net* selection change ourselves at the end.
    {
        const QSignalBlocker block(m_list);
        m_list->clear();

        for (const auto& asset : m_project->assets()) {
            if (!m_filter.isEmpty()) {
                const bool match = asset->displayName.toLower().contains(m_filter)
                    || QFileInfo(asset->filePath).fileName().toLower().contains(m_filter)
                    || kindLabel(asset->kind).toLower().contains(m_filter);
                if (!match) continue;
            }
            ++shown;

            QString proxyTag;
            if (m_proxyManager && asset->hasVideo() && asset->kind != MediaKind::Image) {
                switch (m_proxyManager->statusForAsset(asset)) {
                    case ProxyStatus::Ready:      proxyTag = QStringLiteral(" · proxy"); break;
                    case ProxyStatus::Generating: proxyTag = QStringLiteral(" · proxy…"); break;
                    case ProxyStatus::Queued:     proxyTag = QStringLiteral(" · queued"); break;
                    case ProxyStatus::Failed:     proxyTag = QStringLiteral(" · proxy err"); break;
                    case ProxyStatus::NotGenerated: break;
                }
            }

            const QString text = QStringLiteral("%1\n%2 · %3%4")
                .arg(asset->displayName, kindLabel(asset->kind), metaLine(asset), proxyTag);

            auto* item = new QListWidgetItem(text);
            if (!asset->thumbnail.isNull()) {
                item->setIcon(QIcon(QPixmap::fromImage(asset->thumbnail)));
            } else if (asset->kind == MediaKind::Audio) {
                item->setIcon(QIcon(audioFallbackIcon(kThumbWidth, kThumbHeight)));
            } else {
                item->setIcon(QIcon(placeholderIcon(kThumbWidth, kThumbHeight, kindLabel(asset->kind))));
            }
            item->setData(Qt::UserRole, asset->id);
            item->setToolTip(asset->filePath);
            m_list->addItem(item);

            if (!previousId.isEmpty() && asset->id == previousId) {
                toSelect = item;
            }
        }

        if (toSelect) {
            m_list->setCurrentItem(toSelect);
        }
    }

    const QString newId = m_list->currentItem()
        ? m_list->currentItem()->data(Qt::UserRole).toString()
        : QString();
    if (newId != previousId) {
        emit assetSelected(newId);
    }

    m_countLabel->setText(shown == total
        ? tr("%1 media").arg(total)
        : tr("Đang hiện %1/%2 media").arg(shown).arg(total));
}

} // namespace hc
