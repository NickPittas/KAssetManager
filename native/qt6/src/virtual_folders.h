#pragma once
#include <QAbstractItemModel>
#include <QVector>
#include <QHash>
#include <QString>
#include <QtQml/qqml.h>

struct VFNode {
    int id=0;
    QString name;
    int parentId=0;
    QVector<int> children;
    bool isProjectFolder=false;
    int projectFolderId=0;
};

class VirtualFolderTreeModel : public QAbstractItemModel {
    Q_OBJECT
    QML_ELEMENT
public:
    enum Roles { IdRole=Qt::UserRole+1, NameRole, DepthRole, HasChildrenRole, IsProjectFolderRole, ProjectFolderIdRole };
    explicit VirtualFolderTreeModel(QObject* parent=nullptr);

    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent) const override;
    int columnCount(const QModelIndex& parent) const override { Q_UNUSED(parent); return 1; }
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int,QByteArray> roleNames() const override;

    // Drag-and-drop support
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override;
    Qt::DropActions supportedDropActions() const override;

    Q_INVOKABLE int rootId() const { return m_rootId; }
    Q_INVOKABLE int createFolder(int parentId, const QString& name);
    Q_INVOKABLE bool renameFolder(int id, const QString& name);
    Q_INVOKABLE bool deleteFolder(int id);
    Q_INVOKABLE bool moveFolder(int id, int newParentId);
    Q_INVOKABLE int nodeIdAt(int row, int parentId) const;
    Q_INVOKABLE QString nodeName(int id) const;
    Q_INVOKABLE bool isProjectFolder(int id) const;
    Q_INVOKABLE int getProjectFolderId(int virtualFolderId) const;

public slots:
    void reload();

private:
    void build();
    const VFNode* nodeForId(int id) const; VFNode* nodeForId(int id);
    int rowInParent(const VFNode* n) const;

    int m_rootId = 0;
    QVector<VFNode> m_nodes; // index by position in m_nodes
    QHash<int,int> m_idToIdx; // id -> index in m_nodes
};

