#pragma once
#include <QObject>
#include <QMap>
#include <QStringList>
#include <QJsonObject>

namespace hc {

struct LanguagePack {
    QString languageCode;    // "vi", "en", "ja", "ko", "zh", "th"
    QString nativeName;      // "Tiếng Việt", "English", etc.
    QString author;
    int version = 1;
    QMap<QString, QString> translations;
};

// Singleton managing the active UI language.
// Usage: LanguageManager::instance().tr("menu.file") → "Tệp" (in vi)
// Connect to languageChanged() to refresh UI strings on language switch.
class LanguageManager : public QObject {
    Q_OBJECT
public:
    static LanguageManager& instance();

    // Load a .langhc file and register it. Returns true on success.
    bool loadFromFile(const QString& path, QString* errorOut = nullptr);

    // Switch to a language by code ("vi", "en", "ja", …).
    // Returns false if the language is not loaded.
    bool setLanguage(const QString& languageCode);

    // Translate a key. Falls back to English, then the raw key if not found.
    QString translate(const QString& key) const;

    // Get all loaded language packs (ordered by native name)
    QList<LanguagePack> availableLanguages() const;

    // Current active language code
    QString currentLanguage() const { return m_currentLang; }

    // Save language preference to QSettings
    void savePreference() const;
    void loadPreference();
    void discoverBundledLanguages();

signals:
    void languageChanged(const QString& newLanguageCode);

private:
    LanguageManager() = default;

    QMap<QString, LanguagePack> m_packs; // code → pack
    QString m_currentLang = "vi";

    bool parseJsonPack(const QJsonObject& root, LanguagePack& out, QString* errorOut);
};

// Global shorthand macro
inline QString LTR(const QString& key) {
    return LanguageManager::instance().translate(key);
}

} // namespace hc
