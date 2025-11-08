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
#include <QSortFilterProxyModel>
#include <QFileInfo>

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
    QLabel *favHeader = new QLabel("★ Favorites", favContainer);
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
        connect(fmTree, &QTreeView::customContextMenuRequested, m_host, &MainWindow::onFmTreeContextMenu);
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
    if (m_host) connect(fmBackButton, &QToolButton::clicked, m_host, &MainWindow::onFmNavigateBack);
    tb->addWidget(fmBackButton);

    fmUpButton = mkTb(icoUp(), "Up");
    if (m_host) connect(fmUpButton, &QToolButton::clicked, m_host, &MainWindow::onFmNavigateUp);
    tb->addWidget(fmUpButton);

    QToolButton *refreshButton = mkTb(icoRefresh(), "Refresh");
    if (m_host) connect(refreshButton, &QToolButton::clicked, m_host, &MainWindow::onFmRefresh);
    tb->addWidget(refreshButton);

    QToolButton *newFolderBtn = mkTb(icoFolderNew(), "New Folder");
    if (m_host) connect(newFolderBtn, &QToolButton::clicked, m_host, &MainWindow::onFmNewFolder);
    tb->addWidget(newFolderBtn);

    QToolButton *copyBtn = mkTb(icoCopy(), "Copy"); if (m_host) connect(copyBtn, &QToolButton::clicked, m_host, &MainWindow::onFmCopy); tb->addWidget(copyBtn);
    QToolButton *cutBtn = mkTb(icoCut(), "Cut"); if (m_host) connect(cutBtn, &QToolButton::clicked, m_host, &MainWindow::onFmCut); tb->addWidget(cutBtn);
    QToolButton *pasteBtn = mkTb(icoPaste(), "Paste"); if (m_host) connect(pasteBtn, &QToolButton::clicked, m_host, &MainWindow::onFmPaste); tb->addWidget(pasteBtn);
    QToolButton *deleteBtn = mkTb(icoDelete(), "Delete"); if (m_host) connect(deleteBtn, &QToolButton::clicked, m_host, &MainWindow::onFmDelete); tb->addWidget(deleteBtn);
    QToolButton *renameBtn = mkTb(icoRename(), "Rename"); if (m_host) connect(renameBtn, &QToolButton::clicked, m_host, &MainWindow::onFmRename); tb->addWidget(renameBtn);

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
    fmViewStack->addWidget(fmGridView);

    // List view
    fmListView = new FmListViewEx(fmProxyModel, fmDirModel, fmViewStack);
    fmListView->setModel(fmProxyModel);
    fmListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    fmListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fmListView->setContextMenuPolicy(Qt::CustomContextMenu);
    if (m_host) connect(fmListView, &QTableView::doubleClicked, m_host, &MainWindow::onFmItemDoubleClicked);
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
