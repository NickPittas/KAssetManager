#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>
#include <QVector>
#include <QPair>
#include <QStringList>

// Version history row for an asset
struct AssetVersionRow {
    int id = 0;
    int assetId = 0;
    int versionNumber = 0;      // 1-based
    QString versionName;        // e.g., "v1"
    QString filePath;           // path to stored version copy
    qint64 fileSize = 0;
    QString checksum;           // SHA-256
    QString createdAt;          // ISO timestamp
    QString notes;              // optional user notes
};

class DB : public QObject {
    Q_OBJECT
public:
    static DB& instance();

    // Initialize SQLite DB in given path (directory is created by caller if needed)
    bool init(const QString& dbFilePath);

    QSqlDatabase database() const { return m_db; }

    // Folder ops
    int ensureRootFolder();
    int createFolder(const QString& name, int parentId);
    bool renameFolder(int id, const QString& name);
    bool deleteFolder(int id);
    bool moveFolder(int id, int newParentId);

    // Project folder ops (watched folders)
    int createProjectFolder(const QString& name, const QString& path);
    bool renameProjectFolder(int id, const QString& name);
    bool deleteProjectFolder(int id);
    QVector<QPair<int, QPair<QString, QString>>> listProjectFolders() const; // Returns (id, (name, path))
    QString getProjectFolderPath(int id) const;
    int getProjectFolderIdByVirtualFolderId(int virtualFolderId) const;

    // Asset ops
    int upsertAsset(const QString& filePath);
    int upsertSequence(const QString& sequencePattern, int startFrame, int endFrame, int frameCount, const QString& firstFramePath);
    // Fast path for bulk imports: metadata only (no checksum, no versioning, no signals)
    int insertAssetMetadataFast(const QString& filePath, int folderId);
    // Fast path for image sequences during bulk import (no signals)
    int upsertSequenceInFolderFast(const QString& sequencePattern, int startFrame, int endFrame, int frameCount, const QString& firstFramePath, int folderId, bool hasGaps = false, int gapCount = 0, const QString& version = QString());
    bool setAssetFolder(int assetId, int folderId);
    bool removeAssets(const QList<int>& assetIds);
    bool setAssetsRating(const QList<int>& assetIds, int rating); // 0-5, -1 to clear
    bool updateAssetPath(int assetId, const QString& newPath);
    QList<int> getAssetIdsInFolder(int folderId, bool recursive = true) const;
    QString getAssetFilePath(int assetId) const;

    // Versioning ops
    int getAssetIdByPath(const QString& filePath) const;
    QVector<AssetVersionRow> listAssetVersions(int assetId) const;
    int createAssetVersion(int assetId, const QString& srcFilePath, const QString& notes = QString());
    bool revertAssetToVersion(int assetId, int versionId, bool createBackupVersion);

    // Tags ops
    int createTag(const QString& name);
    bool renameTag(int id, const QString& name);
    bool deleteTag(int id);
    bool mergeTags(int sourceTagId, int targetTagId);
    QVector<QPair<int, QString>> listTags() const;
    bool assignTagsToAssets(const QList<int>& assetIds, const QList<int>& tagIds);
    QStringList tagsForAsset(int assetId) const;

    // Database management
    bool exportDatabase(const QString& filePath);
    bool importDatabase(const QString& filePath);
    bool clearAllData();

    // Explicit notification helpers (safe wrappers for emitting signals)
    void notifyAssetsChanged(int folderId);
    void notifyFoldersChanged();
    void notifyTagsChanged();
    void notifyProjectFoldersChanged();
    void notifyAssetVersionsChanged(int assetId);

signals:
    void foldersChanged();
    void assetsChanged(int folderId);
    void tagsChanged();
    void projectFoldersChanged();
    void assetVersionsChanged(int assetId);

private:
    explicit DB(QObject* parent=nullptr);
    bool migrate();
    bool exec(const QString& sql);
    bool hasColumn(const QString& table, const QString& column) const;

    QSqlDatabase m_db;
    int m_rootId = 0;
    QString m_dataDir; // directory that holds the DB; used for version storage
};

