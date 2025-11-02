#include "virtual_folders.h"
#include "db.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QMimeData>
#include <QIcon>
#include "log_manager.h"

VirtualFolderTreeModel::VirtualFolderTreeModel(QObject* parent): QAbstractItemModel(parent){
    connect(&DB::instance(), &DB::foldersChanged, this, &VirtualFolderTreeModel::reload);
    reload();
}

void VirtualFolderTreeModel::reload(){ qDebug() << "VirtualFolderTreeModel::reload()"; beginResetModel(); build(); endResetModel(); LogManager::instance().addLog("Folders reload complete", "DEBUG"); }

void VirtualFolderTreeModel::build(){
    m_nodes.clear(); m_idToIdx.clear();
    QSqlQuery q(DB::instance().database());
    if (!q.exec("SELECT id,name,COALESCE(parent_id,0) FROM virtual_folders ORDER BY parent_id,name")) {
        qWarning() << q.lastError(); return; }
    // First pass: create nodes
    QVector<VFNode> tmp;
    while (q.next()) {
        VFNode n;
        n.id=q.value(0).toInt();
        n.name=q.value(1).toString();
        n.parentId=q.value(2).toInt();
        tmp.push_back(n);
    }

    // Mark project folders
    QSqlQuery pq(DB::instance().database());
    if (pq.exec("SELECT id, virtual_folder_id FROM project_folders")) {
        while (pq.next()) {
            int projectFolderId = pq.value(0).toInt();
            int virtualFolderId = pq.value(1).toInt();
            for (auto& n : tmp) {
                if (n.id == virtualFolderId) {
                    n.isProjectFolder = true;
                    n.projectFolderId = projectFolderId;
                    break;
                }
            }
        }
    }

    // Find root id (name='Root', parentId=0)
    for (const auto& n: tmp) if (n.parentId==0 && n.name=="Root") { m_rootId=n.id; break; }
    m_nodes = tmp; m_idToIdx.reserve(m_nodes.size());
    for (int i=0;i<m_nodes.size();++i) m_idToIdx.insert(m_nodes[i].id,i);
    // Build children lists
    for (int i=0;i<m_nodes.size();++i) if (m_nodes[i].parentId!=0) {
        int pidx = m_idToIdx.value(m_nodes[i].parentId,-1); if (pidx>=0) m_nodes[pidx].children.push_back(m_nodes[i].id);
    }
}

QModelIndex VirtualFolderTreeModel::index(int row, int column, const QModelIndex& parent) const{
    if (column!=0 || row<0) return QModelIndex();

    // If no parent, we're at the top level - show Root itself
    if (!parent.isValid()) {
        if (row != 0) return QModelIndex(); // Only one top-level item: Root
        const VFNode* root = nodeForId(m_rootId);
        return root ? createIndex(0, 0, const_cast<VFNode*>(root)) : QModelIndex();
    }

    // Otherwise, show children of the parent node
    const VFNode* p = static_cast<const VFNode*>(parent.internalPointer());
    if (!p || row >= p->children.size()) return QModelIndex();
    const VFNode* c = nodeForId(p->children[row]);
    return c ? createIndex(row, 0, const_cast<VFNode*>(c)) : QModelIndex();
}

QModelIndex VirtualFolderTreeModel::parent(const QModelIndex& child) const{
    if (!child.isValid()) return QModelIndex();
    const VFNode* n = static_cast<const VFNode*>(child.internalPointer());
    if (!n || n->id == m_rootId) return QModelIndex(); // Root has no parent

    const VFNode* p = nodeForId(n->parentId);
    if (!p) return QModelIndex();

    // If parent is Root, return invalid index (Root is top-level)
    if (p->id == m_rootId) {
        return createIndex(0, 0, const_cast<VFNode*>(p));
    }

    // Otherwise find parent's row in grandparent
    const VFNode* gp = nodeForId(p->parentId);
    int row = gp ? rowInParent(p) : 0;
    return createIndex(row, 0, const_cast<VFNode*>(p));
}

int VirtualFolderTreeModel::rowCount(const QModelIndex& parent) const{
    if (parent.column() > 0) return 0;
    if (!m_rootId) return 0;

    // Top level: show Root as the only item
    if (!parent.isValid()) return 1;

    // Otherwise show children of the parent node
    const VFNode* n = static_cast<const VFNode*>(parent.internalPointer());
    return n ? n->children.size() : 0;
}

QVariant VirtualFolderTreeModel::data(const QModelIndex& idx, int role) const{
    if (!idx.isValid()) return {};
    const VFNode* n = static_cast<const VFNode*>(idx.internalPointer());
    switch(role){
        case Qt::DisplayRole: return n->name;
        case Qt::DecorationRole: {
            // Return icon for project folders
            if (n->isProjectFolder) {
                return QIcon(":/icons/project_folder.png"); // We'll need to add this icon
            }
            return QVariant();
        }
        case IdRole: return n->id;
        case NameRole: return n->name;
        case DepthRole: {
            // Root is at depth 0, its children at depth 1, etc.
            int d = 0;
            const VFNode* p = n;
            while(p && p->id != m_rootId) {
                p = nodeForId(p->parentId);
                ++d;
            }
            return d;
        }
        case HasChildrenRole: return !n->children.isEmpty();
        case IsProjectFolderRole: return n->isProjectFolder;
        case ProjectFolderIdRole: return n->projectFolderId;
    }
    return {};
}

QHash<int,QByteArray> VirtualFolderTreeModel::roleNames() const{
    QHash<int,QByteArray> r;
    r[IdRole]="id";
    r[NameRole]="name";
    r[DepthRole]="depth";
    r[HasChildrenRole]="hasChildren";
    r[IsProjectFolderRole]="isProjectFolder";
    r[ProjectFolderIdRole]="projectFolderId";
    return r;
}

int VirtualFolderTreeModel::createFolder(int parentId, const QString& name){ int id=DB::instance().createFolder(name,parentId); return id; }
bool VirtualFolderTreeModel::renameFolder(int id, const QString& name){ return DB::instance().renameFolder(id,name); }
bool VirtualFolderTreeModel::deleteFolder(int id){ return DB::instance().deleteFolder(id); }
bool VirtualFolderTreeModel::moveFolder(int id, int newParentId){ return DB::instance().moveFolder(id,newParentId); }

int VirtualFolderTreeModel::rowInParent(const VFNode* n) const{
    const VFNode* p = nodeForId(n->parentId==0? m_rootId: n->parentId); if (!p) return 0;
    for (int i=0;i<p->children.size();++i) if (p->children[i]==n->id) return i; return 0;
}

const VFNode* VirtualFolderTreeModel::nodeForId(int id) const { int idx = m_idToIdx.value(id,-1); return idx>=0? &m_nodes[idx] : nullptr; }
VFNode* VirtualFolderTreeModel::nodeForId(int id) { int idx = m_idToIdx.value(id,-1); return idx>=0? &m_nodes[idx] : nullptr; }

int VirtualFolderTreeModel::nodeIdAt(int row, int parentId) const{
    const VFNode* p = nodeForId(parentId==0? m_rootId: parentId); if (!p) return 0;
    if (row<0 || row>=p->children.size()) return 0; return p->children[row];
}

QString VirtualFolderTreeModel::nodeName(int id) const{ const VFNode* n=nodeForId(id); return n? n->name:QString(); }

// Drag-and-drop support
Qt::ItemFlags VirtualFolderTreeModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags defaultFlags = QAbstractItemModel::flags(index);
    if (index.isValid()) {
        return defaultFlags | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
    }
    return defaultFlags | Qt::ItemIsDropEnabled;
}

QMimeData *VirtualFolderTreeModel::mimeData(const QModelIndexList &indexes) const
{
    QMimeData *mimeData = new QMimeData();
    QByteArray encodedData;
    QDataStream stream(&encodedData, QIODevice::WriteOnly);

    // Encode folder IDs
    QList<int> folderIds;
    for (const QModelIndex &index : indexes) {
        if (index.isValid()) {
            const VFNode* node = static_cast<const VFNode*>(index.internalPointer());
            if (node) {
                folderIds.append(node->id);
            }
        }
    }

    stream << folderIds;
    mimeData->setData("application/x-kasset-folder-ids", encodedData);

    qDebug() << "VirtualFolderTreeModel::mimeData() - Dragging" << folderIds.size() << "folders:" << folderIds;

    return mimeData;
}

Qt::DropActions VirtualFolderTreeModel::supportedDragActions() const
{
    return Qt::MoveAction;
}

Qt::DropActions VirtualFolderTreeModel::supportedDropActions() const
{
    return Qt::MoveAction | Qt::CopyAction;
}

bool VirtualFolderTreeModel::isProjectFolder(int id) const
{
    const VFNode* n = nodeForId(id);
    return n ? n->isProjectFolder : false;
}

int VirtualFolderTreeModel::getProjectFolderId(int virtualFolderId) const
{
    const VFNode* n = nodeForId(virtualFolderId);
    return n ? n->projectFolderId : 0;
}

QModelIndex VirtualFolderTreeModel::findIndexById(int folderId) const
{
    if (folderId <= 0) return QModelIndex();

    const VFNode* node = nodeForId(folderId);
    if (!node) return QModelIndex();

    // Build path from root to this node
    QVector<int> path;
    const VFNode* current = node;
    while (current && current->id != m_rootId) {
        path.prepend(current->id);
        current = nodeForId(current->parentId);
    }

    // Navigate from root to build QModelIndex
    QModelIndex idx;
    for (int nodeId : path) {
        const VFNode* parent = idx.isValid() ? nodeForId(idx.data(IdRole).toInt()) : nodeForId(m_rootId);
        if (!parent) return QModelIndex();

        int row = parent->children.indexOf(nodeId);
        if (row < 0) return QModelIndex();

        idx = index(row, 0, idx);
    }

    return idx;
}
