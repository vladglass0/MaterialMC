// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QVector>

#include "modplatform/ModIndex.h"
#include "modplatform/import_ftb/PackHelpers.h"
#include <QElapsedTimer>
#include "modplatform/legacy_ftb/PackFetchTask.h"

#include <functional>
#include "tasks/Task.h"

namespace api {

class ApiRouter;
class ApiReply;
class TaskTracker;

class ModpackApi : public QObject {
    Q_OBJECT
   public:
    ModpackApi(ApiRouter* router, TaskTracker* tasks, QObject* parent = nullptr);

   private:
    void registerMethods();
    void runRequest(const Task::Ptr& task, const ApiReply& reply);
    void managedVersions(const QJsonObject& params, const ApiReply& reply);
    QJsonObject updateManagedPack(const QJsonObject& params);
    void discoverFtbApp();
    QJsonObject searchFtbApp(const QJsonObject& params);
    QJsonArray ftbAppVersions(const QString& projectId);
    QJsonObject installFtbApp(const QJsonObject& params);
    void searchLegacy(const QJsonObject& params, const ApiReply& reply);
    QJsonArray legacyVersions(const QString& projectId);
    QJsonObject installLegacy(const QJsonObject& params);
    void finishLegacy(const LegacyFTB::ModpackList* packs, const QString& error = {}, bool cancelled = false);
    QJsonObject startImport(const QUrl& url,
                            bool trusted,
                            const QString& name,
                            const QString& group,
                            const QString& originalName,
                            const QString& originalVersion = {},
                            const QMap<QString, QString>& extraInfo = {},
                            const QString& icon = QStringLiteral("default"),
                            bool confirmUpdate = true);
    static QString cacheKey(ModPlatform::ResourceProvider provider, const QString& projectId);

    ApiRouter* m_router;
    TaskTracker* m_tasks;
    QHash<QString, ModPlatform::IndexedPack::Ptr> m_packs;
    QHash<QString, QVector<ModPlatform::IndexedVersion>> m_versions;
    QSet<Task::Ptr> m_requests;
    QHash<QString, LegacyFTB::Modpack> m_legacyPacks;
    QSet<QString> m_legacyVersionsListed;
    std::shared_ptr<LegacyFTB::PackFetchTask> m_legacyFetch;
    QList<std::function<void(const QString&, bool)>> m_legacyReplies;
    bool m_legacyLoaded = false;
    QHash<QString, FTBImportAPP::Modpack> m_ftbAppPacks;
    QHash<QString, QString> m_ftbAppVersionsListed;
    QElapsedTimer m_ftbAppScanAge;
    QString m_ftbAppConfiguredPath;
};

}  // namespace api
