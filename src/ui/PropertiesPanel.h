#pragma once
#include <QWidget>
#include "../core/Project.h"
#include "../core/Clip.h"

class QTabWidget;
class QLabel;

namespace hc {

class TransformPanel;
class TextPanel;
class AudioFilterPanel;
class EffectsPanel;

// Unified right-dock Inspector. It groups the four previously separate
// clip-editing docks (Transform, Video Effects, Text, Audio Filters) into
// one tabbed panel and adds a read-only "Media" tab that shows the file
// properties of the asset currently selected in the Explorer (Media Pool).
//
// MainWindow keeps raw (non-owning) pointers to the sub-panels via the
// getters below so its existing signal/slot wiring and per-frame update
// calls (setCurrentTime / setTransformExternal) keep working unchanged.
class PropertiesPanel : public QWidget {
    Q_OBJECT
public:
    explicit PropertiesPanel(QWidget* parent = nullptr);

    // --- Timeline clip selection (drives the editor tabs). ---
    void setSelectedClip(Project* project, const QString& trackId, const QString& clipId);
    void clearClipSelection();

    // --- Explorer asset selection (drives the Media info tab). ---
    void showAssetInfo(const MediaAssetPtr& asset);
    void clearAssetInfo();

    // --- Forwarded per-frame / external updates for the transform editor. ---
    void setCurrentTime(Ticks t);
    void setTransformExternal(const Transform& t);

    TransformPanel* transformPanel() const { return m_transform; }
    TextPanel* textPanel() const { return m_text; }
    AudioFilterPanel* audioFilterPanel() const { return m_audio; }
    EffectsPanel* effectsPanel() const { return m_effects; }

    void retranslateUi();

private:
    QWidget* wrapInScroll(QWidget* w);

    QTabWidget* m_tabs = nullptr;
    int m_mediaTab = 0;
    int m_transformTab = 1;
    int m_effectsTab = 2;
    int m_textTab = 3;
    int m_audioTab = 4;

    // Media info tab widgets.
    QWidget* m_mediaPage = nullptr;
    QLabel* m_mediaNoSelectionLabel = nullptr;
    QWidget* m_mediaInfoBox = nullptr;
    QLabel* m_mediaNameValue = nullptr;
    QLabel* m_mediaTypeValue = nullptr;
    QLabel* m_mediaPathValue = nullptr;
    QLabel* m_mediaDurationValue = nullptr;
    QLabel* m_mediaResolutionValue = nullptr;
    QLabel* m_mediaFrameRateValue = nullptr;
    QLabel* m_mediaBitrateValue = nullptr;
    QLabel* m_mediaSampleRateValue = nullptr;
    QLabel* m_mediaChannelsValue = nullptr;
    QLabel* m_mediaFileSizeValue = nullptr;

    TransformPanel* m_transform = nullptr;
    TextPanel* m_text = nullptr;
    AudioFilterPanel* m_audio = nullptr;
    EffectsPanel* m_effects = nullptr;
};

} // namespace hc
