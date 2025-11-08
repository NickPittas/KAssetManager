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

class FileManagerWidget;
class VirtualFolderTreeModel;
class AssetsModel;
class TagsModel;
class PreviewOverlay;
class SequenceGroupingProxyModel;
class GridScrubController;

class ImportProgressDialog;
class ProjectFolderWatcher;

class MainWindow : public QMainWindow
{
    Q_OBJECT
    friend class FileManagerWidget;

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

private:
    QString fmPathForIndex(const QModelIndex& idx) const;
    void releaseAnyPreviewLocksForPaths(const QStringList& paths);
    void updateFmInfoPanel();
    void fmNavigateToPath(const QString& path, bool addToHistory = true);
    void fmUpdateNavigationButtons();
    void fmScrollTreeToPath(const QString& path);


protected:
    void closeEvent(QCloseEvent* event) override;

private:
    bool m_initializing = false; // guard for eventFilter during UI construction

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
    FileManagerWidget *fileManagerWidget = nullptr;

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
    QToolButton *thumbGenButton; // Toolbar button: Generate thumbnails

    QComboBox *tagFilterModeCombo;

    // View controls
    QSlider *thumbnailSizeSlider;
    QToolButton *viewModeButton;
    bool isGridMode;
    class QCheckBox *lockCheckBox;
    class QCheckBox *recursiveCheckBox;
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
    QFileSystemModel *fmTreeModel;
    // Right pane
    QFileSystemModel *fmDirModel;
    QWidget *fmToolbar;
    QToolButton *fmBackButton;
    QToolButton *fmUpButton;
    QToolButton *fmViewModeButton;
    class QSlider *fmThumbnailSizeSlider;
    QToolButton *fmPreviewToggleButton;
    class QStackedWidget *fmViewStack;
    class FmGridViewEx *fmGridView;
    class FmListViewEx *fmListView;
    bool fmIsGridMode;

    // Auto-refresh watchers for File Manager
    QFileSystemWatcher *fmDirectoryWatcher = nullptr;
    QTimer fmDirChangeTimer;

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
    class QVideoWidget *fmVideoWidget = nullptr;
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
    // Media
    class QMediaPlayer *fmMediaPlayer;
    class QAudioOutput *fmAudioOutput;
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
};

#endif // MAINWINDOW_H

