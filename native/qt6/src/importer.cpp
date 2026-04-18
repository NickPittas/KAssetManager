#include "importer.h"
#include "db.h"
#include "i_asset_database.h"
#include "log_manager.h"
#include "project_path_utils.h"
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QApplication>
#include <QElapsedTimer>

#include <QSet>
#include <QSqlDatabase>

#include "file_utils.h"

Importer::Importer(QObject* parent, IAssetDatabase* db)
    : QObject(parent)
    , m_db(db ? *db : static_cast<IAssetDatabase&>(DB::instance()))
{
    // ⚠️ CRITICAL: m_db defaults to DB::instance() for backward compatibility
    // with existing Asset Manager code. For Project Manager, pass ProjectDB::instance().
}

void Importer::setSequenceDetectionEnabled(bool enabled)
{
    m_sequenceDetectionEnabled = enabled;
}

static QString norm(const QString& p){ return QFileInfo(p).absoluteFilePath(); }

bool Importer::isMediaFile(const QString& path){
    static const char* exts[] = {
        // Video formats
        ".mp4",".mov",".avi",".mkv",".wmv",".flv",".webm",".m4v",".mpg",".mpeg",".3gp",".mts",".m2ts",".ts",".vob",".ogv",".mxf",
        // Common image formats
        ".jpg",".jpeg",".png",".gif",".bmp",".tiff",".tif",".webp",".svg",".ico",
        // RAW formats
        ".heic",".heif",".dng",".cr2",".cr3",".nef",".arw",".orf",".rw2",".pef",".srw",".raf",".raw",
        // HDR/EXR formats
        ".exr",".hdr",".pic",
        // Adobe formats
        ".psd",".psb",".aep",".aepx",
        // Nuke formats
        ".nk",
        // Other formats
        ".tga",".pcx",".pbm",".pgm",".ppm",".pnm",".avif",".jxl"
    };
    QString e = QFileInfo(path).suffix().toLower(); e.prepend('.');
    for (auto s: exts) if (e==s) return true; return false;
}

bool Importer::importPaths(const QStringList& paths){
    LogManager::instance().addLog(QString("Import requested (%1 item%2)").arg(paths.size()).arg(paths.size()==1?"":"s"));
    int imported=0;
    for (const auto& p: paths){
        QFileInfo fi(p);
        if (!fi.exists()) {
            continue;
        }
        if (fi.isDir()) {
            if (importFolder(fi.absoluteFilePath())) ++imported;
        } else {
            if (importFile(fi.absoluteFilePath())) ++imported;
        }
    }
    emit importCompleted(imported);
    LogManager::instance().addLog(QString("Import completed: %1 item%2").arg(imported).arg(imported==1?"":"s"));
    return imported>0;
}

bool Importer::importFile(const QString& filePath, int parentFolderId){
    if (!FileUtils::fileExists(filePath)) {
        return false;
    }
    if (!isMediaFile(filePath)) {
        return false;
    }
    if (parentFolderId<=0) parentFolderId = m_db.ensureRootFolder();
    int assetId = m_db.insertAssetMetadataFast(norm(filePath), parentFolderId);
    if (assetId<=0) {
        return false;
    }
    LogManager::instance().addLog(QString("Imported %1").arg(QFileInfo(filePath).fileName()));
    return true;
}

bool Importer::importFolder(const QString& dirPath, int parentFolderId){
    QDir dir(dirPath); if (!dir.exists()) return false;
    if (parentFolderId<=0) parentFolderId = m_db.ensureRootFolder();
    QString topName = QFileInfo(dirPath).fileName(); if (topName.isEmpty()) topName = dir.dirName();
    int topId = m_db.createFolder(topName, parentFolderId);
    if (topId<=0) return false;

    // Emit folder name for progress dialog
    emit currentFolderChanged(topName);

    LogManager::instance().addLog(QString("Importing folder %1").arg(topName));

    // Build all subfolders first (breadth-first)
    QHash<QString,int> folderIds; folderIds.insert(dirPath, topId);
    QList<QString> pending; pending.push_back(dirPath);
    while(!pending.isEmpty()){
        QString cur = pending.front(); pending.pop_front(); int curId = folderIds.value(cur);
        QDir d(cur);
        auto folders = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto& f: folders){
            QString sub = QDir(cur).filePath(f);
            int id = m_db.createFolder(f, curId);
            folderIds.insert(sub, id);
            pending.push_back(sub);
        }
    }

    // Single-pass directory iteration: collect files by directory and count total
    int totalFiles = 0;
    QHash<QString, QStringList> filesByDir;
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString fp = it.next();
        if (!isMediaFile(fp)) continue;
        ++totalFiles;
        const QString folderPath = QFileInfo(fp).absolutePath();
        filesByDir[folderPath].append(fp);
    }

    // Begin a single transaction for bulk import
    QSqlDatabase sdb = m_db.database();
    bool inTx = sdb.transaction();
    if (!inTx) {
        qWarning() << "Importer::importFolder: failed to start transaction";
    }

    QSet<int> changedFolders; // aggregate folders to notify once at end

    // Process each directory's files, detecting sequences
    int currentFile = 0;
    QElapsedTimer uiTimer;
    uiTimer.start();
    const int UI_UPDATE_INTERVAL_MS = 50;  // Update UI every 50ms
    
    // Emit initial progress to show dialog is working
    emit progressChanged(0, totalFiles);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    
    for (auto dirIt = filesByDir.begin(); dirIt != filesByDir.end(); ++dirIt) {
        QString folderPath = dirIt.key();
        QStringList files = dirIt.value();
        int fid = folderIds.value(folderPath, topId);
        changedFolders.insert(fid);

        QSet<QString> sequenceFiles;

        if (m_sequenceDetectionEnabled) {
            QVector<ImageSequence> sequences = SequenceDetector::detectSequences(files);

            // Import sequences (fast path: metadata only, assign folder in insert)
            for (const ImageSequence& seq : sequences) {
                emit currentFileChanged(seq.pattern);

                int seqId = m_db.upsertSequenceInFolderFast(seq.pattern, seq.startFrame, seq.endFrame, seq.frameCount, seq.firstFramePath, fid, seq.hasGaps, seq.gapCount, seq.version);
                if (seqId > 0) {
                    qDebug() << "Imported sequence:" << seq.pattern << "frames:" << seq.startFrame << "-" << seq.endFrame;
                    if (seq.hasGaps) {
                        qDebug() << "  WARNING: Sequence has" << seq.gapCount << "gap(s)," << seq.missingFrames.size() << "missing frames";
                    }
                }

                // Mark all sequence files as processed and update progress for each frame
                for (const QString& framePath : seq.framePaths) {
                    sequenceFiles.insert(framePath);
                    currentFile++;
                    emit progressChanged(currentFile, totalFiles);
                    
                    // Periodically update UI
                    if (uiTimer.elapsed() >= UI_UPDATE_INTERVAL_MS) {
                        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                        uiTimer.restart();
                    }
                }
            }
        }

        // Import remaining non-sequence files (fast path)
        for (const QString& fp : files) {
            if (sequenceFiles.contains(fp)) continue; // Skip files that are part of sequences

            // Insert into database
            m_db.insertAssetMetadataFast(fp, fid);
            currentFile++;

            // Throttle UI updates - emit signal and process events periodically
            if (uiTimer.elapsed() >= UI_UPDATE_INTERVAL_MS) {
                emit currentFileChanged(QFileInfo(fp).fileName());
                emit progressChanged(currentFile, totalFiles);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                uiTimer.restart();
            }
        }
    }
    
    // Final progress update
    emit progressChanged(totalFiles, totalFiles);

    bool commitOk = inTx ? sdb.commit() : true;
    if (!commitOk) {
        qWarning() << "Importer::importFolder: commit failed";
    }

    // Emit a single assetsChanged per touched folder
    for (int fid : std::as_const(changedFolders)) {
        m_db.notifyAssetsChanged(fid);
    }

    LogManager::instance().addLog(QString("Imported folder %1").arg(topName));
    return true;
}

bool Importer::importFolderContents(const QString& dirPath, int targetFolderId) {
    QDir dir(dirPath); 
    if (!dir.exists()) {
        LogManager::instance().addLog(QString("[PM Import] Directory does not exist: %1").arg(dirPath), "ERROR");
        return false;
    }
    if (targetFolderId <= 0) targetFolderId = m_db.ensureRootFolder();

    LogManager::instance().addLog(QString("[PM Import] Starting import from %1 into folder %2").arg(dirPath).arg(targetFolderId), "INFO");
    emit currentFolderChanged(QFileInfo(dirPath).fileName());
    LogManager::instance().addLog(QString("Importing folder contents from %1").arg(dirPath));

    // Helper to normalize paths for consistent comparison
    auto normalizePath = [](const QString& p) -> QString {
        return ProjectPathUtils::keyForPath(p);
    };

    // Build subfolders directly under target folder (breadth-first)
    QHash<QString, int> folderIds;  // normalized path -> folder ID
    folderIds.insert(normalizePath(dirPath), targetFolderId);
    QList<QString> pending; 
    pending.push_back(dirPath);
    
    while (!pending.isEmpty()) {
        QString cur = pending.front(); 
        pending.pop_front(); 
        int curId = folderIds.value(normalizePath(cur));
        QDir d(cur);
        auto folders = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto& f : folders) {
            QString sub = QDir(cur).filePath(f);
            int id = m_db.createFolder(f, curId);
            folderIds.insert(normalizePath(sub), id);
            pending.push_back(sub);
        }
    }

    // Single-pass directory iteration: collect files by directory and count total
    // Import ALL files, not just media files - PM should work like a file manager
    int totalFiles = 0;
    QHash<QString, QStringList> filesByDir;  // normalized path -> files
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString fp = it.next();
        ++totalFiles;
        const QString folderPath = normalizePath(QFileInfo(fp).absolutePath());
        filesByDir[folderPath].append(fp);
    }
    
    LogManager::instance().addLog(QString("[PM Import] Found %1 files in %2 directories, sequenceDetection=%3")
        .arg(totalFiles).arg(filesByDir.count()).arg(m_sequenceDetectionEnabled ? "ON" : "OFF"), "INFO");

    // Begin a single transaction for bulk import
    QSqlDatabase sdb = m_db.database();
    bool inTx = sdb.transaction();
    if (!inTx) {
        qWarning() << "Importer::importFolderContents: failed to start transaction";
    }

    QSet<int> changedFolders;

    // Process each directory's files, detecting sequences
    int currentFile = 0;
    QElapsedTimer uiTimer;
    uiTimer.start();
    const int UI_UPDATE_INTERVAL_MS = 50;  // Update UI every 50ms
    
    // Emit initial progress to show dialog is working
    emit progressChanged(0, totalFiles);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    
    for (auto dirIt = filesByDir.begin(); dirIt != filesByDir.end(); ++dirIt) {
        QString folderPath = dirIt.key();
        QStringList files = dirIt.value();
        int fid = folderIds.value(folderPath, targetFolderId);
        
        // Debug: log folder path to ID mapping
        if (fid == targetFolderId && folderPath != normalizePath(dirPath)) {
            qWarning() << "[Importer] Folder path not found in map:" << folderPath << "- using target folder" << targetFolderId;
        }
        
        changedFolders.insert(fid);

        QSet<QString> sequenceFiles;
        if (m_sequenceDetectionEnabled) {
            QVector<ImageSequence> sequences = SequenceDetector::detectSequences(files);
            LogManager::instance().addLog(QString("[PM Import] Folder %1: detected %2 sequences from %3 files")
                .arg(folderPath).arg(sequences.size()).arg(files.size()), "DEBUG");

            // Import sequences
            for (const ImageSequence& seq : sequences) {
                emit currentFileChanged(seq.pattern);
                int seqId = m_db.upsertSequenceInFolderFast(seq.pattern, seq.startFrame, seq.endFrame, seq.frameCount, seq.firstFramePath, fid, seq.hasGaps, seq.gapCount, seq.version);
                if (seqId > 0) {
                    LogManager::instance().addLog(QString("[PM Import] Imported sequence: %1 [%2-%3] -> id %4")
                        .arg(seq.pattern).arg(seq.startFrame).arg(seq.endFrame).arg(seqId), "DEBUG");
                }
                for (const QString& framePath : seq.framePaths) {
                    sequenceFiles.insert(framePath);
                    currentFile++;
                    emit progressChanged(currentFile, totalFiles);
                    
                    // Periodically update UI
                    if (uiTimer.elapsed() >= UI_UPDATE_INTERVAL_MS) {
                        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                        uiTimer.restart();
                    }
                }
            }
        }

        // Import remaining non-sequence files
        for (const QString& fp : files) {
            if (sequenceFiles.contains(fp)) continue;
            
            // Insert into database
            m_db.insertAssetMetadataFast(fp, fid);
            currentFile++;
            
            // Throttle UI updates - emit signal and process events periodically
            if (uiTimer.elapsed() >= UI_UPDATE_INTERVAL_MS) {
                emit currentFileChanged(QFileInfo(fp).fileName());
                emit progressChanged(currentFile, totalFiles);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                uiTimer.restart();
            }
        }
    }
    
    // Final progress update
    emit progressChanged(totalFiles, totalFiles);

    bool commitOk = inTx ? sdb.commit() : true;
    if (!commitOk) {
        qWarning() << "Importer::importFolderContents: commit failed";
    }

    for (int fid : std::as_const(changedFolders)) {
        m_db.notifyAssetsChanged(fid);
    }

    LogManager::instance().addLog(QString("[PM Import] Completed import of %1 files from %2").arg(currentFile).arg(dirPath), "INFO");
    return true;
}

int Importer::purgeMissingAssets() {
    QSqlDatabase db = DB::instance().database();
    QSqlQuery select(db);
    if (!select.exec("SELECT id, file_path, virtual_folder_id FROM assets")) {
        qWarning() << "purgeMissingAssets select failed:" << select.lastError();
        return 0;
    }
    int removed = 0;
    while (select.next()) {
        int id = select.value(0).toInt();
        QString path = select.value(1).toString();
        int folderId = select.value(2).toInt();
        if (!FileUtils::fileExists(path)) {
            QSqlQuery del(db);
            del.prepare("DELETE FROM assets WHERE id=?");
            del.addBindValue(id);
            if (del.exec()) {
                ++removed;
                emit DB::instance().assetsChanged(folderId);
            }
        }
    }
    LogManager::instance().addLog(QString("Purged %1 missing asset(s)").arg(removed));
    return removed;
}

int Importer::purgeAutotestAssets() {
    QSqlDatabase db = DB::instance().database();
    QSqlQuery del(db);
    if (!del.exec("DELETE FROM assets WHERE file_name LIKE 'autotest_%' OR file_path LIKE '%kasset_autotest%'")) {
        qWarning() << "purgeAutotestAssets failed:" << del.lastError();
        return 0;
    }
    // Conservative: signal full refresh
    emit DB::instance().assetsChanged(DB::instance().ensureRootFolder());
    int affected = del.numRowsAffected();
    LogManager::instance().addLog(QString("Purged autotest assets (%1)").arg(affected));
    return affected;
}

void Importer::importFiles(const QStringList& filePaths, int parentFolderId)
{
    qDebug() << "Importer::importFiles() called with" << filePaths.size() << "files, folderId:" << parentFolderId;
    LogManager::instance().addLog(QString("Importing %1 file%2...").arg(filePaths.size()).arg(filePaths.size()==1?"":"s"));

    if (parentFolderId<=0) parentFolderId = m_db.ensureRootFolder();

    int imported = 0;
    int total = filePaths.size();

    QSqlDatabase sdb = m_db.database();
    bool inTx = sdb.transaction();
    if (!inTx) qWarning() << "Importer::importFiles: failed to start transaction";

    for (int i = 0; i < total; ++i) {
        const QString& filePath = filePaths[i];

        // Emit current file name
        QString fileName = QFileInfo(filePath).fileName();
        emit currentFileChanged(fileName);

        // Emit progress
        emit progressChanged(i + 1, total);

        // Throttle event pumping to every 200 files

        // Import the file (fast metadata-only)
        if (isMediaFile(filePath) && m_db.insertAssetMetadataFast(filePath, parentFolderId) > 0) {
            ++imported;
        }
    }

    bool commitOk = inTx ? sdb.commit() : true;
    if (!commitOk) qWarning() << "Importer::importFiles: commit failed";

    // Notify view once for the target folder
    m_db.notifyAssetsChanged(parentFolderId);

    qDebug() << "Importer::importFiles() completed, imported" << imported << "of" << total << "files";
    LogManager::instance().addLog(QString("Import completed: %1 of %2 file%3").arg(imported).arg(total).arg(total==1?"":"s"));

    // Emit completion signal
    emit importFinished();
    emit importCompleted(imported);
}
