#include "assets_model.h"
#include "db.h"
#include "thumbnail_generator.h"
#include "progress_manager.h"
#include "log_manager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDebug>
#include <QThread>
#include <QElapsedTimer>
#include <QMimeData>

AssetsModel::AssetsModel(QObject* parent): QAbstractListModel(parent){
    // Debounce DB-driven reloads to avoid re-entrancy and view churn during batch imports
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(100);
    connect(&m_reloadTimer, &QTimer::timeout, this, &AssetsModel::triggerDebouncedReload);

    connect(&DB::instance(), &DB::assetsChanged, this, &AssetsModel::onAssetsChangedForFolder);

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
        case RatingRole: return r.rating;
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
    r[RatingRole] = "rating";
    return r;
}

Qt::ItemFlags AssetsModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags defaultFlags = QAbstractListModel::flags(index);
    if (index.isValid()) {
        return defaultFlags | Qt::ItemIsDragEnabled;
    }
    return defaultFlags;
}

QMimeData *AssetsModel::mimeData(const QModelIndexList &indexes) const
{
    QMimeData *mimeData = new QMimeData();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);

    // Encode asset IDs
    QList<int> assetIds;
    for (const QModelIndex &index : indexes) {
        if (index.isValid()) {
            int assetId = data(index, IdRole).toInt();
            assetIds.append(assetId);
        }
    }

    stream << assetIds;
    mimeData->setData("application/x-kasset-asset-ids", encodedData);

    qDebug() << "AssetsModel::mimeData() - Dragging" << assetIds.size() << "assets:" << assetIds;

    return mimeData;
}

Qt::DropActions AssetsModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

void AssetsModel::setFolderId(int id){
    if (m_folderId==id) return;
    m_folderId=id;
    qDebug() << "AssetsModel::setFolderId" << id;
    scheduleReload();
    emit folderIdChanged();
}

void AssetsModel::setSearchQuery(const QString& query) {
    QString normalized = query;
    if (normalized == m_searchQuery)
        return;
    m_searchQuery = normalized;
    // If search is active, broaden query across all assets so tag name search works globally
    reload();
    emit searchQueryChanged();
}

void AssetsModel::setTypeFilter(int f) {
    if (m_typeFilter == f) return;
    m_typeFilter = f;
    beginResetModel();
    rebuildFilter();
    endResetModel();
    emit typeFilterChanged();
}

void AssetsModel::setSelectedTagNames(const QStringList& tags) {
    if (m_selectedTagNames == tags) return;
    m_selectedTagNames = tags;
    // Changing tag selection may require loading assets across folders
    beginResetModel();
    rebuildFilter();
    endResetModel();
    emit selectedTagNamesChanged();
}

void AssetsModel::setTagFilterMode(int mode) {
    if (m_tagFilterMode == mode) return;
    m_tagFilterMode = mode;
    beginResetModel();
    rebuildFilter();
    endResetModel();
    emit tagFilterModeChanged();
}

void AssetsModel::reload(){
    qDebug() << "AssetsModel::reload() for folderId" << m_folderId << "on thread" << QThread::currentThread();
    QElapsedTimer t; t.start();
    beginResetModel();
    query();
    rebuildFilter();
    endResetModel();
    qDebug() << "AssetsModel::reload() loaded" << m_rows.size() << "assets in" << t.elapsed() << "ms";
    LogManager::instance().addLog(QString("AssetsModel reload: %1 assets in %2 ms").arg(m_rows.size()).arg(t.elapsed()), "DEBUG");
}

void AssetsModel::query(){
    m_rows.clear();

    const bool globalScope = !m_selectedTagNames.isEmpty() || !m_searchQuery.trimmed().isEmpty();

    QSqlQuery q(DB::instance().database());
    if (globalScope) {
        LogManager::instance().addLog("DB query (all assets) started", "DEBUG");
        q.prepare("SELECT id,file_name,file_path,file_size,COALESCE(rating,-1),virtual_folder_id FROM assets ORDER BY file_name");
    } else {
        if (m_folderId<=0) {
            qDebug() << "AssetsModel::query() skipped - invalid folderId" << m_folderId;
            m_filteredRowIndexes.clear();
            return;
        }
        q.prepare("SELECT id,file_name,file_path,file_size,COALESCE(rating,-1),virtual_folder_id FROM assets WHERE virtual_folder_id=? ORDER BY file_name");
        LogManager::instance().addLog(QString("DB query (assets by folder %1) started").arg(m_folderId), "DEBUG");
        q.addBindValue(m_folderId);
    }
    if (!q.exec()) {
        qWarning() << "AssetsModel::query() SQL error:" << q.lastError();
        return;
    }
    int rows = 0;
    while (q.next()) {
        AssetRow r;
        r.id = q.value(0).toInt();
        r.fileName = q.value(1).toString();
        r.filePath = q.value(2).toString();
        r.fileSize = q.value(3).toLongLong();
        r.folderId = q.value(5).toInt();
        r.rating = q.value(4).toInt();
        QFileInfo fi(r.filePath);
        r.fileType = fi.exists() ? fi.suffix().toLower() : QString();
        r.lastModified = fi.exists() ? fi.lastModified() : QDateTime();
        m_rows.push_back(r);
        ++rows;
    }
    LogManager::instance().addLog(QString("DB query complete: %1 rows").arg(rows), "DEBUG");
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

bool AssetsModel::moveAssetsToFolder(const QVariantList& assetIds, int folderId){ bool any=false; for (const auto& v: assetIds){ any |= DB::instance().setAssetFolder(v.toInt(),folderId); } scheduleReload(); return any; }

bool AssetsModel::removeAssets(const QVariantList& assetIds){ QList<int> ids; for (const auto &v: assetIds) ids << v.toInt(); bool ok = DB::instance().removeAssets(ids); scheduleReload(); return ok; }
bool AssetsModel::setAssetsRating(const QVariantList& assetIds, int rating){ QList<int> ids; for (const auto &v: assetIds) ids << v.toInt(); bool ok = DB::instance().setAssetsRating(ids, rating); scheduleReload(); return ok; }
bool AssetsModel::assignTags(const QVariantList& assetIds, const QVariantList& tagIds){
    QList<int> aids; for (const auto &v: assetIds) aids << v.toInt();
    QList<int> tids; for (const auto &t: tagIds) tids << t.toInt();
    bool ok = DB::instance().assignTagsToAssets(aids, tids);
    if (ok) {
        // Notify QML delegates to refresh tag text for affected assets
        for (int aid : aids) {
            emit tagsChangedForAsset(aid);
        }
    }
    return ok;
}

void AssetsModel::rebuildFilter() {
    m_filteredRowIndexes.clear();
    m_filteredRowIndexes.reserve(m_rows.size());

    for (int i = 0; i < m_rows.size(); ++i) {
        if (matchesFilter(m_rows[i])) {
            m_filteredRowIndexes.append(i);
        }
    }
}

bool AssetsModel::matchesFilter(const AssetRow& row) const {
    // Apply type filter
    if (m_typeFilter == Images) {
        if (!ThumbnailGenerator::instance().isImageFile(row.filePath)) return false;
    } else if (m_typeFilter == Videos) {
        if (!ThumbnailGenerator::instance().isVideoFile(row.filePath)) return false;
    }

    // Apply tag filter
    if (!m_selectedTagNames.isEmpty()) {
        QStringList assetTags = DB::instance().tagsForAsset(row.id);
        bool hasAnyTag = false;
        bool hasAllTags = true;

        for (const QString& selectedTag : m_selectedTagNames) {
            bool assetHasTag = assetTags.contains(selectedTag);
            if (assetHasTag) {
                hasAnyTag = true;
            } else {
                hasAllTags = false;
            }
        }

        if (m_tagFilterMode == And) {
            if (!hasAllTags) return false;
        } else { // Or mode
            if (!hasAnyTag) return false;
        }
    }

    const QString needle = m_searchQuery.trimmed();
    if (needle.isEmpty())
        return true;
    const QString needleLower = needle.toLower();
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

QStringList AssetsModel::tagsForAsset(int assetId) const {
    return DB::instance().tagsForAsset(assetId);
}

void AssetsModel::onAssetsChangedForFolder(int folderId) {
    if (folderId != m_folderId)
        return;
    // Coalesce rapid-fire updates
    scheduleReload();
}

void AssetsModel::triggerDebouncedReload() {
    m_reloadScheduled = false;
    reload();
}

void AssetsModel::scheduleReload() {
    if (!m_reloadTimer.isActive()) {
        m_reloadTimer.start();
    }
    m_reloadScheduled = true;
}
