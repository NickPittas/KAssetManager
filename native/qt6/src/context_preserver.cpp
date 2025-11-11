#include "context_preserver.h"
#include <QDebug>
#include <QMetaType>
#include <QList>
#include <QDataStream>

ContextPreserver& ContextPreserver::instance() {
    static ContextPreserver s;
    return s;
}

ContextPreserver::ContextPreserver(QObject* parent)
    : QObject(parent)
{
    // Register legacy type used in older settings to avoid QVariant::load warnings
    // Keys "SelectedTagIds" and "SelectedAssetIds" previously stored as QVariant(QList<int>).
    // This registration lets Qt deserialize existing values so we can migrate them.
    // Register legacy type name so QSettings can deserialize existing values.
    // In Qt 6 the free function qRegisterMetaTypeStreamOperators was removed;
    // stream operators for QList<int> are already available via templates.
    qRegisterMetaType<QList<int>>("QList<int>");
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
    
    // Save selected tag IDs (store as QVariantList of ints for portability)
    QVariantList tagList;
    tagList.reserve(context.selectedTagIds.size());
    for (int id : context.selectedTagIds) tagList.push_back(id);
    s->setValue("SelectedTagIds", tagList);

    // Save selected asset IDs (store as QVariantList of ints)
    QVariantList assetList;
    assetList.reserve(context.selectedAssetIds.size());
    for (int id : context.selectedAssetIds) assetList.push_back(id);
    s->setValue("SelectedAssetIds", assetList);
    
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
    
    // Load selected tag IDs (prefer QVariantList; fall back to legacy QList<int>)
    if (s->contains("SelectedTagIds")) {
        QVariant v = s->value("SelectedTagIds");
        QSet<int> out;
        if (v.canConvert<QVariantList>()) {
            const QVariantList lst = v.toList();
            for (const QVariant& qv : lst) out.insert(qv.toInt());
        } else {
            // Legacy path
            QList<int> legacy = v.value<QList<int>>();
            out = QSet<int>(legacy.begin(), legacy.end());
            // Migrate by re-saving in the new format
            QVariantList asList; for (int id : out) asList.push_back(id);
            s->setValue("SelectedTagIds", asList);
        }
        context.selectedTagIds = out;
    }

    // Load selected asset IDs (prefer QVariantList; fall back to legacy QList<int>)
    if (s->contains("SelectedAssetIds")) {
        QVariant v = s->value("SelectedAssetIds");
        QSet<int> out;
        if (v.canConvert<QVariantList>()) {
            const QVariantList lst = v.toList();
            for (const QVariant& qv : lst) out.insert(qv.toInt());
        } else {
            QList<int> legacy = v.value<QList<int>>();
            out = QSet<int>(legacy.begin(), legacy.end());
            QVariantList asList; for (int id : out) asList.push_back(id);
            s->setValue("SelectedAssetIds", asList);
        }
        context.selectedAssetIds = out;
    }
    
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

