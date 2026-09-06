#pragma once
#include <QMainWindow>
#include <QElapsedTimer>
#include <memory>
#include "../core/Project.h"
#include "../cache/ProxyManager.h"

class QDockWidget;
class QAction;
class QMenu;
class QLabel;

#include "WindowSettingsDialog.h"

namespace hc {

class MediaPoolWidget;
class TimelineWidget;
class PreviewWidget;
class PlaybackController;
class PropertiesPanel;
class TransformPanel;
class TextPanel;
class AudioFilterPanel;
class EffectsPanel;
class ProxyManager;
struct WindowSettings;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Loads an existing .hcproj into this window (used by the GUI entry
    // point when a project is passed on the command line). Returns false
    // and fills errorOut on failure; the window is left unchanged.
    bool openProjectFromFile(const QString& path, QString* errorOut = nullptr);

private slots:
    void onImportRequested();
    void onNewProject();
    void onOpenProject();
    void onProjectSettings();
    bool onSaveProject();
    bool onSaveProjectAs();
    void onExport();
    void onAddVideoTrack();
    void onAddImageTrack();
    void onAddAudioTrack();
    void onAddTextTrack();
    void onSplitAtPlayhead();
    void onDeleteSelectedClip();
    void onDeleteSelectedTrack();
    void onZoomIn();
    void onZoomOut();
    void onZoomToFit();
    void updateZoomLabel(double pixelsPerSecond);
    void onToggleCutTool(bool checked);
    void onUndo();
    void onRedo();
    void onSelectFirstClip();
    void onDeselectAll();
    void onRelinkMissingMedia();
    void onMediaAssetSelected(QString assetId);
    // Explorer preset library handlers (CapCut-style left rail).
    void onTextPresetRequested(QString presetId);
    void onEffectPresetRequested(QString effectTypeId);
    void onTransitionPresetRequested(QString transitionId);
    void onSoundPresetRequested(QString sfxId);
    void onTimelineEdited();
    void onSeekRequested(hc::Ticks t);
    void onTimelineSelectionChanged(QString clipId, QString trackId);
    void onTransformEdited();
    void onTextEdited();
    void onAudioFiltersEdited();
    void onEffectsEdited();
    void onGenerateProxiesForAll();
    void onToggleUseProxy(bool checked);
    void onProxyStatusChanged(QString assetId, hc::ProxyStatus status);
    void onProxyReady(QString assetId, QString proxyPath);
    void onProxyFailed(QString assetId, QString error);
    void onProxyQueueProgress(int done, int total);
    // Language & Plugin slots
    void onLanguageSelected(const QString& langCode);
    void onLoadCustomLanguage();
    void onOpenPluginManager();
    void onGraphicsBackendSelected(const QString& backend);
    void onScreenRecord();
    void onWindowSettings();
    void openSettingsDialog(hc::SettingsTab tab = hc::SettingsTab::Window);
    void onThemeSelected(const QString& theme);
    void onAbout();
    void updateUiTexts();

    // Bounding-box overlay signals from PreviewWidget.
    void onPreviewTransformDragStarted();
    void onPreviewTransformChanged(hc::Transform transform);
    void onPreviewTransformCommitted(hc::Transform transform);

private:
    void buildMenus();
    void buildToolbar();
    void buildDocks();
    // Creates the three QDockWidget shells (Media, Timeline, Properties)
    // once for the whole window session. rebuildProjectDependentUi() only
    // swaps each dock's *content* afterwards — deleting and re-adding docks
    // on every rebuild leaves a stale tab layout that crashes inside Qt's
    // dock layout (QWidget::setVisible -> QMainWindow::tabifyDockWidget).
    void ensureDocks();
    QMenu* buildAddLayerMenu();
    void rebuildProjectDependentUi();
    void updateWindowTitle();
    void generateThumbnail(const MediaAssetPtr& asset);
    void generateWaveform(const MediaAssetPtr& asset);
    void importFileAndAddToProject(const QString& filePath, bool addToTimeline = false);
    // Places a clip for `asset` on a compatible (new or existing) track at the
    // playhead, nudging forward past any overlapping clips. Returns the added
    // clip, or nullptr on failure.
    Clip* placeAssetClip(const MediaAssetPtr& asset, ClipType type);
    void applyWindowSettings(const hc::WindowSettings& settings);
    void resetDockLayout();
    bool maybeSaveUnsavedChanges();
    void updateUndoRedoActions();
    void refreshTextPreview();

    std::unique_ptr<Project> m_project;
    std::unique_ptr<PlaybackController> m_playback;
    // Owned here (not per-project) so generated proxies and their on-disk
    // cache index persist across New/Open project and across app restarts —
    // see ProxyManager.h for the identity-key scheme that makes that safe.
    std::unique_ptr<ProxyManager> m_proxyManager;

    MediaPoolWidget* m_mediaPool = nullptr;
    TimelineWidget* m_timelineWidget = nullptr;
    PreviewWidget* m_preview = nullptr;
    PropertiesPanel* m_propertiesPanel = nullptr;
    // Non-owning pointers into m_propertiesPanel (see PropertiesPanel.h).
    TransformPanel* m_transformPanel = nullptr;
    TextPanel* m_textPanel = nullptr;
    AudioFilterPanel* m_audioFilterPanel = nullptr;
    EffectsPanel* m_effectsPanel = nullptr;
    QDockWidget* m_mediaDock = nullptr;
    QDockWidget* m_timelineDock = nullptr;
    QDockWidget* m_propertiesDock = nullptr;
    QMenu* m_viewMenu = nullptr;
    QToolBar* m_mainToolbar = nullptr;
    QAction* m_cutToolAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_useProxyAction = nullptr;
    QAction* m_snapAction = nullptr;
    QLabel* m_zoomLabel = nullptr;
    bool m_modified = false;
    bool m_initialLayoutDone = false;

    // Currently selected visual clip (kept for bounding-box overlay updates).
    QString m_selectedTrackId;
    QString m_selectedClipId;

    // Last asset the Explorer reported as selected; used to ignore redundant
    // re-selection (e.g. when proxy status changes refresh the pool) so the
    // Inspector tab isn't yanked away while the user is editing a clip.
    QString m_lastSelectedAssetId;

    // Whether playback was running when a preview-seekbar scrub began; used
    // to resume playback when the user releases the "lever" (see the
    // PreviewWidget::scrubStarted/scrubFinished wiring in
    // rebuildProjectDependentUi).
    bool m_scrubWasPlaying = false;

    // Throttles the preview re-render while the user drags the transform
    // bounding box, so a fast drag doesn't fire one synchronous seek/decode
    // per mouse-move and make the image lag behind (or "run away" from) the
    // cursor. The overlay box still tracks the mouse 1:1; only the GL
    // re-composite is rate-limited. Invalidated on drag start / release.
    QElapsedTimer m_transformSeekThrottle;

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
};

} // namespace hc
