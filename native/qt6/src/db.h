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

#include <QHash>

#include "i_asset_database.h"

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

class DB : public QObject, public IAssetDatabase {
    Q_OBJECT
public:
    static DB& instance();

    // Initialize SQLite DB in given path (directory is created by caller if needed)
    bool init(const QString& dbFilePath);

    // IAssetDatabase interface implementation
    QSqlDatabase database() const override { return m_db; }
    int ensureRootFolder() override;
    int createFolder(const QString& name, int parentId) override;
    int insertAssetMetadataFast(const QString& filePath, int folderId) override;
    int upsertSequenceInFolderFast(const QString& sequencePattern, int startFrame, int endFrame, int frameCount, const QString& firstFramePath, int folderId, bool hasGaps = false, int gapCount = 0, const QString& version = QString()) override;
    void notifyAssetsChanged(int folderId) override;

    // Folder ops (additional, non-interface)
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
    bool setAssetFolder(int assetId, int folderId);
    bool removeAssets(const QList<int>& assetIds);
    bool setAssetsRating(const QList<int>& assetIds, int rating); // 0-5, -1 to clear
    bool updateAssetPath(int assetId, const QString& newPath);
    QList<int> getAssetIdsInFolder(int folderId, bool recursive = true) const;
    QString getAssetFilePath(int assetId) const;

    // Versioning ops
    int getAssetIdByPath(const QString& filePath) const;
    QVector<AssetVersionRow> listAssetVersions(int assetId) const;
    int createAssetVersion(int assetId, const QString& srcFilePath, const QString& notes = QString(), const QString& precomputedChecksum = QString());
    bool revertAssetToVersion(int assetId, int versionId, bool createBackupVersion);

    // Tags ops
    int createTag(const QString& name);
    bool renameTag(int id, const QString& name);
    bool deleteTag(int id);
    bool mergeTags(int sourceTagId, int targetTagId);
    QVector<QPair<int, QString>> listTags() const;
    QHash<int, QStringList> tagsForAssets(const QList<int>& assetIds) const;

    bool assignTagsToAssets(const QList<int>& assetIds, const QList<int>& tagIds);
    QStringList tagsForAsset(int assetId) const;

    // Database management
    bool exportDatabase(const QString& filePath);
    bool importDatabase(const QString& filePath);
    bool clearAllData();

    // Explicit notification helpers (safe wrappers for emitting signals)
    // notifyAssetsChanged is declared above with override (from IAssetDatabase)
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

    // Schema versioning helpers (SQLite PRAGMA user_version)
    int schemaUserVersion() const;
    bool setSchemaUserVersion(int v);

    // Schedule background checksum computation and apply results on DB thread
    void scheduleChecksumJob(int assetId,
                             const QString& filePath,
                             qint64 newSize,
                             const QString& oldChecksum,
                             bool isNewAsset,
                             const QString& versionNotes);

private slots:
    void applyChecksumUpdate(int assetId,
                             const QString& filePath,
                             qint64 newSize,
                             const QString& newChecksum,
                             const QString& oldChecksum,
                             bool isNewAsset,
                             const QString& versionNotes);


private:


    QSqlDatabase m_db;
    int m_rootId = 0;
    QString m_dataDir; // directory that holds the DB; used for version storage

};
