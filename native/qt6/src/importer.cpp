#include "importer.h"
#include "db.h"
#include "log_manager.h"
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QDebug>

Importer::Importer(QObject* parent): QObject(parent) {}

static QString norm(const QString& p){ return QFileInfo(p).absoluteFilePath(); }

bool Importer::isMediaFile(const QString& path){
    static const char* exts[] = {
        ".mp4",".mov",".avi",".mkv",".wmv",".flv",".webm",".m4v",".mpg",".mpeg",".3gp",".mts",".m2ts",".ts",".vob",".ogv",
        ".jpg",".jpeg",".png",".gif",".bmp",".tiff",".tif",".webp",".svg",".ico",".heic",".heif",".dng",".cr2",".nef",".arw",".orf",".rw2",".pef",".srw"
    };
    QString e = QFileInfo(path).suffix().toLower(); e.prepend('.');
    for (auto s: exts) if (e==s) return true; return false;
}

bool Importer::importPaths(const QStringList& paths){
    qDebug() << "Importer::importPaths() called with" << paths.size() << "paths";
    LogManager::instance().addLog(QString("Import requested (%1 item%2)").arg(paths.size()).arg(paths.size()==1?"":"s"));
    int imported=0;
    for (const auto& p: paths){
        QFileInfo fi(p);
        if (!fi.exists()) {
            qDebug() << "  Path does not exist:" << p;
            continue;
        }
        if (fi.isDir()) {
            qDebug() << "  Importing folder:" << p;
            if (importFolder(fi.absoluteFilePath())) ++imported;
        } else {
            qDebug() << "  Importing file:" << p;
            if (importFile(fi.absoluteFilePath())) ++imported;
        }
    }
    qDebug() << "Importer::importPaths() completed, imported" << imported << "items";
    emit importCompleted(imported);
    LogManager::instance().addLog(QString("Import completed: %1 item%2").arg(imported).arg(imported==1?"":"s"));
    return imported>0;
}

bool Importer::importFile(const QString& filePath, int parentFolderId){
    if (!QFileInfo::exists(filePath)) {
        qDebug() << "  importFile: file does not exist:" << filePath;
        return false;
    }
    if (!isMediaFile(filePath)) {
        qDebug() << "  importFile: not a media file:" << filePath;
        LogManager::instance().addLog(QString("Skipped non-media: %1").arg(QFileInfo(filePath).fileName()));
        return false;
    }
    int assetId = DB::instance().upsertAsset(norm(filePath));
    if (assetId<=0) {
        qDebug() << "  importFile: failed to upsert asset:" << filePath;
        return false;
    }
    if (parentFolderId<=0) parentFolderId = DB::instance().ensureRootFolder();
    bool ok = DB::instance().setAssetFolder(assetId, parentFolderId);
    qDebug() << "  importFile: imported" << filePath << "to folder" << parentFolderId << "assetId=" << assetId << "ok=" << ok;
    if (ok) {
        LogManager::instance().addLog(QString("Imported %1").arg(QFileInfo(filePath).fileName()));
    }
    return ok;
}

bool Importer::importFolder(const QString& dirPath, int parentFolderId){
    QDir dir(dirPath); if (!dir.exists()) return false;
    if (parentFolderId<=0) parentFolderId = DB::instance().ensureRootFolder();
    QString topName = QFileInfo(dirPath).fileName(); if (topName.isEmpty()) topName = dir.dirName();
    int topId = DB::instance().createFolder(topName, parentFolderId);
    if (topId<=0) return false;
    LogManager::instance().addLog(QString("Importing folder %1").arg(topName));

    // Build all subfolders first (breadth-first)
    QHash<QString,int> folderIds; folderIds.insert(dirPath, topId);
    QList<QString> pending; pending.push_back(dirPath);
    while(!pending.isEmpty()){
        QString cur = pending.front(); pending.pop_front(); int curId = folderIds.value(cur);
        QDir d(cur);
        auto folders = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const auto& f: folders){ QString sub = QDir(cur).filePath(f); int id = DB::instance().createFolder(f, curId); folderIds.insert(sub, id); pending.push_back(sub); }
    }
    // Add files to corresponding folders
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while(it.hasNext()){
        QString fp = it.next(); if (!isMediaFile(fp)) continue;
        QString folderPath = QFileInfo(fp).absolutePath(); int fid = folderIds.value(folderPath, topId);
        int assetId = DB::instance().upsertAsset(fp); if (assetId>0) DB::instance().setAssetFolder(assetId, fid);
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
        if (!QFileInfo::exists(path)) {
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
