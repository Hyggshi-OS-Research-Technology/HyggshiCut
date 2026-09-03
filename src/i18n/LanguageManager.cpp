#include "LanguageManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

namespace hc {

LanguageManager& LanguageManager::instance() {
    static LanguageManager s;
    return s;
}

bool LanguageManager::loadFromFile(const QString& path, QString* errorOut) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = "Cannot open file: " + path;
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        if (errorOut) *errorOut = "JSON parse error: " + err.errorString();
        return false;
    }
    if (!doc.isObject()) {
        if (errorOut) *errorOut = "Invalid .langhc format: root must be an object";
        return false;
    }
    LanguagePack pack;
    if (!parseJsonPack(doc.object(), pack, errorOut)) return false;
    m_packs[pack.languageCode] = pack;
    return true;
}

bool LanguageManager::parseJsonPack(const QJsonObject& root, LanguagePack& out, QString* errorOut) {
    if (!root.contains("language")) {
        if (errorOut) *errorOut = "Missing required field: 'language'";
        return false;
    }
    out.version = root["version"].toInt(1);
    out.languageCode = root["language"].toString();
    out.nativeName = root["nativeName"].toString(out.languageCode);
    out.author = root["author"].toString();

    const QJsonObject t = root["translations"].toObject();
    for (auto it = t.begin(); it != t.end(); ++it) {
        out.translations[it.key()] = it.value().toString();
    }
    return true;
}

bool LanguageManager::setLanguage(const QString& languageCode) {
    if (!m_packs.contains(languageCode)) return false;
    if (m_currentLang == languageCode) return true;
    m_currentLang = languageCode;
    savePreference();
    emit languageChanged(languageCode);
    return true;
}

QString LanguageManager::translate(const QString& key) const {
    // Try current language first
    if (m_packs.contains(m_currentLang)) {
        const auto& pack = m_packs[m_currentLang];
        if (pack.translations.contains(key)) return pack.translations[key];
    }
    // Fallback to English
    if (m_currentLang != "en" && m_packs.contains("en")) {
        const auto& enPack = m_packs["en"];
        if (enPack.translations.contains(key)) return enPack.translations[key];
    }
    // Return the key itself if not found
    return key;
}

QList<LanguagePack> LanguageManager::availableLanguages() const {
    auto values = m_packs.values();
    std::sort(values.begin(), values.end(), [](const LanguagePack& a, const LanguagePack& b) {
        return a.nativeName < b.nativeName;
    });
    return values;
}

void LanguageManager::savePreference() const {
    QSettings settings("HyggshiCut", "Language");
    settings.setValue("language", m_currentLang);
}

void LanguageManager::loadPreference() {
    QSettings settings("HyggshiCut", "Language");
    const QString saved = settings.value("language", "vi").toString();
    if (m_packs.contains(saved)) {
        m_currentLang = saved;
    }
}

} // namespace hc
