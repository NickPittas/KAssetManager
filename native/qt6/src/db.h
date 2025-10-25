#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QFileInfo>

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

signals:
    void foldersChanged();
    void assetsChanged(int folderId);

private:
    explicit DB(QObject* parent=nullptr);
    bool migrate();
    bool exec(const QString& sql);

    QSqlDatabase m_db;
    int m_rootId = 0;
};

