#include "PluginManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>

namespace hc {

PluginManager& PluginManager::instance() {
    static PluginManager s;
    return s;
}

bool PluginManager::loadFromFile(const QString& path, QString* errorOut) {
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
        if (errorOut) *errorOut = "Invalid .plhc format";
        return false;
    }

    const QJsonObject root = doc.object();
    PluginInfo info;
    info.pluginId = root["pluginId"].toString();
    if (info.pluginId.isEmpty()) {
        if (errorOut) *errorOut = "Missing 'pluginId' field";
        return false;
    }
    info.name = root["name"].toString(info.pluginId);
    info.author = root["author"].toString();
    info.description = root["description"].toString();
    info.versionStr = root["version_num"].toString("1.0.0");
    info.type = root["type"].toString("effects");
    info.enabled = root["enabled"].toBool(true);
    info.filePath = path;

    // Parse effects array
    const QJsonArray effects = root["effects"].toArray();
    for (const auto& v : effects) {
        const QJsonObject eo = v.toObject();
        PluginEffectDef def;
        def.id = eo["id"].toString();
        def.name = eo["name"].toString(def.id);
        def.effectType = eo["type"].toString("color_grade");

        if (eo.contains("params")) {
            const QJsonObject po = eo["params"].toObject();
            for (auto it = po.begin(); it != po.end(); ++it) {
                def.defaultParams[it.key()] = it.value().toDouble();
            }
        }
        info.effects.append(def);
    }

    // Remove old version if re-loading same pluginId
    if (m_plugins.contains(info.pluginId)) {
        m_loadOrder.removeAll(info.pluginId);
    }
    m_plugins[info.pluginId] = info;
    m_loadOrder.append(info.pluginId);

    emit pluginsChanged();
    saveState();
    return true;
}

bool PluginManager::removePlugin(const QString& pluginId) {
    if (!m_plugins.contains(pluginId)) return false;
    m_plugins.remove(pluginId);
    m_loadOrder.removeAll(pluginId);
    emit pluginsChanged();
    saveState();
    return true;
}

void PluginManager::setPluginEnabled(const QString& pluginId, bool enabled) {
    if (!m_plugins.contains(pluginId)) return;
    m_plugins[pluginId].enabled = enabled;
    emit pluginsChanged();
    saveState();
}

QList<PluginEffectDef> PluginManager::allEffects() const {
    QList<PluginEffectDef> result;
    for (const QString& id : m_loadOrder) {
        const auto& plug = m_plugins[id];
        if (!plug.enabled) continue;
        for (const auto& eff : plug.effects) {
            result.append(eff);
        }
    }
    return result;
}

bool PluginManager::findEffect(const QString& pluginId, const QString& effectId, PluginEffectDef* out) const {
    if (!m_plugins.contains(pluginId)) return false;
    for (const auto& eff : m_plugins[pluginId].effects) {
        if (eff.id == effectId) {
            if (out) *out = eff;
            return true;
        }
    }
    return false;
}

Effect PluginManager::buildEffect(const PluginEffectDef& def) {
    Effect eff;
    eff.type = def.effectType;
    eff.enabled = true;

    // Encode pluginId/effectId for round-tripping
    eff.params.push_back(EffectParameter{"_plugin_effect_id", 0.0});

    for (auto it = def.defaultParams.begin(); it != def.defaultParams.end(); ++it) {
        eff.params.push_back(EffectParameter{it.key(), it.value()});
    }
    return eff;
}

void PluginManager::saveState() const {
    QSettings settings("HyggshiCut", "Plugins");
    settings.setValue("loadOrder", m_loadOrder);
    for (const auto& id : m_loadOrder) {
        settings.setValue("path_" + id, m_plugins[id].filePath);
        settings.setValue("enabled_" + id, m_plugins[id].enabled);
    }
}

void PluginManager::loadState() {
    QSettings settings("HyggshiCut", "Plugins");
    const QStringList order = settings.value("loadOrder").toStringList();
    for (const QString& id : order) {
        const QString path = settings.value("path_" + id).toString();
        const bool enabled = settings.value("enabled_" + id, true).toBool();
        if (!path.isEmpty()) {
            QString err;
            if (loadFromFile(path, &err)) {
                if (m_plugins.contains(id)) {
                    m_plugins[id].enabled = enabled;
                }
            }
        }
    }
}

} // namespace hc
