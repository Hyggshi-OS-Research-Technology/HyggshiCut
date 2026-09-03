#include "MainWindow.h"
#include "MediaPoolWidget.h"
#include "TimelineWidget.h"
#include "PreviewWidget.h"
#include "TransformPanel.h"
#include "TextPanel.h"
#include "AudioFilterPanel.h"
#include "EffectsPanel.h"
#include "ProjectSettingsDialog.h"
#include "ExportDialog.h"
#include "SourcePreviewDialog.h"
#include "PluginManagerDialog.h"
#include "../i18n/LanguageManager.h"
#include "../playback/PlaybackController.h"
#include "../decode/Decoder.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QToolButton>
#include <QDockWidget>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QStatusBar>
#include <QKeySequence>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QFileInfo>
#include <QTimer>

namespace hc {

namespace {
// QScrollArea has no "viewport resized" signal, so we override the event
// directly to let TimelineWidget auto-scale its track row height to fill
// the available space (see TimelineWidget::recomputeTrackHeight()) instead
// of always needing a vertical scroll even with only 2-3 tracks.
class TimelineScrollArea : public QScrollArea {
public:
    explicit TimelineScrollArea(TimelineWidget* timeline, QWidget* parent = nullptr)
        : QScrollArea(parent), m_timeline(timeline) {
        setMinimumHeight(80);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    QSize sizeHint() const override { return QSize(1000, 280); }
    QSize minimumSizeHint() const override { return QSize(200, 80); }

protected:
    void resizeEvent(QResizeEvent* event) override {
        QScrollArea::resizeEvent(event);
        if (m_timeline) m_timeline->setAvailableHeight(viewport()->height());
    }

private:
    TimelineWidget* m_timeline;
};
} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("HyggshiCut");
    resize(1400, 900);

    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setDockNestingEnabled(true);

    m_project = std::make_unique<Project>();
    m_project->timeline().addTrack(TrackType::Visual, tr("Visual 1"));
    m_project->timeline().addTrack(TrackType::Audio, tr("Audio 1"));

    // Lives for the whole app session (see MainWindow.h) so its on-disk
    // proxy cache is reused across New/Open project instead of being
    // rebuilt from scratch every time.
    m_proxyManager = std::make_unique<ProxyManager>();
    connect(m_proxyManager.get(), &ProxyManager::proxyStatusChanged, this, &MainWindow::onProxyStatusChanged);
    connect(m_proxyManager.get(), &ProxyManager::proxyReady, this, &MainWindow::onProxyReady);
    connect(m_proxyManager.get(), &ProxyManager::proxyFailed, this, &MainWindow::onProxyFailed);
    connect(m_proxyManager.get(), &ProxyManager::queueProgress, this, &MainWindow::onProxyQueueProgress);

    m_preview = new PreviewWidget(this);
    setCentralWidget(m_preview);

    buildMenus();
    buildToolbar();
    rebuildProjectDependentUi();
    statusBar()->showMessage(tr("Sẵn sàng. Kéo media vào timeline để bắt đầu dựng."));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildMenus() {
    auto* fileMenu = menuBar()->addMenu(LTR("menu.file"));
    fileMenu->addAction(LTR("menu.file.new"), QKeySequence::New, this, &MainWindow::onNewProject);
    fileMenu->addAction(LTR("menu.file.open"), QKeySequence::Open, this, &MainWindow::onOpenProject);
    fileMenu->addAction(LTR("menu.file.save"), QKeySequence::Save, this, &MainWindow::onSaveProject);
    fileMenu->addAction(LTR("menu.file.saveas"), QKeySequence::SaveAs, this, &MainWindow::onSaveProjectAs);
    fileMenu->addSeparator();
    fileMenu->addAction(LTR("menu.settings.canvas"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this, &MainWindow::onProjectSettings);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Nhập media..."), QKeySequence(Qt::CTRL | Qt::Key_I), this, &MainWindow::onImportRequested);
    fileMenu->addAction(LTR("menu.file.relinkMedia"), this, &MainWindow::onRelinkMissingMedia);
    fileMenu->addSeparator();
    fileMenu->addAction(LTR("menu.file.export"), QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::onExport);
    fileMenu->addSeparator();
    fileMenu->addAction(LTR("menu.file.quit"), QKeySequence::Quit, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(LTR("menu.edit"));
    m_undoAction = editMenu->addAction(LTR("menu.edit.undo"), QKeySequence::Undo, this, &MainWindow::onUndo);
    m_redoAction = editMenu->addAction(LTR("menu.edit.redo"), QKeySequence::Redo, this, &MainWindow::onRedo);
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});
    updateUndoRedoActions();

    auto* timelineMenu = menuBar()->addMenu(LTR("dock.timeline"));
    auto* addLayerMenu = buildAddLayerMenu();
    addLayerMenu->setTitle(LTR("menu.track"));
    timelineMenu->addMenu(addLayerMenu);
    timelineMenu->addAction(LTR("menu.track.addText"), QKeySequence(Qt::CTRL | Qt::Key_T), this, &MainWindow::onAddTextTrack);
    timelineMenu->addSeparator();
    timelineMenu->addAction(LTR("menu.edit.splitAtPlayhead"), QKeySequence(Qt::Key_S), this, &MainWindow::onSplitAtPlayhead);
    timelineMenu->addAction(LTR("menu.edit.deleteClip"), QKeySequence::Delete, this, &MainWindow::onDeleteSelectedClip);
    timelineMenu->addAction(LTR("menu.edit.deleteTrack"), QKeySequence(Qt::SHIFT | Qt::Key_Delete), this, &MainWindow::onDeleteSelectedTrack);

    m_viewMenu = menuBar()->addMenu(LTR("menu.view"));
    m_viewMenu->addAction(LTR("menu.view.zoomin"), QKeySequence::ZoomIn, this, &MainWindow::onZoomIn);
    m_viewMenu->addAction(LTR("menu.view.zoomout"), QKeySequence::ZoomOut, this, &MainWindow::onZoomOut);
    m_viewMenu->addSeparator();

    auto* meterAct = m_viewMenu->addAction(tr("Đồng hồ đo âm lượng (VU Meter)"));
    meterAct->setCheckable(true);
    meterAct->setChecked(false);
    connect(meterAct, &QAction::toggled, this, [this](bool checked) {
        if (m_preview) m_preview->setAudioMeterVisible(checked);
    });
    m_viewMenu->addSeparator();

    // --- Settings & Extensions Menu ---
    auto* settingsMenu = menuBar()->addMenu(LTR("menu.settings"));
    settingsMenu->addAction(LTR("menu.settings.canvas"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this, &MainWindow::onProjectSettings);

    // Language Submenu
    auto* langSubMenu = settingsMenu->addMenu(LTR("menu.settings.language"));
    const auto availableLangs = LanguageManager::instance().availableLanguages();
    const QString curLang = LanguageManager::instance().currentLanguage();

    for (const auto& l : availableLangs) {
        auto* act = langSubMenu->addAction(l.nativeName);
        act->setCheckable(true);
        act->setChecked(l.languageCode == curLang);
        const QString code = l.languageCode;
        connect(act, &QAction::triggered, this, [this, code]() {
            onLanguageSelected(code);
        });
    }
    langSubMenu->addSeparator();
    langSubMenu->addAction(LTR("lang.loadFile"), this, &MainWindow::onLoadCustomLanguage);

    settingsMenu->addSeparator();
    settingsMenu->addAction(LTR("menu.settings.plugins"), this, &MainWindow::onOpenPluginManager);
    settingsMenu->addSeparator();
    settingsMenu->addAction(LTR("menu.settings.generateProxies"), this, &MainWindow::onGenerateProxiesForAll);

    // --- Performance Menu ---
    auto* perfMenu = menuBar()->addMenu(LTR("menu.perf"));
    perfMenu->addAction(LTR("menu.settings.generateProxies"), this, &MainWindow::onGenerateProxiesForAll);
    m_useProxyAction = perfMenu->addAction(LTR("menu.view.useProxy"));
    m_useProxyAction->setCheckable(true);
    m_useProxyAction->setChecked(true);
    connect(m_useProxyAction, &QAction::toggled, this, &MainWindow::onToggleUseProxy);
}

void MainWindow::buildToolbar() {
    if (!m_mainToolbar) {
        m_mainToolbar = addToolBar(tr("Toolbar"));
        m_mainToolbar->setMovable(false);
    }
    m_mainToolbar->clear();

    const bool wasCutChecked = m_cutToolAction ? m_cutToolAction->isChecked() : false;

    m_cutToolAction = new QAction(LTR("menu.edit.cutTool"), this);
    m_cutToolAction->setText("✂ " + LTR("menu.edit.cutTool"));
    m_cutToolAction->setCheckable(true);
    m_cutToolAction->setChecked(wasCutChecked);
    connect(m_cutToolAction, &QAction::toggled, this, &MainWindow::onToggleCutTool);
    m_mainToolbar->addAction(m_cutToolAction);

    m_mainToolbar->addSeparator();
    m_mainToolbar->addAction(LTR("menu.edit.splitAtPlayhead"), this, &MainWindow::onSplitAtPlayhead);
    m_mainToolbar->addAction(LTR("menu.edit.deleteClip"), this, &MainWindow::onDeleteSelectedClip);
    m_mainToolbar->addAction(LTR("menu.edit.deleteTrack"), this, &MainWindow::onDeleteSelectedTrack);
    m_mainToolbar->addSeparator();
    auto* addLayerBtn = new QToolButton(this);
    addLayerBtn->setText("+ " + LTR("menu.track"));
    addLayerBtn->setPopupMode(QToolButton::InstantPopup);
    addLayerBtn->setMenu(buildAddLayerMenu());
    m_mainToolbar->addWidget(addLayerBtn);
    m_mainToolbar->addSeparator();
    m_mainToolbar->addAction(LTR("menu.view.zoomin"), this, &MainWindow::onZoomIn);
    m_mainToolbar->addAction(LTR("menu.view.zoomout"), this, &MainWindow::onZoomOut);
}

QMenu* MainWindow::buildAddLayerMenu() {
    auto* menu = new QMenu(this);
    menu->addAction(LTR("menu.track.addVideo"), this, &MainWindow::onAddVideoTrack);
    menu->addAction(LTR("menu.track.addImage"), this, &MainWindow::onAddImageTrack);
    menu->addAction(LTR("menu.track.addAudio"), this, &MainWindow::onAddAudioTrack);
    menu->addAction(LTR("menu.track.addText"), this, &MainWindow::onAddTextTrack);
    menu->addSeparator();
    menu->addAction(LTR("menu.track.deleteSelected"), this, &MainWindow::onDeleteSelectedTrack);
    return menu;
}

void MainWindow::ensureDocks() {
    // Docks are created a single time for the whole window and then kept
    // alive; only their contents are swapped by rebuildProjectDependentUi().
    // This is deliberate: deleting and re-adding *tabified* QDockWidgets on
    // every project (re)build leaves a stale tab layout inside QMainWindow
    // that crashes in Qt's own dock code on some setups (a hard segfault in
    // QWidget::setVisible <- QMainWindow::tabifyDockWidget).
    if (m_mediaDock) return;

    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);

    m_mediaDock = new QDockWidget(LTR("dock.mediaPool"), this);
    m_mediaDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, m_mediaDock);

    m_timelineDock = new QDockWidget(LTR("dock.timeline"), this);
    m_timelineDock->setObjectName("TimelineDock");
    m_timelineDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);

    m_transformDock = new QDockWidget(LTR("dock.transform"), this);
    m_transformDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_transformDock);

    m_textDock = new QDockWidget(LTR("dock.text"), this);
    m_textDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_textDock);
    tabifyDockWidget(m_transformDock, m_textDock);

    m_audioFilterDock = new QDockWidget(LTR("dock.audioFilter"), this);
    m_audioFilterDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_audioFilterDock);
    tabifyDockWidget(m_textDock, m_audioFilterDock);

    m_effectsDock = new QDockWidget(LTR("dock.effects"), this);
    m_effectsDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_effectsDock);
    tabifyDockWidget(m_audioFilterDock, m_effectsDock);
}
void MainWindow::rebuildProjectDependentUi() {
    // PlaybackController owns a raw Project* because it is tightly coupled to
    // the currently displayed timeline. When New/Open replaces m_project,
    // the old Project is destroyed; keeping the old controller alive leaves
    // a dangling pointer and the first seek/render can segfault. Always tear
    // the controller down before rebuilding project-dependent UI, then create
    // a fresh controller below for the new Project instance.
    // The controller owns decoders/timers tied to the previous Project.
    // Tear it down before replacing any project-dependent widgets.
    if (m_playback) {
        m_playback->pause();
        m_playback.reset();
    }

    // Selection IDs belong to the old project and must never leak into a
    // newly opened project.
    m_selectedTrackId.clear();
    m_selectedClipId.clear();

    // The six QDockWidget shells are created once (ensureDocks) and reused
    // for the whole window session. Here we only (re)create each dock's
    // content for the (possibly new) Project and bind it into the existing
    // dock — we do NOT remove/delete/re-add the docks, because tabbed docks
    // torn down repeatedly crash Qt's dock layout (see ensureDocks comment).
    ensureDocks();

    auto makeScrollWrapper = [](QWidget* widget, QWidget* parent) -> QScrollArea* {
        auto* scroll = new QScrollArea(parent);
        scroll->setWidget(widget);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        return scroll;
    };

    m_mediaPool = new MediaPoolWidget(m_project.get(), m_proxyManager.get(), this);
    m_mediaDock->setWidget(m_mediaPool);

    m_timelineWidget = new TimelineWidget(m_project.get(), this);
    m_timelineWidget->setCutToolActive(m_cutToolAction && m_cutToolAction->isChecked());
    auto* scrollArea = new TimelineScrollArea(m_timelineWidget, this);
    scrollArea->setWidget(m_timelineWidget);
    scrollArea->setWidgetResizable(false);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_timelineDock->setWidget(scrollArea);

    m_transformPanel = new TransformPanel(this);
    m_transformDock->setWidget(makeScrollWrapper(m_transformPanel, this));

    connect(m_transformPanel, &TransformPanel::transformEdited, this, &MainWindow::onTransformEdited);

    m_textPanel = new TextPanel(this);
    m_textDock->setWidget(makeScrollWrapper(m_textPanel, this));

    connect(m_textPanel, &TextPanel::textEdited, this, &MainWindow::onTextEdited);

    m_audioFilterPanel = new AudioFilterPanel(this);
    m_audioFilterDock->setWidget(makeScrollWrapper(m_audioFilterPanel, this));

    connect(m_audioFilterPanel, &AudioFilterPanel::audioFiltersEdited, this, &MainWindow::onAudioFiltersEdited);

    m_effectsPanel = new EffectsPanel(this);
    m_effectsDock->setWidget(makeScrollWrapper(m_effectsPanel, this));

    connect(m_effectsPanel, &EffectsPanel::effectsEdited, this, &MainWindow::onEffectsEdited);

    m_playback = std::make_unique<PlaybackController>(m_project.get(), m_preview->glWidget(),
                                                        m_proxyManager.get(), this);
    m_playback->setUseProxy(m_useProxyAction ? m_useProxyAction->isChecked() : true);

    connect(m_mediaPool, &MediaPoolWidget::importRequested, this, &MainWindow::onImportRequested);
    connect(m_project.get(), &Project::assetsChanged, m_mediaPool, &MediaPoolWidget::refresh);
    connect(m_mediaPool, &MediaPoolWidget::assetActivated, this, [this](QString assetId) {
        auto asset = m_project->findAsset(assetId);
        if (!asset) return;
        // Real audio+video playback of the raw source file via libmpv — the
        // timeline preview (PlaybackController/GLVideoWidget) only ever
        // draws silent frames, so this is the one place HyggshiCut can
        // actually let you *listen* to a clip before dragging it in.
        auto* dlg = new SourcePreviewDialog(asset->filePath, asset->displayName, this);
        dlg->setModal(false);
        dlg->show();
    });

    connect(m_timelineWidget, &TimelineWidget::seekRequested, this, &MainWindow::onSeekRequested);
    connect(m_timelineWidget, &TimelineWidget::timelineEdited, this, &MainWindow::onTimelineEdited);
    connect(m_timelineWidget, &TimelineWidget::selectionChanged, this, &MainWindow::onTimelineSelectionChanged);
    connect(m_timelineWidget, &TimelineWidget::togglePlaybackRequested, m_playback.get(),
            &PlaybackController::togglePlay);

    connect(m_preview, &PreviewWidget::playPauseClicked, m_playback.get(), &PlaybackController::togglePlay);
    connect(m_preview, &PreviewWidget::seekRequested, this, &MainWindow::onSeekRequested);
    connect(m_preview, &PreviewWidget::previewTransformChanged, this, &MainWindow::onPreviewTransformChanged);
    connect(m_preview, &PreviewWidget::previewTransformCommitted, this, &MainWindow::onPreviewTransformCommitted);

    connect(m_playback.get(), &PlaybackController::positionChanged, this, [this](Ticks t) {
        m_preview->setPosition(t);
        m_timelineWidget->setPlayheadTime(t);
        if (m_transformPanel) m_transformPanel->setCurrentTime(t);
        if (!m_selectedTrackId.isEmpty() && !m_selectedClipId.isEmpty()) {
            Track* track = m_project->timeline().findTrack(m_selectedTrackId);
            Clip* clip = track ? track->findClip(m_selectedClipId) : nullptr;
            if (clip && (clip->type == ClipType::Video || clip->type == ClipType::Image)) {
                m_preview->updateOverlayTransform(clip->transformAt(t));
            }
        }
    });
    connect(m_playback.get(), &PlaybackController::playingChanged, m_preview, &PreviewWidget::setPlaying);
    connect(m_playback.get(), &PlaybackController::audioLevelsChanged, m_preview, &PreviewWidget::setAudioLevels);

    m_mediaPool->refresh();
    m_timelineWidget->refresh();
    m_preview->setDuration(m_project->timeline().totalDuration());
    m_preview->setPosition(0);

    // QOpenGLWidget may not have an initialized context yet. Rendering here
    // synchronously is unsafe when this function is called while replacing a
    // project. Defer the first seek/render until the event loop has returned.
    PlaybackController* playbackForInitialFrame = m_playback.get();
    QTimer::singleShot(0, this, [this, playbackForInitialFrame]() {
        if (m_playback.get() != playbackForInitialFrame || !m_project) return;
        m_playback->seek(0);
    });

    // Proportional initial sizing for docks
    resizeDocks({m_mediaDock, m_transformDock}, {260, 320}, Qt::Horizontal);
    const int initTimelineH = std::clamp(height() * 35 / 100, 240, 420);
    resizeDocks({m_timelineDock}, {initTimelineH}, Qt::Vertical);

    m_modified = false;
    updateWindowTitle();
    updateUndoRedoActions();
    refreshTextPreview();
}

void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (!m_initialLayoutDone) {
        m_initialLayoutDone = true;
        QTimer::singleShot(50, this, [this]() {
            if (m_timelineDock) {
                const int timelineH = std::clamp(height() * 35 / 100, 240, 450);
                resizeDocks({m_timelineDock}, {timelineH}, Qt::Vertical);
            }
            if (m_mediaDock && m_transformDock) {
                resizeDocks({m_mediaDock, m_transformDock}, {260, 320}, Qt::Horizontal);
            }
        });
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_timelineDock && isVisible()) {
        if (m_timelineDock->height() < 150) {
            const int timelineH = std::clamp(height() * 35 / 100, 220, 450);
            resizeDocks({m_timelineDock}, {timelineH}, Qt::Vertical);
        }
    }
}

void MainWindow::updateWindowTitle() {
    const QString name = m_project->name;
    const QString path = m_project->filePath.isEmpty() ? tr("(chưa lưu)") : m_project->filePath;
    setWindowTitle(QString("%1HyggshiCut — %2 [%3]").arg(m_modified ? "* " : "", name, path));
}

void MainWindow::generateThumbnail(const MediaAssetPtr& asset) {
    if (!asset || !asset->hasVideo()) return;
    Decoder d;
    QString err;
    if (!d.open(asset->filePath, &err)) return;
    const Ticks probeAt = std::min<Ticks>(asset->duration / 10, secondsToTicks(1.0));
    asset->thumbnail = d.grabThumbnail(probeAt);
}

void MainWindow::generateWaveform(const MediaAssetPtr& asset) {
    if (!asset || !asset->hasAudio()) return;
    Decoder d;
    QString err;
    if (!d.open(asset->filePath, &err)) return;
    asset->waveformPeaks = d.computeWaveformPeaks(MediaAsset::kWaveformBucketsPerSecond);
}

void MainWindow::onImportRequested() {
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Nhập media"), QString(),
        tr("Media files (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mp3 *.wav *.flac *.aac *.ogg *.png *.jpg *.jpeg);;Tất cả (*.*)"));
    if (files.isEmpty()) return;

    QStringList failures;
    for (const auto& file : files) {
        QString err;
        auto asset = m_project->importMedia(file, &err);
        if (!asset) {
            failures << QString("%1: %2").arg(file, err);
            continue;
        }
        generateThumbnail(asset);
        generateWaveform(asset);
    }
    m_mediaPool->refresh();
    m_modified = true;
    updateWindowTitle();

    if (!failures.isEmpty()) {
        QMessageBox::warning(this, tr("Một số file không nhập được"), failures.join("\n"));
    }
}

void MainWindow::onNewProject() {
    if (!maybeSaveUnsavedChanges()) return;
    auto newProj = std::make_unique<Project>();
    ProjectSettingsDialog dlg(newProj.get(), true, this);
    if (dlg.exec() != QDialog::Accepted) return;

    m_project = std::move(newProj);
    m_project->name = dlg.projectName();
    m_project->timeline().videoWidth = dlg.videoWidth();
    m_project->timeline().videoHeight = dlg.videoHeight();
    m_project->timeline().frameRate = dlg.frameRate();

    m_project->timeline().addTrack(TrackType::Visual, tr("Visual 1"));
    m_project->timeline().addTrack(TrackType::Audio, tr("Audio 1"));
    rebuildProjectDependentUi();
    updateWindowTitle();
}

void MainWindow::onProjectSettings() {
    if (!m_project) return;
    ProjectSettingsDialog dlg(m_project.get(), false, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_project->name = dlg.projectName();
        m_project->timeline().videoWidth = dlg.videoWidth();
        m_project->timeline().videoHeight = dlg.videoHeight();
        m_project->timeline().frameRate = dlg.frameRate();
        m_modified = true;
        updateWindowTitle();
        if (m_playback) m_playback->seek(m_playback->currentTime());
    }
}

void MainWindow::onOpenProject() {
    if (!maybeSaveUnsavedChanges()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Mở dự án"), QString(),
        tr("Dự án HyggshiCut (*.hcproj);;Tất cả tệp (*.*)"));
    if (path.isEmpty()) return;

    QString err;
    if (!openProjectFromFile(path, &err)) {
        QMessageBox::critical(this, tr("Không mở được dự án"), err);
    }
}

bool MainWindow::openProjectFromFile(const QString& path, QString* errorOut) {
    auto newProject = std::make_unique<Project>();
    if (!newProject->loadFromFile(path, errorOut)) {
        return false;
    }
    m_project = std::move(newProject);
    m_modified = false;
    rebuildProjectDependentUi();
    updateWindowTitle();
    return true;
}

bool MainWindow::onSaveProject() {
    if (m_project->filePath.isEmpty()) {
        return onSaveProjectAs();
    }
    QString err;
    if (!m_project->saveToFile(m_project->filePath, &err)) {
        QMessageBox::critical(this, tr("Không lưu được dự án"), err);
        return false;
    } else {
        m_modified = false;
        updateWindowTitle();
        statusBar()->showMessage(tr("Đã lưu dự án: %1").arg(m_project->filePath), 3000);
        return true;
    }
}

bool MainWindow::onSaveProjectAs() {
    QString initialPath = m_project->filePath;
    if (initialPath.isEmpty()) {
        initialPath = m_project->name.isEmpty() ? "Untitled.hcproj" : (m_project->name + ".hcproj");
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("Lưu dự án thành"), initialPath,
        tr("Dự án HyggshiCut (*.hcproj);;Tất cả tệp (*.*)"));
    if (path.isEmpty()) return false;

    if (!path.endsWith(".hcproj", Qt::CaseInsensitive)) {
        path += ".hcproj";
    }

    m_project->name = QFileInfo(path).completeBaseName();
    QString err;
    if (!m_project->saveToFile(path, &err)) {
        QMessageBox::critical(this, tr("Không lưu được dự án"), err);
        return false;
    } else {
        m_modified = false;
        updateWindowTitle();
        statusBar()->showMessage(tr("Đã lưu dự án: %1").arg(path), 3000);
        return true;
    }
}

void MainWindow::onExport() {
    ExportDialog dialog(m_project.get(), this);
    dialog.exec();
}

void MainWindow::onAddVideoTrack() {
    m_project->pushUndoSnapshot();
    m_project->timeline().addTrack(TrackType::Visual,
        tr("Video %1").arg(m_project->timeline().tracks().size() + 1));
    onTimelineEdited();
}

void MainWindow::onAddImageTrack() {
    // Image layers live on a Visual track (same compositing/overlay rules
    // as video); only the display name differs so the timeline reads clearly.
    m_project->pushUndoSnapshot();
    m_project->timeline().addTrack(TrackType::Visual,
        tr("Ảnh %1").arg(m_project->timeline().tracks().size() + 1));
    onTimelineEdited();
}

void MainWindow::onAddAudioTrack() {
    m_project->pushUndoSnapshot();
    m_project->timeline().addTrack(TrackType::Audio,
        tr("Audio %1").arg(m_project->timeline().tracks().size() + 1));
    onTimelineEdited();
}

void MainWindow::onAddTextTrack() {
    // Adds a Text track with one title clip spanning the current timeline
    // (or 5s if the timeline is still empty). Double-click the Text clip on
    // the timeline to edit its label afterwards.
    m_project->pushUndoSnapshot();
    auto& tl = m_project->timeline();
    Track& textTrack = tl.addTrack(TrackType::Visual,
        tr("Văn bản %1").arg(tl.tracks().size() + 1));

    Clip clip;
    clip.type = ClipType::Text;
    clip.displayLabel = tr("Tiêu đề");
    clip.sourceIn = 0;
    const Ticks tlDur = tl.totalDuration();
    clip.sourceOut = tlDur > 0 ? tlDur : secondsToTicks(5.0);
    clip.timelineStart = 0;
    textTrack.addClip(std::move(clip));

    onTimelineEdited();
}

void MainWindow::onSplitAtPlayhead() { m_timelineWidget->splitAtPlayhead(); }
void MainWindow::onDeleteSelectedClip() { m_timelineWidget->deleteSelectedClip(); }
void MainWindow::onDeleteSelectedTrack() { m_timelineWidget->deleteSelectedTrack(); }
void MainWindow::onZoomIn() { m_timelineWidget->setZoom(m_timelineWidget->zoom() * 1.25); }
void MainWindow::onZoomOut() { m_timelineWidget->setZoom(m_timelineWidget->zoom() / 1.25); }

void MainWindow::onToggleCutTool(bool checked) {
    if (m_timelineWidget) m_timelineWidget->setCutToolActive(checked);
    statusBar()->showMessage(checked
        ? tr("Dao cắt đang bật — bấm vào clip trên timeline để cắt tại đó.")
        : QString(), checked ? 0 : 1);
}

void MainWindow::onUndo() {
    if (!m_project->undo()) return;
    if (m_playback) m_playback->pause();
    if (m_timelineWidget) m_timelineWidget->clearSelection();
    m_selectedTrackId.clear();
    m_selectedClipId.clear();
    if (m_preview) m_preview->clearTransformOverlay();
    onTimelineEdited();
    updateUndoRedoActions();
    statusBar()->showMessage(tr("Đã hoàn tác."), 1500);
}

void MainWindow::onRedo() {
    if (!m_project->redo()) return;
    if (m_playback) m_playback->pause();
    if (m_timelineWidget) m_timelineWidget->clearSelection();
    m_selectedTrackId.clear();
    m_selectedClipId.clear();
    if (m_preview) m_preview->clearTransformOverlay();
    onTimelineEdited();
    updateUndoRedoActions();
    statusBar()->showMessage(tr("Đã làm lại."), 1500);
}

void MainWindow::onRelinkMissingMedia() {
    int missingCount = 0;
    for (const auto& asset : m_project->assets()) {
        if (QFileInfo::exists(asset->filePath)) continue;
        ++missingCount;
    }
    if (missingCount == 0) {
        statusBar()->showMessage(tr("Không có media nào bị thiếu."), 2500);
        return;
    }

    // Walk each missing asset; let the user pick a replacement file (or skip).
    for (const auto& asset : m_project->assets()) {
        if (QFileInfo::exists(asset->filePath)) continue;
        const auto choice = QMessageBox::question(this,
            tr("Media bị mất"),
            tr("Không tìm thấy file:\n%1\n\nChọn lại file mới cho asset này "
               "('Có'), bỏ qua ('Không'), hoặc dừng lại ('Hủy').")
                .arg(asset->filePath),
            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (choice == QMessageBox::Cancel) return;
        if (choice == QMessageBox::No) continue;

        const QString newPath = QFileDialog::getOpenFileName(
            this, tr("Chọn file cho media bị mất"), QFileInfo(asset->filePath).path(),
            tr("Media files (*.mp4 *.mov *.mkv *.avi *.webm *.m4v *.mp3 *.wav *.flac *.aac *.ogg *.png *.jpg *.jpeg);;Tất cả (*.*)"));
        if (newPath.isEmpty()) continue;

        QString err;
        auto relinked = m_project->relinkAsset(asset->id, newPath, &err);
        if (!relinked) {
            QMessageBox::warning(this, tr("Không liên kết được"), err);
            continue;
        }
        generateThumbnail(relinked);
        generateWaveform(relinked);
    }
    m_mediaPool->refresh();
    m_modified = true;
    updateWindowTitle();
    statusBar()->showMessage(tr("Đã cập nhật liên kết media."), 2500);
}

void MainWindow::onTimelineEdited() {
    m_modified = true;
    m_timelineWidget->refresh();
    if (m_preview) m_preview->setDuration(m_project->timeline().totalDuration());
    updateUndoRedoActions();
    refreshTextPreview();
    updateWindowTitle();
}

void MainWindow::onSeekRequested(hc::Ticks t) {
    if (m_playback) m_playback->seek(t);
    if (m_timelineWidget) m_timelineWidget->setPlayheadTime(t);
    if (m_transformPanel) m_transformPanel->setCurrentTime(t);
    if (m_preview && !m_selectedTrackId.isEmpty() && !m_selectedClipId.isEmpty()) {
        Track* track = m_project->timeline().findTrack(m_selectedTrackId);
        Clip* clip = track ? track->findClip(m_selectedClipId) : nullptr;
        if (clip && (clip->type == ClipType::Video || clip->type == ClipType::Image)) {
            m_preview->updateOverlayTransform(clip->transformAt(t));
        }
    }
}

void MainWindow::onTimelineSelectionChanged(QString clipId, QString trackId) {
    m_selectedTrackId = trackId;
    m_selectedClipId = clipId;

    Clip* clip = nullptr;
    if (m_project && !trackId.isEmpty() && !clipId.isEmpty()) {
        Track* track = m_project->timeline().findTrack(trackId);
        if (track) clip = track->findClip(clipId);
    }

    if (m_transformPanel) {
        m_transformPanel->setSelectedClip(m_project.get(), trackId, clipId);
        if (m_playback) m_transformPanel->setCurrentTime(m_playback->currentTime());
    }
    if (m_audioFilterPanel) {
        m_audioFilterPanel->setSelectedClip(m_project.get(), trackId, clipId);
    }
    if (m_effectsPanel) {
        m_effectsPanel->setClip(clip);
    }
    if (m_textPanel) {
        m_textPanel->setSelectedClip(m_project.get(), trackId, clipId);
    }

    if (clip) {
        if (clip->type == ClipType::Audio && m_audioFilterDock) {
            m_audioFilterDock->raise();
        } else if (clip->type == ClipType::Text && m_textDock) {
            m_textDock->raise();
        } else if ((clip->type == ClipType::Video || clip->type == ClipType::Image) && m_transformDock) {
            m_transformDock->raise();
        }
    }

    if (m_preview) {
        if (clip && (clip->type == ClipType::Video || clip->type == ClipType::Image || clip->type == ClipType::Text)) {
            auto asset = m_project->findAsset(clip->assetId);
            int srcW = asset ? asset->width : 1920;
            int srcH = asset ? asset->height : 1080;
            if (srcW <= 0) srcW = 1920;
            if (srcH <= 0) srcH = 1080;
            // For text clips use the timeline canvas as source dimensions
            if (clip->type == ClipType::Text) {
                srcW = m_project->timeline().videoWidth > 0 ? m_project->timeline().videoWidth : 1920;
                srcH = m_project->timeline().videoHeight > 0 ? m_project->timeline().videoHeight : 1080;
            }
            Transform tf = clip->transformAt(m_playback ? m_playback->currentTime() : 0);
            m_preview->setSelectedTransform(tf, srcW, srcH);
        } else {
            m_preview->clearTransformOverlay();
        }
    }
}

void MainWindow::onTransformEdited() {
    // The clip's Transform changed in place; re-render the current frame
    // (topmost-covers-all no longer applies, so this can reveal/hide layers
    // beneath the edited clip) without touching playhead/undo state.
    m_modified = true;
    if (m_preview && !m_selectedTrackId.isEmpty() && !m_selectedClipId.isEmpty()) {
        Track* track = m_project->timeline().findTrack(m_selectedTrackId);
        Clip* clip = track ? track->findClip(m_selectedClipId) : nullptr;
        if (clip && (clip->type == ClipType::Video || clip->type == ClipType::Image || clip->type == ClipType::Text)) {
            Transform tf = clip->transformAt(m_playback ? m_playback->currentTime() : 0);
            m_preview->updateOverlayTransform(tf);
        }
    }
    if (m_playback) m_playback->seek(m_playback->currentTime());
    updateWindowTitle();
}

void MainWindow::onPreviewTransformChanged(hc::Transform transform) {
    if (!m_project || m_selectedTrackId.isEmpty() || m_selectedClipId.isEmpty()) return;
    Track* track = m_project->timeline().findTrack(m_selectedTrackId);
    Clip* clip = track ? track->findClip(m_selectedClipId) : nullptr;
    if (!clip) return;

    if (clip->hasTransformKeyframes()) {
        Ticks curTime = m_playback ? m_playback->currentTime() : 0;
        Ticks rel = std::clamp<Ticks>(curTime - clip->timelineStart, 0, std::max<Ticks>(0, clip->timelineDuration()));
        clip->setTransformKeyframe(rel, transform);
    } else {
        clip->transform = transform;
    }

    m_project->timeline().notifyClipChanged(m_selectedTrackId);
    if (m_transformPanel) {
        m_transformPanel->setTransformExternal(transform);
    }
    m_modified = true;
    if (m_playback) m_playback->seek(m_playback->currentTime());
    updateWindowTitle();
}

void MainWindow::onPreviewTransformCommitted(hc::Transform transform) {
    onPreviewTransformChanged(transform);
    if (m_project) {
        m_project->pushUndoSnapshot();
        updateUndoRedoActions();
    }
}

void MainWindow::onTextEdited() {
    // Text clip content or styling changed — re-render current frame and
    // update bounding-box overlay dimensions from the new rendered text.
    m_modified = true;
    if (m_playback) m_playback->seek(m_playback->currentTime());
    // Update overlay size for the new text extents
    if (m_preview && m_project && !m_selectedTrackId.isEmpty() && !m_selectedClipId.isEmpty()) {
        Track* track = m_project->timeline().findTrack(m_selectedTrackId);
        Clip* clip = track ? track->findClip(m_selectedClipId) : nullptr;
        if (clip && clip->type == ClipType::Text) {
            int canvasW = m_project->timeline().videoWidth > 0 ? m_project->timeline().videoWidth : 1920;
            int canvasH = m_project->timeline().videoHeight > 0 ? m_project->timeline().videoHeight : 1080;
            Transform tf = clip->transformAt(m_playback ? m_playback->currentTime() : 0);
            m_preview->setSelectedTransform(tf, canvasW, canvasH);
        }
    }
    updateWindowTitle();
    if (m_project) {
        m_project->pushUndoSnapshot();
        updateUndoRedoActions();
    }
}

void MainWindow::onAudioFiltersEdited() {
    // Nothing needs a visual re-render, but seeking re-primes PlaybackController's
    // audio pipeline (including any AudioFilterChain) so the change is
    // audible immediately instead of only from the next seek/play.
    m_modified = true;
    if (m_playback) m_playback->seek(m_playback->currentTime());
    updateWindowTitle();
}

void MainWindow::onEffectsEdited() {
    m_modified = true;
    if (m_playback) m_playback->seek(m_playback->currentTime());
    updateWindowTitle();
}

void MainWindow::onGenerateProxiesForAll() {
    if (!m_proxyManager) return;
    int alreadyReady = 0, queued = 0, noVideo = 0;
    for (const auto& asset : m_project->assets()) {
        if (!asset->hasVideo()) { ++noVideo; continue; }
        if (m_proxyManager->statusForAsset(asset) == hc::ProxyStatus::Ready) { ++alreadyReady; continue; }
        ++queued;
    }
    m_proxyManager->requestProxiesForAssets(m_project->assets());

    if (queued == 0) {
        statusBar()->showMessage(
            alreadyReady > 0
                ? tr("Toàn bộ %1 media video đã có proxy.").arg(alreadyReady)
                : tr("Không có media video nào để tạo proxy."),
            3000);
    } else {
        statusBar()->showMessage(
            tr("Đang tạo proxy cho %1 media (đã có sẵn %2)...").arg(queued).arg(alreadyReady));
    }
    if (m_mediaPool) m_mediaPool->refresh();
}

void MainWindow::onToggleUseProxy(bool checked) {
    if (m_playback) m_playback->setUseProxy(checked);
    statusBar()->showMessage(
        checked ? tr("Đã bật dùng proxy khi xem trước.")
                : tr("Đã tắt proxy — xem trước dùng file gốc."),
        2500);
}

void MainWindow::onProxyStatusChanged(QString assetId, hc::ProxyStatus status) {
    Q_UNUSED(assetId);
    Q_UNUSED(status);
    // Cheap enough to just refresh the whole pool — it's a handful of
    // items and this only fires on state transitions, not per-frame.
    if (m_mediaPool) m_mediaPool->refresh();
}

void MainWindow::onProxyReady(QString assetId, QString proxyPath) {
    Q_UNUSED(proxyPath);
    if (m_playback) m_playback->onProxyReady(assetId);
    if (m_mediaPool) m_mediaPool->refresh();
}

void MainWindow::onProxyFailed(QString assetId, QString error) {
    auto asset = m_project->findAsset(assetId);
    const QString name = asset ? asset->displayName : assetId;
    statusBar()->showMessage(tr("Tạo proxy thất bại cho %1: %2").arg(name, error), 5000);
    if (m_mediaPool) m_mediaPool->refresh();
}

void MainWindow::onProxyQueueProgress(int done, int total) {
    if (total <= 0) return;
    if (done >= total) {
        statusBar()->showMessage(tr("Đã tạo xong proxy cho %1 media.").arg(total), 3000);
    } else {
        statusBar()->showMessage(tr("Đang tạo proxy: %1/%2...").arg(done).arg(total));
    }
}

void MainWindow::updateUndoRedoActions() {
    if (m_undoAction) m_undoAction->setEnabled(m_project && m_project->canUndo());
    if (m_redoAction) m_redoAction->setEnabled(m_project && m_project->canRedo());
}

void MainWindow::refreshTextPreview() {
    if (!m_preview || !m_project) return;
    const Ticks t = m_playback ? m_playback->currentTime() : 0;
    const Clip* textClip = m_project->timeline().topmostVisualClipAt(t);
    m_preview->setTextOverlay(textClip ? textClip->displayLabel : QString());
}

bool MainWindow::maybeSaveUnsavedChanges() {
    if (!m_modified) return true;
    const auto choice = QMessageBox::question(this, tr("Lưu thay đổi?"),
        tr("Dự án có thay đổi chưa được lưu. Bạn có muốn lưu trước không?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Save) {
        if (!onSaveProject()) {
            return false;
        }
    }
    m_modified = false;
    return true;
}

void MainWindow::onLanguageSelected(const QString& langCode) {
    if (LanguageManager::instance().setLanguage(langCode)) {
        updateUiTexts();
        statusBar()->showMessage(LTR("status.langChanged").arg(langCode), 2500);
    }
}

void MainWindow::onLoadCustomLanguage() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        LTR("lang.loadFile"),
        QString(),
        "HyggshiCut Language Pack (*.langhc);;All Files (*)");
    if (path.isEmpty()) return;

    QString err;
    if (LanguageManager::instance().loadFromFile(path, &err)) {
        QFileInfo fi(path);
        statusBar()->showMessage(LTR("lang.loadSuccess") + fi.fileName(), 3000);
        updateUiTexts();
    } else {
        QMessageBox::warning(this, tr("Lỗi"), LTR("lang.loadFail") + err);
    }
}

void MainWindow::onOpenPluginManager() {
    PluginManagerDialog dlg(this);
    dlg.exec();
}

void MainWindow::updateUiTexts() {
    menuBar()->clear();
    buildMenus();
    buildToolbar();
    if (m_mediaDock) m_mediaDock->setWindowTitle(LTR("dock.mediaPool"));
    if (m_timelineDock) m_timelineDock->setWindowTitle(LTR("dock.timeline"));
    if (m_transformDock) m_transformDock->setWindowTitle(LTR("dock.transform"));
    if (m_textDock) m_textDock->setWindowTitle(LTR("dock.text"));
    if (m_audioFilterDock) m_audioFilterDock->setWindowTitle(LTR("dock.audioFilter"));
    if (m_effectsDock) m_effectsDock->setWindowTitle(LTR("dock.effects"));
    if (m_mediaPool) m_mediaPool->retranslateUi();
    if (m_transformPanel) m_transformPanel->retranslateUi();
    if (m_textPanel) m_textPanel->retranslateUi();
    if (m_audioFilterPanel) m_audioFilterPanel->retranslateUi();
    if (m_effectsPanel) m_effectsPanel->retranslateUi();
    updateWindowTitle();
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!maybeSaveUnsavedChanges()) {
        event->ignore();
        return;
    }
    event->accept();
}

} // namespace hc
