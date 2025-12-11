#include "project_import_worker.h"
#include "log_manager.h"

#include <QDebug>
#include <QUuid>
#include <QElapsedTimer>

// ============= ProjectImportWorker =============

ProjectImportWorker::ProjectImportWorker(QObject* parent)
    : QObject(parent)
{
    // Generate unique connection name for this worker
    m_connectionName = "pm_import_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// Helper to throttle progress signals (emit at most every 50ms)
namespace {
    qint64 lastProgressEmit = 0;
    constexpr qint64 PROGRESS_THROTTLE_MS = 50;
}

ProjectImportWorker::~ProjectImportWorker()
{
    closeDatabase();
}

void ProjectImportWorker::setDatabasePath(const QString& dbPath)
{
    m_dbPath = dbPath;
}

void ProjectImportWorker::setImportInfo(int projectId, int targetFolderId, const QString& sourcePath)
{
    m_projectId = projectId;
    m_targetFolderId = targetFolderId;
    m_sourcePath = sourcePath;
}

void ProjectImportWorker::requestCancel()
{
    m_cancelRequested.store(true);
}

bool ProjectImportWorker::openDatabase()
{
    if (m_db.isValid() && m_db.isOpen()) return true;
    
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(m_dbPath);
    
    if (!m_db.open()) {
        qWarning() << "[ProjectImportWorker] Failed to open database:" << m_db.lastError().text();
        return false;
    }
    
    // Enable foreign keys
    QSqlQuery q(m_db);
    q.exec("PRAGMA foreign_keys=ON;");
    
    return true;
}

void ProjectImportWorker::closeDatabase()
{
    if (m_db.isValid()) {
        QString connName = m_db.connectionName();
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(connName);
    }
}

void ProjectImportWorker::doImport()
{
    try {
        QElapsedTimer progressTimer;
        progressTimer.start();
        
        QElapsedTimer fileChangeTimer;
        fileChangeTimer.start();
        
        m_cancelRequested.store(false);
        
        if (m_sourcePath.isEmpty() || m_targetFolderId <= 0) {
            emit importFinished(false, "Invalid import parameters");
            return;
        }
        
        QDir dir(m_sourcePath);
        if (!dir.exists()) {
            emit importFinished(false, "Source folder does not exist");
            return;
        }
        
        if (!openDatabase()) {
            emit importFinished(false, "Failed to open database connection");
            return;
        }
        
        // Helper lambda to emit throttled progress (every 50ms)
        auto emitProgress = [this, &progressTimer](int current, int total, bool force = false) {
            if (force || progressTimer.elapsed() >= PROGRESS_THROTTLE_MS) {
                emit progressChanged(current, total);
                progressTimer.restart();
            }
        };
        
        // Helper lambda to emit throttled file change (every 100ms)
        auto emitFileChange = [this, &fileChangeTimer](const QString& fileName, bool force = false) {
            if (force || fileChangeTimer.elapsed() >= 100) {
                emit currentFileChanged(fileName);
                fileChangeTimer.restart();
            }
        };
        
        emit currentFolderChanged(QFileInfo(m_sourcePath).fileName());
        
        qDebug() << "[ProjectImportWorker] Starting import from" << m_sourcePath << "to folder" << m_targetFolderId;
        
        // Helper to normalize paths
        auto normalizePath = [](const QString& p) -> QString {
            return QDir::cleanPath(p).toLower();
        };
        
        // Phase 1: Build folder structure
        QHash<QString, int> folderIds;
        folderIds.insert(normalizePath(m_sourcePath), m_targetFolderId);
        
        QList<QString> pendingFolders;
        pendingFolders.push_back(m_sourcePath);
        
        while (!pendingFolders.isEmpty()) {
            if (m_cancelRequested.load()) {
                emit importFinished(false, "Import cancelled");
                closeDatabase();
                return;
            }
            
            QString curPath = pendingFolders.front();
            pendingFolders.pop_front();
        
        int parentId = folderIds.value(normalizePath(curPath));
        QDir d(curPath);
        
        auto subfolders = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& folderName : subfolders) {
            QString subPath = d.filePath(folderName);
            
            // Create folder in database
            QSqlQuery q(m_db);
            q.prepare("INSERT INTO virtual_folders (name, parent_id, project_id) VALUES (?, ?, ?)");
            q.addBindValue(folderName);
            q.addBindValue(parentId);
            q.addBindValue(m_projectId);
            
            int folderId = 0;
            if (q.exec()) {
                folderId = q.lastInsertId().toInt();
            } else {
                qWarning() << "[ProjectImportWorker] Failed to create folder:" << folderName << q.lastError().text();
                folderId = parentId;  // Fall back to parent
            }
            
            folderIds.insert(normalizePath(subPath), folderId);
            pendingFolders.push_back(subPath);
        }
    }
    
    // Phase 2: Count total files
    int totalFiles = 0;
    QHash<QString, QStringList> filesByDir;
    
    QDirIterator it(m_sourcePath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString fp = it.next();
        ++totalFiles;
        const QString folderPath = normalizePath(QFileInfo(fp).absolutePath());
        filesByDir[folderPath].append(fp);
    }
    
    if (totalFiles == 0) {
        emit importFinished(true, "No files to import");
        closeDatabase();
        return;
    }
    
    qDebug() << "[ProjectImportWorker] Found" << totalFiles << "files in" << filesByDir.size() << "directories";
    
    // Phase 3: Import files with progress updates
    // Note: We import ALL files individually (1:1 mapping to database rows).
    // Sequence detection/grouping happens at display time via ProjectSequenceGroupingProxyModel.
    qDebug() << "[ProjectImportWorker] Phase 3: Starting file import with transaction...";
    
    QSqlDatabase sdb = m_db;
    bool inTx = sdb.transaction();
    if (!inTx) {
        qWarning() << "[ProjectImportWorker] Failed to start transaction";
    }
    
    int currentFile = 0;
    int importedCount = 0;
    int dirCount = 0;
    
    for (auto dirIt = filesByDir.begin(); dirIt != filesByDir.end(); ++dirIt) {
        if (m_cancelRequested.load()) {
            if (inTx) sdb.rollback();
            emit importFinished(false, "Import cancelled");
            closeDatabase();
            return;
        }
        
        QString folderPath = dirIt.key();
        QStringList files = dirIt.value();
        int folderId = folderIds.value(folderPath, m_targetFolderId);
        
        dirCount++;
        if (dirCount % 5 == 0) {
            qDebug() << "[ProjectImportWorker] Processing directory" << dirCount << "of" << filesByDir.size();
        }
        
        // Import all files individually (no sequence detection at import time)
        for (const QString& filePath : files) {
            if (m_cancelRequested.load()) break;
            
            QFileInfo fi(filePath);
            emitFileChange(fi.fileName());
            
            QSqlQuery q(m_db);
            q.prepare(
                "INSERT OR REPLACE INTO assets "
                "(file_path, file_name, virtual_folder_id, file_size, file_type, "
                "last_modified, is_sequence, updated_at) "
                "VALUES (?, ?, ?, ?, ?, ?, 0, datetime('now'))"
            );
            q.addBindValue(filePath);
            q.addBindValue(fi.fileName());
            q.addBindValue(folderId);
            q.addBindValue(fi.size());
            q.addBindValue(fi.suffix().toLower());
            q.addBindValue(fi.lastModified().toString(Qt::ISODate));
            
            if (q.exec()) {
                importedCount++;
            } else {
                // Log first few errors only
                if (importedCount < 5) {
                    qWarning() << "[ProjectImportWorker] Insert failed:" << q.lastError().text();
                }
            }
            
            currentFile++;
            emitProgress(currentFile, totalFiles);
        }
    }
    
    qDebug() << "[ProjectImportWorker] Phase 3 complete. Committing transaction...";
    
    // Emit final progress
    emitProgress(totalFiles, totalFiles, true);
    emitFileChange("Committing...", true);
    
    // Commit transaction
    if (inTx) {
        if (!sdb.commit()) {
            qWarning() << "[ProjectImportWorker] Failed to commit transaction";
            emit importFinished(false, "Database commit failed");
            closeDatabase();
            return;
        }
    }
    
    qDebug() << "[ProjectImportWorker] Import completed:" << importedCount << "files imported";
    
    closeDatabase();
    emit importFinished(true, QString("Successfully imported %1 files").arg(importedCount));
    
    } catch (const std::exception& e) {
        qCritical() << "[ProjectImportWorker] Exception during import:" << e.what();
        closeDatabase();
        emit importFinished(false, QString("Import failed: %1").arg(e.what()));
    } catch (...) {
        qCritical() << "[ProjectImportWorker] Unknown exception during import";
        closeDatabase();
        emit importFinished(false, "Import failed with unknown error");
    }
}

// ============= ProjectImportController =============

ProjectImportController::ProjectImportController(QObject* parent)
    : QObject(parent)
{
}

ProjectImportController::~ProjectImportController()
{
    cancelImport();
    
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(5000);
        delete m_thread;
    }
}

void ProjectImportController::startImport(const QString& dbPath, int projectId, int targetFolderId,
                                          const QString& sourcePath)
{
    if (m_running) {
        qWarning() << "[ProjectImportController] Import already in progress";
        return;
    }
    
    // Create thread and worker
    m_thread = new QThread(this);
    m_worker = new ProjectImportWorker();
    m_worker->moveToThread(m_thread);
    
    // Setup worker
    m_worker->setDatabasePath(dbPath);
    m_worker->setImportInfo(projectId, targetFolderId, sourcePath);
    
    // Connect signals with QueuedConnection for thread safety
    connect(m_worker, &ProjectImportWorker::progressChanged, this, &ProjectImportController::progressChanged, Qt::QueuedConnection);
    connect(m_worker, &ProjectImportWorker::currentFileChanged, this, &ProjectImportController::currentFileChanged, Qt::QueuedConnection);
    connect(m_worker, &ProjectImportWorker::currentFolderChanged, this, &ProjectImportController::currentFolderChanged, Qt::QueuedConnection);
    connect(m_worker, &ProjectImportWorker::importFinished, this, [this](bool success, const QString& msg) {
        m_running = false;
        emit importFinished(success, msg);
        
        // Clean up thread
        if (m_thread) {
            m_thread->quit();
        }
    }, Qt::QueuedConnection);
    
    // Clean up worker when thread finishes
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, this, [this]() {
        m_thread->deleteLater();
        m_thread = nullptr;
        m_worker = nullptr;
    });
    
    // Start thread and invoke import
    m_running = true;
    m_thread->start();
    
    QMetaObject::invokeMethod(m_worker, &ProjectImportWorker::doImport, Qt::QueuedConnection);
}

void ProjectImportController::cancelImport()
{
    if (m_worker) {
        m_worker->requestCancel();
    }
}
