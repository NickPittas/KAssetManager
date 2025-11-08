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
class FileOpsProgressDialog;
class SequenceGroupingProxyModel;
class FmGridViewEx;
class FmListViewEx;
class PreviewOverlay;
class ImportProgressDialog;
class MainWindow;

class FileManagerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FileManagerWidget(MainWindow* host, QWidget* parent = nullptr);
    ~FileManagerWidget();

signals:
    void navigateToPathRequested(const QString& path, bool addToHistory);

private:
    void setupUi();
    void bindHostPointers();

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
    QVideoWidget *fmVideoWidget = nullptr;
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

public slots:
    void onFmAddToFavorites();
    void onFmRefresh();
    void onFmNewFolder();
    void onFmRename();
    void onFmCopy();
    void onFmCut();
    void onFmPaste();
    void onFmDelete();
    void onFmDeletePermanent();
    void onFmCreateFolderWithSelected();
};


