#include "PropertiesPanel.h"
#include "TransformPanel.h"
#include "TextPanel.h"
#include "AudioFilterPanel.h"
#include "EffectsPanel.h"
#include "../i18n/LanguageManager.h"

#include <QTabWidget>
#include <QScrollArea>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFileInfo>

namespace hc {

namespace {

QString humanFileSize(qint64 bytes) {
    if (bytes <= 0) return QStringLiteral("—");
    constexpr double kb = 1024.0;
    constexpr double mb = kb * 1024.0;
    constexpr double gb = mb * 1024.0;
    if (bytes >= gb) return QStringLiteral("%1 GB").arg(bytes / gb, 0, 'f', 2);
    if (bytes >= mb) return QStringLiteral("%1 MB").arg(bytes / mb, 0, 'f', 1);
    if (bytes >= kb) return QStringLiteral("%1 KB").arg(bytes / kb, 0, 'f', 0);
    return QStringLiteral("%1 B").arg(bytes);
}

QString bitrateString(int64_t bitsPerSecond) {
    if (bitsPerSecond <= 0) return QStringLiteral("—");
    if (bitsPerSecond >= 1000000) {
        return QStringLiteral("%1 Mbps").arg(bitsPerSecond / 1000000.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 kbps").arg(bitsPerSecond / 1000.0, 0, 'f', 0);
}

QString channelsString(int channels) {
    if (channels <= 0) return QStringLiteral("—");
    switch (channels) {
        case 1:  return QStringLiteral("Mono (1)");
        case 2:  return QStringLiteral("Stereo (2)");
        case 6:  return QStringLiteral("5.1 (6)");
        default: return QStringLiteral("%1 channels").arg(channels);
    }
}

QString kindString(MediaKind kind) {
    switch (kind) {
        case MediaKind::Video: return QStringLiteral("Video");
        case MediaKind::Audio: return QStringLiteral("Audio");
        case MediaKind::Image: return QStringLiteral("Image");
        default:               return QStringLiteral("Unknown");
    }
}

} // namespace

PropertiesPanel::PropertiesPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    outer->addWidget(m_tabs);

    // --- Media info tab (read-only file properties). ---
    m_mediaPage = new QWidget(this);
    auto* mediaLayout = new QVBoxLayout(m_mediaPage);
    mediaLayout->setContentsMargins(8, 8, 8, 8);

    m_mediaNoSelectionLabel = new QLabel(m_mediaPage);
    m_mediaNoSelectionLabel->setWordWrap(true);
    m_mediaNoSelectionLabel->setStyleSheet("color: #888;");
    mediaLayout->addWidget(m_mediaNoSelectionLabel);

    m_mediaInfoBox = new QWidget(m_mediaPage);
    auto* mediaForm = new QFormLayout(m_mediaInfoBox);
    mediaForm->setContentsMargins(0, 0, 0, 0);
    mediaForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    mediaForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    mediaForm->setRowWrapPolicy(QFormLayout::WrapLongRows);

    auto makeValue = [this]() {
        auto* lbl = new QLabel(m_mediaInfoBox);
        lbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lbl->setWordWrap(true);
        return lbl;
    };
    m_mediaNameValue = makeValue();
    m_mediaTypeValue = makeValue();
    m_mediaPathValue = makeValue();
    m_mediaDurationValue = makeValue();
    m_mediaResolutionValue = makeValue();
    m_mediaFrameRateValue = makeValue();
    m_mediaBitrateValue = makeValue();
    m_mediaSampleRateValue = makeValue();
    m_mediaChannelsValue = makeValue();
    m_mediaFileSizeValue = makeValue();

    mediaForm->addRow(new QLabel(m_mediaInfoBox), m_mediaNameValue);
    mediaForm->addRow(new QLabel(m_mediaInfoBox), m_mediaTypeValue);
    mediaForm->addRow(new QLabel(m_mediaInfoBox), m_mediaPathValue);
    mediaForm->addRow(new QLabel(m_mediaInfoBox), m_mediaDurationValue);
    mediaForm->addRow(new QLabel(m_mediaInfoBox), m_mediaResolutionValue);
    mediaForm->addRow(new QLabel(m_mediaInfoBox), m_mediaFrameRateValue);
    mediaForm->addRow(new QLabel(m_mediaInfoBox), m_mediaBitrateValue);
    mediaForm->addRow(new QLabel(m_mediaInfoBox), m_mediaSampleRateValue);
    mediaForm->addRow(new QLabel(m_mediaInfoBox), m_mediaChannelsValue);
    mediaForm->addRow(new QLabel(m_mediaInfoBox), m_mediaFileSizeValue);

    mediaLayout->addWidget(m_mediaInfoBox);
    mediaLayout->addStretch(1);

    m_tabs->addTab(m_mediaPage, QString());

    // --- Editor tabs (each wrapped so narrow docks scroll instead of clip). ---
    m_transform = new TransformPanel(this);
    m_tabs->addTab(wrapInScroll(m_transform), QString());

    m_effects = new EffectsPanel(this);
    m_tabs->addTab(wrapInScroll(m_effects), QString());

    m_text = new TextPanel(this);
    m_tabs->addTab(wrapInScroll(m_text), QString());

    m_audio = new AudioFilterPanel(this);
    m_tabs->addTab(wrapInScroll(m_audio), QString());

    clearAssetInfo();
    retranslateUi();

    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi();
    });
}

QWidget* PropertiesPanel::wrapInScroll(QWidget* w) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(w);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

void PropertiesPanel::setSelectedClip(Project* project, const QString& trackId, const QString& clipId) {
    m_transform->setSelectedClip(project, trackId, clipId);
    m_audio->setSelectedClip(project, trackId, clipId);
    m_text->setSelectedClip(project, trackId, clipId);

    Clip* clip = nullptr;
    if (project && !trackId.isEmpty() && !clipId.isEmpty()) {
        Track* track = project->timeline().findTrack(trackId);
        if (track) clip = track->findClip(clipId);
    }
    m_effects->setClip(clip);

    if (!clip) {
        return;
    }

    // Jump to the tab that is relevant for the newly selected clip. Tabs
    // stay enabled so the user can still browse the others afterwards.
    switch (clip->type) {
        case ClipType::Text:  m_tabs->setCurrentIndex(m_textTab); break;
        case ClipType::Audio: m_tabs->setCurrentIndex(m_audioTab); break;
        case ClipType::Video:
        case ClipType::Image: m_tabs->setCurrentIndex(m_transformTab); break;
    }
}

void PropertiesPanel::clearClipSelection() {
    m_transform->setSelectedClip(nullptr, {}, {});
    m_audio->setSelectedClip(nullptr, {}, {});
    m_text->setSelectedClip(nullptr, {}, {});
    m_effects->setClip(nullptr);
}

void PropertiesPanel::showAssetInfo(const MediaAssetPtr& asset) {
    if (!asset) {
        clearAssetInfo();
        return;
    }

    m_mediaNoSelectionLabel->setVisible(false);
    m_mediaInfoBox->setVisible(true);

    m_mediaNameValue->setText(asset->displayName);
    m_mediaTypeValue->setText(kindString(asset->kind));
    m_mediaPathValue->setText(asset->filePath);

    if (asset->kind == MediaKind::Image || asset->duration <= 0) {
        m_mediaDurationValue->setText(QStringLiteral("—"));
    } else {
        m_mediaDurationValue->setText(formatTimecode(asset->duration).left(8));
    }

    if (asset->width > 0 && asset->height > 0) {
        m_mediaResolutionValue->setText(QStringLiteral("%1×%2").arg(asset->width).arg(asset->height));
    } else {
        m_mediaResolutionValue->setText(QStringLiteral("—"));
    }

    if (asset->frameRate > 0.0 && asset->kind != MediaKind::Image) {
        m_mediaFrameRateValue->setText(QStringLiteral("%1 fps").arg(asset->frameRate, 0, 'f', 2));
    } else {
        m_mediaFrameRateValue->setText(QStringLiteral("—"));
    }

    m_mediaBitrateValue->setText(bitrateString(asset->bitRate));

    if (asset->sampleRate > 0) {
        m_mediaSampleRateValue->setText(QStringLiteral("%1 Hz").arg(asset->sampleRate));
    } else {
        m_mediaSampleRateValue->setText(QStringLiteral("—"));
    }

    m_mediaChannelsValue->setText(channelsString(asset->channels));
    m_mediaFileSizeValue->setText(humanFileSize(QFileInfo(asset->filePath).size()));

    m_tabs->setCurrentIndex(m_mediaTab);
}

void PropertiesPanel::clearAssetInfo() {
    m_mediaNoSelectionLabel->setVisible(true);
    m_mediaInfoBox->setVisible(false);
}

void PropertiesPanel::setCurrentTime(Ticks t) {
    m_transform->setCurrentTime(t);
}

void PropertiesPanel::setTransformExternal(const Transform& t) {
    m_transform->setTransformExternal(t);
}

void PropertiesPanel::retranslateUi() {
    m_tabs->setTabText(m_mediaTab, LTR("props.tab.media"));
    m_tabs->setTabText(m_transformTab, LTR("props.tab.transform"));
    m_tabs->setTabText(m_effectsTab, LTR("props.tab.effects"));
    m_tabs->setTabText(m_textTab, LTR("props.tab.text"));
    m_tabs->setTabText(m_audioTab, LTR("props.tab.audio"));

    m_mediaNoSelectionLabel->setText(LTR("props.media.hint"));

    // Re-label the media form rows. The value labels live in a QFormLayout;
    // we walk its rows and relabel in the fixed construction order.
    if (auto* form = qobject_cast<QFormLayout*>(m_mediaInfoBox->layout())) {
        const QString labels[] = {
            LTR("props.media.name"), LTR("props.media.type"), LTR("props.media.location"),
            LTR("props.media.duration"), LTR("props.media.resolution"), LTR("props.media.frameRate"),
            LTR("props.media.bitrate"), LTR("props.media.sampleRate"), LTR("props.media.channels"),
            LTR("props.media.fileSize")
        };
        for (int i = 0; i < form->rowCount() && i < static_cast<int>(sizeof(labels) / sizeof(labels[0])); ++i) {
            if (auto* lbl = qobject_cast<QLabel*>(form->itemAt(i, QFormLayout::LabelRole)->widget())) {
                lbl->setText(labels[i]);
            }
        }
    }

    m_transform->retranslateUi();
    m_effects->retranslateUi();
    m_text->retranslateUi();
    m_audio->retranslateUi();
}

} // namespace hc
