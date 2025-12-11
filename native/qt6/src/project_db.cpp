#include "project_db.h"
#include <QDir>
#include <QDebug>
#include <QFile>
#include <QDirIterator>
#include <QRegularExpression>
#include <QSet>

#include "file_utils.h"

ProjectDB& ProjectDB::instance() {
    static ProjectDB s;
    return s;
}

ProjectDB::ProjectDB(QObject* parent) : QObject(parent) {}

bool ProjectDB::init(const QString& dbFilePath) {
    if (m_db.isValid()) return true;
    
    // Use a unique connection name to avoid conflicts with main DB
    m_db = QSqlDatabase::addDatabase("QSQLITE", "project_db_connection");
    m_db.setDatabaseName(dbFilePath);
    if (!m_db.open()) {
        qWarning() << "ProjectDB open failed:" << m_db.lastError();
        return false;
    }

    if (!migrate()) return false;
    m_rootId = ensureRootFolder();
    return m_rootId > 0;
}

bool ProjectDB::migrate() {
    // Enable FK enforcement
    if (!exec("PRAGMA foreign_keys=ON;")) return false;

    // Projects table
    if (!exec(
        "CREATE TABLE IF NOT EXISTS projects (\n"
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "  name TEXT NOT NULL,\n"
        "  watch_path TEXT NOT NULL UNIQUE,\n"
        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP,\n"
        "  updated_at TEXT DEFAULT CURRENT_TIMESTAMP\n"
        ");")) return false;

    // Virtual folders (same structure as main DB for Importer compatibility)
    if (!exec(
        "CREATE TABLE IF NOT EXISTS virtual_folders (\n"
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "  name TEXT NOT NULL,\n"
        "  parent_id INTEGER NULL REFERENCES virtual_folders(id) ON DELETE CASCADE,\n"
        "  project_id INTEGER NULL REFERENCES projects(id) ON DELETE CASCADE,\n"
        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP\n"
        ");")) return false;

    // Assets table (mirrors main DB structure + version grouping)
    if (!exec(
        "CREATE TABLE IF NOT EXISTS assets (\n"
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "  file_path TEXT NOT NULL UNIQUE,\n"
        "  file_name TEXT NOT NULL,\n"
        "  virtual_folder_id INTEGER NOT NULL REFERENCES virtual_folders(id) ON DELETE CASCADE,\n"
        "  file_size INTEGER NULL,\n"
        "  file_type TEXT NULL,\n"
        "  last_modified TEXT NULL,\n"
        "  is_sequence INTEGER DEFAULT 0,\n"
        "  sequence_pattern TEXT NULL,\n"
        "  sequence_start_frame INTEGER NULL,\n"
        "  sequence_end_frame INTEGER NULL,\n"
        "  sequence_frame_count INTEGER NULL,\n"
        "  sequence_has_gaps INTEGER DEFAULT 0,\n"
        "  sequence_gap_count INTEGER DEFAULT 0,\n"
        "  sequence_version TEXT NULL,\n"
        "  version_group_key TEXT NULL,\n"
        "  version_string TEXT NULL,\n"
        "  created_at TEXT DEFAULT CURRENT_TIMESTAMP,\n"
        "  updated_at TEXT DEFAULT CURRENT_TIMESTAMP\n"
        ");")) return false;

    // Notifications table (persistent across restarts)
    if (!exec(
        "CREATE TABLE IF NOT EXISTS notifications (\n"
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
        "  project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE CASCADE,\n"
        "  asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,\n"
        "  file_path TEXT NOT NULL,\n"
        "  detected_at TEXT DEFAULT CURRENT_TIMESTAMP,\n"
        "  acknowledged INTEGER DEFAULT 0\n"
        ");")) return false;

    // Indexes
    exec("CREATE INDEX IF NOT EXISTS idx_assets_folder ON assets(virtual_folder_id);");
    exec("CREATE INDEX IF NOT EXISTS idx_assets_version_group ON assets(version_group_key);");
    exec("CREATE INDEX IF NOT EXISTS idx_notifications_project ON notifications(project_id);");
    exec("CREATE INDEX IF NOT EXISTS idx_notifications_unack ON notifications(acknowledged) WHERE acknowledged=0;");
    exec("CREATE INDEX IF NOT EXISTS idx_virtual_folders_project ON virtual_folders(project_id);");

    return true;
}

bool ProjectDB::exec(const QString& sql) {
    QSqlQuery q(m_db);
    if (!q.exec(sql)) {
        qWarning() << "ProjectDB SQL failed:" << sql << q.lastError();
        return false;
    }
    return true;
}

bool ProjectDB::hasColumn(const QString& table, const QString& column) const {
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    if (!q.exec()) return false;
    while (q.next()) {
        if (q.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

int ProjectDB::ensureRootFolder() {
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id FROM virtual_folders WHERE parent_id IS NULL AND name='Root' LIMIT 1")) return 0;
    if (q.next()) {
        bool ok = false;
        int id = q.value(0).toInt(&ok);
        return ok ? id : 0;
    }
    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO virtual_folders(name, parent_id, project_id) VALUES('Root', NULL, NULL)");
    if (!ins.exec()) {
        qWarning() << "ProjectDB ensureRootFolder failed:" << ins.lastError();
        return 0;
    }
    bool ok = false;
    int id = ins.lastInsertId().toInt(&ok);
    return ok ? id : 0;
}

int ProjectDB::createFolder(const QString& name, int parentId) {
    // Determine project_id from parent folder
    int projectId = 0;
    if (parentId > 0) {
        QSqlQuery pq(m_db);
        pq.prepare("SELECT project_id FROM virtual_folders WHERE id=?");
        pq.addBindValue(parentId);
        if (pq.exec() && pq.next()) {
            projectId = pq.value(0).toInt();
        }
    }
    
    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO virtual_folders(name, parent_id, project_id) VALUES(?, ?, ?)");
    ins.addBindValue(name);
    ins.addBindValue(parentId <= 0 ? m_rootId : parentId);
    ins.addBindValue(projectId > 0 ? projectId : QVariant());
    
    if (!ins.exec()) {
        qWarning() << "ProjectDB createFolder failed:" << ins.lastError();
        return 0;
    }
    bool ok = false;
    int id = ins.lastInsertId().toInt(&ok);
    return ok ? id : 0;
}

int ProjectDB::insertAssetMetadataFast(const QString& filePath, int folderId) {
    QFileInfo fi(filePath);
    if (!fi.exists()) {
        return 0;
    }
    const QString absPath = fi.absoluteFilePath();
    const QString fileType = fi.suffix().toLower();
    const QString lastModified = fi.lastModified().toString(Qt::ISODate);

    // Check if already exists
    QSqlQuery sel(m_db);
    sel.prepare("SELECT id FROM assets WHERE file_path=?");
    sel.addBindValue(absPath);
    if (sel.exec() && sel.next()) {
        bool okId = false;
        int existingId = sel.value(0).toInt(&okId);
        if (!okId) return 0;
        
        // Update existing
        QSqlQuery upd(m_db);
        upd.prepare("UPDATE assets SET file_name=?, virtual_folder_id=?, file_size=?, file_type=?, last_modified=?, updated_at=CURRENT_TIMESTAMP WHERE id=?");
        upd.addBindValue(fi.fileName());
        upd.addBindValue(folderId <= 0 ? m_rootId : folderId);
        upd.addBindValue((qint64)fi.size());
        upd.addBindValue(fileType);
        upd.addBindValue(lastModified);
        upd.addBindValue(existingId);
        upd.exec();
        return existingId;
    }

    // New asset
    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO assets(file_path, file_name, virtual_folder_id, file_size, file_type, last_modified, is_sequence) VALUES(?, ?, ?, ?, ?, ?, 0)");
    ins.addBindValue(absPath);
    ins.addBindValue(fi.fileName());
    ins.addBindValue(folderId <= 0 ? m_rootId : folderId);
    ins.addBindValue((qint64)fi.size());
    ins.addBindValue(fileType);
    ins.addBindValue(lastModified);
    
    if (!ins.exec()) {
        qWarning() << "ProjectDB insertAssetMetadataFast failed:" << ins.lastError();
        return 0;
    }
    bool ok = false;
    int id = ins.lastInsertId().toInt(&ok);
    return ok ? id : 0;
}

int ProjectDB::upsertSequenceInFolderFast(const QString& sequencePattern, int startFrame, int endFrame, 
                                          int frameCount, const QString& firstFramePath, int folderId, 
                                          bool hasGaps, int gapCount, const QString& version) {
    QFileInfo fi(firstFramePath);
    if (!fi.exists()) {
        return 0;
    }

    // Check if sequence already exists
    QSqlQuery sel(m_db);
    sel.prepare("SELECT id FROM assets WHERE sequence_pattern=? AND is_sequence=1");
    sel.addBindValue(sequencePattern);
    if (sel.exec() && sel.next()) {
        bool okId = false;
        int existingId = sel.value(0).toInt(&okId);
        if (!okId) return 0;
        
        QSqlQuery upd(m_db);
        upd.prepare("UPDATE assets SET file_path=?, file_name=?, virtual_folder_id=?, file_size=?, "
                    "sequence_start_frame=?, sequence_end_frame=?, sequence_frame_count=?, "
                    "sequence_has_gaps=?, sequence_gap_count=?, sequence_version=?, "
                    "updated_at=CURRENT_TIMESTAMP WHERE id=?");
        upd.addBindValue(fi.absoluteFilePath());
        upd.addBindValue(sequencePattern);
        upd.addBindValue(folderId <= 0 ? m_rootId : folderId);
        upd.addBindValue((qint64)fi.size());
        upd.addBindValue(startFrame);
        upd.addBindValue(endFrame);
        upd.addBindValue(frameCount);
        upd.addBindValue(hasGaps ? 1 : 0);
        upd.addBindValue(gapCount);
        upd.addBindValue(version);
        upd.addBindValue(existingId);
        upd.exec();
        return existingId;
    }

    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO assets(file_path, file_name, virtual_folder_id, file_size, is_sequence, "
                "sequence_pattern, sequence_start_frame, sequence_end_frame, sequence_frame_count, "
                "sequence_has_gaps, sequence_gap_count, sequence_version) VALUES(?, ?, ?, ?, 1, ?, ?, ?, ?, ?, ?, ?)");
    ins.addBindValue(fi.absoluteFilePath());
    ins.addBindValue(sequencePattern);
    ins.addBindValue(folderId <= 0 ? m_rootId : folderId);
    ins.addBindValue((qint64)fi.size());
    ins.addBindValue(sequencePattern);
    ins.addBindValue(startFrame);
    ins.addBindValue(endFrame);
    ins.addBindValue(frameCount);
    ins.addBindValue(hasGaps ? 1 : 0);
    ins.addBindValue(gapCount);
    ins.addBindValue(version);

    if (!ins.exec()) {
        qWarning() << "ProjectDB upsertSequenceInFolderFast failed:" << ins.lastError();
        return 0;
    }
    bool ok = false;
    int id = ins.lastInsertId().toInt(&ok);
    return ok ? id : 0;
}

void ProjectDB::notifyAssetsChanged(int folderId) {
    // Determine project ID from folder
    int projectId = getProjectIdByFolderId(folderId);
    emit projectAssetsChanged(projectId);
}

// Project operations

int ProjectDB::createProject(const QString& name, const QString& watchPath) {
    if (!m_db.isOpen()) {
        qWarning() << "ProjectDB createProject: database not open";
        return 0;
    }
    
    // Create project entry
    QSqlQuery ins(m_db);
    if (!ins.prepare("INSERT INTO projects(name, watch_path) VALUES(?, ?)")) {
        qWarning() << "ProjectDB createProject prepare failed:" << ins.lastError();
        return 0;
    }
    ins.addBindValue(name);
    ins.addBindValue(watchPath);
    
    qDebug() << "ProjectDB createProject: name=" << name << "watchPath=" << watchPath;
    
    if (!ins.exec()) {
        qWarning() << "ProjectDB createProject exec failed:" << ins.lastError();
        return 0;
    }
    bool ok = false;
    int projectId = ins.lastInsertId().toInt(&ok);
    if (!ok || projectId <= 0) return 0;

    // Create root folder for this project
    QSqlQuery folderIns(m_db);
    folderIns.prepare("INSERT INTO virtual_folders(name, parent_id, project_id) VALUES(?, ?, ?)");
    folderIns.addBindValue(name);
    folderIns.addBindValue(m_rootId);
    folderIns.addBindValue(projectId);
    
    if (!folderIns.exec()) {
        qWarning() << "ProjectDB createProject folder failed:" << folderIns.lastError();
        // Rollback project creation
        QSqlQuery del(m_db);
        del.prepare("DELETE FROM projects WHERE id=?");
        del.addBindValue(projectId);
        del.exec();
        return 0;
    }
    
    // Cache folder->project mapping
    int folderId = folderIns.lastInsertId().toInt();
    m_folderToProject.insert(folderId, projectId);

    emit projectsChanged();
    return projectId;
}

bool ProjectDB::renameProject(int id, const QString& name) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE projects SET name=?, updated_at=CURRENT_TIMESTAMP WHERE id=?");
    q.addBindValue(name);
    q.addBindValue(id);
    bool ok = q.exec();
    if (ok) emit projectsChanged();
    return ok;
}

bool ProjectDB::deleteProject(int id) {
    // Explicitly delete in order to handle cases where FK cascade might not work
    // (e.g., if the database was created before FK was enabled)
    
    // First, get all folder IDs for this project
    QSqlQuery fq(m_db);
    fq.prepare("SELECT id FROM virtual_folders WHERE project_id = ?");
    fq.addBindValue(id);
    
    QList<int> folderIds;
    if (fq.exec()) {
        while (fq.next()) {
            folderIds.append(fq.value(0).toInt());
        }
    }
    
    // Delete assets in all those folders
    if (!folderIds.isEmpty()) {
        QString placeholders = QString("?,").repeated(folderIds.size());
        placeholders.chop(1); // Remove trailing comma
        
        QSqlQuery aq(m_db);
        aq.prepare(QString("DELETE FROM assets WHERE virtual_folder_id IN (%1)").arg(placeholders));
        for (int fid : folderIds) {
            aq.addBindValue(fid);
        }
        aq.exec();
    }
    
    // Delete notifications for this project
    QSqlQuery nq(m_db);
    nq.prepare("DELETE FROM notifications WHERE project_id = ?");
    nq.addBindValue(id);
    nq.exec();
    
    // Delete folders for this project
    QSqlQuery dq(m_db);
    dq.prepare("DELETE FROM virtual_folders WHERE project_id = ?");
    dq.addBindValue(id);
    dq.exec();
    
    // Finally delete the project
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM projects WHERE id=?");
    q.addBindValue(id);
    bool ok = q.exec();
    if (ok) {
        // Clear cache entries for this project
        QMutableHashIterator<int, int> it(m_folderToProject);
        while (it.hasNext()) {
            it.next();
            if (it.value() == id) it.remove();
        }
        emit projectsChanged();
    }
    return ok;
}

bool ProjectDB::updateProjectWatchPath(int id, const QString& watchPath) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE projects SET watch_path=?, updated_at=CURRENT_TIMESTAMP WHERE id=?");
    q.addBindValue(watchPath);
    q.addBindValue(id);
    bool ok = q.exec();
    if (ok) emit projectsChanged();
    return ok;
}

QVector<Project> ProjectDB::listProjects() const {
    QVector<Project> result;
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id, name, watch_path, created_at, updated_at FROM projects ORDER BY name")) {
        return result;
    }
    while (q.next()) {
        Project p;
        p.id = q.value(0).toInt();
        p.name = q.value(1).toString();
        p.watchPath = q.value(2).toString();
        p.createdAt = q.value(3).toString();
        p.updatedAt = q.value(4).toString();
        result.append(p);
    }
    return result;
}

Project ProjectDB::getProject(int id) const {
    Project p;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, name, watch_path, created_at, updated_at FROM projects WHERE id=?");
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        p.id = q.value(0).toInt();
        p.name = q.value(1).toString();
        p.watchPath = q.value(2).toString();
        p.createdAt = q.value(3).toString();
        p.updatedAt = q.value(4).toString();
    }
    return p;
}

int ProjectDB::getProjectIdByFolderId(int folderId) const {
    // Check cache first
    if (m_folderToProject.contains(folderId)) {
        return m_folderToProject.value(folderId);
    }
    
    // Query database - traverse up to find project_id
    QSqlQuery q(m_db);
    q.prepare("SELECT project_id, parent_id FROM virtual_folders WHERE id=?");
    
    int currentId = folderId;
    while (currentId > 0) {
        q.addBindValue(currentId);
        if (q.exec() && q.next()) {
            int projectId = q.value(0).toInt();
            if (projectId > 0) {
                m_folderToProject.insert(folderId, projectId);
                return projectId;
            }
            currentId = q.value(1).toInt();
            q.finish();
        } else {
            break;
        }
    }
    return 0;
}

int ProjectDB::getProjectRootFolderId(int projectId) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM virtual_folders WHERE project_id=? AND parent_id=?");
    q.addBindValue(projectId);
    q.addBindValue(m_rootId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

// Version grouping

bool ProjectDB::setAssetVersionInfo(int assetId, const QString& versionGroupKey, const QString& versionString) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE assets SET version_group_key=?, version_string=? WHERE id=?");
    q.addBindValue(versionGroupKey);
    q.addBindValue(versionString);
    q.addBindValue(assetId);
    return q.exec();
}

QVector<QPair<int, QString>> ProjectDB::getVersionsForGroup(const QString& versionGroupKey, int projectId) const {
    QVector<QPair<int, QString>> result;
    QSqlQuery q(m_db);
    q.prepare(
        "SELECT a.id, a.version_string FROM assets a "
        "JOIN virtual_folders f ON a.virtual_folder_id = f.id "
        "WHERE a.version_group_key = ? AND f.project_id = ? "
        "ORDER BY a.version_string DESC"
    );
    q.addBindValue(versionGroupKey);
    q.addBindValue(projectId);
    if (q.exec()) {
        while (q.next()) {
            result.append({q.value(0).toInt(), q.value(1).toString()});
        }
    }
    return result;
}

// Notifications

int ProjectDB::addNotification(int projectId, int assetId, const QString& filePath) {
    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO notifications(project_id, asset_id, file_path) VALUES(?, ?, ?)");
    ins.addBindValue(projectId);
    ins.addBindValue(assetId);
    ins.addBindValue(filePath);
    
    if (!ins.exec()) {
        qWarning() << "ProjectDB addNotification failed:" << ins.lastError();
        return 0;
    }
    emit notificationsChanged();
    bool ok = false;
    int id = ins.lastInsertId().toInt(&ok);
    return ok ? id : 0;
}

bool ProjectDB::acknowledgeNotification(int notificationId) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE notifications SET acknowledged=1 WHERE id=?");
    q.addBindValue(notificationId);
    bool ok = q.exec();
    if (ok) emit notificationsChanged();
    return ok;
}

bool ProjectDB::acknowledgeAllNotifications(int projectId) {
    QSqlQuery q(m_db);
    if (projectId > 0) {
        q.prepare("UPDATE notifications SET acknowledged=1 WHERE project_id=? AND acknowledged=0");
        q.addBindValue(projectId);
    } else {
        q.prepare("UPDATE notifications SET acknowledged=1 WHERE acknowledged=0");
    }
    bool ok = q.exec();
    if (ok) emit notificationsChanged();
    return ok;
}

QVector<ProjectNotification> ProjectDB::getUnacknowledgedNotifications(int projectId) const {
    QVector<ProjectNotification> result;
    QSqlQuery q(m_db);
    if (projectId > 0) {
        q.prepare("SELECT id, project_id, asset_id, file_path, detected_at FROM notifications WHERE acknowledged=0 AND project_id=? ORDER BY detected_at DESC");
        q.addBindValue(projectId);
    } else {
        q.prepare("SELECT id, project_id, asset_id, file_path, detected_at FROM notifications WHERE acknowledged=0 ORDER BY detected_at DESC");
    }
    if (q.exec()) {
        while (q.next()) {
            ProjectNotification n;
            n.id = q.value(0).toInt();
            n.projectId = q.value(1).toInt();
            n.assetId = q.value(2).toInt();
            n.filePath = q.value(3).toString();
            n.detectedAt = q.value(4).toString();
            n.acknowledged = false;
            result.append(n);
        }
    }
    return result;
}

int ProjectDB::getUnacknowledgedCount(int projectId) const {
    QSqlQuery q(m_db);
    if (projectId > 0) {
        q.prepare("SELECT COUNT(*) FROM notifications WHERE acknowledged=0 AND project_id=?");
        q.addBindValue(projectId);
    } else {
        q.prepare("SELECT COUNT(*) FROM notifications WHERE acknowledged=0");
    }
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

bool ProjectDB::markNotificationsRead(int projectId) {
    return acknowledgeAllNotifications(projectId);
}

int ProjectDB::getUnreadNotificationCount(int projectId) const {
    return getUnacknowledgedCount(projectId);
}

// Asset operations

QList<int> ProjectDB::getAssetIdsInProject(int projectId) const {
    QList<int> result;
    QSqlQuery q(m_db);
    q.prepare(
        "SELECT a.id FROM assets a "
        "JOIN virtual_folders f ON a.virtual_folder_id = f.id "
        "WHERE f.project_id = ?"
    );
    q.addBindValue(projectId);
    if (q.exec()) {
        while (q.next()) {
            result.append(q.value(0).toInt());
        }
    }
    return result;
}

QList<int> ProjectDB::getAssetIdsInFolder(int folderId, bool recursive) const {
    QList<int> assetIds;
    if (folderId <= 0) {
        return assetIds;
    }

    if (recursive) {
        QList<int> folderIds;

        QSqlQuery q(m_db);
        q.prepare(
            "WITH RECURSIVE folder_tree AS ("
            "  SELECT id FROM virtual_folders WHERE id = ?"
            "  UNION ALL "
            "  SELECT vf.id FROM virtual_folders vf "
            "  JOIN folder_tree ft ON vf.parent_id = ft.id"
            ") SELECT id FROM folder_tree"
        );
        q.addBindValue(folderId);

        if (q.exec()) {
            while (q.next()) {
                bool ok = false;
                int id = q.value(0).toInt(&ok);
                if (ok) folderIds.append(id);
            }
        } else {
            qWarning() << "[ProjectDB] getAssetIdsInFolder: folder query failed:" << q.lastError();
            return assetIds;
        }

        if (!folderIds.isEmpty()) {
            QStringList placeholders;
            placeholders.reserve(folderIds.size());
            for (int i = 0; i < folderIds.size(); ++i) placeholders << "?";

            q.prepare(QString("SELECT id FROM assets WHERE virtual_folder_id IN (%1)").arg(placeholders.join(',')));
            for (int fid : folderIds) {
                q.addBindValue(fid);
            }

            if (q.exec()) {
                while (q.next()) {
                    bool ok = false;
                    int id = q.value(0).toInt(&ok);
                    if (ok) assetIds.append(id);
                }
            } else {
                qWarning() << "[ProjectDB] getAssetIdsInFolder: asset query failed:" << q.lastError();
            }
        }
    } else {
        QSqlQuery q(m_db);
        q.prepare("SELECT id FROM assets WHERE virtual_folder_id = ?");
        q.addBindValue(folderId);

        if (q.exec()) {
            while (q.next()) {
                bool ok = false;
                int id = q.value(0).toInt(&ok);
                if (ok) assetIds.append(id);
            }
        } else {
            qWarning() << "[ProjectDB] getAssetIdsInFolder: asset query failed:" << q.lastError();
        }
    }

    return assetIds;
}

int ProjectDB::ensureChildFolder(int parentId, const QString& name, int projectId) {
    if (parentId <= 0 || name.isEmpty()) {
        return 0;
    }

    QSqlQuery sel(m_db);
    sel.prepare("SELECT id FROM virtual_folders WHERE parent_id = ? AND name = ?");
    sel.addBindValue(parentId);
    sel.addBindValue(name);
    if (sel.exec() && sel.next()) {
        return sel.value(0).toInt();
    }

    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO virtual_folders(name, parent_id, project_id) VALUES(?, ?, ?)");
    ins.addBindValue(name);
    ins.addBindValue(parentId);
    ins.addBindValue(projectId > 0 ? projectId : QVariant());

    if (!ins.exec()) {
        qWarning() << "[ProjectDB] ensureChildFolder failed:" << ins.lastError();
        return 0;
    }

    int id = ins.lastInsertId().toInt();
    if (id > 0 && projectId > 0) {
        m_folderToProject.insert(id, projectId);
        emit projectFoldersChanged(projectId);
    }
    return id;
}

int ProjectDB::ensureFolderForPath(int projectId, const QString& absolutePath) {
    Project proj = getProject(projectId);
    if (proj.id <= 0 || proj.watchPath.isEmpty()) {
        return 0;
    }

    QString watchPath = QDir::cleanPath(proj.watchPath);
    QString dirPath = QDir::cleanPath(absolutePath);
    if (dirPath.isEmpty()) {
        dirPath = watchPath;
    }

    // Ensure the target path is inside the project
    QString watchLower = watchPath.toLower();
    QString dirLower = dirPath.toLower();
    if (!dirLower.startsWith(watchLower)) {
        return getProjectRootFolderId(projectId);
    }

    QString relativePath = QDir(watchPath).relativeFilePath(dirPath);
    if (relativePath == "." || relativePath.isEmpty()) {
        return getProjectRootFolderId(projectId);
    }

    QStringList segments = relativePath.split(QRegularExpression("[/\\\\]"), Qt::SkipEmptyParts);
    int currentId = getProjectRootFolderId(projectId);
    for (const QString& segment : segments) {
        currentId = ensureChildFolder(currentId, segment, projectId);
        if (currentId <= 0) {
            break;
        }
    }
    return currentId > 0 ? currentId : getProjectRootFolderId(projectId);
}

QString ProjectDB::getAssetFilePath(int assetId) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT file_path FROM assets WHERE id=?");
    q.addBindValue(assetId);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return QString();
}

int ProjectDB::getAssetFolderId(int assetId) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT virtual_folder_id FROM assets WHERE id=?");
    q.addBindValue(assetId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}


bool ProjectDB::removeAssets(const QList<int>& assetIds) {
    if (assetIds.isEmpty()) return true;
    
    QSqlQuery q(m_db);
    for (int id : assetIds) {
        q.prepare("DELETE FROM assets WHERE id=?");
        q.addBindValue(id);
        if (!q.exec()) {
            qWarning() << "ProjectDB removeAssets failed for id" << id << ":" << q.lastError();
        }
    }
    return true;
}

int ProjectDB::removeAssetsByPath(const QStringList& filePaths) {
    if (filePaths.isEmpty()) return 0;
    
    int removed = 0;
    QSet<int> affectedFolders;
    QSqlQuery q(m_db);
    
    for (const QString& path : filePaths) {
        // First get the folder ID so we can notify
        q.prepare("SELECT id, virtual_folder_id FROM assets WHERE file_path = ?");
        q.addBindValue(path);
        if (q.exec() && q.next()) {
            int assetId = q.value(0).toInt();
            int folderId = q.value(1).toInt();
            affectedFolders.insert(folderId);
            
            // Delete the asset
            QSqlQuery delQ(m_db);
            delQ.prepare("DELETE FROM assets WHERE id = ?");
            delQ.addBindValue(assetId);
            if (delQ.exec()) {
                removed++;
            }
        }
    }
    
    // Notify about changes
    for (int folderId : affectedFolders) {
        notifyAssetsChanged(folderId);
    }
    
    if (removed > 0) {
        qDebug() << "[ProjectDB] removeAssetsByPath: removed" << removed << "assets";
    }
    
    return removed;
}

bool ProjectDB::updateAssetPath(const QString& oldPath, const QString& newPath) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE assets SET file_path = ?, display_name = ? WHERE file_path = ?");
    q.addBindValue(newPath);
    q.addBindValue(QFileInfo(newPath).fileName());
    q.addBindValue(oldPath);
    
    if (q.exec() && q.numRowsAffected() > 0) {
        qDebug() << "[ProjectDB] updateAssetPath:" << oldPath << "->" << newPath;
        return true;
    }
    return false;
}

int ProjectDB::resyncAssetFolders(int projectId) {
    // Get the project's watch path (root directory)
    Project proj = getProject(projectId);
    if (proj.id <= 0 || proj.watchPath.isEmpty()) {
        qWarning() << "[ProjectDB] resyncAssetFolders: invalid project" << projectId;
        return 0;
    }
    
    QString watchPath = QDir::cleanPath(proj.watchPath);
    qDebug() << "[ProjectDB] resyncAssetFolders: project" << projectId << "watchPath:" << watchPath;
    
    // Build folder path -> folder ID mapping
    // First, get all folders for this project with their parent_id and name
    QHash<int, QString> folderNames;
    QHash<int, int> folderParents;
    QSqlQuery fq(m_db);
    fq.prepare("SELECT id, name, parent_id FROM virtual_folders WHERE project_id = ?");
    fq.addBindValue(projectId);
    if (!fq.exec()) {
        qWarning() << "[ProjectDB] resyncAssetFolders: folder query failed:" << fq.lastError();
        return 0;
    }
    while (fq.next()) {
        int fid = fq.value(0).toInt();
        folderNames.insert(fid, fq.value(1).toString());
        folderParents.insert(fid, fq.value(2).isNull() ? 0 : fq.value(2).toInt());
    }
    
    // Also get the project root folder (parent_id = global root)
    int projectRootId = getProjectRootFolderId(projectId);
    
    // Build full path for each folder by traversing up to root
    QHash<QString, int> pathToFolderId;  // normalized path -> folder ID
    for (auto it = folderNames.begin(); it != folderNames.end(); ++it) {
        int fid = it.key();
        QStringList pathParts;
        int current = fid;
        while (current > 0 && current != projectRootId && current != m_rootId) {
            if (folderNames.contains(current)) {
                pathParts.prepend(folderNames[current]);
            }
            current = folderParents.value(current, 0);
        }
        
        // Build the full path: watchPath + relative folder path
        QString folderPath;
        if (pathParts.isEmpty()) {
            folderPath = watchPath;
        } else {
            folderPath = watchPath + "/" + pathParts.join("/");
        }
        folderPath = QDir::cleanPath(folderPath);
        pathToFolderId.insert(folderPath.toLower(), fid);
    }
    // Also map the watch path itself to project root
    pathToFolderId.insert(watchPath.toLower(), projectRootId);
    
    qDebug() << "[ProjectDB] resyncAssetFolders: built" << pathToFolderId.size() << "folder path mappings";
    
    // Now get all assets and fix their folder associations
    QSqlQuery aq(m_db);
    aq.prepare(
        "SELECT a.id, a.file_path, a.virtual_folder_id "
        "FROM assets a "
        "JOIN virtual_folders f ON a.virtual_folder_id = f.id "
        "WHERE f.project_id = ?"
    );
    aq.addBindValue(projectId);
    if (!aq.exec()) {
        qWarning() << "[ProjectDB] resyncAssetFolders: asset query failed:" << aq.lastError();
        return 0;
    }
    
    int fixed = 0;
    int imported = 0;
    QSet<QString> existingPathsLower;
    QSet<int> changedFolders;
    QSqlQuery upd(m_db);
    upd.prepare("UPDATE assets SET virtual_folder_id = ? WHERE id = ?");
    
    while (aq.next()) {
        int assetId = aq.value(0).toInt();
        QString filePath = aq.value(1).toString();
        int currentFolderId = aq.value(2).toInt();
        existingPathsLower.insert(QDir::cleanPath(filePath).toLower());
        
        // Get the directory containing this file
        QString assetDir = QDir::cleanPath(QFileInfo(filePath).absolutePath()).toLower();
        
        // Find matching folder
        int correctFolderId = pathToFolderId.value(assetDir, 0);
        if (correctFolderId <= 0) {
            qDebug() << "[ProjectDB] resyncAssetFolders: no folder match for" << assetDir;
            continue;
        }
        
        if (correctFolderId != currentFolderId) {
            upd.addBindValue(correctFolderId);
            upd.addBindValue(assetId);
            if (upd.exec()) {
                fixed++;
                changedFolders.insert(correctFolderId);
                qDebug() << "[ProjectDB] resyncAssetFolders: fixed asset" << assetId 
                         << "from folder" << currentFolderId << "to" << correctFolderId;
            }
            upd.finish();
        }
    }

    // Scan filesystem for missing files and import them
    QDirIterator it(watchPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString filePath = QDir::cleanPath(it.next());
        QString normalized = filePath.toLower();
        if (existingPathsLower.contains(normalized)) {
            continue;
        }

        QFileInfo info(filePath);
        if (!info.exists()) continue;

        QString dirPath = info.absolutePath();
        int folderId = ensureFolderForPath(projectId, dirPath);
        if (folderId <= 0) folderId = projectRootId;

        int assetId = insertAssetMetadataFast(filePath, folderId);
        if (assetId > 0) {
            imported++;
            existingPathsLower.insert(normalized);
            changedFolders.insert(folderId);
            qDebug() << "[ProjectDB] resyncAssetFolders: imported missing file" << filePath;
        }
    }

    for (int fid : std::as_const(changedFolders)) {
        notifyAssetsChanged(fid);
    }
    
    qDebug() << "[ProjectDB] resyncAssetFolders: fixed" << fixed << "assets, imported" << imported;
    
    if (fixed > 0 || imported > 0) {
        emit projectAssetsChanged(projectId);
    }
    
    return fixed + imported;
}
