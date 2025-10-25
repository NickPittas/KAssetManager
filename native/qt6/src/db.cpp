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
    // Minimal schema for virtual folders and assets
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
        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP,\n"
        "  updated_at TEXT DEFAULT CURRENT_TIMESTAMP\n"
        ");",
        "CREATE INDEX IF NOT EXISTS idx_assets_folder ON assets(virtual_folder_id);"
    };
    for (const char* sql : ddl) if (!exec(QString::fromLatin1(sql))) return false;
    return true;
}

bool DB::exec(const QString& sql){ QSqlQuery q(m_db); if (!q.exec(sql)) { qWarning() << "SQL failed:" << sql << q.lastError(); return false; } return true; }

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

