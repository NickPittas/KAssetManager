#include "project_assets_model.h"
#include "project_db.h"
#include "project_version_detector.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QMimeData>
#include <QUrl>
#include <QDebug>
#include <algorithm>

ProjectAssetsModel::ProjectAssetsModel(QObject* parent)
    : QAbstractListModel(parent)
{
    // Debounced reload (100ms)
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(100);
    connect(&m_reloadTimer, &QTimer::timeout, this, &ProjectAssetsModel::reload);

    // Connect to DB signals
    connect(&ProjectDB::instance(), &ProjectDB::projectAssetsChanged,
            this, [this](int projectId) {
                if (projectId == m_projectId || m_projectId == 0) {
                    scheduleReload();
                }
            });
}

int ProjectAssetsModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return m_filteredRowIndexes.size();
}

QVariant ProjectAssetsModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_filteredRowIndexes.size())
        return QVariant();

    int rowIndex = m_filteredRowIndexes.at(idx.row());
    if (rowIndex < 0 || rowIndex >= m_allRows.size())
        return QVariant();

    const ProjectAssetRow& r = m_allRows.at(rowIndex);

    switch (role) {
    case IdRole:
        return r.id;
    case FileNameRole:
    case Qt::DisplayRole:
        return r.fileName;
    case FilePathRole:
        return r.filePath;
    case FileSizeRole:
        return r.fileSize;
    case FileTypeRole:
        return r.fileType;
    case LastModifiedRole:
        return r.lastModified;
    case IsSequenceRole:
        return r.isSequence;
    case SequencePatternRole:
        return r.sequencePattern;
    case SequenceStartFrameRole:
        return r.sequenceStartFrame;
    case SequenceEndFrameRole:
        return r.sequenceEndFrame;
    case SequenceFrameCountRole:
        return r.sequenceFrameCount;
    case SequenceHasGapsRole:
        return r.sequenceHasGaps;
    case SequenceGapCountRole:
        return r.sequenceGapCount;
    case SequenceVersionRole:
        return r.sequenceVersion;
    case VersionGroupKeyRole:
        return r.versionGroupKey;
    case VersionStringRole:
        return r.versionString;
    case IsPrimaryVersionRole:
        return r.isPrimaryVersion;
    case VersionListRole:
        return r.versionList;
    case VersionAssetIdsRole:
        return QVariant::fromValue(r.versionAssetIds);
    case HasMultipleVersionsRole:
        return r.versionList.size() > 1;
    case IsFolderRole:
        return r.isFolder;
    case SubFolderIdRole:
        return r.subFolderId;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ProjectAssetsModel::roleNames() const {
    return {
        {IdRole, "id"},
        {FileNameRole, "fileName"},
        {FilePathRole, "filePath"},
        {FileSizeRole, "fileSize"},
        {FileTypeRole, "fileType"},
        {LastModifiedRole, "lastModified"},
        {IsSequenceRole, "isSequence"},
        {SequencePatternRole, "sequencePattern"},
        {SequenceStartFrameRole, "sequenceStartFrame"},
        {SequenceEndFrameRole, "sequenceEndFrame"},
        {SequenceFrameCountRole, "sequenceFrameCount"},
        {SequenceHasGapsRole, "sequenceHasGaps"},
        {SequenceGapCountRole, "sequenceGapCount"},
        {SequenceVersionRole, "sequenceVersion"},
        {VersionGroupKeyRole, "versionGroupKey"},
        {VersionStringRole, "versionString"},
        {IsPrimaryVersionRole, "isPrimaryVersion"},
        {VersionListRole, "versionList"},
        {VersionAssetIdsRole, "versionAssetIds"},
        {HasMultipleVersionsRole, "hasMultipleVersions"},
        {IsFolderRole, "isFolder"},
        {SubFolderIdRole, "subFolderId"}
    };
}

Qt::ItemFlags ProjectAssetsModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags defaultFlags = QAbstractListModel::flags(index);
    if (index.isValid()) {
        // Only allow dragging for non-folder items
        bool isFolder = data(index, IsFolderRole).toBool();
        if (!isFolder) {
            return defaultFlags | Qt::ItemIsDragEnabled;
        }
    }
    return defaultFlags;
}

QMimeData* ProjectAssetsModel::mimeData(const QModelIndexList& indexes) const {
    QMimeData* mimeData = new QMimeData();
    QList<QUrl> urls;

    for (const QModelIndex& idx : indexes) {
        if (idx.isValid()) {
            // Skip folder entries
            bool isFolder = data(idx, IsFolderRole).toBool();
            if (isFolder) continue;
            
            QString path = data(idx, FilePathRole).toString();
            if (!path.isEmpty()) {
                urls.append(QUrl::fromLocalFile(path));
            }
        }
    }

    mimeData->setUrls(urls);
    return mimeData;
}

Qt::DropActions ProjectAssetsModel::supportedDragActions() const {
    return Qt::CopyAction;
}

void ProjectAssetsModel::setProjectId(int id) {
    if (m_projectId == id) return;
    m_projectId = id;
    m_folderId = -1;  // Reset folder filter when project changes
    emit projectIdChanged();
    reload();
}

void ProjectAssetsModel::setFolderId(int id) {
    if (m_folderId == id) return;
    m_folderId = id;
    emit folderIdChanged();
    applyFilters();
}

void ProjectAssetsModel::setSearchQuery(const QString& query) {
    if (m_searchQuery == query) return;
    m_searchQuery = query;
    emit searchQueryChanged();
    applyFilters();
}

void ProjectAssetsModel::setTypeFilter(int f) {
    if (m_typeFilter == f) return;
    m_typeFilter = f;
    emit typeFilterChanged();
    applyFilters();
}

void ProjectAssetsModel::setShowAllVersions(bool show) {
    if (m_showAllVersions == show) return;
    m_showAllVersions = show;
    emit showAllVersionsChanged();
    applyFilters();
}

QVariantMap ProjectAssetsModel::get(int row) const {
    QVariantMap m;
    if (row < 0 || row >= m_filteredRowIndexes.size()) return m;

    int rowIndex = m_filteredRowIndexes.at(row);
    if (rowIndex < 0 || rowIndex >= m_allRows.size()) return m;

    const ProjectAssetRow& r = m_allRows.at(rowIndex);
    m["id"] = r.id;
    m["fileName"] = r.fileName;
    m["filePath"] = r.filePath;
    m["fileSize"] = r.fileSize;
    m["fileType"] = r.fileType;
    m["lastModified"] = r.lastModified;
    m["isSequence"] = r.isSequence;
    m["sequencePattern"] = r.sequencePattern;
    m["versionGroupKey"] = r.versionGroupKey;
    m["versionString"] = r.versionString;
    m["isPrimaryVersion"] = r.isPrimaryVersion;
    m["versionList"] = r.versionList;
    m["hasMultipleVersions"] = r.versionList.size() > 1;
    return m;
}

int ProjectAssetsModel::getAssetIdForVersion(int primaryAssetId, const QString& versionString) const {
    // Find the row for the primary asset
    auto it = m_assetIdToRow.find(primaryAssetId);
    if (it == m_assetIdToRow.end()) return primaryAssetId;

    int rowIndex = it.value();
    if (rowIndex < 0 || rowIndex >= m_allRows.size()) return primaryAssetId;

    const ProjectAssetRow& r = m_allRows.at(rowIndex);

    // Find the version in the list
    int versionIdx = r.versionList.indexOf(versionString);
    if (versionIdx >= 0 && versionIdx < r.versionAssetIds.size()) {
        return r.versionAssetIds.at(versionIdx);
    }

    return primaryAssetId;
}

bool ProjectAssetsModel::removeAssets(const QVariantList& assetIds) {
    QList<int> ids;
    for (const QVariant& v : assetIds) {
        ids.append(v.toInt());
    }
    bool ok = ProjectDB::instance().removeAssets(ids);
    if (ok) scheduleReload();
    return ok;
}

void ProjectAssetsModel::scheduleReload() {
    m_reloadTimer.start();
}

void ProjectAssetsModel::reload() {
    beginResetModel();

    m_allRows.clear();
    m_filteredRowIndexes.clear();
    m_versionGroups.clear();
    m_assetIdToRow.clear();

    qDebug() << "[ProjectAssetsModel] reload - projectId:" << m_projectId << "folderId:" << m_folderId;

    if (m_projectId <= 0) {
        qDebug() << "[ProjectAssetsModel] reload - no project set, returning empty";
        endResetModel();
        return;
    }

    QSqlDatabase db = ProjectDB::instance().database();

    // First, load all subfolders for this project (we'll filter in applyFilters)
    QSqlQuery fq(db);
    fq.prepare("SELECT id, name, parent_id FROM virtual_folders WHERE project_id = ? ORDER BY name");
    fq.addBindValue(m_projectId);
    
    if (fq.exec()) {
        while (fq.next()) {
            ProjectAssetRow r;
            r.subFolderId = fq.value(0).toInt();
            r.fileName = fq.value(1).toString();
            r.folderId = fq.value(2).isNull() ? 0 : fq.value(2).toInt();  // parent_id becomes folderId
            r.isFolder = true;
            r.id = -r.subFolderId;  // Use negative ID for folders to distinguish from assets
            r.fileType = "folder";
            m_allRows.append(r);
        }
        qDebug() << "[ProjectAssetsModel] reload - loaded" << m_allRows.size() << "folders";
    }
    
    int folderCount = m_allRows.size();

    // Query assets for this project
    QSqlQuery q(db);
    q.prepare(
        "SELECT a.id, a.file_path, a.file_name, a.file_size, a.file_type, a.last_modified, "
        "a.is_sequence, a.sequence_pattern, a.sequence_start_frame, a.sequence_end_frame, "
        "a.sequence_frame_count, a.sequence_has_gaps, a.sequence_gap_count, a.sequence_version, "
        "a.version_group_key, a.version_string, a.virtual_folder_id "
        "FROM assets a "
        "JOIN virtual_folders f ON a.virtual_folder_id = f.id "
        "WHERE f.project_id = ? "
        "ORDER BY a.file_name"
    );
    q.addBindValue(m_projectId);

    if (!q.exec()) {
        qWarning() << "[ProjectAssetsModel] reload query failed:" << q.lastError();
        endResetModel();
        return;
    }

    while (q.next()) {
        ProjectAssetRow r;
        r.id = q.value(0).toInt();
        r.filePath = q.value(1).toString();
        r.fileName = q.value(2).toString();
        r.fileSize = q.value(3).toLongLong();
        r.fileType = q.value(4).toString();
        r.lastModified = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
        r.isSequence = q.value(6).toBool();
        r.sequencePattern = q.value(7).toString();
        r.sequenceStartFrame = q.value(8).toInt();
        r.sequenceEndFrame = q.value(9).toInt();
        r.sequenceFrameCount = q.value(10).toInt();
        r.sequenceHasGaps = q.value(11).toBool();
        r.sequenceGapCount = q.value(12).toInt();
        r.sequenceVersion = q.value(13).toString();
        r.versionGroupKey = q.value(14).toString();
        r.versionString = q.value(15).toString();
        r.folderId = q.value(16).toInt();
        r.isFolder = false;

        int rowIdx = m_allRows.size();
        m_assetIdToRow.insert(r.id, rowIdx);
        m_allRows.append(r);
    }

    qDebug() << "[ProjectAssetsModel] reload - loaded" << folderCount << "folders and" << (m_allRows.size() - folderCount) << "assets";

    // Debug: show distribution of folder IDs
    QMap<int, int> folderIdCounts;
    for (const auto& row : m_allRows) {
        if (!row.isFolder) folderIdCounts[row.folderId]++;
    }
    qDebug() << "[ProjectAssetsModel] Asset folder ID distribution (top 10):";
    QList<QPair<int, int>> sorted;
    for (auto it = folderIdCounts.begin(); it != folderIdCounts.end(); ++it) {
        sorted.append({it.key(), it.value()});
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (int i = 0; i < std::min(10, (int)sorted.size()); ++i) {
        qDebug() << "  folderId:" << sorted[i].first << "count:" << sorted[i].second;
    }

    // Detect and apply version grouping
    detectVersionGroups();
    applyFilters();

    endResetModel();
}

void ProjectAssetsModel::detectVersionGroups() {
    m_versionGroups.clear();

    // Collect all project file paths for version detection
    QStringList projectFilePaths;
    QMap<QString, int> pathToRowIndex;

    for (int i = 0; i < m_allRows.size(); ++i) {
        const QString& path = m_allRows[i].filePath;
        if (ProjectVersionDetector::isProjectFile(path)) {
            projectFilePaths.append(path);
            pathToRowIndex.insert(path, i);
        }
    }

    // Use version detector to group files
    QMap<QString, QVector<QPair<int, QString>>> groups = 
        ProjectVersionDetector::groupByVersion(projectFilePaths);

    // Apply grouping to rows
    for (auto it = groups.constBegin(); it != groups.constEnd(); ++it) {
        const QString& groupKey = it.key();
        const QVector<QPair<int, QString>>& versions = it.value();

        if (versions.isEmpty()) continue;

        QVector<int> rowIndexes;
        QStringList versionStrings;
        QList<int> versionAssetIds;

        // Versions are already sorted descending by version number
        for (const auto& v : versions) {
            QString path = v.second;
            if (pathToRowIndex.contains(path)) {
                int rowIdx = pathToRowIndex[path];
                rowIndexes.append(rowIdx);

                VersionInfo info = ProjectVersionDetector::parseVersion(path);
                versionStrings.append(info.versionString.isEmpty() ? "base" : info.versionString);
                versionAssetIds.append(m_allRows[rowIdx].id);
            }
        }

        if (rowIndexes.isEmpty()) continue;

        m_versionGroups.insert(groupKey, rowIndexes);

        // Update all rows in this group with version info
        bool isFirst = true;
        for (int rowIdx : rowIndexes) {
            ProjectAssetRow& r = m_allRows[rowIdx];
            r.versionGroupKey = groupKey;
            r.isPrimaryVersion = isFirst;
            r.versionList = versionStrings;
            r.versionAssetIds = versionAssetIds;

            // Parse version string for this specific row
            VersionInfo info = ProjectVersionDetector::parseVersion(r.filePath);
            r.versionString = info.versionString;

            isFirst = false;
        }
    }
}

void ProjectAssetsModel::applyFilters() {
    beginResetModel();
    m_filteredRowIndexes.clear();

    static const QSet<QString> imageTypes = {"jpg", "jpeg", "png", "gif", "bmp", "tiff", "tif", 
                                              "webp", "exr", "hdr", "psd", "tga"};
    static const QSet<QString> videoTypes = {"mp4", "mov", "avi", "mkv", "wmv", "flv", "webm", 
                                              "m4v", "mpg", "mpeg", "mxf"};
    static const QSet<QString> projectTypes = {"aep", "aepx", "nk"};

    QString searchLower = m_searchQuery.toLower();
    
    int skippedFolder = 0, skippedVersion = 0, skippedType = 0, skippedSearch = 0;

    // First pass: add folders at the top (if showing a specific folder)
    for (int i = 0; i < m_allRows.size(); ++i) {
        const ProjectAssetRow& r = m_allRows.at(i);
        if (!r.isFolder) continue;
        
        // For folders, folderId is their parent_id
        // Only show immediate child folders of the current folder
        if (m_folderId > 0) {
            // Show subfolders whose parent is the current folder
            if (r.folderId != m_folderId) continue;
        } else {
            // m_folderId == -1 means show all - don't show folder entries in "all" view
            // Or optionally show only root folders (parent_id = root folder)
            continue;  // Skip folders in "all assets" view
        }
        
        // Search filter for folders
        if (!searchLower.isEmpty()) {
            if (!r.fileName.toLower().contains(searchLower)) {
                skippedSearch++;
                continue;
            }
        }
        
        m_filteredRowIndexes.append(i);
    }
    
    int folderCount = m_filteredRowIndexes.size();

    // Second pass: add assets
    for (int i = 0; i < m_allRows.size(); ++i) {
        const ProjectAssetRow& r = m_allRows.at(i);
        if (r.isFolder) continue;  // Already handled above

        // Folder filter: only show assets in the selected folder (or all if -1)
        if (m_folderId > 0 && r.folderId != m_folderId) {
            skippedFolder++;
            continue;
        }

        // Version filtering: only show primary versions unless showAllVersions is true
        if (!m_showAllVersions && !r.isPrimaryVersion && !r.versionGroupKey.isEmpty()) {
            // Check if this row is part of a version group and is not primary
            if (m_versionGroups.contains(r.versionGroupKey) && 
                m_versionGroups[r.versionGroupKey].size() > 1) {
                skippedVersion++;
                continue;  // Skip non-primary versions
            }
        }

        // Type filter (don't apply to folders)
        QString ext = r.fileType.toLower();
        if (m_typeFilter == Images && !imageTypes.contains(ext) && !r.isSequence) { skippedType++; continue; }
        if (m_typeFilter == Videos && !videoTypes.contains(ext)) { skippedType++; continue; }
        if (m_typeFilter == ProjectFiles && !projectTypes.contains(ext)) { skippedType++; continue; }

        // Search filter
        if (!searchLower.isEmpty()) {
            bool match = r.fileName.toLower().contains(searchLower) ||
                         r.filePath.toLower().contains(searchLower);
            if (!match) { skippedSearch++; continue; }
        }

        m_filteredRowIndexes.append(i);
    }

    qDebug() << "[ProjectAssetsModel] applyFilters - folderId:" << m_folderId
             << "typeFilter:" << m_typeFilter
             << "total:" << m_allRows.size()
             << "filtered:" << m_filteredRowIndexes.size()
             << "(folders:" << folderCount << ")"
             << "skipped(folder:" << skippedFolder << "version:" << skippedVersion 
             << "type:" << skippedType << "search:" << skippedSearch << ")";

    endResetModel();
}
