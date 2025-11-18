#include "everything_folder_model.h"

#include "everything_search.h"

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QSet>
#include <QUrl>
#include <QIcon>
#include <QPainter>
#include <QApplication>
#include <QStyle>

#include <algorithm>

namespace {
constexpr int kMaxEverythingResults = 65535;
}

EverythingFolderModel::EverythingFolderModel(QObject *parent)
    : QAbstractItemModel(parent)
{
    m_root = new Node;
    m_root->name = QStringLiteral("Computer");
    m_root->path.clear();
    m_root->fetched = true;
    populateRoot();
}

EverythingFolderModel::~EverythingFolderModel()
{
    clear(m_root);
    m_nodesByPath.clear();
}

void EverythingFolderModel::refresh()
{
    beginResetModel();
    m_nodesByPath.clear();
    clear(m_root);
    m_root = new Node;
    m_root->name = QStringLiteral("Computer");
    m_root->path.clear();
    m_root->fetched = true;
    populateRoot();
    endResetModel();
}

int EverythingFolderModel::rowCount(const QModelIndex &parent) const
{
    Node *node = parent.isValid() ? static_cast<Node*>(parent.internalPointer()) : m_root;
    if (!node) {
        return 0;
    }
    // DON'T call ensureFetched here - it blocks the UI thread
    // Instead, use canFetchMore/fetchMore for lazy loading
    return node->children.size();
}

int EverythingFolderModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QModelIndex EverythingFolderModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0 || row < 0) {
        return QModelIndex();
    }

    Node *node = parent.isValid() ? static_cast<Node*>(parent.internalPointer()) : m_root;
    if (!node) {
        return QModelIndex();
    }
    ensureFetched(node);
    if (row >= node->children.size()) {
        return QModelIndex();
    }
    Node *child = node->children.at(row);
    return createIndex(row, column, child);
}

QModelIndex EverythingFolderModel::parent(const QModelIndex &child) const
{
    if (!child.isValid()) {
        return QModelIndex();
    }
    Node *node = static_cast<Node*>(child.internalPointer());
    if (!node || !node->parent || node->parent == m_root) {
        return QModelIndex();
    }
    Node *parentNode = node->parent;
    return createIndex(rowOfNode(parentNode), 0, parentNode);
}

QVariant EverythingFolderModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }
    Node *node = static_cast<Node*>(index.internalPointer());
    if (!node) {
        return QVariant();
    }

    switch (role) {
    case Qt::DisplayRole:
        return node->name;
    case Qt::DecorationRole:
        if (index.column() == 0) {
            bool isDrive = node->parent == m_root && node->path.contains(QLatin1Char(':'));
            bool isNetwork = node->path.startsWith(QStringLiteral("\\\\"));
            const QStyle::StandardPixmap pix = isDrive
                ? QStyle::SP_DriveHDIcon
                : (isNetwork ? QStyle::SP_DriveNetIcon : QStyle::SP_DirIcon);
            return QApplication::style()->standardIcon(pix);
        }
        break;
    case Qt::ToolTipRole:
        return node->path;
    default:
        break;
    }
    return QVariant();
}

Qt::ItemFlags EverythingFolderModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled;
}

QVariant EverythingFolderModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole && section == 0) {
        return QStringLiteral("Folders");
    }
    return QVariant();
}

QStringList EverythingFolderModel::mimeTypes() const
{
    return { QStringLiteral("text/uri-list") };
}

QMimeData *EverythingFolderModel::mimeData(const QModelIndexList &indexes) const
{
    auto *mime = new QMimeData();
    QList<QUrl> urls;
    QSet<QString> unique;
    for (const QModelIndex &idx : indexes) {
        QString path = pathForIndex(idx);
        if (path.isEmpty()) {
            continue;
        }
        const QString key = path.toCaseFolded();
        if (unique.contains(key)) {
            continue;
        }
        unique.insert(key);
        urls.append(QUrl::fromLocalFile(path));
    }
    mime->setUrls(urls);
    return mime;
}

Qt::DropActions EverythingFolderModel::supportedDragActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

bool EverythingFolderModel::canFetchMore(const QModelIndex &parent) const
{
    Node *node = parent.isValid() ? static_cast<Node*>(parent.internalPointer()) : m_root;
    if (!node) {
        return false;
    }
    // Can fetch more if the node hasn't been fetched yet
    return !node->fetched && node != m_root;
}

void EverythingFolderModel::fetchMore(const QModelIndex &parent)
{
    Node *node = parent.isValid() ? static_cast<Node*>(parent.internalPointer()) : m_root;
    if (!node || node->fetched || node == m_root) {
        return;
    }
    qInfo() << "[EverythingFolderModel] fetchMore called for node:" << node->name << "path:" << node->path;
    fetchChildren(node);
    qInfo() << "[EverythingFolderModel] fetchMore completed for node:" << node->name;
}

bool EverythingFolderModel::hasChildren(const QModelIndex &parent) const
{
    Node *node = parent.isValid() ? static_cast<Node*>(parent.internalPointer()) : m_root;
    if (!node) {
        return false;
    }
    // Root always has children (drives)
    if (node == m_root) {
        return true;
    }
    // If already fetched, check if it has children
    if (node->fetched) {
        return !node->children.isEmpty();
    }
    // If not fetched yet, assume folders can have children
    // This allows the expand arrow to appear
    return true;
}

QString EverythingFolderModel::pathForIndex(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return QString();
    }
    Node *node = static_cast<Node*>(index.internalPointer());
    return node ? node->path : QString();
}

QModelIndex EverythingFolderModel::indexForPath(const QString &path)
{
    QString normalized = normalizePath(path);
    if (normalized.isEmpty()) {
        return QModelIndex();
    }

    const QString key = normalized.toCaseFolded();
    if (m_nodesByPath.contains(key)) {
        Node *node = m_nodesByPath.value(key);
        return createIndex(rowOfNode(node), 0, node);
    }

    QStringList segments = tokenizePath(normalized);
    if (segments.isEmpty()) {
        return QModelIndex();
    }

    Node *current = m_root;
    QModelIndex currentIndex;

    for (int i = 0; i < segments.size(); ++i) {
        const QString &segment = segments.at(i);
        ensureFetched(current);
        Node *match = nullptr;
        int row = -1;

        if (i == 0 && segment.contains(QLatin1Char(':'))) {
            for (int j = 0; j < current->children.size(); ++j) {
                Node *candidate = current->children.at(j);
                if (candidate->path.compare(segment, Qt::CaseInsensitive) == 0) {
                    match = candidate;
                    row = j;
                    break;
                }
            }
            if (!match) {
                return QModelIndex();
            }
        } else {
            for (int j = 0; j < current->children.size(); ++j) {
                Node *candidate = current->children.at(j);
                if (candidate->name.compare(segment, Qt::CaseInsensitive) == 0) {
                    match = candidate;
                    row = j;
                    break;
                }
            }
            if (!match) {
                return currentIndex;
            }
        }

        current = match;
        currentIndex = createIndex(row, 0, current);
    }

    if (current) {
        m_nodesByPath.insert(key, current);
    }
    return currentIndex;
}

void EverythingFolderModel::clear(Node *node)
{
    if (!node) {
        return;
    }
    for (Node *child : node->children) {
        clear(child);
    }
    delete node;
}

void EverythingFolderModel::populateRoot()
{
    const QFileInfoList drives = QDir::drives();
    for (const QFileInfo &drive : drives) {
        QString drivePath = normalizePath(drive.absoluteFilePath());
        if (drivePath.isEmpty()) {
            continue;
        }
        QString displayName = drivePath;
        if (displayName.endsWith(QLatin1Char('\\'))) {
            displayName.chop(1);
        }
        createNode(m_root, displayName, drivePath);
    }
}

void EverythingFolderModel::ensureFetched(Node *node) const
{
    if (!node || node == m_root || node->fetched) {
        return;
    }
    qInfo() << "[EverythingFolderModel] ensureFetched called for node:" << node->name << "path:" << node->path;
    const_cast<EverythingFolderModel*>(this)->fetchChildren(node);
    qInfo() << "[EverythingFolderModel] ensureFetched completed for node:" << node->name;
}

void EverythingFolderModel::fetchChildren(Node *node)
{
    if (!node || node->fetched) {
        return;
    }

    qInfo() << "[EverythingFolderModel] fetchChildren START for node:" << node->name << "path:" << node->path;

    QVector<Node*> newChildren;

    QString parentPath = node->path;
    QVector<EverythingResult> results;
    if (!parentPath.isEmpty()) {
        const QString query = QStringLiteral("parent:\"%1\" folder:").arg(escapeQueryString(parentPath));
        qInfo() << "[EverythingFolderModel] About to call Everything search with query:" << query;
        results = EverythingSearch::instance().search(query, kMaxEverythingResults);
        qInfo() << "[EverythingFolderModel] Everything search returned" << results.size() << "results";
    }

    std::sort(results.begin(), results.end(), [](const EverythingResult &a, const EverythingResult &b) {
        return QString::localeAwareCompare(a.fileName, b.fileName) < 0;
    });

    QSet<QString> seen;
    for (const EverythingResult &result : results) {
        if (!result.isFolder) {
            continue;
        }
        QString childPath = normalizePath(result.fullPath);
        if (childPath.isEmpty()) {
            continue;
        }
        const QString key = childPath.toCaseFolded();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        QString name = result.fileName;
        newChildren.append(createNode(nullptr, name, childPath));
    }

    if (newChildren.isEmpty()) {
        QDir dir(parentPath);
        const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &entry : entries) {
            QString childPath = normalizePath(entry.absoluteFilePath());
            if (childPath.isEmpty()) {
                continue;
            }
            const QString key = childPath.toCaseFolded();
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            newChildren.append(createNode(nullptr, entry.fileName(), childPath));
        }
    }

    if (!newChildren.isEmpty()) {
        const int startRow = node->children.size();
        const int endRow = startRow + newChildren.size() - 1;
        QModelIndex parentIndex = (node->parent == nullptr || node == m_root)
            ? QModelIndex()
            : createIndex(rowOfNode(node), 0, node);
        beginInsertRows(parentIndex, startRow, endRow);
        for (Node *child : newChildren) {
            child->parent = node;
            node->children.append(child);
            m_nodesByPath.insert(child->path.toCaseFolded(), child);
        }
        endInsertRows();
    }

    node->fetched = true;
}

EverythingFolderModel::Node *EverythingFolderModel::createNode(Node *parent, const QString &name, const QString &path)
{
    Node *node = new Node;
    node->name = name;
    node->path = path;
    node->parent = parent;
    node->fetched = false;
    if (parent) {
        parent->children.append(node);
    }
    if (!path.isEmpty()) {
        m_nodesByPath.insert(path.toCaseFolded(), node);
    }
    return node;
}

QString EverythingFolderModel::normalizePath(const QString &path) const
{
    QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    QString native = QDir::toNativeSeparators(trimmed);
    native.replace('/', '\\');

    if (native.startsWith(QStringLiteral("\\\\"))) {
        while (native.endsWith(QLatin1Char('\\')) && native.length() > 2) {
            native.chop(1);
        }
        return native;
    }

    if (native.size() >= 2 && native[1] == QLatin1Char(':')) {
        native[0] = native[0].toUpper();
        if (native.size() == 2) {
            native.append(QLatin1Char('\\'));
        } else {
            while (native.endsWith(QLatin1Char('\\')) && native.size() > 3) {
                native.chop(1);
            }
        }
        if (native.size() == 2 || native[2] != QLatin1Char('\\')) {
            native.insert(2, QLatin1Char('\\'));
        }
    }
    return native;
}

QStringList EverythingFolderModel::tokenizePath(const QString &path) const
{
    QString normalized = path;
    QStringList segments;
    if (normalized.startsWith(QStringLiteral("\\\\"))) {
        QString withoutPrefix = normalized.mid(2);
        const QStringList parts = withoutPrefix.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
        if (parts.size() >= 2) {
            QString shareRoot = QStringLiteral("\\\\%1\\%2").arg(parts.at(0), parts.at(1));
            segments << shareRoot;
            for (int i = 2; i < parts.size(); ++i) {
                segments << parts.at(i);
            }
        } else {
            segments << normalized;
        }
        return segments;
    }

    if (normalized.size() >= 3 && normalized[1] == QLatin1Char(':')) {
        QString drive = normalized.left(3);
        segments << drive;
        QString rest = normalized.mid(3);
        if (!rest.isEmpty()) {
            segments << rest.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
        }
    }
    return segments;
}

QString EverythingFolderModel::escapeQueryString(const QString &path) const
{
    QString escaped = path;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\""));
    return escaped;
}

int EverythingFolderModel::rowOfNode(const Node *node) const
{
    if (!node || !node->parent) {
        return 0;
    }
    const QVector<Node*> &siblings = node->parent->children;
    for (int i = 0; i < siblings.size(); ++i) {
        if (siblings.at(i) == node) {
            return i;
        }
    }
    return 0;
}
