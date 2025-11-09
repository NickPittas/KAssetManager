#include "widgets/file_manager_widget.h"
#include "mainwindow.h"
#include "widgets/fm_icon_provider.h"
#include "widgets/fm_item_delegate.h"
#include "widgets/sequence_grouping_proxy_model.h"
#include "widgets/fm_drag_views.h"
#include "ui/icon_helpers.h"
#include "ui/file_type_helpers.h"
#include "log_manager.h"
#include "file_ops_dialog.h"
#include "file_ops.h"
#include "media_convert_dialog.h"
#include "bulk_rename_dialog.h"


#include <QApplication>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QTimer>
#include "widgets/grid_scrub_controller.h"
#include "live_preview_manager.h"

#include "drag_utils.h"

#include <QMenu>
#include <QSettings>
#include <QShortcut>
#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QClipboard>

static QString fmSettingsKey(const QString& name) { return QStringLiteral("FileManager/%1").arg(name); }

FileManagerWidget::FileManagerWidget(MainWindow* host, QWidget* parent)
    : QWidget(parent), m_host(host)
{
    LogManager::instance().addLog("[TRACE] FM: constructor begin", "DEBUG");
    setupUi();
    LogManager::instance().addLog("[TRACE] FM: constructor end", "DEBUG");
}

FileManagerWidget::~FileManagerWidget() {}

void FileManagerWidget::setupUi()
{
    LogManager::instance().addLog("[TRACE] FM: setupUi enter", "DEBUG");
    // Root splitter
    fmSplitter = new QSplitter(Qt::Horizontal, this);
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(fmSplitter);

    // Left splitter: Favorites | Tree
    QWidget* leftPanel = new QWidget(fmSplitter);
    auto leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0,0,0,0);
    leftLayout->setSpacing(0);

    fmLeftSplitter = new QSplitter(Qt::Vertical, leftPanel);
    fmFavoritesList = new QListWidget(fmLeftSplitter);
    fmTree = new QTreeView(fmLeftSplitter);
    fmLeftSplitter->setStretchFactor(0, 0);
    fmLeftSplitter->setStretchFactor(1, 1);
    leftLayout->addWidget(fmLeftSplitter);

    // Right side: Toolbar + View stack (preview/info is added by ensurePreviewInfoLayout)
    QWidget* rightPanel = new QWidget(fmSplitter);
    auto rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0,0,0,0);
    rightLayout->setSpacing(0);

    // Toolbar
    fmToolbar = new QWidget(rightPanel);
    fmToolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fmToolbar->setFixedHeight(28);
    auto tbLayout = new QHBoxLayout(fmToolbar);
    tbLayout->setContentsMargins(8,4,8,4);
    tbLayout->setSpacing(6);

    auto mkTb = [this](const QIcon& ico, const QString& tip) {
        auto b = new QToolButton(fmToolbar);
        b->setIcon(ico);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setIconSize(QSize(20,20));
        return b;
    };

    // Navigation
    fmBackButton = mkTb(icoBack(), "Back");
    fmUpButton = mkTb(icoUp(), "Up");

    auto newFolderBtn = mkTb(icoFolderNew(), "New Folder");
    auto copyBtn = mkTb(icoCopy(), "Copy");
    auto cutBtn = mkTb(icoCut(), "Cut");
    auto pasteBtn = mkTb(icoPaste(), "Paste");
    auto deleteBtn = mkTb(icoDelete(), "Delete");
    auto renameBtn = mkTb(icoRename(), "Rename");
    auto addToLibraryBtn = mkTb(icoAdd(), "Add to Library");

    fmViewModeButton = mkTb(icoGrid(), "Toggle Grid/List");

    auto fmSizeLbl = new QLabel("Size:", fmToolbar);
    fmSizeLbl->setStyleSheet("color:#9aa0a6;");

    fmThumbnailSizeSlider = new QSlider(Qt::Horizontal, fmToolbar);
    fmThumbnailSizeSlider->setRange(64, 320);
    fmThumbnailSizeSlider->setFixedWidth(140);
    fmThumbnailSizeSlider->setToolTip("Adjust thumbnail size");

    {
        QSettings s("AugmentCode", "KAssetManager");
        int thumb = s.value("FileManager/GridThumbSize", 120).toInt();
        fmThumbnailSizeSlider->setValue(thumb);
    }

    fmGroupSequencesCheckBox = mkTb(icoGroup(), "Group sequences");
    fmGroupSequencesCheckBox->setCheckable(true);

    fmHideFoldersCheckBox = mkTb(icoHide(), "Hide folders in the view");
    fmHideFoldersCheckBox->setCheckable(true);

    fmPreviewToggleButton = mkTb(icoEye(), "Show/Hide preview panel");
    fmPreviewToggleButton->setCheckable(true);
    fmPreviewToggleButton->setChecked(true);

    tbLayout->addWidget(fmBackButton);
    tbLayout->addWidget(fmUpButton);
    tbLayout->addWidget(newFolderBtn);
    tbLayout->addWidget(copyBtn);
    tbLayout->addWidget(cutBtn);
    tbLayout->addWidget(pasteBtn);

    tbLayout->addWidget(deleteBtn);
    tbLayout->addWidget(renameBtn);
    tbLayout->addWidget(addToLibraryBtn);
    tbLayout->addWidget(fmViewModeButton);
    tbLayout->addWidget(fmSizeLbl);
    tbLayout->addWidget(fmThumbnailSizeSlider);
    tbLayout->addWidget(fmGroupSequencesCheckBox);
    tbLayout->addWidget(fmHideFoldersCheckBox);
    tbLayout->addStretch(1);
    tbLayout->addWidget(fmPreviewToggleButton);

    rightLayout->addWidget(fmToolbar);

    // Models
    fmTreeModel = new QFileSystemModel(this);
    fmTreeModel->setRootPath(QString());
    fmTreeModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);

    fmDirModel = new QFileSystemModel(this);
    fmDirModel->setIconProvider(new FmIconProvider());
    fmDirModel->setRootPath(QString());
    fmDirModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);

    // Tree setup
    fmTree->setModel(fmTreeModel);
    for (int c = 1; c < fmTreeModel->columnCount(); ++c) fmTree->hideColumn(c);
    fmTree->setHeaderHidden(false);
    fmTree->header()->setStretchLastSection(true);
    fmTree->header()->setSectionResizeMode(QHeaderView::Interactive);
    fmTree->setExpandsOnDoubleClick(true);
    fmTree->setUniformRowHeights(true);
    fmTree->setSortingEnabled(true);
    fmTree->sortByColumn(0, Qt::AscendingOrder);
    fmTree->setRootIndex(fmTreeModel->index(fmTreeModel->rootPath()));


    // Views stack
    fmViewStack = new QStackedWidget(rightPanel);

    // Restore persisted view state
    {
        QSettings s("AugmentCode", "KAssetManager");
        fmIsGridMode = s.value("FileManager/ViewMode", true).toBool();
        const bool group = s.value("FileManager/GroupSequences", true).toBool();
        const bool hideFolders = s.value("FileManager/HideFolders", false).toBool();
        const bool previewVisible = s.value("FileManager/PreviewVisible", true).toBool();
        if (fmGroupSequencesCheckBox) fmGroupSequencesCheckBox->setChecked(group);
        if (fmHideFoldersCheckBox) fmHideFoldersCheckBox->setChecked(hideFolders);
        if (fmPreviewToggleButton) fmPreviewToggleButton->setChecked(previewVisible);
    }

    // Proxy model for grouping/hiding folders
    fmProxyModel = new SequenceGroupingProxyModel(this);
    fmProxyModel->setSourceModel(fmDirModel);
    fmProxyModel->setGroupingEnabled(fmGroupSequencesCheckBox && fmGroupSequencesCheckBox->isChecked());
    fmProxyModel->setHideFolders(fmHideFoldersCheckBox && fmHideFoldersCheckBox->isChecked());


    fmGridView = new FmGridViewEx(fmProxyModel, fmDirModel, fmViewStack);
    fmGridView->setModel(fmProxyModel);
    fmGridView->setViewMode(QListView::IconMode);
    fmGridView->setResizeMode(QListView::Adjust);
    fmGridView->setWrapping(true);
    fmGridView->setSpacing(4);
    {
        auto *d = new FmItemDelegate(fmGridView);
        fmGridView->setItemDelegate(d);
        QSettings s("AugmentCode", "KAssetManager");
        int fmThumb = s.value("FileManager/GridThumbSize", 120).toInt();
        d->setThumbnailSize(fmThumb);
        fmGridView->setIconSize(QSize(fmThumb, fmThumb));
        fmGridView->setGridSize(QSize(fmThumb + 24, fmThumb + 40));
        if (fmThumbnailSizeSlider) fmThumbnailSizeSlider->setValue(fmThumb);
    }
    fmGridView->setUniformItemSizes(true);
    fmGridView->setContextMenuPolicy(Qt::CustomContextMenu);
    fmGridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fmGridView->setSelectionBehavior(QAbstractItemView::SelectItems);
    fmGridView->setDragEnabled(true);
    fmGridView->setAcceptDrops(true);
    fmGridView->setDropIndicatorShown(true);
    fmGridView->setDragDropMode(QAbstractItemView::DragDrop);
    fmGridView->setDefaultDropAction(Qt::CopyAction);


    fmListView = new FmListViewEx(fmProxyModel, fmDirModel, fmViewStack);
    fmListView->setModel(fmProxyModel);
    fmListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    fmListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fmListView->setSortingEnabled(true);
    fmListView->setAlternatingRowColors(false);
    fmListView->setShowGrid(false);
    fmListView->verticalHeader()->setVisible(false);
    fmListView->verticalHeader()->setDefaultSectionSize(22);
    fmListView->verticalHeader()->setMinimumSectionSize(18);
    fmListView->setIconSize(QSize(18,18));
    fmListView->horizontalHeader()->setStretchLastSection(true);
    fmListView->horizontalHeader()->setSortIndicatorShown(true);
    fmListView->setContextMenuPolicy(Qt::CustomContextMenu);
    fmListView->setDragEnabled(true);
    fmListView->setAcceptDrops(true);
    fmListView->setDropIndicatorShown(true);
    fmListView->setDragDropMode(QAbstractItemView::DragDrop);
    fmListView->setDefaultDropAction(Qt::CopyAction);

    fmViewStack->addWidget(fmGridView);
    fmViewStack->addWidget(fmListView);
    fmViewStack->setCurrentWidget(fmIsGridMode ? static_cast<QWidget*>(fmGridView) : static_cast<QWidget*>(fmListView));
    // Create scrub controller (hover/CTRL scrub) for File Manager grid
    if (m_host && fmGridView) {
        m_host->fmScrubController = new GridScrubController(
            fmGridView,
            [this](const QModelIndex& idx) -> QString {
                QModelIndex src = idx;
                if (idx.isValid() && fmProxyModel && idx.model() == fmProxyModel)
                    src = fmProxyModel->mapToSource(idx);
                return fmDirModel ? fmDirModel->filePath(src) : QString();
            },
            this);
        // Initialize sequence/grouping state for scrubbing and preview system
        const bool groupOn = fmGroupSequencesCheckBox && fmGroupSequencesCheckBox->isChecked();
        m_host->fmScrubController->setSequenceGroupingEnabled(groupOn);
        LivePreviewManager::instance().setSequenceDetectionEnabled(groupOn);
    }

    if (fmViewModeButton) fmViewModeButton->setIcon(fmIsGridMode ? icoGrid() : icoList());

    // Build the right splitter now so Preview and Info are vertically resizable immediately
    ensurePreviewInfoLayout();
    // Restore sort column/order and column widths for List and Tree
    {
        QSettings s("AugmentCode", "KAssetManager");
        // Sorting
        const int sortCol = s.value("FileManager/SortColumn", 0).toInt();
        const Qt::SortOrder sortOrd = static_cast<Qt::SortOrder>(s.value("FileManager/SortOrder", static_cast<int>(Qt::AscendingOrder)).toInt());
        if (fmListView && fmListView->model()) {
            auto hh = fmListView->horizontalHeader();
            hh->setSortIndicator(sortCol, sortOrd);
            fmListView->sortByColumn(sortCol, sortOrd);
            if (fmProxyModel) fmProxyModel->sort(sortCol, sortOrd);
        }
        // Column widths
        if (fmListView && fmListView->model()) {
            auto hh = fmListView->horizontalHeader();
            for (int c = 0; c < fmListView->model()->columnCount(); ++c) {
                int w = s.value(QString("FileManager/ListView/Col%1").arg(c), -1).toInt();
                if (w > 0) hh->resizeSection(c, w);
            }
        }
        if (fmTree && fmTree->model()) {
            auto th = fmTree->header();
            for (int c = 0; c < fmTree->model()->columnCount(); ++c) {
                int w = s.value(QString("FileManager/Tree/Col%1").arg(c), -1).toInt();
                if (w > 0) th->resizeSection(c, w);
            }
        }
    }
    // Persist column widths immediately when resized
    if (fmListView && fmListView->horizontalHeader()) {
        connect(fmListView->horizontalHeader(), &QHeaderView::sectionResized, this,
                [](int logical, int /*oldSize*/, int newSize) {
                    QSettings s("AugmentCode", "KAssetManager");
                    s.setValue(QString("FileManager/ListView/Col%1").arg(logical), newSize);
                });
    }
    if (fmTree && fmTree->header()) {
        connect(fmTree->header(), &QHeaderView::sectionResized, this,
                [](int logical, int /*oldSize*/, int newSize) {
                    QSettings s("AugmentCode", "KAssetManager");
                    s.setValue(QString("FileManager/Tree/Col%1").arg(logical), newSize);
                });
    }
    // Persist sort changes and re-apply to proxy so Grid follows List sorting
    if (fmListView && fmListView->horizontalHeader()) {
        connect(fmListView->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this,
                [this](int logical, Qt::SortOrder order) {
                    QSettings s("AugmentCode", "KAssetManager");
                    s.setValue("FileManager/SortColumn", logical);
                    s.setValue("FileManager/SortOrder", static_cast<int>(order));
                    if (fmProxyModel) fmProxyModel->sort(logical, order);
                });
    }



    // Add panels to root splitter
    fmSplitter->addWidget(leftPanel);
    fmSplitter->addWidget(rightPanel);
    fmSplitter->setStretchFactor(0, 0);
    fmSplitter->setStretchFactor(1, 1);

    // Connections
    connect(fmTree, &QTreeView::clicked, this, &FileManagerWidget::onFmTreeActivated);
    // Trigger navigation on single-click and on activated (Enter/double-click)
    connect(fmTree, &QTreeView::activated, this, &FileManagerWidget::onFmTreeActivated);

    if (m_host) {
        connect(newFolderBtn, &QToolButton::clicked, m_host, &MainWindow::onFmNewFolder);
        connect(copyBtn, &QToolButton::clicked, m_host, &MainWindow::onFmCopy);
        connect(cutBtn, &QToolButton::clicked, m_host, &MainWindow::onFmCut);
        connect(pasteBtn, &QToolButton::clicked, m_host, &MainWindow::onFmPaste);
        connect(deleteBtn, &QToolButton::clicked, m_host, &MainWindow::onFmDelete);
        connect(renameBtn, &QToolButton::clicked, m_host, &MainWindow::onFmRename);
        connect(fmBackButton, &QToolButton::clicked, m_host, &MainWindow::onFmNavigateBack);
        connect(fmUpButton, &QToolButton::clicked, m_host, &MainWindow::onFmNavigateUp);

        connect(addToLibraryBtn, &QToolButton::clicked, m_host, &MainWindow::onAddSelectionToAssetLibrary);
        connect(fmViewModeButton, &QToolButton::clicked, m_host, &MainWindow::onFmViewModeToggled);
        connect(fmThumbnailSizeSlider, &QSlider::valueChanged, m_host, &MainWindow::onFmThumbnailSizeChanged);
        connect(fmGroupSequencesCheckBox, &QToolButton::toggled, m_host, &MainWindow::onFmGroupSequencesToggled);
        connect(fmHideFoldersCheckBox, &QToolButton::toggled, m_host, &MainWindow::onFmHideFoldersToggled);
        connect(fmPreviewToggleButton, &QToolButton::toggled, m_host, &MainWindow::onFmTogglePreview);
    } else {
        connect(newFolderBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmNewFolder);
        connect(copyBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmCopy);
        connect(cutBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmCut);
        connect(pasteBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmPaste);
        connect(deleteBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmDelete);
        connect(renameBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmRename);
        connect(fmBackButton, &QToolButton::clicked, this, &FileManagerWidget::onFmNavigateBack);
        connect(fmUpButton, &QToolButton::clicked, this, &FileManagerWidget::onFmNavigateUp);

        connect(fmViewModeButton, &QToolButton::clicked, this, &FileManagerWidget::onFmViewModeToggled);
        connect(fmThumbnailSizeSlider, &QSlider::valueChanged, this, &FileManagerWidget::onFmThumbnailSizeChanged);
        connect(fmGroupSequencesCheckBox, &QToolButton::toggled, this, &FileManagerWidget::onFmGroupSequencesToggled);
        connect(fmHideFoldersCheckBox, &QToolButton::toggled, this, &FileManagerWidget::onFmHideFoldersToggled);
    }

    connect(fmGridView, &QAbstractItemView::doubleClicked, this, &FileManagerWidget::onFmItemDoubleClicked);
    connect(fmListView, &QAbstractItemView::doubleClicked, this, &FileManagerWidget::onFmItemDoubleClicked);
    fmTree->setContextMenuPolicy(Qt::CustomContextMenu);
    if (m_host) {
        if (fmGridView) fmGridView->installEventFilter(m_host);
        if (fmListView) fmListView->installEventFilter(m_host);
        if (fmGridView->viewport()) fmGridView->viewport()->installEventFilter(m_host);
        if (fmListView->viewport()) fmListView->viewport()->installEventFilter(m_host);
        if (fmTree->viewport()) fmTree->viewport()->installEventFilter(m_host);
        if (fmGridView->selectionModel()) connect(fmGridView->selectionModel(), &QItemSelectionModel::selectionChanged, m_host, &MainWindow::onFmSelectionChanged);
        if (fmListView->selectionModel()) connect(fmListView->selectionModel(), &QItemSelectionModel::selectionChanged, m_host, &MainWindow::onFmSelectionChanged);
        // Route context menus to the widget's own handlers
        connect(fmGridView, &QWidget::customContextMenuRequested, this, &FileManagerWidget::onFmShowContextMenu);
        connect(fmListView, &QWidget::customContextMenuRequested, this, &FileManagerWidget::onFmShowContextMenu);
        connect(fmTree, &QWidget::customContextMenuRequested, this, &FileManagerWidget::onFmTreeContextMenu);
    } else {
        if (fmGridView) fmGridView->installEventFilter(this);
        if (fmListView) fmListView->installEventFilter(this);
        if (fmGridView->viewport()) fmGridView->viewport()->installEventFilter(this);
        if (fmListView->viewport()) fmListView->viewport()->installEventFilter(this);
        if (fmTree->viewport()) fmTree->viewport()->installEventFilter(this);
        connect(fmGridView, &QWidget::customContextMenuRequested, this, &FileManagerWidget::onFmShowContextMenu);
        connect(fmListView, &QWidget::customContextMenuRequested, this, &FileManagerWidget::onFmShowContextMenu);
        connect(fmTree, &QWidget::customContextMenuRequested, this, &FileManagerWidget::onFmTreeContextMenu);
    }

    // Favorites basic context
    fmFavoritesList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fmFavoritesList, &QListWidget::itemActivated, this, &FileManagerWidget::onFmFavoriteActivated);
    connect(fmFavoritesList, &QListWidget::customContextMenuRequested, this, &FileManagerWidget::onFmTreeContextMenu);

    // Configure splitters and restore sizes
    fmSplitter->setChildrenCollapsible(false);
    fmSplitter->setHandleWidth(6);
    fmSplitter->setOpaqueResize(true);
    fmLeftSplitter->setChildrenCollapsible(false);
    fmLeftSplitter->setHandleWidth(6);
    fmLeftSplitter->setOpaqueResize(true);

    QSettings s("AugmentCode", "KAssetManager");
    if (s.contains("FileManager/MainSplitter")) {
        fmSplitter->restoreState(s.value("FileManager/MainSplitter").toByteArray());
        // Restore explicit sizes if present (ensures collapsed panes remain collapsed)
        const QVariantList sizesVar = s.value("FileManager/MainSplitterSizes").toList();
        if (!sizesVar.isEmpty()) { QList<int> sz; for (const QVariant& v : sizesVar) sz << v.toInt(); fmSplitter->setSizes(sz); }
    }
    if (s.contains("FileManager/LeftSplitter")) {
        fmLeftSplitter->restoreState(s.value("FileManager/LeftSplitter").toByteArray());
        const QVariantList sizesVar = s.value("FileManager/LeftSplitterSizes").toList();
        if (!sizesVar.isEmpty()) { QList<int> sz; for (const QVariant& v : sizesVar) sz << v.toInt(); fmLeftSplitter->setSizes(sz); }
    }

    // Initial path: defer until MainWindow connects our signal to avoid missing it
    QSettings s2("AugmentCode", "KAssetManager");
    QString startPath = s2.value("FileManager/CurrentPath").toString();
    if (startPath.isEmpty() || !QFileInfo::exists(startPath)) {
        QFileInfoList drives = QDir::drives();
        startPath = drives.isEmpty() ? QDir::homePath() : drives.first().absoluteFilePath();
    }
    LogManager::instance().addLog(QString("[TRACE] FM: initial path chosen: %1").arg(startPath), "DEBUG");
    QTimer::singleShot(0, this, [this, startPath]() {
        if (m_host) emit navigateToPathRequested(startPath, false);
        else navigateToPath(startPath, false);
        LogManager::instance().addLog("[TRACE] FM: initial navigation complete (deferred)", "DEBUG");
    });

    LogManager::instance().addLog("[TRACE] FM: setupUi leave", "DEBUG");
}

void FileManagerWidget::bindHostPointers()
{
    if (!m_host) return;
    m_host->fmSplitter = fmSplitter;
    m_host->fmLeftSplitter = fmLeftSplitter;
    m_host->fmRightSplitter = fmRightSplitter;
    m_host->fmPreviewInfoSplitter = fmPreviewInfoSplitter;
    m_host->fmFavoritesList = fmFavoritesList;
    m_host->fmTree = fmTree;
    m_host->fmTreeModel = fmTreeModel;
    m_host->fmDirModel = fmDirModel;
    m_host->fmProxyModel = fmProxyModel;
    m_host->fmToolbar = fmToolbar;
    m_host->fmBackButton = fmBackButton;
    m_host->fmUpButton = fmUpButton;
    m_host->fmViewModeButton = fmViewModeButton;
    m_host->fmThumbnailSizeSlider = fmThumbnailSizeSlider;
    m_host->fmPreviewToggleButton = fmPreviewToggleButton;
    m_host->fmViewStack = fmViewStack;
    m_host->fmGridView = fmGridView;
    m_host->fmListView = fmListView;
    m_host->fmIsGridMode = fmIsGridMode;
    m_host->fmDirectoryWatcher = fmDirectoryWatcher;
    m_host->fmNavigationHistory = fmNavigationHistory;
    m_host->fmNavigationIndex = fmNavigationIndex;
    m_host->fmFavorites = fmFavorites;
    m_host->fmPreviewPanel = fmPreviewPanel;
    m_host->fmImageView = fmImageView;
    m_host->fmImageScene = fmImageScene;
    m_host->fmImageItem = fmImageItem;
    m_host->fmVideoWidget = fmVideoWidget;
    m_host->fmTextView = fmTextView;
    m_host->fmCsvView = fmCsvView;
    m_host->fmCsvModel = fmCsvModel;
    m_host->fmPdfDoc = fmPdfDoc;
    m_host->fmPdfView = fmPdfView;
    m_host->fmPdfCurrentPage = fmPdfCurrentPage;
    m_host->fmPdfPrevBtn = fmPdfPrevBtn;
    m_host->fmPdfNextBtn = fmPdfNextBtn;
    m_host->fmPdfPageLabel = fmPdfPageLabel;
    m_host->fmSvgView = fmSvgView;
    m_host->fmSvgScene = fmSvgScene;
    m_host->fmSvgItem = fmSvgItem;
    m_host->fmAlphaCheck = fmAlphaCheck;
    m_host->fmImageFitToView = fmImageFitToView;
    m_host->fmOriginalImage = fmOriginalImage;
    m_host->fmCurrentPreviewPath = fmCurrentPreviewPath;
    m_host->fmPreviewHasAlpha = fmPreviewHasAlpha;
    m_host->fmAlphaOnlyMode = fmAlphaOnlyMode;
    m_host->fmPreviewDragStartPos = fmPreviewDragStartPos;
    m_host->fmPreviewDragPending = fmPreviewDragPending;
    m_host->fmMediaPlayer = fmMediaPlayer;
    m_host->fmAudioOutput = fmAudioOutput;
    m_host->fmPlayPauseBtn = fmPlayPauseBtn;
    m_host->fmPrevFrameBtn = fmPrevFrameBtn;
    m_host->fmNextFrameBtn = fmNextFrameBtn;
    m_host->fmShortcutObjs = fmShortcutObjs;
    m_host->fmPositionSlider = fmPositionSlider;
    m_host->fmTimeLabel = fmTimeLabel;
    m_host->fmVolumeSlider = fmVolumeSlider;
    m_host->fmIsSequence = fmIsSequence;
    m_host->fmSequenceFramePaths = fmSequenceFramePaths;
    m_host->fmSequenceStartFrame = fmSequenceStartFrame;
    m_host->fmSequenceEndFrame = fmSequenceEndFrame;
    m_host->fmSequenceCurrentIndex = fmSequenceCurrentIndex;
    m_host->fmSequenceTimer = fmSequenceTimer;
    m_host->fmSequencePlaying = fmSequencePlaying;
    m_host->fmSequenceFps = fmSequenceFps;
    m_host->fmMuteBtn = fmMuteBtn;
    m_host->fmInfoPanel = fmInfoPanel;
    m_host->fmInfoFileName = fmInfoFileName;
    m_host->fmInfoFilePath = fmInfoFilePath;
    m_host->fmInfoFileSize = fmInfoFileSize;
    m_host->fmInfoFileType = fmInfoFileType;
    m_host->fmInfoDimensions = fmInfoDimensions;
    m_host->fmInfoCreated = fmInfoCreated;
    m_host->fmInfoModified = fmInfoModified;
    m_host->fmInfoPermissions = fmInfoPermissions;
    m_host->fmClipboard = fmClipboard;
    m_host->fmClipboardCutMode = fmClipboardCutMode;
    m_host->fileOpsDialog = fileOpsDialog;
    m_host->fmOverlayCurrentIndex = fmOverlayCurrentIndex;
    m_host->fmOverlaySourceView = fmOverlaySourceView;
}

void FileManagerWidget::navigateToPath(const QString& path, bool addToHistory)
{
    LogManager::instance().addLog(QString("[TRACE] FM: navigateToPath enter: %1").arg(path), "DEBUG");
    if (!fmDirModel) { LogManager::instance().addLog("[TRACE] FM: navigateToPath no fmDirModel", "DEBUG"); return; }
    LogManager::instance().addLog("[TRACE] FM: setRootPath about to call", "DEBUG");
    QModelIndex srcRoot = fmDirModel->setRootPath(path);
    LogManager::instance().addLog("[TRACE] FM: setRootPath returned", "DEBUG");
    if (fmProxyModel) {
        LogManager::instance().addLog("[TRACE] FM: proxy rebuildForRoot", "DEBUG");
        fmProxyModel->rebuildForRoot(path);
        QSettings s("AugmentCode", "KAssetManager");
        const int sortCol = s.value("FileManager/SortColumn", 0).toInt();
        const Qt::SortOrder sortOrd = static_cast<Qt::SortOrder>(s.value("FileManager/SortOrder", static_cast<int>(Qt::AscendingOrder)).toInt());
        fmProxyModel->sort(sortCol, sortOrd);
    }
    QModelIndex rootIndex = fmProxyModel ? fmProxyModel->mapFromSource(srcRoot) : srcRoot;
    LogManager::instance().addLog("[TRACE] FM: setRootIndex on views", "DEBUG");
    fmGridView->setRootIndex(rootIndex);
    fmListView->setRootIndex(rootIndex);

    if (!m_host && addToHistory) {
        if (fmNavigationIndex + 1 < fmNavigationHistory.size())
            fmNavigationHistory = fmNavigationHistory.mid(0, fmNavigationIndex + 1);
        fmNavigationHistory.push_back(path);
        fmNavigationIndex = fmNavigationHistory.size() - 1;
    }

    LogManager::instance().addLog("[TRACE] FM: scrollTreeToPath", "DEBUG");
    scrollTreeToPath(path);
    LogManager::instance().addLog("[TRACE] FM: updateNavigationButtons", "DEBUG");
    updateNavigationButtons();
    LogManager::instance().addLog("[TRACE] FM: settings save current path", "DEBUG");
    QSettings("AugmentCode", "KAssetManager").setValue("FileManager/CurrentPath", path);
    LogManager::instance().addLog("[TRACE] FM: navigateToPath leave", "DEBUG");
}

void FileManagerWidget::scrollTreeToPath(const QString& path)
{
    if (!fmTree || !fmTreeModel) return;
    QModelIndex idx = fmTreeModel->index(path);
    if (!idx.isValid()) return;
    fmTree->expand(idx);
    fmTree->scrollTo(idx, QAbstractItemView::PositionAtCenter);
    fmTree->setCurrentIndex(idx);
    // ensure right splitter is present for resizing and preview visibility restoration
    ensurePreviewInfoLayout();
}

void FileManagerWidget::updateNavigationButtons()
{
    if (m_host) m_host->fmUpdateNavigationButtons();
}

void FileManagerWidget::onFmTreeActivated(const QModelIndex &index)
{
    const QString path = fmTreeModel->filePath(index);
    if (path.isEmpty()) return;
    LogManager::instance().addLog(QString("[TRACE] FM: tree activated -> %1").arg(path), "DEBUG");
    emit navigateToPathRequested(path, true);
}

void FileManagerWidget::onFmViewModeToggled()
{
    fmIsGridMode = !fmIsGridMode;
    fmViewStack->setCurrentWidget(fmIsGridMode ? static_cast<QWidget*>(fmGridView) : static_cast<QWidget*>(fmListView));
    fmViewModeButton->setIcon(fmIsGridMode ? icoGrid() : icoList());
}

void FileManagerWidget::onFmThumbnailSizeChanged(int size)
{
    QSettings("AugmentCode", "KAssetManager").setValue("FileManager/GridThumbSize", size);
    if (fmGridView) {
        if (auto d = qobject_cast<FmItemDelegate*>(fmGridView->itemDelegate())) {
            d->setThumbnailSize(size);
        }
        fmGridView->setIconSize(QSize(size, size));
        fmGridView->setGridSize(QSize(size + 24, size + 40));
        fmGridView->viewport()->update();
    }
}

void FileManagerWidget::onFmGroupSequencesToggled(bool checked)
{
    fmGroupSequences = checked;
    if (fmProxyModel) {
        fmProxyModel->setGroupingEnabled(checked);
        const QString cur = QSettings("AugmentCode", "KAssetManager").value(fmSettingsKey("CurrentPath")).toString();
        if (!cur.isEmpty()) fmProxyModel->rebuildForRoot(cur);
    }
}

void FileManagerWidget::onFmHideFoldersToggled(bool checked)
{
    fmHideFolders = checked;
    if (fmProxyModel) {
        fmProxyModel->setHideFolders(checked);
    }
}

void FileManagerWidget::onFmRemoveFavorite()
{
    auto *item = fmFavoritesList->currentItem();
    if (!item) return;
    delete item;
}

void FileManagerWidget::onFmFavoriteActivated(QListWidgetItem* item)
{
    if (!item) return;
    emit navigateToPathRequested(item->data(Qt::UserRole).toString(), true);
}

void FileManagerWidget::onFmTreeContextMenu(const QPoint &pos)
{
    QWidget* senderW = qobject_cast<QWidget*>(sender());
    if (!senderW) return;

    // Determine which widget triggered the menu: tree or favorites
    const bool isTree = (senderW == fmTree || senderW == fmTree->viewport());
    const bool isFavorites = (senderW == fmFavoritesList);

    QMenu menu(this);

    if (isFavorites) {
        // Favorites list context
        QAction* removeFav = menu.addAction("Remove from Favorites");
        QAction* openFav = menu.addAction("Open in File Manager");
        QAction* chosen = menu.exec(fmFavoritesList->mapToGlobal(pos));
        if (!chosen) return;
        if (chosen == removeFav) {
            onFmRemoveFavorite();
        } else if (chosen == openFav) {
            auto *it = fmFavoritesList->itemAt(pos);
            if (!it) it = fmFavoritesList->currentItem();
            if (it) emit navigateToPathRequested(it->data(Qt::UserRole).toString(), true);
        }
        return;
    }

    // Tree view context
    QModelIndex idx = fmTree->indexAt(pos);
    QString basePath;
    if (idx.isValid()) basePath = fmDirModel->filePath(idx);
    if (basePath.isEmpty()) basePath = QSettings("AugmentCode","KAssetManager").value(fmSettingsKey("CurrentPath")).toString();

    QAction* newFolderA = menu.addAction("New Folder");
    QAction* renameA = nullptr;
    if (idx.isValid()) renameA = menu.addAction("Rename");
    QAction* pasteA = menu.addAction("Paste");
    menu.addSeparator();
    QAction* showInExplorerA = menu.addAction("Show in Explorer");
    QAction* refreshA = menu.addAction("Refresh");

    QAction* chosen = menu.exec(fmTree->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == newFolderA) {
        onFmNewFolder();
    } else if (chosen == renameA) {
        if (idx.isValid()) fmTree->edit(idx);
    } else if (chosen == pasteA) {
        if (m_host && !basePath.isEmpty()) m_host->onFmPasteInto(basePath);
        else onFmPaste();
    } else if (chosen == showInExplorerA) {
        if (!basePath.isEmpty()) DragUtils::instance().showInExplorer(basePath);
    } else if (chosen == refreshA) {
        onFmRefresh();
    }
}

void FileManagerWidget::onFmShowContextMenu(const QPoint &pos)
{
    // Identify which view: grid or list
    QAbstractItemView* view = nullptr;
    if (sender() == fmGridView) view = fmGridView;
    else if (sender() == fmListView) view = fmListView;
    if (!view) return;

    // Map to viewport and figure out the index under cursor
    QPoint vpPos = view->viewport()->mapFrom(view, pos);
    QModelIndex idx = view->indexAt(vpPos);

    // Collect selected paths
    QStringList selectedPaths;
    QModelIndexList rows = view->selectionModel() ? view->selectionModel()->selectedRows() : QModelIndexList();
    for (const QModelIndex& r : rows) {
        QModelIndex sidx = r;
        if (fmProxyModel && r.model() == fmProxyModel) sidx = fmProxyModel->mapToSource(r);
        selectedPaths << fmDirModel->filePath(sidx);
    }

    // If right-clicked item is valid but not selected, use it as selection
    if (idx.isValid() && selectedPaths.isEmpty()) {
        QModelIndex sidx = idx;
        if (fmProxyModel && idx.model() == fmProxyModel) sidx = fmProxyModel->mapToSource(idx);
        selectedPaths << fmDirModel->filePath(sidx);
    }

    const bool hasSelection = !selectedPaths.isEmpty();

    QMenu menu(this);
    QAction* openPreviewA = nullptr;
    QAction* openA = nullptr;
    if (hasSelection) {
        openPreviewA = menu.addAction("Open Preview");
        openA = menu.addAction("Open");
    }

    QAction* addFavA = menu.addAction("Add Current Folder to Favorites");
    menu.addSeparator();

    QAction* copyA = menu.addAction("Copy");
    QAction* cutA = menu.addAction("Cut");
    QAction* pasteA = menu.addAction("Paste");
    QAction* renameA = nullptr;
    QAction* bulkRenameA = nullptr;
    QAction* createFolderWithSelA = nullptr;
    QAction* deleteA = nullptr;
    QAction* deletePermA = nullptr;
    QAction* convertA = nullptr;
    if (hasSelection) {
        renameA = menu.addAction("Rename");
        if (selectedPaths.size() >= 2)
            bulkRenameA = menu.addAction("Bulk Rename...");
        createFolderWithSelA = menu.addAction("Create Folder with Selected...");
        deleteA = menu.addAction("Delete");
        deletePermA = menu.addAction("Delete Permanently");
        // Offer converter only when all are supported media files (images or videos)
        auto isSupportedExt = [](const QString &ext){
            static const QSet<QString> img{ "png","jpg","jpeg","tif","tiff","exr","iff","psd" };
            static const QSet<QString> vid{ "mov","mxf","mp4","avi","mp5" };
            return img.contains(ext) || vid.contains(ext);
        };
        bool allSupported = true;
        for (const QString &p : selectedPaths) {
            QFileInfo fi(p);
            if (!fi.exists() || fi.isDir() || !isSupportedExt(fi.suffix().toLower())) { allSupported = false; break; }
        }
        if (allSupported) convertA = menu.addAction("Convert to Format...");
    }

    menu.addSeparator();
    QAction* newFolderA = menu.addAction("New Folder");
    QAction* showInExplorerA = menu.addAction("Show in Explorer");
    QAction* refreshA = menu.addAction("Refresh");

    QPoint globalPos = view->mapToGlobal(pos);
    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == openPreviewA && hasSelection) {
        // Use the index under cursor if available, else the first selected
        QModelIndex target = idx.isValid() ? idx : (rows.isEmpty() ? QModelIndex() : rows.first());
        if (target.isValid()) onFmItemDoubleClicked(target);
    } else if (chosen == openA && hasSelection) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(selectedPaths.first()));
    } else if (chosen == addFavA) {
        onFmAddToFavorites();
    } else if (chosen == copyA) {
        if (m_host) m_host->onFmCopy(); else onFmCopy();
    } else if (chosen == cutA) {
        if (m_host) m_host->onFmCut(); else onFmCut();
    } else if (chosen == pasteA) {
        if (m_host) m_host->onFmPaste(); else onFmPaste();
    } else if (chosen == renameA && hasSelection) {
        if (m_host) m_host->onFmRename(); else view->edit(idx.isValid() ? idx : rows.first());
    } else if (chosen == bulkRenameA && hasSelection) {
        if (m_host) m_host->onFmBulkRename();
    } else if (chosen == createFolderWithSelA && hasSelection) {
        if (m_host) m_host->onFmCreateFolderWithSelected();
    } else if (chosen == deleteA && hasSelection) {
        if (m_host) m_host->onFmDelete(); else onFmDelete();
    } else if (chosen == deletePermA && hasSelection) {
        if (m_host) m_host->onFmDeletePermanent(); else onFmDeletePermanent();
    } else if (chosen == convertA && hasSelection) {
        if (m_host) m_host->releaseAnyPreviewLocksForPaths(selectedPaths);
        auto *dlg = new MediaConvertDialog(selectedPaths, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, this, &FileManagerWidget::onFmRefresh);
        connect(dlg, &QObject::destroyed, this, [this](){ QTimer::singleShot(100, this, &FileManagerWidget::onFmRefresh); });
        dlg->show();
    } else if (chosen == newFolderA) {
        if (m_host) m_host->onFmNewFolder(); else onFmNewFolder();
    } else if (chosen == showInExplorerA) {
        const QString base = QSettings("AugmentCode","KAssetManager").value(fmSettingsKey("CurrentPath")).toString();
        if (!base.isEmpty()) DragUtils::instance().showInExplorer(base);
    } else if (chosen == refreshA) {
        onFmRefresh();
    }
}

void FileManagerWidget::onFmAddToFavorites()
{
    const QString path = QSettings("AugmentCode", "KAssetManager").value(fmSettingsKey("CurrentPath")).toString();
    if (path.isEmpty()) return;
    auto *it = new QListWidgetItem(QFileInfo(path).fileName(), fmFavoritesList);
    it->setData(Qt::UserRole, path);
}

void FileManagerWidget::onFmRefresh()
{
    const QString path = QSettings("AugmentCode", "KAssetManager").value(fmSettingsKey("CurrentPath")).toString();
    navigateToPath(path, false);
}

void FileManagerWidget::onFmNewFolder()
{
    const QString base = QSettings("AugmentCode", "KAssetManager").value(fmSettingsKey("CurrentPath")).toString();
    if (base.isEmpty()) return;
    QString newPath = base + QDir::separator() + "New Folder";
    int n = 2;
    while (QFileInfo::exists(newPath)) newPath = base + QDir::separator() + QString("New Folder (%1)").arg(n++);
    QDir().mkpath(newPath);
    onFmRefresh();
}

void FileManagerWidget::onFmRename()
{
    auto view = fmIsGridMode ? static_cast<QAbstractItemView*>(fmGridView) : static_cast<QAbstractItemView*>(fmListView);
    QModelIndex idx = view->currentIndex();
    if (!idx.isValid()) return;
    view->edit(idx);
}

void FileManagerWidget::onFmCopy()
{
    if (m_host) { m_host->onFmCopy(); return; }
    QStringList paths;
    auto sel = fmIsGridMode ? fmGridView->selectionModel()->selectedRows() : fmListView->selectionModel()->selectedRows();
    for (const auto& i : sel) {
        QModelIndex src = (fmProxyModel && i.model()==fmProxyModel) ? fmProxyModel->mapToSource(i) : i;
        paths << fmDirModel->filePath(src);
    }
    QClipboard *cb = QApplication::clipboard();
    QMimeData *md = new QMimeData();
    QList<QUrl> urls; for (const QString &p : paths) urls << QUrl::fromLocalFile(p);
    md->setUrls(urls);
    cb->setMimeData(md);
}

void FileManagerWidget::onFmCut() { if (m_host) { m_host->onFmCut(); return; } onFmCopy(); fmClipboardCutMode = true; }

void FileManagerWidget::onFmPaste()
{
    if (m_host) { m_host->onFmPaste(); return; }
    // No host: paste into current root using FileOpsQueue if clipboard contains URLs
    const QMimeData* md = QApplication::clipboard()->mimeData();
    if (!md || !md->hasUrls()) return;
    QStringList srcs; for (const QUrl &u : md->urls()) if (u.isLocalFile()) srcs << u.toLocalFile();
    if (srcs.isEmpty()) return;
    const QString destDir = fmDirModel ? fmDirModel->rootPath() : QString();
    if (destDir.isEmpty()) return;
    if (m_host) m_host->releaseAnyPreviewLocksForPaths(srcs);
    auto &q = FileOpsQueue::instance();
    if (fmClipboardCutMode) q.enqueueMove(srcs, destDir); else q.enqueueCopy(srcs, destDir);
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
    fmClipboardCutMode = false;
}

void FileManagerWidget::onFmDelete()
{
    if (m_host) { m_host->onFmDelete(); return; }
    QStringList paths;
    auto sel = fmIsGridMode ? fmGridView->selectionModel()->selectedRows() : fmListView->selectionModel()->selectedRows();
    for (const auto& i : sel) {
        QModelIndex src = (fmProxyModel && i.model()==fmProxyModel) ? fmProxyModel->mapToSource(i) : i;
        paths << fmDirModel->filePath(src);
    }
    if (m_host) m_host->releaseAnyPreviewLocksForPaths(paths);
    FileOpsQueue::instance().enqueueDelete(paths);
}

void FileManagerWidget::onFmDeletePermanent()
{
    if (m_host) { m_host->onFmDeletePermanent(); return; }
    QStringList paths;
    auto sel = fmIsGridMode ? fmGridView->selectionModel()->selectedRows() : fmListView->selectionModel()->selectedRows();
    for (const auto& i : sel) {
        QModelIndex src = (fmProxyModel && i.model()==fmProxyModel) ? fmProxyModel->mapToSource(i) : i;
        paths << fmDirModel->filePath(src);
    }
    if (m_host) m_host->releaseAnyPreviewLocksForPaths(paths);
    FileOpsQueue::instance().enqueueDeletePermanent(paths);
}

void FileManagerWidget::onFmCreateFolderWithSelected()
{
    const QString base = QSettings("AugmentCode", "KAssetManager").value(fmSettingsKey("CurrentPath")).toString();
    if (base.isEmpty()) return;
    QString newDir = base + QDir::separator() + "Group";
    int n = 2; while (QFileInfo::exists(newDir)) newDir = base + QDir::separator() + QString("Group (%1)").arg(n++);
    QDir().mkpath(newDir);
    QStringList paths; auto sel = fmIsGridMode ? fmGridView->selectionModel()->selectedRows() : fmListView->selectionModel()->selectedRows();
    for (const auto& i : sel) paths << fmDirModel->filePath(i);
    if (m_host) m_host->releaseAnyPreviewLocksForPaths(paths);
    FileOpsQueue::instance().enqueueMove(paths, newDir);
}

void FileManagerWidget::onFmBulkRename()
{
    // Collect selection from active view
    QAbstractItemView* view = fmIsGridMode ? static_cast<QAbstractItemView*>(fmGridView)
                                           : static_cast<QAbstractItemView*>(fmListView);
    if (!view || !view->selectionModel()) return;
    QStringList paths;
    QModelIndexList rows = view->selectionModel()->selectedRows();
    for (const QModelIndex& r : rows) {
        QModelIndex sidx = r;
        if (fmProxyModel && r.model() == fmProxyModel) sidx = fmProxyModel->mapToSource(r);
        paths << fmDirModel->filePath(sidx);
    }
    if (paths.size() < 2) return;
    if (m_host) m_host->releaseAnyPreviewLocksForPaths(paths);
    BulkRenameDialog dialog(paths, this);
    if (dialog.exec() == QDialog::Accepted) {
        onFmRefresh();
    }
}

void FileManagerWidget::onFmNavigateBack()
{
    if (fmNavigationIndex <= 0) return;
    fmNavigationIndex--; emit navigateToPathRequested(fmNavigationHistory.value(fmNavigationIndex), false);
}

void FileManagerWidget::onFmNavigateUp()
{
    const QString cur = QSettings("AugmentCode", "KAssetManager").value(fmSettingsKey("CurrentPath")).toString();
    if (cur.isEmpty()) return; QDir d(cur); d.cdUp(); const QString up = d.absolutePath();
    emit navigateToPathRequested(up, true);
}

