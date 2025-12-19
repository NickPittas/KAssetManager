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
#include <QtConcurrent/QtConcurrentRun>
#include <QMutexLocker>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
constexpr int kMaxEverythingResults = 65535;

// Cached icons - standardIcon() is very expensive (shell calls on Windows)
static QIcon& cachedDriveIcon() {
    static QIcon icon;
    if (icon.isNull()) icon = QApplication::style()->standardIcon(QStyle::SP_DriveHDIcon);
    return icon;
}
static QIcon& cachedNetworkIcon() {
    static QIcon icon;
    if (icon.isNull()) icon = QApplication::style()->standardIcon(QStyle::SP_DriveNetIcon);
    return icon;
}
static QIcon& cachedFolderIcon() {
    static QIcon icon;
    if (icon.isNull()) icon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    return icon;
}
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
    // Cancel all pending fetches
    for (auto *watcher : m_pendingFetches) {
        watcher->cancel();
        watcher->waitForFinished();
        delete watcher;
    }
    m_pendingFetches.clear();
    
    for (auto *watcher : m_pendingPathResolves) {
        watcher->cancel();
        watcher->waitForFinished();
        delete watcher;
    }
    m_pendingPathResolves.clear();
    
    clear(m_root);
    m_nodesByPath.clear();
}

void EverythingFolderModel::refresh()
{
    beginResetModel();
    
    // Cancel all pending fetches
    for (auto *watcher : m_pendingFetches) {
        watcher->cancel();
        watcher->waitForFinished();
        delete watcher;
    }
    m_pendingFetches.clear();
    m_fetchingPaths.clear();
    
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
    // DO NOT call ensureFetched here - it blocks the UI thread!
    // The view should call canFetchMore/fetchMore for lazy loading
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
            // Use cached icons to avoid expensive shell calls on every repaint
            if (isDrive) return cachedDriveIcon();
            if (isNetwork) return cachedNetworkIcon();
            return cachedFolderIcon();
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
    // Can fetch more if the node hasn't been fetched yet and isn't currently fetching
    return !node->fetched && !node->fetching && node != m_root;
}

void EverythingFolderModel::fetchMore(const QModelIndex &parent)
{
    Node *node = parent.isValid() ? static_cast<Node*>(parent.internalPointer()) : m_root;
    if (!node || node->fetched || node->fetching || node == m_root) {
        return;
    }
    qDebug() << "[EverythingFolderModel] fetchMore (async) called for node:" << node->name << "path:" << node->path;
    fetchChildrenAsync(node);
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
        
        // If current node isn't fetched, we can't continue synchronously
        // Return what we have and let caller use resolvePathAsync for full resolution
        if (!current->fetched) {
            qDebug() << "[EverythingFolderModel] indexForPath: node not fetched, returning partial index for" << current->path;
            return currentIndex;
        }
        
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

void EverythingFolderModel::resolvePathAsync(const QString &path)
{
    QString normalized = normalizePath(path);
    if (normalized.isEmpty()) {
        emit pathResolved(path, QModelIndex());
        return;
    }
    
    const QString key = normalized.toCaseFolded();
    
    // Check if already cached
    if (m_nodesByPath.contains(key)) {
        Node *node = m_nodesByPath.value(key);
        emit pathResolved(path, createIndex(rowOfNode(node), 0, node));
        return;
    }
    
    // Check if resolution is already in progress
    if (m_pendingPathResolves.contains(key)) {
        return;
    }
    
    QStringList segments = tokenizePath(normalized);
    if (segments.isEmpty()) {
        emit pathResolved(path, QModelIndex());
        return;
    }
    
    // Find how far we can go synchronously
    Node *current = m_root;
    int resolvedCount = 0;
    
    for (int i = 0; i < segments.size(); ++i) {
        if (!current->fetched) {
            break;
        }
        
        const QString &segment = segments.at(i);
        Node *match = nullptr;
        
        if (i == 0 && segment.contains(QLatin1Char(':'))) {
            for (Node *candidate : current->children) {
                if (candidate->path.compare(segment, Qt::CaseInsensitive) == 0) {
                    match = candidate;
                    break;
                }
            }
        } else {
            for (Node *candidate : current->children) {
                if (candidate->name.compare(segment, Qt::CaseInsensitive) == 0) {
                    match = candidate;
                    break;
                }
            }
        }
        
        if (!match) {
            break;
        }
        
        current = match;
        resolvedCount = i + 1;
    }
    
    // If fully resolved, emit immediately
    if (resolvedCount == segments.size()) {
        m_nodesByPath.insert(key, current);
        emit pathResolved(path, createIndex(rowOfNode(current), 0, current));
        return;
    }
    
    // Need to fetch more - start async resolution
    // First, trigger fetch for the first unfetched node
    if (!current->fetched && !current->fetching) {
        fetchChildrenAsync(current);
    }
    
    // Store the pending request to retry when fetch completes
    // For simplicity, we'll just retry indexForPath after each childrenFetched signal
    // The MainWindow will handle re-calling resolvePathAsync
    qDebug() << "[EverythingFolderModel] resolvePathAsync: need to fetch more for" << path 
             << ", resolved" << resolvedCount << "of" << segments.size() << "segments";
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

        // Get drive letter (e.g., "C:")
        QString driveLetter = drivePath;
        if (driveLetter.endsWith(QLatin1Char('\\'))) {
            driveLetter.chop(1);
        }

        // Get volume name using Windows API
        QString displayName = driveLetter;

#ifdef Q_OS_WIN
        wchar_t volumeName[MAX_PATH + 1] = {0};
        wchar_t fileSystemName[MAX_PATH + 1] = {0};
        DWORD serialNumber = 0;
        DWORD maxComponentLen = 0;
        DWORD fileSystemFlags = 0;

        QString rootPath = drivePath;
        if (!rootPath.endsWith(QLatin1Char('\\'))) {
            rootPath += QLatin1Char('\\');
        }

        if (GetVolumeInformationW(
            reinterpret_cast<const wchar_t*>(rootPath.utf16()),
            volumeName,
            MAX_PATH,
            &serialNumber,
            &maxComponentLen,
            &fileSystemFlags,
            fileSystemName,
            MAX_PATH))
        {
            QString volName = QString::fromWCharArray(volumeName);
            if (!volName.isEmpty()) {
                displayName = QString("%1 (%2)").arg(driveLetter).arg(volName);
            }
        }
#endif

        createNode(m_root, displayName, drivePath);
    }
}

void EverythingFolderModel::fetchChildrenAsync(Node *node)
{
    if (!node || node->fetched || node->fetching || node == m_root) {
        return;
    }
    
    const QString pathKey = node->path.toCaseFolded();
    
    // Check if already fetching this path
    {
        QMutexLocker locker(&m_fetchMutex);
        if (m_fetchingPaths.contains(pathKey)) {
            return;
        }
        m_fetchingPaths.insert(pathKey);
    }
    
    node->fetching = true;
    qDebug() << "[EverythingFolderModel] fetchChildrenAsync START for node:" << node->name << "path:" << node->path;
    
    // Create watcher for this fetch
    auto *watcher = new QFutureWatcher<FetchResult>(this);
    m_pendingFetches.insert(pathKey, watcher);
    
    // Use lambda to capture pathKey for the callback
    connect(watcher, &QFutureWatcher<FetchResult>::finished, this, [this, watcher, pathKey]() {
        FetchResult result = watcher->result();
        
        // Remove from pending
        m_pendingFetches.remove(pathKey);
        {
            QMutexLocker locker(&m_fetchMutex);
            m_fetchingPaths.remove(pathKey);
        }
        watcher->deleteLater();
        
        // Apply result on UI thread
        applyFetchResult(result);
    });
    
    // Start background fetch
    QFuture<FetchResult> future = QtConcurrent::run(&EverythingFolderModel::fetchChildrenWorker, node->path);
    watcher->setFuture(future);
}

EverythingFolderModel::FetchResult EverythingFolderModel::fetchChildrenWorker(const QString &parentPath)
{
    FetchResult result;
    result.nodePath = parentPath;
    
    if (parentPath.isEmpty()) {
        return result;
    }
    
    qDebug() << "[EverythingFolderModel] fetchChildrenWorker running in thread for:" << parentPath;
    
    // Try Everything SDK first
    const QString query = QStringLiteral("parent:\"%1\" folder:").arg(parentPath);
    QVector<EverythingResult> searchResults = EverythingSearch::instance().search(query, kMaxEverythingResults);
    
    qDebug() << "[EverythingFolderModel] Everything search returned" << searchResults.size() << "results for" << parentPath;
    
    // Sort results
    std::sort(searchResults.begin(), searchResults.end(), [](const EverythingResult &a, const EverythingResult &b) {
        return QString::localeAwareCompare(a.fileName, b.fileName) < 0;
    });
    
    QSet<QString> seen;
    for (const EverythingResult &sr : searchResults) {
        if (!sr.isFolder) {
            continue;
        }
        QString childPath = sr.fullPath;
        // Normalize path
        childPath = QDir::toNativeSeparators(childPath.trimmed());
        if (childPath.isEmpty()) {
            continue;
        }
        const QString key = childPath.toCaseFolded();
        if (seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        result.children.append(qMakePair(sr.fileName, childPath));
    }
    
    // Fallback to QDir if Everything returned nothing
    if (result.children.isEmpty()) {
        qDebug() << "[EverythingFolderModel] Falling back to QDir for:" << parentPath;
        QDir dir(parentPath);
        const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &entry : entries) {
            QString childPath = QDir::toNativeSeparators(entry.absoluteFilePath());
            if (childPath.isEmpty()) {
                continue;
            }
            const QString key = childPath.toCaseFolded();
            if (seen.contains(key)) {
                continue;
            }
            seen.insert(key);
            result.children.append(qMakePair(entry.fileName(), childPath));
        }
    }
    
    qDebug() << "[EverythingFolderModel] fetchChildrenWorker completed with" << result.children.size() << "children";
    return result;
}

void EverythingFolderModel::applyFetchResult(const FetchResult &result)
{
    const QString pathKey = result.nodePath.toCaseFolded();
    
    // Find the node
    Node *node = m_nodesByPath.value(pathKey, nullptr);
    if (!node) {
        qWarning() << "[EverythingFolderModel] applyFetchResult: node not found for" << result.nodePath;
        return;
    }
    
    if (node->fetched) {
        qDebug() << "[EverythingFolderModel] applyFetchResult: node already fetched" << result.nodePath;
        node->fetching = false;
        return;
    }
    
    qDebug() << "[EverythingFolderModel] applyFetchResult applying" << result.children.size() << "children to" << result.nodePath;
    
    QVector<Node*> newChildren;
    for (const auto &pair : result.children) {
        const QString &name = pair.first;
        const QString &childPath = pair.second;
        const QString childKey = childPath.toCaseFolded();
        
        // Skip if already exists
        if (m_nodesByPath.contains(childKey)) {
            continue;
        }
        
        newChildren.append(createNode(nullptr, name, childPath));
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
    node->fetching = false;
    
    // Emit signal so MainWindow can continue path resolution if needed
    QModelIndex parentIndex = (node->parent == nullptr || node == m_root)
        ? QModelIndex()
        : createIndex(rowOfNode(node), 0, node);
    emit childrenFetched(parentIndex);
    
    qDebug() << "[EverythingFolderModel] applyFetchResult completed for" << result.nodePath;
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
