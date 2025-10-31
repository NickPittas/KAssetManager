#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QDateTime>
#include <QtQml/qqml.h>
#include <QTimer>
#include <QHash>

struct AssetRow {
    int id = 0;
    QString fileName;
    QString filePath;
    qint64 fileSize = 0;
    int folderId = 0;
    QString fileType;
    QDateTime lastModified;
    int rating = -1;
    QString thumbnailPath;  // Path to generated thumbnail
    bool isSequence = false;
    QString sequencePattern;
    int sequenceStartFrame = 0;
    int sequenceEndFrame = 0;
    int sequenceFrameCount = 0;
};

class AssetsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int folderId READ folderId WRITE setFolderId NOTIFY folderIdChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(int typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)
    Q_PROPERTY(QStringList selectedTagNames READ selectedTagNames WRITE setSelectedTagNames NOTIFY selectedTagNamesChanged)
    Q_PROPERTY(int tagFilterMode READ tagFilterMode WRITE setTagFilterMode NOTIFY tagFilterModeChanged)
    Q_PROPERTY(bool recursiveMode READ recursiveMode WRITE setRecursiveMode NOTIFY recursiveModeChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        FileNameRole,
        FilePathRole,
        FileSizeRole,
        ThumbnailPathRole,
        FileTypeRole,
        LastModifiedRole,
        RatingRole,
        IsSequenceRole,
        SequencePatternRole,
        SequenceStartFrameRole,
        SequenceEndFrameRole,
        SequenceFrameCountRole
    };
    explicit AssetsModel(QObject* parent=nullptr);

    int rowCount(const QModelIndex& parent) const override { Q_UNUSED(parent); return m_filteredRowIndexes.size(); }
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int,QByteArray> roleNames() const override;

    // Drag-and-drop support
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override;

    int folderId() const { return m_folderId; }
    void setFolderId(int id);

    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString& query);

    enum TypeFilter { All = 0, Images = 1, Videos = 2 };
    int typeFilter() const { return m_typeFilter; }
    void setTypeFilter(int f);

    enum RatingFilter { AllRatings = 0, FiveStars = 1, FourPlusStars = 2, ThreePlusStars = 3, Unrated = 4 };
    int ratingFilter() const { return m_ratingFilter; }
    void setRatingFilter(int f);

    QStringList selectedTagNames() const { return m_selectedTagNames; }
    void setSelectedTagNames(const QStringList& tags);

    enum TagFilterMode { And = 0, Or = 1 };
    int tagFilterMode() const { return m_tagFilterMode; }
    void setTagFilterMode(int mode);

    bool recursiveMode() const { return m_recursiveMode; }
    void setRecursiveMode(bool recursive);

    Q_INVOKABLE bool moveAssetToFolder(int assetId, int folderId);
    Q_INVOKABLE bool moveAssetsToFolder(const QVariantList& assetIds, int folderId);
    Q_INVOKABLE bool removeAssets(const QVariantList& assetIds);
    Q_INVOKABLE bool setAssetsRating(const QVariantList& assetIds, int rating);
    Q_INVOKABLE bool assignTags(const QVariantList& assetIds, const QVariantList& tagIds);
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QStringList tagsForAsset(int assetId) const;

public slots:
    void reload();

signals:
    void folderIdChanged();
    void searchQueryChanged();
    void typeFilterChanged();
    void selectedTagNamesChanged();
    void tagFilterModeChanged();
    void recursiveModeChanged();
    void tagsChangedForAsset(int assetId);

private slots:
    void onThumbnailGenerated(const QString& filePath, const QString& thumbnailPath);
    void onAssetsChangedForFolder(int folderId);
    void triggerDebouncedReload();

private:
    void query();
    void rebuildFilter();
    bool matchesFilter(const AssetRow& row) const;
    void scheduleReload();

    int m_folderId = 0;
    QVector<AssetRow> m_rows;
    QString m_searchQuery;
    int m_typeFilter = All;
    int m_ratingFilter = AllRatings;
    QStringList m_selectedTagNames;
    int m_tagFilterMode = And;
    bool m_recursiveMode = false;
    QVector<int> m_filteredRowIndexes;

    // Guard to avoid emitting dataChanged while the model is resetting
    bool m_isResetting = false;

    QTimer m_reloadTimer;
    bool m_reloadScheduled = false;
    QHash<int, QStringList> m_tagCache;
};
