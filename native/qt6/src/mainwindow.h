#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QTreeView>
#include <QListView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QMenu>
#include <QAction>
#include <QSet>
#include <QTimer>
#include <QTableWidget>
#include <QCheckBox>
#include <QTabWidget>
#include <QFileSystemModel>
#include <QListWidget>

#include <QToolButton>

class VirtualFolderTreeModel;
class AssetsModel;
class TagsModel;
class PreviewOverlay;
class SequenceGroupingProxyModel;

class ImportProgressDialog;
class ProjectFolderWatcher;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onFolderSelected(const QModelIndex &index);
    void onAssetSelectionChanged();
    void onAssetDoubleClicked(const QModelIndex &index);
    void onAssetContextMenu(const QPoint &pos);
    void onFolderContextMenu(const QPoint &pos);
    void onEmptySpaceContextMenu(const QPoint &pos);

    void showPreview(int index);
    void closePreview();
    void changePreview(int delta);

    void applyFilters();
    void clearFilters();
    void onSearchTextChanged(const QString &text);

    void onCreateTag();
    void onApplyTags();
    void onFilterByTags();
    void onTagContextMenu(const QPoint &pos);
    void updateTagButtonStates();

    void onOpenSettings();
    void onThumbnailSizeChanged(int size);
    void onViewModeChanged();

    void importFiles(const QStringList &filePaths);
    void onImportProgress(int current, int total);
    void onImportFileChanged(const QString& fileName);
    void onImportFolderChanged(const QString& folderName);
    void onImportComplete();
    void onThumbnailProgress(int current, int total);
    void onRatingChanged(int rating);

    // Project folder operations
    void onAddProjectFolder();
    void onRefreshAssets();
    void onLockToggled(bool checked);
    void onProjectFolderChanged(int projectFolderId, const QString& path);

    // Versioning
    void onRevertSelectedVersion();
    void onAssetVersionsChanged(int assetId);

    // Log viewer
    void onToggleLogViewer();

    // File Manager slots
    void onFmTreeActivated(const QModelIndex &index);
    void onFmTreeContextMenu(const QPoint &pos);
    void onFmItemDoubleClicked(const QModelIndex &index);
    void onFmViewModeToggled();
    void onFmThumbnailSizeChanged(int size);
    void onAddSelectionToAssetLibrary();
    void onFmAddToFavorites();
    void onFmRemoveFavorite();
    void onFmFavoriteActivated(QListWidgetItem* item);

    // File operations
    void onFmCopy();
    void onFmCut();
    void onFmPaste();
    void onFmDelete();
    void onFmDeletePermanent();
    void onFmRename();
    void onFmNewFolder();
    void onFmCreateFolderWithSelected();
    void onFmShowContextMenu(const QPoint &pos);
    void onFmBackToParent();
    void onFmGroupSequencesToggled(bool checked);


    // File Manager preview
    void onFmSelectionChanged();
    void onFmTogglePreview(); // toolbar toggle
    void onFmOpenOverlay();   // Space: open full-screen overlay
private:
    QString fmPathForIndex(const QModelIndex& idx) const;
    void setFmRootPath(const QString& path);


protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void setupConnections();
    void setupFileManagerUi();
    void updateInfoPanel();
    void updateSelectionInfo();
    void reloadVersionHistory();

    // Visible-only thumbnail progress (Option B)
    void scheduleVisibleThumbProgressUpdate();
    void updateVisibleThumbProgress();

    QSet<int> getSelectedAssetIds() const;
    class QItemSelectionModel* getCurrentSelectionModel();
    int getAnchorIndex() const;
    void selectAsset(int assetId, int index, Qt::KeyboardModifiers modifiers);
    void selectSingle(int assetId, int index);
    void toggleSelection(int assetId, int index);
    void selectRange(int fromIndex, int toIndex);
    void clearSelection();

    // Folder tree expansion state management
    void saveFolderExpansionState();
    void restoreFolderExpansionState();
    QSet<int> expandedFolderIds;

    // Sequence helper
    QStringList reconstructSequenceFramePaths(const QString& firstFramePath, int startFrame, int endFrame);

    // Tabs
    QTabWidget *mainTabs;
    QWidget *assetManagerPage;
    QWidget *fileManagerPage;

    // UI Components
    QSplitter *mainSplitter;
    QSplitter *rightSplitter;

    // Left panel: Folder tree (Asset Manager)
    QTreeView *folderTreeView;
    VirtualFolderTreeModel *folderModel;

    // Center panel: Asset grid and table
    class QStackedWidget *viewStack;
    QListView *assetGridView;
    class QTableView *assetTableView;
    AssetsModel *assetsModel;

    // Right panel: Filters + Info
    QWidget *rightPanel;
    QWidget *filtersPanel;
    QWidget *infoPanel;

    // Importer
    class Importer *importer;

    // Filters
    QLineEdit *searchBox;
    QComboBox *ratingFilter;
    QListView *tagsListView;
    TagsModel *tagsModel;
    QPushButton *applyTagsBtn;
    QPushButton *filterByTagsBtn;
    QComboBox *tagFilterModeCombo;

    // View controls
    QSlider *thumbnailSizeSlider;
    QPushButton *viewModeButton;
    bool isGridMode;
    class QCheckBox *lockCheckBox;
    class QCheckBox *recursiveCheckBox;
    QPushButton *refreshButton;

    // Info panel labels
    QLabel *infoFileName;
    QLabel *infoFilePath;
    QLabel *infoFileSize;
    QLabel *infoFileType;
    QLabel *infoDimensions;
    QLabel *infoCreated;
    QLabel *infoModified;
    QLabel *infoPermissions;
    QLabel *infoRatingLabel;
    class StarRatingWidget *infoRatingWidget;
    QLabel *infoTags;

    // Version history UI
    QLabel *versionsTitleLabel;
    QTableWidget *versionTable;
    QPushButton *revertVersionButton;
    QCheckBox *backupVersionCheck;

    // Selection state
    QSet<int> selectedAssetIds;
    int anchorIndex;
    int currentAssetId;
    int previewIndex;

    // Preview overlay
    PreviewOverlay *previewOverlay;

    // Thumbnail generation progress
    QLabel *thumbnailProgressLabel;
    class QProgressBar *thumbnailProgressBar;

    // Debounced updater for visible-only thumbnail progress
    QTimer visibleThumbTimer;

    // Import progress dialog
    ImportProgressDialog *importProgressDialog;

    // Project folder watcher
    ProjectFolderWatcher *projectFolderWatcher;
    bool assetsLocked;

    // Log viewer
    class LogViewerWidget *logViewerWidget;
    QAction *toggleLogViewerAction;

    // File Manager members
    QSplitter *fmSplitter;
    // Sequence grouping
    SequenceGroupingProxyModel *fmProxyModel = nullptr;
    QToolButton *fmGroupSequencesButton = nullptr;
    bool fmGroupSequences = true;

    QSplitter *fmLeftSplitter;   // Favorites | Folder tree
    QSplitter *fmRightSplitter;  // Views | Preview panel
    // Left pane
    QListWidget *fmFavoritesList;
    QTreeView *fmTree;
    QFileSystemModel *fmTreeModel;
    // Right pane
    QFileSystemModel *fmDirModel;
    QWidget *fmToolbar;
    QPushButton *fmViewModeButton;
    class QSlider *fmThumbnailSizeSlider;
    QPushButton *fmPreviewToggleButton;
    class QStackedWidget *fmViewStack;
    QListView *fmGridView;
    class QTableView *fmListView;
    bool fmIsGridMode;

    // Favorites persistence
    QStringList fmFavorites;
    void loadFmFavorites();
    void saveFmFavorites();

    // Preview panel (embedded, right side)
    QWidget *fmPreviewPanel;
    class QGraphicsView *fmImageView;
    class QGraphicsScene *fmImageScene;
    class QGraphicsPixmapItem *fmImageItem;
    class QVideoWidget *fmVideoWidget;
    bool fmImageFitToView = true; // auto fit image to view and refit on resize until user zooms manually
    class QMediaPlayer *fmMediaPlayer;
    class QAudioOutput *fmAudioOutput;
    QPushButton *fmPlayPauseBtn;
    // Shortcuts management for File Manager
    QHash<QString, class QShortcut*> fmShortcutObjs;
    void applyFmShortcuts();
    static QKeySequence fmShortcutFor(const QString& actionName, const QKeySequence& def);

    // Helpers for tree/context operations
    QStringList getSelectedFmTreePaths() const;
    void onFmPasteInto(const QString& destDir);
    void doPermanentDelete(const QStringList& paths);

    QSlider *fmPositionSlider;
    QLabel *fmTimeLabel;
    QSlider *fmVolumeSlider;

    // File operations state
    QStringList fmClipboard;
    bool fmClipboardCutMode = false;
    class FileOpsProgressDialog *fileOpsDialog;

    // Helpers
    void updateFmPreviewForIndex(const QModelIndex &idx);
    void clearFmPreview();
};

#endif // MAINWINDOW_H

