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

#include "InstanceApi.h"

#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QPointer>

#include "Application.h"
#include "InstanceCopyPrefs.h"
#include "InstanceDirUpdate.h"
#include "InstanceCopyTask.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "icons/IconList.h"
#include "launch/LaunchTask.h"
#include "meta/Index.h"
#include "meta/VersionList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/ShortcutUtils.h"
#include "minecraft/WorldList.h"
#include "minecraft/launch/MinecraftTarget.h"
#include "interaction/UserInteraction.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "tools/BaseProfiler.h"
#include "DesktopServices.h"
#include "FileSystem.h"
#include "settings/SettingsObject.h"

#include "ApiRouter.h"
#include "ApiUtils.h"
#include "SensitiveChange.h"
#include "TaskTracker.h"

namespace api {

namespace {

struct LoaderInfo {
    const char* uid;
    const char* kind;
};
constexpr LoaderInfo Loaders[] = {
    { "net.fabricmc.fabric-loader", "fabric" }, { "org.quiltmc.quilt-loader", "quilt" }, { "net.neoforged", "neoforge" },
    { "net.minecraftforge", "forge" },          { "com.mumfrey.liteloader", "liteloader" },
};

int countModFiles(const QString& dir)
{
    return static_cast<int>(QDir(dir).entryList(QDir::Files | QDir::NoDotAndDotDot).size());
}

/** Instance settings the web UI may edit: JSON field -> settings key. `sensitive` fields need native confirmation. */
struct SettingField {
    const char* field;
    const char* key;
    QMetaType::Type type;
    bool sensitive;
};
constexpr SettingField InstanceSettingFields[] = {
    { "overrideMemory", "OverrideMemory", QMetaType::Bool, false },
    { "minMemAlloc", "MinMemAlloc", QMetaType::Int, false },
    { "maxMemAlloc", "MaxMemAlloc", QMetaType::Int, false },
    { "overrideJavaLocation", "OverrideJavaLocation", QMetaType::Bool, false },
    { "javaPath", "JavaPath", QMetaType::QString, true },
    { "overrideJavaArgs", "OverrideJavaArgs", QMetaType::Bool, false },
    { "jvmArgs", "JvmArgs", QMetaType::QString, true },
    { "overrideWindow", "OverrideWindow", QMetaType::Bool, false },
    { "launchMaximized", "LaunchMaximized", QMetaType::Bool, false },
    { "minecraftWinWidth", "MinecraftWinWidth", QMetaType::Int, false },
    { "minecraftWinHeight", "MinecraftWinHeight", QMetaType::Int, false },
    { "useAccountForInstance", "UseAccountForInstance", QMetaType::Bool, false },
    { "instanceAccountId", "InstanceAccountId", QMetaType::QString, false },
    { "joinServerOnLaunch", "JoinServerOnLaunch", QMetaType::Bool, false },
    { "joinServerOnLaunchAddress", "JoinServerOnLaunchAddress", QMetaType::QString, false },
};

}  // namespace

InstanceApi::InstanceApi(ApiRouter* router, TaskTracker* tasks, QObject* parent) : QObject(parent), m_router(router), m_tasks(tasks)
{
    m_changedTimer.setSingleShot(true);
    m_changedTimer.setInterval(100);
    connect(&m_changedTimer, &QTimer::timeout, this, [this] {
        watchInstances();
        m_router->emitEvent("instances.changed");
    });
    watchInstances();

    auto* list = APPLICATION->instances();
    connect(list, &QAbstractItemModel::rowsInserted, this, &InstanceApi::scheduleChanged);
    connect(list, &QAbstractItemModel::rowsRemoved, this, &InstanceApi::scheduleChanged);
    connect(list, &QAbstractItemModel::modelReset, this, &InstanceApi::scheduleChanged);
    connect(list, &QAbstractItemModel::dataChanged, this, &InstanceApi::scheduleChanged);
    connect(list, &InstanceList::instancesChanged, this, &InstanceApi::scheduleChanged);
    connect(list, &InstanceList::groupsChanged, this, &InstanceApi::scheduleChanged);
    connect(APPLICATION->icons(), &IconList::iconUpdated, this, &InstanceApi::scheduleChanged);

    registerMethods();
}

InstanceApi::~InstanceApi() = default;

void InstanceApi::scheduleChanged()
{
    if (!m_changedTimer.isActive()) {
        m_changedTimer.start();
    }
}

void InstanceApi::watchInstances()
{
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); i++) {
        watchInstance(list->at(i));
    }
}

void InstanceApi::watchInstance(MinecraftInstance* instance)
{
    if (m_watched.contains(instance)) {
        return;
    }
    m_watched.insert(instance);
    connect(instance, &QObject::destroyed, this, [this, instance] { m_watched.remove(instance); });
    connect(instance, &BaseInstance::propertiesChanged, this, &InstanceApi::scheduleChanged);
    connect(instance, &BaseInstance::launchTaskChanged, this, [this, instance](LaunchTask* task) {
        const auto id = instance->id();
        emit launchTaskChanged(id, task);
        if (!task) {
            return;
        }
        // The launcher part emits readyForLaunch once the game process is up.
        connect(task, &LaunchTask::readyForLaunch, this, [this, id] {
            if (m_states.value(id) == QLatin1String("launching")) {
                m_states[id] = "running";
                m_router->emitEvent("instance.started", QJsonObject{ { "instanceId", id } });
                scheduleChanged();
            }
        });
    });
}

QString InstanceApi::stateOf(MinecraftInstance* instance) const
{
    const auto state = m_states.value(instance->id());
    if (!state.isEmpty()) {
        return state;
    }
    return instance->isRunning() ? QStringLiteral("running") : QStringLiteral("stopped");
}

QJsonObject InstanceApi::serializeInstance(MinecraftInstance* instance, const QString& state)
{
    auto* profile = loadedPackProfile(instance);
    QJsonValue loader;
    QString mcVersion;
    if (profile) {
        mcVersion = profile->getComponentVersion("net.minecraft");
        for (const auto& l : Loaders) {
            const auto version = profile->getComponentVersion(l.uid);
            if (!version.isEmpty()) {
                loader = QJsonObject{ { "kind", l.kind }, { "version", version } };
                break;
            }
        }
    }
    const auto group = APPLICATION->instances()->getInstanceGroup(instance->id());
    QJsonValue managed;
    if (instance->isManagedPack()) {
        managed = QJsonObject{ { "type", instance->getManagedPackType() },
                               { "name", instance->getManagedPackName() },
                               { "version", instance->getManagedPackVersionName() } };
    }
    const auto iconKey = instance->iconKey();
    return {
        { "id", instance->id() },
        { "name", instance->name() },
        { "group", group.isEmpty() ? QJsonValue() : QJsonValue(group) },
        { "iconKey", iconKey },
        { "iconUrl", hostResourceUrl({ "_icon", iconKey }) },
        { "minecraftVersion", mcVersion.isEmpty() ? QJsonValue() : QJsonValue(mcVersion) },
        { "loader", loader },
        { "state", state },
        { "modCount", countModFiles(instance->modsRoot()) },
        { "lastLaunch", timestamp(instance->lastLaunch()) },
        { "totalPlayTime", static_cast<double>(instance->totalTimePlayed()) },
        { "lastPlayTime", static_cast<double>(instance->lastTimePlayed()) },
        { "canLaunch", instance->canLaunch() },
        { "hasCrashed", instance->hasCrashed() },
        { "hasVersionBroken", instance->hasVersionBroken() },
        { "managedPack", managed },
        { "supportsDemo", instance->supportsDemo() },
        { "shortcutCount", static_cast<int>(instance->shortcuts().size()) },
    };
}

void InstanceApi::registerMethods()
{
    m_router->addSync("instances.list", [this](const QJsonObject&) { return list(); });
    m_router->addSync("instances.groups", [](const QJsonObject&) {
        auto groups = APPLICATION->instances()->getGroups();
        groups.removeAll(QString());
        groups.sort(Qt::CaseInsensitive);
        return QJsonArray::fromStringList(groups);
    });
    m_router->addSync("instances.get", [this](const QJsonObject& p) { return get(p); });
    m_router->addSync("instances.copy", [this](const QJsonObject& p) { return copy(p); });
    m_router->addDeferred("instances.remove", [this](const QJsonObject& p) { return remove(p); });
    // May ask whether to rename the folder too (InstRenamingMode), hence deferred.
    m_router->addDeferred("instances.rename", [](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        const auto name = params::requireNonEmpty(p, "name", 256);
        const auto before = instance->name();
        instance->setName(name);
        auto id = instance->id();
        if (auto newRoot = askToUpdateInstanceDirName(instance, before, name); !newRoot.isEmpty()) {
            auto* list = APPLICATION->instances();
            const auto newId = QFileInfo(newRoot).fileName();
            const auto group = list->getInstanceGroup(id);
            const bool syncGroup = !group.isEmpty() && id != newId;
            if (syncGroup) {
                list->setInstanceGroup(id, GroupId());
            }
            list->loadList();
            if (syncGroup) {
                list->setInstanceGroup(newId, group);
            }
            id = newId;
        }
        return QJsonObject{ { "ok", true }, { "id", id } };
    });
    m_router->addSync("instances.renameGroup", [](const QJsonObject& p) {
        const auto group = params::requireNonEmpty(p, "group", 256);
        const auto name = params::requireNonEmpty(p, "name", 256).simplified();
        auto* list = APPLICATION->instances();
        if (!list->getGroups().contains(group)) {
            throw ApiError::notFound(tr("Group '%1' does not exist").arg(group));
        }
        if (list->getGroups().contains(name, Qt::CaseInsensitive) && group.toLower() != name.toLower()) {
            throw ApiError::invalidParams(QCoreApplication::translate("MainWindow", "Group already exists. :/"));
        }
        list->renameGroup(group, name);
        return ok();
    });
    m_router->addSync("instances.deleteGroup", [](const QJsonObject& p) {
        const auto group = params::requireNonEmpty(p, "group", 256);
        APPLICATION->instances()->deleteGroup(group);
        return ok();
    });
    m_router->addSync("instances.setGroupCollapsed", [](const QJsonObject& p) {
        const auto group = params::requireString(p, "group", 256);
        APPLICATION->instances()->on_GroupStateChanged(group, params::requireBool(p, "collapsed"));
        return ok();
    });
    m_router->addSync("instances.overview", [](const QJsonObject&) {
        auto* list = APPLICATION->instances();
        QJsonArray collapsed;
        for (const auto& g : list->getGroups()) {
            if (!g.isEmpty() && list->isGroupCollapsed(g)) {
                collapsed.append(g);
            }
        }
        return QJsonObject{
            { "canUndoTrash", list->trashedSomething() },
            { "totalPlayTime", static_cast<double>(APPLICATION->playtimeSettings()->get("TotalPlayTime").toLongLong()) },
            { "showGlobalGameTime", APPLICATION->settings()->get("ShowGlobalGameTime").toBool() },
            { "collapsedGroups", collapsed },
        };
    });
    m_router->addDeferred("instances.undoTrash", [this](const QJsonObject&) {
        auto* list = APPLICATION->instances();
        if (!list->trashedSomething()) {
            throw ApiError::notFound(tr("Nothing to restore"));
        }
        if (!list->undoTrashInstance()) {
            throw ApiError::io(QCoreApplication::translate(
                "MainWindow", "Some instances and shortcuts could not be restored.\nPlease check your trashbin to manually restore them."));
        }
        scheduleChanged();
        return ok();
    });
    m_router->addSync("instances.profilers", [](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        QJsonArray out;
        for (auto it = APPLICATION->profilers().cbegin(); it != APPLICATION->profilers().cend(); ++it) {
            QString error;
            const bool available = it.value()->check(&error);
            out.append(QJsonObject{ { "key", it.key() }, { "name", it.value()->name() }, { "available", available }, { "error", error } });
        }
        return QJsonObject{ { "selected", instance->settings()->get("Profiler").toString() }, { "profilers", out } };
    });
    m_router->addSync("instances.setProfiler", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        const auto key = params::requireString(p, "profiler", 64);
        if (!key.isEmpty() && !APPLICATION->profilers().contains(key)) {
            throw ApiError::notFound(tr("Unknown profiler '%1'").arg(key));
        }
        instance->settings()->set("Profiler", key);
        scheduleChanged();
        return ok();
    });
    m_router->addSync("instances.shortcutTargets", [](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        QJsonArray targets;
        if (!DesktopServices::isFlatpak()) {
            if (!FS::getDesktopDir().isEmpty()) {
                targets.append("desktop");
            }
            if (!FS::getApplicationsDir().isEmpty()) {
                targets.append("applications");
            }
        }
        targets.append("other");
        QJsonArray worlds;
        const bool quickJoin = instance->traits().contains("feature:is_quick_play_singleplayer");
        if (quickJoin) {
            auto* worldList = instance->worldList();
            worldList->update();
            for (const auto& world : worldList->allWorlds()) {
                worlds.append(QJsonObject{ { "name", world.name() }, { "lastPlayed", timestamp(world.lastPlayed()) } });
            }
        }
        return QJsonObject{ { "targets", targets }, { "worlds", worlds }, { "quickPlaySingleplayer", quickJoin } };
    });
    m_router->addSync("instances.copyInfo", [](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        const auto fs = FS::statFS(instance->instanceRoot()).fsType;
        return QJsonObject{ { "cloneSupported", FS::canCloneOnFS(fs) },
                            { "linkSupported", FS::canLinkOnFS(fs) },
                            { "filesystem", FS::getFilesystemTypeName(fs) } };
    });
    m_router->addDeferred("instances.createShortcut", [](const QJsonObject& p) { return createShortcut(p); });
    m_router->addSync("instances.setGroup", [](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        const auto group = params::optionalString(p, "group", 256).value_or(QString()).trimmed();
        APPLICATION->instances()->setInstanceGroup(instance->id(), group);
        return ok();
    });
    m_router->addSync("instances.setIcon", [](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        const auto key = params::requireNonEmpty(p, "iconKey", 256);
        if (APPLICATION->icons()->getIconIndex(key) < 0) {
            throw ApiError::notFound(QObject::tr("Unknown icon '%1'").arg(key));
        }
        instance->setIconKey(key);
        return ok();
    });
    m_router->addSync("instances.setNotes", [](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        instance->setNotes(params::requireString(p, "notes", 1 << 20));
        return ok();
    });
    m_router->addSync("instances.getSettings", [this](const QJsonObject& p) { return getSettings(p); });
    m_router->addSync("instances.setSettings", [this](const QJsonObject& p) { return setSettings(p); });
    // Deferred: the launch controller may ask questions (account, player name, demo) before it returns.
    m_router->addDeferred("instances.launch", [this](const QJsonObject& p) { return launch(p); });
    m_router->addSync("instances.kill", [this](const QJsonObject& p) { return kill(p); });

    // Creating needs the version list loaded, which may involve a download: asynchronous.
    m_router->add("instances.create", [this](const QJsonObject& p, const ApiReply& reply) {
        const auto name = params::requireNonEmpty(p, "name", 256);
        const auto group = params::optionalString(p, "group", 256).value_or(QString()).trimmed();
        const auto iconKey = params::optionalString(p, "iconKey", 256).value_or(QStringLiteral("default"));
        const auto mcVersion = params::requireNonEmpty(p, "minecraftVersion", 128);
        QString loaderUid;
        QString loaderVersion;
        if (!params::isMissing(p.value("loader"))) {
            const auto loader = params::requireObject(p, "loader");
            const auto kind = params::requireNonEmpty(loader, "kind", 32);
            for (const auto& l : Loaders) {
                if (kind == QLatin1String(l.kind)) {
                    loaderUid = l.uid;
                }
            }
            if (loaderUid.isEmpty()) {
                throw ApiError::invalidParams(QObject::tr("Unknown mod loader '%1'").arg(kind));
            }
            loaderVersion = params::requireNonEmpty(loader, "version", 128);
        }

        auto whenLoaded = [this, reply, name, group, iconKey, mcVersion, loaderUid, loaderVersion] {
            reply.guard([&] {
                auto mcList = APPLICATION->metadataIndex()->get("net.minecraft");
                if (!mcList->hasVersion(mcVersion)) {
                    throw ApiError::notFound(QObject::tr("Minecraft version %1 does not exist").arg(mcVersion));
                }
                InstanceTask* creation = nullptr;
                if (loaderUid.isEmpty()) {
                    creation = new VanillaCreationTask(mcList->getVersion(mcVersion));
                } else {
                    auto loaderList = APPLICATION->metadataIndex()->get(loaderUid);
                    if (!loaderList->hasVersion(loaderVersion)) {
                        throw ApiError::notFound(QObject::tr("Loader version %1 does not exist").arg(loaderVersion));
                    }
                    creation = new VanillaCreationTask(mcList->getVersion(mcVersion), loaderUid, loaderList->getVersion(loaderVersion));
                }
                creation->setName(name);
                creation->setOriginalName(name, mcVersion);
                creation->setGroup(group);
                creation->setIcon(iconKey);
                APPLICATION->settings()->set("LastUsedGroupForNewInstance", group);

                Task::Ptr task(APPLICATION->instances()->wrapInstanceTask(creation));
                const auto taskId = m_tasks->start(task, "instance.create", QObject::tr("Creating %1").arg(name));
                reply.resolve(QJsonObject{ { "taskId", taskId } });
            });
        };

        // Make sure both version lists are loaded (from cache or network) before validating.
        QList<Meta::VersionList::Ptr> lists{ APPLICATION->metadataIndex()->get("net.minecraft") };
        if (!loaderUid.isEmpty()) {
            lists << APPLICATION->metadataIndex()->get(loaderUid);
        }
        auto pending = std::make_shared<int>(0);
        auto failed = std::make_shared<bool>(false);
        for (const auto& vlist : lists) {
            if (vlist->isLoaded()) {
                continue;
            }
            auto task = vlist->getLoadTask();
            if (!task) {
                continue;
            }
            ++*pending;
            connect(task.get(), &Task::finished, this, [task, pending, failed, whenLoaded, reply]() {
                if (!task->wasSuccessful() && !*failed) {
                    *failed = true;
                    reply.reject(ApiError("NETWORK_ERROR", QObject::tr("Could not load the version list: %1").arg(task->failReason())));
                }
                if (--*pending == 0 && !*failed) {
                    whenLoaded();
                }
            }, Qt::SingleShotConnection);
            if (!task->isRunning()) {
                task->start();
            }
        }
        if (*pending == 0) {
            whenLoaded();
        }
    });
}

QJsonValue InstanceApi::list() const
{
    QJsonArray out;
    auto* list = APPLICATION->instances();
    for (int i = 0; i < list->count(); i++) {
        auto* instance = list->at(i);
        out.append(serializeInstance(instance, stateOf(instance)));
    }
    return out;
}

QJsonValue InstanceApi::get(const QJsonObject& params) const
{
    auto* instance = requireInstance(params);
    auto obj = serializeInstance(instance, stateOf(instance));
    obj.insert("notes", instance->notes());
    obj.insert("instanceRoot", instance->instanceRoot());
    obj.insert("gameRoot", instance->gameRoot());
    QJsonArray components;
    if (auto* profile = loadedPackProfile(instance)) {
        for (int i = 0; i < profile->rowCount(); i++) {
            auto component = profile->getComponent(static_cast<size_t>(i));
            if (!component) {
                continue;
            }
            components.append(QJsonObject{
                { "uid", component->getID() },
                { "name", component->getName() },
                { "version", component->getVersion() },
                { "enabled", component->isEnabled() },
                { "important", !component->isRemovable() },
            });
        }
    }
    obj.insert("components", components);
    obj.insert("statusDescription", instance->getStatusbarDescription());
    QJsonArray shortcuts;
    for (const auto& sc : instance->shortcuts()) {
        shortcuts.append(QJsonObject{ { "name", sc.name }, { "path", sc.filePath } });
    }
    obj.insert("shortcuts", shortcuts);
    return obj;
}

QJsonValue InstanceApi::launch(const QJsonObject& params)
{
    auto* instance = requireInstance(params);
    if (instance->isRunning() || m_states.contains(instance->id())) {
        throw ApiError("INSTANCE_RUNNING", tr("%1 is already running").arg(instance->name()));
    }
    if (!instance->canLaunch()) {
        throw ApiError("LAUNCH_FAILED", tr("%1 cannot be launched (its version may be broken)").arg(instance->name()));
    }

    static constexpr std::pair<const char*, LaunchMode> Modes[] = {
        { "normal", LaunchMode::Normal }, { "offline", LaunchMode::Offline }, { "demo", LaunchMode::Demo }
    };
    const auto mode = params::isMissing(params.value("mode")) ? LaunchMode::Normal : params::requireEnum(params, "mode", Modes);
    const auto offlineName = params::optionalString(params, "offlineName", 16).value_or(QString()).trimmed();
    if (mode != LaunchMode::Normal && !offlineName.isEmpty()) {
        static const QRegularExpression s_validName(QStringLiteral("^[A-Za-z0-9_]{1,16}$"));
        if (!s_validName.match(offlineName).hasMatch()) {
            throw ApiError::invalidParams(tr("Player names may only contain letters, digits and underscores (1-16 characters)"));
        }
    }

    MinecraftAccountPtr account;
    if (const auto accountId = params::optionalString(params, "accountId", 256)) {
        auto* accounts = APPLICATION->accounts();
        for (int i = 0; i < accounts->count(); i++) {
            if (accounts->at(i)->internalId() == *accountId) {
                account = accounts->at(i);
            }
        }
        if (!account) {
            throw ApiError::accountNotFound(*accountId);
        }
    }

    MinecraftTarget::Ptr target;
    if (const auto server = params::optionalString(params, "server", 512); server && !server->trimmed().isEmpty()) {
        target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(server->trimmed(), false));
    } else if (const auto world = params::optionalString(params, "world", 255); world && !world->isEmpty()) {
        target = std::make_shared<MinecraftTarget>(MinecraftTarget::parse(*world, true));
    }

    if (const auto profiler = params::optionalString(params, "profiler", 64)) {
        if (!profiler->isEmpty() && !APPLICATION->profilers().contains(*profiler)) {
            throw ApiError::notFound(tr("Unknown profiler '%1'").arg(*profiler));
        }
        instance->settings()->set("Profiler", *profiler);
    }

    const auto id = instance->id();
    m_launchErrors.remove(id);
    m_requestedOfflineName.insert(id, offlineName);
    m_states[id] = "launching";
    if (!APPLICATION->launch(instance, mode, target, account, offlineName)) {
        m_states.remove(id);
        throw ApiError("LAUNCH_FAILED", tr("The launcher refused to start %1 (an update may be running)").arg(instance->name()));
    }
    m_router->emitEvent("instance.launching", QJsonObject{ { "instanceId", id } });
    scheduleChanged();
    return ok();
}

QJsonValue InstanceApi::kill(const QJsonObject& params)
{
    auto* instance = requireInstance(params);
    if (!instance->isRunning() && !m_states.contains(instance->id())) {
        throw ApiError("INSTANCE_NOT_RUNNING", tr("%1 is not running").arg(instance->name()));
    }
    m_killRequested.insert(instance->id());
    if (!APPLICATION->kill(instance)) {
        m_killRequested.remove(instance->id());
        throw ApiError("LAUNCH_FAILED", tr("%1 cannot be stopped right now").arg(instance->name()));
    }
    return ok();
}

QJsonValue InstanceApi::remove(const QJsonObject& params)
{
    auto* instance = requireInstance(params);
    if (instance->isRunning() || m_states.contains(instance->id())) {
        throw ApiError("INSTANCE_RUNNING", tr("Stop %1 before deleting it").arg(instance->name()));
    }
    const auto id = instance->id();
    if (!checkLinkedInstances(id, QCoreApplication::translate("MainWindow", "Deleting"))) {
        throw ApiError::cancelled();
    }
    if (!APPLICATION->instances()->trashInstance(id)) {
        APPLICATION->instances()->deleteInstance(id);
    }
    APPLICATION->settings()->set("SelectedInstance", QString());
    return ok();
}

QJsonValue InstanceApi::copy(const QJsonObject& params)
{
    auto* instance = requireInstance(params);
    const auto name = params::requireNonEmpty(params, "name", 256);
    InstanceCopyPrefs prefs;
    prefs.enableCopySaves(params::optionalBool(params, "copySaves", true));
    prefs.enableKeepPlaytime(params::optionalBool(params, "keepPlaytime", true));
    prefs.enableCopyGameOptions(params::optionalBool(params, "copyGameOptions", true));
    prefs.enableCopyResourcePacks(params::optionalBool(params, "copyResourcePacks", true));
    prefs.enableCopyShaderPacks(params::optionalBool(params, "copyShaderPacks", true));
    prefs.enableCopyServers(params::optionalBool(params, "copyServers", true));
    prefs.enableCopyMods(params::optionalBool(params, "copyMods", true));
    prefs.enableCopyScreenshots(params::optionalBool(params, "copyScreenshots", true));
    prefs.enableUseSymLinks(params::optionalBool(params, "useSymLinks", false));
    prefs.enableLinkRecursively(params::optionalBool(params, "linkRecursively", false));
    prefs.enableUseHardLinks(params::optionalBool(params, "useHardLinks", false));
    prefs.enableDontLinkSaves(params::optionalBool(params, "dontLinkSaves", false));
    prefs.enableUseClone(params::optionalBool(params, "useClone", false));
    auto* copyTask = new InstanceCopyTask(instance, prefs);
    copyTask->setName(name);
    copyTask->setGroup(params::optionalString(params, "group", 256).value_or(QString()));
    const auto iconKey = params::optionalString(params, "iconKey", 256).value_or(instance->iconKey());
    if (APPLICATION->icons()->getIconIndex(iconKey) < 0) {
        throw ApiError::notFound(tr("Unknown icon '%1'").arg(iconKey));
    }
    copyTask->setIcon(iconKey);
    Task::Ptr task(APPLICATION->instances()->wrapInstanceTask(copyTask));
    return QJsonObject{ { "taskId", m_tasks->start(task, "instance.copy", tr("Copying %1").arg(instance->name()), instance->id()) } };
}

QJsonValue InstanceApi::createShortcut(const QJsonObject& params)
{
    auto* instance = requireInstance(params);
    static constexpr std::pair<const char*, ShortcutTarget> Targets[] = {
        { "desktop", ShortcutTarget::Desktop }, { "applications", ShortcutTarget::Applications }, { "other", ShortcutTarget::Other }
    };
    const auto target = params::requireEnum(params, "target", Targets);
    auto name = params::optionalString(params, "name", 256).value_or(QString()).trimmed();
    const auto iconKey = params::optionalString(params, "iconKey", 256).value_or(instance->iconKey());

    QString targetString = QCoreApplication::translate("CreateShortcutDialog", "instance");
    QStringList extraArgs;
    if (const auto world = params::optionalString(params, "world", 255); world && !world->isEmpty()) {
        targetString = QCoreApplication::translate("CreateShortcutDialog", "world");
        extraArgs = { "--world", *world };
        if (name.isEmpty()) {
            name = QCoreApplication::translate("CreateShortcutDialog", "%1 - %2").arg(instance->name(), *world);
        }
    } else if (const auto server = params::optionalString(params, "server", 512); server && !server->trimmed().isEmpty()) {
        targetString = QCoreApplication::translate("CreateShortcutDialog", "server");
        extraArgs = { "--server", server->trimmed() };
        if (name.isEmpty()) {
            name = QCoreApplication::translate("CreateShortcutDialog", "%1 - Server %2").arg(instance->name(), server->trimmed());
        }
    }
    if (name.isEmpty()) {
        name = instance->name();
    }
    if (const auto accountId = params::optionalString(params, "accountId", 256)) {
        MinecraftAccountPtr account;
        auto* accounts = APPLICATION->accounts();
        for (int i = 0; i < accounts->count(); i++) {
            if (accounts->at(i)->internalId() == *accountId) {
                account = accounts->at(i);
            }
        }
        if (!account) {
            throw ApiError::accountNotFound(*accountId);
        }
        extraArgs.append({ "--profile", account->profileName() });
    }

    ShortcutUtils::Shortcut args{ instance, name, targetString, extraArgs, iconKey, target };
    bool created = false;
    if (target == ShortcutTarget::Desktop) {
        created = ShortcutUtils::createInstanceShortcutOnDesktop(args);
    } else if (target == ShortcutTarget::Applications) {
        created = ShortcutUtils::createInstanceShortcutInApplications(args);
    } else {
        created = ShortcutUtils::createInstanceShortcutInOther(args);
    }
    return QJsonObject{ { "created", created } };
}

QJsonObject InstanceApi::readSettings(MinecraftInstance* instance)
{
    auto* settings = instance->settings();
    QJsonObject out;
    for (const auto& f : InstanceSettingFields) {
        const auto value = settings->get(f.key);
        switch (f.type) {
            case QMetaType::Bool:
                out.insert(f.field, value.toBool());
                break;
            case QMetaType::Int:
                out.insert(f.field, value.toInt());
                break;
            default:
                out.insert(f.field, value.toString());
                break;
        }
    }
    return out;
}

QJsonValue InstanceApi::getSettings(const QJsonObject& params) const
{
    return readSettings(requireInstance(params));
}

QJsonValue InstanceApi::setSettings(const QJsonObject& params)
{
    auto* instance = requireInstance(params);
    const auto patch = params::requireObject(params, "settings");

    // Validate everything first, then apply atomically.
    QList<QPair<const SettingField*, QVariant>> updates;
    QList<QPair<QString, QString>> sensitive;
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const SettingField* field = nullptr;
        for (const auto& f : InstanceSettingFields) {
            if (it.key() == QLatin1String(f.field)) {
                field = &f;
            }
        }
        if (!field) {
            throw ApiError::invalidParams(tr("Unknown instance setting '%1'").arg(it.key()));
        }
        QVariant value;
        switch (field->type) {
            case QMetaType::Bool:
                value = params::requireBool(patch, it.key());
                break;
            case QMetaType::Int:
                value = static_cast<int>(params::requireInt(patch, it.key(), 0, 1 << 24));
                break;
            default:
                value = params::requireString(patch, it.key(), 8192);
                break;
        }
        if (field->sensitive && instance->settings()->get(field->key) != value) {
            sensitive.append({ QString::fromLatin1(field->key), value.toString() });
        }
        updates.append({ field, value });
    }
    if (!sensitive.isEmpty() && !confirmSensitiveChange(instance->name(), sensitive)) {
        throw ApiError::permissionDenied(tr("The settings change was not confirmed"));
    }

    auto* settings = instance->settings();
    SettingsObject::Lock lock(settings);
    for (const auto& [field, value] : updates) {
        settings->set(field->key, value);
    }
    if (!settings->get("UseAccountForInstance").toBool()) {
        settings->set("InstanceAccountId", QString());
    }
    return readSettings(instance);
}

// ---------------------------------------------------------------------------------------------------------------------
// LaunchInteraction: answers LaunchController's questions from the launch request and reports problems as events.

void InstanceApi::recordLaunchError(MinecraftInstance* instance, const ApiError& error)
{
    // Keep the first, most specific reason.
    if (!m_launchErrors.contains(instance->id())) {
        m_launchErrors.insert(instance->id(), error);
    }
}

MinecraftAccountPtr InstanceApi::chooseAccount(MinecraftInstance* instance)
{
    // Same question as the former ProfileSelectDialog: which account, and should it become the default?
    auto* accounts = APPLICATION->accounts();
    QJsonArray items;
    for (int i = 0; i < accounts->count(); i++) {
        const auto account = accounts->at(i);
        items.append(QJsonObject{ { "id", account->internalId() },
                                  { "label", account->displayName() },
                                  { "description", account->typeString() + (account->ownsMinecraft() ? QString() : " · " + tr("no Minecraft license")) } });
    }
    if (items.isEmpty()) {
        recordLaunchError(instance, ApiError("NO_ACCOUNT", tr("No Minecraft account is signed in.")));
        return nullptr;
    }
    interaction::Prompt prompt;
    prompt.kind = "choice";
    prompt.title = QCoreApplication::translate("ProfileSelectDialog", "Select an Account");
    prompt.text = QCoreApplication::translate("ProfileSelectDialog", "Select a profile.");
    prompt.buttons = { interaction::reject(tr("Cancel")), interaction::accept(tr("OK")) };
    prompt.defaultButton = "ok";
    prompt.checkbox = QCoreApplication::translate("ProfileSelectDialog", "Use as default?");
    prompt.payload = { { "items", items }, { "selected", items.first().toObject().value("id") } };
    const auto answer = interaction::UserInteraction::instance()->askBlocking(prompt);
    if (!answer.is("ok")) {
        recordLaunchError(instance, ApiError::cancelled(tr("No account was selected")));
        return nullptr;
    }
    const auto selected = answer.data.value("selected").toString();
    for (int i = 0; i < accounts->count(); i++) {
        if (accounts->at(i)->internalId() == selected) {
            if (answer.checked) {
                accounts->setDefaultAccount(accounts->at(i));
            }
            return accounts->at(i);
        }
    }
    recordLaunchError(instance, ApiError("NO_ACCOUNT", tr("No account was selected")));
    return nullptr;
}

bool InstanceApi::waitForTask(MinecraftInstance* instance, Task* task, const QString& title)
{
    if (!task || task->isFinished()) {
        return !task || task->getState() != Task::State::AbortedByUser;
    }
    // The task is owned by the launch pipeline; track it without taking ownership.
    QPointer<Task> guard(task);
    m_tasks->observeUnowned(task, "launch.update", title, instance->id());
    if (guard && guard->isRunning()) {
        QEventLoop loop;
        connect(task, &Task::finished, &loop, &QEventLoop::quit);
        connect(task, &QObject::destroyed, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return !guard || guard->getState() != Task::State::AbortedByUser;
}

bool InstanceApi::reauthenticate(MinecraftInstance* instance, const MinecraftAccountPtr& account, const QString& reason)
{
    recordLaunchError(instance, ApiError("ACCOUNT_NEEDS_REAUTH",
                                         tr("%1. Remove the account and sign in again on the Accounts page.").arg(reason),
                                         QJsonObject{ { "accountId", account->internalId() } }));
    return false;
}

bool InstanceApi::confirmDemo(MinecraftInstance* instance, bool hasAccount)
{
    QString text = hasAccount ? QCoreApplication::translate(
                                    "LaunchController", "This account does not own Minecraft.\nYou need to purchase the game first to play the full version.")
                              : QCoreApplication::translate("LaunchController", "No account was selected for launch.");
    text += QCoreApplication::translate("LaunchController", "\n\nDo you want to play the demo?");
    const auto answer = interaction::message(QCoreApplication::translate("LaunchController", "Play demo?"), text, "warning",
                                             { interaction::reject(QCoreApplication::translate("LaunchController", "Cancel")),
                                               interaction::accept(QCoreApplication::translate("LaunchController", "Play Demo"), "demo") },
                                             "cancel");
    if (!answer.is("demo")) {
        recordLaunchError(instance, ApiError("NO_ACCOUNT", hasAccount ? tr("This account does not own Minecraft.")
                                                                      : tr("No Minecraft account is signed in.")));
        return false;
    }
    return true;
}

std::optional<QString> InstanceApi::offlineName(MinecraftInstance* instance, const QString& suggested, const QString& reason)
{
    if (const auto requested = m_requestedOfflineName.value(instance->id()); !requested.isEmpty()) {
        return requested;
    }
    static const QRegularExpression s_validName(QStringLiteral("^[A-Za-z0-9_]{3,16}$"));
    interaction::Prompt prompt;
    prompt.kind = "text";
    prompt.title = QCoreApplication::translate("LaunchController", "Player name");
    prompt.text = reason;
    prompt.buttons = { interaction::reject(tr("Cancel")), interaction::accept(tr("OK")) };
    prompt.defaultButton = "ok";
    prompt.payload = { { "value", suggested.trimmed().isEmpty() ? QStringLiteral("Player") : suggested.trimmed() },
                       { "maxLength", 16 },
                       { "pattern", "^[A-Za-z0-9_]{3,16}$" },
                       { "patternHint", QCoreApplication::translate("ChooseOfflineNameDialog",
                                                                    "Username must be between 3 and 16 characters long and can only contain "
                                                                    "letters, numbers and underscores.") } };
    const auto answer = interaction::UserInteraction::instance()->askBlocking(prompt);
    const auto name = answer.data.value("value").toString().trimmed();
    if (!answer.is("ok") || !s_validName.match(name).hasMatch()) {
        recordLaunchError(instance, ApiError::cancelled(tr("No player name was chosen")));
        return std::nullopt;
    }
    return name;
}

bool InstanceApi::setupProfile(MinecraftInstance* instance, const MinecraftAccountPtr& account)
{
    // TODO(webui): port ProfileSetupDialog (choose a Minecraft profile name) to the web UI.
    recordLaunchError(instance, ApiError("ACCOUNT_NEEDS_PROFILE",
                                         tr("The Microsoft account %1 has no Minecraft profile yet. Create one on minecraft.net first.")
                                             .arg(account->displayName())));
    return false;
}

void InstanceApi::showError(MinecraftInstance* instance, const QString& title, const QString& message)
{
    recordLaunchError(instance, ApiError("LAUNCH_FAILED", QStringLiteral("%1: %2").arg(title, message)));
}

void InstanceApi::showConsole(MinecraftInstance* instance)
{
    m_router->emitEvent("instance.consoleRequested", QJsonObject{ { "instanceId", instance->id() } });
}

bool InstanceApi::confirmKill(MinecraftInstance*)
{
    return true;  // the web UI asks before calling instances.kill
}

void InstanceApi::profilerReady(MinecraftInstance* instance, const QString& message)
{
    // The game waits until the user confirms, like the Qt dialog did.
    const auto answer = interaction::message(
        QCoreApplication::translate("LaunchController", "Waiting."),
        QCoreApplication::translate("LaunchController",
                                    "The game launch is delayed until you press the "
                                    "button. This is the right time to setup the profiler, as the "
                                    "profiler server is running now.\n\n%1")
            .arg(message),
        "info", { interaction::accept(QCoreApplication::translate("LaunchController", "&Launch").remove('&'), "launch") }, "launch",
        QCoreApplication::translate("LaunchController", "Disable profiler on next launch"));
    if (answer.checked) {
        instance->settings()->set("Profiler", "");
    }
}

void InstanceApi::launchFinished(MinecraftInstance* instance, bool success, bool aborted, const QString& reason)
{
    const auto id = instance->id();
    const auto previous = m_states.take(id);
    const auto error = m_launchErrors.take(id);
    const bool killed = m_killRequested.remove(id);
    aborted = aborted || killed;
    m_requestedOfflineName.remove(id);
    if (previous == QLatin1String("running")) {
        const QJsonObject payload{ { "instanceId", id }, { "success", success }, { "reason", killed ? tr("Stopped by the user") : reason } };
        m_router->emitEvent("instance.stopped", payload);
        if (!success && !aborted) {
            m_router->emitEvent("minecraft.crashed", payload);
        }
    } else if (!success) {
        ApiError failure = error;
        if (failure.code.isEmpty()) {
            failure = aborted ? ApiError::cancelled(tr("The launch was cancelled"))
                              : ApiError("LAUNCH_FAILED", reason.isEmpty() ? tr("The launch failed") : reason);
        }
        m_router->emitEvent("instance.launchFailed", QJsonObject{ { "instanceId", id }, { "error", failure.toJson() } });
    } else {
        m_router->emitEvent("instance.stopped", QJsonObject{ { "instanceId", id }, { "success", true }, { "reason", reason } });
    }
    scheduleChanged();
}

}  // namespace api
