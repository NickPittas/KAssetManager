#include "db.h"
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QCryptographicHash>

static QString lastErrorToString(const QSqlQuery& q){ return q.lastError().text(); }

static QString computeFileSha256(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hasher(QCryptographicHash::Sha256);
    const qint64 bufSize = 1 << 20; // 1MB
    QByteArray buf;
    buf.resize((int)bufSize);
    while (true) {
        qint64 n = f.read(buf.data(), buf.size());
        if (n <= 0) break;
        hasher.addData(buf.constData(), (int)n);
    }
    return hasher.result().toHex();
}

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
    // Derive data dir from DB file path for storing versions
    QFileInfo dbFi(dbFilePath);
    m_dataDir = dbFi.absolutePath();
    QDir().mkpath(m_dataDir + "/versions");

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
        "  virtual_folder_id INTEGER NOT NULL REFERENCES virtual_folders(id) ON DELETE CASCADE,\n"
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
        ");",
        // Project folders (watched folders)
        "CREATE TABLE IF NOT EXISTS project_folders (\n"
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "  name TEXT NOT NULL UNIQUE,\n"
        "  path TEXT NOT NULL UNIQUE,\n"
        "  virtual_folder_id INTEGER NOT NULL REFERENCES virtual_folders(id) ON DELETE CASCADE,\n"
        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP\n"
        ");"
    };
    for (const char* sql : ddl) if (!exec(QString::fromLatin1(sql))) return false;

    // If older DB lacked 'rating' column, add it
    if (!hasColumn("assets", "rating")) {
        exec("ALTER TABLE assets ADD COLUMN rating INTEGER NULL");
    }

    // Add image sequence support columns
    if (!hasColumn("assets", "is_sequence")) {
        exec("ALTER TABLE assets ADD COLUMN is_sequence INTEGER DEFAULT 0");
    }
    if (!hasColumn("assets", "sequence_pattern")) {
        exec("ALTER TABLE assets ADD COLUMN sequence_pattern TEXT NULL");
    }
    if (!hasColumn("assets", "sequence_start_frame")) {
        exec("ALTER TABLE assets ADD COLUMN sequence_start_frame INTEGER NULL");
    }
    if (!hasColumn("assets", "sequence_end_frame")) {
        exec("ALTER TABLE assets ADD COLUMN sequence_end_frame INTEGER NULL");
    }
    if (!hasColumn("assets", "sequence_frame_count")) {
        exec("ALTER TABLE assets ADD COLUMN sequence_frame_count INTEGER NULL");
    }

    // Ensure checksum column exists for change detection
    if (!hasColumn("assets", "checksum")) {
        exec("ALTER TABLE assets ADD COLUMN checksum TEXT NULL");
    }

    // Version history table
    exec(
        "CREATE TABLE IF NOT EXISTS asset_versions (\n"
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "  asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,\n"
        "  version_number INTEGER NOT NULL,\n"
        "  version_name TEXT NOT NULL,\n"
        "  file_path TEXT NOT NULL,\n"
        "  file_size INTEGER NOT NULL,\n"
        "  checksum TEXT NOT NULL,\n"
        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP,\n"
        "  notes TEXT NULL,\n"
        "  UNIQUE(asset_id, version_number)\n"
        ");"
    );
    exec("CREATE INDEX IF NOT EXISTS idx_asset_versions_asset_id ON asset_versions(asset_id);");

    // PERFORMANCE: Add indexes for frequently queried columns
    exec("CREATE INDEX IF NOT EXISTS idx_assets_file_name ON assets(file_name);");
    exec("CREATE INDEX IF NOT EXISTS idx_assets_rating ON assets(rating);");
    exec("CREATE INDEX IF NOT EXISTS idx_assets_updated_at ON assets(updated_at);");
    exec("CREATE INDEX IF NOT EXISTS idx_asset_tags_tag_id ON asset_tags(tag_id);");
    exec("CREATE INDEX IF NOT EXISTS idx_asset_tags_asset_id ON asset_tags(asset_id);");

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
    const QString absPath = fi.absoluteFilePath();

    // Check if already exists
    QSqlQuery sel(m_db);
    sel.prepare("SELECT id, COALESCE(file_size,0), COALESCE(checksum,'') FROM assets WHERE file_path=?");
    sel.addBindValue(absPath);
    if (sel.exec() && sel.next()) {
        int existingId = sel.value(0).toInt();
        qint64 oldSize = sel.value(1).toLongLong();
        QString oldChecksum = sel.value(2).toString();

        // Compare size first; compute checksum only if size differs or checksum missing
        qint64 newSize = fi.size();
        bool changed = (newSize != oldSize) || oldChecksum.isEmpty();
        QString newChecksum;
        if (changed) {
            newChecksum = computeFileSha256(absPath);
            changed = (oldChecksum.isEmpty() || newChecksum != oldChecksum);
        }

        if (changed) {
            // Create a new version snapshot and update metadata
            createAssetVersion(existingId, absPath, QStringLiteral("Auto-sync: detected change on disk"));
            QSqlQuery upd(m_db);
            upd.prepare("UPDATE assets SET file_size=?, checksum=?, updated_at=CURRENT_TIMESTAMP WHERE id=?");
            upd.addBindValue(newSize);
            upd.addBindValue(newChecksum);
            upd.addBindValue(existingId);
            if (!upd.exec()) {
                qWarning() << "DB::upsertAsset: UPDATE failed:" << upd.lastError();
            }
            emit assetsChanged(m_rootId);
        } else {
            qDebug() << "DB::upsertAsset: unchanged asset, id=" << existingId;
        }
        return existingId;
    }

    // New asset: insert row
    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO assets(file_path,file_name,virtual_folder_id,file_size,checksum) VALUES(?,?,?,?,?)");
    ins.addBindValue(absPath);
    ins.addBindValue(fi.fileName());
    ins.addBindValue(m_rootId);
    ins.addBindValue((qint64)fi.size());
    const QString checksum = computeFileSha256(absPath);
    ins.addBindValue(checksum);
    if (!ins.exec()) {
        qWarning() << "DB::upsertAsset: INSERT failed:" << ins.lastError();
        return 0;
    }
    int newId = ins.lastInsertId().toInt();

    // Create initial version v1
    createAssetVersion(newId, absPath, QStringLiteral("Initial import"));

    qDebug() << "DB::upsertAsset: created new asset, id=" << newId << "path=" << filePath;
    emit assetsChanged(m_rootId);
    return newId;
}

int DB::upsertSequence(const QString& sequencePattern, int startFrame, int endFrame, int frameCount, const QString& firstFramePath){
    QFileInfo fi(firstFramePath);
    if (!fi.exists()) {
        qDebug() << "DB::upsertSequence: first frame does not exist:" << firstFramePath;
        return 0;
    }

    // Check if sequence already exists
    QSqlQuery sel(m_db);
    sel.prepare("SELECT id FROM assets WHERE sequence_pattern=? AND is_sequence=1");
    sel.addBindValue(sequencePattern);
    if (sel.exec() && sel.next()) {
        int existingId = sel.value(0).toInt();
        qDebug() << "DB::upsertSequence: sequence already exists, id=" << existingId << "pattern=" << sequencePattern;

        // Update frame range if changed
        QSqlQuery upd(m_db);
        upd.prepare("UPDATE assets SET sequence_start_frame=?, sequence_end_frame=?, sequence_frame_count=? WHERE id=?");
        upd.addBindValue(startFrame);
        upd.addBindValue(endFrame);
        upd.addBindValue(frameCount);
        upd.addBindValue(existingId);
        upd.exec();

        return existingId;
    }

    // Create new sequence entry
    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO assets(file_path,file_name,virtual_folder_id,file_size,is_sequence,sequence_pattern,sequence_start_frame,sequence_end_frame,sequence_frame_count) VALUES(?,?,?,?,1,?,?,?,?)");
    ins.addBindValue(fi.absoluteFilePath()); // Store first frame path
    ins.addBindValue(sequencePattern); // Display name is the pattern
    ins.addBindValue(m_rootId);
    ins.addBindValue((qint64)fi.size());
    ins.addBindValue(sequencePattern);
    ins.addBindValue(startFrame);
    ins.addBindValue(endFrame);
    ins.addBindValue(frameCount);

    if (!ins.exec()) {
        qWarning() << "DB::upsertSequence: INSERT failed:" << ins.lastError();
        return 0;
    }
    int newId = ins.lastInsertId().toInt();
    qDebug() << "DB::upsertSequence: created new sequence, id=" << newId << "pattern=" << sequencePattern << "frames=" << startFrame << "-" << endFrame;
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

bool DB::mergeTags(int sourceTagId, int targetTagId) {
    if (sourceTagId == targetTagId) return false;

    m_db.transaction();

    // Get all assets with source tag
    QSqlQuery q(m_db);
    q.prepare("SELECT asset_id FROM asset_tags WHERE tag_id=?");
    q.addBindValue(sourceTagId);
    if (!q.exec()) {
        qWarning() << "mergeTags: Failed to query assets with source tag:" << q.lastError();
        m_db.rollback();
        return false;
    }

    QList<int> assetIds;
    while (q.next()) {
        assetIds.append(q.value(0).toInt());
    }

    // For each asset, add target tag if not already present
    for (int assetId : assetIds) {
        QSqlQuery checkQ(m_db);
        checkQ.prepare("SELECT 1 FROM asset_tags WHERE asset_id=? AND tag_id=?");
        checkQ.addBindValue(assetId);
        checkQ.addBindValue(targetTagId);
        if (!checkQ.exec()) {
            qWarning() << "mergeTags: Failed to check existing tag:" << checkQ.lastError();
            m_db.rollback();
            return false;
        }

        // If target tag doesn't exist for this asset, add it
        if (!checkQ.next()) {
            QSqlQuery insertQ(m_db);
            insertQ.prepare("INSERT INTO asset_tags (asset_id, tag_id) VALUES (?, ?)");
            insertQ.addBindValue(assetId);
            insertQ.addBindValue(targetTagId);
            if (!insertQ.exec()) {
                qWarning() << "mergeTags: Failed to insert target tag:" << insertQ.lastError();
                m_db.rollback();
                return false;
            }
        }
    }

    // Delete source tag (CASCADE will remove asset_tags entries)
    QSqlQuery deleteQ(m_db);
    deleteQ.prepare("DELETE FROM tags WHERE id=?");
    deleteQ.addBindValue(sourceTagId);
    if (!deleteQ.exec()) {
        qWarning() << "mergeTags: Failed to delete source tag:" << deleteQ.lastError();
        m_db.rollback();
        return false;
    }

    m_db.commit();
    emit tagsChanged();
    return true;
}

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


int DB::getAssetIdByPath(const QString& filePath) const
{
    QSqlQuery q(m_db);
    QFileInfo fi(filePath);
    q.prepare("SELECT id FROM assets WHERE file_path=?");
    q.addBindValue(fi.absoluteFilePath());
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

QVector<AssetVersionRow> DB::listAssetVersions(int assetId) const
{
    QVector<AssetVersionRow> rows;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, asset_id, version_number, version_name, file_path, file_size, checksum, created_at, COALESCE(notes,'') FROM asset_versions WHERE asset_id=? ORDER BY version_number ASC");
    q.addBindValue(assetId);
    if (q.exec()) {
        while (q.next()) {
            AssetVersionRow r;
            r.id = q.value(0).toInt();
            r.assetId = q.value(1).toInt();
            r.versionNumber = q.value(2).toInt();
            r.versionName = q.value(3).toString();
            r.filePath = q.value(4).toString();
            r.fileSize = q.value(5).toLongLong();
            r.checksum = q.value(6).toString();
            r.createdAt = q.value(7).toString();
            r.notes = q.value(8).toString();
            rows.append(r);
        }
    }
    return rows;
}

int DB::createAssetVersion(int assetId, const QString& srcFilePath, const QString& notes)
{
    QFileInfo sfi(srcFilePath);
    if (!sfi.exists()) return 0;

    // Determine next version number
    QSqlQuery q(m_db);
    q.prepare("SELECT COALESCE(MAX(version_number),0)+1 FROM asset_versions WHERE asset_id=?");
    q.addBindValue(assetId);
    int nextVersion = 1;
    if (q.exec() && q.next()) nextVersion = q.value(0).toInt();
    const QString versionName = QStringLiteral("v%1").arg(nextVersion);

    // Prepare destination path
    const QString versionsDir = m_dataDir + "/versions/" + QString::number(assetId);
    QDir().mkpath(versionsDir);
    const QString destFileName = versionName + "_" + sfi.fileName();
    const QString destPath = versionsDir + "/" + destFileName;

    // Copy file
    if (QFile::exists(destPath)) QFile::remove(destPath);
    if (!QFile::copy(sfi.absoluteFilePath(), destPath)) {
        qWarning() << "createAssetVersion: failed to copy" << sfi.absoluteFilePath() << "to" << destPath;
        return 0;
    }

    const qint64 fsize = sfi.size();
    const QString sha256 = computeFileSha256(sfi.absoluteFilePath());

    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO asset_versions(asset_id, version_number, version_name, file_path, file_size, checksum, notes) VALUES(?,?,?,?,?,?,?)");
    ins.addBindValue(assetId);
    ins.addBindValue(nextVersion);
    ins.addBindValue(versionName);
    ins.addBindValue(destPath);
    ins.addBindValue(fsize);
    ins.addBindValue(sha256);
    ins.addBindValue(notes);
    if (!ins.exec()) {
        qWarning() << "createAssetVersion: INSERT failed" << ins.lastError();
        return 0;
    }

    emit assetVersionsChanged(assetId);
    return ins.lastInsertId().toInt();
}

bool DB::revertAssetToVersion(int assetId, int versionId, bool createBackupVersion)
{
    // Get target version row
    QSqlQuery vs(m_db);
    vs.prepare("SELECT version_number, version_name, file_path, file_size, checksum FROM asset_versions WHERE id=? AND asset_id=?");
    vs.addBindValue(versionId);
    vs.addBindValue(assetId);
    if (!vs.exec() || !vs.next()) {
        qWarning() << "revertAssetToVersion: version not found" << versionId << "asset" << assetId;
        return false;
    }
    const QString verName = vs.value(1).toString();
    const QString srcPath = vs.value(2).toString();

    // Current asset path and folder id
    QSqlQuery a(m_db);
    a.prepare("SELECT file_path, virtual_folder_id FROM assets WHERE id=?");
    a.addBindValue(assetId);
    if (!a.exec() || !a.next()) return false;
    const QString destPath = a.value(0).toString();
    const int folderId = a.value(1).toInt();

    // Optionally backup current file as a new version
    if (createBackupVersion) {
        createAssetVersion(assetId, destPath, QStringLiteral("Backup before revert to %1").arg(verName));
    }

    // Overwrite asset file with version file
    if (QFile::exists(destPath)) QFile::remove(destPath);
    if (!QFile::copy(srcPath, destPath)) {
        qWarning() << "revertAssetToVersion: failed to copy" << srcPath << "to" << destPath;
        return false;
    }

    // Update asset metadata
    QFileInfo dfi(destPath);
    const qint64 newSize = dfi.size();
    const QString newChecksum = computeFileSha256(destPath);
    QSqlQuery upd(m_db);
    upd.prepare("UPDATE assets SET file_size=?, checksum=?, updated_at=CURRENT_TIMESTAMP WHERE id=?");
    upd.addBindValue(newSize);
    upd.addBindValue(newChecksum);
    upd.addBindValue(assetId);
    upd.exec();

    emit assetsChanged(folderId);
    emit assetVersionsChanged(assetId);
    return true;
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

QString DB::getAssetFilePath(int assetId) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT file_path FROM assets WHERE id = ?");
    q.addBindValue(assetId);

    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }

    return QString();
}

bool DB::exportDatabase(const QString& filePath)
{
    // Close current connection
    QString dbName = m_db.databaseName();
    m_db.close();

    // Copy database file
    bool success = QFile::copy(dbName, filePath);

    // Reopen connection
    m_db.open();

    if (!success) {
        qWarning() << "DB::exportDatabase: Failed to copy database to" << filePath;
    }

    return success;
}

bool DB::importDatabase(const QString& filePath)
{
    if (!QFile::exists(filePath)) {
        qWarning() << "DB::importDatabase: Source file does not exist:" << filePath;
        return false;
    }

    // Close current connection
    QString dbName = m_db.databaseName();
    m_db.close();

    // Remove old database
    QFile::remove(dbName);

    // Copy new database
    bool success = QFile::copy(filePath, dbName);

    // Reopen connection
    m_db.open();

    if (!success) {
        qWarning() << "DB::importDatabase: Failed to copy database from" << filePath;
    } else {
        // Reload root folder ID
        m_rootId = ensureRootFolder();
        emit foldersChanged();
        emit assetsChanged(m_rootId);
        emit tagsChanged();
    }

    return success;
}

bool DB::clearAllData()
{
    QSqlQuery q(m_db);

    // Delete all data
    bool ok = true;
    ok &= q.exec("DELETE FROM asset_tags");
    ok &= q.exec("DELETE FROM assets");
    ok &= q.exec("DELETE FROM tags");
    ok &= q.exec("DELETE FROM virtual_folders");

    if (!ok) {
        qWarning() << "DB::clearAllData: Failed to clear data:" << q.lastError();
        return false;
    }

    // Recreate root folder
    m_rootId = ensureRootFolder();

    emit foldersChanged();
    emit assetsChanged(m_rootId);
    emit tagsChanged();

    return true;
}

// Project folder operations
int DB::createProjectFolder(const QString& name, const QString& path)
{
    // First create a virtual folder for this project
    int virtualFolderId = createFolder(name, 0); // Create at root level
    if (virtualFolderId <= 0) {
        qWarning() << "DB::createProjectFolder: Failed to create virtual folder";
        return 0;
    }

    // Then create the project folder entry
    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO project_folders(name, path, virtual_folder_id) VALUES(?, ?, ?)");
    ins.addBindValue(name);
    ins.addBindValue(path);
    ins.addBindValue(virtualFolderId);

    if (!ins.exec()) {
        qWarning() << "DB::createProjectFolder: INSERT failed:" << ins.lastError();
        // Clean up the virtual folder
        deleteFolder(virtualFolderId);
        return 0;
    }

    int projectFolderId = ins.lastInsertId().toInt();
    qDebug() << "DB::createProjectFolder: created project folder" << projectFolderId << "name=" << name << "path=" << path;
    emit projectFoldersChanged();
    return projectFolderId;
}

bool DB::renameProjectFolder(int id, const QString& name)
{
    // Get the virtual folder ID
    QSqlQuery sel(m_db);
    sel.prepare("SELECT virtual_folder_id FROM project_folders WHERE id=?");
    sel.addBindValue(id);
    if (!sel.exec() || !sel.next()) {
        qWarning() << "DB::renameProjectFolder: Failed to find project folder" << id;
        return false;
    }
    int virtualFolderId = sel.value(0).toInt();

    // Update both the project folder and virtual folder names
    QSqlQuery upd1(m_db);
    upd1.prepare("UPDATE project_folders SET name=? WHERE id=?");
    upd1.addBindValue(name);
    upd1.addBindValue(id);

    QSqlQuery upd2(m_db);
    upd2.prepare("UPDATE virtual_folders SET name=?, updated_at=CURRENT_TIMESTAMP WHERE id=?");
    upd2.addBindValue(name);
    upd2.addBindValue(virtualFolderId);

    bool ok = upd1.exec() && upd2.exec();
    if (!ok) {
        qWarning() << "DB::renameProjectFolder: UPDATE failed:" << upd1.lastError() << upd2.lastError();
    } else {
        qDebug() << "DB::renameProjectFolder: renamed project folder" << id << "to" << name;
        emit projectFoldersChanged();
        emit foldersChanged();
    }
    return ok;
}

bool DB::deleteProjectFolder(int id)
{
    // Get the virtual folder ID first
    QSqlQuery sel(m_db);
    sel.prepare("SELECT virtual_folder_id FROM project_folders WHERE id=?");
    sel.addBindValue(id);
    if (!sel.exec() || !sel.next()) {
        qWarning() << "DB::deleteProjectFolder: Failed to find project folder" << id;
        return false;
    }
    int virtualFolderId = sel.value(0).toInt();

    // Delete the project folder entry (virtual folder will be deleted by CASCADE)
    QSqlQuery del(m_db);
    del.prepare("DELETE FROM project_folders WHERE id=?");
    del.addBindValue(id);

    bool ok = del.exec();
    if (!ok) {
        qWarning() << "DB::deleteProjectFolder: DELETE failed:" << del.lastError();
    } else {
        qDebug() << "DB::deleteProjectFolder: deleted project folder" << id;
        // Also delete the virtual folder
        deleteFolder(virtualFolderId);
        emit projectFoldersChanged();
    }
    return ok;
}

QVector<QPair<int, QPair<QString, QString>>> DB::listProjectFolders() const
{
    QVector<QPair<int, QPair<QString, QString>>> result;
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id, name, path FROM project_folders ORDER BY name")) {
        qWarning() << "DB::listProjectFolders: SELECT failed:" << q.lastError();
        return result;
    }

    while (q.next()) {
        int id = q.value(0).toInt();
        QString name = q.value(1).toString();
        QString path = q.value(2).toString();
        result.append({id, {name, path}});
    }

    return result;
}

QString DB::getProjectFolderPath(int id) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT path FROM project_folders WHERE id=?");
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return QString();
}

int DB::getProjectFolderIdByVirtualFolderId(int virtualFolderId) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM project_folders WHERE virtual_folder_id=?");
    q.addBindValue(virtualFolderId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}
