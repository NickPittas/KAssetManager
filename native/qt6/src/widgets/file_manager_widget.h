#pragma once

#include <QWidget>
#include <QAbstractItemView>
#include <QAudioOutput>
#include <QCheckBox>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPdfDocument>
#include <QPdfView>
#include <QPlainTextEdit>
#include <QPointer>
#include <QModelIndex>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QMediaPlayer>
#include <QSettings>
#include <QComboBox>
#include <QGraphicsVideoItem>
#include <QVideoSink>


#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QUrl>
#include <QDesktopServices>
#include <QVideoWidget>
#include "ui/file_type_helpers.h"
#include "ui/icon_helpers.h"
#include <QBoxLayout>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QDesktopServices>
#include <QUrl>
#include "mainwindow.h"
#include "widgets/sequence_grouping_proxy_model.h"
#include "widgets/fm_drag_views.h"
#include "preview_overlay.h"
#include "log_manager.h"


#include <QHash>
#include <QPersistentModelIndex>
#include <QSet>

class QListWidgetItem;
class QFileSystemModel;
class QFileSystemWatcher;
class QListWidget;
class QTreeView;
class QToolButton;
class QSlider;
class QStackedWidget;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QVideoWidget;
class QPlainTextEdit;
class QTableView;
class QStandardItemModel;
class QPdfDocument;
class QPdfView;
class QGraphicsItem;
class QCheckBox;
class QMediaPlayer;
class QAudioOutput;
class QPushButton;
class QLabel;
class QShortcut;
class QComboBox;
class QGraphicsVideoItem;
class FileOpsProgressDialog;
class SequenceGroupingProxyModel;
class FmGridViewEx;
class FmListViewEx;
class PreviewOverlay;
class ImportProgressDialog;
class MainWindow;
class QThread;
class FfmpegVideoReader;


class FileManagerWidget : public QWidget
{
    Q_OBJECT
    friend class MainWindow;


public:
    explicit FileManagerWidget(MainWindow* host, QWidget* parent = nullptr);
    ~FileManagerWidget();
    void ensurePreviewInfoLayout();

    void reapplyShortcutsFromSettings();


signals:
    void navigateToPathRequested(const QString& path, bool addToHistory);

private:
    void setupUi();
    void bindHostPointers();
    void navigateToPath(const QString& path, bool addToHistory);
    void scrollTreeToPath(const QString& path);
    void updateNavigationButtons();


	    void setupShortcuts();
	    void applyFmShortcuts();
	    bool shouldIgnoreShortcutFromFocus() const;
	    void releaseAnyPreviewLocksForPaths(const QStringList& paths);
    // FM preview/info helpers moved from MainWindow
    void clearFmPreview();
    void updateFmPreviewForIndex(const QModelIndex& index);
    void loadFmSequenceFrame(int index);
    void playFmSequence();
    void pauseFmSequence();
    void stepFmSequence(int delta);
    void updateFmInfoPanel();
    void changeFmPreview(int delta);
#ifdef HAVE_FFMPEG
    void startFmFallbackVideo(const QString& path);
    void stopFmFallbackVideo();
    void onFmFallbackFrameReady(const QImage &image, qint64 ptsMs);
    void onFmFallbackFinished();
#endif


    MainWindow* m_host = nullptr;

public:
    QSplitter *fmSplitter = nullptr;
    SequenceGroupingProxyModel *fmProxyModel = nullptr;
    QToolButton *fmGroupSequencesCheckBox = nullptr;
    bool fmGroupSequences = true;
    QToolButton *fmHideFoldersCheckBox = nullptr;
    bool fmHideFolders = false;

    QSplitter *fmLeftSplitter = nullptr;
    QSplitter *fmRightSplitter = nullptr;
    QSplitter *fmPreviewInfoSplitter = nullptr;
    QListWidget *fmFavoritesList = nullptr;
    QTreeView *fmTree = nullptr;
    QFileSystemModel *fmTreeModel = nullptr;
    QFileSystemModel *fmDirModel = nullptr;
    QWidget *fmToolbar = nullptr;
    QToolButton *fmBackButton = nullptr;
    QToolButton *fmUpButton = nullptr;
    QToolButton *fmViewModeButton = nullptr;
    QSlider *fmThumbnailSizeSlider = nullptr;
    QToolButton *fmPreviewToggleButton = nullptr;
    QStackedWidget *fmViewStack = nullptr;
    FmGridViewEx *fmGridView = nullptr;
    FmListViewEx *fmListView = nullptr;
    bool fmIsGridMode = true;

    QFileSystemWatcher *fmDirectoryWatcher = nullptr;

    QStringList fmNavigationHistory;
    int fmNavigationIndex = -1;

    QStringList fmFavorites;

    QWidget *fmPreviewPanel = nullptr;
    QGraphicsView *fmImageView = nullptr;
    QGraphicsScene *fmImageScene = nullptr;
    QGraphicsPixmapItem *fmImageItem = nullptr;
    QGraphicsVideoItem *fmVideoItem = nullptr; // video inside graphics view for zoom/pan (legacy)
    QVideoWidget *fmVideoWidget = nullptr; // legacy fallback (hidden)
    QVideoSink *fmVideoSink = nullptr; // modern video frame sink for color processing
    QImage fmLastVideoFrameRaw; // last raw video frame for re-render on color change
    QSize fmLastVideoPixmapSize; // track last displayed pixmap size to avoid refitting each frame
#ifdef HAVE_FFMPEG
    // FFmpeg fallback state
    bool fmUsingFallbackVideo = false;
    FfmpegVideoReader* fmFallbackReader = nullptr;
    QThread* fmFallbackThread = nullptr;
    qint64 fmFallbackDurationMs = 0;
    double fmFallbackFps = 0.0;
    bool fmFallbackPaused = false;
#endif
    QPlainTextEdit *fmTextView = nullptr;
    QTableView *fmCsvView = nullptr;
    QStandardItemModel *fmCsvModel = nullptr;
    QPdfDocument *fmPdfDoc = nullptr;
    QPdfView *fmPdfView = nullptr;
    int fmPdfCurrentPage = 0;
    QToolButton *fmPdfPrevBtn = nullptr;
    QToolButton *fmPdfNextBtn = nullptr;
    QLabel *fmPdfPageLabel = nullptr;
    QGraphicsView *fmSvgView = nullptr;
    QGraphicsScene *fmSvgScene = nullptr;
    QGraphicsItem *fmSvgItem = nullptr;
    QCheckBox *fmAlphaCheck = nullptr;
    bool fmImageFitToView = true;
    QImage fmOriginalImage;
    QString fmCurrentPreviewPath;
    bool fmPreviewHasAlpha = false;
    bool fmAlphaOnlyMode = false;
    QPoint fmPreviewDragStartPos;
    bool fmPreviewDragPending = false;
    QMediaPlayer *fmMediaPlayer = nullptr;
    QAudioOutput *fmAudioOutput = nullptr;
    QPushButton *fmPlayPauseBtn = nullptr;
    QPushButton *fmPrevFrameBtn = nullptr;
    QPushButton *fmNextFrameBtn = nullptr;
    // Color space controls (HDR sequences)
    QLabel *fmColorSpaceLabel = nullptr;
    QComboBox *fmColorSpaceCombo = nullptr;
    QHash<QString, QShortcut*> fmShortcutObjs;

    QSlider *fmPositionSlider = nullptr;
    QLabel *fmTimeLabel = nullptr;
    QSlider *fmVolumeSlider = nullptr;

    bool fmIsSequence = false;
    QStringList fmSequenceFramePaths;
    int fmSequenceStartFrame = 0;
    int fmSequenceEndFrame = 0;
    int fmSequenceCurrentIndex = 0;
    QTimer *fmSequenceTimer = nullptr;
    bool fmSequencePlaying = false;
    double fmSequenceFps = 24.0;
    bool fmWasPlayingBeforeSeek = false;
    int fmSeqLoadEpoch = 0;
    QThread* fmSeqWorkerThread = nullptr;
    bool fmSeqHasPending = false;
    int fmSeqPendingIndex = -1;

    QPushButton *fmMuteBtn = nullptr;

    QWidget *fmInfoPanel = nullptr;
    QLabel *fmInfoFileName = nullptr;
    QLabel *fmInfoFilePath = nullptr;
    QLabel *fmInfoFileSize = nullptr;
    QLabel *fmInfoFileType = nullptr;
    QLabel *fmInfoDimensions = nullptr;
    QLabel *fmInfoCreated = nullptr;
    QLabel *fmInfoModified = nullptr;
    QLabel *fmInfoPermissions = nullptr;

    QStringList fmClipboard;
    bool fmClipboardCutMode = false;
FileOpsProgressDialog *fileOpsDialog = nullptr;

    QPersistentModelIndex fmOverlayCurrentIndex;
    QPointer<QAbstractItemView> fmOverlaySourceView;

private slots:
    void onFmTreeActivated(const QModelIndex &index);
    void onFmViewModeToggled();
    void onFmThumbnailSizeChanged(int size);
    void onFmGroupSequencesToggled(bool checked);
    void onFmHideFoldersToggled(bool checked);
    void onFmRemoveFavorite();
    void onFmFavoriteActivated(QListWidgetItem* item);

    void onFmTreeContextMenu(const QPoint &pos);
    void onFmShowContextMenu(const QPoint &pos);

public slots:
    void onFmAddToFavorites();
    void onFmSelectionChanged();
    void onFmTogglePreview(bool checked);
    void onFmOpenOverlay();

    void onFmItemDoubleClicked(const QModelIndex &index);

    void onFmRefresh();
    void onFmNewFolder();
    void onFmRename();
    void onFmCopy();
    void onFmCut();
    void onFmPaste();
    void onFmDelete();
    void onFmDeletePermanent();
    void onFmCreateFolderWithSelected();
    void onFmBulkRename();
    void onFmNavigateBack();
    void onFmNavigateUp();
};






