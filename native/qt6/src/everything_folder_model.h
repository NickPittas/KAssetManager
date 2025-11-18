#ifndef EVERYTHING_FOLDER_MODEL_H
#define EVERYTHING_FOLDER_MODEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QMimeData>
#include <QVector>
#include <QStringList>

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

    void refresh();

private:
    struct Node {
        QString name;
        QString path;
        bool fetched = false;
        Node *parent = nullptr;
        QVector<Node*> children;
    };

    Node *m_root = nullptr;
    QHash<QString, Node*> m_nodesByPath;

    void clear(Node *node);
    void populateRoot();
    void ensureFetched(Node *node) const;
    void fetchChildren(Node *node);
    Node *createNode(Node *parent, const QString &name, const QString &path);
    QString normalizePath(const QString &path) const;
    QStringList tokenizePath(const QString &path) const;
    QString escapeQueryString(const QString &path) const;
    int rowOfNode(const Node *node) const;
};

#endif // EVERYTHING_FOLDER_MODEL_H
