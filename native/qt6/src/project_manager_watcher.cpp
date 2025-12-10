#include "project_manager_watcher.h"
#include "project_db.h"
#include "project_version_detector.h"
#include "log_manager.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>

ProjectManagerWatcher::ProjectManagerWatcher(QObject* parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &ProjectManagerWatcher::onDirectoryChanged);
    
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(1000); // 1 second debounce
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
    m_projectPaths[projectId] = watchPath;
    m_pathToProject[watchPath] = projectId;
    
    // Add to watcher
    if (m_watcher->addPath(watchPath)) {
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Watching project %1 at: %2").arg(projectId).arg(watchPath), "INFO");
        
        // Also watch subdirectories
        QDirIterator it(watchPath, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString subDir = it.next();
            if (m_watcher->addPath(subDir)) {
                m_pathToProject[subDir] = projectId;
            }
        }
        
        // Initial scan to build known files list
        QStringList initialFilesList = scanDirectory(watchPath);
        QSet<QString> initialFiles(initialFilesList.begin(), initialFilesList.end());
        m_knownFiles[projectId] = initialFiles;
        
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Initial scan found %1 files").arg(initialFiles.count()), "DEBUG");
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
    
    QString watchPath = m_projectPaths[projectId];
    
    // Remove all paths for this project
    QStringList toRemove;
    for (auto it = m_pathToProject.begin(); it != m_pathToProject.end(); ++it) {
        if (it.value() == projectId) {
            toRemove.append(it.key());
        }
    }
    
    for (const QString& path : toRemove) {
        m_watcher->removePath(path);
        m_pathToProject.remove(path);
    }
    
    m_projectPaths.remove(projectId);
    m_knownFiles.remove(projectId);
    m_pendingScans.remove(projectId);
    
    LogManager::instance().addLog(
        QString("[ProjectManagerWatcher] Stopped watching project %1").arg(projectId), "INFO");
}

void ProjectManagerWatcher::rescan(int projectId)
{
    m_pendingScans.insert(projectId);
    m_debounceTimer.start();
}

QSet<QString> ProjectManagerWatcher::knownFiles(int projectId) const
{
    return m_knownFiles.value(projectId);
}

QStringList ProjectManagerWatcher::scanDirectory(const QString& path) const
{
    QStringList files;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    
    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fi(filePath);
        QString suffix = fi.suffix().toLower();
        
        // Filter to relevant file types (images, videos, project files)
        static const QSet<QString> supportedExtensions = {
            // Images
            "jpg", "jpeg", "png", "tif", "tiff", "exr", "dpx", "bmp", "gif",
            // Videos
            "mov", "mp4", "avi", "mkv", "mxf", "r3d",
            // Project files
            "aep", "aepx", "nk"
        };
        
        if (supportedExtensions.contains(suffix)) {
            files.append(filePath);
        }
    }
    
    return files;
}

void ProjectManagerWatcher::onDirectoryChanged(const QString& path)
{
    int projectId = m_pathToProject.value(path, -1);
    if (projectId < 0) {
        // Try to find parent project
        for (auto it = m_projectPaths.begin(); it != m_projectPaths.end(); ++it) {
            if (path.startsWith(it.value())) {
                projectId = it.key();
                break;
            }
        }
    }
    
    if (projectId >= 0) {
        LogManager::instance().addLog(
            QString("[ProjectManagerWatcher] Directory changed: %1 (project %2)").arg(path).arg(projectId), "DEBUG");
        m_pendingScans.insert(projectId);
        m_debounceTimer.start();
        
        // Check for new subdirectories to watch
        if (QDir(path).exists()) {
            QDir d(path);
            const QFileInfoList subdirs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo& fi : subdirs) {
                const QString subDir = fi.absoluteFilePath();
                if (!m_pathToProject.contains(subDir)) {
                    if (m_watcher->addPath(subDir)) {
                        m_pathToProject[subDir] = projectId;
                    }
                }
            }
        }
    }
}

void ProjectManagerWatcher::onProcessChanges()
{
    for (int projectId : m_pendingScans) {
        if (!m_projectPaths.contains(projectId)) continue;
        
        QString watchPath = m_projectPaths[projectId];
        QStringList currentFilesList = scanDirectory(watchPath);
        QSet<QString> currentFiles(currentFilesList.begin(), currentFilesList.end());
        QSet<QString> knownFiles = m_knownFiles.value(projectId);
        
        // Find new files
        QSet<QString> newFiles = currentFiles - knownFiles;
        
        // Find removed files
        QSet<QString> removedFiles = knownFiles - currentFiles;
        
        if (!newFiles.isEmpty()) {
            QStringList newFilesList = newFiles.values();
            LogManager::instance().addLog(
                QString("[ProjectManagerWatcher] %1 new file(s) in project %2")
                    .arg(newFilesList.count()).arg(projectId), "INFO");

            QSet<int> changedFolders;
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
        
        if (!removedFiles.isEmpty()) {
            QStringList removedList = removedFiles.values();
            LogManager::instance().addLog(
                QString("[ProjectManagerWatcher] %1 file(s) removed from project %2")
                    .arg(removedList.count()).arg(projectId), "INFO");
            emit filesRemoved(projectId, removedList);
        }
        
        // Update known files
        m_knownFiles[projectId] = currentFiles;
    }
    
    m_pendingScans.clear();
}
