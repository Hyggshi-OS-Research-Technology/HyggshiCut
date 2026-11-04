#include "../src/plugin/PluginManager.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QStandardPaths>

#include <iostream>

using namespace hc;

namespace {
int failures = 0;
void check(bool value, const char* message) {
    std::cout << (value ? "[ok] " : "[FAIL] ") << message << '\n';
    if (!value) ++failures;
}

QString writeManifest(const QString& path, const QString& type = QStringLiteral("brightness")) {
    QJsonObject params;
    params["amount"] = 0.25;
    QJsonObject effect;
    effect["id"] = "test_brightness";
    effect["name"] = "Test Brightness";
    effect["type"] = type;
    effect["params"] = params;
    QJsonArray effects;
    effects.append(effect);
    QJsonObject root;
    root["version"] = 1;
    root["pluginId"] = "arena_test_pack";
    root["name"] = "Arena Test Pack";
    root["effects"] = effects;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QJsonDocument(root).toJson());
    return path;
}
}

int main(int argc, char** argv) {
    QTemporaryDir temp;
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("HyggshiCut");
    QCoreApplication::setApplicationName("PluginManifestTest");

    auto& manager = PluginManager::instance();
    QString error;
    const QString validPath = writeManifest(temp.filePath("valid.plhc"));
    check(manager.loadFromFile(validPath, &error), "valid declarative preset manifest loads");
    const auto defs = manager.allEffects();
    check(defs.size() == 1, "enabled plugin exposes its effect definition");
    if (!defs.isEmpty()) {
        const Effect effect = PluginManager::buildEffect(defs.front());
        check(effect.type == "brightness" && effect.pluginId == "arena_test_pack" &&
                  effect.pluginEffectId == "test_brightness" &&
                  effect.displayName == "Test Brightness" &&
                  effect.paramValue("amount", 0.0) == 0.25,
              "plugin factory creates a native effect with provenance and defaults");
    }

    const QString invalidPath = writeManifest(temp.filePath("invalid.plhc"), "made_up_shader");
    error.clear();
    check(!manager.loadFromFile(invalidPath, &error) && error.contains("unsupported type"),
          "unsupported types fail with actionable diagnostics rather than becoming no-ops");
    check(manager.allEffects().size() == 1,
          "rejecting a bad replacement leaves the previously loaded plugin intact");

    manager.setPluginEnabled("arena_test_pack", false);
    check(manager.allEffects().isEmpty(), "disabled plugin effects are excluded");
    manager.removePlugin("arena_test_pack");
    return failures == 0 ? 0 : 1;
}
