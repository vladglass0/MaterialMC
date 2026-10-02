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

#include <QJsonObject>
#include <QJsonValue>

#include <functional>
#include <memory>

class QObject;
class BaseVersionList;
namespace Meta {
class Version;
}

namespace api {

class ApiReply;
class ApiRouter;
class TaskTracker;

/**
 * Makes sure `list` is loaded (from the cache or the network), then resolves `reply` with `then()`.
 * Loading runs as a tracked task so it shows up in the downloads view.
 */
void withLoadedList(BaseVersionList* list,
                    bool forceReload,
                    TaskTracker* tasks,
                    QObject* context,
                    const QString& title,
                    const ApiReply& reply,
                    std::function<QJsonValue()> then);

/** { version, type, releaseTime, recommended } of a meta version. */
QJsonObject serializeMetaVersion(const std::shared_ptr<Meta::Version>& version);

/** `versions.minecraft`, `versions.loaders` (from the meta server index) and `java.list` (detected installations). */
void registerVersionApi(ApiRouter* router, TaskTracker* tasks, QObject* context);

}  // namespace api
