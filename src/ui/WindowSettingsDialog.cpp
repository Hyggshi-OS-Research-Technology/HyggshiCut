#include "WindowSettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <QGuiApplication>
#include <QScrollArea>
#include <QClipboard>
#include <QApplication>
#include <QFrame>
#include <QToolTip>
#include "../core/EnvironmentCheck.h"
#include "../core/Autosave.h"
#include "../i18n/LanguageManager.h"
#include "../render/GLVideoWidget.h"
#include "../cache/ProxyManager.h"
#include "ThemeManager.h"

namespace hc {

WindowSettings WindowSettings::loadFromPreferences() {
    QSettings s("HyggshiCut", "Preferences");
    WindowSettings ws;
    ws.startupMode = s.value("window/startupMode", "remember").toString();
    ws.opacityPercent = s.value("window/opacityPercent", 100).toInt();
    if (ws.opacityPercent < 70 || ws.opacityPercent > 100) ws.opacityPercent = 100;
    ws.alwaysOnTop = s.value("window/alwaysOnTop", false).toBool();
    ws.lockDocks = s.value("window/lockDocks", false).toBool();
    ws.confirmExit = s.value("window/confirmExit", true).toBool();
    ws.showToolbar = s.value("window/showToolbar", true).toBool();
    ws.showStatusBar = s.value("window/showStatusBar", true).toBool();
    return ws;
}

void WindowSettings::saveToPreferences() const {
    QSettings s("HyggshiCut", "Preferences");
    s.setValue("window/startupMode", startupMode);
    s.setValue("window/opacityPercent", opacityPercent);
    s.setValue("window/alwaysOnTop", alwaysOnTop);
    s.setValue("window/lockDocks", lockDocks);
    s.setValue("window/confirmExit", confirmExit);
    s.setValue("window/showToolbar", showToolbar);
    s.setValue("window/showStatusBar", showStatusBar);
}

WindowSettingsDialog::WindowSettingsDialog(QWidget* parent, SettingsTab initialTab)
    : QDialog(parent) {
    setWindowTitle(LTR("menu.settings") + " - HyggshiCut");
    setMinimumWidth(720);
    setMinimumHeight(520);
    resize(740, 560);

    setupUi();
    loadValues();
    setCurrentTab(initialTab);
}

WindowSettingsDialog::~WindowSettingsDialog() = default;

void WindowSettingsDialog::setCurrentTab(SettingsTab tab) {
    if (m_tabWidget) {
        m_tabWidget->setCurrentIndex(static_cast<int>(tab));
    }
}

void WindowSettingsDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setUsesScrollButtons(false);
    m_tabWidget->setStyleSheet("QTabBar::tab { min-width: 90px; padding: 6px 10px; font-size: 12px; }");

    m_tabWidget->addTab(createWindowTab(), LTR("settings.tab.window"));
    m_tabWidget->addTab(createAppearanceTab(), LTR("settings.tab.appearance"));
    m_tabWidget->addTab(createLanguageTab(), LTR("settings.tab.language"));
    m_tabWidget->addTab(createGraphicsTab(), LTR("settings.tab.graphics"));
    m_tabWidget->addTab(createProxyTab(), LTR("settings.tab.proxy"));
    m_tabWidget->addTab(createEnvironmentTab(), LTR("settings.tab.environment"));
    m_tabWidget->addTab(createAboutTab(), LTR("settings.tab.about"));

    mainLayout->addWidget(m_tabWidget);

    // Bottom Action Buttons
    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->setContentsMargins(4, 4, 4, 4);
    bottomLayout->setSpacing(8);
    bottomLayout->addStretch();

    auto getLabel = [](const QString& key1, const QString& key2, const QString& fallback) -> QString {
        QString s = LTR(key1.toUtf8().constData());
        if (!s.isEmpty() && s != key1) return s;
        s = LTR(key2.toUtf8().constData());
        if (!s.isEmpty() && s != key2) return s;
        return fallback;
    };

    m_okBtn = new QPushButton(getLabel("dialog.ok", "btn.ok", "OK"), this);
    m_okBtn->setStyleSheet("QPushButton { background-color: #e06c1b; color: #ffffff; font-weight: bold; border-radius: 4px; padding: 6px 22px; font-size: 13px; } QPushButton:hover { background-color: #ff7f2a; }");
    connect(m_okBtn, &QPushButton::clicked, this, &WindowSettingsDialog::onOkClicked);

    m_applyBtn = new QPushButton(getLabel("dialog.apply", "btn.apply", "Apply"), this);
    m_applyBtn->setStyleSheet("QPushButton { background-color: #383844; color: #eeeeee; border-radius: 4px; padding: 6px 18px; font-size: 13px; } QPushButton:hover { background-color: #4a4a58; }");
    connect(m_applyBtn, &QPushButton::clicked, this, &WindowSettingsDialog::onApplyClicked);

    m_cancelBtn = new QPushButton(getLabel("dialog.cancel", "btn.cancel", "Cancel"), this);
    m_cancelBtn->setStyleSheet("QPushButton { background-color: #2c2c34; color: #cccccc; border-radius: 4px; padding: 6px 18px; font-size: 13px; } QPushButton:hover { background-color: #3c3c46; color: #ffffff; }");
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    bottomLayout->addWidget(m_okBtn);
    bottomLayout->addWidget(m_applyBtn);
    bottomLayout->addWidget(m_cancelBtn);

    mainLayout->addLayout(bottomLayout);
}

QWidget* WindowSettingsDialog::createWindowTab() {
    auto* tab = new QWidget(this);
    auto* tabLayout = new QVBoxLayout(tab);

    // 1. Startup & Size
    auto* sizeGroup = new QGroupBox(LTR("windowSettings.startupGroup"), tab);
    auto* sizeForm = new QFormLayout(sizeGroup);

    m_startupModeCombo = new QComboBox(sizeGroup);
    m_startupModeCombo->addItem(LTR("windowSettings.startupRemember"), "remember");
    m_startupModeCombo->addItem(LTR("windowSettings.startupMaximized"), "maximized");
    m_startupModeCombo->addItem(LTR("windowSettings.startupFullscreen"), "fullscreen");
    m_startupModeCombo->addItem(LTR("windowSettings.startupDefault"), "default");
    sizeForm->addRow(LTR("windowSettings.startupMode"), m_startupModeCombo);

    auto* opacityLayout = new QHBoxLayout();
    m_opacitySlider = new QSlider(Qt::Horizontal, sizeGroup);
    m_opacitySlider->setRange(70, 100);
    m_opacityValLabel = new QLabel("100%", sizeGroup);
    m_opacityValLabel->setMinimumWidth(40);
    opacityLayout->addWidget(m_opacitySlider);
    opacityLayout->addWidget(m_opacityValLabel);
    connect(m_opacitySlider, &QSlider::valueChanged, this, &WindowSettingsDialog::onOpacitySliderChanged);
    sizeForm->addRow(LTR("windowSettings.opacity"), opacityLayout);

    tabLayout->addWidget(sizeGroup);

    // 2. Behavior
    auto* behaviorGroup = new QGroupBox(LTR("windowSettings.behaviorGroup"), tab);
    auto* behaviorLayout = new QVBoxLayout(behaviorGroup);

    m_alwaysOnTopCheck = new QCheckBox(LTR("windowSettings.alwaysOnTop"), behaviorGroup);
    m_lockDocksCheck = new QCheckBox(LTR("windowSettings.lockDocks"), behaviorGroup);
    m_confirmExitCheck = new QCheckBox(LTR("windowSettings.confirmExit"), behaviorGroup);

    behaviorLayout->addWidget(m_alwaysOnTopCheck);
    behaviorLayout->addWidget(m_lockDocksCheck);
    behaviorLayout->addWidget(m_confirmExitCheck);
    tabLayout->addWidget(behaviorGroup);

    // 3. Toolbar & Status bar & Layout
    auto* uiElementsGroup = new QGroupBox(LTR("windowSettings.uiElementsGroup"), tab);
    auto* uiElementsLayout = new QVBoxLayout(uiElementsGroup);

    m_showToolbarCheck = new QCheckBox(LTR("windowSettings.showToolbar"), uiElementsGroup);
    m_showStatusBarCheck = new QCheckBox(LTR("windowSettings.showStatusBar"), uiElementsGroup);
    uiElementsLayout->addWidget(m_showToolbarCheck);
    uiElementsLayout->addWidget(m_showStatusBarCheck);

    m_resetLayoutBtn = new QPushButton(LTR("windowSettings.resetLayout"), uiElementsGroup);
    connect(m_resetLayoutBtn, &QPushButton::clicked, this, &WindowSettingsDialog::onResetLayoutClicked);
    uiElementsLayout->addWidget(m_resetLayoutBtn);

    tabLayout->addWidget(uiElementsGroup);

    // 4. Autosave & crash recovery. Lives here rather than in its own tab
    //    because it is window/session behaviour, next to "confirm on exit".
    auto* autosaveGroup = new QGroupBox(LTR("settings.autosave.title"), tab);
    auto* autosaveLayout = new QVBoxLayout(autosaveGroup);

    m_autosaveEnableCheck = new QCheckBox(LTR("settings.autosave.enable"), autosaveGroup);
    autosaveLayout->addWidget(m_autosaveEnableCheck);

    auto* intervalRow = new QHBoxLayout();
    intervalRow->addWidget(new QLabel(LTR("settings.autosave.interval"), autosaveGroup));
    m_autosaveIntervalSpin = new QSpinBox(autosaveGroup);
    m_autosaveIntervalSpin->setRange(AutosaveManager::kMinIntervalSec,
                                     AutosaveManager::kMaxIntervalSec);
    m_autosaveIntervalSpin->setSingleStep(15);
    m_autosaveIntervalSpin->setSuffix(LTR("settings.autosave.seconds"));
    intervalRow->addWidget(m_autosaveIntervalSpin);
    intervalRow->addStretch();
    autosaveLayout->addLayout(intervalRow);

    // The interval is meaningless while autosave is off; grey it out rather
    // than letting the user set a value that does nothing.
    connect(m_autosaveEnableCheck, &QCheckBox::toggled,
            m_autosaveIntervalSpin, &QWidget::setEnabled);

    auto* autosaveHint = new QLabel(LTR("settings.autosave.hint"), autosaveGroup);
    autosaveHint->setWordWrap(true);
    autosaveHint->setStyleSheet("color: #888; font-size: 11px;");
    autosaveLayout->addWidget(autosaveHint);

    tabLayout->addWidget(autosaveGroup);
    tabLayout->addStretch();
    return tab;
}

QWidget* WindowSettingsDialog::createAppearanceTab() {
    auto* tab = new QWidget(this);
    auto* tabLayout = new QVBoxLayout(tab);

    auto* themeGroup = new QGroupBox(LTR("settings.appearance.themeTitle"), tab);
    auto* groupLayout = new QVBoxLayout(themeGroup);

    m_themeGroup = new QButtonGroup(this);

    m_darkThemeRadio = new QRadioButton(LTR("settings.appearance.darkTheme"), themeGroup);
    m_lightThemeRadio = new QRadioButton(LTR("settings.appearance.lightTheme"), themeGroup);
    m_systemThemeRadio = new QRadioButton(LTR("settings.appearance.systemTheme"), themeGroup);

    m_themeGroup->addButton(m_darkThemeRadio, 0);
    m_themeGroup->addButton(m_lightThemeRadio, 1);
    m_themeGroup->addButton(m_systemThemeRadio, 2);

    groupLayout->addWidget(m_darkThemeRadio);
    auto* darkDesc = new QLabel(LTR("settings.appearance.darkDesc"), themeGroup);
    darkDesc->setStyleSheet("color: #888; margin-left: 24px; margin-bottom: 8px; font-size: 11px;");
    groupLayout->addWidget(darkDesc);

    groupLayout->addWidget(m_lightThemeRadio);
    auto* lightDesc = new QLabel(LTR("settings.appearance.lightDesc"), themeGroup);
    lightDesc->setStyleSheet("color: #888; margin-left: 24px; margin-bottom: 8px; font-size: 11px;");
    groupLayout->addWidget(lightDesc);

    groupLayout->addWidget(m_systemThemeRadio);
    auto* sysDesc = new QLabel(LTR("settings.appearance.systemDesc"), themeGroup);
    sysDesc->setStyleSheet("color: #888; margin-left: 24px; margin-bottom: 8px; font-size: 11px;");
    groupLayout->addWidget(sysDesc);

    connect(m_darkThemeRadio, &QRadioButton::toggled, this, &WindowSettingsDialog::onThemeChanged);
    connect(m_lightThemeRadio, &QRadioButton::toggled, this, &WindowSettingsDialog::onThemeChanged);
    connect(m_systemThemeRadio, &QRadioButton::toggled, this, &WindowSettingsDialog::onThemeChanged);

    tabLayout->addWidget(themeGroup);
    tabLayout->addStretch();
    return tab;
}

QWidget* WindowSettingsDialog::createLanguageTab() {
    auto* tab = new QWidget(this);
    auto* tabLayout = new QVBoxLayout(tab);

    auto* langGroup = new QGroupBox(LTR("menu.settings.language"), tab);
    auto* formLayout = new QFormLayout(langGroup);

    m_languageCombo = new QComboBox(langGroup);
    const auto langs = LanguageManager::instance().availableLanguages();
    const QString cur = LanguageManager::instance().currentLanguage();

    int selectedIdx = 0;
    for (int i = 0; i < langs.size(); ++i) {
        m_languageCombo->addItem(langs[i].nativeName, langs[i].languageCode);
        if (langs[i].languageCode == cur) selectedIdx = i;
    }
    m_languageCombo->setCurrentIndex(selectedIdx);
    connect(m_languageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WindowSettingsDialog::onLanguageComboChanged);

    formLayout->addRow(LTR("settings.language.select"), m_languageCombo);

    m_languageInfoLabel = new QLabel(langGroup);
    m_languageInfoLabel->setStyleSheet("color: #aaa; padding: 6px 0;");
    formLayout->addRow(m_languageInfoLabel);

    m_loadLanguageBtn = new QPushButton(LTR("lang.loadFile"), langGroup);
    connect(m_loadLanguageBtn, &QPushButton::clicked, this, &WindowSettingsDialog::onLoadLanguageClicked);
    formLayout->addRow("", m_loadLanguageBtn);

    tabLayout->addWidget(langGroup);
    tabLayout->addStretch();
    return tab;
}

QWidget* WindowSettingsDialog::createGraphicsTab() {
    auto* tab = new QWidget(this);
    auto* tabLayout = new QVBoxLayout(tab);

    auto* backendGroup = new QGroupBox(LTR("menu.settings.graphicsBackend"), tab);
    auto* groupLayout = new QVBoxLayout(backendGroup);

    m_backendGroup = new QButtonGroup(this);

    m_gl33Radio = new QRadioButton(LTR("menu.settings.backend.gl33"), backendGroup);
    m_glLatestRadio = new QRadioButton(LTR("menu.settings.backend.glLatest"), backendGroup);
    m_vulkanRadio = new QRadioButton(LTR("menu.settings.backend.vulkan"), backendGroup);

    m_backendGroup->addButton(m_gl33Radio, 0);
    m_backendGroup->addButton(m_glLatestRadio, 1);
    m_backendGroup->addButton(m_vulkanRadio, 2);

    groupLayout->addWidget(m_gl33Radio);
    groupLayout->addWidget(m_glLatestRadio);
    groupLayout->addWidget(m_vulkanRadio);

    connect(m_gl33Radio, &QRadioButton::toggled, this, &WindowSettingsDialog::onGraphicsBackendChanged);
    connect(m_glLatestRadio, &QRadioButton::toggled, this, &WindowSettingsDialog::onGraphicsBackendChanged);
    connect(m_vulkanRadio, &QRadioButton::toggled, this, &WindowSettingsDialog::onGraphicsBackendChanged);

    tabLayout->addWidget(backendGroup);

    // Active GPU Information Card
    auto* gpuCard = new QGroupBox(LTR("settings.graphics.gpuCardTitle"), tab);
    auto* cardLayout = new QFormLayout(gpuCard);

    m_gpuRendererLabel = new QLabel(GLVideoWidget::rendererString(), gpuCard);
    m_gpuRendererLabel->setStyleSheet("font-weight: bold; color: #ff9944;");

    m_gpuVersionLabel = new QLabel(GLVideoWidget::versionString(), gpuCard);
    m_gpuGlslLabel = new QLabel(GLVideoWidget::glslVersionString(), gpuCard);

    cardLayout->addRow(LTR("settings.graphics.renderer"), m_gpuRendererLabel);
    cardLayout->addRow(LTR("settings.graphics.driverVersion"), m_gpuVersionLabel);
    cardLayout->addRow(LTR("settings.graphics.glslVersion"), m_gpuGlslLabel);

    auto* noteLabel = new QLabel(LTR("settings.graphics.restartNote"), gpuCard);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet("color: #888; font-style: italic; margin-top: 6px;");
    cardLayout->addRow(noteLabel);

    tabLayout->addWidget(gpuCard);
    tabLayout->addStretch();
    return tab;
}

QWidget* WindowSettingsDialog::createProxyTab() {
    auto* tab = new QWidget(this);
    auto* tabLayout = new QVBoxLayout(tab);

    auto* proxyGroup = new QGroupBox(LTR("settings.proxy.title"), tab);
    auto* proxyForm = new QFormLayout(proxyGroup);

    m_useProxyCheck = new QCheckBox(LTR("menu.view.useProxy"), proxyGroup);
    proxyForm->addRow(m_useProxyCheck);

    m_proxyResolutionCombo = new QComboBox(proxyGroup);
    m_proxyResolutionCombo->addItem("Tự động (4K/8K → 720p, HD → 480p) (Mặc định)", 0);
    m_proxyResolutionCombo->addItem("480p (854x480) - Tiết kiệm RAM/VRAM", 854);
    m_proxyResolutionCombo->addItem("540p (960x540) - Cân bằng", 960);
    m_proxyResolutionCombo->addItem("720p (1280x720) - Chất lượng cao", 1280);
    m_proxyResolutionCombo->addItem("360p (640x360) - Siêu nhanh", 640);
    proxyForm->addRow(LTR("settings.proxy.resolution"), m_proxyResolutionCombo);

    auto* dirLayout = new QHBoxLayout();
    m_proxyDirEdit = new QLineEdit(proxyGroup);
    m_proxyDirEdit->setReadOnly(true);
    m_browseProxyDirBtn = new QPushButton(LTR("dialog.browse"), proxyGroup);
    connect(m_browseProxyDirBtn, &QPushButton::clicked, this, &WindowSettingsDialog::onBrowseProxyDirClicked);
    dirLayout->addWidget(m_proxyDirEdit);
    dirLayout->addWidget(m_browseProxyDirBtn);
    proxyForm->addRow(LTR("settings.proxy.cacheDir"), dirLayout);

    m_proxyCacheSizeLabel = new QLabel(proxyGroup);
    m_proxyCacheSizeLabel->setStyleSheet("color: #aaa;");
    proxyForm->addRow(LTR("settings.proxy.cacheSize"), m_proxyCacheSizeLabel);

    m_autoGenProxyCheck = new QCheckBox(LTR("settings.proxy.autoGenOnImport"), proxyGroup);
    proxyForm->addRow(m_autoGenProxyCheck);

    tabLayout->addWidget(proxyGroup);

    // Proxy Actions
    auto* actGroup = new QGroupBox(LTR("settings.proxy.actionsTitle"), tab);
    auto* actLayout = new QHBoxLayout(actGroup);

    m_genProxiesBtn = new QPushButton(LTR("menu.settings.generateProxies"), actGroup);
    connect(m_genProxiesBtn, &QPushButton::clicked, this, &WindowSettingsDialog::onGenerateProxiesClicked);

    m_clearProxyCacheBtn = new QPushButton(LTR("settings.proxy.clearCache"), actGroup);
    connect(m_clearProxyCacheBtn, &QPushButton::clicked, this, &WindowSettingsDialog::onClearProxyCacheClicked);

    actLayout->addWidget(m_genProxiesBtn);
    actLayout->addWidget(m_clearProxyCacheBtn);

    tabLayout->addWidget(actGroup);
    tabLayout->addStretch();
    return tab;
}

namespace {

// Colour for each status, matching the dark theme's accent palette.
QString envStatusColor(hc::CheckStatus s) {
    switch (s) {
        case hc::CheckStatus::Ok:      return QStringLiteral("#4caf50");
        case hc::CheckStatus::Warning: return QStringLiteral("#ffb300");
        case hc::CheckStatus::Error:   return QStringLiteral("#e53935");
        case hc::CheckStatus::Unknown: break;
    }
    return QStringLiteral("#888888");
}

QString envStatusGlyph(hc::CheckStatus s) {
    switch (s) {
        case hc::CheckStatus::Ok:      return QStringLiteral("OK");
        case hc::CheckStatus::Warning: return QStringLiteral("!");
        case hc::CheckStatus::Error:   return QStringLiteral("X");
        case hc::CheckStatus::Unknown: break;
    }
    return QStringLiteral("?");
}

} // namespace

QWidget* WindowSettingsDialog::createEnvironmentTab() {
    auto* tab = new QWidget(this);
    auto* tabLayout = new QVBoxLayout(tab);

    auto* intro = new QLabel(LTR("settings.env.intro"), tab);
    intro->setWordWrap(true);
    intro->setStyleSheet("color: #aaa;");
    tabLayout->addWidget(intro);

    m_envSummaryLabel = new QLabel(tab);
    m_envSummaryLabel->setWordWrap(true);
    m_envSummaryLabel->setStyleSheet("font-weight: bold; padding: 6px 0;");
    tabLayout->addWidget(m_envSummaryLabel);

    // The result rows are rebuilt on every run, so they live inside a
    // dedicated host widget whose layout we can clear wholesale.
    auto* scroll = new QScrollArea(tab);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_envResultsHost = new QWidget(scroll);
    auto* hostLayout = new QVBoxLayout(m_envResultsHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(8);
    scroll->setWidget(m_envResultsHost);
    tabLayout->addWidget(scroll, 1);

    auto* btnRow = new QHBoxLayout();
    m_envRunBtn = new QPushButton(LTR("settings.env.run"), tab);
    connect(m_envRunBtn, &QPushButton::clicked, this, &WindowSettingsDialog::onRunEnvironmentCheck);
    btnRow->addWidget(m_envRunBtn);

    m_envCopyBtn = new QPushButton(LTR("settings.env.copy"), tab);
    m_envCopyBtn->setEnabled(false);
    connect(m_envCopyBtn, &QPushButton::clicked, this, &WindowSettingsDialog::onCopyEnvironmentReport);
    btnRow->addWidget(m_envCopyBtn);

    btnRow->addStretch();
    tabLayout->addLayout(btnRow);

    // Probing spawns ffmpeg twice, so it is not run until the user asks.
    m_envSummaryLabel->setText(LTR("settings.env.notRun"));
    return tab;
}

void WindowSettingsDialog::onRunEnvironmentCheck() {
    if (!m_envResultsHost) return;

    auto* hostLayout = qobject_cast<QVBoxLayout*>(m_envResultsHost->layout());
    if (!hostLayout) return;

    // Clear previous results.
    while (QLayoutItem* item = hostLayout->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }

    if (m_envRunBtn) {
        m_envRunBtn->setEnabled(false);
        m_envRunBtn->setText(LTR("settings.env.running"));
    }
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    // Let the button repaint before the (blocking) ffmpeg probes start.
    QApplication::processEvents();

    // The GPU entry needs a live GL context, which only the preview widget
    // has; GLVideoWidget caches the driver strings from its initializeGL().
    hc::CheckResult gpu;
    gpu.id = "gpu.opengl";
    gpu.name = "OpenGL renderer";
    if (!hc::GLVideoWidget::glProbed()) {
        gpu.status = hc::CheckStatus::Unknown;
        gpu.value = LTR("settings.env.gpuNotReady");
        gpu.detail = LTR("settings.env.gpuNotReadyDetail");
    } else {
        const QString renderer = hc::GLVideoWidget::rendererString();
        gpu.value = renderer;
        gpu.detail = QStringLiteral("%1\nGLSL %2")
                         .arg(hc::GLVideoWidget::versionString(),
                              hc::GLVideoWidget::glslVersionString());
        if (!hc::GLVideoWidget::glShadersLinked()) {
            gpu.status = hc::CheckStatus::Error;
            gpu.remedy = LTR("settings.env.gpuShaderFail");
        } else if (renderer.contains("llvmpipe", Qt::CaseInsensitive) ||
                   renderer.contains("softpipe", Qt::CaseInsensitive) ||
                   renderer.contains("swrast", Qt::CaseInsensitive)) {
            // Mesa's software rasterisers. GL works, but compositing is on the
            // CPU and preview will be slow, which is worth flagging explicitly.
            gpu.status = hc::CheckStatus::Warning;
            gpu.remedy = LTR("settings.env.gpuSoftware");
        } else {
            gpu.status = hc::CheckStatus::Ok;
        }
    }

    const QList<hc::CheckGroup> groups = hc::EnvironmentCheck::runWithGpu(gpu);
    m_envReportText = hc::EnvironmentCheck::toPlainText(groups);

    for (const hc::CheckGroup& g : groups) {
        auto* box = new QGroupBox(g.name, m_envResultsHost);
        auto* boxLayout = new QVBoxLayout(box);
        boxLayout->setSpacing(6);

        for (const hc::CheckResult& res : g.results) {
            auto* row = new QWidget(box);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(8);

            auto* badge = new QLabel(envStatusGlyph(res.status), row);
            badge->setFixedWidth(26);
            badge->setAlignment(Qt::AlignCenter);
            badge->setStyleSheet(
                QStringLiteral("background: %1; color: #fff; border-radius: 3px; "
                               "font-weight: bold; font-size: 10px; padding: 2px 0;")
                    .arg(envStatusColor(res.status)));
            rowLayout->addWidget(badge, 0, Qt::AlignTop);

            QString text = QStringLiteral("<b>%1</b>: %2")
                               .arg(res.name.toHtmlEscaped(), res.value.toHtmlEscaped());
            if (!res.detail.isEmpty()) {
                text += QStringLiteral("<br><span style='color:#999;font-size:11px;'>%1</span>")
                            .arg(res.detail.toHtmlEscaped().replace('\n', QStringLiteral("<br>")));
            }
            if (!res.remedy.isEmpty() && res.status != hc::CheckStatus::Ok) {
                text += QStringLiteral("<br><span style='color:%1;font-size:11px;'>&#8594; %2</span>")
                            .arg(envStatusColor(res.status), res.remedy.toHtmlEscaped());
            }
            auto* label = new QLabel(text, row);
            label->setWordWrap(true);
            label->setTextInteractionFlags(Qt::TextSelectableByMouse);
            rowLayout->addWidget(label, 1);

            boxLayout->addWidget(row);
        }
        hostLayout->addWidget(box);
    }
    hostLayout->addStretch();

    // Summary line.
    if (hc::EnvironmentCheck::hasErrors(groups)) {
        m_envSummaryLabel->setText(LTR("settings.env.summaryError"));
        m_envSummaryLabel->setStyleSheet("font-weight: bold; padding: 6px 0; color: #e53935;");
    } else if (hc::EnvironmentCheck::hasWarnings(groups)) {
        m_envSummaryLabel->setText(LTR("settings.env.summaryWarning"));
        m_envSummaryLabel->setStyleSheet("font-weight: bold; padding: 6px 0; color: #ffb300;");
    } else {
        m_envSummaryLabel->setText(LTR("settings.env.summaryOk"));
        m_envSummaryLabel->setStyleSheet("font-weight: bold; padding: 6px 0; color: #4caf50;");
    }

    QGuiApplication::restoreOverrideCursor();
    if (m_envRunBtn) {
        m_envRunBtn->setEnabled(true);
        m_envRunBtn->setText(LTR("settings.env.rerun"));
    }
    if (m_envCopyBtn) m_envCopyBtn->setEnabled(true);
}

void WindowSettingsDialog::onCopyEnvironmentReport() {
    if (m_envReportText.isEmpty()) return;
    QGuiApplication::clipboard()->setText(m_envReportText);
    if (m_envCopyBtn) {
        QToolTip::showText(m_envCopyBtn->mapToGlobal(QPoint(0, 0)),
                           LTR("settings.env.copied"), m_envCopyBtn);
    }
}

QWidget* WindowSettingsDialog::createAboutTab() {
    auto* tab = new QWidget(this);
    auto* tabLayout = new QVBoxLayout(tab);

    auto* titleLabel = new QLabel("HyggshiCut Video Editor", tab);
    titleLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #ff9944;");
    titleLabel->setAlignment(Qt::AlignCenter);
    tabLayout->addWidget(titleLabel);

    auto* verLabel = new QLabel("Phiên bản 1.0.0 (x86_64 Linux)", tab);
    verLabel->setStyleSheet("color: #aaa; margin-bottom: 12px;");
    verLabel->setAlignment(Qt::AlignCenter);
    tabLayout->addWidget(verLabel);

    auto* descBox = new QGroupBox(LTR("settings.about.descriptionTitle"), tab);
    auto* descLayout = new QVBoxLayout(descBox);
    auto* descLabel = new QLabel(
        LTR("settings.about.descriptionText"),
        descBox);
    descLabel->setWordWrap(true);
    descLayout->addWidget(descLabel);
    tabLayout->addWidget(descBox);

    auto* stackBox = new QGroupBox(LTR("settings.about.stackTitle"), tab);
    auto* stackLayout = new QFormLayout(stackBox);
    stackLayout->addRow("Core C++:", new QLabel("C++20 Standard", stackBox));
    stackLayout->addRow("GUI Framework:", new QLabel("Qt 6.4+ (Widgets, OpenGLWidgets)", stackBox));
    stackLayout->addRow("Video Codec Engine:", new QLabel("FFmpeg libavcodec / libavformat", stackBox));
    stackLayout->addRow("Compositor:", new QLabel("OpenGL 3.3 Core Profile Shaders", stackBox));
    stackLayout->addRow("Audio Subsystem:", new QLabel("ALSA / PipeWire / PulseAudio Low-latency PCM", stackBox));
    stackLayout->addRow("Preview Backend:", new QLabel("libmpv 2.0 Engine", stackBox));
    tabLayout->addWidget(stackBox);

    // The project ships exactly two licences: MIT (LICENSE) and HOSL-1.3
    // (LICENSE-HOSL-1.3.md). This label previously claimed "GPL v3 / MIT",
    // which named a licence the repository does not contain.
    auto* copyLabel = new QLabel(LTR("settings.about.copyright"), tab);
    copyLabel->setStyleSheet("color: #777; font-size: 11px; margin-top: 10px;");
    copyLabel->setAlignment(Qt::AlignCenter);
    tabLayout->addWidget(copyLabel);

    tabLayout->addStretch();
    return tab;
}

void WindowSettingsDialog::loadValues() {
    // 1. Window settings
    const WindowSettings ws = WindowSettings::loadFromPreferences();
    int idx = m_startupModeCombo->findData(ws.startupMode);
    if (idx >= 0) m_startupModeCombo->setCurrentIndex(idx);
    m_opacitySlider->setValue(ws.opacityPercent);
    m_opacityValLabel->setText(QString::number(ws.opacityPercent) + "%");
    m_alwaysOnTopCheck->setChecked(ws.alwaysOnTop);
    m_lockDocksCheck->setChecked(ws.lockDocks);
    m_confirmExitCheck->setChecked(ws.confirmExit);

    const bool autosaveOn = AutosaveManager::isEnabled();
    m_autosaveEnableCheck->setChecked(autosaveOn);
    m_autosaveIntervalSpin->setValue(AutosaveManager::intervalSeconds());
    m_autosaveIntervalSpin->setEnabled(autosaveOn);
    m_showToolbarCheck->setChecked(ws.showToolbar);
    m_showStatusBarCheck->setChecked(ws.showStatusBar);

    // 2. Appearance
    const QString theme = ThemeManager::currentTheme();
    m_darkThemeRadio->blockSignals(true);
    m_lightThemeRadio->blockSignals(true);
    m_systemThemeRadio->blockSignals(true);

    if (theme == "light") m_lightThemeRadio->setChecked(true);
    else if (theme == "system") m_systemThemeRadio->setChecked(true);
    else m_darkThemeRadio->setChecked(true);

    m_darkThemeRadio->blockSignals(false);
    m_lightThemeRadio->blockSignals(false);
    m_systemThemeRadio->blockSignals(false);

    // 3. Language info
    const auto langs = LanguageManager::instance().availableLanguages();
    const QString cur = LanguageManager::instance().currentLanguage();
    for (const auto& l : langs) {
        if (l.languageCode == cur) {
            m_languageInfoLabel->setText(QString("%1: %2 | %3: %4 | %5: %6")
                .arg(LTR("settings.language.author"), l.author.isEmpty() ? "Hyggshi Dev" : l.author)
                .arg(LTR("settings.language.version"), QString::number(l.version))
                .arg(LTR("settings.language.terms"), QString::number(l.translations.size())));
            break;
        }
    }

    // 4. Graphics Backend
    QSettings pref("HyggshiCut", "Preferences");
    const QString backend = pref.value("graphicsBackend", "opengl33").toString();
    if (backend == "opengl_latest") m_glLatestRadio->setChecked(true);
    else if (backend == "vulkan") m_vulkanRadio->setChecked(true);
    else m_gl33Radio->setChecked(true);

    // 5. Proxy
    m_useProxyCheck->setChecked(pref.value("proxy/useProxy", true).toBool());
    const int width = pref.value("proxy/maxProxyWidth", 0).toInt();
    int wIdx = m_proxyResolutionCombo->findData(width);
    if (wIdx >= 0) m_proxyResolutionCombo->setCurrentIndex(wIdx);
    m_autoGenProxyCheck->setChecked(pref.value("proxy/autoGenerate", false).toBool());

    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString defaultCache = (base.isEmpty() ? QDir::tempPath() : base) + "/HyggshiCut/proxies";
    m_proxyDirEdit->setText(pref.value("proxy/cacheDir", defaultCache).toString());
    updateProxyCacheSizeLabel();
}

void WindowSettingsDialog::updateProxyCacheSizeLabel() {
    const QString path = m_proxyDirEdit->text();
    QDir dir(path);
    qint64 total = 0;
    int count = 0;
    if (dir.exists()) {
        const auto list = dir.entryInfoList(QStringList() << "*.mp4", QDir::Files);
        count = list.size();
        for (const auto& fi : list) total += fi.size();
    }
    double mb = total / (1024.0 * 1024.0);
    m_proxyCacheSizeLabel->setText(QString("%1 MB (%2 files)").arg(QString::number(mb, 'f', 1)).arg(count));
}

void WindowSettingsDialog::onThemeChanged() {
    QString theme = "dark";
    if (m_lightThemeRadio->isChecked()) theme = "light";
    else if (m_systemThemeRadio->isChecked()) theme = "system";

    ThemeManager::setTheme(theme);
    emit themeChanged(theme);
}

void WindowSettingsDialog::onLanguageComboChanged(int index) {
    if (index < 0) return;
    const QString langCode = m_languageCombo->itemData(index).toString();
    if (langCode != LanguageManager::instance().currentLanguage()) {
        LanguageManager::instance().setLanguage(langCode);
        emit languageChanged(langCode);
    }
}

void WindowSettingsDialog::onLoadLanguageClicked() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        LTR("lang.loadFile"),
        QString(),
        "HyggshiCut Language Pack (*.langhc);;All Files (*)");
    if (path.isEmpty()) return;

    QString err;
    if (LanguageManager::instance().loadFromFile(path, &err)) {
        QFileInfo fi(path);
        QMessageBox::information(this, LTR("lang.loadSuccess"), fi.fileName());
        // Refresh combo
        m_languageCombo->clear();
        const auto langs = LanguageManager::instance().availableLanguages();
        const QString cur = LanguageManager::instance().currentLanguage();
        int sel = 0;
        for (int i = 0; i < langs.size(); ++i) {
            m_languageCombo->addItem(langs[i].nativeName, langs[i].languageCode);
            if (langs[i].languageCode == cur) sel = i;
        }
        m_languageCombo->setCurrentIndex(sel);
    } else {
        QMessageBox::warning(this, LTR("lang.loadFail"), err);
    }
}

void WindowSettingsDialog::onGraphicsBackendChanged() {
    QSettings pref("HyggshiCut", "Preferences");
    QString backend = "opengl33";
    if (m_glLatestRadio->isChecked()) backend = "opengl_latest";
    else if (m_vulkanRadio->isChecked()) backend = "vulkan";
    pref.setValue("graphicsBackend", backend);
}

void WindowSettingsDialog::onBrowseProxyDirClicked() {
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        LTR("settings.proxy.selectCacheDir"),
        m_proxyDirEdit->text());
    if (!dir.isEmpty()) {
        m_proxyDirEdit->setText(dir);
        QSettings pref("HyggshiCut", "Preferences");
        pref.setValue("proxy/cacheDir", dir);
        updateProxyCacheSizeLabel();
    }
}

void WindowSettingsDialog::onClearProxyCacheClicked() {
    if (QMessageBox::question(this, LTR("settings.proxy.clearCache"),
                              LTR("settings.proxy.clearCacheConfirm")) == QMessageBox::Yes) {
        QDir dir(m_proxyDirEdit->text());
        if (dir.exists()) {
            const auto list = dir.entryInfoList(QStringList() << "*.mp4" << "*.json", QDir::Files);
            for (const auto& fi : list) QFile::remove(fi.absoluteFilePath());
        }
        updateProxyCacheSizeLabel();
        QMessageBox::information(this, LTR("settings.proxy.clearCache"), LTR("settings.proxy.cacheCleared"));
    }
}

void WindowSettingsDialog::onGenerateProxiesClicked() {
    emit generateProxiesRequested();
}

void WindowSettingsDialog::onOpacitySliderChanged(int val) {
    m_opacityValLabel->setText(QString::number(val) + "%");
}

void WindowSettingsDialog::onResetLayoutClicked() {
    emit resetLayoutRequested();
}

WindowSettings WindowSettingsDialog::currentSettings() const {
    WindowSettings ws;
    ws.startupMode = m_startupModeCombo->currentData().toString();
    ws.opacityPercent = m_opacitySlider->value();
    ws.alwaysOnTop = m_alwaysOnTopCheck->isChecked();
    ws.lockDocks = m_lockDocksCheck->isChecked();
    ws.confirmExit = m_confirmExitCheck->isChecked();

    // Persisted separately from WindowSettings because AutosaveManager reads
    // these directly (it has no dependency on the UI layer).
    AutosaveManager::setEnabled(m_autosaveEnableCheck->isChecked());
    AutosaveManager::setIntervalSeconds(m_autosaveIntervalSpin->value());
    ws.showToolbar = m_showToolbarCheck->isChecked();
    ws.showStatusBar = m_showStatusBarCheck->isChecked();
    return ws;
}

void WindowSettingsDialog::onApplyClicked() {
    const WindowSettings ws = currentSettings();
    ws.saveToPreferences();

    // Save Proxy Preferences
    QSettings pref("HyggshiCut", "Preferences");
    pref.setValue("proxy/useProxy", m_useProxyCheck->isChecked());
    pref.setValue("proxy/maxProxyWidth", m_proxyResolutionCombo->currentData().toInt());
    pref.setValue("proxy/autoGenerate", m_autoGenProxyCheck->isChecked());
    pref.setValue("proxy/cacheDir", m_proxyDirEdit->text());

    emit proxyUsageToggled(m_useProxyCheck->isChecked());
    emit applySettings(ws);
}

void WindowSettingsDialog::onOkClicked() {
    onApplyClicked();
    accept();
}

} // namespace hc
