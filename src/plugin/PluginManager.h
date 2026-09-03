#pragma once
#include <QObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include "../core/Clip.h"

namespace hc {

struct PluginEffectDef {
    QString id;          // unique id within the plugin
    QString name;        // display name (translated)
    QString effectType;  // maps to Clip::Effect::type (e.g. "color_grade")
    QMap<QString, double> defaultParams;
};

struct PluginInfo {
    QString pluginId;
    QString name;
    QString author;
    QString description;
    QString versionStr;
    QString type;        // "effects", "presets", etc.
    bool enabled = true;
    QString filePath;    // where the .plhc was loaded from
    QList<PluginEffectDef> effects;
};

// Singleton managing loaded .plhc plugin packs.
class PluginManager : public QObject {
    Q_OBJECT
public:
    static PluginManager& instance();

    // Load a .plhc file. Returns true on success.
    bool loadFromFile(const QString& path, QString* errorOut = nullptr);

    // Remove a plugin by pluginId. Returns true if found and removed.
    bool removePlugin(const QString& pluginId);

    // Enable/disable a plugin
    void setPluginEnabled(const QString& pluginId, bool enabled);

    // All loaded plugins (in load order)
    QList<PluginInfo> plugins() const { return m_plugins.values(); }

    // All effect definitions from all enabled plugins
    QList<PluginEffectDef> allEffects() const;

    // Find a single effect def by pluginId + effectId
    bool findEffect(const QString& pluginId, const QString& effectId, PluginEffectDef* out) const;

    // Persist load list to QSettings (paths + enabled state)
    void saveState() const;
    void loadState();    // Re-loads all saved plugins from disk

    // Build an Effect object from a plugin effect def
    static Effect buildEffect(const PluginEffectDef& def);

signals:
    void pluginsChanged();

private:
    PluginManager() = default;
    QMap<QString, PluginInfo> m_plugins; // pluginId → info
    QStringList m_loadOrder;
};

} // namespace hc
