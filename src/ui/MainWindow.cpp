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
#include "ScreenRecordDialog.h"
#include "WindowSettingsDialog.h"
#include "ThemeManager.h"
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
#include <QLabel>
#include <QKeySequence>
#include <QActionGroup>
#include <QSettings>
#include <QResizeEvent>
#include <QCloseEvent>
#include <QFileInfo>
#include <QTimer>
#include <cmath>

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

    // Permanent zoom readout in the status bar (updated live via
    // TimelineWidget::zoomChanged). Created once here so language/UI rebuilds
    // never accumulate duplicate labels.
    m_zoomLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_zoomLabel);

    rebuildProjectDependentUi();
    statusBar()->showMessage(tr("Sẵn sàng. Kéo media vào timeline để bắt đầu dựng."));
}

MainWindow::~MainWindow() {
    if (m_playback) {
        m_playback->pause();
    }
    if (m_timelineWidget) {
        m_timelineWidget->setProject(nullptr);
    }
    if (m_mediaPool) {
        m_mediaPool->setProject(nullptr);
    }
    if (m_transformPanel) {
        m_transformPanel->setSelectedClip(nullptr, {}, {});
    }
    if (m_textPanel) {
        m_textPanel->setSelectedClip(nullptr, {}, {});
    }
    if (m_audioFilterPanel) {
        m_audioFilterPanel->setSelectedClip(nullptr, {}, {});
    }
}

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
    fileMenu->addAction(LTR("menu.file.screenRecord"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this, &MainWindow::onScreenRecord);
    fileMenu->addAction(LTR("menu.file.relinkMedia"), this, &MainWindow::onRelinkMissingMedia);
    fileMenu->addSeparator();
    fileMenu->addAction(LTR("menu.file.export"), QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::onExport);
    fileMenu->addSeparator();
    fileMenu->addAction(LTR("menu.file.quit"), QKeySequence::Quit, this, &QWidget::close);

    auto* editMenu = menuBar()->addMenu(LTR("menu.edit"));
    m_undoAction = editMenu->addAction(LTR("menu.edit.undo"), QKeySequence::Undo, this, &MainWindow::onUndo);
    m_redoAction = editMenu->addAction(LTR("menu.edit.redo"), QKeySequence::Redo, this, &MainWindow::onRedo);
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::Key_Y)});

    editMenu->addSeparator();
    auto* cutToolAct = editMenu->addAction(LTR("menu.edit.cutTool"), QKeySequence(Qt::Key_C), this, [this]() {
        if (m_cutToolAction) m_cutToolAction->toggle();
    });
    cutToolAct->setCheckable(true);
    if (m_cutToolAction) {
        cutToolAct->setChecked(m_cutToolAction->isChecked());
        connect(m_cutToolAction, &QAction::toggled, cutToolAct, &QAction::setChecked);
    }

    editMenu->addAction(LTR("menu.edit.splitAtPlayhead"), QKeySequence(Qt::Key_S), this, &MainWindow::onSplitAtPlayhead);
    editMenu->addAction(LTR("menu.edit.deleteClip"), QKeySequence::Delete, this, &MainWindow::onDeleteSelectedClip);
    editMenu->addAction(LTR("menu.edit.deleteTrack"), QKeySequence(Qt::SHIFT | Qt::Key_Delete), this, &MainWindow::onDeleteSelectedTrack);

    editMenu->addSeparator();
    editMenu->addAction(tr("Sao chép clip"), QKeySequence::Copy, this, [this]() {
        if (m_timelineWidget) m_timelineWidget->copySelectedClip();
    });
    editMenu->addAction(tr("Dán clip"), QKeySequence::Paste, this, [this]() {
        if (m_timelineWidget) m_timelineWidget->pasteClip();
    });
    editMenu->addAction(tr("Nhân đôi clip"), QKeySequence(Qt::CTRL | Qt::Key_D), this, [this]() {
        if (m_timelineWidget) m_timelineWidget->duplicateSelectedClip();
    });
    editMenu->addSeparator();
    editMenu->addAction(tr("Xóa & dồn clip sau lại (Ripple delete)"), this, [this]() {
        if (m_timelineWidget) m_timelineWidget->rippleDeleteSelectedClip();
    });
    editMenu->addAction(tr("Dịch clip trái 1 khung hình (,)"), this, [this]() {
        if (m_timelineWidget) m_timelineWidget->nudgeSelectedClip(-m_timelineWidget->frameStepTicks());
    });
    editMenu->addAction(tr("Dịch clip phải 1 khung hình (.)"), this, [this]() {
        if (m_timelineWidget) m_timelineWidget->nudgeSelectedClip(m_timelineWidget->frameStepTicks());
    });

    editMenu->addSeparator();
    editMenu->addAction(tr("Chọn clip đầu tiên"), QKeySequence(Qt::CTRL | Qt::Key_A), this, &MainWindow::onSelectFirstClip);
    editMenu->addAction(tr("Bỏ chọn tất cả"), QKeySequence(Qt::Key_Escape), this, &MainWindow::onDeselectAll);

    editMenu->addSeparator();
    editMenu->addAction(LTR("menu.settings.preferences"), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, [this]() {
        openSettingsDialog(SettingsTab::Window);
    });

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
    m_viewMenu->addAction(tr("Vừa khớp toàn bộ timeline (Zoom to fit)"), QKeySequence(Qt::SHIFT | Qt::Key_Z), this, &MainWindow::onZoomToFit);
    m_viewMenu->addSeparator();

    // Snap toggle: when on (default), clip edges/keyframes/playhead magnetize
    // to each other during drags. Persisted and re-applied to every newly
    // created TimelineWidget (see rebuildProjectDependentUi).
    {
        QSettings snapPrefs("HyggshiCut", "Preferences");
        m_snapAction = m_viewMenu->addAction(tr("Bắt dính vào mép clip / playhead (Snap)"));
        m_snapAction->setCheckable(true);
        m_snapAction->setChecked(snapPrefs.value("timeline/snapEnabled", true).toBool());
        connect(m_snapAction, &QAction::toggled, this, [this](bool on) {
            QSettings p("HyggshiCut", "Preferences");
            p.setValue("timeline/snapEnabled", on);
            if (m_timelineWidget) m_timelineWidget->setSnapEnabled(on);
        });
    }

    m_viewMenu->addSeparator();

    auto* meterAct = m_viewMenu->addAction(tr("Đồng hồ đo âm lượng (VU Meter)"));
    meterAct->setCheckable(true);
    meterAct->setChecked(false);
    connect(meterAct, &QAction::toggled, this, [this](bool checked) {
        if (m_preview) m_preview->setAudioMeterVisible(checked);
    });
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(LTR("windowSettings.resetLayout"), this, &MainWindow::resetDockLayout);
    m_viewMenu->addSeparator();

    // --- Settings & Extensions Menu ---
    auto* settingsMenu = menuBar()->addMenu(LTR("menu.settings"));
    settingsMenu->addAction(LTR("menu.settings.preferences"), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, [this]() {
        openSettingsDialog(SettingsTab::Window);
    });
    settingsMenu->addAction(LTR("menu.settings.canvas"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this, &MainWindow::onProjectSettings);
    settingsMenu->addSeparator();

    // Theme / Appearance Submenu (Dark, Light, System)
    auto* themeSubMenu = settingsMenu->addMenu(LTR("menu.settings.theme"));
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    const QString curTheme = ThemeManager::currentTheme();
    struct ThemeItem { QString key; QString code; };
    const ThemeItem themeItems[] = {
        { "menu.settings.theme.dark", "dark" },
        { "menu.settings.theme.light", "light" },
        { "menu.settings.theme.system", "system" }
    };
    for (const auto& item : themeItems) {
        auto* act = themeSubMenu->addAction(LTR(item.key));
        act->setCheckable(true);
        act->setChecked(curTheme == item.code);
        themeGroup->addAction(act);
        const QString code = item.code;
        connect(act, &QAction::triggered, this, [this, code]() {
            onThemeSelected(code);
        });
    }

    // Graphics Backend Submenu (OpenGL 3.3, Latest OpenGL, Vulkan)
    auto* backendSubMenu = settingsMenu->addMenu(LTR("menu.settings.graphicsBackend"));
    auto* backendGroup = new QActionGroup(this);
    backendGroup->setExclusive(true);

    QSettings prefSettings("HyggshiCut", "Preferences");
    const QString curBackend = prefSettings.value("graphicsBackend", "opengl33").toString();

    struct BackendItem {
        QString key;
        QString code;
    };
    const BackendItem backendItems[] = {
        { "menu.settings.backend.gl33", "opengl33" },
        { "menu.settings.backend.glLatest", "opengl_latest" },
        { "menu.settings.backend.vulkan", "vulkan" }
    };

    for (const auto& item : backendItems) {
        QString text = LTR(item.key);
        auto* act = backendSubMenu->addAction(text);
        act->setCheckable(true);
        act->setChecked(curBackend == item.code);
        backendGroup->addAction(act);
        const QString code = item.code;
        connect(act, &QAction::triggered, this, [this, code]() {
            onGraphicsBackendSelected(code);
        });
    }

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

    // Proxy Submenu
    auto* proxySubMenu = settingsMenu->addMenu(LTR("menu.settings.proxy"));
    m_useProxyAction = proxySubMenu->addAction(LTR("menu.view.useProxy"));
    m_useProxyAction->setCheckable(true);
    m_useProxyAction->setChecked(prefSettings.value("proxy/useProxy", true).toBool());
    connect(m_useProxyAction, &QAction::toggled, this, &MainWindow::onToggleUseProxy);

    proxySubMenu->addAction(LTR("menu.settings.generateProxies"), this, &MainWindow::onGenerateProxiesForAll);
    proxySubMenu->addAction(LTR("menu.settings.proxy.openSettings"), this, [this]() {
        openSettingsDialog(SettingsTab::Proxy);
    });

    settingsMenu->addSeparator();
    settingsMenu->addAction(LTR("menu.settings.plugins"), this, &MainWindow::onOpenPluginManager);
    settingsMenu->addSeparator();
    settingsMenu->addAction(LTR("menu.settings.about"), this, &MainWindow::onAbout);

    // --- Performance Menu ---
    auto* perfMenu = menuBar()->addMenu(LTR("menu.perf"));
    perfMenu->addAction(LTR("menu.settings.generateProxies"), this, &MainWindow::onGenerateProxiesForAll);
    auto* perfProxyAct = perfMenu->addAction(LTR("menu.view.useProxy"));
    perfProxyAct->setCheckable(true);
    perfProxyAct->setChecked(m_useProxyAction->isChecked());
    connect(perfProxyAct, &QAction::toggled, m_useProxyAction, &QAction::setChecked);

    // --- Help Menu ---
    auto* helpMenu = menuBar()->addMenu(LTR("menu.help"));
    helpMenu->addAction(LTR("menu.help.about"), this, &MainWindow::onAbout);
}

void MainWindow::buildToolbar() {
    if (!m_mainToolbar) {
        m_mainToolbar = addToolBar(tr("Toolbar"));
        m_mainToolbar->setObjectName("MainToolbar");
        m_mainToolbar->setMovable(false);
    }
    m_mainToolbar->clear();

    const bool wasCutChecked = m_cutToolAction ? m_cutToolAction->isChecked() : false;

    m_cutToolAction = new QAction(LTR("menu.edit.cutTool"), this);
    m_cutToolAction->setText(LTR("menu.edit.cutTool"));
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
    m_mainToolbar->addAction(tr("Vừa khớp"), this, &MainWindow::onZoomToFit);
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
    m_mediaDock->setObjectName("MediaPoolDock");
    m_mediaDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, m_mediaDock);

    m_timelineDock = new QDockWidget(LTR("dock.timeline"), this);
    m_timelineDock->setObjectName("TimelineDock");
    m_timelineDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);

    m_transformDock = new QDockWidget(LTR("dock.transform"), this);
    m_transformDock->setObjectName("TransformDock");
    m_transformDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_transformDock);

    m_textDock = new QDockWidget(LTR("dock.text"), this);
    m_textDock->setObjectName("TextDock");
    m_textDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_textDock);
    tabifyDockWidget(m_transformDock, m_textDock);

    m_audioFilterDock = new QDockWidget(LTR("dock.audioFilter"), this);
    m_audioFilterDock->setObjectName("AudioFilterDock");
    m_audioFilterDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_audioFilterDock);
    tabifyDockWidget(m_textDock, m_audioFilterDock);

    m_effectsDock = new QDockWidget(LTR("dock.effects"), this);
    m_effectsDock->setObjectName("EffectsDock");
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
    if (m_snapAction) m_timelineWidget->setSnapEnabled(m_snapAction->isChecked());
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
    connect(m_mediaPool, &MediaPoolWidget::recordScreenRequested, this, &MainWindow::onScreenRecord);
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
    connect(m_timelineWidget, &TimelineWidget::zoomChanged, this, &MainWindow::updateZoomLabel);
    updateZoomLabel(m_timelineWidget->zoom());

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
        WindowSettings ws = WindowSettings::loadFromPreferences();
        applyWindowSettings(ws);

        if (ws.startupMode == "maximized") {
            setWindowState(windowState() | Qt::WindowMaximized);
        } else if (ws.startupMode == "fullscreen") {
            setWindowState(windowState() | Qt::WindowFullScreen);
        } else if (ws.startupMode == "default") {
            resize(1400, 900);
        } else {
            QSettings prefSettings("HyggshiCut", "Preferences");
            if (prefSettings.contains("window/geometry")) {
                restoreGeometry(prefSettings.value("window/geometry").toByteArray());
            }
            if (prefSettings.contains("window/state")) {
                restoreState(prefSettings.value("window/state").toByteArray());
            }
        }

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
        QSettings pref("HyggshiCut", "Preferences");
        if (pref.value("proxy/autoGenerate", false).toBool() && m_proxyManager && asset->hasVideo()) {
            m_proxyManager->requestProxy(asset);
        }
    }
    m_mediaPool->refresh();
    m_modified = true;
    updateWindowTitle();

    if (!failures.isEmpty()) {
        QMessageBox::warning(this, tr("Một số file không nhập được"), failures.join("\n"));
    }
}

void MainWindow::onScreenRecord() {
    ScreenRecordDialog dlg(this);
    connect(&dlg, &ScreenRecordDialog::recordingCompleted, this,
            [this](const QString& filePath, bool autoImport, bool insertTimeline) {
        if (autoImport) {
            importFileAndAddToProject(filePath, insertTimeline);
        }
    });
    dlg.exec();
}

void MainWindow::importFileAndAddToProject(const QString& filePath, bool addToTimeline) {
    if (filePath.isEmpty() || !QFile::exists(filePath)) return;

    QString err;
    auto asset = m_project->importMedia(filePath, &err);
    if (!asset) {
        QMessageBox::warning(this, tr("Lỗi nhập media"), err);
        return;
    }
    generateThumbnail(asset);
    generateWaveform(asset);
    QSettings pref("HyggshiCut", "Preferences");
    if (pref.value("proxy/autoGenerate", false).toBool() && m_proxyManager && asset->hasVideo()) {
        m_proxyManager->requestProxy(asset);
    }
    m_mediaPool->refresh();
    m_modified = true;
    updateWindowTitle();

    if (addToTimeline && m_timelineWidget) {
        m_project->pushUndoSnapshot();
        // Find or create a visual track
        Track* targetTrack = nullptr;
        for (auto& track : m_project->timeline().tracks()) {
            if (track.type == TrackType::Visual && !track.locked) {
                targetTrack = &track;
                break;
            }
        }
        if (!targetTrack) {
            const QString name = QString("Visual %1").arg(m_project->timeline().tracks().size() + 1);
            targetTrack = &m_project->timeline().addTrack(TrackType::Visual, name);
        }

        Clip clip;
        clip.assetId = asset->id;
        clip.type = ClipType::Video;
        clip.sourceIn = 0;
        clip.sourceOut = asset->duration > 0 ? asset->duration : secondsToTicks(5.0);

        // Place at current playhead position without overlapping existing clips
        Ticks placedStart = m_playback ? m_playback->currentTime() : 0;
        const Ticks duration = clip.timelineDuration();
        const auto& existing = targetTrack->clips();
        for (size_t guard = 0; guard < existing.size() + 1; ++guard) {
            bool collided = false;
            for (const auto& other : existing) {
                const Ticks oStart = other.timelineStart;
                const Ticks oEnd = other.timelineStart + other.timelineDuration();
                if (placedStart < oEnd && (placedStart + duration) > oStart) {
                    placedStart = oEnd;
                    collided = true;
                    break;
                }
            }
            if (!collided) break;
        }
        clip.timelineStart = placedStart;
        targetTrack->addClip(std::move(clip));
        onTimelineEdited();
        if (m_playback) m_playback->seek(placedStart);
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

    // Thumbnails and audio waveforms are derived in-memory data produced at
    // import time and are deliberately NOT stored in the .hcproj file (they
    // would bloat it and go stale). loadFromFile() re-probes each asset's
    // metadata but not its preview image, so without this pass the media
    // pool would reopen with every image/video row blank. Regenerate them
    // now so reopening a project looks exactly like the session it was
    // saved in. Missing/unlinkable media is handled inside each helper
    // (the decoder simply fails to open and we skip it).
    for (const auto& asset : newProject->assets()) {
        generateThumbnail(asset);
        generateWaveform(asset);
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
void MainWindow::onZoomIn() { if (m_timelineWidget) m_timelineWidget->zoomBy(1.25); }
void MainWindow::onZoomOut() { if (m_timelineWidget) m_timelineWidget->zoomBy(1.0 / 1.25); }
void MainWindow::onZoomToFit() { if (m_timelineWidget) m_timelineWidget->zoomToFit(); }

void MainWindow::updateZoomLabel(double pixelsPerSecond) {
    if (!m_zoomLabel) return;
    // Report zoom relative to the default (60 px/s = 100%).
    const int pct = static_cast<int>(std::lround(pixelsPerSecond / 60.0 * 100.0));
    m_zoomLabel->setText(tr("Thu phóng: %1%").arg(pct));
}

void MainWindow::onToggleCutTool(bool checked) {
    if (m_timelineWidget) m_timelineWidget->setCutToolActive(checked);
    statusBar()->showMessage(checked
        ? tr("Dao cắt đang bật — bấm vào clip trên timeline để cắt tại đó.")
        : QString(), checked ? 0 : 1);
}

void MainWindow::onUndo() {
    if (!m_project || !m_project->canUndo()) {
        statusBar()->showMessage(tr("Không có thao tác nào để hoàn tác (Undo)."), 2500);
        return;
    }
    if (!m_project->undo()) return;
    if (m_playback) m_playback->pause();
    if (m_timelineWidget) m_timelineWidget->clearSelection();
    m_selectedTrackId.clear();
    m_selectedClipId.clear();
    if (m_preview) m_preview->clearTransformOverlay();
    onTimelineEdited();
    updateUndoRedoActions();
    statusBar()->showMessage(tr("Đã hoàn tác (Undo)."), 1500);
}

void MainWindow::onRedo() {
    if (!m_project || !m_project->canRedo()) {
        statusBar()->showMessage(tr("Không có thao tác nào để làm lại (Redo)."), 2500);
        return;
    }
    if (!m_project->redo()) return;
    if (m_playback) m_playback->pause();
    if (m_timelineWidget) m_timelineWidget->clearSelection();
    m_selectedTrackId.clear();
    m_selectedClipId.clear();
    if (m_preview) m_preview->clearTransformOverlay();
    onTimelineEdited();
    updateUndoRedoActions();
    statusBar()->showMessage(tr("Đã làm lại (Redo)."), 1500);
}

void MainWindow::onSelectFirstClip() {
    if (!m_project) return;
    for (const auto& track : m_project->timeline().tracks()) {
        if (!track.clips().empty()) {
            m_selectedTrackId = track.id;
            m_selectedClipId = track.clips().front().id;
            onTimelineSelectionChanged(m_selectedClipId, m_selectedTrackId);
            statusBar()->showMessage(tr("Đã chọn clip: %1").arg(track.clips().front().displayLabel.isEmpty() ? track.clips().front().id : track.clips().front().displayLabel), 2000);
            return;
        }
    }
    statusBar()->showMessage(tr("Chưa có clip nào trên timeline để chọn."), 2000);
}

void MainWindow::onDeselectAll() {
    if (m_timelineWidget) m_timelineWidget->clearSelection();
    m_selectedTrackId.clear();
    m_selectedClipId.clear();
    if (m_preview) m_preview->clearTransformOverlay();
    if (m_transformPanel) m_transformPanel->setSelectedClip(m_project.get(), {}, {});
    if (m_textPanel) m_textPanel->setSelectedClip(m_project.get(), {}, {});
    if (m_audioFilterPanel) m_audioFilterPanel->setSelectedClip(m_project.get(), {}, {});
    statusBar()->showMessage(tr("Đã bỏ chọn tất cả."), 2000);
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
    if (m_undoAction) {
        m_undoAction->setEnabled(true);
        if (m_project && m_project->canUndo()) {
            m_undoAction->setToolTip(tr("Hoàn tác thao tác trước đó (Ctrl+Z)"));
        } else {
            m_undoAction->setToolTip(tr("Chưa có thao tác nào để hoàn tác"));
        }
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(true);
        if (m_project && m_project->canRedo()) {
            m_redoAction->setToolTip(tr("Làm lại thao tác vừa hoàn tác (Ctrl+Shift+Z hoặc Ctrl+Y)"));
        } else {
            m_redoAction->setToolTip(tr("Chưa có thao tác nào để làm lại"));
        }
    }
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

void MainWindow::onGraphicsBackendSelected(const QString& backend) {
    QSettings prefSettings("HyggshiCut", "Preferences");
    prefSettings.setValue("graphicsBackend", backend);

    QString name = "OpenGL 3.3 Core";
    if (backend == "opengl_latest") name = "OpenGL Mới nhất (Latest Core Profile)";
    else if (backend == "vulkan") name = "Vulkan (Experimental)";

    QString msg = LTR("menu.settings.backend.restartNotice").arg(name);

    QMessageBox::information(this, LTR("menu.settings.graphicsBackend"), msg);
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

void MainWindow::onWindowSettings() {
    openSettingsDialog(SettingsTab::Window);
}

void MainWindow::openSettingsDialog(SettingsTab tab) {
    // A QDialog::exec() runs its own nested event loop, but QTimer-driven
    // repaints elsewhere in the app (notably the preview's playback timer,
    // which keeps calling GLVideoWidget::update()/paintGL on its own clock)
    // still fire *during* that nested loop, because they belong to the same
    // thread. If playback is running when the dialog is shown, paintGL can
    // end up racing the dialog's own window-activation/backing-store setup
    // for the shared GL context — on some drivers (real GPU, not the
    // software llvmpipe path) this segfaults inside Qt's own repaint code.
    // Pausing first removes the race entirely; it costs nothing when
    // playback wasn't running.
    if (m_playback) m_playback->pause();

    WindowSettingsDialog dlg(this, tab);
    connect(&dlg, &WindowSettingsDialog::applySettings, this, &MainWindow::applyWindowSettings);
    connect(&dlg, &WindowSettingsDialog::resetLayoutRequested, this, &MainWindow::resetDockLayout);
    connect(&dlg, &WindowSettingsDialog::generateProxiesRequested, this, &MainWindow::onGenerateProxiesForAll);
    connect(&dlg, &WindowSettingsDialog::proxyUsageToggled, this, &MainWindow::onToggleUseProxy);
    connect(&dlg, &WindowSettingsDialog::languageChanged, this, &MainWindow::onLanguageSelected);
    connect(&dlg, &WindowSettingsDialog::themeChanged, this, &MainWindow::onThemeSelected);
    dlg.exec();
}

void MainWindow::onThemeSelected(const QString& theme) {
    ThemeManager::setTheme(theme);
    updateUiTexts();
    statusBar()->showMessage(QString("Đã áp dụng giao diện: %1").arg(theme), 2500);
}

void MainWindow::onAbout() {
    openSettingsDialog(SettingsTab::About);
}

void MainWindow::applyWindowSettings(const hc::WindowSettings& settings) {
    // 1. Always on top
    Qt::WindowFlags flags = windowFlags();
    if (settings.alwaysOnTop) {
        flags |= Qt::WindowStaysOnTopHint;
    } else {
        flags &= ~Qt::WindowStaysOnTopHint;
    }
    if (flags != windowFlags()) {
        const bool wasVisible = isVisible();
        setWindowFlags(flags);
        if (wasVisible) show();
    }

    // 2. Opacity
    setWindowOpacity(settings.opacityPercent / 100.0);

    // 3. Lock docks
    QDockWidget::DockWidgetFeatures features = QDockWidget::NoDockWidgetFeatures;
    if (!settings.lockDocks) {
        features = QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable;
    }
    if (m_mediaDock) m_mediaDock->setFeatures(features);
    if (m_timelineDock) m_timelineDock->setFeatures(features);
    if (m_transformDock) m_transformDock->setFeatures(features);
    if (m_textDock) m_textDock->setFeatures(features);
    if (m_audioFilterDock) m_audioFilterDock->setFeatures(features);
    if (m_effectsDock) m_effectsDock->setFeatures(features);

    // 4. Bars visibility
    if (m_mainToolbar) m_mainToolbar->setVisible(settings.showToolbar);
    if (statusBar()) statusBar()->setVisible(settings.showStatusBar);
}

void MainWindow::resetDockLayout() {
    ensureDocks();
    addDockWidget(Qt::LeftDockWidgetArea, m_mediaDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_timelineDock);
    addDockWidget(Qt::RightDockWidgetArea, m_transformDock);
    addDockWidget(Qt::RightDockWidgetArea, m_textDock);
    tabifyDockWidget(m_transformDock, m_textDock);
    addDockWidget(Qt::RightDockWidgetArea, m_audioFilterDock);
    tabifyDockWidget(m_textDock, m_audioFilterDock);
    addDockWidget(Qt::RightDockWidgetArea, m_effectsDock);
    tabifyDockWidget(m_audioFilterDock, m_effectsDock);

    m_mediaDock->show();
    m_timelineDock->show();
    m_transformDock->show();
    m_textDock->show();
    m_audioFilterDock->show();
    m_effectsDock->show();
    m_transformDock->raise();

    const int timelineH = std::clamp(height() * 35 / 100, 240, 450);
    resizeDocks({m_timelineDock}, {timelineH}, Qt::Vertical);
    resizeDocks({m_mediaDock, m_transformDock}, {260, 320}, Qt::Horizontal);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    WindowSettings ws = WindowSettings::loadFromPreferences();
    if (ws.confirmExit && !maybeSaveUnsavedChanges()) {
        event->ignore();
        return;
    }
    if (ws.startupMode == "remember") {
        QSettings prefSettings("HyggshiCut", "Preferences");
        prefSettings.setValue("window/geometry", saveGeometry());
        prefSettings.setValue("window/state", saveState());
    }
    event->accept();
}

} // namespace hc
