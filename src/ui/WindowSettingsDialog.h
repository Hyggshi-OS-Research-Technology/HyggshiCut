#pragma once
#include <QDialog>
#include <QTabWidget>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QButtonGroup>
#include <QSettings>

namespace hc {

enum class SettingsTab {
    Window = 0,
    Appearance = 1,
    Language = 2,
    Graphics = 3,
    Proxy = 4,
    About = 5
};

struct WindowSettings {
    QString startupMode = "remember"; // "remember", "maximized", "fullscreen", "default"
    int opacityPercent = 100;         // 70 .. 100
    bool alwaysOnTop = false;
    bool lockDocks = false;
    bool confirmExit = true;
    bool showToolbar = true;
    bool showStatusBar = true;

    static WindowSettings loadFromPreferences();
    void saveToPreferences() const;
};

class WindowSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit WindowSettingsDialog(QWidget* parent = nullptr, SettingsTab initialTab = SettingsTab::Window);
    ~WindowSettingsDialog() override;

    WindowSettings currentSettings() const;

signals:
    void applySettings(const hc::WindowSettings& settings);
    void resetLayoutRequested();
    void generateProxiesRequested();
    void proxyUsageToggled(bool enabled);
    void languageChanged(const QString& langCode);
    void themeChanged(const QString& theme);

public slots:
    void setCurrentTab(SettingsTab tab);

private slots:
    void onApplyClicked();
    void onOkClicked();
    void onResetLayoutClicked();
    void onOpacitySliderChanged(int val);

    // Appearance / Dark mode
    void onThemeChanged();

    // Language
    void onLanguageComboChanged(int index);
    void onLoadLanguageClicked();

    // Graphics
    void onGraphicsBackendChanged();

    // Proxy
    void onBrowseProxyDirClicked();
    void onClearProxyCacheClicked();
    void onGenerateProxiesClicked();
    void updateProxyCacheSizeLabel();

private:
    void setupUi();
    QWidget* createWindowTab();
    QWidget* createAppearanceTab();
    QWidget* createLanguageTab();
    QWidget* createGraphicsTab();
    QWidget* createProxyTab();
    QWidget* createAboutTab();

    void loadValues();

    QTabWidget* m_tabWidget = nullptr;

    // Window tab widgets
    QComboBox* m_startupModeCombo = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityValLabel = nullptr;
    QCheckBox* m_alwaysOnTopCheck = nullptr;
    QCheckBox* m_lockDocksCheck = nullptr;
    QCheckBox* m_confirmExitCheck = nullptr;
    QCheckBox* m_showToolbarCheck = nullptr;
    QCheckBox* m_showStatusBarCheck = nullptr;
    QPushButton* m_resetLayoutBtn = nullptr;

    // Appearance tab widgets
    QRadioButton* m_darkThemeRadio = nullptr;
    QRadioButton* m_lightThemeRadio = nullptr;
    QRadioButton* m_systemThemeRadio = nullptr;
    QButtonGroup* m_themeGroup = nullptr;

    // Language tab widgets
    QComboBox* m_languageCombo = nullptr;
    QLabel* m_languageInfoLabel = nullptr;
    QPushButton* m_loadLanguageBtn = nullptr;

    // Graphics tab widgets
    QRadioButton* m_gl33Radio = nullptr;
    QRadioButton* m_glLatestRadio = nullptr;
    QRadioButton* m_vulkanRadio = nullptr;
    QButtonGroup* m_backendGroup = nullptr;
    QLabel* m_gpuRendererLabel = nullptr;
    QLabel* m_gpuVersionLabel = nullptr;
    QLabel* m_gpuGlslLabel = nullptr;

    // Proxy tab widgets
    QCheckBox* m_useProxyCheck = nullptr;
    QComboBox* m_proxyResolutionCombo = nullptr;
    QLineEdit* m_proxyDirEdit = nullptr;
    QPushButton* m_browseProxyDirBtn = nullptr;
    QLabel* m_proxyCacheSizeLabel = nullptr;
    QCheckBox* m_autoGenProxyCheck = nullptr;
    QPushButton* m_genProxiesBtn = nullptr;
    QPushButton* m_clearProxyCacheBtn = nullptr;

    // Dialog buttons
    QPushButton* m_applyBtn = nullptr;
    QPushButton* m_okBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
};

// Friendly alias
using SettingsDialog = WindowSettingsDialog;

} // namespace hc
