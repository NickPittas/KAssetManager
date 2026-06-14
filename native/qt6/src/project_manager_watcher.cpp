#include "project_manager_watcher.h"
#include "project_db.h"
#include "project_path_utils.h"
#include "project_version_detector.h"
#include "log_manager.h"
#include "file_utils.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>
#include <QtConcurrent>

ProjectManagerWatcher::ProjectManagerWatcher(QObject* parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &ProjectManagerWatcher::onDirectoryChanged);
    
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(2000); // 2 second debounce
    connect(&m_debounceTimer, &QTimer::timeout,
            this, &ProjectManagerWatcher::onProcessChanges);
}

ProjectManagerWatcher::~ProjectManagerWatcher()
{
    if (!m_watcher->directories().isEmpty()) {
        m_watcher->removePaths(m_watcher->directories());
    }
}

void ProjectManagerWatcher::watchProject(int projectId, const QString& watchPath)
{
    const auto availability = FileUtils::checkPathAvailability(watchPath, FileUtils::PathAvailabilityMode::DirectoryOnly);
    if (!availability.available) {
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Cannot watch unavailable path: %1 (%2)")
                .arg(watchPath, availability.message), "WARN");
        return;
    }
    
    // Unwatch previous path if any
    unwatchProject(projectId);
    
    // Store mapping
    const QString normalizedWatchPath = ProjectPathUtils::cleanPath(watchPath);
    m_projectPaths[projectId] = normalizedWatchPath;
    m_pathToProject[ProjectPathUtils::keyForPath(normalizedWatchPath)] = projectId;
    
    const int generation = m_scanGenerations.value(projectId, 0) + 1;
    m_scanGenerations[projectId] = generation;

    QtConcurrent::run([normalizedWatchPath] {
        return ProjectManagerWatcher::scanDirectoryTree(normalizedWatchPath);
    }).then(this, [this, projectId, generation](const DirectoryScanResult& result) {
        applyInitialScan(projectId, generation, result);
    });

    LogManager::instance().addLog(
        QString("[ProjectManagerWatcher] Scheduled project watch scan %1 at: %2")
            .arg(projectId).arg(watchPath), "INFO");
}

void ProjectManagerWatcher::unwatchProject(int projectId)
{
    if (!m_projectPaths.contains(projectId)) {
        return;
    }
    
    // Remove all watched paths for this project
    const QStringList pathsToRemove = m_projectWatchedDirs.value(projectId);
    
    if (!pathsToRemove.isEmpty()) {
        m_watcher->removePaths(pathsToRemove);
    }
    
    // Clean up mappings
    for (const QString& path : pathsToRemove) {
        m_pathToProject.remove(ProjectPathUtils::keyForPath(path));
    }
    
    m_projectPaths.remove(projectId);
    m_projectWatchedDirs.remove(projectId);
    m_dirFiles.remove(projectId);
    m_dirModTimes.remove(projectId);
    m_pendingChangedDirs.remove(projectId);
    m_pendingFullScans.remove(projectId);
    
    LogManager::instance().addLog(
        QString("[ProjectManagerWatcher] Stopped watching project %1 (%2 paths)")
            .arg(projectId).arg(pathsToRemove.size()), "INFO");
    m_scanGenerations[projectId] = m_scanGenerations.value(projectId, 0) + 1;
}

void ProjectManagerWatcher::rescan(int projectId)
{
    m_pendingFullScans.insert(projectId);
    m_debounceTimer.start();
}

QSet<QString> ProjectManagerWatcher::knownFiles(int projectId) const
{
    QSet<QString> allFiles;
    const auto& dirFiles = m_dirFiles.value(projectId);
    for (const auto& files : dirFiles) {
        allFiles.unite(files);
    }
    return allFiles;
}

QStringList ProjectManagerWatcher::scanSingleDirectory(const QString& dirPath)
{
    QStringList files;
    QDir dir(dirPath);
    if (!dir.exists()) return files;

    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        files.append(ProjectPathUtils::cleanPath(fi.absoluteFilePath()));
    }
    return files;
}

ProjectManagerWatcher::DirectoryScanResult ProjectManagerWatcher::scanDirectoryTree(const QString& rootPath)
{
    DirectoryScanResult result;
    result.rootPath = ProjectPathUtils::cleanPath(rootPath);

    const auto availability = FileUtils::checkPathAvailability(result.rootPath, FileUtils::PathAvailabilityMode::DirectoryOnly);
    if (!availability.available)
        return result;

    result.directories.append(result.rootPath);

    QFileInfo rootInfo(result.rootPath);
    result.dirModTimes[result.rootPath] = rootInfo.lastModified();
    const QStringList rootFiles = scanSingleDirectory(result.rootPath);
    result.dirFiles[result.rootPath] = QSet<QString>(rootFiles.begin(), rootFiles.end());

    QDirIterator it(result.rootPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString dirPath = ProjectPathUtils::cleanPath(it.next());
        const auto dirAvailability = FileUtils::checkPathAvailability(dirPath, FileUtils::PathAvailabilityMode::DirectoryOnly);
        if (!dirAvailability.available)
            continue;

        result.directories.append(dirPath);
        QFileInfo dirInfo(dirPath);
        result.dirModTimes[dirPath] = dirInfo.lastModified();
        const QStringList dirFilesList = scanSingleDirectory(dirPath);
        result.dirFiles[dirPath] = QSet<QString>(dirFilesList.begin(), dirFilesList.end());
    }

    return result;
}

void ProjectManagerWatcher::applyInitialScan(int projectId, int generation, const DirectoryScanResult& result)
{
    if (!m_projectPaths.contains(projectId))
        return;
    if (m_scanGenerations.value(projectId) != generation)
        return;
    if (result.rootPath != m_projectPaths.value(projectId))
        return;
    if (result.directories.isEmpty()) {
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Initial scan found no watchable directories for project %1 at: %2")
                .arg(projectId).arg(result.rootPath), "WARN");
        return;
    }

    const QStringList failed = m_watcher->addPaths(result.directories);
    QSet<QString> failedSet(failed.begin(), failed.end());
    QStringList watched;
    watched.reserve(result.directories.size() - failed.size());
    for (const QString& dir : result.directories) {
        if (!failedSet.contains(dir)) {
            watched.append(dir);
            m_pathToProject[ProjectPathUtils::keyForPath(dir)] = projectId;
        }
    }

    if (watched.isEmpty()) {
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Failed to watch: %1").arg(result.rootPath), "ERROR");
        return;
    }

    m_projectWatchedDirs[projectId] = watched;
    m_dirFiles[projectId] = result.dirFiles;
    m_dirModTimes[projectId] = result.dirModTimes;

    int totalFiles = 0;
    for (const auto& files : result.dirFiles) {
        totalFiles += files.size();
    }

    LogManager::instance().addLog(
        QString("[ProjectManagerWatcher] Watching project %1 at: %2 (%3 directories, %4 files)")
            .arg(projectId).arg(result.rootPath).arg(watched.size()).arg(totalFiles), "INFO");
    if (!failed.isEmpty()) {
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Failed to watch %1 directories (first: %2)")
                .arg(failed.size()).arg(failed.first()), "WARN");
    }
}

void ProjectManagerWatcher::onDirectoryChanged(const QString& path)
{
    // Direct lookup since we watch all subdirectories now
    const QString normalizedPath = ProjectPathUtils::cleanPath(path);
    int projectId = m_pathToProject.value(ProjectPathUtils::keyForPath(normalizedPath), -1);
    
    if (projectId < 0) {
        // Fallback: Try to find parent project by checking if path starts with any watched root
        for (auto it = m_projectPaths.begin(); it != m_projectPaths.end(); ++it) {
            if (ProjectPathUtils::isSameOrChildPath(it.value(), normalizedPath)) {
                projectId = it.key();
                // Add this new subdirectory to watch list
                if (m_watcher->addPath(normalizedPath)) {
                    m_pathToProject[ProjectPathUtils::keyForPath(normalizedPath)] = projectId;
                    m_projectWatchedDirs[projectId].append(normalizedPath);
                }
                break;
            }
        }
    }
    
    if (projectId >= 0) {
        // Throttle logging
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastLogTime > 5000) {
            LogManager::instance().addLog(
                QString("[ProjectManagerWatcher] Directory changed: %1 (project %2)").arg(normalizedPath).arg(projectId), "DEBUG");
            m_lastLogTime = now;
        }
        
        // Add to pending changed directories (not full rescan)
        m_pendingChangedDirs[projectId].insert(normalizedPath);
        
        // Start debounce timer if not running
        if (!m_debounceTimer.isActive()) {
            m_debounceTimer.start();
        }
    }
}

void ProjectManagerWatcher::processChangedDirectories(int projectId)
{
    if (!m_projectPaths.contains(projectId)) return;
    
    QSet<QString> changedDirs = m_pendingChangedDirs.take(projectId);
    
    QStringList newFilesList;
    QStringList removedFilesList;
    QSet<int> changedFolders;
    
    // For each changed directory, check if it actually changed
    for (const QString& dirPath : changedDirs) {
        QFileInfo dirInfo(dirPath);
        if (!dirInfo.exists()) {
            // Directory was removed - mark all files as removed
            if (m_dirFiles[projectId].contains(dirPath)) {
                const QSet<QString>& oldFiles = m_dirFiles[projectId][dirPath];
                for (const QString& f : oldFiles) {
                    removedFilesList.append(f);
                }
                m_dirFiles[projectId].remove(dirPath);
                m_dirModTimes[projectId].remove(dirPath);
            }
            continue;
        }
        
        QDateTime currentModTime = dirInfo.lastModified();
        QDateTime cachedModTime = m_dirModTimes[projectId].value(dirPath);
        
        // Skip if modification time hasn't changed (spurious notification)
        if (cachedModTime.isValid() && currentModTime == cachedModTime) {
            continue;
        }
        
        // Directory actually changed - scan it
        QStringList currentFilesList = scanSingleDirectory(dirPath);
        QSet<QString> currentFiles(currentFilesList.begin(), currentFilesList.end());
        QSet<QString> oldFiles = m_dirFiles[projectId].value(dirPath);
        
        // Find new files in this directory
        QSet<QString> newInDir = currentFiles - oldFiles;
        for (const QString& f : newInDir) {
            newFilesList.append(f);
        }
        
        // Find removed files in this directory
        QSet<QString> removedInDir = oldFiles - currentFiles;
        for (const QString& f : removedInDir) {
            removedFilesList.append(f);
        }
        
        // Update cache
        m_dirFiles[projectId][dirPath] = currentFiles;
        m_dirModTimes[projectId][dirPath] = currentModTime;
    }
    
    // Also check for new subdirectories directly under changed directories.
    // This avoids rescanning the full tree on every change.
    for (const QString& dirPath : changedDirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;

        const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& subdirInfo : subdirs) {
            const QString subdirPath = ProjectPathUtils::cleanPath(subdirInfo.absoluteFilePath());
            if (m_dirModTimes[projectId].contains(subdirPath)) {
                continue;
            }

            if (m_watcher->addPath(subdirPath)) {
                m_pathToProject[ProjectPathUtils::keyForPath(subdirPath)] = projectId;
                m_projectWatchedDirs[projectId].append(subdirPath);
            }

            m_dirModTimes[projectId][subdirPath] = subdirInfo.lastModified();
            QStringList dirFilesList = scanSingleDirectory(subdirPath);
            m_dirFiles[projectId][subdirPath] = QSet<QString>(dirFilesList.begin(), dirFilesList.end());

            for (const QString& f : dirFilesList) {
                newFilesList.append(f);
            }
        }
    }
    
    // Process new files
    if (!newFilesList.isEmpty()) {
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] %1 new file(s) in project %2")
                .arg(newFilesList.count()).arg(projectId), "INFO");

        for (const QString& filePath : newFilesList) {
            QFileInfo info(filePath);
            if (!info.exists()) continue;

            const QString dirPath = info.absolutePath();
            int folderId = ProjectDB::instance().ensureFolderForPath(projectId, dirPath);
            if (folderId <= 0) folderId = ProjectDB::instance().getProjectRootFolderId(projectId);
            changedFolders.insert(folderId);

            int assetId = ProjectDB::instance().insertAssetMetadataFast(filePath, folderId);
            ProjectDB::instance().addNotification(projectId, assetId, filePath);
        }

        for (int folderId : std::as_const(changedFolders)) {
            ProjectDB::instance().notifyAssetsChanged(folderId);
        }
        
        emit newFilesDetected(projectId, newFilesList);
    }
    
    // Process removed files
    if (!removedFilesList.isEmpty()) {
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] %1 file(s) removed from project %2")
                .arg(removedFilesList.count()).arg(projectId), "INFO");
        emit filesRemoved(projectId, removedFilesList);
    }
}

void ProjectManagerWatcher::onProcessChanges()
{
    // Process full rescans first (manual trigger)
    for (int projectId : m_pendingFullScans) {
        if (!m_projectPaths.contains(projectId)) continue;
        
        QString watchPath = m_projectPaths[projectId];
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Full rescan for project %1").arg(projectId), "INFO");
        
        const int generation = m_scanGenerations.value(projectId, 0) + 1;
        m_scanGenerations[projectId] = generation;
        const QSet<QString> oldFiles = knownFiles(projectId);
        QtConcurrent::run([watchPath] {
            return ProjectManagerWatcher::scanDirectoryTree(watchPath);
        }).then(this, [this, projectId, generation, oldFiles](const DirectoryScanResult& result) {
            if (!m_projectPaths.contains(projectId) || m_scanGenerations.value(projectId) != generation)
                return;

            if (result.directories.isEmpty()) {
                LogManager::instance().addLog(
                    QString("[ProjectManagerWatcher] Full rescan skipped unavailable project %1 at: %2")
                        .arg(projectId).arg(result.rootPath), "WARN");
                return;
            }

            const QStringList oldWatchedDirs = m_projectWatchedDirs.value(projectId);
            QSet<QString> activeWatchedSet;
            const QStringList activeWatcherDirs = m_watcher->directories();
            for (const QString& dir : activeWatcherDirs) {
                activeWatchedSet.insert(ProjectPathUtils::cleanPath(dir));
            }
            const QSet<QString> newDirSet(result.directories.begin(), result.directories.end());

            QStringList dirsToAdd;
            for (const QString& dir : result.directories) {
                if (!activeWatchedSet.contains(dir))
                    dirsToAdd.append(dir);
            }

            QStringList watched;
            watched.reserve(result.directories.size());
            for (const QString& dir : result.directories) {
                if (activeWatchedSet.contains(dir))
                    watched.append(dir);
            }

            const QStringList failed = dirsToAdd.isEmpty() ? QStringList() : m_watcher->addPaths(dirsToAdd);
            const QSet<QString> failedSet(failed.begin(), failed.end());
            for (const QString& dir : dirsToAdd) {
                if (!failedSet.contains(dir))
                    watched.append(dir);
            }

            if (watched.isEmpty()) {
                LogManager::instance().addLog(
                    QString("[ProjectManagerWatcher] Full rescan kept previous watches because reattach failed for project %1")
                        .arg(projectId), "WARN");
                return;
            }

            QStringList dirsToRemove;
            for (const QString& dir : oldWatchedDirs) {
                if (!newDirSet.contains(dir))
                    dirsToRemove.append(dir);
            }
            if (!dirsToRemove.isEmpty())
                m_watcher->removePaths(dirsToRemove);
            for (const QString& dir : dirsToRemove) {
                m_pathToProject.remove(ProjectPathUtils::keyForPath(dir));
            }
            for (const QString& dir : watched) {
                m_pathToProject[ProjectPathUtils::keyForPath(dir)] = projectId;
            }

            m_projectWatchedDirs[projectId] = watched;
            m_dirFiles[projectId] = result.dirFiles;
            m_dirModTimes[projectId] = result.dirModTimes;
            const QSet<QString> newFiles = knownFiles(projectId);
            const QSet<QString> added = newFiles - oldFiles;
            const QSet<QString> removed = oldFiles - newFiles;

            if (!added.isEmpty()) {
                emit newFilesDetected(projectId, added.values());
            }
            if (!removed.isEmpty()) {
                emit filesRemoved(projectId, removed.values());
            }
        });
        continue;
        
    }
    m_pendingFullScans.clear();
    
    // Process changed directories (incremental updates)
    QList<int> projectIds = m_pendingChangedDirs.keys();
    for (int projectId : projectIds) {
        processChangedDirectories(projectId);
    }
}
