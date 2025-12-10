#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QSet>
#include <QStringList>

/**
 * @brief Watches project folders for new files and manages notifications.
 * 
 * This is specifically for the Project Manager feature, separate from the
 * Asset Manager's ProjectFolderWatcher.
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
     * @brief Re-scan a project folder for changes.
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
    QStringList scanDirectory(const QString& path) const;
    void updateKnownFiles(int projectId, const QSet<QString>& files);

    QFileSystemWatcher* m_watcher;
    
    // Project ID -> watch path
    QHash<int, QString> m_projectPaths;
    
    // Watch path -> project ID
    QHash<QString, int> m_pathToProject;
    
    // Project ID -> known files
    QHash<int, QSet<QString>> m_knownFiles;
    
    // Debounce timer
    QTimer m_debounceTimer;
    QSet<int> m_pendingScans;
};
