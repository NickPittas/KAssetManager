#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QDateTime>
#include <QtQml/qqml.h>
#include <QTimer>

struct AssetRow {
    int id = 0;
    QString fileName;
    QString filePath;
    qint64 fileSize = 0;
    int folderId = 0;
    QString fileType;
    QDateTime lastModified;
    int rating = -1;
};

class AssetsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int folderId READ folderId WRITE setFolderId NOTIFY folderIdChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(int typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        FileNameRole,
        FilePathRole,
        FileSizeRole,
        ThumbnailPathRole,
        FileTypeRole,
        LastModifiedRole,
        RatingRole
    };
    explicit AssetsModel(QObject* parent=nullptr);

    int rowCount(const QModelIndex& parent) const override { Q_UNUSED(parent); return m_filteredRowIndexes.size(); }
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int,QByteArray> roleNames() const override;

    int folderId() const { return m_folderId; }
    void setFolderId(int id);

    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString& query);

    enum TypeFilter { All = 0, Images = 1, Videos = 2 };
    int typeFilter() const { return m_typeFilter; }
    void setTypeFilter(int f);

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
    QVector<int> m_filteredRowIndexes;

    QTimer m_reloadTimer;
    bool m_reloadScheduled = false;
};
