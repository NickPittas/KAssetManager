#include "importer.h"
#include "db.h"
#include "log_manager.h"
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QApplication>

#include <QSet>
#include <QSqlDatabase>

#include "file_utils.h"

Importer::Importer(QObject* parent): QObject(parent) {}

static QString norm(const QString& p){ return QFileInfo(p).absoluteFilePath(); }

bool Importer::isMediaFile(const QString& path){
    static const char* exts[] = {
        // Video formats
        ".mp4",".mov",".avi",".mkv",".wmv",".flv",".webm",".m4v",".mpg",".mpeg",".3gp",".mts",".m2ts",".ts",".vob",".ogv",
        // Common image formats
        ".jpg",".jpeg",".png",".gif",".bmp",".tiff",".tif",".webp",".svg",".ico",
        // RAW formats
        ".heic",".heif",".dng",".cr2",".cr3",".nef",".arw",".orf",".rw2",".pef",".srw",".raf",".raw",
        // HDR/EXR formats
        ".exr",".hdr",".pic",
        // Adobe formats
        ".psd",".psb",
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
    if (parentFolderId<=0) parentFolderId = DB::instance().ensureRootFolder();
    int assetId = DB::instance().insertAssetMetadataFast(norm(filePath), parentFolderId);
    if (assetId<=0) {
        return false;
    }
    LogManager::instance().addLog(QString("Imported %1").arg(QFileInfo(filePath).fileName()));
    return true;
}

bool Importer::importFolder(const QString& dirPath, int parentFolderId){
    QDir dir(dirPath); if (!dir.exists()) return false;
    if (parentFolderId<=0) parentFolderId = DB::instance().ensureRootFolder();
    QString topName = QFileInfo(dirPath).fileName(); if (topName.isEmpty()) topName = dir.dirName();
    int topId = DB::instance().createFolder(topName, parentFolderId);
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
            int id = DB::instance().createFolder(f, curId);
            folderIds.insert(sub, id);
            pending.push_back(sub);
        }
    }

    // Count total files first for progress reporting
    QDirIterator countIt(dirPath, QDir::Files, QDirIterator::Subdirectories);
    int totalFiles = 0;
    while(countIt.hasNext()) {
        countIt.next();
        if (isMediaFile(countIt.filePath())) totalFiles++;
    }

    // Collect all files first, grouped by directory
    QMap<QString, QStringList> filesByDir;
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while(it.hasNext()){
        QString fp = it.next();
        if (!isMediaFile(fp)) continue;
        QString folderPath = QFileInfo(fp).absolutePath();
        filesByDir[folderPath].append(fp);
    }

    // Begin a single transaction for bulk import
    QSqlDatabase sdb = DB::instance().database();
    bool inTx = sdb.transaction();
    if (!inTx) {
        qWarning() << "Importer::importFolder: failed to start transaction";
    }

    QSet<int> changedFolders; // aggregate folders to notify once at end

    // Process each directory's files, detecting sequences
    int currentFile = 0;
    for (auto dirIt = filesByDir.begin(); dirIt != filesByDir.end(); ++dirIt) {
        QString folderPath = dirIt.key();
        QStringList files = dirIt.value();
        int fid = folderIds.value(folderPath, topId);
        changedFolders.insert(fid);

        // Detect sequences in this directory
        QVector<ImageSequence> sequences = SequenceDetector::detectSequences(files);
        QSet<QString> sequenceFiles;

        // Import sequences (fast path: metadata only, assign folder in insert)
        for (const ImageSequence& seq : sequences) {
            emit currentFileChanged(seq.pattern);

            int seqId = DB::instance().upsertSequenceInFolderFast(seq.pattern, seq.startFrame, seq.endFrame, seq.frameCount, seq.firstFramePath, fid);
            if (seqId > 0) {
                qDebug() << "Imported sequence:" << seq.pattern << "frames:" << seq.startFrame << "-" << seq.endFrame;
            }

            // Mark all sequence files as processed and update progress for each frame
            for (const QString& framePath : seq.framePaths) {
                sequenceFiles.insert(framePath);
                currentFile++;
                emit progressChanged(currentFile, totalFiles);
                if ((currentFile % 200) == 0) QApplication::processEvents();
            }
        }

        // Import remaining non-sequence files (fast path)
        for (const QString& fp : files) {
            if (sequenceFiles.contains(fp)) continue; // Skip files that are part of sequences

            currentFile++;
            QString fileName = QFileInfo(fp).fileName();
            emit currentFileChanged(fileName);
            emit progressChanged(currentFile, totalFiles);
            if ((currentFile % 200) == 0) QApplication::processEvents();

            DB::instance().insertAssetMetadataFast(fp, fid);
        }
    }

    bool commitOk = inTx ? sdb.commit() : true;
    if (!commitOk) {
        qWarning() << "Importer::importFolder: commit failed";
    }

    // Emit a single assetsChanged per touched folder
    for (int fid : std::as_const(changedFolders)) {
        DB::instance().notifyAssetsChanged(fid);
    }

    LogManager::instance().addLog(QString("Imported folder %1").arg(topName));
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

    if (parentFolderId<=0) parentFolderId = DB::instance().ensureRootFolder();

    int imported = 0;
    int total = filePaths.size();

    QSqlDatabase sdb = DB::instance().database();
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
        if (((i + 1) % 200) == 0) QApplication::processEvents();

        // Import the file (fast metadata-only)
        if (isMediaFile(filePath) && DB::instance().insertAssetMetadataFast(filePath, parentFolderId) > 0) {
            ++imported;
        }
    }

    bool commitOk = inTx ? sdb.commit() : true;
    if (!commitOk) qWarning() << "Importer::importFiles: commit failed";

    // Notify view once for the target folder
    DB::instance().notifyAssetsChanged(parentFolderId);

    qDebug() << "Importer::importFiles() completed, imported" << imported << "of" << total << "files";
    LogManager::instance().addLog(QString("Import completed: %1 of %2 file%3").arg(imported).arg(total).arg(total==1?"":"s"));

    // Emit completion signal
    emit importFinished();
    emit importCompleted(imported);
}
