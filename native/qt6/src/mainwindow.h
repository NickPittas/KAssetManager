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
#include <QHash>
#include <QPixmap>
#include <QTimer>
#include <QTableWidget>
#include <QCheckBox>
#include <QTabWidget>
#include <QFileSystemModel>
#include <QListWidget>

#include <QFileSystemWatcher>

#include <QToolButton>

class QSettings;
class VirtualFolderTreeModel;
class AssetsModel;
class TagsModel;
class PreviewOverlay;
class SequenceGroupingProxyModel;
class GridScrubController;

class ImportProgressDialog;
class ProjectFolderWatcher;
class EverythingFolderModel;
class ProjectsModel;
class ProjectAssetsModel;
class ProjectItemDelegate;
class ProjectManagerWatcher;
class ProjectImportController;

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
    void onRatingChanged(int rating);

    // Live preview prefetch
    void onPrefetchLivePreviewsForFolder();
    void onRefreshLivePreviewsForFolder();
    void onPrefetchLivePreviewsRecursive();
    void onRefreshLivePreviewsRecursive();

    // Project Manager thumbnail generation
    void onPmPrefetchThumbnailsForFolder();
    void onPmPrefetchThumbnailsRecursive();
    void onPmRefreshThumbnailsForFolder();
    void onPmRefreshThumbnailsRecursive();
    void onPmPrefetchThumbnailsForProject(bool forceRefresh = false);

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

    // Database health
    void showDatabaseHealthDialog();
    void performStartupHealthCheck();

    // Main tabs
    void onTabChanged(int index);

    // Project Manager slots
    void onPmProjectSelected(const QModelIndex &index);
    void onPmFolderSelected(const QModelIndex &index);
    void onPmAssetSelectionChanged();
    void onPmAssetDoubleClicked(const QModelIndex &index);
    void onPmAssetContextMenu(const QPoint &pos);
    void onPmProjectContextMenu(const QPoint &pos);
    void onPmFolderContextMenu(const QPoint &pos);
    void onPmCreateProject();
    void pmImportToProject(const QString& name, const QString& watchPath);
    void onPmRenameProject();
    void onPmDeleteProject();
    void onPmAddWatchFolder();
    void onPmViewModeToggled();
    void onPmThumbnailSizeChanged(int size);
    void onPmToggleShowAllVersions(bool checked);
    void onPmVersionSelected(qint64 assetId, const QString &versionPath);
    void onPmVersionDropdownRequested(const QModelIndex &index, const QPoint &globalPos);
    void onPmRefresh();
    void onPmMarkNotificationsRead();
    void onPmShowNotifications();
    void onPmNewFilesDetected(int projectId, const QStringList &newFiles);
    void onPmFilesRemoved(int projectId, const QStringList &removedFiles);
    void onPmOpenOverlay();
    void changePmPreview(int delta);
    void onPmNavigateBack();
    void onPmNavigateUp();
    void onPmCopy();
    void onPmCut();
    void onPmPaste();
    void onPmDelete();
    void onPmRename();
    void onPmNewFolder();
    void onPmOpenExternal();
    void onPmTogglePreview(bool checked);
    void onPmGroupSequencesToggled(bool checked);

    // Help menu
    void onShowUserGuide();
    void onAboutKAssetManager();

    // Asset Manager navigation toolbar
    void onAssetNavigateBack();
    void onAssetNavigateUp();
    void onAssetNewFolder();
    void onAssetGroupSequencesToggled(bool checked);

    // Asset Manager folder model lifecycle hooks
    void onAssetFoldersModelAboutToReset();
    void onAssetFoldersModelReset();

    // File Manager slots
    void onFmTreeCurrentChanged(const QModelIndex &current, const QModelIndex &previous);
    void onFmTreeActivated(const QModelIndex &index);
    void onFmTreeContextMenu(const QPoint &pos);
    void onFmItemDoubleClicked(const QModelIndex &index);
    void onFmViewModeToggled();
    void onFmThumbnailSizeChanged(int size);
    void onAddSelectionToAssetLibrary();
    void onAddTreeSelectionToAssetLibrary();
    void onFmAddToFavorites();
    void onFmRemoveFavorite();
    void onFmFavoriteActivated(QListWidgetItem* item);
    void onFmNavigateBack();
    void onFmNavigateUp();

    // File operations
    void onFmCopy();
    void onFmCut();
    void onFmPaste();
    void onFmDelete();
    void onFmDeletePermanent();
    void onFmRename();
    void onFmBulkRename();
    void onFmNewFolder();
    void onFmCreateFolderWithSelected();
    void onFmShowContextMenu(const QPoint &pos);
    void onFmBackToParent();
    void onFmRefresh();
    void onFmLightRefresh();
    void onFmGroupSequencesToggled(bool checked);
    void onFmHideFoldersToggled(bool checked);

    // Everything Search
    void onEverythingSearchAssetManager();
    void onEverythingSearchFileManager();
    void onEverythingImportRequested(const QStringList& paths);

    // File Manager preview
    void onFmSelectionChanged();
    void onFmTogglePreview(); // toolbar toggle
    void onFmOpenOverlay();   // Space: toggle full-screen overlay
    void changeFmPreview(int delta); // Navigate in File Manager overlay
    void applyTheme(); // Apply current theme to all UI elements
    void onFmTreeChildrenFetched(const QModelIndex &parent); // Handle async tree fetch completion

private:
    QString fmPathForIndex(const QModelIndex& idx) const;
    QModelIndex fmIndexForPath(const QString& path);
    void fmRefreshTreeModel();
    void releaseAnyPreviewLocksForPaths(const QStringList& paths);
    void importToAssetLibrary(const QStringList& filePaths, const QStringList& folderPaths);
    void updateFmInfoPanel();
    void fmNavigateToPath(const QString& path, bool addToHistory = true);
    void fmUpdateNavigationButtons();
    void fmScrollTreeToPath(const QString& path);
    void amUpdateNavigationButtons();
    void navigateToFolder(int folderId, bool addToHistory = true);
    void setSequenceGroupingEnabled(bool enabled);
    void setAssetManagerSequenceGroupingEnabled(bool enabled);


protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private:
    bool m_initializing = false; // guard for eventFilter during UI construction
    bool m_windowResizing = false; // guard to skip heavy updates during resize/move
    QTimer m_resizeSettleTimer; // fires after resize/move stops
    QTimer m_splitterSaveTimer; // debounces splitter state persistence

    void setupUi();
    void setupConnections();
    void setupFileManagerUi();
    void setupProjectManagerUi();
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

    // Saved state for folder tree during model reset
    int savedTreeScrollPosition = 0;
    QList<int> savedSelectedFolderIds;
    int pendingSelectFolderIdAfterReload = -1;

    // Sequence helper
    QStringList reconstructSequenceFramePaths(const QString& firstFramePath, int startFrame, int endFrame);

    // Tabs
    QTabWidget *mainTabs;
    QWidget *assetManagerPage;
    QWidget *fileManagerPage;
    QWidget *projectManagerPage;


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
    class AssetSequenceGroupingProxyModel *amProxyModel = nullptr;

    // Right panel: Filters + Info
    QWidget *rightPanel;
    QWidget *filtersPanel;
    QWidget *infoPanel;

    // Importer
    class Importer *importer;

    // Filters
    QLineEdit *searchBox;
    QPushButton *filterSearchButton = nullptr;
    QComboBox *ratingFilter;
    QListView *tagsListView;
    TagsModel *tagsModel;
    QPushButton *applyTagsBtn;
    QPushButton *filterByTagsBtn;
    QToolButton *thumbGenButton; // Toolbar button: Generate thumbnails

    QComboBox *tagFilterModeCombo;

    // View controls
    QSlider *thumbnailSizeSlider;
    QToolButton *viewModeButton;
    bool isGridMode;
    class QCheckBox *lockCheckBox;
    class QCheckBox *recursiveCheckBox;
    // Asset Manager navigation toolbar controls
    QWidget *amToolbar = nullptr;
    QToolButton *amBackButton = nullptr;
    QToolButton *amUpButton = nullptr;
    QToolButton *amNewFolderButton = nullptr;
    QToolButton *amGroupSequencesButton = nullptr;
    QList<int> amNavigationHistory;
    int amNavigationIndex = -1;

    // Asset Manager toolbar labels for theme updates
    QLabel *amSizeLabel = nullptr;
    QLabel *amSizeValueLabel = nullptr;

    class QCheckBox *searchEntireDbCheckBox;
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
    GridScrubController *assetScrubController = nullptr;
    GridScrubController *fmScrubController = nullptr;

    // Thumbnail generation progress
    QLabel *thumbnailProgressLabel;
    class QProgressBar *thumbnailProgressBar;
    QTimer visibleThumbTimer;
    QHash<QString, QPixmap> versionPreviewCache;


    // Debounce for folder selection in Asset Manager
    QTimer folderSelectTimer;
    int pendingFolderId = -1;

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
    QToolButton *fmGroupSequencesCheckBox = nullptr;
    bool fmGroupSequences = true;
    // Hide folders in grid view
    QToolButton *fmHideFoldersCheckBox = nullptr;
    bool fmHideFolders = false;

    QSplitter *fmLeftSplitter;   // Favorites | Folder tree
    QSplitter *fmRightSplitter;  // Views | Preview+Info panels
    QSplitter *fmPreviewInfoSplitter; // Preview | Info (vertical)
    // Left pane
    QListWidget *fmFavoritesList;
    QTreeView *fmTree;
    QFileSystemModel *fmTreeModel = nullptr;
    EverythingFolderModel *fmEverythingTreeModel = nullptr;
    // Right pane
    QFileSystemModel *fmDirModel;
    bool fmSuppressTreeSync = false;
    QWidget *fmToolbar;
    QLineEdit *fmPathBar = nullptr;  // Editable path bar like Windows Explorer
    QToolButton *fmBackButton;
    QToolButton *fmUpButton;
    QToolButton *fmViewModeButton;
    class QSlider *fmThumbnailSizeSlider;
    QToolButton *fmPreviewToggleButton;

    // File Manager toolbar buttons for theme updates
    QToolButton *fmNewFolderBtn = nullptr;
    QToolButton *fmCopyBtn = nullptr;
    QToolButton *fmCutBtn = nullptr;
    QToolButton *fmPasteBtn = nullptr;
    QToolButton *fmDeleteBtn = nullptr;
    QToolButton *fmRenameBtn = nullptr;
    QToolButton *fmAddToLibraryBtn = nullptr;
    QToolButton *fmSearchButton = nullptr;
    QLabel *fmSizeLabel = nullptr;
    class QStackedWidget *fmViewStack;
    class FmGridViewEx *fmGridView;
    class FmListViewEx *fmListView;
    bool fmIsGridMode;

    // Auto-refresh watchers for File Manager
    QFileSystemWatcher *fmDirectoryWatcher = nullptr;
    QTimer fmDirChangeTimer;
    
    // Navigation debouncing and async tree scrolling
    QTimer fmNavigationDebounceTimer;
    QString fmPendingNavigationPath;
    QString fmPendingTreeScrollPath;  // Path waiting for async fetch to complete

    // Navigation history
    QStringList fmNavigationHistory;
    int fmNavigationIndex = -1;

    // Favorites persistence
    QStringList fmFavorites;
    void loadFmFavorites();
    void saveFmFavorites();

    // Preview panel (embedded, right side)
    QWidget *fmPreviewPanel = nullptr;
    class QGraphicsView *fmImageView = nullptr;
    class QGraphicsScene *fmImageScene = nullptr;
    class QGraphicsPixmapItem *fmImageItem = nullptr;
    QWidget *fmVideoWidget = nullptr; // Changed from QVideoWidget to QWidget for GStreamer
    // Additional preview widgets
    class QPlainTextEdit *fmTextView = nullptr;           // TXT/LOG
    class QTableView *fmCsvView = nullptr;                // CSV table
    class QStandardItemModel *fmCsvModel = nullptr;
    class QPdfDocument *fmPdfDoc = nullptr;               // PDF (core)
    class QPdfView *fmPdfView = nullptr;                 // Optional (QtPdfWidgets)
    int fmPdfCurrentPage = 0;                            // Fallback navigation when PdfWidgets missing
    class QToolButton *fmPdfPrevBtn = nullptr; class QToolButton *fmPdfNextBtn = nullptr; QLabel *fmPdfPageLabel = nullptr;
    class QGraphicsView *fmSvgView = nullptr; class QGraphicsScene *fmSvgScene = nullptr; class QGraphicsItem *fmSvgItem = nullptr;
    QCheckBox *fmAlphaCheck = nullptr;          // Alpha toggle for images
    // State for image/alpha
    bool fmImageFitToView = true; // auto fit image to view and refit on resize until user zooms manually
    QImage fmOriginalImage; QString fmCurrentPreviewPath; bool fmPreviewHasAlpha = false; bool fmAlphaOnlyMode = false;
    QPoint fmPreviewDragStartPos; bool fmPreviewDragPending = false;
    // Media - GStreamer player
    class GStreamerPlayer *fmGStreamerPlayer = nullptr;
    QPushButton *fmPlayPauseBtn;
    QPushButton *fmPrevFrameBtn = nullptr;
    QPushButton *fmNextFrameBtn = nullptr;
    // Shortcuts management for File Manager
    QHash<QString, class QShortcut*> fmShortcutObjs;
    void applyFmShortcuts();
    static QKeySequence fmShortcutFor(const QString& actionName, const QKeySequence& def);

    // Sequence preview helpers (File Manager)
    void loadFmSequenceFrame(int index);
    void playFmSequence();
    void pauseFmSequence();
    void stepFmSequence(int delta);

    // Helpers for tree/context operations
    QStringList getSelectedFmTreePaths() const;
    void onFmPasteInto(const QString& destDir);
    void doPermanentDelete(const QStringList& paths);

    QSlider *fmPositionSlider;
    QLabel *fmTimeLabel;
    QSlider *fmVolumeSlider;

    // Sequence playback state for File Manager preview
    bool fmIsSequence = false;
    QStringList fmSequenceFramePaths;
    int fmSequenceStartFrame = 0;
    int fmSequenceEndFrame = 0;
    int fmSequenceCurrentIndex = 0;
    class QTimer *fmSequenceTimer = nullptr;
    bool fmSequencePlaying = false;
    double fmSequenceFps = 24.0;

    QPushButton *fmMuteBtn;


    // Info panel (embedded, right side)
    QWidget *fmInfoPanel = nullptr;
    QLabel *fmInfoFileName = nullptr;
    QLabel *fmInfoFilePath = nullptr;
    QLabel *fmInfoFileSize = nullptr;
    QLabel *fmInfoFileType = nullptr;
    QLabel *fmInfoDimensions = nullptr;
    QLabel *fmInfoCreated = nullptr;
    QLabel *fmInfoModified = nullptr;
    QLabel *fmInfoPermissions = nullptr;

    // File operations state
    QStringList fmClipboard;
    bool fmClipboardCutMode = false;
    class FileOpsProgressDialog *fileOpsDialog;

    // Overlay navigation context for File Manager
    QPersistentModelIndex fmOverlayCurrentIndex; QAbstractItemView* fmOverlaySourceView = nullptr; // grid or list

    // Helpers
    void updateFmPreviewForIndex(const QModelIndex &idx);
    void clearFmPreview();

    // Project Manager members - mirrors File Manager structure
    QSplitter *pmSplitter = nullptr;
    QSplitter *pmRightSplitter = nullptr;
    QSplitter *pmLeftSplitter = nullptr;
    QSplitter *pmPreviewInfoSplitter = nullptr;
    
    // Project list
    QListView *pmProjectsListView = nullptr;
    class ProjectsModel *pmProjectsModel = nullptr;
    
    // Folder tree for selected project
    class QTreeView *pmFolderTree = nullptr;
    class ProjectFoldersModel *pmFoldersModel = nullptr;
    int pmCurrentFolderId = -1;
    QList<int> pmFolderHistory;
    int pmFolderHistoryIndex = -1;
    
    // Assets views
    QListView *pmAssetsGridView = nullptr;
    class QTableView *pmAssetsTableView = nullptr;
    class QStackedWidget *pmViewStack = nullptr;
    class ProjectAssetsModel *pmAssetsModel = nullptr;
    class ProjectSequenceGroupingProxyModel *pmSequenceProxy = nullptr;
    class ProjectItemDelegate *pmItemDelegate = nullptr;
    
    // Toolbar and buttons (mirrors FM)
    QWidget *pmToolbar = nullptr;
    QToolButton *pmBackButton = nullptr;
    QToolButton *pmUpButton = nullptr;
    QToolButton *pmNewFolderBtn = nullptr;
    QToolButton *pmCopyBtn = nullptr;
    QToolButton *pmCutBtn = nullptr;
    QToolButton *pmPasteBtn = nullptr;
    QToolButton *pmDeleteBtn = nullptr;
    QToolButton *pmRenameBtn = nullptr;
    QToolButton *pmOpenExternalBtn = nullptr;
    QToolButton *pmViewModeButton = nullptr;
    class QSlider *pmThumbnailSizeSlider = nullptr;
    QLabel *pmSizeLabel = nullptr;
    QToolButton *pmRefreshButton = nullptr;
    QToolButton *pmThumbGenButton = nullptr;
    QToolButton *pmShowAllVersionsButton = nullptr;
    QToolButton *pmPreviewToggleButton = nullptr;
    QToolButton *pmGroupSequencesBtn = nullptr;
    
    bool pmIsGridMode = true;
    int pmCurrentProjectId = -1;
    QString pmCurrentPreviewPath;  // Track current preview for async updates
    QStringList pmClipboard;
    bool pmClipboardCutMode = false;
    QHash<qint64, QString> pmSelectedVersions;  // asset ID -> selected version string
    
    // Project Manager preview panel (full FM-style)
    QWidget *pmPreviewPanel = nullptr;
    class QGraphicsView *pmImageView = nullptr;
    class QGraphicsScene *pmImageScene = nullptr;
    class QGraphicsPixmapItem *pmImageItem = nullptr;
    QWidget *pmVideoWidget = nullptr;
    class GStreamerPlayer *pmGStreamerPlayer = nullptr;
    
    // Media controls
    QPushButton *pmPlayPauseBtn = nullptr;
    QPushButton *pmPrevFrameBtn = nullptr;
    QPushButton *pmNextFrameBtn = nullptr;
    class QSlider *pmPositionSlider = nullptr;
    QLabel *pmTimeLabel = nullptr;
    class QSlider *pmVolumeSlider = nullptr;
    QPushButton *pmMuteBtn = nullptr;
    
    // PM sequence playback state
    QTimer *pmSequenceTimer = nullptr;
    bool pmIsSequence = false;
    bool pmSequencePlaying = false;
    QStringList pmSequenceFramePaths;
    int pmSequenceCurrentIndex = 0;
    
    // PM scrub controller
    GridScrubController *pmScrubController = nullptr;
    
    // Project Manager info panel (full FM-style with metadata)
    QWidget *pmInfoPanel = nullptr;
    QLabel *pmInfoFileName = nullptr;
    QLabel *pmInfoFilePath = nullptr;
    QLabel *pmInfoFileSize = nullptr;
    QLabel *pmInfoFileType = nullptr;
    QLabel *pmInfoDimensions = nullptr;
    QLabel *pmInfoCreated = nullptr;
    QLabel *pmInfoModified = nullptr;
    QLabel *pmInfoPermissions = nullptr;
    QLabel *pmInfoVersions = nullptr;
    
    // Notification badge
    class QPushButton *pmNotificationBadge = nullptr;
    int pmUnreadNotificationCount = 0;
    ProjectManagerWatcher *pmWatcher = nullptr;
    
    // PM import - uses Importer with ProjectDB (same as Asset Manager)
    Importer *pmImporter = nullptr;
    ImportProgressDialog *pmImportProgressDialog = nullptr;
    int pmPendingImportProjectId = -1;
    
    // PM helper functions (mirrors FM)
    void updatePmNotificationBadge();
    void updatePmInfoPanel();
    void updatePmPreviewForIndex(const QModelIndex &idx);
    void clearPmPreview();
    void pmNavigateToFolder(int folderId);
    void pmNavigateBack();
    void pmNavigateUp();
    QStringList getSelectedPmAssetPaths() const;
    void restoreProjectManagerState();
    void saveProjectManagerState(QSettings& s);
    QModelIndex pmIndexForProjectId(int projectId) const;
    void navigateToProjectAsset(int projectId, int assetId, const QString& filePath);
    void onPmImportProgress(int current, int total);
    void onPmImportFileChanged(const QString& fileName);
    void onPmImportFolderChanged(const QString& folderName);
    void onPmImportFinished();
    
    // PM sequence/video playback helpers
    void playPmSequence();
    void pausePmSequence();
    void stepPmSequence(int delta);
    void loadPmSequenceFrame(int index);
    void showPmVideo(const QString &filePath);
    void showPmImage(const QString &filePath);
    void showPmSequence(const QStringList &framePaths);
};

#endif // MAINWINDOW_H
