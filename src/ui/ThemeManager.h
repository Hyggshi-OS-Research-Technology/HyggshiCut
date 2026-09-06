#pragma once
#include <QString>
#include <QObject>

namespace hc {

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance();

    static QString currentTheme(); // "dark", "light", "system"
    static void setTheme(const QString& theme);
    static void loadPreference();

signals:
    void themeChanged(const QString& newTheme);

private:
    ThemeManager() = default;
    static void applyDarkTheme();
    static void applyLightTheme();
    static void applySystemTheme();
};

} // namespace hc
