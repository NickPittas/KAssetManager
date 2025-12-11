#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QSet>
#include <QStringList>
#include <QDateTime>

/**
 * @brief Watches project folders for new files and manages notifications.
 * 
 * This is specifically for the Project Manager feature, separate from the
 * Asset Manager's ProjectFolderWatcher.
 * 
 * Uses smart caching to avoid re-scanning unchanged directories:
 * - Caches directory modification times
 * - Only scans directories that have actually changed
 * - Tracks pending changed directories instead of full rescans
 */
class ProjectManagerWatcher : public QObject
{
    Q_OBJECT

public:
    explicit ProjectManagerWatcher(QObject* parent = nullptr);
    ~ProjectManagerWatcher();

    /**
     * @brief Start watching a project's folder.
     * @param projectId The project's database ID
     * @param watchPath The folder path to watch
     */
    void watchProject(int projectId, const QString& watchPath);

    /**
     * @brief Stop watching a project's folder.
     * @param projectId The project's database ID
     */
    void unwatchProject(int projectId);

    /**
     * @brief Re-scan a project folder for changes (full rescan).
     * @param projectId The project's database ID
     */
    void rescan(int projectId);

    /**
     * @brief Get the list of files known for a project.
     */
    QSet<QString> knownFiles(int projectId) const;

signals:
    /**
     * @brief Emitted when new files are detected in a project.
     * @param projectId The project's database ID
     * @param newFiles List of new file paths detected
     */
    void newFilesDetected(int projectId, const QStringList& newFiles);

    /**
     * @brief Emitted when files are removed from a project folder.
     * @param projectId The project's database ID
     * @param removedFiles List of removed file paths
     */
    void filesRemoved(int projectId, const QStringList& removedFiles);

private slots:
    void onDirectoryChanged(const QString& path);
    void onProcessChanges();

private:
    // Scan a single directory (non-recursive) - returns files in that dir only
    QStringList scanSingleDirectory(const QString& dirPath) const;
    
    // Build initial cache with directory mod times
    void buildDirectoryCache(int projectId, const QString& rootPath);
    
    // Process only changed directories efficiently
    void processChangedDirectories(int projectId);

    QFileSystemWatcher* m_watcher;
    
    // Project ID -> root watch path
    QHash<int, QString> m_projectPaths;
    
    // Watch path -> project ID (for quick lookup)
    QHash<QString, int> m_pathToProject;
    
    // Project ID -> directory path -> set of files in that directory
    QHash<int, QHash<QString, QSet<QString>>> m_dirFiles;
    
    // Project ID -> directory path -> last modification time
    QHash<int, QHash<QString, QDateTime>> m_dirModTimes;
    
    // Pending changed directories per project
    QHash<int, QSet<QString>> m_pendingChangedDirs;
    
    // Debounce timer
    QTimer m_debounceTimer;
    
    // Full rescan requests (manual trigger)
    QSet<int> m_pendingFullScans;
    
    // Throttle logging to prevent spam
    qint64 m_lastLogTime = 0;
};
