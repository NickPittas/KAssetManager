#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QString>
#include <QTimer>
#include <QSet>

class ProjectFolderWatcher : public QObject
{
    Q_OBJECT

public:
    explicit ProjectFolderWatcher(QObject* parent = nullptr);
    ~ProjectFolderWatcher();

    // Add a project folder to watch
    void addProjectFolder(int projectFolderId, const QString& path);
    
    // Remove a project folder from watching
    void removeProjectFolder(int projectFolderId);
    
    // Remove all watched folders
    void clear();
    
    // Manually trigger a refresh for a specific project folder
    void refreshProjectFolder(int projectFolderId);

signals:
    // Emitted when changes are detected in a project folder
    void projectFolderChanged(int projectFolderId, const QString& path);

private slots:
    void onDirectoryChanged(const QString& path);
    void onFileChanged(const QString& path);
    void onRefreshTimeout();

private:
    QFileSystemWatcher* m_watcher;
    QHash<QString, int> m_pathToProjectId; // Maps watched paths to project folder IDs
    QHash<int, QString> m_projectIdToPath; // Maps project folder IDs to paths
    
    // Debounce timer to avoid too many rapid refreshes
    QTimer* m_refreshTimer;
    QSet<int> m_pendingRefreshes;
};

