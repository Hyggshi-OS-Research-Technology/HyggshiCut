#include "ThemeManager.h"
#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QPalette>
#include <QSettings>
#include <QColor>

namespace hc {

namespace {

const char* flatSystemQss = R"(
    QWidget { border-radius: 0; }
    QToolButton, QPushButton, QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox,
    QDateEdit, QTimeEdit, QDateTimeEdit, QSlider::groove:horizontal,
    QSlider::groove:vertical, QProgressBar, QGroupBox, QTabWidget::pane {
        border-radius: 0;
    }
    QScrollBar::handle:vertical, QScrollBar::handle:horizontal { border-radius: 0; }
    QDockWidget::title { padding: 7px 10px; border-radius: 0; }
    QTabBar::tab { border-radius: 0; }
)";

} // namespace

ThemeManager& ThemeManager::instance() {
    static ThemeManager s_instance;
    return s_instance;
}

QString ThemeManager::currentTheme() {
    QSettings s("HyggshiCut", "Preferences");
    return s.value("theme/mode", "dark").toString();
}

void ThemeManager::setTheme(const QString& theme) {
    static QString s_activeAppliedTheme;
    QSettings s("HyggshiCut", "Preferences");
    s.setValue("theme/mode", theme);

    if (s_activeAppliedTheme == theme && !s_activeAppliedTheme.isEmpty()) {
        return;
    }
    s_activeAppliedTheme = theme;

    if (theme == "light") {
        applyLightTheme();
    } else if (theme == "system") {
        applySystemTheme();
    } else {
        applyDarkTheme();
    }

    emit instance().themeChanged(theme);
}

void ThemeManager::loadPreference() {
    setTheme(currentTheme());
}

void ThemeManager::applyDarkTheme() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    const QColor canvas("#14171b");
    const QColor panel("#1b1f24");
    const QColor raised("#22272e");
    const QColor outline("#303740");
    const QColor text("#dce1e8");
    const QColor muted("#929ba7");
    const QColor accent("#4a9eff");

    QPalette dark;
    dark.setColor(QPalette::Window, panel);
    dark.setColor(QPalette::WindowText, text);
    dark.setColor(QPalette::Base, canvas);
    dark.setColor(QPalette::AlternateBase, raised);
    dark.setColor(QPalette::ToolTipBase, raised);
    dark.setColor(QPalette::ToolTipText, text);
    dark.setColor(QPalette::Text, text);
    dark.setColor(QPalette::Button, raised);
    dark.setColor(QPalette::ButtonText, text);
    dark.setColor(QPalette::BrightText, QColor("#ff6262"));
    dark.setColor(QPalette::Link, accent);
    dark.setColor(QPalette::Highlight, accent);
    dark.setColor(QPalette::HighlightedText, QColor("#07111c"));
    dark.setColor(QPalette::Mid, outline);
    dark.setColor(QPalette::Dark, canvas);
    dark.setColor(QPalette::Disabled, QPalette::Text, muted);
    dark.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
    dark.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    QApplication::setPalette(dark);

    const QString darkQss = R"(
        QWidget {
            color: #dce1e8;
            selection-background-color: #2866a2;
            selection-color: #ffffff;
            outline: none;
        }
        QMainWindow, QDialog { background: #1b1f24; }
        QMenuBar {
            background: #171a1e;
            color: #cbd2dc;
            border: 0;
            border-bottom: 1px solid #303740;
            padding: 0 4px;
        }
        QMenuBar::item { padding: 7px 10px; margin: 0; background: transparent; }
        QMenuBar::item:selected { background: #252b33; color: #70b5ff; }
        QMenu {
            background: #20252b;
            color: #dce1e8;
            border: 1px solid #343b44;
            padding: 4px 0;
        }
        QMenu::item { padding: 6px 28px 6px 12px; margin: 0; background: transparent; }
        QMenu::item:selected { background: #2a3644; color: #ffffff; }
        QMenu::separator { height: 1px; background: #343b44; margin: 4px 0; }
        QToolBar {
            background: #1b1f24;
            border: 0;
            border-bottom: 1px solid #303740;
            spacing: 3px;
            padding: 4px 8px;
        }
        QToolBar::separator { width: 1px; background: #343b44; margin: 5px 6px; }
        QToolButton {
            color: #cbd2dc;
            background: transparent;
            border: 1px solid transparent;
            border-radius: 0;
            padding: 6px 9px;
        }
        QToolButton:hover { background: #282e36; border-color: #363e48; }
        QToolButton:pressed, QToolButton:checked {
            background: #1d3a57;
            color: #8bc5ff;
            border-color: #315c82;
        }
        QPushButton {
            color: #dce1e8;
            background: #252a31;
            border: 1px solid #39414b;
            border-radius: 0;
            padding: 5px 10px;
        }
        QPushButton:hover { background: #2d343d; border-color: #4b5663; }
        QPushButton:pressed { background: #1b354d; border-color: #3d78ad; }
        QPushButton:disabled { color: #727b86; background: #20242a; border-color: #2b3037; }
        QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox,
        QDateEdit, QTimeEdit, QDateTimeEdit {
            color: #dce1e8;
            background: #14171b;
            border: 1px solid #343b44;
            border-radius: 0;
            padding: 5px 7px;
            selection-background-color: #2866a2;
        }
        QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus,
        QSpinBox:focus, QDoubleSpinBox:focus { border-color: #4a9eff; }
        QComboBox::drop-down { border: 0; width: 22px; }
        QComboBox QAbstractItemView {
            color: #dce1e8; background: #20252b; border: 1px solid #343b44;
            selection-background-color: #2866a2;
        }
        QGroupBox {
            border: 1px solid #303740;
            border-radius: 0;
            margin-top: 12px;
            padding: 8px 8px 6px 8px;
            font-weight: 600;
        }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #aeb8c5; }
        QDockWidget { color: #dce1e8; border: 0; }
        QDockWidget::title {
            background: #1b1f24;
            color: #aeb8c5;
            padding: 7px 10px;
            border: 0;
            border-bottom: 1px solid #303740;
            text-align: left;
            font-weight: 600;
        }
        QMainWindow::separator { background: #303740; width: 1px; height: 1px; }
        QTabWidget::pane { background: #1b1f24; border: 0; border-top: 1px solid #303740; }
        QTabBar::tab {
            color: #9ca6b3;
            background: #1b1f24;
            border: 0;
            border-bottom: 2px solid transparent;
            padding: 8px 12px;
            margin: 0;
        }
        QTabBar::tab:hover { color: #dce1e8; background: #22272e; }
        QTabBar::tab:selected { color: #70b5ff; border-bottom-color: #4a9eff; background: #1b1f24; }
        QTabBar::tab:disabled { color: #69727d; }
        QAbstractItemView {
            color: #dce1e8;
            background: #171a1e;
            alternate-background-color: #1d2228;
            border: 0;
            selection-background-color: #263d54;
            selection-color: #ffffff;
            outline: 0;
        }
        QHeaderView::section {
            color: #aeb8c5; background: #22272e;
            border: 0; border-right: 1px solid #303740;
            border-bottom: 1px solid #303740; padding: 6px 8px;
        }
        QScrollBar:vertical { background: #171a1e; width: 9px; margin: 0; }
        QScrollBar:horizontal { background: #171a1e; height: 9px; margin: 0; }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
            background: #3a424d; min-width: 22px; min-height: 22px; border: 0; border-radius: 0;
        }
        QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: #56616e; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QSlider::groove:horizontal { height: 3px; background: #3a424d; border: 0; border-radius: 0; }
        QSlider::sub-page:horizontal { background: #4a9eff; }
        QSlider::handle:horizontal {
            background: #dce1e8; border: 1px solid #dce1e8;
            width: 10px; height: 10px; margin: -4px 0; border-radius: 0;
        }
        QSlider::groove:vertical { width: 3px; background: #3a424d; border: 0; border-radius: 0; }
        QSlider::handle:vertical {
            background: #dce1e8; border: 1px solid #dce1e8;
            width: 10px; height: 10px; margin: 0 -4px; border-radius: 0;
        }
        QProgressBar { background: #14171b; border: 1px solid #343b44; border-radius: 0; text-align: center; }
        QProgressBar::chunk { background: #3c8bd4; border-radius: 0; }
        QStatusBar { color: #929ba7; background: #171a1e; border-top: 1px solid #303740; }
        QStatusBar::item { border: 0; }
        QToolTip { color: #f0f3f7; background: #242a31; border: 1px solid #46505c; padding: 4px 7px; }
        QSplitter::handle { background: #303740; }
    )";
    qApp->setStyleSheet(darkQss);
}

void ThemeManager::applyLightTheme() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    const QColor canvas("#f4f6f8");
    const QColor panel("#ffffff");
    const QColor raised("#edf0f3");
    const QColor outline("#d4dbe2");
    const QColor text("#242a31");
    const QColor muted("#68727e");
    const QColor accent("#1677c8");

    QPalette light;
    light.setColor(QPalette::Window, canvas);
    light.setColor(QPalette::WindowText, text);
    light.setColor(QPalette::Base, panel);
    light.setColor(QPalette::AlternateBase, raised);
    light.setColor(QPalette::ToolTipBase, panel);
    light.setColor(QPalette::ToolTipText, text);
    light.setColor(QPalette::Text, text);
    light.setColor(QPalette::Button, raised);
    light.setColor(QPalette::ButtonText, text);
    light.setColor(QPalette::BrightText, QColor("#c62828"));
    light.setColor(QPalette::Link, accent);
    light.setColor(QPalette::Highlight, accent);
    light.setColor(QPalette::HighlightedText, Qt::white);
    light.setColor(QPalette::Mid, outline);
    light.setColor(QPalette::Dark, outline);
    light.setColor(QPalette::Disabled, QPalette::Text, muted);
    light.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
    light.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    QApplication::setPalette(light);

    const QString lightQss = R"(
        QWidget { color: #242a31; selection-background-color: #1677c8; selection-color: #ffffff; outline: none; }
        QMainWindow, QDialog { background: #f4f6f8; }
        QMenuBar { background: #f4f6f8; color: #3a424b; border: 0; border-bottom: 1px solid #d4dbe2; padding: 0 4px; }
        QMenuBar::item { padding: 7px 10px; margin: 0; background: transparent; }
        QMenuBar::item:selected { background: #e8edf2; color: #1268aa; }
        QMenu { background: #ffffff; color: #242a31; border: 1px solid #d4dbe2; padding: 4px 0; }
        QMenu::item { padding: 6px 28px 6px 12px; margin: 0; }
        QMenu::item:selected { background: #e7f1fa; color: #125f9f; }
        QMenu::separator { height: 1px; background: #e1e5e9; margin: 4px 0; }
        QToolBar { background: #ffffff; border: 0; border-bottom: 1px solid #d4dbe2; spacing: 3px; padding: 4px 8px; }
        QToolBar::separator { width: 1px; background: #d4dbe2; margin: 5px 6px; }
        QToolButton { color: #3a424b; background: transparent; border: 1px solid transparent; border-radius: 0; padding: 6px 9px; }
        QToolButton:hover { background: #edf1f5; border-color: #d4dbe2; }
        QToolButton:pressed, QToolButton:checked { background: #e0effb; color: #1268aa; border-color: #9bc7e8; }
        QPushButton { color: #242a31; background: #f3f5f7; border: 1px solid #cbd3dc; border-radius: 0; padding: 5px 10px; }
        QPushButton:hover { background: #e9eef3; border-color: #aab6c2; }
        QPushButton:pressed { background: #dcebf7; border-color: #83b4dc; }
        QPushButton:disabled { color: #929ba5; background: #f1f2f4; }
        QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox, QDoubleSpinBox, QDateEdit, QTimeEdit, QDateTimeEdit {
            color: #242a31; background: #ffffff; border: 1px solid #cbd3dc; border-radius: 0; padding: 5px 7px;
        }
        QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #1677c8; }
        QComboBox::drop-down { border: 0; width: 22px; }
        QComboBox QAbstractItemView { color: #242a31; background: #ffffff; border: 1px solid #cbd3dc; selection-background-color: #1677c8; }
        QGroupBox { border: 1px solid #d4dbe2; border-radius: 0; margin-top: 12px; padding: 8px 8px 6px 8px; font-weight: 600; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #4c5865; }
        QDockWidget { color: #3a424b; border: 0; }
        QDockWidget::title { background: #f4f6f8; color: #4c5865; padding: 7px 10px; border: 0; border-bottom: 1px solid #d4dbe2; text-align: left; font-weight: 600; }
        QMainWindow::separator { background: #d4dbe2; width: 1px; height: 1px; }
        QTabWidget::pane { background: #ffffff; border: 0; border-top: 1px solid #d4dbe2; }
        QTabBar::tab { color: #68727e; background: #f4f6f8; border: 0; border-bottom: 2px solid transparent; padding: 8px 12px; margin: 0; }
        QTabBar::tab:hover { color: #242a31; background: #edf0f3; }
        QTabBar::tab:selected { color: #1268aa; border-bottom-color: #1677c8; background: #ffffff; }
        QAbstractItemView { color: #242a31; background: #ffffff; alternate-background-color: #f5f7f9; border: 0; selection-background-color: #e0effb; selection-color: #173d5c; outline: 0; }
        QHeaderView::section { color: #4c5865; background: #edf0f3; border: 0; border-right: 1px solid #d4dbe2; border-bottom: 1px solid #d4dbe2; padding: 6px 8px; }
        QScrollBar:vertical { background: #f4f6f8; width: 9px; margin: 0; }
        QScrollBar:horizontal { background: #f4f6f8; height: 9px; margin: 0; }
        QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: #c2cbd4; min-width: 22px; min-height: 22px; border: 0; border-radius: 0; }
        QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: #aab6c2; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        QSlider::groove:horizontal { height: 3px; background: #d0d7df; border: 0; border-radius: 0; }
        QSlider::sub-page:horizontal { background: #1677c8; }
        QSlider::handle:horizontal { background: #ffffff; border: 1px solid #1677c8; width: 10px; height: 10px; margin: -4px 0; border-radius: 0; }
        QSlider::groove:vertical { width: 3px; background: #d0d7df; border: 0; border-radius: 0; }
        QSlider::handle:vertical { background: #ffffff; border: 1px solid #1677c8; width: 10px; height: 10px; margin: 0 -4px; border-radius: 0; }
        QProgressBar { background: #ffffff; border: 1px solid #cbd3dc; border-radius: 0; text-align: center; }
        QProgressBar::chunk { background: #1677c8; border-radius: 0; }
        QStatusBar { color: #68727e; background: #f4f6f8; border-top: 1px solid #d4dbe2; }
        QStatusBar::item { border: 0; }
        QToolTip { color: #242a31; background: #ffffff; border: 1px solid #cbd3dc; padding: 4px 7px; }
        QSplitter::handle { background: #d4dbe2; }
    )";
    qApp->setStyleSheet(lightQss);
}

void ThemeManager::applySystemTheme() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setPalette(qApp->style()->standardPalette());
    qApp->setStyleSheet(QLatin1String(flatSystemQss));
}

} // namespace hc
