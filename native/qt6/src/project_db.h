#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <QVector>
#include <QHash>
#include <QStringList>
#include <QDateTime>

#include "i_asset_database.h"

/**
 * @brief Notification entry for new files detected in a project.
 */
struct ProjectNotification {
    int id = 0;
    int projectId = 0;
    int assetId = 0;
    QString filePath;
    QString detectedAt;     // ISO timestamp
    bool acknowledged = false;
};

/**
 * @brief Project entry.
 */
struct Project {
    int id = 0;
    QString name;
    QString watchPath;
    QString createdAt;
    QString updatedAt;
};

/**
 * @brief Separate SQLite database for Project Manager.
 * 
 * This database is completely separate from the Asset Manager's database.
 * It stores projects, their assets, and persistent notifications.
 * 
 * Implements IAssetDatabase to allow sharing the Importer class.
 */
class ProjectDB : public QObject, public IAssetDatabase {
    Q_OBJECT
public:
    static ProjectDB& instance();

    /**
     * @brief Initialize the project database.
     * @param dbFilePath Path to the SQLite database file (e.g., projects.db)
     * @return true on success
     */
    bool init(const QString& dbFilePath);

    // IAssetDatabase interface implementation
    QSqlDatabase database() const override { return m_db; }
    int ensureRootFolder() override;
    int createFolder(const QString& name, int parentId) override;
    int insertAssetMetadataFast(const QString& filePath, int folderId) override;
    int upsertSequenceInFolderFast(const QString& sequencePattern, int startFrame, int endFrame, 
                                   int frameCount, const QString& firstFramePath, int folderId, 
                                   bool hasGaps = false, int gapCount = 0, 
                                   const QString& version = QString()) override;
    void notifyAssetsChanged(int folderId) override;

    // Project operations
    int createProject(const QString& name, const QString& watchPath);
    bool renameProject(int id, const QString& name);
    bool deleteProject(int id);
    bool updateProjectWatchPath(int id, const QString& watchPath);
    QVector<Project> listProjects() const;
    Project getProject(int id) const;
    int getProjectIdByFolderId(int folderId) const;
    int getProjectRootFolderId(int projectId) const;

    // Version grouping support
    bool setAssetVersionInfo(int assetId, const QString& versionGroupKey, const QString& versionString);
    QVector<QPair<int, QString>> getVersionsForGroup(const QString& versionGroupKey, int projectId) const;

    // Notification operations (persistent across restarts)
    int addNotification(int projectId, int assetId, const QString& filePath);
    bool acknowledgeNotification(int notificationId);
    bool acknowledgeAllNotifications(int projectId = -1);
    bool markNotificationsRead(int projectId = -1);
    QVector<ProjectNotification> getUnacknowledgedNotifications(int projectId = -1) const;
    int getUnacknowledgedCount(int projectId = -1) const;
    int getUnreadNotificationCount(int projectId = -1) const;

    // Asset operations
    QList<int> getAssetIdsInProject(int projectId) const;
    QList<int> getAssetIdsInFolder(int folderId, bool recursive = true) const;
    int ensureFolderForPath(int projectId, const QString& absolutePath);
    QString getAssetFilePath(int assetId) const;
    int getAssetFolderId(int assetId) const;
    bool removeAssets(const QList<int>& assetIds);
    int removeAssetsByPath(const QStringList& filePaths);  // Remove assets by file path
    bool updateAssetPath(const QString& oldPath, const QString& newPath);  // Update asset file path
    int resyncAssetFolders(int projectId);  // Fix folder associations based on file paths

signals:
    void projectsChanged();
    void projectAssetsChanged(int projectId);
    void notificationsChanged();
    void projectFoldersChanged(int projectId);

private:
    explicit ProjectDB(QObject* parent = nullptr);
    bool migrate();
    bool exec(const QString& sql);
    bool hasColumn(const QString& table, const QString& column) const;
    int ensureChildFolder(int parentId, const QString& name, int projectId);

    QSqlDatabase m_db;
    int m_rootId = 0;
    
    // Map folder IDs to project IDs for efficient lookup
    mutable QHash<int, int> m_folderToProject;
};
