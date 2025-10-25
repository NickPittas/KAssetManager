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

    // Asset ops
    int upsertAsset(const QString& filePath);
    bool setAssetFolder(int assetId, int folderId);
    bool removeAssets(const QList<int>& assetIds);
    bool setAssetsRating(const QList<int>& assetIds, int rating); // 0-5, -1 to clear
    QList<int> getAssetIdsInFolder(int folderId, bool recursive = true) const;

    // Tags ops
    int createTag(const QString& name);
    bool renameTag(int id, const QString& name);
    bool deleteTag(int id);
    QVector<QPair<int, QString>> listTags() const;
    bool assignTagsToAssets(const QList<int>& assetIds, const QList<int>& tagIds);
    QStringList tagsForAsset(int assetId) const;

signals:
    void foldersChanged();
    void assetsChanged(int folderId);
    void tagsChanged();

private:
    explicit DB(QObject* parent=nullptr);
    bool migrate();
    bool exec(const QString& sql);
    bool hasColumn(const QString& table, const QString& column) const;

    QSqlDatabase m_db;
    int m_rootId = 0;
};

