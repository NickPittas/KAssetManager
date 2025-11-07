#include "assets_model.h"
#include "db.h"
#include "progress_manager.h"
#include "log_manager.h"
#include <QSet>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDebug>
#include <QThread>
#include <QElapsedTimer>
#include <QMimeData>
#include <QFile>
#include <QTextStream>

namespace {

bool isImageExtension(const QString& suffix) {
    static const QSet<QString> extensions = {
        "png","jpg","jpeg","bmp","tga","tif","tiff","gif","webp",
        "ico","heic","heif","avif","psd","svg","dds"
    };
    return extensions.contains(suffix.toLower());
}

bool isVideoExtension(const QString& suffix) {
    static const QSet<QString> extensions = {
        "mp4","mov","m4v","mkv","avi","mpg","mpeg","mp2","mpg2",
        "wmv","flv","webm","mxf","r3d","ogv","mts","m2ts"
    };
    return extensions.contains(suffix.toLower());
}

bool looksLikeSequence(const QString& filePath) {
    QFileInfo info(filePath);
    const QString base = info.completeBaseName();
    static QRegularExpression seqPattern(R"(.*(\d{2,}|#+|%0\d+d).*)");
    return seqPattern.match(base).hasMatch();
}

} // namespace

AssetsModel::AssetsModel(QObject* parent): QAbstractListModel(parent){
    // Debounce DB-driven reloads to avoid re-entrancy and view churn during batch imports
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(100);
    connect(&m_reloadTimer, &QTimer::timeout, this, &AssetsModel::triggerDebouncedReload);

    connect(&DB::instance(), &DB::assetsChanged, this, &AssetsModel::onAssetsChangedForFolder);

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
        case FileTypeRole: return r.fileType;
        case LastModifiedRole: return r.lastModified;
        case RatingRole: return r.rating;
        case IsSequenceRole: return r.isSequence;
        case SequencePatternRole: return r.sequencePattern;
        case SequenceStartFrameRole: return r.sequenceStartFrame;
        case SequenceEndFrameRole: return r.sequenceEndFrame;
        case SequenceFrameCountRole: return r.sequenceFrameCount;
        case SequenceHasGapsRole: return r.sequenceHasGaps;
        case SequenceGapCountRole: return r.sequenceGapCount;
        case SequenceVersionRole: return r.sequenceVersion;
        case PreviewStateRole: {
            QVariantMap preview;
            preview["filePath"] = r.filePath;
            preview["fileType"] = r.fileType;
            preview["isVideo"] = isVideoExtension(r.fileType);
            preview["isSequence"] = r.isSequence;
            preview["sequencePattern"] = r.sequencePattern;
            preview["sequenceStart"] = r.sequenceStartFrame;
            preview["sequenceEnd"] = r.sequenceEndFrame;
            preview["sequenceCount"] = r.sequenceFrameCount;
            preview["looksLikeSequence"] = looksLikeSequence(r.filePath);
            return preview;
        }
    }
    return {};
}

QHash<int,QByteArray> AssetsModel::roleNames() const{
    QHash<int,QByteArray> r;
    r[IdRole]="assetId";
    r[FileNameRole]="fileName";
    r[FilePathRole]="filePath";
    r[FileSizeRole]="fileSize";
    r[PreviewStateRole]="previewState";
    r[FileTypeRole] = "fileType";
    r[LastModifiedRole] = "lastModified";
    r[RatingRole] = "rating";
    r[IsSequenceRole] = "isSequence";
    r[SequencePatternRole] = "sequencePattern";
    r[SequenceStartFrameRole] = "sequenceStartFrame";
    r[SequenceEndFrameRole] = "sequenceEndFrame";
    r[SequenceFrameCountRole] = "sequenceFrameCount";
    r[SequenceHasGapsRole] = "sequenceHasGaps";
    r[SequenceGapCountRole] = "sequenceGapCount";
    r[SequenceVersionRole] = "sequenceVersion";
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

    // Encode asset IDs for internal drag-drop
    QList<int> assetIds;
    QList<QUrl> urls;

    for (const QModelIndex &index : indexes) {
        if (index.isValid()) {
            int assetId = data(index, IdRole).toInt();
            assetIds.append(assetId);

            // Get file path and add as URL for external drag-drop
            QString filePath = data(index, FilePathRole).toString();
            if (!filePath.isEmpty()) {
                urls.append(QUrl::fromLocalFile(filePath));
            }
        }
    }

    stream << assetIds;
    mimeData->setData("application/x-kasset-asset-ids", encodedData);

    // Add file URLs for external apps (Windows Explorer, Desktop, etc.)
    if (!urls.isEmpty()) {
        mimeData->setUrls(urls);
    }

    return mimeData;
}

Qt::DropActions AssetsModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

void AssetsModel::setFolderId(int id){
    if (m_folderId==id) return;
    m_folderId=id;
    scheduleReload();
    emit folderIdChanged();
}

void AssetsModel::setSearchQuery(const QString& query) {
    QString normalized = query;
    if (normalized == m_searchQuery)
        return;
    m_searchQuery = normalized;
    // Reload; scope (folder vs global) is controlled by m_searchEntireDatabase
    reload();
    emit searchQueryChanged();
}

void AssetsModel::setTypeFilter(int f) {
    if (m_typeFilter == f) return;
    m_typeFilter = f;
    m_isResetting = true;
    beginResetModel();
    rebuildFilter();
    endResetModel();
    m_isResetting = false;
    emit typeFilterChanged();
}

void AssetsModel::setRatingFilter(int f) {
    if (m_ratingFilter == f) return;
    m_ratingFilter = f;
    m_isResetting = true;
    beginResetModel();
    rebuildFilter();
    endResetModel();
    m_isResetting = false;
}

void AssetsModel::setSelectedTagNames(const QStringList& tags) {
    if (m_selectedTagNames == tags) return;
    m_selectedTagNames = tags;
    // Changing tag selection may require loading assets across folders
    m_isResetting = true;
    beginResetModel();
    rebuildFilter();
    endResetModel();
    m_isResetting = false;
    emit selectedTagNamesChanged();
}

void AssetsModel::setTagFilterMode(int mode) {
    if (m_tagFilterMode == mode) return;
    m_tagFilterMode = mode;
    m_isResetting = true;
    beginResetModel();
    rebuildFilter();
    endResetModel();
    m_isResetting = false;
    emit tagFilterModeChanged();
}

void AssetsModel::setRecursiveMode(bool recursive) {
    if (m_recursiveMode == recursive) return;
    m_recursiveMode = recursive;
    scheduleReload();
    emit recursiveModeChanged();
}

void AssetsModel::setSearchEntireDatabase(bool enabled) {
    if (m_searchEntireDatabase == enabled) return;
    m_searchEntireDatabase = enabled;
    scheduleReload();
    emit searchEntireDatabaseChanged();
}

void AssetsModel::reload(){
    QElapsedTimer t; t.start();

    m_isResetting = true;
    beginResetModel();

    query();

    rebuildFilter();

    endResetModel();
    m_isResetting = false;

    LogManager::instance().addLog(QString("AssetsModel reload: %1 assets in %2 ms").arg(m_rows.size()).arg(t.elapsed()), "DEBUG");
}

void AssetsModel::query(){
    m_rows.clear();

    const bool globalScope = !m_selectedTagNames.isEmpty() || (m_searchEntireDatabase && !m_searchQuery.trimmed().isEmpty());

    QSqlQuery q(DB::instance().database());
    if (globalScope) {
        LogManager::instance().addLog("DB query (all assets) started", "DEBUG");
        q.prepare("SELECT id,file_name,file_path,file_size,COALESCE(rating,-1),virtual_folder_id,COALESCE(is_sequence,0),sequence_pattern,sequence_start_frame,sequence_end_frame,sequence_frame_count,COALESCE(sequence_has_gaps,0),COALESCE(sequence_gap_count,0),sequence_version FROM assets ORDER BY file_name");
    } else {
        if (m_folderId<=0) {
            m_filteredRowIndexes.clear();
            return;
        }

        // If recursive mode is enabled, get all asset IDs from this folder and subfolders
        if (m_recursiveMode) {
            QList<int> assetIds = DB::instance().getAssetIdsInFolder(m_folderId, true);
            if (assetIds.isEmpty()) {
                m_filteredRowIndexes.clear();
                return;
            }

            // Build IN clause for asset IDs
            QString placeholders = QString("?").repeated(assetIds.size());
            for (int i = 1; i < assetIds.size(); ++i) {
                placeholders.replace(i * 2 - 1, 1, ",?");
            }

            q.prepare(QString("SELECT id,file_name,file_path,file_size,COALESCE(rating,-1),virtual_folder_id,COALESCE(is_sequence,0),sequence_pattern,sequence_start_frame,sequence_end_frame,sequence_frame_count,COALESCE(sequence_has_gaps,0),COALESCE(sequence_gap_count,0),sequence_version FROM assets WHERE id IN (%1) ORDER BY file_name").arg(placeholders));
            LogManager::instance().addLog(QString("DB query (assets by folder %1, recursive) started - %2 assets").arg(m_folderId).arg(assetIds.size()), "DEBUG");
            for (int assetId : assetIds) {
                q.addBindValue(assetId);
            }
        } else {
            // Non-recursive: just get assets in this folder
            q.prepare("SELECT id,file_name,file_path,file_size,COALESCE(rating,-1),virtual_folder_id,COALESCE(is_sequence,0),sequence_pattern,sequence_start_frame,sequence_end_frame,sequence_frame_count,COALESCE(sequence_has_gaps,0),COALESCE(sequence_gap_count,0),sequence_version FROM assets WHERE virtual_folder_id=? ORDER BY file_name");
            LogManager::instance().addLog(QString("DB query (assets by folder %1) started").arg(m_folderId), "DEBUG");
            q.addBindValue(m_folderId);
        }
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
        r.isSequence = q.value(6).toBool();
        r.sequencePattern = q.value(7).toString();
        r.sequenceStartFrame = q.value(8).toInt();
        r.sequenceEndFrame = q.value(9).toInt();
        r.sequenceFrameCount = q.value(10).toInt();
        r.sequenceHasGaps = q.value(11).toBool();
        r.sequenceGapCount = q.value(12).toInt();
        r.sequenceVersion = q.value(13).toString();

        QFileInfo fi(r.filePath);
        r.fileType = fi.exists() ? fi.suffix().toLower() : QString();
        r.lastModified = fi.exists() ? fi.lastModified() : QDateTime();
        m_rows.push_back(r);
        ++rows;
    }
    LogManager::instance().addLog(QString("DB query complete: %1 rows").arg(rows), "DEBUG");
}

bool AssetsModel::moveAssetToFolder(int assetId, int folderId){ bool ok=DB::instance().setAssetFolder(assetId,folderId); if (ok) scheduleReload(); return ok; }

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
    if (m_typeFilter == Images) {
        if (!isImageExtension(row.fileType)) return false;
    } else if (m_typeFilter == Videos) {
        if (!isVideoExtension(row.fileType)) return false;
    }

    if (m_ratingFilter == FiveStars) {
        if (row.rating != 5) return false;
    } else if (m_ratingFilter == FourPlusStars) {
        if (row.rating < 4) return false;
    } else if (m_ratingFilter == ThreePlusStars) {
        if (row.rating < 3) return false;
    } else if (m_ratingFilter == Unrated) {
        if (row.rating > 0) return false;
    }

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
        } else {
            if (!hasAnyTag) return false;
        }
    }

    const QString needle = m_searchQuery.trimmed();
    if (needle.isEmpty()) return true;
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    if (row.fileName.contains(needle, cs)) return true;
    if (row.filePath.contains(needle, cs)) return true;
    if (!row.fileType.isEmpty() && row.fileType.contains(needle, cs)) return true;
    if (row.lastModified.isValid() && row.lastModified.toString("yyyy-MM-dd hh:mm").toLower().contains(needle.toLower())) return true;
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
    map.insert("fileType", r.fileType);
    map.insert("lastModified", r.lastModified);
    QVariantMap preview;
    preview["filePath"] = r.filePath;
    preview["fileType"] = r.fileType;
    preview["isVideo"] = isVideoExtension(r.fileType);
    preview["isSequence"] = r.isSequence;
    preview["sequencePattern"] = r.sequencePattern;
    preview["sequenceStart"] = r.sequenceStartFrame;
    preview["sequenceEnd"] = r.sequenceEndFrame;
    preview["sequenceCount"] = r.sequenceFrameCount;
    preview["looksLikeSequence"] = looksLikeSequence(r.filePath);
    map.insert("previewState", preview);
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
