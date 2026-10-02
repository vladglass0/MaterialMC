// SPDX-FileCopyrightText: 2022 Sefa Eyeoglu <contact@scrumplex.net>
// SPDX-FileCopyrightText: 2022 Rachel Powers <508861+Ryex@users.noreply.github.com>
// SPDX-FileCopyrightText: 2022 kumquat-ir <66188216+kumquat-ir@users.noreply.github.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <QDebug>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include "tasks/ConcurrentTask.h"

/** A file a modpack needs that the platform does not allow third-party launchers to download. */
struct BlockedMod {
    QString name;
    QString websiteUrl;
    QString hash;
    bool matched;
    QString localPath;
    QString targetFolder;
    bool disabled = false;
    bool move = false;
};

QDebug operator<<(QDebug debug, const BlockedMod& m);

/**
 * Watches the downloads folder, the central mods folder and any folder the user adds, hashing candidate files until
 * every blocked mod has been found locally. (Formerly part of BlockedModsDialog.)
 */
class BlockedModsWatcher : public QObject {
    Q_OBJECT
   public:
    BlockedModsWatcher(QList<BlockedMod>& mods, QString hashType = "sha1", QObject* parent = nullptr);

    /** Starts watching the default folders and scans them. */
    void start();
    /** Adds a folder (or a file's folder) chosen by the user and scans it. */
    void addPath(const QString& path);

    bool allMatched() const;
    QStringList watched() const { return m_watcher.directories(); }
    /** { mods: [{ name, url, hash, matched, localPath }], watched: [string], allMatched } */
    QJsonObject toJson() const;

   signals:
    void changed();

   private:
    void directoryChanged(const QString& path);
    void watchPath(const QString& path, bool watchRecursive = false);
    void scanPaths();
    void scanPath(const QString& path, bool startTask);
    void addHashTask(const QString& path);
    void buildHashTask(const QString& path);
    void checkMatchHash(const QString& hash, const QString& path);
    void validateMatchedMods();
    void runHashTask();
    void hashTaskFinished();
    bool checkValidPath(const QString& path);

    QList<BlockedMod>& m_mods;
    QFileSystemWatcher m_watcher;
    shared_qobject_ptr<ConcurrentTask> m_hashingTask;
    QSet<QString> m_pendingHashPaths;
    bool m_rehashPending = false;
    QString m_hashType;
};

namespace interaction {
/**
 * Asks the user to download blocked files manually (blocking, like the old BlockedModsDialog::exec()).
 * Returns true when the user continues (all found, or "Skip"), false when they cancelled.
 */
bool resolveBlockedMods(const QString& title, const QString& text, QList<BlockedMod>& mods, const QString& hashType = "sha1");
}  // namespace interaction
