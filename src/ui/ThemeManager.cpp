#include "ThemeManager.h"
#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QPalette>
#include <QSettings>
#include <QColor>

namespace hc {

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

    QPalette dark;
    dark.setColor(QPalette::Window, QColor(30, 30, 35));
    dark.setColor(QPalette::WindowText, QColor(230, 230, 235));
    dark.setColor(QPalette::Base, QColor(22, 22, 26));
    dark.setColor(QPalette::AlternateBase, QColor(36, 36, 42));
    dark.setColor(QPalette::ToolTipBase, QColor(40, 40, 48));
    dark.setColor(QPalette::ToolTipText, Qt::white);
    dark.setColor(QPalette::Text, QColor(235, 235, 240));
    dark.setColor(QPalette::Button, QColor(44, 44, 52));
    dark.setColor(QPalette::ButtonText, Qt::white);
    dark.setColor(QPalette::BrightText, Qt::red);
    dark.setColor(QPalette::Link, QColor(255, 153, 68));
    dark.setColor(QPalette::Highlight, QColor(255, 153, 68));
    dark.setColor(QPalette::HighlightedText, Qt::black);
    dark.setColor(QPalette::Disabled, QPalette::Text, QColor(125, 125, 130));
    dark.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(125, 125, 130));
    dark.setColor(QPalette::Disabled, QPalette::WindowText, QColor(125, 125, 130));

    QApplication::setPalette(dark);

    const QString darkQss = R"(
        QMainWindow, QDialog {
            background-color: #1e1e23;
            color: #eee;
        }
        QMenuBar {
            background-color: #18181c;
            color: #ddd;
            border-bottom: 1px solid #33333d;
        }
        QMenuBar::item:selected {
            background-color: #2b2b35;
            color: #ff9944;
        }
        QMenu {
            background-color: #222228;
            color: #eee;
            border: 1px solid #3d3d48;
            padding: 4px;
        }
        QMenu::item:selected {
            background-color: #ff9944;
            color: #111;
            border-radius: 3px;
        }
        QMenu::separator {
            height: 1px;
            background: #383844;
            margin: 4px 6px;
        }
        QToolBar {
            background-color: #1d1d22;
            border-bottom: 1px solid #33333d;
            spacing: 6px;
            padding: 4px;
        }
        QToolTip {
            background-color: #2a2a32;
            color: #fff;
            border: 1px solid #484856;
            padding: 4px 8px;
            border-radius: 4px;
        }
        QDockWidget {
            titlebar-close-icon: url();
            titlebar-normal-icon: url();
            color: #ddd;
            font-weight: bold;
        }
        QDockWidget::title {
            background: #23232a;
            padding: 6px;
            border-bottom: 1px solid #33333d;
        }
        QTabBar::tab {
            background: #1f1f25;
            color: #bbb;
            padding: 6px 14px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background: #2c2c36;
            color: #ff9944;
            font-weight: bold;
        }
        QScrollBar:vertical {
            background: #1a1a1e;
            width: 10px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background: #3d3d4a;
            min-height: 20px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical:hover {
            background: #555566;
        }
        QScrollBar:horizontal {
            background: #1a1a1e;
            height: 10px;
            margin: 0px;
        }
        QScrollBar::handle:horizontal {
            background: #3d3d4a;
            min-width: 20px;
            border-radius: 5px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #555566;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
            width: 0px;
            height: 0px;
        }
    )";

    qApp->setStyleSheet(darkQss);
}

void ThemeManager::applyLightTheme() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    QPalette light;
    light.setColor(QPalette::Window, QColor(245, 245, 248));
    light.setColor(QPalette::WindowText, QColor(25, 25, 30));
    light.setColor(QPalette::Base, Qt::white);
    light.setColor(QPalette::AlternateBase, QColor(238, 238, 242));
    light.setColor(QPalette::ToolTipBase, Qt::white);
    light.setColor(QPalette::ToolTipText, Qt::black);
    light.setColor(QPalette::Text, QColor(20, 20, 25));
    light.setColor(QPalette::Button, QColor(235, 235, 240));
    light.setColor(QPalette::ButtonText, Qt::black);
    light.setColor(QPalette::BrightText, Qt::red);
    light.setColor(QPalette::Link, QColor(220, 100, 20));
    light.setColor(QPalette::Highlight, QColor(220, 100, 20));
    light.setColor(QPalette::HighlightedText, Qt::white);
    light.setColor(QPalette::Disabled, QPalette::Text, QColor(140, 140, 145));
    light.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(140, 140, 145));
    light.setColor(QPalette::Disabled, QPalette::WindowText, QColor(140, 140, 145));

    QApplication::setPalette(light);

    const QString lightQss = R"(
        QMainWindow, QDialog {
            background-color: #f5f5f8;
            color: #222;
        }
        QMenuBar {
            background-color: #e8e8ed;
            color: #333;
            border-bottom: 1px solid #d0d0d8;
        }
        QMenuBar::item:selected {
            background-color: #dcdce2;
            color: #e06c1b;
        }
        QMenu {
            background-color: #ffffff;
            color: #222;
            border: 1px solid #c8c8d0;
            padding: 4px;
        }
        QMenu::item:selected {
            background-color: #e06c1b;
            color: #fff;
            border-radius: 3px;
        }
        QMenu::separator {
            height: 1px;
            background: #e0e0e8;
            margin: 4px 6px;
        }
        QToolBar {
            background-color: #ededf2;
            border-bottom: 1px solid #d5d5dc;
            spacing: 6px;
            padding: 4px;
        }
        QDockWidget::title {
            background: #e5e5ec;
            padding: 6px;
            border-bottom: 1px solid #d0d0d8;
        }
        QTabBar::tab {
            background: #e4e4eb;
            color: #555;
            padding: 6px 14px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
            margin-right: 2px;
        }
        QTabBar::tab:selected {
            background: #ffffff;
            color: #e06c1b;
            font-weight: bold;
        }
        QScrollBar:vertical {
            background: #f0f0f4;
            width: 10px;
        }
        QScrollBar::handle:vertical {
            background: #c5c5cf;
            border-radius: 5px;
        }
        QScrollBar:horizontal {
            background: #f0f0f4;
            height: 10px;
        }
        QScrollBar::handle:horizontal {
            background: #c5c5cf;
            border-radius: 5px;
        }
        QScrollBar::add-line, QScrollBar::sub-line {
            width: 0px;
            height: 0px;
        }
    )";

    qApp->setStyleSheet(lightQss);
}

void ThemeManager::applySystemTheme() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QApplication::setPalette(qApp->style()->standardPalette());
    qApp->setStyleSheet("");
}

} // namespace hc
