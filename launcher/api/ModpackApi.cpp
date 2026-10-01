// SPDX-License-Identifier: GPL-3.0-only
#include "ModpackApi.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirListing>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QUuid>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>

#include "Application.h"
#include "BuildConfig.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "Version.h"
#include "icons/IconList.h"
#include "modplatform/ResourceAPI.h"
#include "modplatform/flame/FlameAPI.h"
#include "modplatform/import_ftb/PackInstallTask.h"
#include "modplatform/legacy_ftb/PackInstallTask.h"
#include "modplatform/modrinth/ModrinthAPI.h"
#include "net/HttpMetaCache.h"
#include "settings/SettingsObject.h"

#include "ApiParams.h"
#include "ApiRouter.h"
#include "TaskTracker.h"

namespace api {
namespace {

constexpr std::pair<const char*, ModPlatform::ResourceProvider> Providers[] = {
    { "modrinth", ModPlatform::ResourceProvider::MODRINTH },
    { "curseforge", ModPlatform::ResourceProvider::FLAME },
};
constexpr qsizetype MaxCachedProjects = 512;
constexpr qsizetype MaxPendingRequests = 16;

const char* providerName(ModPlatform::ResourceProvider provider)
{
    return provider == ModPlatform::ResourceProvider::FLAME ? "curseforge" : "modrinth";
}

const ResourceAPI& apiFor(ModPlatform::ResourceProvider provider)
{
    if (provider == ModPlatform::ResourceProvider::FLAME) {
        if (!APPLICATION->capabilities().testFlag(Application::SupportsFlame)) {
            throw ApiError::unsupported(QObject::tr("This build has no CurseForge API key"));
        }
        return FlameAPI::get();
    }
    return ModrinthAPI::get();
}

QUrl remoteUrl(const QString& raw)
{
    const QUrl url(raw, QUrl::StrictMode);
    if (!url.isValid() || url.isRelative() || url.host().isEmpty() ||
        (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")) || !url.userInfo().isEmpty()) {
        throw ApiError::invalidParams(QObject::tr("A valid HTTP or HTTPS URL with a host and no credentials is required"));
    }
    return url;
}

QJsonObject serializePack(const ModPlatform::IndexedPack& pack)
{
    QJsonArray authors;
    for (const auto& author : pack.authors) {
        authors.append(author.name);
    }
    return {
        { "provider", providerName(pack.provider) },
        { "id", pack.addonId.toString() },
        { "slug", pack.slug },
        { "name", pack.name },
        { "description", pack.description },
        { "authors", authors },
        { "iconUrl", pack.logoUrl.startsWith("https://") ? pack.logoUrl : QString() },
        { "websiteUrl", pack.websiteUrl },
    };
}

QJsonObject serializeVersion(const ModPlatform::IndexedVersion& version)
{
    QJsonArray loaders;
    for (auto loader : ModPlatform::modLoaderTypesToList(version.loaders)) {
        loaders.append(ModPlatform::getModLoaderAsString(loader));
    }
    return {
        { "id", version.fileId.toString() },
        { "name", version.version },
        { "versionNumber", version.versionNumber },
        { "type", version.versionType.isValid() ? version.versionType.toString() : QString() },
        { "gameVersions", QJsonArray::fromStringList(version.mcVersion) },
        { "loaders", loaders },
        { "date", version.date },
        { "fileName", version.fileName },
        { "compatible", true },
    };
}

bool legacyPathPart(const QString& value)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*$"));
    return !value.isEmpty() && value.size() <= 100 && !value.contains("..") && valid.match(value).hasMatch();
}

QString legacyId(const LegacyFTB::Modpack& pack)
{
    return (pack.type == LegacyFTB::PackType::Public ? QStringLiteral("public:") : QStringLiteral("thirdparty:")) + pack.dir;
}

QStringList legacyVersionNames(const LegacyFTB::Modpack& pack)
{
    auto versions = pack.oldVersions;
    if (!pack.currentVersion.isEmpty()) {
        versions.append(pack.currentVersion);
    }
    versions.removeDuplicates();
    versions.removeIf([](const QString& version) { return !legacyPathPart(version); });
    return versions;
}

QJsonObject serializeLegacyPack(const LegacyFTB::Modpack& pack)
{
    const auto icon = legacyPathPart(pack.logo) ? QUrl(BuildConfig.LEGACY_FTB_CDN_BASE_URL + "static/" + pack.logo) : QUrl();
    return {
        { "provider", "legacy-ftb" },
        { "id", legacyId(pack) },
        { "slug", pack.dir },
        { "name", pack.name },
        { "description", pack.description },
        { "authors", pack.author.isEmpty() ? QJsonArray() : QJsonArray{ pack.author } },
        { "iconUrl", icon.scheme() == "https" ? icon.toString() : QString() },
        { "websiteUrl", QString() },
    };
}

QString ftbAppId(const FTBImportAPP::Modpack& pack)
{
    const auto identity = QUuid(pack.uuid).toString(QUuid::WithoutBraces).toUtf8() + '\0' + pack.path.toUtf8();
    return QStringLiteral("ftb-app:") + QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
}

QString ftbAppVersionId(const FTBImportAPP::Modpack& pack)
{
    const QJsonArray identity{ ftbAppId(pack), pack.versionId, pack.version, pack.mcVersion,
                               pack.loaderType ? ModPlatform::getModLoaderAsString(*pack.loaderType) : QString(), pack.loaderVersion };
    return QStringLiteral("ftb-app:") + QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

void rejectFtbAppPaths(const QJsonObject& p)
{
    for (const auto* key : { "path", "url", "instancesPath", "instanceLocation", "FTBAppInstancesPath" }) {
        if (p.contains(key)) {
            throw ApiError::invalidParams(QObject::tr("FTB App paths are discovered by the backend, not supplied by the webpage"));
        }
    }
}

void checkFtbAppFile(const QString& path, qint64 limit)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return;
    }
    QFile file(path);
    if (!info.isFile() || !info.isReadable() || !file.open(QIODevice::ReadOnly)) {
        throw ApiError::io(QObject::tr("Cannot read local FTB App metadata"));
    }
    if (info.size() > limit) {
        throw ApiError("PARSE_ERROR", QObject::tr("Local FTB App metadata exceeds the size limit"));
    }
}

FTBImportAPP::Modpack readFtbAppPack(const QString& path)
{
    const QFileInfo directory(path);
    if (!directory.isDir() || !directory.isReadable() || !directory.isExecutable() || directory.canonicalFilePath() != path) {
        throw ApiError::io(QObject::tr("The discovered FTB App instance is no longer accessible; search again"));
    }
    for (const auto* relative : { "instance.json", ".ftbapp/version.json", "version.json" }) {
        checkFtbAppFile(QDir(path).filePath(relative), 1024 * 1024);
    }
    for (const auto* relative : { "folder.jpg", ".ftbapp/logo" }) {
        checkFtbAppFile(QDir(path).filePath(relative), 8 * 1024 * 1024);
    }
    auto result = FTBImportAPP::parseDirectory(path);
    if (!result) {
        // Parser diagnostics can contain native paths; do not expose them to the webpage.
        throw ApiError("PARSE_ERROR", QObject::tr("Could not parse a local FTB App instance"));
    }
    const auto& pack = *result;
    if (QUuid(pack.uuid).isNull() || pack.name.trimmed().isEmpty() || pack.name.size() > 256 || pack.version.size() > 128 ||
        pack.mcVersion.trimmed().isEmpty() || pack.mcVersion.size() > 128 || pack.loaderVersion.size() > 128 ||
        pack.totalPlayTime < 0 || pack.id < 0 || pack.versionId < 0) {
        throw ApiError("PARSE_ERROR", QObject::tr("Invalid local FTB App instance metadata"));
    }
    return *result;
}

QString importGroup(const QJsonObject& params)
{
    return params::optionalString(params, "group", 256).value_or(QString()).trimmed();
}

}  // namespace

ModpackApi::ModpackApi(ApiRouter* router, TaskTracker* tasks, QObject* parent) : QObject(parent), m_router(router), m_tasks(tasks)
{
    registerMethods();
}

QString ModpackApi::cacheKey(ModPlatform::ResourceProvider provider, const QString& projectId)
{
    return QStringLiteral("%1:%2").arg(QString::fromLatin1(providerName(provider)), projectId);
}

void ModpackApi::runRequest(const Task::Ptr& task, const ApiReply& reply)
{
    if (!task) {
        throw ApiError::internal(tr("Could not create the modpack request"));
    }
    if (m_requests.size() + m_legacyReplies.size() >= MaxPendingRequests) {
        throw ApiError::invalidParams(tr("Too many pending modpack requests"));
    }
    m_requests.insert(task);
    // ResourceAPI may finish successfully without invoking its callback on malformed JSON.
    connect(task.get(), &Task::succeeded, this,
            [reply] { reply.reject(ApiError("NETWORK_ERROR", tr("Invalid modpack provider response"))); });
    connect(
        task.get(), &Task::finished, this,
        [this, task] { QMetaObject::invokeMethod(this, [this, task] { m_requests.remove(task); }, Qt::QueuedConnection); },
        Qt::SingleShotConnection);
    task->start();
}

QJsonObject ModpackApi::startImport(const QUrl& url,
                                    bool trusted,
                                    const QString& name,
                                    const QString& group,
                                    const QString& originalName,
                                    const QString& originalVersion,
                                    const QMap<QString, QString>& extraInfo)
{
    auto* creation = new InstanceImportTask(url, trusted, nullptr, extraInfo);
    creation->setName(name);
    creation->setOriginalName(originalName.trimmed(), originalVersion);
    creation->setGroup(group);
    creation->setIcon("default");
    APPLICATION->settings()->set("LastUsedGroupForNewInstance", group);
    Task::Ptr task(APPLICATION->instances()->wrapInstanceTask(creation));
    return { { "taskId", m_tasks->start(task, trusted ? "modpack.install" : "modpack.import", tr("Installing %1").arg(name)) } };
}

void ModpackApi::finishLegacy(const LegacyFTB::ModpackList* packs, const QString& error, bool cancelled)
{
    if (!m_legacyFetch) {
        return;
    }
    auto fetch = std::move(m_legacyFetch);
    fetch->disconnect(this);
    QMetaObject::invokeMethod(this, [fetch] {}, Qt::QueuedConnection);
    QString failure = error;
    if (packs && failure.isEmpty() && !cancelled) {
        QHash<QString, LegacyFTB::Modpack> cache;
        for (const auto& pack : *packs) {
            if ((pack.type != LegacyFTB::PackType::Public && pack.type != LegacyFTB::PackType::ThirdParty) || pack.broken ||
                pack.name.trimmed().isEmpty() || pack.mcVersion.trimmed().isEmpty() || !legacyPathPart(pack.dir) ||
                !legacyPathPart(pack.file) || legacyVersionNames(pack).isEmpty()) {
                continue;
            }
            const auto id = legacyId(pack);
            if (cache.contains(id) || cache.size() >= MaxCachedProjects) {
                failure = tr("Invalid or oversized legacy FTB catalogue");
                break;
            }
            cache.insert(id, pack);
        }
        if (cache.isEmpty()) {
            failure = tr("Invalid or empty legacy FTB catalogue");
        }
        if (failure.isEmpty()) {
            m_legacyPacks = std::move(cache);
            m_legacyVersionsListed.clear();
            m_legacyLoaded = true;
        }
    }
    auto replies = std::move(m_legacyReplies);
    m_legacyReplies.clear();
    for (const auto& callback : replies) {
        callback(failure, cancelled);
    }
}

void ModpackApi::searchLegacy(const QJsonObject& p, const ApiReply& reply)
{
    const auto query = params::requireString(p, "query", 256).trimmed();
    const auto offset = static_cast<int>(params::optionalInt(p, "offset", 0, 0, 100000));
    const auto sort = params::optionalString(p, "sort", 64).value_or(QStringLiteral("minecraft"));
    const auto minecraft = params::optionalString(p, "minecraftVersion", 128).value_or(QString()).trimmed();
    if (sort != "name" && sort != "minecraft") {
        throw ApiError::invalidParams(tr("Unknown sorting method '%1'").arg(sort));
    }
    if (p.contains("packCode") || p.contains("privateCode")) {
        throw ApiError::invalidParams(tr("Private legacy FTB pack codes are not supported"));
    }
    auto complete = [this, reply, query, offset, sort, minecraft](const QString& error, bool cancelled) {
        reply.guard([&] {
            if (cancelled) {
                throw ApiError::cancelled();
            }
            if (!error.isEmpty()) {
                throw ApiError("NETWORK_ERROR", error);
            }
            LegacyFTB::ModpackList matches;
            for (const auto& pack : m_legacyPacks) {
                if (pack.name.contains(query, Qt::CaseInsensitive) && (minecraft.isEmpty() || pack.mcVersion == minecraft)) {
                    matches.append(pack);
                }
            }
            std::sort(matches.begin(), matches.end(), [sort](const auto& left, const auto& right) {
                if (sort == "minecraft" && Version(left.mcVersion) != Version(right.mcVersion)) {
                    return Version(right.mcVersion) < Version(left.mcVersion);
                }
                const auto names = QString::compare(left.name, right.name, Qt::CaseInsensitive);
                return names == 0 ? legacyId(left) < legacyId(right) : names < 0;
            });
            QJsonArray projects;
            for (qsizetype i = offset; i < matches.size() && i < offset + 25; ++i) {
                projects.append(serializeLegacyPack(matches.at(i)));
            }
            reply.resolve(QJsonObject{
                { "projects", projects },
                { "offset", offset },
                { "sortingMethods", QJsonArray{ QJsonObject{ { "id", "minecraft" }, { "name", tr("Sort by Game Version") } },
                                                QJsonObject{ { "id", "name" }, { "name", tr("Sort by Name") } } } },
            });
        });
    };
    if (m_legacyLoaded) {
        complete({}, false);
        return;
    }
    if (m_legacyReplies.size() + m_requests.size() >= MaxPendingRequests) {
        throw ApiError::invalidParams(tr("Too many pending modpack requests"));
    }
    m_legacyReplies.append(std::move(complete));
    if (m_legacyFetch) {
        return;
    }
    m_legacyFetch = std::make_shared<LegacyFTB::PackFetchTask>(APPLICATION->network());
    auto* fetch = m_legacyFetch.get();
    connect(fetch, &LegacyFTB::PackFetchTask::finished, this,
            [this](LegacyFTB::ModpackList publicPacks, const LegacyFTB::ModpackList& thirdPartyPacks) {
                publicPacks.append(thirdPartyPacks);
                finishLegacy(&publicPacks);
            });
    connect(fetch, &LegacyFTB::PackFetchTask::failed, this,
            [this](const QString& reason) { finishLegacy(nullptr, reason.isEmpty() ? tr("Legacy FTB request failed") : reason); });
    connect(fetch, &LegacyFTB::PackFetchTask::aborted, this, [this] { finishLegacy(nullptr, {}, true); });
    auto* timeout = new QTimer(fetch);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, this, [this] { finishLegacy(nullptr, tr("Legacy FTB request timed out")); });
    timeout->start(120000);
    fetch->fetch();
}

QJsonArray ModpackApi::legacyVersions(const QString& projectId)
{
    const auto found = m_legacyPacks.constFind(projectId);
    if (found == m_legacyPacks.cend()) {
        throw ApiError::notFound(tr("Unknown project %1; search for it first").arg(projectId));
    }
    QJsonArray versions;
    for (const auto& version : legacyVersionNames(*found)) {
        versions.append(QJsonObject{
            { "id", version },
            { "name", version },
            { "versionNumber", version },
            { "type", QString() },
            { "gameVersions", found->mcVersion.isEmpty() ? QJsonArray() : QJsonArray{ found->mcVersion } },
            { "loaders", QJsonArray() },
            { "date", QString() },
            { "fileName", found->file },
            { "compatible", true },
        });
    }
    m_legacyVersionsListed.insert(projectId);
    return versions;
}

QJsonObject ModpackApi::installLegacy(const QJsonObject& p)
{
    const auto projectId = params::requireNonEmpty(p, "projectId", 128);
    const auto version = params::requireNonEmpty(p, "versionId", 128);
    const auto name = params::requireNonEmpty(p, "name", 256);
    const auto group = importGroup(p);
    const auto found = m_legacyPacks.constFind(projectId);
    if (found == m_legacyPacks.cend() || !m_legacyVersionsListed.contains(projectId) || !legacyVersionNames(*found).contains(version)) {
        throw ApiError::notFound(tr("Unknown version %1 of %2; list the versions first").arg(version, projectId));
    }
    if (found->broken || found->mcVersion.trimmed().isEmpty()) {
        throw ApiError::unsupported(tr("This legacy FTB pack is broken or has no Minecraft version"));
    }
    auto* creation = new LegacyFTB::PackInstallTask(APPLICATION->network(), *found, version);
    creation->setName(name);
    creation->setOriginalName(found->name.trimmed(), version);
    creation->setGroup(group);
    creation->setIcon("default");
    if (legacyPathPart(found->logo)) {
        const auto logo = APPLICATION->metacache()->resolveEntry("FTBPacks", "logos/" + found->logo)->getFullPath();
        if (QFileInfo(logo).isFile()) {
            const auto iconName = found->logo.startsWith("ftb", Qt::CaseInsensitive) ? found->logo : "ftb_" + found->logo;
            APPLICATION->icons()->installIcon(logo, iconName);
            creation->setIcon(iconName);
        }
    }
    APPLICATION->settings()->set("LastUsedGroupForNewInstance", group);
    Task::Ptr task(APPLICATION->instances()->wrapInstanceTask(creation));
    return { { "taskId", m_tasks->start(task, "modpack.install", tr("Installing %1").arg(name)) } };
}

void ModpackApi::discoverFtbApp()
{
    const auto configured = APPLICATION->settings()->get("FTBAppInstancesPath").toString();
    if (m_ftbAppScanAge.isValid() && m_ftbAppScanAge.elapsed() < 30000 && configured == m_ftbAppConfiguredPath) {
        return;
    }
    QStringList roots{ configured };
    auto appRoot = QDir::homePath();
#if defined(Q_OS_MACOS)
    appRoot = QDir(appRoot).filePath("Library/Application Support");
#endif
    appRoot = QDir(appRoot).filePath(".ftba");
    auto settingsPath = QDir(appRoot).filePath("storage/settings.json");
    if (!QFileInfo::exists(settingsPath)) {
        settingsPath = QDir(appRoot).filePath("bin/settings.json");
    }
    if (QFileInfo::exists(settingsPath)) {
        checkFtbAppFile(settingsPath, 1024 * 1024);
        QFile settings(settingsPath);
        if (!settings.open(QIODevice::ReadOnly)) {
            throw ApiError::io(tr("Cannot read FTB App settings"));
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(settings.read(1024 * 1024 + 1), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject() || !doc.object().value("instanceLocation").isString()) {
            throw ApiError("PARSE_ERROR", tr("Invalid FTB App settings or instanceLocation"));
        }
        roots.append(doc.object().value("instanceLocation").toString());
    }
    QHash<QString, FTBImportAPP::Modpack> cache;
    QSet<QString> visitedRoots;
    QSet<QString> visitedPaths;
    qsizetype scanned = 0;
    for (const auto& root : roots) {
        if (root.isEmpty()) {
            continue;
        }
        if (root.size() > 4096 || !QDir::isAbsolutePath(root)) {
            throw ApiError("PARSE_ERROR", tr("FTB App instanceLocation must be an absolute native path"));
        }
        const QFileInfo rootInfo(root);
        if (!rootInfo.exists()) {
            continue;
        }
        if (!rootInfo.isDir() || !rootInfo.isReadable() || !rootInfo.isExecutable()) {
            throw ApiError::io(tr("Cannot access the FTB App instances directory"));
        }
        const auto canonicalRoot = rootInfo.canonicalFilePath();
        if (canonicalRoot.isEmpty()) {
            throw ApiError::io(tr("Cannot resolve the FTB App instances directory"));
        }
        if (visitedRoots.contains(canonicalRoot)) {
            continue;
        }
        visitedRoots.insert(canonicalRoot);
        for (const auto& entry : QDirListing(canonicalRoot, QDirListing::IteratorFlag::DirsOnly |
                                                              QDirListing::IteratorFlag::IncludeHidden |
                                                              QDirListing::IteratorFlag::ResolveSymlinks |
                                                              QDirListing::IteratorFlag::FollowDirSymlinks)) {
            if (++scanned > 2048) {
                throw ApiError::io(tr("The local FTB App directory scan exceeds the entry limit"));
            }
            const auto path = QFileInfo(entry.absoluteFilePath()).canonicalFilePath();
            if (path.isEmpty()) {
                throw ApiError::io(tr("Cannot resolve a local FTB App instance directory"));
            }
            if (visitedPaths.contains(path)) {
                continue;
            }
            visitedPaths.insert(path);
            if (!QFileInfo::exists(QDir(path).filePath("instance.json"))) {
                continue;
            }
            auto pack = readFtbAppPack(path);
            if (cache.size() >= MaxCachedProjects) {
                throw ApiError::io(tr("Too many local FTB App instances"));
            }
            cache.insert(ftbAppId(pack), std::move(pack));
        }
    }
    m_ftbAppPacks = std::move(cache);
    for (auto it = m_ftbAppVersionsListed.begin(); it != m_ftbAppVersionsListed.end();) {
        const auto pack = m_ftbAppPacks.constFind(it.key());
        if (pack == m_ftbAppPacks.cend() || ftbAppVersionId(*pack) != it.value()) {
            it = m_ftbAppVersionsListed.erase(it);
        } else {
            ++it;
        }
    }
    m_ftbAppConfiguredPath = configured;
    m_ftbAppScanAge.start();
}

QJsonObject ModpackApi::searchFtbApp(const QJsonObject& p)
{
    rejectFtbAppPaths(p);
    const auto query = params::requireString(p, "query", 256).trimmed();
    const auto offset = static_cast<int>(params::optionalInt(p, "offset", 0, 0, 100000));
    const auto sort = params::optionalString(p, "sort", 64).value_or(QStringLiteral("name"));
    const auto minecraft = params::optionalString(p, "minecraftVersion", 128).value_or(QString()).trimmed();
    if (sort != "name") {
        throw ApiError::invalidParams(tr("Unknown sorting method '%1'").arg(sort));
    }
    discoverFtbApp();
    QStringList matches;
    for (auto it = m_ftbAppPacks.cbegin(); it != m_ftbAppPacks.cend(); ++it) {
        if (it->name.contains(query, Qt::CaseInsensitive) && (minecraft.isEmpty() || it->mcVersion == minecraft)) {
            matches.append(it.key());
        }
    }
    std::sort(matches.begin(), matches.end(), [this](const auto& left, const auto& right) {
        const auto names = QString::compare(m_ftbAppPacks.value(left).name, m_ftbAppPacks.value(right).name, Qt::CaseInsensitive);
        return names == 0 ? left < right : names < 0;
    });
    QJsonArray projects;
    for (qsizetype i = offset; i < matches.size() && i < offset + 25; ++i) {
        const auto& id = matches.at(i);
        const auto pack = m_ftbAppPacks.value(id);
        projects.append(QJsonObject{ { "provider", "ftb-app" }, { "id", id }, { "slug", QString() }, { "name", pack.name },
                                     { "description", QString() }, { "authors", QJsonArray() }, { "iconUrl", QString() },
                                     { "websiteUrl", QString() } });
    }
    return { { "projects", projects }, { "offset", offset },
             { "sortingMethods", QJsonArray{ QJsonObject{ { "id", "name" }, { "name", tr("Sort by Name") } } } } };
}

QJsonArray ModpackApi::ftbAppVersions(const QString& projectId)
{
    const auto found = m_ftbAppPacks.constFind(projectId);
    if (found == m_ftbAppPacks.cend()) {
        throw ApiError::notFound(tr("Unknown local FTB App project; search for it first"));
    }
    auto pack = readFtbAppPack(found->path);
    if (ftbAppId(pack) != projectId || ftbAppVersionId(pack) != ftbAppVersionId(*found)) {
        throw ApiError::notFound(tr("The local FTB App instance changed; search again"));
    }
    const auto versionId = ftbAppVersionId(pack);
    m_ftbAppVersionsListed.insert(projectId, versionId);
    QJsonArray loaders;
    if (pack.loaderType) {
        loaders.append(ModPlatform::getModLoaderAsString(*pack.loaderType));
    }
    return { QJsonObject{ { "id", versionId }, { "name", pack.version }, { "versionNumber", pack.version }, { "type", QString() },
                          { "gameVersions", QJsonArray{ pack.mcVersion } }, { "loaders", loaders }, { "date", QString() },
                          { "fileName", QString() }, { "compatible", false } } };
}

QJsonObject ModpackApi::installFtbApp(const QJsonObject& p)
{
    rejectFtbAppPaths(p);
    const auto projectId = params::requireNonEmpty(p, "projectId", 128);
    const auto versionId = params::requireNonEmpty(p, "versionId", 128);
    const auto name = params::requireNonEmpty(p, "name", 256);
    const auto group = importGroup(p);
    const auto found = m_ftbAppPacks.constFind(projectId);
    if (found == m_ftbAppPacks.cend() || m_ftbAppVersionsListed.value(projectId) != versionId ||
        ftbAppVersionId(*found) != versionId) {
        throw ApiError::notFound(tr("Unknown local FTB App version; list the versions first"));
    }
    const auto current = readFtbAppPack(found->path);
    if (ftbAppId(current) != projectId || ftbAppVersionId(current) != versionId) {
        throw ApiError::notFound(tr("The local FTB App instance changed; search again"));
    }
    // Keep the native Modpack, including playtime and JVM args, for the existing importer.
    auto* creation = new FTBImportAPP::PackInstallTask(*found);
    creation->setName(name);
    creation->setOriginalName(found->name.trimmed(), found->version);
    creation->setGroup(group);
    creation->setIcon("default");
    if (!found->icon.isNull()) {
        QTemporaryFile iconFile(QDir::tempPath() + "/materialmc-ftb-XXXXXX.png");
        if (iconFile.open() && found->icon.pixmap(128, 128).save(&iconFile, "PNG")) {
            iconFile.flush();
            const auto iconName = QStringLiteral("ftb_") + projectId.mid(8);
            APPLICATION->icons()->installIcon(iconFile.fileName(), iconName);
            creation->setIcon(iconName);
        }
    }
    APPLICATION->settings()->set("LastUsedGroupForNewInstance", group);
    Task::Ptr task(APPLICATION->instances()->wrapInstanceTask(creation));
    return { { "taskId", m_tasks->start(task, "modpack.install", tr("Importing %1").arg(name)) } };
}

void ModpackApi::registerMethods()
{
    m_router->add("modpacks.search", [this](const QJsonObject& p, const ApiReply& reply) {
        if (params::requireString(p, "provider", 64) == "ftb-app") {
            reply.resolve(searchFtbApp(p));
            return;
        }
        if (params::requireString(p, "provider", 64) == "legacy-ftb") {
            searchLegacy(p, reply);
            return;
        }
        const auto provider = params::requireEnum(p, "provider", Providers);
        const auto& api = apiFor(provider);
        const auto query = params::requireString(p, "query", 256).trimmed();
        const auto offset = static_cast<int>(params::optionalInt(p, "offset", 0, 0, 100000));
        const auto sortId = params::optionalString(p, "sort", 64);
        const auto mcVersion = params::optionalString(p, "minecraftVersion", 128);
        const auto methods = api.getSortingMethods();
        std::optional<ResourceAPI::SortingMethod> sorting;
        QJsonArray sortingJson;
        for (const auto& method : methods) {
            sortingJson.append(QJsonObject{ { "id", method.name }, { "name", method.readableName } });
            if (sortId && method.name == *sortId) {
                sorting = method;
            }
        }
        if (sortId && !sorting) {
            throw ApiError::invalidParams(tr("Unknown sorting method '%1'").arg(*sortId));
        }
        if (!sorting && !methods.isEmpty()) {
            sorting = methods.first();
        }
        ResourceAPI::SearchArgs args{ .type = ModPlatform::ResourceType::Modpack, .offset = offset, .search = query, .sorting = sorting };
        if (mcVersion && !mcVersion->trimmed().isEmpty()) {
            args.versions = std::vector<Version>{ Version(mcVersion->trimmed()) };
        }
        ResourceAPI::Callback<QList<ModPlatform::IndexedPack::Ptr>> callbacks;
        callbacks.onSucceed = [this, reply, offset, sortingJson](QList<ModPlatform::IndexedPack::Ptr>& packs) {
            QJsonArray projects;
            for (const auto& pack : packs) {
                const auto key = cacheKey(pack->provider, pack->addonId.toString());
                if (!m_packs.contains(key) && m_packs.size() >= MaxCachedProjects) {
                    const auto evicted = m_packs.constBegin().key();
                    m_packs.remove(evicted);
                    m_versions.remove(evicted);
                }
                m_packs.insert(key, pack);
                projects.append(serializePack(*pack));
            }
            reply.resolve(QJsonObject{ { "projects", projects }, { "offset", offset }, { "sortingMethods", sortingJson } });
        };
        callbacks.onFail = [reply](const QString& reason, int) { reply.reject(ApiError("NETWORK_ERROR", reason)); };
        callbacks.onAbort = [reply] { reply.reject(ApiError::cancelled()); };
        runRequest(api.searchProjects(args, callbacks), reply);
    });

    m_router->add("modpacks.versions", [this](const QJsonObject& p, const ApiReply& reply) {
        if (params::requireString(p, "provider", 64) == "ftb-app") {
            rejectFtbAppPaths(p);
            reply.resolve(ftbAppVersions(params::requireNonEmpty(p, "projectId", 128)));
            return;
        }
        if (params::requireString(p, "provider", 64) == "legacy-ftb") {
            reply.resolve(legacyVersions(params::requireNonEmpty(p, "projectId", 128)));
            return;
        }
        const auto provider = params::requireEnum(p, "provider", Providers);
        const auto& api = apiFor(provider);
        const auto projectId = params::requireNonEmpty(p, "projectId", 128);
        const auto key = cacheKey(provider, projectId);
        const auto pack = m_packs.value(key);
        if (!pack) {
            throw ApiError::notFound(tr("Unknown project %1; search for it first").arg(projectId));
        }
        ResourceAPI::VersionSearchArgs args{
            .pack = pack, .mcVersions = {}, .loaders = {}, .resourceType = ModPlatform::ResourceType::Modpack
        };
        ResourceAPI::Callback<QVector<ModPlatform::IndexedVersion>> callbacks;
        callbacks.onSucceed = [this, reply, key](QVector<ModPlatform::IndexedVersion>& versions) {
            if (m_packs.contains(key)) {
                m_versions.insert(key, versions);
            }
            QJsonArray out;
            for (const auto& version : versions) {
                out.append(serializeVersion(version));
            }
            reply.resolve(out);
        };
        callbacks.onFail = [reply](const QString& reason, int) { reply.reject(ApiError("NETWORK_ERROR", reason)); };
        callbacks.onAbort = [reply] { reply.reject(ApiError::cancelled()); };
        runRequest(api.getProjectVersions(args, callbacks), reply);
    });

    m_router->addSync("modpacks.install", [this](const QJsonObject& p) {
        if (params::requireString(p, "provider", 64) == "ftb-app") {
            return installFtbApp(p);
        }
        if (params::requireString(p, "provider", 64) == "legacy-ftb") {
            return installLegacy(p);
        }
        const auto provider = params::requireEnum(p, "provider", Providers);
        apiFor(provider);
        const auto projectId = params::requireNonEmpty(p, "projectId", 128);
        const auto versionId = params::requireNonEmpty(p, "versionId", 128);
        const auto name = params::requireNonEmpty(p, "name", 256);
        const auto group = importGroup(p);
        const auto key = cacheKey(provider, projectId);
        const auto pack = m_packs.value(key);
        const auto versions = m_versions.value(key);
        const auto version =
            std::find_if(versions.cbegin(), versions.cend(), [&](const auto& v) { return v.fileId.toString() == versionId; });
        if (!pack || version == versions.cend()) {
            throw ApiError::notFound(tr("Unknown version %1 of %2; list the versions first").arg(versionId, projectId));
        }
        if (version->downloadUrl.isEmpty()) {
            throw ApiError::unsupported(
                tr("%1 does not allow third-party downloads of this file; download it from the website").arg(pack->name));
        }
        const auto url = remoteUrl(version->downloadUrl);
        return startImport(url, true, name, group, pack->name, version->version,
                           { { "pack_id", projectId }, { "pack_version_id", versionId } });
    });

    m_router->addDeferred("modpacks.importFile", [this](const QJsonObject& p) {
        const auto name = params::requireNonEmpty(p, "name", 256);
        const auto group = importGroup(p);
        if (p.contains("path") || p.contains("url")) {
            throw ApiError::invalidParams(tr("Choose the modpack using the native file picker"));
        }
        const auto path = QFileDialog::getOpenFileName(nullptr, tr("Choose modpack"), {}, tr("Supported files") + " (*.zip *.mrpack)");
        if (path.isEmpty()) {
            throw ApiError::cancelled();
        }
        const QFileInfo file(path);
        if (!file.isFile() || !file.isReadable()) {
            throw ApiError::io(tr("The selected modpack cannot be read"));
        }
        return startImport(QUrl::fromLocalFile(file.absoluteFilePath()), false, name, group, file.completeBaseName());
    });

    m_router->addSync("modpacks.importUrl", [this](const QJsonObject& p) {
        const auto name = params::requireNonEmpty(p, "name", 256);
        const auto group = importGroup(p);
        const auto url = remoteUrl(params::requireNonEmpty(p, "url", 4096));
        return startImport(url, false, name, group, QFileInfo(url.fileName()).completeBaseName());
    });
}

}  // namespace api
