#include "widgets/file_manager_widget.h"

#include "file_ops.h"
#include "file_ops_dialog.h"
#include "log_manager.h"
#include "mainwindow.h"
#include "ui/icon_helpers.h"
#include "widgets/fm_drag_views.h"
#include "widgets/fm_icon_provider.h"
#include "widgets/fm_item_delegate.h"
#include "widgets/sequence_grouping_proxy_model.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QGraphicsPixmapItem>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QListView>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QSortFilterProxyModel>
#include "media_convert_dialog.h"
#ifdef Q_OS_WIN
#include <Windows.h>
#include <shellapi.h>
#endif

static QString fm_uniqueNameInDir(const QString &dirPath, const QString &baseName);
FileManagerWidget::FileManagerWidget(MainWindow* host, QWidget* parent)
    : QWidget(parent)
    , m_host(host)
{
    setupUi();
    bindHostPointers();
}

FileManagerWidget::~FileManagerWidget() = default;

void FileManagerWidget::setupUi()
{

    // Splitter: left (tree) | right (view)
    fmSplitter = new QSplitter(Qt::Horizontal, this);

    // Left: Favorites (top) | Folder tree (bottom) in a vertical splitter
    QWidget *left = new QWidget(fmSplitter);
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0,0,0,0);
    leftLayout->setSpacing(0);

    fmTreeModel = new QFileSystemModel(left);
    fmTreeModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);

    fmLeftSplitter = new QSplitter(Qt::Vertical, left);

    // Favorites container
    QWidget *favContainer = new QWidget(fmLeftSplitter);
    QVBoxLayout *favLayout = new QVBoxLayout(favContainer);
    favLayout->setContentsMargins(0,0,0,0);
    favLayout->setSpacing(0);
    QLabel *favHeader = new QLabel("â˜… Favorites", favContainer);
    favHeader->setStyleSheet("color:#9aa0a6; font-weight:bold; padding:6px 4px;");
    favLayout->addWidget(favHeader);

    fmFavoritesList = new QListWidget(favContainer);
    fmFavoritesList->setStyleSheet("QListWidget{background:#0a0a0a; border:none; color:#fff;} QListWidget::item:selected{background:#2f3a4a;}");
    fmFavoritesList->setContextMenuPolicy(Qt::CustomContextMenu);
    if (m_host) {
        connect(fmFavoritesList, &QListWidget::itemDoubleClicked, this, &FileManagerWidget::onFmFavoriteActivated);
    }
    connect(fmFavoritesList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!fmFavoritesList || !m_host) return;
        QPoint gp = fmFavoritesList->viewport()->mapToGlobal(pos);
        QMenu m; QAction *rem = m.addAction("Remove Favorite", this, &FileManagerWidget::onFmRemoveFavorite);
        rem->setEnabled(fmFavoritesList->currentItem()!=nullptr);
        m.exec(gp);
    });
    favLayout->addWidget(fmFavoritesList);
    // Load favorites from settings
    {
        QSettings s("AugmentCode", "KAssetManager");
        int size = s.beginReadArray("FileManager/Favorites");
        for (int i=0;i<size;++i) {
            s.setArrayIndex(i);
            const QString p = s.value("path").toString();
            if (!p.isEmpty()) fmFavorites << p;
        }
        s.endArray();
        fmFavorites.removeDuplicates();
        if (fmFavoritesList) {
            fmFavoritesList->clear();
            for (const QString &p : fmFavorites) {
                QListWidgetItem *it = new QListWidgetItem(QIcon::fromTheme("star"), QFileInfo(p).fileName());
                it->setToolTip(p);
                it->setData(Qt::UserRole, p);
                fmFavoritesList->addItem(it);
            }
        }
    }

    // Folder tree
    fmTreeModel->setRootPath(""); // show drives at root
    fmTree = new QTreeView(fmLeftSplitter);

    fmTree->setModel(fmTreeModel);
    fmTree->setHeaderHidden(false);
    fmTree->header()->setStretchLastSection(true);
    fmTree->header()->setSectionResizeMode(QHeaderView::Interactive);
    // Persist fmTree column widths immediately when resized
    connect(fmTree->header(), &QHeaderView::sectionResized, this, [this](int logical, int /*oldSize*/, int newSize){
        QSettings s("AugmentCode", "KAssetManager");
        s.setValue(QString("FileManager/Tree/Col%1").arg(logical), newSize);
    });

    fmTree->setContextMenuPolicy(Qt::CustomContextMenu);
    fmTree->setExpandsOnDoubleClick(true);
    fmTree->setSelectionMode(QAbstractItemView::SingleSelection);
    fmTree->setStyleSheet(
        "QTreeView { background-color: #121212; color: #ffffff; border: none; }"
        "QTreeView::item:selected { background-color: #2f3a4a; color: #ffffff; }"
        "QHeaderView::section { background-color: #1a1a1a; color: #ffffff; border: none; padding: 4px; }"
    );
    if (m_host) {
        connect(fmTree, &QTreeView::clicked, this, &FileManagerWidget::onFmTreeActivated);
        connect(fmTree, &QTreeView::customContextMenuRequested, this, &FileManagerWidget::onFmTreeContextMenu);
    }
    fmTree->setDragEnabled(true);
    fmTree->setAcceptDrops(true);
    fmTree->setDropIndicatorShown(true);
    fmTree->setDragDropMode(QAbstractItemView::DragDrop);
    fmTree->viewport()->installEventFilter(this);

    fmTree->setRootIndex(fmTreeModel->index(fmTreeModel->rootPath()));

    leftLayout->addWidget(fmLeftSplitter);

    QWidget *right = new QWidget(fmSplitter);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0,0,0,0);
    rightLayout->setSpacing(0);

    fmToolbar = new QWidget(right);
    fmToolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fmToolbar->setFixedHeight(48);

    QHBoxLayout *tb = new QHBoxLayout(fmToolbar);
    tb->setContentsMargins(8,6,8,6);
    tb->setSpacing(8);

    fmIsGridMode = true;

    auto mkTb = [&](const QIcon &ic, const QString &tip) {
        QToolButton *b = new QToolButton(fmToolbar);
        b->setIcon(ic); b->setToolTip(tip); b->setAutoRaise(true); b->setIconSize(QSize(28,28));
        return b;
    };

    fmBackButton = mkTb(icoBack(), "Back");
    if (m_host) connect(fmBackButton, &QToolButton::clicked, this, &FileManagerWidget::onFmNavigateBack);
    tb->addWidget(fmBackButton);

    fmUpButton = mkTb(icoUp(), "Up");
    if (m_host) connect(fmUpButton, &QToolButton::clicked, this, &FileManagerWidget::onFmNavigateUp);
    tb->addWidget(fmUpButton);

    QToolButton *refreshButton = mkTb(icoRefresh(), "Refresh");
    if (m_host) connect(refreshButton, &QToolButton::clicked, this, &FileManagerWidget::onFmRefresh);
    tb->addWidget(refreshButton);

    QToolButton *newFolderBtn = mkTb(icoFolderNew(), "New Folder");
    if (m_host) connect(newFolderBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmNewFolder);
    tb->addWidget(newFolderBtn);

    QToolButton *copyBtn = mkTb(icoCopy(), "Copy"); if (m_host) connect(copyBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmCopy); tb->addWidget(copyBtn);
    QToolButton *cutBtn = mkTb(icoCut(), "Cut"); if (m_host) connect(cutBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmCut); tb->addWidget(cutBtn);
    QToolButton *pasteBtn = mkTb(icoPaste(), "Paste"); if (m_host) connect(pasteBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmPaste); tb->addWidget(pasteBtn);
    QToolButton *deleteBtn = mkTb(icoDelete(), "Delete"); if (m_host) connect(deleteBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmDelete); tb->addWidget(deleteBtn);
    QToolButton *renameBtn = mkTb(icoRename(), "Rename"); if (m_host) connect(renameBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmRename); tb->addWidget(renameBtn);

    tb->addSpacing(12);

    QToolButton *addToLibraryBtn = mkTb(icoAdd(), "Add to Library");
    if (m_host) connect(addToLibraryBtn, &QToolButton::clicked, m_host, &MainWindow::onAddSelectionToAssetLibrary);
    tb->addWidget(addToLibraryBtn);

    tb->addSpacing(12);

    fmViewModeButton = mkTb(icoGrid(), "Grid/List");
    if (m_host) connect(fmViewModeButton, &QToolButton::clicked, this, &FileManagerWidget::onFmViewModeToggled);
    tb->addWidget(fmViewModeButton);

    fmThumbnailSizeSlider = new QSlider(Qt::Horizontal, fmToolbar);
    fmThumbnailSizeSlider->setRange(64, 256);
    fmThumbnailSizeSlider->setValue(160);
    fmThumbnailSizeSlider->setFixedWidth(160);
    if (m_host) connect(fmThumbnailSizeSlider, &QSlider::valueChanged, this, &FileManagerWidget::onFmThumbnailSizeChanged);
    tb->addWidget(fmThumbnailSizeSlider);

    fmGroupSequencesCheckBox = mkTb(icoGroup(), "Group Sequences");
    fmGroupSequencesCheckBox->setCheckable(true);
    fmGroupSequencesCheckBox->setChecked(true);
    if (m_host) connect(fmGroupSequencesCheckBox, &QToolButton::toggled, this, &FileManagerWidget::onFmGroupSequencesToggled);
    tb->addWidget(fmGroupSequencesCheckBox);

    fmHideFoldersCheckBox = mkTb(icoHide(), "Hide Folders");
    fmHideFoldersCheckBox->setCheckable(true);
    fmHideFoldersCheckBox->setChecked(false);
    if (m_host) connect(fmHideFoldersCheckBox, &QToolButton::toggled, this, &FileManagerWidget::onFmHideFoldersToggled);
    tb->addWidget(fmHideFoldersCheckBox);

    tb->addStretch();

    fmPreviewToggleButton = mkTb(icoEye(), "Toggle Preview Panel");
    fmPreviewToggleButton->setCheckable(true);
    fmPreviewToggleButton->setChecked(true);
    if (m_host) connect(fmPreviewToggleButton, &QToolButton::toggled, m_host, &MainWindow::onFmTogglePreview);
    tb->addWidget(fmPreviewToggleButton);

    rightLayout->addWidget(fmToolbar);

    // Models/views
    fmViewStack = new QStackedWidget(right);

    fmDirModel = new QFileSystemModel(fmViewStack);
    fmDirModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    fmDirModel->setRootPath("");
    fmDirModel->setIconProvider(new FmIconProvider());

    // Sequence grouping proxy
    fmProxyModel = new SequenceGroupingProxyModel(fmViewStack);
    fmProxyModel->setSourceModel(fmDirModel);
    fmProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    fmProxyModel->setSortRole(Qt::DisplayRole);
    fmProxyModel->setDynamicSortFilter(true);
    fmProxyModel->sort(0, Qt::AscendingOrder);

    // Grid view
    fmGridView = new FmGridViewEx(fmProxyModel, fmDirModel, fmViewStack);
    fmGridView->setModel(fmProxyModel);
    fmGridView->setViewMode(QListView::IconMode);
    fmGridView->setResizeMode(QListView::Adjust);
    fmGridView->setSpacing(4);
    fmGridView->setUniformItemSizes(false);
    fmGridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fmGridView->setContextMenuPolicy(Qt::CustomContextMenu);
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
    fmGridView->setStyleSheet("QListView { background-color: #0a0a0a; border: none; }");
    fmGridView->setDragEnabled(true);
    fmGridView->setAcceptDrops(true);
    fmGridView->setDropIndicatorShown(true);
    fmGridView->setDragDropMode(QAbstractItemView::DragDrop);
    if (fmGridView->viewport()) fmGridView->viewport()->installEventFilter(this);
    if (m_host) connect(fmGridView, &QListView::doubleClicked, m_host, &MainWindow::onFmItemDoubleClicked);
    connect(fmGridView, &QListView::customContextMenuRequested, this, &FileManagerWidget::onFmShowContextMenu);
    fmViewStack->addWidget(fmGridView);

    // List view
    fmListView = new FmListViewEx(fmProxyModel, fmDirModel, fmViewStack);
    fmListView->setModel(fmProxyModel);
    fmListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    fmListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fmListView->setContextMenuPolicy(Qt::CustomContextMenu);
    if (m_host) connect(fmListView, &QTableView::doubleClicked, m_host, &MainWindow::onFmItemDoubleClicked);
    connect(fmListView, &QTableView::customContextMenuRequested, this, &FileManagerWidget::onFmShowContextMenu);
    fmViewStack->addWidget(fmListView);

    rightLayout->addWidget(fmViewStack);

    fmViewStack = new QStackedWidget(right);
    rightLayout->addWidget(fmViewStack);
}

void FileManagerWidget::bindHostPointers()
{
    if (!m_host) return;
    m_host->fmSplitter = fmSplitter;
    m_host->fmProxyModel = fmProxyModel;
    m_host->fmGroupSequencesCheckBox = fmGroupSequencesCheckBox;
    m_host->fmGroupSequences = fmGroupSequences;
    m_host->fmHideFoldersCheckBox = fmHideFoldersCheckBox;
    m_host->fmHideFolders = fmHideFolders;
    m_host->fmLeftSplitter = fmLeftSplitter;
    m_host->fmRightSplitter = fmRightSplitter;
    m_host->fmPreviewInfoSplitter = fmPreviewInfoSplitter;
    m_host->fmFavoritesList = fmFavoritesList;
    m_host->fmTree = fmTree;
    m_host->fmTreeModel = fmTreeModel;
    m_host->fmDirModel = fmDirModel;
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


void FileManagerWidget::onFmTreeActivated(const QModelIndex &index)
{
    if (fmTreeModel && index.isValid()) {
        const QString path = fmTreeModel->filePath(index);
        if (!path.isEmpty()) emit navigateToPathRequested(path, true);
    }
}

void FileManagerWidget::onFmViewModeToggled()
{
    if (!fmViewStack) return;
    fmIsGridMode = !fmIsGridMode;
    fmViewStack->setCurrentIndex(fmIsGridMode ? 0 : 1);
}

void FileManagerWidget::onFmThumbnailSizeChanged(int size)
{
    if (fmGridView) {
        fmGridView->setIconSize(QSize(size, size));
        fmGridView->setGridSize(QSize(size + 24, size + 40));
        if (auto *d = dynamic_cast<FmItemDelegate*>(fmGridView->itemDelegate())) d->setThumbnailSize(size);
        fmGridView->viewport()->update();
    }
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/GridThumbSize", size);
}

void FileManagerWidget::onFmGroupSequencesToggled(bool checked)
{
    fmGroupSequences = checked;
    if (fmProxyModel) fmProxyModel->setGroupingEnabled(checked);
}

void FileManagerWidget::onFmHideFoldersToggled(bool checked)
{
    fmHideFolders = checked;
    if (fmProxyModel) fmProxyModel->setHideFolders(checked);
}


// Helpers local to this translation unit for selection mapping and favorites actions
namespace {
    static QStringList fm_selectedPaths(QFileSystemModel *model, QListView *grid, QTableView *list, QStackedWidget *stack)
    {
        QStringList out;
        auto mapToSource = [](const QModelIndex &viewIdx) -> QModelIndex {
            if (!viewIdx.isValid()) return viewIdx;
            auto proxy = qobject_cast<const QSortFilterProxyModel*>(viewIdx.model());
            if (proxy) return proxy->mapToSource(viewIdx);
            return viewIdx;
        };
        if (!model || !grid || !list || !stack) return out;
        if (stack->currentIndex() == 0) {
            const auto idxs = grid->selectionModel() ? grid->selectionModel()->selectedIndexes() : QModelIndexList{};
            for (const QModelIndex &idx : idxs) {
                if (idx.column() != 0) continue;
                QModelIndex src = mapToSource(idx);
                if (src.isValid()) out << model->filePath(src);
            }
        } else {
            const auto rows = list->selectionModel() ? list->selectionModel()->selectedRows() : QModelIndexList{};
            for (const QModelIndex &idx : rows) {
                QModelIndex src = mapToSource(idx);
                if (src.isValid()) out << model->filePath(src);
            }
        }
        out.removeDuplicates();
        return out;
    }
}

void FileManagerWidget::onFmAddToFavorites()
{
    const QStringList sel = fm_selectedPaths(fmDirModel, reinterpret_cast<QListView*>(fmGridView), reinterpret_cast<QTableView*>(fmListView), fmViewStack);
    if (sel.isEmpty()) return;
    bool changed = false;
    for (const QString &p : sel) {
        if (!fmFavorites.contains(p)) { fmFavorites << p; changed = true; }
    }
    if (!changed) return;
    fmFavorites.removeDuplicates();

    // Refresh list widget
    if (fmFavoritesList) {
        fmFavoritesList->clear();
        for (const QString &p : fmFavorites) {
            QListWidgetItem *it = new QListWidgetItem(QIcon::fromTheme("star"), QFileInfo(p).fileName());
            it->setToolTip(p);
            it->setData(Qt::UserRole, p);
            fmFavoritesList->addItem(it);
        }
    }

    // Persist
    QSettings s("AugmentCode", "KAssetManager");
    s.beginWriteArray("FileManager/Favorites");
    for (int i=0;i<fmFavorites.size();++i) { s.setArrayIndex(i); s.setValue("path", fmFavorites.at(i)); }
    s.endArray();
}

void FileManagerWidget::onFmRemoveFavorite()
{
    if (!fmFavoritesList) return;
    QListWidgetItem *it = fmFavoritesList->currentItem();
    if (!it) return;
    const QString path = it->data(Qt::UserRole).toString();
    fmFavorites.removeAll(path);
    delete it;

    QSettings s("AugmentCode", "KAssetManager");
    s.beginWriteArray("FileManager/Favorites");
    for (int i=0;i<fmFavorites.size();++i) { s.setArrayIndex(i); s.setValue("path", fmFavorites.at(i)); }
    s.endArray();
}

void FileManagerWidget::onFmFavoriteActivated(QListWidgetItem* item)
{
    if (!item) return;
    const QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;
    emit navigateToPathRequested(path, true);
}

void FileManagerWidget::onFmTreeContextMenu(const QPoint &pos)
{
    if (!fmTree || !fmTreeModel) return;
    QModelIndex idx = fmTree->indexAt(pos);
    if (!idx.isValid()) return;
    const QString path = fmTreeModel->filePath(idx);
    if (path.isEmpty()) return;

    QMenu menu;
    QAction *refreshA = menu.addAction("Refresh");
    menu.addSeparator();
    QAction *copyA = menu.addAction("Copy");
    QAction *cutA = menu.addAction("Cut");
    QAction *pasteA = menu.addAction("Paste");
    menu.addSeparator();
    QAction *renameA = menu.addAction("Rename");
    QAction *delA = menu.addAction("Delete (Recycle Bin)");
    QAction *permDelA = menu.addAction("Permanent Delete (Shift+Delete)");
    QAction *newFolderA = menu.addAction("New Folder");
    QAction *createFolderWithSelA = menu.addAction("Create Folder with Selected Files");

    // Enable states
    const bool hasClipboard = !fmClipboard.isEmpty();
    pasteA->setEnabled(hasClipboard);

    QAction *chosen = menu.exec(fmTree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    auto selectedTreePaths = [&]() {
        QStringList out;
        auto sel = fmTree->selectionModel();
        if (!sel) return out;
        const auto rows = sel->selectedRows();
        for (const QModelIndex &r : rows) out << fmTreeModel->filePath(r);
        out.removeDuplicates();
        return out;
    };

    if (chosen == refreshA) {
        onFmRefresh();
    } else if (chosen == copyA) {
        fmClipboard = selectedTreePaths();
        fmClipboardCutMode = false;
    } else if (chosen == cutA) {
        fmClipboard = selectedTreePaths();
        fmClipboardCutMode = true;
    } else if (chosen == pasteA) {
        // Paste into specific tree folder
        if (!fmClipboard.isEmpty()) {
            if (m_host) m_host->releaseAnyPreviewLocksForPaths(fmClipboard);
            auto &q = FileOpsQueue::instance();
            if (fmClipboardCutMode) q.enqueueMove(fmClipboard, path); else q.enqueueCopy(fmClipboard, path);
            if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
            fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
            fmClipboard.clear(); fmClipboardCutMode = false;
        }
    } else if (chosen == delA) {
        QStringList paths = selectedTreePaths();
        if (paths.isEmpty()) return;
        if (m_host) m_host->releaseAnyPreviewLocksForPaths(paths);
        FileOpsQueue::instance().enqueueDelete(paths);
    } else if (chosen == permDelA) {
        QStringList paths = selectedTreePaths();
        if (paths.isEmpty()) return;
        if (m_host) m_host->doPermanentDelete(paths);
    } else if (chosen == renameA) {
        QStringList paths = selectedTreePaths();
        if (paths.size() != 1) return;
        QFileInfo fi(paths.first());
        bool ok=false;
        QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, fi.fileName(), &ok);
        if (!ok || newName.trimmed().isEmpty()) return;
        QDir parent(fi.absolutePath());
        parent.rename(fi.fileName(), newName.trimmed());
    } else if (chosen == newFolderA) {
        QDir dir(path);
        QString newPath = fm_uniqueNameInDir(path, "New Folder");
        dir.mkpath(newPath);
    } else if (chosen == createFolderWithSelA) {
        // Use selection from main view, create folder inside tree path
        const QStringList files = fm_selectedPaths(fmDirModel, reinterpret_cast<QListView*>(fmGridView), reinterpret_cast<QTableView*>(fmListView), fmViewStack);
        if (files.isEmpty()) return;
        bool ok=false;
        QString folderName = QInputDialog::getText(this, "Create Folder", "Enter folder name:", QLineEdit::Normal, "New Folder", &ok);
        if (!ok) return;
        folderName = folderName.trimmed();
        if (folderName.isEmpty()) return;
        QDir dd(path);
        QString folderPath = dd.filePath(folderName);
        if (QFileInfo::exists(folderPath)) { int i=2; QString base = folderName; while (QFileInfo::exists(folderPath)) { folderName = QString("%1 (%2)").arg(base).arg(i++); folderPath = dd.filePath(folderName);} }
        if (!dd.mkpath(folderPath)) { QMessageBox::warning(this, "Error", QString("Failed to create folder: %1").arg(folderPath)); return; }
        if (m_host) m_host->releaseAnyPreviewLocksForPaths(files);
        FileOpsQueue::instance().enqueueMove(files, folderPath);
        if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
        fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
    }
}

void FileManagerWidget::onFmShowContextMenu(const QPoint &pos)
{
    QWidget *senderW = qobject_cast<QWidget*>(sender());
    if (!senderW || !fmDirModel || !fmViewStack) return;
    QPoint globalPos = senderW->mapToGlobal(pos);

    QMenu menu;
    QAction *refreshA = menu.addAction("Refresh", this, &FileManagerWidget::onFmRefresh, QKeySequence(Qt::Key_F5));
    menu.addSeparator();
    QAction *copyA = menu.addAction("Copy", this, &FileManagerWidget::onFmCopy, QKeySequence::Copy);
    QAction *cutA = menu.addAction("Cut", this, &FileManagerWidget::onFmCut, QKeySequence::Cut);
    QAction *pasteA = menu.addAction("Paste", this, &FileManagerWidget::onFmPaste, QKeySequence::Paste);
    menu.addSeparator();
    QAction *renameA = menu.addAction("Rename", this, &FileManagerWidget::onFmRename, QKeySequence(Qt::Key_F2));
    QAction *bulkRenameA = menu.addAction("Bulk Rename...");
    QAction *delA = menu.addAction("Delete", this, &FileManagerWidget::onFmDelete, QKeySequence::Delete);
    QAction *createFolderWithSel = menu.addAction("Create Folder with Selected Files", this, &FileManagerWidget::onFmCreateFolderWithSelected);
    menu.addSeparator();
    QAction *addLibA = nullptr;
    if (m_host)
        addLibA = menu.addAction("Add to Asset Library", m_host, &MainWindow::onAddSelectionToAssetLibrary);

    QAction *favA = menu.addAction("Add to Favorites", this, &FileManagerWidget::onFmAddToFavorites);

    menu.addSeparator();
    QAction *openInExplorerA = menu.addAction("Open in Explorer");
    QAction *propertiesA = menu.addAction("Properties");
    QAction *openWithA = menu.addAction("Open With...");

    // Enable/disable depending on selection
    QStringList selectedPaths = fm_selectedPaths(fmDirModel, reinterpret_cast<QListView*>(fmGridView), reinterpret_cast<QTableView*>(fmListView), fmViewStack);
    bool hasSel = !selectedPaths.isEmpty();
    int selCount = selectedPaths.size();

    copyA->setEnabled(hasSel);
    cutA->setEnabled(hasSel);
    renameA->setEnabled(selCount == 1);
    bulkRenameA->setEnabled(selCount >= 2);
    delA->setEnabled(hasSel);
    pasteA->setEnabled(!fmClipboard.isEmpty());
    if (addLibA) addLibA->setEnabled(hasSel);
    favA->setEnabled(hasSel);
    createFolderWithSel->setEnabled(hasSel);
    openInExplorerA->setEnabled(selCount == 1);
    propertiesA->setEnabled(selCount == 1);

    // Optional conversion action if all selected are supported media
    QAction *convertA = nullptr;
    if (hasSel) {
        auto isSupportedExt = [](const QString &ext){
            static const QSet<QString> img{ "png","jpg","jpeg","tif","tiff","exr","iff","psd" };
            static const QSet<QString> vid{ "mov","mxf","mp4","avi","mp5" };
            return img.contains(ext) || vid.contains(ext);
        };
        bool allSupported = true;
        for (const QString &p : selectedPaths) {
            QFileInfo fi(p);
            if (!fi.exists() || fi.isDir()) { allSupported = false; break; }
            if (!isSupportedExt(fi.suffix().toLower())) { allSupported = false; break; }
        }
        if (allSupported) convertA = menu.addAction("Convert to Format...");
    }

    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == convertA) {
        if (m_host) m_host->releaseAnyPreviewLocksForPaths(selectedPaths);
        auto *dlg = new MediaConvertDialog(selectedPaths, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, this, &FileManagerWidget::onFmRefresh);
        connect(dlg, &QObject::destroyed, this, [this](){ QTimer::singleShot(100, this, &FileManagerWidget::onFmRefresh); });
        dlg->show(); dlg->raise(); dlg->activateWindow();
        return;
    }

    if (chosen == openInExplorerA && selCount == 1) {
        QString path = selectedPaths.first();
        QProcess::startDetached("explorer.exe", QStringList() << "/select," << QDir::toNativeSeparators(path));
    } else if (chosen == propertiesA && selCount == 1) {
        QString path = selectedPaths.first();
#ifdef Q_OS_WIN
        std::wstring wpath = path.toStdWString();
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"properties";
        sei.lpFile = wpath.c_str();
        sei.nShow = SW_SHOW;
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        ShellExecuteExW(&sei);
#endif
    } else if (chosen == openWithA && selCount == 1) {
        QString path = selectedPaths.first();
#ifdef Q_OS_WIN
        std::wstring wpath = path.toStdWString();
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"openas";
        sei.lpFile = wpath.c_str();
        sei.nShow = SW_SHOW;
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        ShellExecuteExW(&sei);
#endif
    }
}


static QString fm_uniqueNameInDir(const QString &dirPath, const QString &baseName)
{
    QFileInfo fi(dirPath + QDir::separator() + baseName);
    if (!fi.exists()) return fi.absoluteFilePath();
    QString name = fi.completeBaseName();
    QString ext = fi.completeSuffix();
    int n = 2;
    while (true) {
        QString candidate = name + QString(" (%1)").arg(n);
        if (!ext.isEmpty()) candidate += "." + ext;
        QFileInfo fi2(dirPath + QDir::separator() + candidate);
        if (!fi2.exists()) return fi2.absoluteFilePath();
        ++n;
    }
}

void FileManagerWidget::onFmRefresh()
{
    if (!fmDirModel) return;
    const QString currentPath = fmDirModel->rootPath();
    if (currentPath.isEmpty()) return;
    fmDirModel->setRootPath("");
    fmDirModel->setRootPath(currentPath);
    if (fmProxyModel) {
        fmProxyModel->rebuildForRoot(currentPath);
        QModelIndex srcRoot = fmDirModel->index(currentPath);
        QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
        if (fmGridView) fmGridView->setRootIndex(proxyRoot);
        if (fmListView) fmListView->setRootIndex(proxyRoot);
    } else {
        QModelIndex srcRoot = fmDirModel->index(currentPath);
        if (fmGridView) fmGridView->setRootIndex(srcRoot);
        if (fmListView) fmListView->setRootIndex(srcRoot);
    }
}

void FileManagerWidget::onFmNewFolder()
{
    if (!fmDirModel) return;
    const QString destDir = fmDirModel->rootPath();
    if (destDir.isEmpty()) return;
    QString path = fm_uniqueNameInDir(destDir, "New Folder");
    QDir().mkpath(path);
}

void FileManagerWidget::onFmRename()
{
    if (!fmDirModel || !fmViewStack) return;
    const QStringList paths = fm_selectedPaths(fmDirModel, reinterpret_cast<QListView*>(fmGridView), reinterpret_cast<QTableView*>(fmListView), fmViewStack);
    if (paths.size() != 1) return;
    const QString p = paths.first();
    if (m_host) m_host->releaseAnyPreviewLocksForPaths(QStringList{p});
    QFileInfo fi(p);
    bool ok=false;
    QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, fi.fileName(), &ok);
    if (!ok || newName.trimmed().isEmpty()) return;
    QString dest = fi.absolutePath() + QDir::separator() + newName.trimmed();
    if (fi.isDir()) {
        QDir parent(fi.absolutePath());
        parent.rename(fi.fileName(), newName.trimmed());
    } else {
        QFile::rename(p, dest);
    }
}

void FileManagerWidget::onFmCopy()
{
    fmClipboard = fm_selectedPaths(fmDirModel, reinterpret_cast<QListView*>(fmGridView), reinterpret_cast<QTableView*>(fmListView), fmViewStack);
    fmClipboardCutMode = false;
}

void FileManagerWidget::onFmCut()
{
    fmClipboard = fm_selectedPaths(fmDirModel, reinterpret_cast<QListView*>(fmGridView), reinterpret_cast<QTableView*>(fmListView), fmViewStack);
    fmClipboardCutMode = true;
}

void FileManagerWidget::onFmPaste()
{
    if (!fmDirModel || fmClipboard.isEmpty()) return;
    const QString destDir = fmDirModel->rootPath();
    if (destDir.isEmpty()) return;
    if (m_host) m_host->releaseAnyPreviewLocksForPaths(fmClipboard);
    auto &q = FileOpsQueue::instance();
    if (fmClipboardCutMode) q.enqueueMove(fmClipboard, destDir);
    else q.enqueueCopy(fmClipboard, destDir);
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
}

void FileManagerWidget::onFmDelete()
{
    if (!fmDirModel) return;
    QStringList paths = fm_selectedPaths(fmDirModel, reinterpret_cast<QListView*>(fmGridView), reinterpret_cast<QTableView*>(fmListView), fmViewStack);
    if (paths.isEmpty()) return;
    if (m_host) m_host->releaseAnyPreviewLocksForPaths(paths);
    FileOpsQueue::instance().enqueueDelete(paths);
}

void FileManagerWidget::onFmDeletePermanent()
{
    if (!fmDirModel) return;
    QStringList paths = fm_selectedPaths(fmDirModel, reinterpret_cast<QListView*>(fmGridView), reinterpret_cast<QTableView*>(fmListView), fmViewStack);
    if (paths.isEmpty()) return;
    if (m_host) m_host->releaseAnyPreviewLocksForPaths(paths);
    FileOpsQueue::instance().enqueueDeletePermanent(paths);
}

void FileManagerWidget::onFmCreateFolderWithSelected()
{
    if (!fmDirModel) return;
    QStringList paths = fm_selectedPaths(fmDirModel, reinterpret_cast<QListView*>(fmGridView), reinterpret_cast<QTableView*>(fmListView), fmViewStack);
    if (paths.isEmpty()) return;
    const QString destDir = fmDirModel->rootPath();
    bool ok=false;
    QString folderName = QInputDialog::getText(this, "Create Folder", "Enter folder name:", QLineEdit::Normal, "New Folder", &ok);
    if (!ok) return;
    folderName = folderName.trimmed();
    if (folderName.isEmpty()) return;
    QDir dd(destDir);
    QString folderPath = dd.filePath(folderName);
    if (QFileInfo::exists(folderPath)) {
        int i=2; QString base = folderName; while (QFileInfo::exists(folderPath)) { folderName = QString("%1 (%2)").arg(base).arg(i++); folderPath = dd.filePath(folderName);}    }
    if (!dd.mkpath(folderPath)) { QMessageBox::warning(this, "Error", QString("Failed to create folder: %1").arg(folderPath)); return; }
    if (m_host) m_host->releaseAnyPreviewLocksForPaths(paths);
    FileOpsQueue::instance().enqueueMove(paths, folderPath);
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
}


void FileManagerWidget::onFmNavigateBack()
{
    if (m_host) m_host->onFmNavigateBack();
}

void FileManagerWidget::onFmNavigateUp()
{
    if (m_host) m_host->onFmNavigateUp();
}
