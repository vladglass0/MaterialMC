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

#pragma once

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

class MinecraftInstance;

namespace api {

class ApiRouter;
class TaskTracker;

/**
 * `components.*`: the component editor (formerly the Qt VersionPage and InstallLoaderDialog).
 * Every change goes through PackProfile; resolving runs as a tracked task. Emits `components.changed { instanceId }`.
 */
class ComponentApi : public QObject {
    Q_OBJECT
   public:
    ComponentApi(ApiRouter* router, TaskTracker* tasks, QObject* parent = nullptr);

   private:
    void watch(MinecraftInstance* instance);
    void resolve(MinecraftInstance* instance);

    ApiRouter* m_router;
    TaskTracker* m_tasks;
    QSet<QString> m_watched;
    QSet<QString> m_pendingChanged;
    QTimer m_changedTimer;
};

}  // namespace api
