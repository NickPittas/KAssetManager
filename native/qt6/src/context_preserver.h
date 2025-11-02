#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QPoint>

/**
 * @brief Context Preserver - Saves and restores UI state per folder and session
 * 
 * This class manages the preservation of UI context including:
 * - Scroll positions per folder
 * - View mode (grid/list) per folder
 * - Filter settings per folder
 * - Selected assets per folder
 * - Last active folder
 * 
 * Uses QSettings for persistence with keys like:
 * "AssetManager/Context/Folder_{folderId}/ScrollPosition"
 * "AssetManager/Context/Folder_{folderId}/ViewMode"
 * etc.
 */
class ContextPreserver : public QObject {
    Q_OBJECT

public:
    static ContextPreserver& instance();

    // Per-folder context
    struct FolderContext {
        int scrollPosition = 0;
        bool isGridMode = true;
        QString searchText;
        int ratingFilter = -1; // -1 = all
        QSet<int> selectedTagIds;
        QSet<int> selectedAssetIds;
        QString sortColumn;
        Qt::SortOrder sortOrder = Qt::AscendingOrder;
        bool recursiveMode = false;
        
        FolderContext() = default;
    };

    // Save/restore per-folder context
    void saveFolderContext(int folderId, const FolderContext& context);
    FolderContext loadFolderContext(int folderId) const;
    bool hasFolderContext(int folderId) const;
    void clearFolderContext(int folderId);
    void clearAllFolderContexts();

    // Global session state
    void saveLastActiveFolder(int folderId);
    int loadLastActiveFolder() const;

    void saveLastActiveTab(int tabIndex);
    int loadLastActiveTab() const;

    // File Manager context
    struct FileManagerContext {
        QString currentPath;
        int scrollPosition = 0;
        bool isGridMode = true;
        QStringList selectedPaths;
        
        FileManagerContext() = default;
    };

    void saveFileManagerContext(const FileManagerContext& context);
    FileManagerContext loadFileManagerContext() const;

    // Cleanup old contexts (e.g., for deleted folders)
    void cleanupOrphanedContexts(const QSet<int>& validFolderIds);

signals:
    void contextRestored(int folderId);

private:
    explicit ContextPreserver(QObject* parent = nullptr);
    ~ContextPreserver() = default;
    ContextPreserver(const ContextPreserver&) = delete;
    ContextPreserver& operator=(const ContextPreserver&) = delete;

    QString folderContextKey(int folderId, const QString& key) const;
    QSettings* settings() const;

    mutable QSettings* m_settings = nullptr;
};

