#ifndef PROJECT_FOLDERS_MODEL_H
#define PROJECT_FOLDERS_MODEL_H

#include <QAbstractItemModel>
#include <QVector>
#include <QHash>

struct ProjectFolder {
    int id = 0;
    QString name;
    int parentId = 0;
    int projectId = 0;
    QVector<int> childIds;
};

class ProjectFoldersModel : public QAbstractItemModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ParentIdRole,
        ProjectIdRole
    };

    explicit ProjectFoldersModel(QObject *parent = nullptr);

    void setProjectId(int projectId);
    int projectId() const { return m_projectId; }

    void reload();

    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    int folderIdForIndex(const QModelIndex &index) const;
    QModelIndex indexForFolderId(int folderId) const;
    int parentFolderId(int folderId) const;

private:
    int m_projectId = -1;
    QVector<ProjectFolder> m_folders;
    QHash<int, int> m_idToIndex;  // folder id -> index in m_folders
    int m_rootFolderId = -1;

    void buildTree();
    QVector<int> childrenOf(int parentId) const;
    QModelIndex findIndexRecursive(const QModelIndex &parent, int folderId) const;
};

#endif // PROJECT_FOLDERS_MODEL_H
