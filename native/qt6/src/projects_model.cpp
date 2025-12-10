#include "projects_model.h"
#include <QDebug>

ProjectsModel::ProjectsModel(QObject* parent)
    : QAbstractListModel(parent)
{
    // Debounced reload (100ms)
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(100);
    connect(&m_reloadTimer, &QTimer::timeout, this, &ProjectsModel::reload);

    // Connect to DB signals
    connect(&ProjectDB::instance(), &ProjectDB::projectsChanged,
            this, &ProjectsModel::scheduleReload);
    connect(&ProjectDB::instance(), &ProjectDB::notificationsChanged,
            this, &ProjectsModel::scheduleReload);

    reload();
}

int ProjectsModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return m_projects.size();
}

QVariant ProjectsModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_projects.size())
        return QVariant();

    const Project& p = m_projects.at(idx.row());

    switch (role) {
    case IdRole:
        return p.id;
    case NameRole:
    case Qt::DisplayRole:
        return p.name;
    case WatchPathRole:
        return p.watchPath;
    case CreatedAtRole:
        return p.createdAt;
    case UpdatedAtRole:
        return p.updatedAt;
    case AssetCountRole:
        return ProjectDB::instance().getAssetIdsInProject(p.id).size();
    case UnreadCountRole:
        return ProjectDB::instance().getUnacknowledgedCount(p.id);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ProjectsModel::roleNames() const {
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {WatchPathRole, "watchPath"},
        {CreatedAtRole, "createdAt"},
        {UpdatedAtRole, "updatedAt"},
        {AssetCountRole, "assetCount"},
        {UnreadCountRole, "unreadCount"}
    };
}

int ProjectsModel::createProject(const QString& name, const QString& watchPath) {
    int id = ProjectDB::instance().createProject(name, watchPath);
    if (id > 0) {
        scheduleReload();
    }
    return id;
}

bool ProjectsModel::renameProject(int id, const QString& name) {
    bool ok = ProjectDB::instance().renameProject(id, name);
    if (ok) scheduleReload();
    return ok;
}

bool ProjectsModel::deleteProject(int id) {
    bool ok = ProjectDB::instance().deleteProject(id);
    if (ok) scheduleReload();
    return ok;
}

QVariantMap ProjectsModel::get(int row) const {
    QVariantMap m;
    if (row < 0 || row >= m_projects.size()) return m;

    const Project& p = m_projects.at(row);
    m["id"] = p.id;
    m["name"] = p.name;
    m["watchPath"] = p.watchPath;
    m["createdAt"] = p.createdAt;
    m["updatedAt"] = p.updatedAt;
    m["assetCount"] = ProjectDB::instance().getAssetIdsInProject(p.id).size();
    m["unreadCount"] = ProjectDB::instance().getUnacknowledgedCount(p.id);
    return m;
}

void ProjectsModel::scheduleReload() {
    m_reloadTimer.start();
}

void ProjectsModel::reload() {
    beginResetModel();
    m_projects = ProjectDB::instance().listProjects();
    endResetModel();
}
