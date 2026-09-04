#pragma once
#include <QMainWindow>
#include <memory>
#include "../core/Project.h"
#include "../cache/ProxyManager.h"

class QDockWidget;
class QAction;
class QMenu;

namespace hc {

class MediaPoolWidget;
class TimelineWidget;
class PreviewWidget;
class PlaybackController;
class TransformPanel;
class TextPanel;
class AudioFilterPanel;
class EffectsPanel;
class ProxyManager;

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
    void onToggleCutTool(bool checked);
    void onUndo();
    void onRedo();
    void onRelinkMissingMedia();
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
    void updateUiTexts();

    // Bounding-box overlay signals from PreviewWidget.
    void onPreviewTransformChanged(hc::Transform transform);
    void onPreviewTransformCommitted(hc::Transform transform);

private:
    void buildMenus();
    void buildToolbar();
    void buildDocks();
    // Creates the six QDockWidget shells (with their tab relationships) once
    // for the whole window session. rebuildProjectDependentUi() only swaps
    // each dock's *content* afterwards — deleting and re-adding tabified
    // docks on every rebuild leaves a stale tab layout that crashes inside
    // Qt's dock layout (QWidget::setVisible -> QMainWindow::tabifyDockWidget).
    void ensureDocks();
    QMenu* buildAddLayerMenu();
    void rebuildProjectDependentUi();
    void updateWindowTitle();
    void generateThumbnail(const MediaAssetPtr& asset);
    void generateWaveform(const MediaAssetPtr& asset);
    void importFileAndAddToProject(const QString& filePath, bool addToTimeline = false);
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
    TransformPanel* m_transformPanel = nullptr;
    TextPanel* m_textPanel = nullptr;
    AudioFilterPanel* m_audioFilterPanel = nullptr;
    EffectsPanel* m_effectsPanel = nullptr;
    QDockWidget* m_mediaDock = nullptr;
    QDockWidget* m_timelineDock = nullptr;
    QDockWidget* m_transformDock = nullptr;
    QDockWidget* m_textDock = nullptr;
    QDockWidget* m_audioFilterDock = nullptr;
    QDockWidget* m_effectsDock = nullptr;
    QMenu* m_viewMenu = nullptr;
    QToolBar* m_mainToolbar = nullptr;
    QAction* m_cutToolAction = nullptr;
    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_useProxyAction = nullptr;
    bool m_modified = false;
    bool m_initialLayoutDone = false;

    // Currently selected visual clip (kept for bounding-box overlay updates).
    QString m_selectedTrackId;
    QString m_selectedClipId;

protected:
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
};

} // namespace hc
