#pragma once
#include <QAbstractListModel>
#include <QVector>
#include <QString>
#include <QVariantMap>
#include <QDateTime>
#include <QtQml/qqml.h>

struct AssetRow {
    int id = 0;
    QString fileName;
    QString filePath;
    qint64 fileSize = 0;
    int folderId = 0;
    QString fileType;
    QDateTime lastModified;
};

class AssetsModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int folderId READ folderId WRITE setFolderId NOTIFY folderIdChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        FileNameRole,
        FilePathRole,
        FileSizeRole,
        ThumbnailPathRole,
        FileTypeRole,
        LastModifiedRole
    };
    explicit AssetsModel(QObject* parent=nullptr);

    int rowCount(const QModelIndex& parent) const override { Q_UNUSED(parent); return m_filteredRowIndexes.size(); }
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int,QByteArray> roleNames() const override;

    int folderId() const { return m_folderId; }
    void setFolderId(int id);

    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString& query);

    Q_INVOKABLE bool moveAssetToFolder(int assetId, int folderId);
    Q_INVOKABLE bool moveAssetsToFolder(const QVariantList& assetIds, int folderId);
    Q_INVOKABLE QVariantMap get(int row) const;

public slots:
    void reload();

signals:
    void folderIdChanged();
    void searchQueryChanged();

private slots:
    void onThumbnailGenerated(const QString& filePath, const QString& thumbnailPath);

private:
    void query();
    void rebuildFilter();
    bool matchesFilter(const AssetRow& row) const;
    int m_folderId = 0;
    QVector<AssetRow> m_rows;
    QString m_searchQuery;
    QVector<int> m_filteredRowIndexes;
};
