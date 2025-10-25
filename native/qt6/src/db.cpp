#include "db.h"
#include <QDir>
#include <QDateTime>
#include <QDebug>

static QString lastErrorToString(const QSqlQuery& q){ return q.lastError().text(); }

DB& DB::instance(){ static DB s; return s; }

DB::DB(QObject* parent): QObject(parent) {}

bool DB::init(const QString& dbFilePath){
    if (m_db.isValid()) return true;
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dbFilePath);
    if (!m_db.open()) {
        qWarning() << "DB open failed:" << m_db.lastError();
        return false;
    }
    if (!migrate()) return false;
    m_rootId = ensureRootFolder();
    return m_rootId > 0;
}

bool DB::migrate(){
    // Minimal schema for virtual folders and assets + tags/ratings
    const char* ddl[] = {
        "PRAGMA foreign_keys=ON;",
        "CREATE TABLE IF NOT EXISTS virtual_folders (\n"
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "  name TEXT NOT NULL,\n"
        "  parent_id INTEGER NULL REFERENCES virtual_folders(id) ON DELETE CASCADE,\n"
        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP,\n"
        "  updated_at TEXT DEFAULT CURRENT_TIMESTAMP\n"
        ");",
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_virtual_folders_parent_name ON virtual_folders(parent_id, name);",
        "CREATE TABLE IF NOT EXISTS assets (\n"
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "  file_path TEXT NOT NULL UNIQUE,\n"
        "  file_name TEXT NOT NULL,\n"
        "  virtual_folder_id INTEGER NOT NULL REFERENCES virtual_folders(id) ON DELETE SET DEFAULT DEFAULT 1,\n"
        "  file_size INTEGER NULL,\n"
        "  mime_type TEXT NULL,\n"
        "  checksum TEXT NULL,\n"
        "  rating INTEGER NULL,\n"
        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP,\n"
        "  updated_at TEXT DEFAULT CURRENT_TIMESTAMP\n"
        ");",
        "CREATE INDEX IF NOT EXISTS idx_assets_folder ON assets(virtual_folder_id);",
        // tags
        "CREATE TABLE IF NOT EXISTS tags (\n"
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "  name TEXT NOT NULL UNIQUE\n"
        ");",
        "CREATE TABLE IF NOT EXISTS asset_tags (\n"
        "  asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,\n"
        "  tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,\n"
        "  PRIMARY KEY (asset_id, tag_id)\n"
        ");"
    };
    for (const char* sql : ddl) if (!exec(QString::fromLatin1(sql))) return false;

    // If older DB lacked 'rating' column, add it
    if (!hasColumn("assets", "rating")) {
        exec("ALTER TABLE assets ADD COLUMN rating INTEGER NULL");
    }
    return true;
}

bool DB::exec(const QString& sql){ QSqlQuery q(m_db); if (!q.exec(sql)) { qWarning() << "SQL failed:" << sql << q.lastError(); return false; } return true; }

bool DB::hasColumn(const QString& table, const QString& column) const {
    QSqlQuery q(m_db);
    q.prepare("PRAGMA table_info(" + table + ")");
    if (!q.exec()) return false;
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

int DB::ensureRootFolder(){
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id FROM virtual_folders WHERE parent_id IS NULL AND name='Root' LIMIT 1")) return 0;
    if (q.next()) return q.value(0).toInt();
    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO virtual_folders(name,parent_id) VALUES('Root',NULL)");
    if (!ins.exec()) { qWarning() << ins.lastError(); return 0; }
    return ins.lastInsertId().toInt();
}

int DB::createFolder(const QString& name, int parentId){
    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO virtual_folders(name,parent_id) VALUES(?,?)");
    ins.addBindValue(name);
    if (parentId<=0) parentId = m_rootId; ins.addBindValue(parentId);
    if (!ins.exec()) { qWarning() << ins.lastError(); return 0; }
    emit foldersChanged();
    return ins.lastInsertId().toInt();
}

bool DB::renameFolder(int id, const QString& name){
    QSqlQuery q(m_db);
    q.prepare("UPDATE virtual_folders SET name=?, updated_at=CURRENT_TIMESTAMP WHERE id=?");
    q.addBindValue(name); q.addBindValue(id);
    bool ok = q.exec(); if (!ok) qWarning() << q.lastError();
    if (ok) emit foldersChanged();
    return ok;
}

bool DB::deleteFolder(int id){
    if (id==m_rootId) return false;
    QSqlQuery q(m_db); q.prepare("DELETE FROM virtual_folders WHERE id=?"); q.addBindValue(id);
    bool ok = q.exec(); if (!ok) qWarning() << q.lastError();
    if (ok) emit foldersChanged();
    return ok;
}

bool DB::moveFolder(int id, int newParentId){
    if (id==m_rootId) return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE virtual_folders SET parent_id=?, updated_at=CURRENT_TIMESTAMP WHERE id=?");
    q.addBindValue(newParentId<=0?m_rootId:newParentId);
    q.addBindValue(id);
    bool ok = q.exec(); if (!ok) qWarning() << q.lastError();
    if (ok) emit foldersChanged();
    return ok;
}

int DB::upsertAsset(const QString& filePath){
    QFileInfo fi(filePath);
    if (!fi.exists()) {
        qDebug() << "DB::upsertAsset: file does not exist:" << filePath;
        return 0;
    }
    QSqlQuery sel(m_db);
    sel.prepare("SELECT id FROM assets WHERE file_path=?");
    sel.addBindValue(fi.absoluteFilePath());
    if (sel.exec() && sel.next()) {
        int existingId = sel.value(0).toInt();
        qDebug() << "DB::upsertAsset: asset already exists, id=" << existingId << "path=" << filePath;
        return existingId;
    }

    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO assets(file_path,file_name,virtual_folder_id,file_size) VALUES(?,?,?,?)");
    ins.addBindValue(fi.absoluteFilePath());
    ins.addBindValue(fi.fileName());
    ins.addBindValue(m_rootId);
    ins.addBindValue((qint64)fi.size());
    if (!ins.exec()) {
        qWarning() << "DB::upsertAsset: INSERT failed:" << ins.lastError();
        return 0;
    }
    int newId = ins.lastInsertId().toInt();
    qDebug() << "DB::upsertAsset: created new asset, id=" << newId << "path=" << filePath;
    emit assetsChanged(m_rootId);
    return newId;
}

bool DB::setAssetFolder(int assetId, int folderId){
    // Get old folder ID first
    QSqlQuery sel(m_db);
    sel.prepare("SELECT virtual_folder_id FROM assets WHERE id=?");
    sel.addBindValue(assetId);
    int oldFolderId = m_rootId;
    if (sel.exec() && sel.next()) {
        oldFolderId = sel.value(0).toInt();
    }

    QSqlQuery q(m_db);
    q.prepare("UPDATE assets SET virtual_folder_id=?, updated_at=CURRENT_TIMESTAMP WHERE id=?");
    q.addBindValue(folderId<=0?m_rootId:folderId);
    q.addBindValue(assetId);
    bool ok = q.exec();
    if (!ok) {
        qWarning() << "DB::setAssetFolder: UPDATE failed:" << q.lastError();
    } else {
        qDebug() << "DB::setAssetFolder: moved asset" << assetId << "from folder" << oldFolderId << "to folder" << folderId;
        // Emit for both old and new folders
        if (oldFolderId != folderId) {
            emit assetsChanged(oldFolderId);
        }
        emit assetsChanged(folderId);
    }
return ok;
}

bool DB::removeAssets(const QList<int>& assetIds){
    if (assetIds.isEmpty()) return true;
    QSqlQuery q(m_db);
    bool ok = true;
    for (int id : assetIds) {
        q.prepare("DELETE FROM assets WHERE id=?");
        q.addBindValue(id);
        ok &= q.exec();
    }
    if (!ok) qWarning() << "DB::removeAssets: delete failed" << q.lastError();
    emit assetsChanged(m_rootId);
    return ok;
}

bool DB::setAssetsRating(const QList<int>& assetIds, int rating){
    QSqlQuery q(m_db);
    bool ok = true;
    for (int id : assetIds) {
        q.prepare("UPDATE assets SET rating=?, updated_at=CURRENT_TIMESTAMP WHERE id=?");
        if (rating < 0) q.addBindValue(QVariant(QVariant::Int)); else q.addBindValue(rating);
        q.addBindValue(id);
        ok &= q.exec();
    }
    if (!ok) qWarning() << "DB::setAssetsRating failed" << q.lastError();
    emit assetsChanged(m_rootId);
    return ok;
}

int DB::createTag(const QString& name){
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO tags(name) VALUES(?)");
    q.addBindValue(name);
    if (!q.exec()) { qWarning() << q.lastError(); return 0; }
    emit tagsChanged();
    return q.lastInsertId().toInt();
}

bool DB::renameTag(int id, const QString& name){ QSqlQuery q(m_db); q.prepare("UPDATE tags SET name=? WHERE id=?"); q.addBindValue(name); q.addBindValue(id); bool ok=q.exec(); if (!ok) qWarning()<<q.lastError(); if (ok) emit tagsChanged(); return ok; }
bool DB::deleteTag(int id){ QSqlQuery q(m_db); q.prepare("DELETE FROM tags WHERE id=?"); q.addBindValue(id); bool ok=q.exec(); if (!ok) qWarning()<<q.lastError(); if (ok) emit tagsChanged(); return ok; }

QVector<QPair<int, QString>> DB::listTags() const {
    QVector<QPair<int, QString>> tags;
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id,name FROM tags ORDER BY name")) return tags;
    while (q.next()) tags.append({q.value(0).toInt(), q.value(1).toString()});
    return tags;
}

QStringList DB::tagsForAsset(int assetId) const {
    QStringList names;
    QSqlQuery q(m_db);
    q.prepare("SELECT t.name FROM tags t JOIN asset_tags at ON at.tag_id=t.id WHERE at.asset_id=? ORDER BY t.name");
    q.addBindValue(assetId);
    if (q.exec()) {
        while (q.next()) names << q.value(0).toString();
    }
    return names;
}

bool DB::assignTagsToAssets(const QList<int>& assetIds, const QList<int>& tagIds){
    if (assetIds.isEmpty() || tagIds.isEmpty()) return true;
    QSqlQuery q(m_db);
    bool ok = true;
    for (int aid : assetIds) {
        for (int tid : tagIds) {
            q.prepare("INSERT OR IGNORE INTO asset_tags(asset_id, tag_id) VALUES(?,?)");
            q.addBindValue(aid); q.addBindValue(tid);
            ok &= q.exec();
        }
    }
    if (!ok) qWarning() << "DB::assignTagsToAssets failed" << q.lastError();
    emit assetsChanged(m_rootId);
    return ok;
}

QList<int> DB::getAssetIdsInFolder(int folderId, bool recursive) const
{
    QList<int> assetIds;

    if (recursive) {
        // Get all descendant folder IDs
        QList<int> folderIds;
        folderIds.append(folderId);

        // Recursively collect all child folders
        QSqlQuery q(m_db);
        q.prepare("WITH RECURSIVE folder_tree AS ("
                  "  SELECT id FROM virtual_folders WHERE id = ?"
                  "  UNION ALL"
                  "  SELECT vf.id FROM virtual_folders vf"
                  "  INNER JOIN folder_tree ft ON vf.parent_id = ft.id"
                  ") SELECT id FROM folder_tree");
        q.addBindValue(folderId);

        if (q.exec()) {
            folderIds.clear();
            while (q.next()) {
                folderIds.append(q.value(0).toInt());
            }
        } else {
            qWarning() << "DB::getAssetIdsInFolder - Failed to get folder tree:" << q.lastError();
            return assetIds;
        }

        // Get all assets in these folders
        if (!folderIds.isEmpty()) {
            QString placeholders = QString("?").repeated(folderIds.size());
            for (int i = 1; i < folderIds.size(); ++i) {
                placeholders.replace(i * 2 - 1, 1, ",?");
            }

            q.prepare(QString("SELECT id FROM assets WHERE virtual_folder_id IN (%1)").arg(placeholders));
            for (int fid : folderIds) {
                q.addBindValue(fid);
            }

            if (q.exec()) {
                while (q.next()) {
                    assetIds.append(q.value(0).toInt());
                }
            } else {
                qWarning() << "DB::getAssetIdsInFolder - Failed to get assets:" << q.lastError();
            }
        }
    } else {
        // Non-recursive: just get assets in this folder
        QSqlQuery q(m_db);
        q.prepare("SELECT id FROM assets WHERE virtual_folder_id = ?");
        q.addBindValue(folderId);

        if (q.exec()) {
            while (q.next()) {
                assetIds.append(q.value(0).toInt());
            }
        } else {
            qWarning() << "DB::getAssetIdsInFolder - Failed to get assets:" << q.lastError();
        }
    }

    return assetIds;
}

