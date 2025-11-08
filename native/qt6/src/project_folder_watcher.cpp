#include "project_folder_watcher.h"
#include <QDebug>
#include <QDir>
#include <QDirIterator>

ProjectFolderWatcher::ProjectFolderWatcher(QObject* parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
    , m_refreshTimer(new QTimer(this))
{
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, 
            this, &ProjectFolderWatcher::onDirectoryChanged);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, 
            this, &ProjectFolderWatcher::onFileChanged);
    
    // Set up debounce timer (500ms delay)
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(500);
    connect(m_refreshTimer, &QTimer::timeout, 
            this, &ProjectFolderWatcher::onRefreshTimeout);
}

ProjectFolderWatcher::~ProjectFolderWatcher()
{
    clear();
}

void ProjectFolderWatcher::addProjectFolder(int projectFolderId, const QString& path)
{
    qDebug() << "ProjectFolderWatcher::addProjectFolder" << projectFolderId << path;
    
    if (!QDir(path).exists()) {
        qWarning() << "ProjectFolderWatcher: Path does not exist:" << path;
        return;
    }
    
    // Remove old path if this project was already being watched
    if (m_projectIdToPath.contains(projectFolderId)) {
        QString oldPath = m_projectIdToPath[projectFolderId];
        m_watcher->removePath(oldPath);
        m_pathToProjectId.remove(oldPath);
    }
    
    // Add the main folder
    if (m_watcher->addPath(path)) {
        m_pathToProjectId[path] = projectFolderId;
        m_projectIdToPath[projectFolderId] = path;
        qDebug() << "ProjectFolderWatcher: Now watching" << path;
        
        // Also watch all subdirectories
        QDirIterator it(path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString subDir = it.next();
            if (m_watcher->addPath(subDir)) {
                m_pathToProjectId[subDir] = projectFolderId;
                qDebug() << "ProjectFolderWatcher: Also watching subdirectory" << subDir;
            }
        }
    } else {
        qWarning() << "ProjectFolderWatcher: Failed to watch" << path;
    }
}

void ProjectFolderWatcher::removeProjectFolder(int projectFolderId)
{
    qDebug() << "ProjectFolderWatcher::removeProjectFolder" << projectFolderId;
    
    if (!m_projectIdToPath.contains(projectFolderId)) {
        return;
    }
    
    QString path = m_projectIdToPath[projectFolderId];
    
    // Remove all paths associated with this project
    QStringList pathsToRemove;
    for (auto it = m_pathToProjectId.begin(); it != m_pathToProjectId.end(); ++it) {
        if (it.value() == projectFolderId) {
            pathsToRemove.append(it.key());
        }
    }
    
    for (const QString& p : pathsToRemove) {
        m_watcher->removePath(p);
        m_pathToProjectId.remove(p);
    }
    
    m_projectIdToPath.remove(projectFolderId);
    m_pendingRefreshes.remove(projectFolderId);
}

void ProjectFolderWatcher::clear()
{
    qDebug() << "ProjectFolderWatcher::clear";
    
    if (!m_watcher->directories().isEmpty()) {
        m_watcher->removePaths(m_watcher->directories());
    }
    if (!m_watcher->files().isEmpty()) {
        m_watcher->removePaths(m_watcher->files());
    }
    
    m_pathToProjectId.clear();
    m_projectIdToPath.clear();
    m_pendingRefreshes.clear();
}

void ProjectFolderWatcher::refreshProjectFolder(int projectFolderId)
{
    qDebug() << "ProjectFolderWatcher::refreshProjectFolder" << projectFolderId;
    
    if (!m_projectIdToPath.contains(projectFolderId)) {
        qWarning() << "ProjectFolderWatcher: Unknown project folder ID" << projectFolderId;
        return;
    }
    
    QString path = m_projectIdToPath[projectFolderId];
    emit projectFolderChanged(projectFolderId, path);
}

void ProjectFolderWatcher::onDirectoryChanged(const QString& path)
{
    qDebug() << "ProjectFolderWatcher::onDirectoryChanged" << path;
    
    if (!m_pathToProjectId.contains(path)) {
        return;
    }
    
    int projectFolderId = m_pathToProjectId[path];
    
    // Check if new subdirectories were added (only immediate children)
    if (QDir(path).exists()) {
        QDir d(path);
        const QFileInfoList subdirs = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& fi : subdirs) {
            const QString subDir = fi.absoluteFilePath();
            if (!m_pathToProjectId.contains(subDir)) {
                if (m_watcher->addPath(subDir)) {
                    m_pathToProjectId[subDir] = projectFolderId;
                    qDebug() << "ProjectFolderWatcher: Started watching new subdirectory" << subDir;
                }
            }
        }
    }

    // Add to pending refreshes and start/restart timer
    m_pendingRefreshes.insert(projectFolderId);
    m_refreshTimer->start();
}

void ProjectFolderWatcher::onFileChanged(const QString& path)
{
    qDebug() << "ProjectFolderWatcher::onFileChanged" << path;
    
    // Find which project this file belongs to
    QFileInfo fileInfo(path);
    QString dirPath = fileInfo.absolutePath();
    
    // Walk up the directory tree to find a watched folder
    while (!dirPath.isEmpty()) {
        if (m_pathToProjectId.contains(dirPath)) {
            int projectFolderId = m_pathToProjectId[dirPath];
            m_pendingRefreshes.insert(projectFolderId);
            m_refreshTimer->start();
            break;
        }
        
        QDir dir(dirPath);
        if (!dir.cdUp()) {
            break;
        }
        dirPath = dir.absolutePath();
    }
}

void ProjectFolderWatcher::onRefreshTimeout()
{
    qDebug() << "ProjectFolderWatcher::onRefreshTimeout - Processing" << m_pendingRefreshes.size() << "pending refreshes";
    
    for (int projectFolderId : m_pendingRefreshes) {
        if (m_projectIdToPath.contains(projectFolderId)) {
            QString path = m_projectIdToPath[projectFolderId];
            qDebug() << "ProjectFolderWatcher: Emitting change signal for project" << projectFolderId << path;
            emit projectFolderChanged(projectFolderId, path);
        }
    }
    
    m_pendingRefreshes.clear();
}

