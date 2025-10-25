#include "assets_model.h"
#include "db.h"
#include "thumbnail_generator.h"
#include "progress_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDebug>

AssetsModel::AssetsModel(QObject* parent): QAbstractListModel(parent){
    connect(&DB::instance(), &DB::assetsChanged, this, [this](int fid){ if (fid==m_folderId) reload(); });

    // Connect to thumbnail generator signals
    connect(&ThumbnailGenerator::instance(), &ThumbnailGenerator::thumbnailGenerated,
            this, &AssetsModel::onThumbnailGenerated);

    rebuildFilter();
}

QVariant AssetsModel::data(const QModelIndex& idx, int role) const{
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_filteredRowIndexes.size())
        return {};
    const auto& r = m_rows[m_filteredRowIndexes[idx.row()]];
    switch(role){
        case IdRole: return r.id;
        case FileNameRole: return r.fileName;
        case FilePathRole: return r.filePath;
        case FileSizeRole: return QVariant::fromValue<qlonglong>(r.fileSize);
        case ThumbnailPathRole: {
            // Check if thumbnail is cached
            QString thumbPath = ThumbnailGenerator::instance().getThumbnailPath(r.filePath);
            static const bool diagEnabled = !qEnvironmentVariableIsEmpty("KASSET_DIAGNOSTICS");
            if (diagEnabled) {
                qDebug() << "[AssetsModel] data() thumbnailPath role for" << r.fileName << "cached?" << !thumbPath.isEmpty();
            }

            // If not cached, request async generation
            if (thumbPath.isEmpty()) {
                // Request thumbnail generation in background (non-blocking)
                ThumbnailGenerator::instance().requestThumbnail(r.filePath);
                if (diagEnabled) {
                    qDebug() << "[AssetsModel] requested thumbnail generation for" << r.fileName;
                }
            }

            return thumbPath;
        }
        case FileTypeRole: return r.fileType;
        case LastModifiedRole: return r.lastModified;
    }
    return {};
}

QHash<int,QByteArray> AssetsModel::roleNames() const{
    QHash<int,QByteArray> r;
    r[IdRole]="assetId";
    r[FileNameRole]="fileName";
    r[FilePathRole]="filePath";
    r[FileSizeRole]="fileSize";
    r[ThumbnailPathRole]="thumbnailPath";
    r[FileTypeRole] = "fileType";
    r[LastModifiedRole] = "lastModified";
    return r;
}

void AssetsModel::setFolderId(int id){
    if (m_folderId==id) return;
    m_folderId=id;
    qDebug() << "AssetsModel::setFolderId" << id;
    reload();
    emit folderIdChanged();
}

void AssetsModel::setSearchQuery(const QString& query) {
    QString normalized = query;
    if (normalized == m_searchQuery)
        return;
    m_searchQuery = normalized;
    beginResetModel();
    rebuildFilter();
    endResetModel();
    emit searchQueryChanged();
}

void AssetsModel::reload(){
    qDebug() << "AssetsModel::reload() for folderId" << m_folderId;
    beginResetModel();
    query();
    rebuildFilter();
    endResetModel();
    qDebug() << "AssetsModel::reload() loaded" << m_rows.size() << "assets";

    // Count how many thumbnails need to be generated
    int needThumbnails = 0;
    for (const auto& row : m_rows) {
        if (ThumbnailGenerator::instance().getThumbnailPath(row.filePath).isEmpty()) {
            needThumbnails++;
        }
    }

    // Start progress tracking in ThumbnailGenerator
    if (needThumbnails > 0) {
        ThumbnailGenerator::instance().startProgress(needThumbnails);
        qDebug() << "AssetsModel::reload() starting progress for" << needThumbnails << "thumbnails";
    }
}

void AssetsModel::query(){
    m_rows.clear();
    if (m_folderId<=0) {
        qDebug() << "AssetsModel::query() skipped - invalid folderId" << m_folderId;
        m_filteredRowIndexes.clear();
        return;
    }
    QSqlQuery q(DB::instance().database());
    q.prepare("SELECT id,file_name,file_path,file_size FROM assets WHERE virtual_folder_id=? ORDER BY file_name");
    q.addBindValue(m_folderId);
    if (!q.exec()) {
        qWarning() << "AssetsModel::query() SQL error:" << q.lastError();
        return;
    }
    while (q.next()) {
        AssetRow r;
        r.id = q.value(0).toInt();
        r.fileName = q.value(1).toString();
        r.filePath = q.value(2).toString();
        r.fileSize = q.value(3).toLongLong();
        r.folderId = m_folderId;
        QFileInfo fi(r.filePath);
        r.fileType = fi.exists() ? fi.suffix().toLower() : QString();
        r.lastModified = fi.exists() ? fi.lastModified() : QDateTime();
        m_rows.push_back(r);
    }
    qDebug() << "AssetsModel::query() found" << m_rows.size() << "assets for folderId" << m_folderId;
}

void AssetsModel::onThumbnailGenerated(const QString& filePath, const QString& thumbnailPath) {
    // Find the row with this file path and update it
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].filePath == filePath) {
            int filteredRow = m_filteredRowIndexes.indexOf(i);
            if (filteredRow >= 0) {
                qDebug() << "AssetsModel::onThumbnailGenerated updating row" << filteredRow << "for" << filePath;
                QModelIndex idx = index(filteredRow, 0);
                emit dataChanged(idx, idx, {ThumbnailPathRole});
            }
            break;
        }
    }
}

bool AssetsModel::moveAssetToFolder(int assetId, int folderId){ bool ok=DB::instance().setAssetFolder(assetId,folderId); if (ok) reload(); return ok; }

bool AssetsModel::moveAssetsToFolder(const QVariantList& assetIds, int folderId){ bool any=false; for (const auto& v: assetIds){ any |= DB::instance().setAssetFolder(v.toInt(),folderId); } reload(); return any; }

void AssetsModel::rebuildFilter() {
    m_filteredRowIndexes.clear();
    m_filteredRowIndexes.reserve(m_rows.size());

    if (m_searchQuery.trimmed().isEmpty()) {
        for (int i = 0; i < m_rows.size(); ++i) {
            m_filteredRowIndexes.append(i);
        }
        return;
    }

    const QString needle = m_searchQuery.trimmed();
    for (int i = 0; i < m_rows.size(); ++i) {
        if (matchesFilter(m_rows[i])) {
            m_filteredRowIndexes.append(i);
        }
    }
}

bool AssetsModel::matchesFilter(const AssetRow& row) const {
    const QString needle = m_searchQuery.trimmed();
    const QString needleLower = needle.toLower();
    if (needle.isEmpty())
        return true;
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    if (row.fileName.contains(needle, cs))
        return true;
    if (row.filePath.contains(needle, cs))
        return true;
    if (!row.fileType.isEmpty() && row.fileType.contains(needle, cs))
        return true;
    if (row.lastModified.isValid() && row.lastModified.toString("yyyy-MM-dd hh:mm").toLower().contains(needleLower))
        return true;
    return false;
}
QVariantMap AssetsModel::get(int row) const {
    QVariantMap map;
    if (row < 0 || row >= m_filteredRowIndexes.size())
        return map;
    const auto &r = m_rows[m_filteredRowIndexes[row]];
    map.insert("assetId", r.id);
    map.insert("fileName", r.fileName);
    map.insert("filePath", r.filePath);
    map.insert("fileSize", QVariant::fromValue<qlonglong>(r.fileSize));
    map.insert("thumbnailPath", ThumbnailGenerator::instance().getThumbnailPath(r.filePath));
    map.insert("fileType", r.fileType);
    map.insert("lastModified", r.lastModified);
    return map;
}
