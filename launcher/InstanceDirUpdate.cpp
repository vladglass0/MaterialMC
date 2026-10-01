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
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "InstanceDirUpdate.h"


#include <QCoreApplication>

#include "Application.h"
#include "BuildConfig.h"
#include "FileSystem.h"

#include "InstanceList.h"
#include "interaction/UserInteraction.h"

QString askToUpdateInstanceDirName(BaseInstance* instance, const QString& oldName, const QString& newName)
{
    if (oldName == newName)
        return QString();

    QString renamingMode = APPLICATION->settings()->get("InstRenamingMode").toString();
    if (renamingMode == "MetadataOnly")
        return QString();

    auto oldRoot = instance->instanceRoot();
    auto newDirName = FS::DirNameFromString(newName, APPLICATION->instances()->instanceDirs());
    auto newRoot = FS::PathCombine(QFileInfo(oldRoot).dir().absolutePath(), newDirName);
    if (oldRoot == newRoot)
        return QString();
    if (oldRoot == FS::PathCombine(QFileInfo(oldRoot).dir().absolutePath(), newName))
        return QString();

    // Check for conflict
    if (QDir(newRoot).exists()) {
        interaction::notify(QObject::tr("Cannot rename instance"),
                            QObject::tr("New instance root (%1) already exists. <br />Only the metadata will be renamed.").arg(newRoot),
                            "warning");
        return QString();
    }

    if (instance->isRunning()) {
        interaction::notify(QObject::tr("Cannot rename instance folder"),
                            QObject::tr("The instance folder cannot be renamed while the instance is running.\n\n"
                                        "Only the instance name will be changed. The folder will keep its current name."),
                            "warning");
        return QString();
    }

    // Ask if we should rename
    if (renamingMode == "AskEverytime") {
        auto res = interaction::message(QObject::tr("Rename instance folder"),
                                        QObject::tr("Would you also like to rename the instance folder?\n\n"
                                                    "Old name: %1\n"
                                                    "New name: %2")
                                            .arg(oldName, newName),
                                        "question",
                                        { interaction::reject(QObject::tr("No"), "no"), interaction::accept(QObject::tr("Yes"), "yes") }, {},
                                        QObject::tr("&Remember my choice").remove('&'));
        if (res.checked) {
            if (res.is("yes"))
                APPLICATION->settings()->set("InstRenamingMode", "PhysicalDir");
            else
                APPLICATION->settings()->set("InstRenamingMode", "MetadataOnly");
        }
        if (!res.is("yes"))
            return QString();
    }

    // Check for linked instances
    if (!checkLinkedInstances(instance->id(), QObject::tr("Renaming")))
        return QString();

    // Now we can confirm that a renaming is happening
    if (!instance->syncInstanceDirName(newRoot)) {
        interaction::notify(QObject::tr("Cannot rename instance"),
                            QObject::tr("An error occurred when performing the following renaming operation: <br/>"
                                        " - Old instance root: %1<br/>"
                                        " - New instance root: %2<br/>"
                                        "Only the metadata is renamed.")
                                .arg(oldRoot, newRoot),
                            "warning");
        return QString();
    }
    return newRoot;
}

bool checkLinkedInstances(const QString& id, const QString& verb)
{
    auto linkedInstances = APPLICATION->instances()->getLinkedInstancesById(id);
    if (!linkedInstances.empty()) {
        return interaction::confirm(QObject::tr("There are linked instances"),
                                    QObject::tr("The following instance(s) might reference files in this instance:\n\n"
                                                                 "%1\n\n"
                                                                 "%2 it could break the other instance(s), \n\n"
                                                                 "Do you wish to proceed?",
                                                                 nullptr, linkedInstances.count())
                                                         .arg(linkedInstances.join("\n"))
                                        .arg(verb),
                                    "warning");
    }
    return true;
}

void checkInstancePathForProblems()
{
    const auto tr = [](const char* text) { return QCoreApplication::translate("MainWindow", text); };
    QString instanceFolder = APPLICATION->settings()->get("InstanceDir").toString();
    if (FS::checkProblemticPathJava(QDir(instanceFolder))) {
        interaction::notify(BuildConfig.LAUNCHER_DISPLAYNAME,
                            tr("Your instance folder contains \'!\' and this is known to cause Java problems!") + "<br/><br/>" +
                                tr("You have now two options: <br/>"
                                   " - change the instance folder in the settings <br/>"
                                   " - move this installation of %1 to a different folder")
                                    .arg(BuildConfig.LAUNCHER_DISPLAYNAME),
                            "warning");
    }
    auto tempFolderText = tr(
        "This is a problem: <br/>"
        " - The launcher will likely be deleted without warning by the operating system <br/>"
        " - close the launcher now and extract it to a real location, not a temporary folder");
    QString pathfoldername = QDir(instanceFolder).absolutePath();
    if (pathfoldername.contains("Rar$", Qt::CaseInsensitive)) {
        interaction::notify(BuildConfig.LAUNCHER_DISPLAYNAME,
                            tr("Your instance folder contains \'Rar$\' - that means you haven't extracted the launcher archive!") +
                                "<br/><br/>" + tempFolderText,
                            "warning");
    } else if (pathfoldername.startsWith(QDir::tempPath()) || pathfoldername.contains("/TempState/")) {
        interaction::notify(BuildConfig.LAUNCHER_DISPLAYNAME,
                            tr("Your instance folder is in a temporary folder: \'%1\'!").arg(QDir::tempPath()) + "<br/><br/>" + tempFolderText,
                            "warning");
    }
}
