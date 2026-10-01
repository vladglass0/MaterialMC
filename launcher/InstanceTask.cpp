#include "InstanceTask.h"
#include <QApplication>
#include <QDir>

#include "Application.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/MinecraftLoadAndCheck.h"
#include "minecraft/PackProfile.h"
#include "settings/SettingsObject.h"
#include "tasks/SequentialTask.h"
#include "interaction/UserInteraction.h"

InstanceNameChange askForChangingInstanceName(QWidget* /*parent*/, const QString& oldName, const QString& newName)
{
    const bool change =
        interaction::confirm(QObject::tr("Change instance name"),
                             QObject::tr("The instance's name seems to include the old version. Would you like to update it?\n\n"
                                         "Old name: %1\n"
                                         "New name: %2")
                                 .arg(oldName, newName));
    return change ? InstanceNameChange::ShouldChange : InstanceNameChange::ShouldKeep;
}

ShouldUpdate askIfShouldUpdate(QWidget* /*parent*/, const QString& originalVersionName)
{
    if (APPLICATION->settings()->get("SkipModpackUpdatePrompt").toBool()) {
        return ShouldUpdate::SkipUpdating;
    }

    const auto answer = interaction::message(
        QObject::tr("Similar modpack was found!"),
        QObject::tr(
            "One or more of your instances are from this same modpack%1. Do you want to create a "
            "separate instance, or update the existing one?\n\nNOTE: Make sure you made a backup of your important instance data before "
            "updating, as worlds can be corrupted and some configuration may be lost (due to pack overrides).")
            .arg(originalVersionName),
        "info",
        { interaction::reject(QObject::tr("Cancel")), interaction::neutral(QObject::tr("Create new instance"), "skip"),
          interaction::accept(QObject::tr("Update existing instance"), "update") });

    if (answer.is("update")) {
        return ShouldUpdate::Update;
    }
    if (answer.is("skip")) {
        return ShouldUpdate::SkipUpdating;
    }
    return ShouldUpdate::Cancel;
}

QString InstanceTask::name() const
{
    if (!m_modifiedName.isEmpty()) {
        return modifiedName();
    }
    if (!m_originalVersion.isEmpty()) {
        return QString("%1 %2").arg(m_originalName, m_originalVersion);
    }

    return m_originalName;
}

QString InstanceTask::originalName() const
{
    return m_originalName;
}

QString InstanceTask::modifiedName() const
{
    if (!m_modifiedName.isEmpty()) {
        return m_modifiedName;
    }
    return m_originalName;
}

QString InstanceTask::version() const
{
    return m_originalVersion;
}

void InstanceTask::setOriginalName(const QString& name, const QString& version)
{
    m_originalName = name;
    m_originalVersion = version;
}
void InstanceTask::setOverride(bool override, const QString& instanceIdToOverride)
{
    m_overrideExisting = override;
    if (!instanceIdToOverride.isEmpty()) {
        m_originalInstanceId = instanceIdToOverride;
    }
}

ShouldDeleteSaves askIfShouldDeleteSaves(QWidget* /*parent*/)
{
    const bool remove = interaction::confirm(QObject::tr("Delete Existing Save Files"),
                                             QObject::tr("An earlier version of this mod pack installed save files.\n"
                                                         "Would you like to remove those existing saves as part of this update?"));
    return remove ? ShouldDeleteSaves::Yes : ShouldDeleteSaves::No;
}

void InstanceTask::scheduleToDelete(QWidget* parent, const QDir& dir, const QString& path, bool checkDisabled)
{
    if (path.isEmpty()) {
        return;
    }
    if (path.startsWith("saves/")) {
        if (m_shouldDeleteSaves == ShouldDeleteSaves::NotAsked) {
            m_shouldDeleteSaves = askIfShouldDeleteSaves(parent);
        }
        if (m_shouldDeleteSaves == ShouldDeleteSaves::No) {
            return;
        }
    }
    qDebug() << "Scheduling" << path << "for removal";
    m_filesToRemove.append(dir.absoluteFilePath(path));
    if (checkDisabled) {
        if (path.endsWith(".disabled")) {  // remove it if it was enabled/disabled by user
            m_filesToRemove.append(dir.absoluteFilePath(path.chopped(9)));
        } else {
            m_filesToRemove.append(dir.absoluteFilePath(path + ".disabled"));
        }
    }
}

void InstanceTask::downloadFiles(MinecraftInstance* inst)
{
    auto finishCreation = [this, inst] {
        // Success commits the staging directory synchronously, before the instance is destroyed.
        auto* profile = inst->getPackProfile();
        if (!profile->saveNow()) {
            emitFailed(tr("Could not save the instance's component list."));
            return;
        }
        emitSucceeded();
    };
    if (!APPLICATION->settings()->get("DownloadGameFilesDuringInstanceCreation").toBool()) {
        finishCreation();
        return;
    }
    setAbortable(true);
    setAbortButtonText(tr("Skip"));
    qDebug() << "Downloading game files";

    auto updateTasks = inst->createUpdateTask();
    if (updateTasks.isEmpty()) {
        finishCreation();
        return;
    }
    auto task = makeShared<SequentialTask>();
    task->addTask(makeShared<MinecraftLoadAndCheck>(inst, Net::Mode::Online));
    for (const auto& t : updateTasks) {
        task->addTask(t);
    }
    connect(task.get(), &Task::finished, this, [this, task, finishCreation] {
        if (!isRunning()) {
            return;
        }
        if (!task->wasSuccessful()) {
            interaction::notify(tr("Error"), tr("Could not download game files: %1").arg(task->failReason()), "warning");
        }
        finishCreation();
    });
    propagateFromOther(task.get());
    setDetails(tr("Downloading game files"));

    m_gameFilesTask = task;
    m_gameFilesTask->start();
}

bool InstanceTask::abort()
{
    if (!canAbort()) {
        return false;
    }

    if (m_gameFilesTask) {
        return m_gameFilesTask->abort();
    }

    return Task::abort();
}
