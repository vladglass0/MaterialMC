// SPDX-License-Identifier: GPL-3.0-only
/*
 *  MaterialMC - Minecraft Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "SettingsApi.h"

#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>

#include "Application.h"
#include "settings/Setting.h"
#include "settings/SettingsObject.h"

#include "ApiRouter.h"
#include "ApiUtils.h"
#include "SensitiveChange.h"

namespace api {

namespace {

enum class Kind { Bool, Int, String, Json, StringList, Path };

struct SettingSpec {
    const char* key;
    Kind kind;
    /** Changes need native confirmation: the value decides which programs / libraries run. */
    bool sensitive = false;
    qint64 min = 0;
    qint64 max = 1 << 24;
    /** Never sent to the page (tokens, passwords); `settings.get` reports only whether it is set. */
    bool secret = false;
};

constexpr qint64 MaxMem = 1 << 22;

// Keep in sync with `LauncherSettings` in frontend/src/types/settings.ts.
// Paths are sensitive when typed by the page; `settings.pickFolder` sets them after a native folder dialog.
const SettingSpec SettingSpecs[] = {
    // Folders
    { "InstanceDir", Kind::Path, true },
    { "CentralModsDir", Kind::Path, true },
    { "IconsDir", Kind::Path, true },
    { "DownloadsDir", Kind::Path, true },
    { "SkinsDir", Kind::Path, true },
    { "JavaDir", Kind::Path, true },
    { "AdditionalInstanceDirs", Kind::StringList, true },
    { "DownloadsDirWatchRecursive", Kind::Bool },
    { "MoveModsFromDownloadsDir", Kind::Bool },
    // Java
    { "JavaPath", Kind::String, true },
    { "JvmArgs", Kind::String, true },
    { "MinMemAlloc", Kind::Int, false, 8, MaxMem },
    { "MaxMemAlloc", Kind::Int, false, 8, MaxMem },
    { "PermGen", Kind::Int, false, 4, 1 << 16 },
    { "LowMemWarning", Kind::Bool },
    { "AutomaticJavaSwitch", Kind::Bool },
    { "AutomaticJavaDownload", Kind::Bool },
    { "IgnoreJavaCompatibility", Kind::Bool },
    { "IgnoreJavaWizard", Kind::Bool },
    { "UserAskedAboutAutomaticJavaDownload", Kind::Bool },
    // Game window
    { "LaunchMaximized", Kind::Bool },
    { "MinecraftWinWidth", Kind::Int, false, 1, 1 << 16 },
    { "MinecraftWinHeight", Kind::Int, false, 1, 1 << 16 },
    { "CloseAfterLaunch", Kind::Bool },
    { "QuitAfterGameStop", Kind::Bool },
    // Console
    { "ShowConsole", Kind::Bool },
    { "AutoCloseConsole", Kind::Bool },
    { "ShowConsoleOnError", Kind::Bool },
    { "ConsoleMaxLines", Kind::Int, false, 1000, 100000000 },
    { "ConsoleOverflowStop", Kind::Bool },
    { "ConsoleFont", Kind::String },
    { "ConsoleFontSize", Kind::Int, false, 5, 72 },
    { "LogPrePostOutput", Kind::Bool },
    // Game time
    { "ShowGameTime", Kind::Bool },
    { "ShowGlobalGameTime", Kind::Bool },
    { "RecordGameTime", Kind::Bool },
    { "ShowGameTimeWithoutDays", Kind::Bool },
    // Workarounds / performance (native libraries and wrappers are sensitive)
    { "OnlineFixes", Kind::Bool },
    { "UseNativeOpenAL", Kind::Bool },
    { "CustomOpenALPath", Kind::String, true },
    { "UseNativeGLFW", Kind::Bool },
    { "CustomGLFWPath", Kind::String, true },
    { "UseNativeSDL", Kind::Bool },
    { "CustomSDLPath", Kind::String, true },
    { "EnableFeralGamemode", Kind::Bool },
    { "EnableMangoHud", Kind::Bool },
    { "UseDiscreteGpu", Kind::Bool },
    { "UseZink", Kind::Bool },
    // Custom commands and environment
    { "PreLaunchCommand", Kind::String, true },
    { "WrapperCommand", Kind::String, true },
    { "PostExitCommand", Kind::String, true },
    { "Env", Kind::Json, true },
    // Mods
    { "ModMetadataDisabled", Kind::Bool },
    { "ModDependenciesDisabled", Kind::Bool },
    { "SkipModpackUpdatePrompt", Kind::Bool },
    { "ShowModIncompat", Kind::Bool },
    { "DownloadGameFilesDuringInstanceCreation", Kind::Bool },
    { "ModUpdateReleaseTypes", Kind::Json },
    // User interface
    { "InstSortMode", Kind::String },
    { "InstRenamingMode", Kind::String },
    { "EditInstanceOnDoubleClick", Kind::Bool },
    { "EnableCat", Kind::Bool },
    { "TheCat", Kind::Bool },
    { "BackgroundCat", Kind::String },
    { "CatOpacity", Kind::Int, false, 0, 100 },
    { "CatFit", Kind::String },
    { "LastUsedGroupForNewInstance", Kind::String },
    { "LastOfflinePlayerName", Kind::String },
    // Network
    { "NumberOfConcurrentDownloads", Kind::Int, false, 1, 64 },
    { "NumberOfConcurrentTasks", Kind::Int, false, 1, 64 },
    { "NumberOfManualRetries", Kind::Int, false, 0, 50 },
    { "RequestTimeout", Kind::Int, false, 5, 3600 },
    { "ProxyType", Kind::String },
    { "ProxyAddr", Kind::String },
    { "ProxyPort", Kind::Int, false, 0, 65535 },
    { "ProxyUser", Kind::String },
    { "ProxyPass", Kind::String, false, 0, 0, true },
    // Services / API
    { "PastebinType", Kind::Int, false, 0, 16 },
    { "PastebinCustomAPIBase", Kind::String },
    { "MetaURLOverride", Kind::String },
    { "ResourceURLOverride", Kind::String },
    { "LegacyFMLLibsURLOverride", Kind::String },
    { "MetaRefreshOnLaunch", Kind::Bool },
    { "FallbackMRBlockedMods", Kind::Bool },
    { "UserAgentOverride", Kind::String },
    { "MSAClientIDOverride", Kind::String, false, 0, 0, true },
    { "ModrinthToken", Kind::String, false, 0, 0, true },
    { "FlameKeyOverride", Kind::String, false, 0, 0, true },
    { "TechnicClientID", Kind::String, false, 0, 0, true },
    { "FTBAppInstancesPath", Kind::Path, true },
    // External tools (programs the launcher starts)
    { "JsonEditor", Kind::String, true },
    { "JProfilerPath", Kind::String, true },
    { "JProfilerPort", Kind::Int, false, 1, 65535 },
    { "JVisualVMPath", Kind::String, true },
    { "WorldTools", Kind::Json, true },
};

const SettingSpec* findSpec(const QString& key)
{
    for (const auto& spec : SettingSpecs) {
        if (key == QLatin1String(spec.key)) {
            return &spec;
        }
    }
    return nullptr;
}

QJsonObject readAll()
{
    auto* settings = APPLICATION->settings();
    QJsonObject out;
    QJsonObject secrets;
    for (const auto& spec : SettingSpecs) {
        if (!settings->contains(spec.key)) {
            continue;  // e.g. a profiler that is not compiled in
        }
        const auto value = settings->get(spec.key);
        if (spec.secret) {
            secrets.insert(spec.key, !value.toString().isEmpty());
            out.insert(spec.key, QString());
            continue;
        }
        switch (spec.kind) {
            case Kind::Bool:
                out.insert(spec.key, value.toBool());
                break;
            case Kind::Int:
                out.insert(spec.key, value.toInt());
                break;
            case Kind::StringList:
                out.insert(spec.key, QJsonArray::fromStringList(value.toStringList()));
                break;
            default:
                out.insert(spec.key, value.toString());
                break;
        }
    }
    out.insert("_secretsSet", secrets);
    return out;
}

QString display(const QVariant& value)
{
    return value.typeId() == QMetaType::QStringList ? value.toStringList().join(", ") : value.toString();
}

bool isProxyKey(const QString& key)
{
    return key.startsWith(QLatin1String("Proxy"));
}

void applyProxy()
{
    auto* s = APPLICATION->settings();
    APPLICATION->updateProxySettings(s->get("ProxyType").toString(), s->get("ProxyAddr").toString(), s->get("ProxyPort").toInt(),
                                     s->get("ProxyUser").toString(), s->get("ProxyPass").toString());
}

}  // namespace

SettingsApi::SettingsApi(ApiRouter* router, QObject* parent) : QObject(parent), m_router(router)
{
    m_changedTimer.setSingleShot(true);
    m_changedTimer.setInterval(100);
    connect(&m_changedTimer, &QTimer::timeout, this, [this] {
        QJsonArray keys;
        for (const auto& key : std::as_const(m_changedKeys)) {
            keys.append(key);
        }
        m_changedKeys.clear();
        m_router->emitEvent("settings.changed", QJsonObject{ { "keys", keys } });
    });
    auto onChanged = [this](const Setting& setting) {
        if (findSpec(setting.id())) {
            m_changedKeys.insert(setting.id());
            m_changedTimer.start();
        }
    };
    connect(APPLICATION->settings(), &SettingsObject::SettingChanged, this, [onChanged](const Setting& s, const QVariant&) { onChanged(s); });
    connect(APPLICATION->settings(), &SettingsObject::settingReset, this, onChanged);

    router->addSync("settings.get", [](const QJsonObject&) { return readAll(); });

    // Deferred: sensitive changes show a native confirmation dialog.
    router->addDeferred("settings.set", [](const QJsonObject& p) {
        const auto values = params::requireObject(p, "values");
        auto* settings = APPLICATION->settings();

        QList<QPair<const SettingSpec*, QVariant>> updates;
        QList<QPair<QString, QString>> sensitive;
        bool proxyChanged = false;
        for (auto it = values.begin(); it != values.end(); ++it) {
            const auto* spec = findSpec(it.key());
            if (!spec || !settings->contains(spec->key)) {
                throw ApiError::invalidParams(tr("Unknown or read-only setting '%1'").arg(it.key()));
            }
            QVariant value;
            switch (spec->kind) {
                case Kind::Bool:
                    value = params::requireBool(values, it.key());
                    break;
                case Kind::Int:
                    value = static_cast<int>(params::requireInt(values, it.key(), spec->min, spec->max));
                    break;
                case Kind::StringList: {
                    auto list = params::requireStringList(values, it.key(), 256);
                    list.removeAll(QString());
                    value = list;
                    break;
                }
                case Kind::Json: {
                    const auto text = params::requireString(values, it.key(), 1 << 20);
                    QJsonParseError error{};
                    QJsonDocument::fromJson(text.toUtf8(), &error);
                    if (!text.isEmpty() && error.error != QJsonParseError::NoError) {
                        throw ApiError::invalidParams(tr("'%1' must be valid JSON: %2").arg(it.key(), error.errorString()));
                    }
                    value = text;
                    break;
                }
                default:
                    value = params::requireString(values, it.key(), 8192);
                    break;
            }
            if (spec->sensitive && settings->get(spec->key) != value) {
                sensitive.append({ QString::fromLatin1(spec->key), display(value) });
            }
            proxyChanged = proxyChanged || isProxyKey(it.key());
            updates.append({ spec, value });
        }
        if (updates.isEmpty()) {
            return readAll();
        }
        if (values.contains("MinMemAlloc") || values.contains("MaxMemAlloc")) {
            const auto minMem = values.contains("MinMemAlloc") ? values.value("MinMemAlloc").toInt() : settings->get("MinMemAlloc").toInt();
            const auto maxMem = values.contains("MaxMemAlloc") ? values.value("MaxMemAlloc").toInt() : settings->get("MaxMemAlloc").toInt();
            if (minMem > maxMem) {
                throw ApiError::invalidParams(tr("The minimum memory allocation must not exceed the maximum"));
            }
        }
        if (!sensitive.isEmpty() && !confirmSensitiveChange(tr("launcher settings"), sensitive)) {
            throw ApiError::permissionDenied(tr("The settings change was not confirmed"));
        }
        {
            SettingsObject::Lock lock(settings);
            for (const auto& [spec, value] : updates) {
                settings->set(spec->key, value);
            }
        }
        if (proxyChanged) {
            applyProxy();
        }
        return readAll();
    });

    router->addSync("settings.reset", [](const QJsonObject& p) {
        const auto keys = params::requireStringList(p, "keys", 256);
        auto* settings = APPLICATION->settings();
        bool proxyChanged = false;
        for (const auto& key : keys) {
            const auto* spec = findSpec(key);
            if (!spec || !settings->contains(spec->key)) {
                throw ApiError::invalidParams(tr("Unknown or read-only setting '%1'").arg(key));
            }
        }
        for (const auto& key : keys) {
            settings->reset(key);
            proxyChanged = proxyChanged || isProxyKey(key);
        }
        if (proxyChanged) {
            applyProxy();
        }
        return readAll();
    });

    // Folder settings chosen in a native dialog: no confirmation needed, the user picked the path themselves.
    router->addDeferred("settings.pickFolder", [](const QJsonObject& p) {
        const auto key = params::requireNonEmpty(p, "key", 64);
        const auto* spec = findSpec(key);
        if (!spec || (spec->kind != Kind::Path && spec->kind != Kind::StringList)) {
            throw ApiError::invalidParams(tr("'%1' is not a folder setting").arg(key));
        }
        auto* settings = APPLICATION->settings();
        const auto current = spec->kind == Kind::Path ? settings->get(key).toString() : QString();
        const auto dir = QFileDialog::getExistingDirectory(nullptr, tr("Select a folder"), current);
        if (dir.isEmpty()) {
            return QJsonValue(QJsonObject{ { "changed", false } });
        }
        if (spec->kind == Kind::StringList) {
            auto list = settings->get(key).toStringList();
            if (!list.contains(dir)) {
                list.append(dir);
            }
            settings->set(key, list);
        } else {
            settings->set(key, dir);
        }
        return QJsonValue(QJsonObject{ { "changed", true }, { "settings", readAll() } });
    });

    // Program paths chosen in a native file dialog (Java, profilers, editors).
    router->addDeferred("settings.pickFile", [](const QJsonObject& p) {
        const auto key = params::requireNonEmpty(p, "key", 64);
        static const QStringList s_fileKeys{ "JavaPath",      "JsonEditor",    "JProfilerPath", "JVisualVMPath",
                                             "CustomOpenALPath", "CustomGLFWPath", "CustomSDLPath" };
        if (!s_fileKeys.contains(key) || !APPLICATION->settings()->contains(key)) {
            throw ApiError::invalidParams(tr("'%1' is not a program setting").arg(key));
        }
        const auto file = QFileDialog::getOpenFileName(nullptr, tr("Select a file"), APPLICATION->settings()->get(key).toString());
        if (file.isEmpty()) {
            return QJsonValue(QJsonObject{ { "changed", false } });
        }
        APPLICATION->settings()->set(key, file);
        return QJsonValue(QJsonObject{ { "changed", true }, { "settings", readAll() } });
    });
}

}  // namespace api
