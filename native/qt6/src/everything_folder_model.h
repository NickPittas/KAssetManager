#ifndef EVERYTHING_FOLDER_MODEL_H
#define EVERYTHING_FOLDER_MODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QMimeData>
#include <QVector>
#include <QStringList>
#include <QFutureWatcher>
#include <QSet>
#include <QMutex>

class EverythingFolderModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit EverythingFolderModel(QObject *parent = nullptr);
    ~EverythingFolderModel() override;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    bool hasChildren(const QModelIndex &parent = QModelIndex()) const override;

    QString pathForIndex(const QModelIndex &index) const;
    QModelIndex indexForPath(const QString &path);
    
    // Async path resolution - emits pathResolved when ready
    void resolvePathAsync(const QString &path);

    void refresh();

signals:
    // Emitted when async path resolution completes
    void pathResolved(const QString &path, const QModelIndex &index);
    // Emitted when a node's children have been fetched
    void childrenFetched(const QModelIndex &parent);

private:
    struct Node {
        QString name;
        QString path;
        bool fetched = false;
        bool fetching = false;  // True while async fetch is in progress
        Node *parent = nullptr;
        QVector<Node*> children;
    };
    
    // Result of async fetch operation
    struct FetchResult {
        QString nodePath;
        QVector<QPair<QString, QString>> children; // (name, path) pairs
    };
    
    // Result of async path resolution
    struct PathResolveResult {
        QString requestedPath;
        QStringList segments;
        QVector<FetchResult> fetchedSegments;
    };

    Node *m_root = nullptr;
    QHash<QString, Node*> m_nodesByPath;
    
    // Track pending async fetches
    QHash<QString, QFutureWatcher<FetchResult>*> m_pendingFetches;
    QSet<QString> m_fetchingPaths;
    mutable QMutex m_fetchMutex;
    
    // Track pending path resolutions
    QHash<QString, QFutureWatcher<PathResolveResult>*> m_pendingPathResolves;

    void clear(Node *node);
    void populateRoot();
    void fetchChildrenAsync(Node *node);
    static FetchResult fetchChildrenWorker(const QString &parentPath);
    void applyFetchResult(const FetchResult &result);
    Node *createNode(Node *parent, const QString &name, const QString &path);
    QString normalizePath(const QString &path) const;
    QStringList tokenizePath(const QString &path) const;
    QString escapeQueryString(const QString &path) const;
    int rowOfNode(const Node *node) const;
};

#endif // EVERYTHING_FOLDER_MODEL_H
