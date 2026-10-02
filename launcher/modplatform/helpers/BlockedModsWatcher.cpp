// SPDX-FileCopyrightText: 2022 Sefa Eyeoglu <contact@scrumplex.net>
// SPDX-FileCopyrightText: 2022 Rachel Powers <508861+Ryex@users.noreply.github.com>
// SPDX-FileCopyrightText: 2022 kumquat-ir <66188216+kumquat-ir@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "BlockedModsWatcher.h"

#include <QDirListing>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

#include "Application.h"
#include "interaction/UserInteraction.h"
#include "modplatform/helpers/HashUtils.h"
#include "settings/SettingsObject.h"

BlockedModsWatcher::BlockedModsWatcher(QList<BlockedMod>& mods, QString hashType, QObject* parent)
    : QObject(parent), m_mods(mods), m_hashType(std::move(hashType))
{
    m_hashingTask = shared_qobject_ptr<ConcurrentTask>(
        new ConcurrentTask("MakeHashesTask", APPLICATION->settings()->get("NumberOfConcurrentTasks").toInt()));
    connect(m_hashingTask.get(), &Task::finished, this, &BlockedModsWatcher::hashTaskFinished);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &BlockedModsWatcher::directoryChanged);
    qDebug() << "[Blocked Mods] Mods List:" << mods;
}

void BlockedModsWatcher::start()
{
    const QString downloadsFolder = APPLICATION->settings()->get("DownloadsDir").toString();
    const QString modsFolder = APPLICATION->settings()->get("CentralModsDir").toString();
    const bool downloadsFolderWatchRecursive = APPLICATION->settings()->get("DownloadsDirWatchRecursive").toBool();
    watchPath(downloadsFolder, downloadsFolderWatchRecursive);
    watchPath(modsFolder, true);
    scanPaths();
    emit changed();
}

void BlockedModsWatcher::addPath(const QString& path)
{
    const QFileInfo info(path);
    if (info.isFile()) {
        addHashTask(info.absoluteFilePath());
        m_watcher.addPath(info.dir().absolutePath());
        scanPaths();
    } else if (info.isDir()) {
        qDebug() << "[Blocked Mods] Adding watch path:" << path;
        m_watcher.addPath(path);
        scanPath(path, true);
    }
    emit changed();
}

bool BlockedModsWatcher::allMatched() const
{
    return std::ranges::all_of(m_mods, [](const auto& mod) { return mod.matched; });
}

QJsonObject BlockedModsWatcher::toJson() const
{
    QJsonArray mods;
    for (const auto& mod : m_mods) {
        mods.append(QJsonObject{ { "name", mod.name },
                                 { "url", mod.websiteUrl },
                                 { "hash", mod.hash },
                                 { "matched", mod.matched },
                                 { "localPath", mod.localPath } });
    }
    return { { "mods", mods }, { "watched", QJsonArray::fromStringList(watched()) }, { "allMatched", allMatched() } };
}

void BlockedModsWatcher::directoryChanged(const QString& path)
{
    qDebug() << "[Blocked Mods] Directory changed:" << path;
    validateMatchedMods();
    scanPath(path, true);
}

void BlockedModsWatcher::watchPath(const QString& path, bool watchRecursive)
{
    auto toWatch = QFileInfo(path);
    if (!toWatch.isReadable()) {
        qWarning() << "[Blocked Mods] Failed to add Watch Path (unable to read):" << path;
        return;
    }
    auto toWatchPath = toWatch.canonicalFilePath();
    if (m_watcher.directories().contains(toWatchPath)) {
        return;  // don't watch the same path twice (no loops!)
    }

    qDebug() << "[Blocked Mods] Adding Watch Path:" << path;
    m_watcher.addPath(toWatchPath);

    if (!toWatch.isDir() || !watchRecursive) {
        return;
    }

    for (const auto& entry : QDirListing(toWatchPath, QDirListing::IteratorFlag::DirsOnly | QDirListing::IteratorFlag::ResolveSymlinks)) {
        watchPath(entry.canonicalFilePath(), watchRecursive);
    }
}

void BlockedModsWatcher::scanPaths()
{
    for (auto& dir : m_watcher.directories()) {
        scanPath(dir, false);
    }
    runHashTask();
}

/// Scan the directory at path, skip paths that do not contain a file name of a blocked mod we are looking for
void BlockedModsWatcher::scanPath(const QString& path, bool startTask)
{
    for (const auto& entry : QDirListing(path, QDirListing::IteratorFlag::FilesOnly | QDirListing::IteratorFlag::ResolveSymlinks |
                                                   QDirListing::IteratorFlag::IncludeHidden)) {
        QString file = entry.absoluteFilePath();
        if (!checkValidPath(file)) {
            continue;
        }
        addHashTask(file);
    }

    if (startTask) {
        runHashTask();
    }
}

void BlockedModsWatcher::addHashTask(const QString& path)
{
    m_pendingHashPaths.insert(path);
}

void BlockedModsWatcher::buildHashTask(const QString& path)
{
    auto hashTask = Hashing::createHasher(path, m_hashType);
    connect(hashTask.get(), &Task::succeeded, this, [this, hashTask, path] { checkMatchHash(hashTask->getResult(), path); });
    connect(hashTask.get(), &Task::failed, this, [path] { qDebug() << "Failed to hash path:" << path; });
    m_hashingTask->addTask(hashTask);
}

void BlockedModsWatcher::checkMatchHash(const QString& hash, const QString& path)
{
    bool match = false;

    auto downloadDir = QFileInfo(APPLICATION->settings()->get("DownloadsDir").toString()).absoluteFilePath();
    auto moveFiles = APPLICATION->settings()->get("MoveModsFromDownloadsDir").toBool();
    for (auto& mod : m_mods) {
        if (mod.matched) {
            continue;
        }
        if (mod.hash.compare(hash, Qt::CaseInsensitive) == 0) {
            mod.matched = true;
            mod.localPath = path;
            if (moveFiles) {
                mod.move = QFileInfo(path).absoluteFilePath().startsWith(downloadDir);
            }
            match = true;
            qDebug() << "[Blocked Mods] Hash match found:" << mod.name << hash << "| From path:" << path;
            break;
        }
    }

    if (match) {
        emit changed();
    }
}

/// Does the name of the file at path match the name of a blocked mod we are searching for?
bool BlockedModsWatcher::checkValidPath(const QString& path)
{
    const QFileInfo file = QFileInfo(path);
    const QString filename = file.fileName();

    auto compare = [](const QString& fsFilename, const QString& metadataFilename) {
        return metadataFilename.compare(fsFilename, Qt::CaseInsensitive) == 0;
    };

    // super lax compare (but not fuzzy): ignore case and all separators
    auto laxCompare = [](const QString& fsfilename, const QString& metadataFilename) {
        QList<QChar> allowedSeperators = { '-', '+', '.', '_' };
        auto fsName = fsfilename.toLower();
        auto metaName = metadataFilename.toLower();
        for (auto sep : allowedSeperators) {
            fsName = fsName.replace(sep, ' ');
            metaName = metaName.replace(sep, ' ');
        }
        return fsName.simplified().compare(metaName.simplified()) == 0;
    };

    auto downloadDir = QFileInfo(APPLICATION->settings()->get("DownloadsDir").toString()).absoluteFilePath();
    auto moveFiles = APPLICATION->settings()->get("MoveModsFromDownloadsDir").toBool();
    for (auto& mod : m_mods) {
        if (compare(filename, mod.name)) {
            // not yet matched and no hash: just match it with the file that has the exact same name
            if (!mod.matched && mod.hash.isEmpty()) {
                mod.matched = true;
                mod.localPath = path;
                if (moveFiles) {
                    mod.move = QFileInfo(path).absoluteFilePath().startsWith(downloadDir);
                }
                emit changed();
                return false;
            }
            return true;
        }
        if (laxCompare(filename, mod.name)) {
            return true;
        }
    }

    return false;
}

void BlockedModsWatcher::validateMatchedMods()
{
    bool changedAny = false;
    for (auto& mod : m_mods) {
        if (mod.matched) {
            QFileInfo file = QFileInfo(mod.localPath);
            if (!file.exists() || !file.isFile()) {
                mod.localPath = "";
                mod.matched = false;
                changedAny = true;
            }
        }
    }
    if (changedAny) {
        emit changed();
    }
}

void BlockedModsWatcher::runHashTask()
{
    if (!m_hashingTask->isRunning()) {
        m_rehashPending = false;
        if (!m_pendingHashPaths.isEmpty()) {
            auto path = m_pendingHashPaths.begin();
            while (path != m_pendingHashPaths.end()) {
                buildHashTask(*path);
                path = m_pendingHashPaths.erase(path);
            }
            m_hashingTask->start();
        }
    } else {
        m_rehashPending = true;
    }
}

void BlockedModsWatcher::hashTaskFinished()
{
    if (m_rehashPending) {
        runHashTask();
    }
}

QDebug operator<<(QDebug debug, const BlockedMod& m)
{
    QDebugStateSaver saver(debug);
    debug.nospace() << "{ name: " << m.name << ", websiteUrl: " << m.websiteUrl << ", hash: " << m.hash << ", matched: " << m.matched
                    << ", localPath: " << m.localPath << "}";
    return debug;
}

namespace interaction {

bool resolveBlockedMods(const QString& title, const QString& text, QList<BlockedMod>& mods, const QString& hashType)
{
    BlockedModsWatcher watcher(mods, hashType);
    auto* ui = UserInteraction::instance();

    Prompt prompt;
    prompt.kind = "blockedMods";
    prompt.title = title;
    prompt.text = text;
    prompt.icon = "warning";
    prompt.buttons = { reject(QObject::tr("Cancel")), accept(QObject::tr("Skip"), "skip") };
    prompt.payload = watcher.toJson();

    int id = 0;
    bool finished = false;
    QObject::connect(&watcher, &BlockedModsWatcher::changed, &watcher, [&] {
        if (id == 0 || finished) {
            return;
        }
        if (watcher.allMatched()) {
            finished = true;
            ui->finish(id, Answer{ "continue", false, {} });
            return;
        }
        ui->update(id, watcher.toJson());
    });

    Answer result;
    QEventLoop loop;
    id = ui->ask(
        prompt,
        [&](const Answer& a) {
            finished = true;
            result = a;
            loop.quit();
        },
        [&](const QString& action, const QJsonObject&) {
            if (action == QLatin1String("addFolder")) {
                // The folder is chosen natively: the page never supplies paths.
                const auto dir = QFileDialog::getExistingDirectory(nullptr, QObject::tr("Select directory where you downloaded the mods"),
                                                                   QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                                                                   QFileDialog::ShowDirsOnly);
                if (!dir.isEmpty()) {
                    watcher.addPath(dir);
                }
            }
        });
    // defer the scan until the prompt is shown (macOS permission prompts appear after the dialog)
    QTimer::singleShot(0, &watcher, [&watcher] { watcher.start(); });
    if (!finished) {
        loop.exec(QEventLoop::DialogExec);
    }
    qDebug() << "Post dialog blocked mods list:" << mods;
    return !result.button.isEmpty() && result.button != QLatin1String("cancel");
}

}  // namespace interaction
