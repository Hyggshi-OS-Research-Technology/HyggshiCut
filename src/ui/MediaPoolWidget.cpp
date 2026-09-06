#include "MediaPoolWidget.h"
#include "PresetLibrary.h"
#include "EffectsPanel.h"
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
#include <QScrollArea>

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

// Paints a preset card icon: a rounded gradient tile with a centered label.
QPixmap presetIcon(int w, int h, const QColor& c1, const QColor& c2, const QString& label) {
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient g(0, 0, w, h);
    g.setColorAt(0.0, c1);
    g.setColorAt(1.0, c2);
    p.setBrush(g);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(2, 2, w - 4, h - 4, 8, 8);

    QFont f = p.font();
    f.setBold(true);
    f.setPointSize(16);
    p.setFont(f);
    p.setPen(QColor(255, 255, 255, 230));
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
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    buildCategoryRail();
    layout->addWidget(m_categories);

    m_stack = new QStackedWidget(this);
    buildMediaPage();   // index 0
    buildSoundsPage();  // index 1
    m_stack->addWidget(makeCardList(&m_textList));        // index 2
    m_stack->addWidget(makeCardList(&m_effectsList));     // index 3
    m_stack->addWidget(makeCardList(&m_transitionsList)); // index 4
    layout->addWidget(m_stack, 1);

    // Rail drives the stacked pages.
    connect(m_categories, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);

    // Preset card activation (connected once; populate only repaints them).
    connect(m_textList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit textPresetRequested(item->data(Qt::UserRole).toString());
    });
    connect(m_effectsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit effectPresetRequested(item->data(Qt::UserRole).toString());
    });
    connect(m_transitionsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit transitionPresetRequested(item->data(Qt::UserRole).toString());
    });

    populatePresetPages();
    retranslateUi();

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi();
    });
}

// Creates the vertical category rail. Each row index matches the
// QStackedWidget page index (Category enum order).
void MediaPoolWidget::buildCategoryRail() {
    m_categories = new QListWidget(this);
    m_categories->setFixedWidth(96);
    m_categories->setMovement(QListView::Static);
    m_categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categories->setFocusPolicy(Qt::NoFocus);
    m_categories->setStyleSheet(
        "QListWidget { background-color: #16161a; border: none; }"
        "QListWidget::item { padding: 12px 6px; border-radius: 4px; }"
        "QListWidget::item:selected { background-color: #2b5b84; color: white; font-weight: bold; }");

    const QStringList keys = {
        QStringLiteral("explorer.cat.media"),
        QStringLiteral("explorer.cat.sounds"),
        QStringLiteral("explorer.cat.text"),
        QStringLiteral("explorer.cat.effects"),
        QStringLiteral("explorer.cat.transitions"),
    };
    for (const auto& key : keys) {
        auto* item = new QListWidgetItem(LTR(key));
        item->setTextAlignment(Qt::AlignCenter);
        m_categories->addItem(item);
    }
    m_categories->setCurrentRow(0);
}

// Builds the Media page. The page itself is inserted at stack index 0.
void MediaPoolWidget::buildMediaPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    m_mediaHeader = new QLabel(page);
    m_mediaHeader->setStyleSheet("font-weight: 600; padding: 2px;");
    layout->addWidget(m_mediaHeader);

    m_searchEdit = new QLineEdit(page);
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

    m_importBtn = new QPushButton(page);
    connect(m_importBtn, &QPushButton::clicked, this, &MediaPoolWidget::importRequested);
    btnLayout->addWidget(m_importBtn);

    m_recordBtn = new QPushButton(page);
    m_recordBtn->setStyleSheet("QPushButton { background-color: #3b2525; color: #ff7777; font-weight: bold; border-radius: 4px; padding: 4px 8px; } QPushButton:hover { background-color: #4a2e2e; color: #ff9999; }");
    connect(m_recordBtn, &QPushButton::clicked, this, &MediaPoolWidget::recordScreenRequested);
    btnLayout->addWidget(m_recordBtn);

    layout->addLayout(btnLayout);

    m_mediaList = new AssetListWidget(page);
    m_mediaList->setViewMode(QListView::IconMode);
    m_mediaList->setResizeMode(QListView::Adjust);
    m_mediaList->setMovement(QListView::Static);
    m_mediaList->setWrapping(true);
    m_mediaList->setWordWrap(true);
    m_mediaList->setSpacing(8);
    m_mediaList->setUniformItemSizes(true);
    m_mediaList->setGridSize(QSize(kCardWidth, kCardHeight));
    m_mediaList->setIconSize(QSize(kThumbWidth, kThumbHeight));
    m_mediaList->setTextElideMode(Qt::ElideRight);
    m_mediaList->setDragEnabled(true);
    m_mediaList->setDragDropMode(QAbstractItemView::DragOnly);
    m_mediaList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_mediaList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_mediaList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_mediaList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit assetActivated(item->data(Qt::UserRole).toString());
    });
    connect(m_mediaList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
        emit assetSelected(current ? current->data(Qt::UserRole).toString() : QString());
    });
    layout->addWidget(m_mediaList, 1);

    m_mediaCount = new QLabel(page);
    m_mediaCount->setStyleSheet("color: #888;");
    layout->addWidget(m_mediaCount);

    m_stack->addWidget(page); // index 0
}

// Builds the Sounds page (index 1): import button + card list of synthesized
// SFX and imported audio assets.
void MediaPoolWidget::buildSoundsPage() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    m_importAudioBtn = new QPushButton(page);
    connect(m_importAudioBtn, &QPushButton::clicked, this, &MediaPoolWidget::importRequested);
    layout->addWidget(m_importAudioBtn);

    auto* hint = new QLabel(page);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #888;");
    layout->addWidget(hint);
    m_soundHint = hint;

    m_soundsList = new AssetListWidget(page);
    m_soundsList->setViewMode(QListView::IconMode);
    m_soundsList->setResizeMode(QListView::Adjust);
    m_soundsList->setMovement(QListView::Static);
    m_soundsList->setWrapping(true);
    m_soundsList->setWordWrap(true);
    m_soundsList->setSpacing(8);
    m_soundsList->setUniformItemSizes(true);
    m_soundsList->setGridSize(QSize(kCardWidth, kCardHeight));
    m_soundsList->setIconSize(QSize(kThumbWidth, kThumbHeight));
    m_soundsList->setTextElideMode(Qt::ElideRight);
    m_soundsList->setDragEnabled(true);
    m_soundsList->setDragDropMode(QAbstractItemView::DragOnly);
    m_soundsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_soundsList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_soundsList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_soundsList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const bool isSfx = item->data(Qt::UserRole + 1).toBool();
        const QString id = item->data(Qt::UserRole).toString();
        if (isSfx) emit soundPresetRequested(id);
        else emit assetActivated(id);
    });
    connect(m_soundsList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* current, QListWidgetItem*) {
        // Only imported assets have an Inspector "Media" page; SFX cards are
        // synthesized on demand and report no selection.
        if (current && !current->data(Qt::UserRole + 1).toBool()) {
            emit assetSelected(current->data(Qt::UserRole).toString());
        } else {
            emit assetSelected(QString());
        }
    });
    layout->addWidget(m_soundsList, 1);

    m_stack->addWidget(page); // index 1
}

// Creates a QListWidget configured as an icon-mode card grid, wrapped in a
// page widget that owns a small hint label above the list.
QWidget* MediaPoolWidget::makeCardList(QListWidget** outList) {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto* hint = new QLabel(page);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #888;");
    layout->addWidget(hint);
    m_presetHints.push_back(hint);

    auto* list = new QListWidget(page);
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setWrapping(true);
    list->setWordWrap(true);
    list->setSpacing(8);
    list->setUniformItemSizes(true);
    list->setGridSize(QSize(kCardWidth, kCardHeight));
    list->setIconSize(QSize(kThumbWidth, kThumbHeight));
    list->setTextElideMode(Qt::ElideRight);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(list, 1);

    *outList = list;
    return page;
}

void MediaPoolWidget::setProject(Project* project) {
    m_project = project;
    m_filter.clear();
    if (m_searchEdit) m_searchEdit->clear();
    refresh();
}

void MediaPoolWidget::retranslateUi() {
    // Category rail
    const QStringList catKeys = {
        QStringLiteral("explorer.cat.media"),
        QStringLiteral("explorer.cat.sounds"),
        QStringLiteral("explorer.cat.text"),
        QStringLiteral("explorer.cat.effects"),
        QStringLiteral("explorer.cat.transitions"),
    };
    for (int i = 0; i < m_categories->count() && i < catKeys.size(); ++i) {
        m_categories->item(i)->setText(LTR(catKeys[i]));
    }

    if (m_mediaHeader) m_mediaHeader->setText(LTR("media.pool.title"));
    if (m_searchEdit) m_searchEdit->setPlaceholderText(LTR("media.pool.search"));
    if (m_importBtn) m_importBtn->setText(LTR("media.pool.import"));
    if (m_recordBtn) m_recordBtn->setText(LTR("screenRecord.btn"));
    if (m_importAudioBtn) m_importAudioBtn->setText(LTR("explorer.sounds.import"));
    if (m_soundHint) m_soundHint->setText(LTR("explorer.hint.sounds"));

    const QStringList hintKeys = {
        QStringLiteral("explorer.hint.text"),
        QStringLiteral("explorer.hint.effects"),
        QStringLiteral("explorer.hint.transitions"),
    };
    for (int i = 0; i < m_presetHints.size() && i < hintKeys.size(); ++i) {
        m_presetHints[i]->setText(LTR(hintKeys[i]));
    }

    populatePresetPages();
    populateSoundsPage();
}

void MediaPoolWidget::refresh() {
    // --- Media page ---
    const QString previousId = m_mediaList->currentItem()
        ? m_mediaList->currentItem()->data(Qt::UserRole).toString()
        : QString();

    if (!m_project) {
        m_mediaList->clear();
        m_soundsList->clear();
        m_mediaCount->clear();
        return;
    }

    int shown = 0;
    const int total = static_cast<int>(m_project->assets().size());
    QListWidgetItem* toSelect = nullptr;

    {
        const QSignalBlocker block(m_mediaList);
        m_mediaList->clear();

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
            m_mediaList->addItem(item);

            if (!previousId.isEmpty() && asset->id == previousId) {
                toSelect = item;
            }
        }

        if (toSelect) {
            m_mediaList->setCurrentItem(toSelect);
        }
    }

    const QString newId = m_mediaList->currentItem()
        ? m_mediaList->currentItem()->data(Qt::UserRole).toString()
        : QString();
    if (newId != previousId) {
        emit assetSelected(newId);
    }

    m_mediaCount->setText(shown == total
        ? tr("%1 media").arg(total)
        : tr("Đang hiện %1/%2 media").arg(shown).arg(total));

    // --- Sounds page (audio assets change with the project) ---
    populateSoundsPage();
}

void MediaPoolWidget::populateSoundsPage() {
    if (!m_soundsList) return;

    const QString previousId = m_soundsList->currentItem()
        ? m_soundsList->currentItem()->data(Qt::UserRole).toString()
        : QString();

    const QSignalBlocker block(m_soundsList);
    m_soundsList->clear();

    // Synthesized SFX first.
    const QColor sfxColors[] = {
        QColor(64, 140, 200), QColor(40, 90, 150),   // beep
        QColor(220, 160, 60), QColor(150, 100, 40),  // click
        QColor(200, 90, 150), QColor(130, 50, 100),  // pop
        QColor(90, 180, 160), QColor(50, 120, 110),  // whoosh
        QColor(150, 120, 220), QColor(90, 70, 150),  // chime
        QColor(230, 120, 90), QColor(150, 70, 50),   // tick
    };
    int colorIdx = 0;
    for (const auto& sfx : sfxPresets()) {
        const QColor c1 = sfxColors[colorIdx % 6 * 2];
        const QColor c2 = sfxColors[colorIdx % 6 * 2 + 1];
        ++colorIdx;
        auto* item = new QListWidgetItem(LTR(sfx.nameKey));
        item->setIcon(QIcon(presetIcon(kThumbWidth, kThumbHeight, c1, c2, QStringLiteral("SND"))));
        item->setData(Qt::UserRole, sfx.id);
        item->setData(Qt::UserRole + 1, true); // is-sfx marker
        item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled); // not an asset yet
        item->setToolTip(tr("Double-click to add this sound to the timeline."));
        m_soundsList->addItem(item);
    }

    // Then imported audio assets.
    QListWidgetItem* toSelect = nullptr;
    if (m_project) {
        for (const auto& asset : m_project->assets()) {
            if (asset->kind != MediaKind::Audio) continue;
            const QString text = QStringLiteral("%1\n%2").arg(asset->displayName, metaLine(asset));
            auto* item = new QListWidgetItem(text);
            item->setIcon(QIcon(audioFallbackIcon(kThumbWidth, kThumbHeight)));
            item->setData(Qt::UserRole, asset->id);
            item->setData(Qt::UserRole + 1, false);
            item->setToolTip(asset->filePath);
            m_soundsList->addItem(item);
            if (!previousId.isEmpty() && asset->id == previousId) {
                toSelect = item;
            }
        }
    }

    if (toSelect) {
        m_soundsList->setCurrentItem(toSelect);
    }
}

void MediaPoolWidget::populatePresetPages() {
    const QSignalBlocker b1(m_textList);
    const QSignalBlocker b2(m_effectsList);
    const QSignalBlocker b3(m_transitionsList);

    // --- Text presets ---
    if (m_textList) {
        m_textList->clear();
        const QColor cols[] = { QColor(90, 150, 230), QColor(230, 90, 130), QColor(90, 200, 160) };
        int ci = 0;
        for (const auto& p : textPresets()) {
            auto* item = new QListWidgetItem(LTR(p.nameKey));
            item->setIcon(QIcon(presetIcon(kThumbWidth, kThumbHeight, cols[ci % 3], cols[ci % 3].darker(150), QStringLiteral("T"))));
            item->setData(Qt::UserRole, p.id);
            item->setToolTip(tr("Double-click to add a Text clip with this style."));
            m_textList->addItem(item);
            ++ci;
        }
    }

    // --- Effects presets (single source of truth: EffectsPanel) ---
    if (m_effectsList) {
        m_effectsList->clear();
        const QColor cols[] = { QColor(200, 140, 60), QColor(90, 170, 210), QColor(170, 120, 210) };
        int ci = 0;
        const QStringList ids = EffectsPanel::effectTypeIds();
        for (const auto& id : ids) {
            auto* item = new QListWidgetItem(EffectsPanel::effectTypeName(id));
            item->setIcon(QIcon(presetIcon(kThumbWidth, kThumbHeight, cols[ci % 3], cols[ci % 3].darker(150), QStringLiteral("Fx"))));
            item->setData(Qt::UserRole, id);
            item->setToolTip(tr("Double-click to apply this effect to the selected clip."));
            m_effectsList->addItem(item);
            ++ci;
        }
    }

    // --- Transition presets ---
    if (m_transitionsList) {
        m_transitionsList->clear();
        const QColor cols[] = { QColor(255, 190, 80), QColor(90, 190, 255), QColor(120, 220, 140), QColor(215, 130, 245) };
        int ci = 0;
        const QString glyphs[] = { QStringLiteral("X"), QStringLiteral("W"), QStringLiteral("S"), QStringLiteral("D"), QStringLiteral("D") };
        for (const auto& p : transitionPresets()) {
            auto* item = new QListWidgetItem(LTR(p.nameKey));
            item->setIcon(QIcon(presetIcon(kThumbWidth, kThumbHeight, cols[ci % 4], cols[ci % 4].darker(150), glyphs[ci % 5])));
            item->setData(Qt::UserRole, p.id);
            item->setToolTip(tr("Double-click to apply this transition before the selected clip."));
            m_transitionsList->addItem(item);
            ++ci;
        }
    }
}

} // namespace hc
