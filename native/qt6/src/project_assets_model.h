#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QDateTime>
#include <QTimer>
#include <QHash>
#include <QMap>

/**
 * @brief Asset row data for Project Manager.
 */
struct ProjectAssetRow {
    int id = 0;
    QString fileName;
    QString filePath;
    qint64 fileSize = 0;
    int folderId = 0;
    QString fileType;
    QDateTime lastModified;
    bool isSequence = false;
    // Folder entry support
    bool isFolder = false;
    int subFolderId = 0;  // For folder entries, the folder's own ID
    QString sequencePattern;
    int sequenceStartFrame = 0;
    int sequenceEndFrame = 0;
    int sequenceFrameCount = 0;
    bool sequenceHasGaps = false;
    int sequenceGapCount = 0;
    QString sequenceVersion;
    // Version grouping
    QString versionGroupKey;
    QString versionString;
    bool isPrimaryVersion = false;      // True if this is the highest version in group
    QStringList versionList;            // All versions in this group (for dropdown)
    QList<int> versionAssetIds;         // Asset IDs corresponding to versionList
};

/**
 * @brief Model for displaying assets within a project, with version grouping support.
 * 
 * Files with detected versions (AE/Nuke projects) are grouped by base name.
 * Only the highest version is shown by default, with a dropdown to select other versions.
 */
class ProjectAssetsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int projectId READ projectId WRITE setProjectId NOTIFY projectIdChanged)
    Q_PROPERTY(int folderId READ folderId WRITE setFolderId NOTIFY folderIdChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(int typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)
    Q_PROPERTY(bool showAllVersions READ showAllVersions WRITE setShowAllVersions NOTIFY showAllVersionsChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        FileNameRole,
        FilePathRole,
        FileSizeRole,
        FileTypeRole,
        LastModifiedRole,
        IsSequenceRole,
        SequencePatternRole,
        SequenceStartFrameRole,
        SequenceEndFrameRole,
        SequenceFrameCountRole,
        SequenceHasGapsRole,
        SequenceGapCountRole,
        SequenceVersionRole,
        // Version grouping roles
        VersionGroupKeyRole,
        VersionStringRole,
        IsPrimaryVersionRole,
        VersionListRole,
        VersionAssetIdsRole,
        HasMultipleVersionsRole,
        // Folder navigation roles
        IsFolderRole,
        SubFolderIdRole
    };

    explicit ProjectAssetsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Drag-and-drop support
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    Qt::DropActions supportedDragActions() const override;

    int projectId() const { return m_projectId; }
    void setProjectId(int id);

    int folderId() const { return m_folderId; }
    void setFolderId(int id);

    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString& query);

    enum TypeFilter { All = 0, Images = 1, Videos = 2, ProjectFiles = 3 };
    int typeFilter() const { return m_typeFilter; }
    void setTypeFilter(int f);

    bool showAllVersions() const { return m_showAllVersions; }
    void setShowAllVersions(bool show);

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int getAssetIdForVersion(int primaryAssetId, const QString& versionString) const;
    Q_INVOKABLE bool removeAssets(const QVariantList& assetIds);

public slots:
    void reload();

signals:
    void projectIdChanged();
    void folderIdChanged();
    void searchQueryChanged();
    void typeFilterChanged();
    void showAllVersionsChanged();

private slots:
    void scheduleReload();

private:
    void applyFilters();
    void detectVersionGroups();

    int m_projectId = 0;
    int m_folderId = -1;  // -1 means show all folders
    QString m_searchQuery;
    int m_typeFilter = All;
    bool m_showAllVersions = false;

    QVector<ProjectAssetRow> m_allRows;
    QVector<int> m_filteredRowIndexes;  // Indexes into m_allRows after filtering

    // Version grouping maps
    QMap<QString, QVector<int>> m_versionGroups;  // groupKey -> list of row indexes
    QMap<int, int> m_assetIdToRow;                // assetId -> row index in m_allRows

    QTimer m_reloadTimer;
};
