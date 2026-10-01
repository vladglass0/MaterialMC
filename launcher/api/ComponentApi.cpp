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

#include "ComponentApi.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>

#include "Application.h"
#include "minecraft/Component.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/auth/AccountList.h"
#include "interaction/UserInteraction.h"
#include "meta/Index.h"
#include "meta/Version.h"
#include "meta/VersionList.h"
#include "settings/SettingsObject.h"
#include "tasks/SequentialTask.h"

#include "ApiRouter.h"
#include "ApiUtils.h"
#include "TaskTracker.h"
#include "VersionApi.h"

namespace api {

namespace {

QString trVersionPage(const char* text)
{
    return QCoreApplication::translate("VersionPage", text);
}

QString severityName(ProblemSeverity severity)
{
    switch (severity) {
        case ProblemSeverity::Warning:
            return "warning";
        case ProblemSeverity::Error:
            return "error";
        case ProblemSeverity::None:
            break;
    }
    return "none";
}

PackProfile* requireProfile(MinecraftInstance* instance, bool forWriting)
{
    auto* profile = loadedPackProfile(instance);
    if (!profile) {
        throw ApiError::internal(QObject::tr("The instance has no component list"));
    }
    if (forWriting && instance->isRunning()) {
        throw ApiError("INSTANCE_RUNNING", QObject::tr("Stop %1 before changing its components").arg(instance->name()));
    }
    return profile;
}

int requireIndex(PackProfile* profile, const QJsonObject& p)
{
    const auto uid = params::requireNonEmpty(p, "uid", 256);
    for (int i = 0; i < profile->rowCount(); i++) {
        if (profile->getComponent(static_cast<size_t>(i))->getID() == uid) {
            return i;
        }
    }
    throw ApiError::notFound(QObject::tr("Component '%1' does not exist").arg(uid));
}

ComponentPtr requireComponent(PackProfile* profile, const QJsonObject& p)
{
    return profile->getComponent(static_cast<size_t>(requireIndex(profile, p)));
}

QString requiredMinecraft(const Meta::Version::Ptr& v)
{
    const auto& reqs = v->requiredSet();
    const auto mc = std::find_if(reqs.begin(), reqs.end(), [](const Meta::Require& r) { return r.uid == "net.minecraft"; });
    return mc != reqs.end() ? mc->equalsVersion : QString();
}

/** Picks files in a native dialog (the page never supplies paths). */
QStringList pickFiles(const QString& title, const QString& filter, bool multiple)
{
    const auto dir = APPLICATION->settings()->get("CentralModsDir").toString();
    if (multiple) {
        return QFileDialog::getOpenFileNames(nullptr, title, dir, filter);
    }
    const auto file = QFileDialog::getOpenFileName(nullptr, title, dir, filter);
    return file.isEmpty() ? QStringList() : QStringList{ file };
}

}  // namespace

ComponentApi::ComponentApi(ApiRouter* router, TaskTracker* tasks, QObject* parent) : QObject(parent), m_router(router), m_tasks(tasks)
{
    m_changedTimer.setSingleShot(true);
    m_changedTimer.setInterval(150);
    connect(&m_changedTimer, &QTimer::timeout, this, [this] {
        for (const auto& id : std::as_const(m_pendingChanged)) {
            m_router->emitEvent("components.changed", QJsonObject{ { "instanceId", id } });
        }
        m_pendingChanged.clear();
    });

    router->addSync("components.list", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, false);
        watch(instance);
        QJsonArray out;
        for (int i = 0; i < profile->rowCount(); i++) {
            auto c = profile->getComponent(static_cast<size_t>(i));
            QJsonArray problems;
            for (const auto& problem : c->getProblems()) {
                problems.append(QJsonObject{ { "severity", severityName(problem.m_severity) }, { "description", problem.m_description } });
            }
            out.append(QJsonObject{
                { "uid", c->getID() },
                { "name", c->getName() },
                { "version", c->getVersion() },
                { "enabled", c->isEnabled() },
                { "canBeDisabled", c->canBeDisabled() },
                { "moveable", c->isMoveable() },
                { "customizable", c->isCustomizable() },
                { "revertible", c->isRevertible() },
                { "removable", c->isRemovable() },
                { "custom", c->isCustom() },
                { "versionChangeable", c->isVersionChangeable(false) },
                { "knownModloader", c->isKnownModloader() },
                { "severity", severityName(c->getProblemSeverity()) },
                { "problems", problems },
            });
        }
        const auto task = profile->getCurrentTask();
        return QJsonObject{ { "components", out }, { "resolving", task && task->isRunning() }, { "running", instance->isRunning() } };
    });

    // Versions available for a component, filtered like the Qt version dialog (exact Minecraft match when present).
    router->add("components.versions", [this](const QJsonObject& p, const ApiReply& reply) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, false);
        const auto uid = params::requireNonEmpty(p, "uid", 256);
        auto list = APPLICATION->metadataIndex()->get(uid);
        if (!list) {
            throw ApiError::notFound(QObject::tr("No version list for '%1'").arg(uid));
        }
        const auto mcVersion = profile->getComponentVersion("net.minecraft");
        QString suggested;
        if (uid == "org.lwjgl" || uid == "org.lwjgl3") {
            // recommend the LWJGL version the current Minecraft version asks for
            if (auto minecraft = profile->getComponent("net.minecraft")) {
                for (const auto& req : minecraft->m_cachedRequires) {
                    if (req.uid == uid) {
                        suggested = !req.equalsVersion.isEmpty() ? req.equalsVersion : req.suggests;
                    }
                }
            }
        }
        const auto current = profile->getComponentVersion(uid);
        withLoadedList(list.get(), params::optionalBool(p, "forceReload", false), m_tasks, this,
                       QObject::tr("Loading %1 versions").arg(list->humanReadable()), reply, [list, mcVersion, suggested, current, uid] {
                           QJsonArray versions;
                           bool anyExact = false;
                           for (const auto& v : list->versions()) {
                               if (requiredMinecraft(v) == mcVersion) {
                                   anyExact = true;
                               }
                           }
                           for (const auto& v : list->versions()) {
                               auto obj = serializeMetaVersion(v);
                               const auto parent = requiredMinecraft(v);
                               obj.insert("minecraft", parent.isEmpty() ? QJsonValue() : QJsonValue(parent));
                               // Same as setExactIfPresentFilter(ParentVersionRole, mc): only filter when something matches.
                               obj.insert("matchesMinecraft", !anyExact || parent == mcVersion);
                               obj.insert("suggested", !suggested.isEmpty() && v->version() == suggested);
                               versions.append(obj);
                           }
                           return QJsonObject{ { "uid", uid },
                                               { "name", list->humanReadable() },
                                               { "current", current.isEmpty() ? QJsonValue() : QJsonValue(current) },
                                               { "minecraftVersion", mcVersion },
                                               { "versions", versions } };
                       });
    });

    router->addSync("components.setVersion", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, true);
        auto component = requireComponent(profile, p);
        const auto version = params::requireNonEmpty(p, "version", 256);
        const auto uid = component->getID();
        if (!component->isVersionChangeable(false)) {
            throw ApiError::unsupported(QObject::tr("The version of %1 cannot be changed").arg(component->getName()));
        }
        bool important = false;
        if (uid == "net.minecraft") {
            important = true;
            // Same rules as VersionPage::on_actionChange_version_triggered
            auto* s = instance->settings();
            if (APPLICATION->settings()->get("AutomaticJavaSwitch").toBool() && s->get("AutomaticJava").toBool() &&
                s->get("OverrideJavaLocation").toBool()) {
                s->set("OverrideJavaLocation", false);
                s->set("JavaPath", "");
            }
            if (s->get("UseLatestMinecraftVersion").toBool()) {
                s->set("UseLatestMinecraftVersion", false);
            }
        }
        profile->setComponentVersion(uid, version, important);
        resolve(instance);
        return ok();
    });

    // May ask what to do with conflicting loaders, hence deferred.
    router->addDeferred("components.installLoader", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, true);
        const auto uid = params::requireNonEmpty(p, "uid", 256);
        const auto version = params::requireNonEmpty(p, "version", 256);
        const auto known = Component::KNOWN_MODLOADERS.find(uid);
        if (known == Component::KNOWN_MODLOADERS.cend()) {
            throw ApiError::invalidParams(QObject::tr("'%1' is not a mod loader").arg(uid));
        }
        const auto tr = [](const char* text) { return QCoreApplication::translate("InstallLoaderDialog", text); };
        auto list = APPLICATION->metadataIndex()->get(uid);
        const QString targetVersion = QString("%1 %2").arg(list ? list->humanReadable() : uid, version);
        for (const QString& conflictId : known->knownConflictingComponents) {
            const ComponentPtr conflict = profile->getComponent(conflictId);
            if (!conflict || !conflict->isEnabled() || conflict->isCustom()) {
                continue;
            }
            const QString conflictVersion = QString("%1 %2").arg(conflict->getName(), conflict->getVersion());
            QList<interaction::Button> buttons{ interaction::reject(tr("Cancel")), interaction::accept(tr("Keep it"), "keep") };
            if (conflict->canBeDisabled()) {
                buttons.append(interaction::neutral(tr("Disable it"), "disable"));
            }
            if (conflict->isRemovable()) {
                buttons.append(interaction::destructive(tr("Uninstall it"), "uninstall"));
            }
            const auto answer = interaction::message(
                tr("Installing a second loader"),
                tr("%1 is known to conflict with %2, which is enabled on this instance. Having both enabled at the same time will "
                   "likely break the instance.\n\nWhat would you like to do with %2?")
                    .arg(targetVersion, conflictVersion),
                "warning", buttons, "cancel");
            if (answer.is("keep")) {
                continue;
            }
            if (answer.is("disable")) {
                conflict->setEnabled(false);
                continue;
            }
            if (answer.is("uninstall")) {
                profile->remove(conflict->getID());
                continue;
            }
            throw ApiError::cancelled();
        }
        if (const ComponentPtr component = profile->getComponent(uid); component && !component->isEnabled()) {
            component->setEnabled(true);
        }
        profile->setComponentVersion(uid, version);
        if (auto component = profile->getComponent(uid)) {
            component->setEnabled(true);
        }
        resolve(instance);
        return ok();
    });

    router->addSync("components.setEnabled", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, true);
        auto component = requireComponent(profile, p);
        if (!component->canBeDisabled()) {
            throw ApiError::unsupported(QObject::tr("%1 cannot be disabled").arg(component->getName()));
        }
        component->setEnabled(params::requireBool(p, "enabled"));
        resolve(instance);
        return ok();
    });

    router->addDeferred("components.remove", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, true);
        const int index = requireIndex(profile, p);
        auto component = profile->getComponent(static_cast<size_t>(index));
        if (!component->isRemovable()) {
            throw ApiError::unsupported(QObject::tr("%1 cannot be removed").arg(component->getName()));
        }
        if (component->isCustom() &&
            !interaction::confirm(trVersionPage("Confirm Removal"),
                                  trVersionPage("You are about to remove \"%1\".\n"
                                                "This is permanent and will completely remove the custom component.\n\n"
                                                "Are you sure?")
                                      .arg(component->getName()),
                                  "warning")) {
            throw ApiError::cancelled();
        }
        if (!profile->remove(index)) {
            throw ApiError::io(trVersionPage("Couldn't remove file"));
        }
        if (auto res = profile->reload(Net::Mode::Online); !res) {
            throw ApiError::io(res.error());
        }
        resolve(instance);
        return ok();
    });

    router->addSync("components.move", [](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, true);
        const int index = requireIndex(profile, p);
        static constexpr std::pair<const char*, PackProfile::MoveDirection> Directions[] = { { "up", PackProfile::MoveUp },
                                                                                             { "down", PackProfile::MoveDown } };
        if (!profile->getComponent(static_cast<size_t>(index))->isMoveable()) {
            throw ApiError::unsupported(QObject::tr("This component cannot be moved"));
        }
        profile->move(index, params::requireEnum(p, "direction", Directions));
        return ok();
    });

    router->addSync("components.customize", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, true);
        const int index = requireIndex(profile, p);
        auto component = profile->getComponent(static_cast<size_t>(index));
        if (!component->isCustomizable() || !component->getVersionFile()) {
            throw ApiError::unsupported(QObject::tr("%1 cannot be customized right now").arg(component->getName()));
        }
        if (!profile->customize(index)) {
            throw ApiError::io(QObject::tr("Could not customize %1").arg(component->getName()));
        }
        resolve(instance);
        return ok();
    });

    router->addDeferred("components.revert", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, true);
        const int index = requireIndex(profile, p);
        auto component = profile->getComponent(static_cast<size_t>(index));
        if (!component->isRevertible()) {
            throw ApiError::unsupported(QObject::tr("%1 cannot be reverted").arg(component->getName()));
        }
        if (!interaction::confirm(trVersionPage("Confirm Reversion"),
                                  trVersionPage("You are about to revert \"%1\".\n"
                                                "This is permanent and will completely revert your customizations.\n\n"
                                                "Are you sure?")
                                      .arg(component->getName()),
                                  "warning")) {
            throw ApiError::cancelled();
        }
        if (!profile->revertToBase(index)) {
            throw ApiError::io(QObject::tr("Could not revert %1").arg(component->getName()));
        }
        resolve(instance);
        return ok();
    });

    router->addSync("components.addEmpty", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, true);
        const auto uid = params::requireNonEmpty(p, "uid", 256);
        const auto name = params::requireNonEmpty(p, "name", 256);
        static const QRegularExpression s_uid(QStringLiteral("^[a-zA-Z0-9-]+(\\.[a-zA-Z0-9-]+)+$"));
        if (!s_uid.match(uid).hasMatch()) {
            throw ApiError::invalidParams(QObject::tr("'%1' is not a valid component uid (e.g. org.example.component)").arg(uid));
        }
        if (profile->getComponent(uid)) {
            throw ApiError::invalidParams(QObject::tr("A component with the uid '%1' already exists").arg(uid));
        }
        if (!profile->installEmpty(uid, name)) {
            throw ApiError::io(QObject::tr("Could not create the component"));
        }
        resolve(instance);
        return ok();
    });

    // Native file pickers: Add to Minecraft.jar, Replace Minecraft.jar, Add agents, Import components.
    router->addDeferred("components.addFiles", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, true);
        const auto kind = params::requireNonEmpty(p, "kind", 32);
        QStringList files;
        if (kind == QLatin1String("jarMods")) {
            files = pickFiles(trVersionPage("Select jar mods"), trVersionPage("Minecraft.jar mods") + " (*.zip *.jar)", true);
            if (!files.isEmpty()) {
                profile->installJarMods(files);
            }
        } else if (kind == QLatin1String("customJar")) {
            files = pickFiles(trVersionPage("Select jar"), trVersionPage("Minecraft.jar replacement") + " (*.jar)", false);
            if (!files.isEmpty()) {
                profile->installCustomJar(files.first());
            }
        } else if (kind == QLatin1String("agents")) {
            files = pickFiles(trVersionPage("Select agents"), trVersionPage("Java agents") + " (*.jar)", true);
            if (!files.isEmpty()) {
                profile->installAgents(files);
            }
        } else if (kind == QLatin1String("components")) {
            files = pickFiles(trVersionPage("Select components"), trVersionPage("Components") + " (*.json)", true);
            if (!files.isEmpty() && !profile->installComponents(files)) {
                throw ApiError::io(trVersionPage("Some components could not be imported. Check logs for details"));
            }
        } else {
            throw ApiError::invalidParams(QObject::tr("Unknown kind '%1'").arg(kind));
        }
        if (!files.isEmpty()) {
            resolve(instance);
        }
        return QJsonObject{ { "added", static_cast<int>(files.size()) } };
    });

    router->addSync("components.edit", [](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, true);
        auto component = requireComponent(profile, p);
        if (!component->isCustom()) {
            throw ApiError::unsupported(QObject::tr("Only custom components can be edited"));
        }
        const auto filename = component->getFilename();
        if (!QFileInfo::exists(filename) || !APPLICATION->openJsonEditor(filename)) {
            throw ApiError::io(QObject::tr("Could not open %1 in an editor").arg(filename));
        }
        return ok();
    });

    router->addSync("components.reload", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        auto* profile = requireProfile(instance, true);
        if (auto res = profile->reload(Net::Mode::Online); !res) {
            throw ApiError::io(res.error());
        }
        m_pendingChanged.insert(instance->id());
        m_changedTimer.start();
        return ok();
    });

    router->addSync("components.downloadAll", [this](const QJsonObject& p) {
        auto* instance = requireInstance(p);
        requireProfile(instance, true);
        if (!APPLICATION->accounts()->anyAccountIsValid()) {
            throw ApiError("NO_ACCOUNT", trVersionPage("Cannot download Minecraft or update instances unless you have at least "
                                                       "one account added.\nPlease add a Microsoft account."));
        }
        auto updateTasks = instance->createUpdateTask();
        if (updateTasks.isEmpty()) {
            return QJsonValue(QJsonObject{ { "taskId", QJsonValue() } });
        }
        auto task = makeShared<SequentialTask>();
        for (const auto& t : updateTasks) {
            task->addTask(t);
        }
        return QJsonValue(QJsonObject{
            { "taskId", m_tasks->start(task, "instance.update", QObject::tr("Downloading files for %1").arg(instance->name()), instance->id()) } });
    });
}

void ComponentApi::watch(MinecraftInstance* instance)
{
    const auto id = instance->id();
    if (m_watched.contains(id)) {
        return;
    }
    m_watched.insert(id);
    auto* profile = instance->getPackProfile();
    QPointer<MinecraftInstance> guard(instance);
    const auto changed = [this, id] {
        m_pendingChanged.insert(id);
        m_changedTimer.start();
    };
    connect(profile, &QAbstractItemModel::dataChanged, this, changed);
    connect(profile, &QAbstractItemModel::rowsInserted, this, changed);
    connect(profile, &QAbstractItemModel::rowsRemoved, this, changed);
    connect(profile, &QAbstractItemModel::rowsMoved, this, changed);
    connect(profile, &QAbstractItemModel::modelReset, this, changed);
    connect(instance, &QObject::destroyed, this, [this, id] { m_watched.remove(id); });
}

void ComponentApi::resolve(MinecraftInstance* instance)
{
    auto* profile = instance->getPackProfile();
    profile->resolve(Net::Mode::Online);
    if (auto task = profile->getCurrentTask(); task && !task->isFinished()) {
        m_tasks->observe(task, "components.resolve", QObject::tr("Updating components of %1").arg(instance->name()), instance->id());
        connect(task.get(), &Task::finished, this, [this, id = instance->id()] {
            m_pendingChanged.insert(id);
            m_changedTimer.start();
        });
    }
    m_pendingChanged.insert(instance->id());
    m_changedTimer.start();
}

}  // namespace api
