#include "context_preserver.h"
#include <QDebug>

ContextPreserver& ContextPreserver::instance() {
    static ContextPreserver s;
    return s;
}

ContextPreserver::ContextPreserver(QObject* parent)
    : QObject(parent)
{
    m_settings = new QSettings("AugmentCode", "KAssetManager", this);
}

QSettings* ContextPreserver::settings() const {
    if (!m_settings) {
        m_settings = new QSettings("AugmentCode", "KAssetManager", const_cast<ContextPreserver*>(this));
    }
    return m_settings;
}

QString ContextPreserver::folderContextKey(int folderId, const QString& key) const {
    return QString("AssetManager/Context/Folder_%1/%2").arg(folderId).arg(key);
}

void ContextPreserver::saveFolderContext(int folderId, const FolderContext& context) {
    if (folderId <= 0) return;

    QSettings* s = settings();
    s->beginGroup(QString("AssetManager/Context/Folder_%1").arg(folderId));
    
    s->setValue("ScrollPosition", context.scrollPosition);
    s->setValue("IsGridMode", context.isGridMode);
    s->setValue("SearchText", context.searchText);
    s->setValue("RatingFilter", context.ratingFilter);
    s->setValue("SortColumn", context.sortColumn);
    s->setValue("SortOrder", static_cast<int>(context.sortOrder));
    s->setValue("RecursiveMode", context.recursiveMode);
    
    // Save selected tag IDs
    QList<int> tagIdList = context.selectedTagIds.values();
    s->setValue("SelectedTagIds", QVariant::fromValue(tagIdList));
    
    // Save selected asset IDs
    QList<int> assetIdList = context.selectedAssetIds.values();
    s->setValue("SelectedAssetIds", QVariant::fromValue(assetIdList));
    
    s->endGroup();
    s->sync();
    
    qDebug() << "[ContextPreserver] Saved context for folder" << folderId;
}

ContextPreserver::FolderContext ContextPreserver::loadFolderContext(int folderId) const {
    FolderContext context;
    if (folderId <= 0) return context;

    QSettings* s = settings();
    s->beginGroup(QString("AssetManager/Context/Folder_%1").arg(folderId));
    
    if (!s->contains("ScrollPosition")) {
        s->endGroup();
        return context; // No saved context
    }
    
    context.scrollPosition = s->value("ScrollPosition", 0).toInt();
    context.isGridMode = s->value("IsGridMode", true).toBool();
    context.searchText = s->value("SearchText", "").toString();
    context.ratingFilter = s->value("RatingFilter", -1).toInt();
    context.sortColumn = s->value("SortColumn", "").toString();
    context.sortOrder = static_cast<Qt::SortOrder>(s->value("SortOrder", Qt::AscendingOrder).toInt());
    context.recursiveMode = s->value("RecursiveMode", false).toBool();
    
    // Load selected tag IDs
    QList<int> tagIdList = s->value("SelectedTagIds").value<QList<int>>();
    context.selectedTagIds = QSet<int>(tagIdList.begin(), tagIdList.end());
    
    // Load selected asset IDs
    QList<int> assetIdList = s->value("SelectedAssetIds").value<QList<int>>();
    context.selectedAssetIds = QSet<int>(assetIdList.begin(), assetIdList.end());
    
    s->endGroup();
    
    qDebug() << "[ContextPreserver] Loaded context for folder" << folderId
             << "- scroll:" << context.scrollPosition
             << "grid:" << context.isGridMode
             << "search:" << context.searchText;
    
    return context;
}

bool ContextPreserver::hasFolderContext(int folderId) const {
    if (folderId <= 0) return false;
    QSettings* s = settings();
    return s->contains(folderContextKey(folderId, "ScrollPosition"));
}

void ContextPreserver::clearFolderContext(int folderId) {
    if (folderId <= 0) return;
    
    QSettings* s = settings();
    s->remove(QString("AssetManager/Context/Folder_%1").arg(folderId));
    s->sync();
    
    qDebug() << "[ContextPreserver] Cleared context for folder" << folderId;
}

void ContextPreserver::clearAllFolderContexts() {
    QSettings* s = settings();
    s->remove("AssetManager/Context");
    s->sync();
    
    qDebug() << "[ContextPreserver] Cleared all folder contexts";
}

void ContextPreserver::saveLastActiveFolder(int folderId) {
    QSettings* s = settings();
    s->setValue("AssetManager/LastActiveFolder", folderId);
    s->sync();
}

int ContextPreserver::loadLastActiveFolder() const {
    QSettings* s = settings();
    return s->value("AssetManager/LastActiveFolder", -1).toInt();
}

void ContextPreserver::saveLastActiveTab(int tabIndex) {
    QSettings* s = settings();
    s->setValue("MainWindow/LastActiveTab", tabIndex);
    s->sync();
}

int ContextPreserver::loadLastActiveTab() const {
    QSettings* s = settings();
    return s->value("MainWindow/LastActiveTab", 0).toInt();
}

void ContextPreserver::saveFileManagerContext(const FileManagerContext& context) {
    QSettings* s = settings();
    s->beginGroup("FileManager/Context");
    
    s->setValue("CurrentPath", context.currentPath);
    s->setValue("ScrollPosition", context.scrollPosition);
    s->setValue("IsGridMode", context.isGridMode);
    s->setValue("SelectedPaths", context.selectedPaths);
    
    s->endGroup();
    s->sync();
    
    qDebug() << "[ContextPreserver] Saved File Manager context - path:" << context.currentPath;
}

ContextPreserver::FileManagerContext ContextPreserver::loadFileManagerContext() const {
    FileManagerContext context;
    
    QSettings* s = settings();
    s->beginGroup("FileManager/Context");
    
    context.currentPath = s->value("CurrentPath", "").toString();
    context.scrollPosition = s->value("ScrollPosition", 0).toInt();
    context.isGridMode = s->value("IsGridMode", true).toBool();
    context.selectedPaths = s->value("SelectedPaths").toStringList();
    
    s->endGroup();
    
    qDebug() << "[ContextPreserver] Loaded File Manager context - path:" << context.currentPath;
    
    return context;
}

void ContextPreserver::cleanupOrphanedContexts(const QSet<int>& validFolderIds) {
    QSettings* s = settings();
    s->beginGroup("AssetManager/Context");
    
    QStringList allGroups = s->childGroups();
    int cleaned = 0;
    
    for (const QString& group : allGroups) {
        // Extract folder ID from "Folder_123" format
        if (group.startsWith("Folder_")) {
            bool ok;
            int folderId = group.mid(7).toInt(&ok);
            if (ok && !validFolderIds.contains(folderId)) {
                s->remove(group);
                cleaned++;
            }
        }
    }
    
    s->endGroup();
    s->sync();
    
    if (cleaned > 0) {
        qDebug() << "[ContextPreserver] Cleaned up" << cleaned << "orphaned folder contexts";
    }
}

