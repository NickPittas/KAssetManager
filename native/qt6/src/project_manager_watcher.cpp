#include "project_manager_watcher.h"
#include "project_db.h"
#include "project_path_utils.h"
#include "project_version_detector.h"
#include "log_manager.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

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
    if (watchPath.isEmpty() || !QDir(watchPath).exists()) {
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Cannot watch invalid path: %1").arg(watchPath), "WARN");
        return;
    }
    
    // Unwatch previous path if any
    unwatchProject(projectId);
    
    // Store mapping
    const QString normalizedWatchPath = ProjectPathUtils::cleanPath(watchPath);
    m_projectPaths[projectId] = normalizedWatchPath;
    m_pathToProject[ProjectPathUtils::keyForPath(normalizedWatchPath)] = projectId;
    
    // Build the list of all directories to watch (recursive)
    QStringList dirsToWatch;
    dirsToWatch.append(normalizedWatchPath);
    
    QDirIterator it(normalizedWatchPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        dirsToWatch.append(it.next());
    }
    
    // Watch all directories (QFileSystemWatcher only watches specific dirs, not recursively)
    const QStringList failed = m_watcher->addPaths(dirsToWatch);
    QSet<QString> failedSet(failed.begin(), failed.end());
    QStringList watched;
    watched.reserve(dirsToWatch.size() - failed.size());
    for (const QString& dir : dirsToWatch) {
        if (!failedSet.contains(dir)) {
            watched.append(dir);
        }
    }

    if (!watched.isEmpty()) {
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Watching project %1 at: %2 (%3 directories)")
                .arg(projectId).arg(watchPath).arg(watched.size()), "INFO");
        
        // Map all watched directories to this project
        for (const QString& dir : watched) {
            m_pathToProject[ProjectPathUtils::keyForPath(dir)] = projectId;
        }
        m_projectWatchedDirs[projectId] = watched;
        
        // Build initial directory cache (stores mod times and file lists per directory)
        buildDirectoryCache(projectId, normalizedWatchPath);
        
        int totalFiles = 0;
        const auto& dirFiles = m_dirFiles[projectId];
        for (const auto& files : dirFiles) {
            totalFiles += files.size();
        }
        
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Initial scan cached %1 directories, %2 files")
                .arg(dirFiles.size()).arg(totalFiles), "DEBUG");
        if (!failed.isEmpty()) {
            LogManager::instance().addLog(
                QString("[ProjectManagerWatcher] Failed to watch %1 directories (first: %2)")
                    .arg(failed.size()).arg(failed.first()), "WARN");
        }
    } else {
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Failed to watch: %1").arg(watchPath), "ERROR");
    }
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

QStringList ProjectManagerWatcher::scanSingleDirectory(const QString& dirPath) const
{
    QStringList files;
    QDir dir(dirPath);
    if (!dir.exists()) return files;
    
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        files.append(fi.absoluteFilePath());
    }
    return files;
}

void ProjectManagerWatcher::buildDirectoryCache(int projectId, const QString& rootPath)
{
    m_dirFiles[projectId].clear();
    m_dirModTimes[projectId].clear();
    
    // Iterate all directories
    QDirIterator it(rootPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    
    // Process root directory first
    {
        QFileInfo rootInfo(rootPath);
        const QString normalizedRootPath = ProjectPathUtils::cleanPath(rootPath);
        m_dirModTimes[projectId][normalizedRootPath] = rootInfo.lastModified();
        QStringList rootFiles = scanSingleDirectory(normalizedRootPath);
        m_dirFiles[projectId][normalizedRootPath] = QSet<QString>(rootFiles.begin(), rootFiles.end());
    }
    
    // Process subdirectories
    while (it.hasNext()) {
        QString dirPath = ProjectPathUtils::cleanPath(it.next());
        QFileInfo dirInfo(dirPath);
        
        // Store modification time
        m_dirModTimes[projectId][dirPath] = dirInfo.lastModified();
        
        // Scan files in this directory only (non-recursive)
        QStringList dirFilesList = scanSingleDirectory(dirPath);
        m_dirFiles[projectId][dirPath] = QSet<QString>(dirFilesList.begin(), dirFilesList.end());
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
        
        // Rebuild entire cache
        QSet<QString> oldFiles = knownFiles(projectId);
        buildDirectoryCache(projectId, watchPath);
        QSet<QString> newFiles = knownFiles(projectId);
        
        QSet<QString> added = newFiles - oldFiles;
        QSet<QString> removed = oldFiles - newFiles;
        
        if (!added.isEmpty()) {
            QStringList addedList = added.values();
            emit newFilesDetected(projectId, addedList);
        }
        if (!removed.isEmpty()) {
            QStringList removedList = removed.values();
            emit filesRemoved(projectId, removedList);
        }
    }
    m_pendingFullScans.clear();
    
    // Process changed directories (incremental updates)
    QList<int> projectIds = m_pendingChangedDirs.keys();
    for (int projectId : projectIds) {
        processChangedDirectories(projectId);
    }
}
