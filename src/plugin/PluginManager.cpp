#include "PluginManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>
#include <cmath>

namespace hc {

namespace {
bool parameterRangeForEffect(const QString& type, const QString& name,
                             double* minimum, double* maximum) {
    if (type == "brightness" && name == "amount") { *minimum = -1.0; *maximum = 1.0; return true; }
    if ((type == "contrast" || type == "saturation") && name == "amount") { *minimum = 0.0; *maximum = 3.0; return true; }
    if (type == "hue_rotate" && name == "degrees") { *minimum = 0.0; *maximum = 360.0; return true; }
    if (type == "blur" && name == "radius") { *minimum = 0.0; *maximum = 30.0; return true; }
    if (type == "sharpen" && name == "amount") { *minimum = 0.0; *maximum = 3.0; return true; }
    if (type == "vignette" && (name == "strength" || name == "radius")) { *minimum = 0.0; *maximum = 1.0; return true; }
    if (type == "sepia" && name == "amount") { *minimum = 0.0; *maximum = 1.0; return true; }
    if (type == "crop" && (name == "left" || name == "top" || name == "right" || name == "bottom")) { *minimum = 0.0; *maximum = 0.9; return true; }
    if (type == "color_grade") {
        static const QSet<QString> names = {
            "lift", "gamma", "gain", "lift_luma", "gamma_luma", "gain_luma",
            "lift_r", "lift_g", "lift_b", "gamma_r", "gamma_g", "gamma_b",
            "gain_r", "gain_g", "gain_b"
        };
        if (names.contains(name)) { *minimum = -1.0; *maximum = 1.0; return true; }
    }
    return false;
}
} // namespace

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
    info.pluginId = root["pluginId"].toString().trimmed();
    static const QRegularExpression idPattern(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9_.-]{0,95}$"));
    if (!idPattern.match(info.pluginId).hasMatch()) {
        if (errorOut) *errorOut = "Invalid or missing 'pluginId' (use 1-96 letters, digits, '.', '_' or '-')";
        return false;
    }
    info.name = root["name"].toString(info.pluginId).trimmed();
    info.author = root["author"].toString().trimmed();
    info.description = root["description"].toString().trimmed();
    info.versionStr = root["version_num"].toString("1.0.0");
    info.type = root["type"].toString("effects");
    info.enabled = root["enabled"].toBool(true);
    info.filePath = QFileInfo(path).absoluteFilePath();

    // .plhc packages are declarative presets, not executable plugins. Each
    // entry must target a renderer/exporter effect that both preview and
    // export understand; otherwise reject the entire package instead of
    // silently shipping an effect that renders as a no-op.
    static const QSet<QString> supportedTypes = {
        "color_grade", "crop", "brightness", "contrast", "saturation",
        "hue_rotate", "blur", "sharpen", "vignette", "invert", "sepia"
    };
    const QJsonArray effects = root["effects"].toArray();
    if (effects.isEmpty()) {
        if (errorOut) *errorOut = "Plugin must provide at least one effect preset";
        return false;
    }
    QSet<QString> effectIds;
    for (int index = 0; index < effects.size(); ++index) {
        if (!effects.at(index).isObject()) {
            if (errorOut) *errorOut = QString("effects[%1] must be an object").arg(index);
            return false;
        }
        const QJsonObject eo = effects.at(index).toObject();
        PluginEffectDef def;
        def.pluginId = info.pluginId;
        def.id = eo["id"].toString().trimmed();
        def.name = eo["name"].toString(def.id).trimmed();
        def.effectType = eo["type"].toString("color_grade").trimmed();
        if (!idPattern.match(def.id).hasMatch() || effectIds.contains(def.id)) {
            if (errorOut) *errorOut = QString("Invalid or duplicate effect id '%1'").arg(def.id);
            return false;
        }
        if (!supportedTypes.contains(def.effectType)) {
            if (errorOut) *errorOut = QString("Effect '%1' uses unsupported type '%2'")
                .arg(def.id, def.effectType);
            return false;
        }
        effectIds.insert(def.id);

        const QJsonValue paramsValue = eo.value("params");
        if (!paramsValue.isUndefined() && !paramsValue.isObject()) {
            if (errorOut) *errorOut = QString("Effect '%1': 'params' must be an object").arg(def.id);
            return false;
        }
        const QJsonObject po = paramsValue.toObject();
        for (auto it = po.begin(); it != po.end(); ++it) {
            const double value = it.value().toDouble();
            double minimum = 0.0, maximum = 0.0;
            if (!it.value().isDouble() || !std::isfinite(value) ||
                !parameterRangeForEffect(def.effectType, it.key(), &minimum, &maximum)) {
                if (errorOut) *errorOut = QString("Effect '%1': unsupported or non-numeric parameter '%2'")
                    .arg(def.id, it.key());
                return false;
            }
            if (value < minimum || value > maximum) {
                if (errorOut) *errorOut = QString("Effect '%1': parameter '%2' must be between %3 and %4")
                    .arg(def.id, it.key()).arg(minimum).arg(maximum);
                return false;
            }
            def.defaultParams[it.key()] = value;
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
    eff.pluginId = def.pluginId;
    eff.pluginEffectId = def.id;
    eff.displayName = def.name;

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
            if (loadFromFile(path, &err) && m_plugins.contains(id)) {
                m_plugins[id].enabled = enabled;
            }
        }
    }
    // loadFromFile persists each successful load, so write once more after
    // applying the saved enabled flags to prevent the manifest default from
    // undoing the user's disabled state on the next application launch.
    saveState();
}

} // namespace hc
