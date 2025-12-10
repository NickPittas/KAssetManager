#include "project_folders_model.h"
#include "project_db.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QIcon>
#include <QDebug>
#include <QApplication>
#include <QStyle>

ProjectFoldersModel::ProjectFoldersModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

void ProjectFoldersModel::setProjectId(int projectId)
{
    if (m_projectId == projectId) return;
    m_projectId = projectId;
    reload();
}

void ProjectFoldersModel::reload()
{
    beginResetModel();
    m_folders.clear();
    m_idToIndex.clear();
    m_rootFolderId = -1;

    if (m_projectId <= 0) {
        qDebug() << "ProjectFoldersModel::reload - no project set";
        endResetModel();
        return;
    }

    qDebug() << "ProjectFoldersModel::reload - loading folders for project" << m_projectId;

    // Query all folders for this project
    QSqlDatabase db = ProjectDB::instance().database();
    QSqlQuery q(db);
    q.prepare("SELECT id, name, parent_id, project_id FROM virtual_folders WHERE project_id = ? ORDER BY name");
    q.addBindValue(m_projectId);

    if (!q.exec()) {
        qWarning() << "ProjectFoldersModel::reload failed:" << q.lastError();
        endResetModel();
        return;
    }

    // Get the global root folder ID for comparison
    int globalRootId = ProjectDB::instance().ensureRootFolder();
    qDebug() << "ProjectFoldersModel::reload - global root ID:" << globalRootId;
    
    while (q.next()) {
        ProjectFolder f;
        f.id = q.value(0).toInt();
        f.name = q.value(1).toString();
        f.parentId = q.value(2).isNull() ? 0 : q.value(2).toInt();
        f.projectId = q.value(3).toInt();
        
        int idx = m_folders.size();
        m_idToIndex.insert(f.id, idx);
        m_folders.append(f);
        
        qDebug() << "ProjectFoldersModel::reload - folder:" << f.id << f.name << "parent:" << f.parentId << "project:" << f.projectId;
    }
    
    qDebug() << "ProjectFoldersModel::reload - loaded" << m_folders.size() << "folders";
    
    // Find root folder: parent is either NULL/0, the global root, or not in this project's folders
    for (const ProjectFolder &f : std::as_const(m_folders)) {
        if (f.parentId == 0 || f.parentId == globalRootId || !m_idToIndex.contains(f.parentId)) {
            m_rootFolderId = f.id;
            qDebug() << "ProjectFoldersModel::reload - found root folder:" << f.id << f.name;
            break; // There should be only one root per project
        }
    }

    buildTree();
    endResetModel();
}

void ProjectFoldersModel::buildTree()
{
    // Build parent-child relationships
    for (int i = 0; i < m_folders.size(); ++i) {
        ProjectFolder &f = m_folders[i];
        if (f.parentId > 0 && m_idToIndex.contains(f.parentId)) {
            int parentIdx = m_idToIndex.value(f.parentId);
            m_folders[parentIdx].childIds.append(f.id);
        }
    }
}

QVector<int> ProjectFoldersModel::childrenOf(int parentId) const
{
    if (!m_idToIndex.contains(parentId)) return {};
    int idx = m_idToIndex.value(parentId);
    return m_folders.at(idx).childIds;
}

QModelIndex ProjectFoldersModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0) return QModelIndex();
    
    if (!parent.isValid()) {
        // Root level - show children of root folder directly (not the root itself)
        if (m_rootFolderId > 0) {
            QVector<int> rootChildren = childrenOf(m_rootFolderId);
            if (row >= 0 && row < rootChildren.size()) {
                return createIndex(row, 0, quintptr(rootChildren.at(row)));
            }
        }
        return QModelIndex();
    }
    
    // Get children of parent
    int parentFolderId = int(parent.internalId());
    QVector<int> children = childrenOf(parentFolderId);
    if (row < 0 || row >= children.size()) return QModelIndex();
    
    return createIndex(row, 0, quintptr(children.at(row)));
}

QModelIndex ProjectFoldersModel::parent(const QModelIndex &index) const
{
    if (!index.isValid()) return QModelIndex();
    
    int folderId = int(index.internalId());
    if (!m_idToIndex.contains(folderId)) return QModelIndex();
    
    const ProjectFolder &f = m_folders.at(m_idToIndex.value(folderId));
    // If parent is root folder or not in map, this is a top-level item
    if (f.parentId <= 0 || f.parentId == m_rootFolderId || !m_idToIndex.contains(f.parentId)) {
        return QModelIndex();
    }
    
    // We have a real parent that's not the root - find its row
    const ProjectFolder &pf = m_folders.at(m_idToIndex.value(f.parentId));
    
    // If parent's parent is the root folder, parent is at top level
    if (pf.parentId <= 0 || pf.parentId == m_rootFolderId || !m_idToIndex.contains(pf.parentId)) {
        // Find row of parent among root's children
        QVector<int> rootChildren = childrenOf(m_rootFolderId);
        int row = rootChildren.indexOf(f.parentId);
        return createIndex(row, 0, quintptr(f.parentId));
    }
    
    // Parent is deeper - find its row among its siblings
    QVector<int> siblings = childrenOf(pf.parentId);
    int row = siblings.indexOf(f.parentId);
    return createIndex(row, 0, quintptr(f.parentId));
}

int ProjectFoldersModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid()) {
        // Root level - show children of the root folder directly
        if (m_rootFolderId > 0) {
            return childrenOf(m_rootFolderId).size();
        }
        return 0;
    }
    
    int parentFolderId = int(parent.internalId());
    return childrenOf(parentFolderId).size();
}

int ProjectFoldersModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant ProjectFoldersModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return QVariant();
    
    int folderId = int(index.internalId());
    if (!m_idToIndex.contains(folderId)) return QVariant();
    
    const ProjectFolder &f = m_folders.at(m_idToIndex.value(folderId));
    
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return f.name;
    case Qt::DecorationRole:
        return QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    case IdRole:
        return f.id;
    case ParentIdRole:
        return f.parentId;
    case ProjectIdRole:
        return f.projectId;
    default:
        return QVariant();
    }
}

Qt::ItemFlags ProjectFoldersModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

QVariant ProjectFoldersModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0) {
        return tr("Folders");
    }
    return QVariant();
}

int ProjectFoldersModel::folderIdForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) return -1;
    return int(index.internalId());
}

QModelIndex ProjectFoldersModel::indexForFolderId(int folderId) const
{
    if (!m_idToIndex.contains(folderId)) return QModelIndex();
    return findIndexRecursive(QModelIndex(), folderId);
}

QModelIndex ProjectFoldersModel::findIndexRecursive(const QModelIndex &parent, int folderId) const
{
    int rows = rowCount(parent);
    for (int i = 0; i < rows; ++i) {
        QModelIndex idx = index(i, 0, parent);
        if (int(idx.internalId()) == folderId) return idx;
        QModelIndex found = findIndexRecursive(idx, folderId);
        if (found.isValid()) return found;
    }
    return QModelIndex();
}

int ProjectFoldersModel::parentFolderId(int folderId) const
{
    if (!m_idToIndex.contains(folderId)) return -1;
    return m_folders.at(m_idToIndex.value(folderId)).parentId;
}
