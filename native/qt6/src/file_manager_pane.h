#ifndef FILE_MANAGER_PANE_H
#define FILE_MANAGER_PANE_H

#include <QWidget>
#include <QFileSystemModel>
#include <QStackedWidget>
#include <QListView>
#include <QTableView>
#include <QLineEdit>
#include <QToolButton>
#include <QSlider>
#include <QLabel>
#include <QSplitter>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QPlainTextEdit>
#include <QStandardItemModel>
#include <QTimer>
#include <QCheckBox>
#include <QPushButton>

class QSettings;
class SequenceGroupingProxyModel;
class FmGridViewEx;
class FmListViewEx;
class GridScrubController;
class GStreamerPlayer;

#if defined(HAVE_QT_PDF)
class QPdfDocument;
#endif
#if defined(HAVE_QT_PDF_WIDGETS)
class QPdfView;
#endif

/**
 * @brief FileManagerPane encapsulates a single file browser pane.
 * 
 * This class contains all the state and widgets needed for one pane of the
 * dual-pane File Manager: directory model, views (grid/list), toolbar, 
 * preview panel, navigation history, etc.
 * 
 * Multiple instances can be created to support dual-pane navigation.
 */
class FileManagerPane : public QWidget
{
    Q_OBJECT

public:
    explicit FileManagerPane(QWidget *parent = nullptr);
    ~FileManagerPane();

    // Navigation
    void navigateToPath(const QString &path, bool addToHistory = true);
    QString currentPath() const;
    void navigateBack();
    void navigateUp();
    void refresh();

    // Selection
    QStringList selectedPaths() const;
    QModelIndex currentIndex() const;
    void setCurrentIndex(const QModelIndex &index);
    QString pathForIndex(const QModelIndex &index) const;
    void clearSelection();

    // View mode
    bool isGridMode() const { return m_isGridMode; }
    void setGridMode(bool grid);
    void setThumbnailSize(int size);
    int thumbnailSize() const;

    // Sequence grouping
    void setSequenceGroupingEnabled(bool enabled);
    bool isSequenceGroupingEnabled() const { return m_groupSequences; }

    // Hide folders option
    void setHideFolders(bool hide);
    bool hideFolders() const { return m_hideFolders; }

    // Preview panel visibility
    void setPreviewVisible(bool visible);
    bool isPreviewVisible() const;

    // Active state (visual highlight when this pane has focus)
    void setActive(bool active);
    bool isActive() const { return m_isActive; }

    // Models access (for external connections like tree view)
    QFileSystemModel *dirModel() const { return m_dirModel; }
    SequenceGroupingProxyModel *proxyModel() const { return m_proxyModel; }
    FmGridViewEx *gridView() const { return m_gridView; }
    FmListViewEx *listView() const { return m_listView; }
    QStackedWidget *viewStack() const { return m_viewStack; }

    // Preview update
    void updatePreviewForIndex(const QModelIndex &idx);
    void clearPreview();

    // State save/restore
    void saveState(QSettings &settings, const QString &prefix);
    void restoreState(QSettings &settings, const QString &prefix);

    // Clipboard operations
    void copySelectedToClipboard();
    void cutSelectedToClipboard();
    void pasteFromClipboard();
    void setClipboard(const QStringList &paths, bool isCut);
    QStringList clipboardPaths() const { return m_clipboardPaths; }
    bool isClipboardCutMode() const { return m_clipboardCutMode; }

signals:
    void pathChanged(const QString &path);
    void selectionChanged();
    void activated();  // Emitted when pane receives focus/click
    void fileDoubleClicked(const QString &path);
    void contextMenuRequested(const QPoint &globalPos);
    void filesDropped(const QStringList &paths, const QString &targetDir);  // Emitted when files dropped

protected:
    void focusInEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onItemDoubleClicked(const QModelIndex &index);
    void onSelectionChanged();
    void onViewModeToggled();
    void onThumbnailSizeChanged(int size);
    void onGroupSequencesToggled(bool checked);
    void onHideFoldersToggled(bool checked);
    void onPreviewToggled(bool checked);

private:
    void setupUi();
    void setupToolbar();
    void setupViews();
    void setupPreviewPanel();
    void setupConnections();
    void updateNavigationButtons();
    void applyActiveStyle();

    // Sequence playback helpers
    void loadSequenceFrame(int index);
    void playSequence();
    void pauseSequence();
    void stepSequence(int delta);

    // Models
    QFileSystemModel *m_dirModel = nullptr;
    SequenceGroupingProxyModel *m_proxyModel = nullptr;

    // Main layout
    QSplitter *m_mainSplitter = nullptr;  // Views | Preview+Info

    // Toolbar
    QWidget *m_toolbar = nullptr;
    QLineEdit *m_pathBar = nullptr;
    QToolButton *m_backButton = nullptr;
    QToolButton *m_upButton = nullptr;
    QToolButton *m_viewModeButton = nullptr;
    QSlider *m_thumbnailSizeSlider = nullptr;
    QLabel *m_sizeLabel = nullptr;
    QToolButton *m_groupSequencesButton = nullptr;
    QToolButton *m_hideFoldersButton = nullptr;
    QToolButton *m_previewToggleButton = nullptr;

    // Views
    QStackedWidget *m_viewStack = nullptr;
    FmGridViewEx *m_gridView = nullptr;
    FmListViewEx *m_listView = nullptr;
    GridScrubController *m_scrubController = nullptr;
    bool m_isGridMode = true;

    // Preview panel
    QSplitter *m_previewInfoSplitter = nullptr;
    QWidget *m_previewPanel = nullptr;
    QGraphicsView *m_imageView = nullptr;
    QGraphicsScene *m_imageScene = nullptr;
    QGraphicsPixmapItem *m_imageItem = nullptr;
    QWidget *m_videoWidget = nullptr;
    GStreamerPlayer *m_gstreamerPlayer = nullptr;
    QPlainTextEdit *m_textView = nullptr;
    QTableView *m_csvView = nullptr;
    QStandardItemModel *m_csvModel = nullptr;
#if defined(HAVE_QT_PDF)
    QPdfDocument *m_pdfDoc = nullptr;
#endif
#if defined(HAVE_QT_PDF_WIDGETS)
    QPdfView *m_pdfView = nullptr;
#endif
    int m_pdfCurrentPage = 0;
    QToolButton *m_pdfPrevBtn = nullptr;
    QToolButton *m_pdfNextBtn = nullptr;
    QLabel *m_pdfPageLabel = nullptr;
    QGraphicsView *m_svgView = nullptr;
    QGraphicsScene *m_svgScene = nullptr;
    QGraphicsItem *m_svgItem = nullptr;
    QCheckBox *m_alphaCheck = nullptr;
    bool m_imageFitToView = true;
    QImage m_originalImage;
    QString m_currentPreviewPath;
    bool m_previewHasAlpha = false;
    bool m_alphaOnlyMode = false;

    // Media controls
    QPushButton *m_playPauseBtn = nullptr;
    QPushButton *m_prevFrameBtn = nullptr;
    QPushButton *m_nextFrameBtn = nullptr;
    QSlider *m_positionSlider = nullptr;
    QLabel *m_timeLabel = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QPushButton *m_muteBtn = nullptr;

    // Sequence playback
    bool m_isSequence = false;
    QStringList m_sequenceFramePaths;
    int m_sequenceStartFrame = 0;
    int m_sequenceEndFrame = 0;
    int m_sequenceCurrentIndex = 0;
    QTimer *m_sequenceTimer = nullptr;
    bool m_sequencePlaying = false;
    double m_sequenceFps = 24.0;

    // Info panel
    QWidget *m_infoPanel = nullptr;
    QLabel *m_infoFileName = nullptr;
    QLabel *m_infoFilePath = nullptr;
    QLabel *m_infoFileSize = nullptr;
    QLabel *m_infoFileType = nullptr;
    QLabel *m_infoDimensions = nullptr;
    QLabel *m_infoCreated = nullptr;
    QLabel *m_infoModified = nullptr;
    QLabel *m_infoPermissions = nullptr;

    // Navigation history
    QStringList m_navigationHistory;
    int m_navigationIndex = -1;

    // State
    bool m_groupSequences = true;
    bool m_hideFolders = false;
    bool m_isActive = false;

    // Clipboard
    QStringList m_clipboardPaths;
    bool m_clipboardCutMode = false;

    // Overlay context
    QPersistentModelIndex m_overlayCurrentIndex;
    QAbstractItemView *m_overlaySourceView = nullptr;
};

#endif // FILE_MANAGER_PANE_H
