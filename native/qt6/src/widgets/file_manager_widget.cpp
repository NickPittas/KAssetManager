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

#include "oiio_image_loader.h"
#include "sequence_detector.h"
#include "video_metadata.h"
#include "office_preview.h"
#include "ffmpeg_video_reader.h"
#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}
#endif



#include <algorithm>
#include <QVideoFrame>


#include <QImageReader>
#include <QGraphicsSvgItem>
#include <QPainter>
#include <QTextStream>
#include <QTextOption>
#include <QStandardItem>


#include <QThread>


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

#include <QScrollBar>

#include <functional>

#include <QInputDialog>
#include <QMessageBox>
#include <QLineEdit>


static QString humanReadableSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024ll * 1024ll * 1024ll)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}

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
        connect(newFolderBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmNewFolder);
        connect(copyBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmCopy);
        connect(cutBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmCut);
        connect(pasteBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmPaste);
        connect(deleteBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmDelete);
        connect(renameBtn, &QToolButton::clicked, this, &FileManagerWidget::onFmRename);
        connect(fmBackButton, &QToolButton::clicked, this, &FileManagerWidget::onFmNavigateBack);
        connect(fmUpButton, &QToolButton::clicked, this, &FileManagerWidget::onFmNavigateUp);

        connect(addToLibraryBtn, &QToolButton::clicked, m_host, &MainWindow::onAddSelectionToAssetLibrary);
        connect(fmViewModeButton, &QToolButton::clicked, this, &FileManagerWidget::onFmViewModeToggled);
        connect(fmThumbnailSizeSlider, &QSlider::valueChanged, this, &FileManagerWidget::onFmThumbnailSizeChanged);
        connect(fmGroupSequencesCheckBox, &QToolButton::toggled, this, &FileManagerWidget::onFmGroupSequencesToggled);
        connect(fmHideFoldersCheckBox, &QToolButton::toggled, this, &FileManagerWidget::onFmHideFoldersToggled);
        connect(fmPreviewToggleButton, &QToolButton::toggled, this, &FileManagerWidget::onFmTogglePreview);
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
        if (fmGridView->selectionModel()) connect(fmGridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &FileManagerWidget::onFmSelectionChanged);
        if (fmListView->selectionModel()) connect(fmListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &FileManagerWidget::onFmSelectionChanged);
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
    setupShortcuts();

}

void FileManagerWidget::setupShortcuts()
{
    auto mk = [this](const QString& name, const QKeySequence& def, std::function<void()> handler) {
        QShortcut* sc = new QShortcut(this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        fmShortcutObjs.insert(name, sc);
        sc->setKey(def);
        connect(sc, &QShortcut::activated, this, [this, handler]() {
            if (shouldIgnoreShortcutFromFocus()) return;
            handler();
        });
    };

    mk("Copy", QKeySequence::Copy, [this](){ onFmCopy(); });
    mk("Cut", QKeySequence::Cut, [this](){ onFmCut(); });
    mk("Paste", QKeySequence::Paste, [this](){ onFmPaste(); });
    mk("Delete", QKeySequence::Delete, [this](){ onFmDelete(); });
    mk("Rename", QKeySequence(Qt::Key_F2), [this](){ onFmRename(); });
    mk("DeletePermanent", QKeySequence(Qt::SHIFT | Qt::Key_Delete), [this](){ onFmDeletePermanent(); });
    mk("NewFolder", QKeySequence::New, [this](){ onFmNewFolder(); });
    mk("CreateFolderWithSelected", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [this](){ onFmCreateFolderWithSelected(); });
    mk("BackToParent", QKeySequence(Qt::Key_Backspace), [this](){ onFmNavigateUp(); });

    {
        QShortcut* sc = new QShortcut(this);
        sc->setContext(Qt::WidgetWithChildrenShortcut);
        fmShortcutObjs.insert("OpenOverlay", sc);
        sc->setKey(QKeySequence(Qt::Key_Space));
        connect(sc, &QShortcut::activated, this, [this]() {
            if (shouldIgnoreShortcutFromFocus()) return;
            this->onFmOpenOverlay();
            /* fallback path if no host overlay exists */
            if (!m_host || !m_host->previewOverlay) {
                QAbstractItemView* view = fmIsGridMode ? static_cast<QAbstractItemView*>(fmGridView)
                                                       : static_cast<QAbstractItemView*>(fmListView);
                if (!view) return;
                QModelIndex idx = view->currentIndex();
                if (idx.isValid()) onFmItemDoubleClicked(idx);
            }
        });
    }

    applyFmShortcuts();
}

void FileManagerWidget::applyFmShortcuts()
{
    QSettings s("AugmentCode", "KAssetManager");
    s.beginGroup("FileManager/Shortcuts");
    for (auto it = fmShortcutObjs.begin(); it != fmShortcutObjs.end(); ++it) {
        const QString action = it.key();
        QShortcut* sc = it.value();
        if (!sc) continue;
        const QString stored = s.value(action).toString();
        if (!stored.isEmpty()) sc->setKey(QKeySequence(stored));
    }
    s.endGroup();
}

void FileManagerWidget::reapplyShortcutsFromSettings()
{
    applyFmShortcuts();
}


bool FileManagerWidget::shouldIgnoreShortcutFromFocus() const
{
    QWidget* fw = QApplication::focusWidget();
    if (!fw) return false;
    if (qobject_cast<QLineEdit*>(fw)) return true;
    if (fw->inherits("QTextEdit")) return true;
    if (fw->inherits("QPlainTextEdit")) return true;
    return false;
}

void FileManagerWidget::releaseAnyPreviewLocksForPaths(const QStringList& paths)
{
    if (m_host) { m_host->releaseAnyPreviewLocksForPaths(paths); return; }
    // Fallback: no-op. Preview/overlay is owned by MainWindow in hosted mode.
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
    m_host->fmVideoItem = fmVideoItem;
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

    m_host->fmMediaPlayer = fmMediaPlayer;
    m_host->fmAudioOutput = fmAudioOutput;
    m_host->fmPlayPauseBtn = fmPlayPauseBtn;
    m_host->fmPrevFrameBtn = fmPrevFrameBtn;
    m_host->fmNextFrameBtn = fmNextFrameBtn;
    m_host->fmPositionSlider = fmPositionSlider;
    m_host->fmTimeLabel = fmTimeLabel;
    m_host->fmVolumeSlider = fmVolumeSlider;
    m_host->fmColorSpaceCombo = fmColorSpaceCombo;
    m_host->fmColorSpaceLabel = fmColorSpaceLabel;
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
    // Apply new root to both views
    fmGridView->setRootIndex(rootIndex);
    fmListView->setRootIndex(rootIndex);

    // Reset list/grid scroll position to top when changing folders
    auto resetViewScrollTop = [](QAbstractItemView* v){
        if (!v) return;
        if (auto *sb = v->verticalScrollBar()) sb->setValue(sb->minimum());
        if (v->model() && v->model()->rowCount() > 0) {
            const QModelIndex first = v->model()->index(0, 0);
            if (first.isValid()) v->scrollTo(first, QAbstractItemView::PositionAtTop);
        }
    };
    resetViewScrollTop(fmGridView);
    resetViewScrollTop(fmListView);

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
    // Trigger visible-only thumbnail requests after navigation completes
    if (m_host) {
        QTimer::singleShot(50, m_host, [this]() {
            if (m_host) m_host->scheduleVisibleThumbProgressUpdate();
        });
    }

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
    if (fmViewStack)
        fmViewStack->setCurrentIndex(fmIsGridMode ? 0 : 1);
    if (fmViewModeButton)
        fmViewModeButton->setIcon(fmIsGridMode ? icoGrid() : icoList());

    // Keep the current folder when switching views
    if (fmDirModel) {
        const QString path = fmDirModel->rootPath();
        if (!path.isEmpty()) {
            QModelIndex srcRoot = fmDirModel->index(path);
            if (fmProxyModel) {
                fmProxyModel->rebuildForRoot(path);
                QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
                if (fmGridView) fmGridView->setRootIndex(proxyRoot);
                if (fmListView) fmListView->setRootIndex(proxyRoot);
            } else {
                if (fmGridView) fmGridView->setRootIndex(srcRoot);
                if (fmListView) fmListView->setRootIndex(srcRoot);
            }
        }
    }

    // Grid view always maintains ascending alphabetical sort with folders first
    if (fmIsGridMode && fmProxyModel) {
        fmProxyModel->sort(0, Qt::AscendingOrder);
    }

    // Persist immediately
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/ViewMode", fmIsGridMode);
    s.sync();
    if (m_host) m_host->scheduleVisibleThumbProgressUpdate();
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
    if (fmProxyModel) fmProxyModel->setGroupingEnabled(checked);

    // Update LivePreviewManager and scrub controller to reflect grouping state
    LivePreviewManager::instance().setSequenceDetectionEnabled(checked);
    if (m_host && m_host->fmScrubController)
        m_host->fmScrubController->setSequenceGroupingEnabled(checked);

    // Clear the cache to force regeneration of thumbnails with new settings
    LivePreviewManager::instance().clear();

    // Rebuild for current root
    if (fmDirModel && fmProxyModel) {
        QString rootPath = fmDirModel->rootPath();
        if (!rootPath.isEmpty()) fmProxyModel->rebuildForRoot(rootPath);
    }

    // Force complete repaint of grid view to regenerate thumbnails
    if (fmGridView) {
        fmGridView->viewport()->update();
        QTimer::singleShot(100, fmGridView->viewport(), [this]() {
            if (fmGridView) fmGridView->viewport()->update();
        });
        QTimer::singleShot(500, fmGridView->viewport(), [this]() {
            if (fmGridView) fmGridView->viewport()->update();
        });
    }

    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/GroupSequences", checked);
}

void FileManagerWidget::onFmHideFoldersToggled(bool checked)
{
    fmHideFolders = checked;
    if (fmDirModel) {
        // Preserve current path
        QString currentPath = fmDirModel->rootPath();

        // Update source model filter to hide/show folders in the listing
        QDir::Filters filters = QDir::NoDotAndDotDot | (checked ? QDir::Files : QDir::AllEntries);
        fmDirModel->setFilter(filters);

        // Rebuild proxy and restore root
        if (!currentPath.isEmpty()) {
            QModelIndex srcRoot = fmDirModel->index(currentPath);
            if (fmProxyModel) {
                fmProxyModel->rebuildForRoot(currentPath);
                QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
                if (fmGridView) fmGridView->setRootIndex(proxyRoot);
                if (fmListView) fmListView->setRootIndex(proxyRoot);
            } else {
                if (fmGridView) fmGridView->setRootIndex(srcRoot);
                if (fmListView) fmListView->setRootIndex(srcRoot);
            }
        }
    }
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/HideFolders", checked);
}

void FileManagerWidget::onFmRemoveFavorite()
{
    if (m_host) { m_host->onFmRemoveFavorite(); return; }
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
        onFmCopy();
    } else if (chosen == cutA) {
        onFmCut();
    } else if (chosen == pasteA) {
        onFmPaste();
    } else if (chosen == renameA && hasSelection) {
        onFmRename();
    } else if (chosen == bulkRenameA && hasSelection) {
        onFmBulkRename();
    } else if (chosen == createFolderWithSelA && hasSelection) {
        onFmCreateFolderWithSelected();
    } else if (chosen == deleteA && hasSelection) {
        onFmDelete();
    } else if (chosen == deletePermA && hasSelection) {
        onFmDeletePermanent();
    } else if (chosen == convertA && hasSelection) {
        if (m_host) m_host->releaseAnyPreviewLocksForPaths(selectedPaths);
        auto *dlg = new MediaConvertDialog(selectedPaths, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, this, &FileManagerWidget::onFmRefresh);
        connect(dlg, &QObject::destroyed, this, [this](){ QTimer::singleShot(100, this, &FileManagerWidget::onFmRefresh); });
        dlg->show();
    } else if (chosen == newFolderA) {
        onFmNewFolder();
    } else if (chosen == showInExplorerA) {
        const QString base = QSettings("AugmentCode","KAssetManager").value(fmSettingsKey("CurrentPath")).toString();
        if (!base.isEmpty()) DragUtils::instance().showInExplorer(base);
    } else if (chosen == refreshA) {
        onFmRefresh();
    }
}

void FileManagerWidget::onFmAddToFavorites()
{
    if (m_host) { m_host->onFmAddToFavorites(); return; }
    const QString path = QSettings("AugmentCode", "KAssetManager").value(fmSettingsKey("CurrentPath")).toString();
    if (path.isEmpty()) return;
    auto *it = new QListWidgetItem(QFileInfo(path).fileName(), fmFavoritesList);
    it->setData(Qt::UserRole, path);
}

void FileManagerWidget::onFmRefresh()
{
    if (!fmDirModel) return;

    QString currentPath = fmDirModel->rootPath();
    if (currentPath.isEmpty()) {
        // Refresh entire model
        fmDirModel->setRootPath("");
        if (fmTreeModel) fmTreeModel->setRootPath("");
        return;
    }

    // Clear cached thumbnails/previews
    LivePreviewManager::instance().clear();

    // Force QFileSystemModel to re-read directory by toggling root
    const QString tempPath = QDir::tempPath();
    fmDirModel->setRootPath(tempPath);
    fmDirModel->setRootPath(currentPath);

    // Also refresh the tree model
    if (fmTreeModel) fmTreeModel->setRootPath("");

    // Rebuild proxy for current root
    if (fmProxyModel) fmProxyModel->rebuildForRoot(currentPath);

    // Force viewport updates
    if (fmGridView) fmGridView->viewport()->update();
    if (fmListView) fmListView->viewport()->update();
}

void FileManagerWidget::onFmNewFolder()
{
    if (qobject_cast<QShortcut*>(sender())) { if (shouldIgnoreShortcutFromFocus()) return; }
    const QString destDir = fmDirModel ? fmDirModel->rootPath() : QString();
    if (destDir.isEmpty()) return;
    QString path = destDir + QDir::separator() + "New Folder";
    int n = 2; while (QFileInfo::exists(path)) path = destDir + QDir::separator() + QString("New Folder (%1)").arg(n++);
    QDir().mkpath(path);
}

void FileManagerWidget::onFmRename()
{
    if (qobject_cast<QShortcut*>(sender())) { if (shouldIgnoreShortcutFromFocus()) return; }
    QAbstractItemView* view = fmIsGridMode ? static_cast<QAbstractItemView*>(fmGridView)
                                           : static_cast<QAbstractItemView*>(fmListView);
    if (!view || !view->selectionModel()) return;
    QModelIndexList rows = view->selectionModel()->selectedRows();
    if (rows.size() != 1) return;
    QModelIndex idx = rows.first();
    QModelIndex src = (fmProxyModel && idx.model()==fmProxyModel) ? fmProxyModel->mapToSource(idx) : idx;
    const QString p = fmDirModel->filePath(src);

    releaseAnyPreviewLocksForPaths(QStringList{p});
    QFileInfo fi(p);
    bool ok = false;
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
    if (qobject_cast<QShortcut*>(sender())) { if (shouldIgnoreShortcutFromFocus()) return; }
    QStringList paths;
    if (fmIsGridMode && fmGridView && fmGridView->selectionModel()) {
        const auto idxs = fmGridView->selectionModel()->selectedIndexes();
        for (const QModelIndex &i : idxs) {
            if (i.column() != 0) continue;
            QModelIndex src = (fmProxyModel && i.model()==fmProxyModel) ? fmProxyModel->mapToSource(i) : i;
            paths << fmDirModel->filePath(src);
        }
    } else if (fmListView && fmListView->selectionModel()) {
        const auto rows = fmListView->selectionModel()->selectedRows();
        for (const QModelIndex &i : rows) {
            QModelIndex src = (fmProxyModel && i.model()==fmProxyModel) ? fmProxyModel->mapToSource(i) : i;
            paths << fmDirModel->filePath(src);
        }
    }
    paths.removeDuplicates();
    fmClipboard = paths;
    fmClipboardCutMode = false;
}

void FileManagerWidget::onFmCut()
{
    if (qobject_cast<QShortcut*>(sender())) { if (shouldIgnoreShortcutFromFocus()) return; }
    onFmCopy();
    fmClipboardCutMode = true;
}

void FileManagerWidget::onFmPaste()
{
    if (qobject_cast<QShortcut*>(sender())) { if (shouldIgnoreShortcutFromFocus()) return; }
    if (fmClipboard.isEmpty()) return;
    const QString destDir = fmDirModel ? fmDirModel->rootPath() : QString();
    if (destDir.isEmpty()) return;

    // Ensure any preview locks are released before file ops
    releaseAnyPreviewLocksForPaths(fmClipboard);

    auto &q = FileOpsQueue::instance();
    if (fmClipboardCutMode) q.enqueueMove(fmClipboard, destDir);
    else q.enqueueCopy(fmClipboard, destDir);

    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(m_host ? static_cast<QWidget*>(m_host) : static_cast<QWidget*>(this));
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();

    fmClipboard.clear();
    fmClipboardCutMode = false;
}

void FileManagerWidget::onFmDelete()
{
    if (qobject_cast<QShortcut*>(sender())) { if (shouldIgnoreShortcutFromFocus()) return; }
    QStringList paths;
    if (fmIsGridMode && fmGridView && fmGridView->selectionModel()) {
        const auto idxs = fmGridView->selectionModel()->selectedIndexes();
        for (const QModelIndex &i : idxs) {
            if (i.column() != 0) continue;
            QModelIndex src = (fmProxyModel && i.model()==fmProxyModel) ? fmProxyModel->mapToSource(i) : i;
            paths << fmDirModel->filePath(src);
        }
    } else if (fmListView && fmListView->selectionModel()) {
        const auto rows = fmListView->selectionModel()->selectedRows();
        for (const QModelIndex &i : rows) {
            QModelIndex src = (fmProxyModel && i.model()==fmProxyModel) ? fmProxyModel->mapToSource(i) : i;
            paths << fmDirModel->filePath(src);
        }
    }
    if (paths.isEmpty()) return;
    releaseAnyPreviewLocksForPaths(paths);
    FileOpsQueue::instance().enqueueDelete(paths);
}

void FileManagerWidget::onFmDeletePermanent()
{
    if (qobject_cast<QShortcut*>(sender())) { if (shouldIgnoreShortcutFromFocus()) return; }
    QStringList paths;
    if (fmIsGridMode && fmGridView && fmGridView->selectionModel()) {
        const auto idxs = fmGridView->selectionModel()->selectedIndexes();
        for (const QModelIndex &i : idxs) {
            if (i.column() != 0) continue;
            QModelIndex src = (fmProxyModel && i.model()==fmProxyModel) ? fmProxyModel->mapToSource(i) : i;
            paths << fmDirModel->filePath(src);
        }
    } else if (fmListView && fmListView->selectionModel()) {
        const auto rows = fmListView->selectionModel()->selectedRows();
        for (const QModelIndex &i : rows) {
            QModelIndex src = (fmProxyModel && i.model()==fmProxyModel) ? fmProxyModel->mapToSource(i) : i;
            paths << fmDirModel->filePath(src);
        }
    }
    if (paths.isEmpty()) return;
    releaseAnyPreviewLocksForPaths(paths);
    FileOpsQueue::instance().enqueueDeletePermanent(paths);
}

void FileManagerWidget::onFmCreateFolderWithSelected()
{
    if (qobject_cast<QShortcut*>(sender())) { if (shouldIgnoreShortcutFromFocus()) return; }

    // Collect selection
    QStringList paths;
    if (fmIsGridMode && fmGridView && fmGridView->selectionModel()) {
        const auto idxs = fmGridView->selectionModel()->selectedIndexes();
        for (const QModelIndex &i : idxs) {
            if (i.column() != 0) continue;
            QModelIndex src = (fmProxyModel && i.model()==fmProxyModel) ? fmProxyModel->mapToSource(i) : i;
            paths << fmDirModel->filePath(src);
        }
    } else if (fmListView && fmListView->selectionModel()) {
        const auto rows = fmListView->selectionModel()->selectedRows();
        for (const QModelIndex &i : rows) {
            QModelIndex src = (fmProxyModel && i.model()==fmProxyModel) ? fmProxyModel->mapToSource(i) : i;
            paths << fmDirModel->filePath(src);
        }
    }
    if (paths.isEmpty()) return;

    const QString destDir = fmDirModel ? fmDirModel->rootPath() : QString();
    if (destDir.isEmpty()) return;

    bool ok = false;
    QString folderName = QInputDialog::getText(this, "Create Folder", "Enter folder name:", QLineEdit::Normal, "New Folder", &ok);
    if (!ok) return;
    folderName = folderName.trimmed();
    if (folderName.isEmpty()) return;

    QDir dd(destDir);
    QString folderPath = dd.filePath(folderName);
    if (QFileInfo::exists(folderPath)) {
        int i = 2; const QString base = folderName;
        while (QFileInfo::exists(folderPath)) { folderName = QString("%1 (%2)").arg(base).arg(i++); folderPath = dd.filePath(folderName);}    }

    if (!dd.mkpath(folderPath)) {
        QMessageBox::warning(this, "Error", QString("Failed to create folder: %1").arg(folderPath));
        return;
    }

    // Release locks and move
    releaseAnyPreviewLocksForPaths(paths);

    auto &q = FileOpsQueue::instance();
    q.enqueueMove(paths, folderPath);

    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(m_host ? static_cast<QWidget*>(m_host) : static_cast<QWidget*>(this));
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
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



void FileManagerWidget::onFmItemDoubleClicked(const QModelIndex &index)
{
    QModelIndex idx = index.sibling(index.row(), 0);
    QModelIndex srcIdx = idx;
    if (fmProxyModel && idx.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(idx);

    const QString path = fmDirModel ? fmDirModel->filePath(srcIdx) : QString();
    if (path.isEmpty()) return;

    if (fmProxyModel && fmGroupSequences && idx.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(idx)) {
        auto info = fmProxyModel->infoForProxyIndex(idx);
        QStringList frames = m_host->reconstructSequenceFramePaths(info.reprPath, info.start, info.end);
        if (!frames.isEmpty()) {
            if (!m_host->previewOverlay) {
                m_host->previewOverlay = new PreviewOverlay(nullptr);
                QObject::connect(m_host->previewOverlay, &PreviewOverlay::closed, m_host, &MainWindow::closePreview);
                QObject::connect(m_host->previewOverlay, &PreviewOverlay::navigateRequested, this, [this](int d){ this->changeFmPreview(d); });
            } else {

            }
            QAbstractItemView* srcView = (fmGridView && fmGridView->isVisible() && fmGridView->hasFocus())
                ? static_cast<QAbstractItemView*>(fmGridView)
                : static_cast<QAbstractItemView*>(fmListView);
            m_host->fmOverlayCurrentIndex = QPersistentModelIndex(idx);
            m_host->fmOverlaySourceView = srcView;
            int pad = 0;
            const QString fileName = QFileInfo(info.reprPath).fileName();
            for (int i = fileName.size() - 1; i >= 0; --i) {
                if (fileName.at(i).isDigit()) {
                    int j = i;
                    while (j >= 0 && fileName.at(j).isDigit()) --j;
                    pad = i - j;
                    break;
                }
            }
            if (pad == 0) pad = QString::number(info.start).length();
            const QString s0 = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
            const QString s1 = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
            const QString seqName = QString("%1.[%2-%3].%4").arg(info.base, s0, s1, info.ext);
            m_host->previewOverlay->showSequence(frames, seqName, info.start, info.end);
            return;
        }
    }

    const QFileInfo fi(path);
    if (fi.isDir()) {
        emit navigateToPathRequested(path, true);
        return;
    }

    const QString ext = fi.suffix();
    if (isImageFile(ext) || isVideoFile(ext)) {
        if (!m_host->previewOverlay) {
            m_host->previewOverlay = new PreviewOverlay(nullptr);
            QObject::connect(m_host->previewOverlay, &PreviewOverlay::closed, m_host, &MainWindow::closePreview);
            QObject::connect(m_host->previewOverlay, &PreviewOverlay::navigateRequested, this, [this](int d){ this->changeFmPreview(d); });
        } else {

        }
        QAbstractItemView* srcView = (fmGridView && fmGridView->isVisible() && fmGridView->hasFocus())
            ? static_cast<QAbstractItemView*>(fmGridView)
            : static_cast<QAbstractItemView*>(fmListView);
        m_host->fmOverlayCurrentIndex = QPersistentModelIndex(idx);
        m_host->fmOverlaySourceView = srcView;
        m_host->previewOverlay->showAsset(path, fi.fileName(), fi.suffix());
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void FileManagerWidget::ensurePreviewInfoLayout()
{
    LogManager::instance().addLog("[TRACE] FM: ensurePreviewInfoLayout enter", "DEBUG");
    if (!fmToolbar) { LogManager::instance().addLog("[TRACE] FM: ensurePreviewInfoLayout no fmToolbar", "DEBUG"); return; }
    QWidget *right = fmToolbar->parentWidget();
    if (!right) { LogManager::instance().addLog("[TRACE] FM: ensurePreviewInfoLayout no right", "DEBUG"); return; }
    auto rightLayout = qobject_cast<QBoxLayout*>(right->layout());
    if (!rightLayout) { LogManager::instance().addLog("[TRACE] FM: ensurePreviewInfoLayout no layout", "DEBUG"); return; }

    // If duplicate view stacks exist (bug from earlier setup), adopt the one that actually has views
    if (fmViewStack) {
        auto stacks = right->findChildren<QStackedWidget*>(QString(), Qt::FindDirectChildrenOnly);
        QStackedWidget *realStack = fmViewStack;
        for (auto s : stacks) { if (s && s->count() >= 2) { realStack = s; break; } }
        if (realStack != fmViewStack && realStack) {
            int i = rightLayout->indexOf(fmViewStack);
            if (i >= 0) { auto it = rightLayout->takeAt(i); Q_UNUSED(it); fmViewStack->deleteLater(); }
            fmViewStack = realStack;
        }
        // Drop any stray stacked widgets except fmViewStack
        for (int i = rightLayout->count() - 1; i >= 0; --i) {
            QWidget *w = rightLayout->itemAt(i) ? rightLayout->itemAt(i)->widget() : nullptr;
            if (w && w != fmViewStack && qobject_cast<QStackedWidget*>(w)) {
                rightLayout->takeAt(i);
                w->deleteLater();
            }
        }
    }

    // If we've already built the right splitter, ensure it's in the layout and exit
    if (fmRightSplitter && fmPreviewInfoSplitter && fmPreviewPanel && fmInfoPanel) {
        if (rightLayout->indexOf(fmRightSplitter) < 0) rightLayout->addWidget(fmRightSplitter);
        bindHostPointers();
        return;
    }

    // Build right side splitter: [viewContainer(fmViewStack)] | [Preview/Info splitter]
    fmRightSplitter = new QSplitter(Qt::Horizontal, right);

    QWidget *viewContainer = new QWidget(fmRightSplitter);
    auto vc = new QVBoxLayout(viewContainer);
    vc->setContentsMargins(0,0,0,0);
    vc->setSpacing(0);
    if (fmViewStack) {
        fmViewStack->setParent(viewContainer);
        vc->addWidget(fmViewStack);
    }

    // Preview panel (compact but complete)
    fmPreviewPanel = new QWidget(fmRightSplitter);
    fmPreviewPanel->setMinimumWidth(260);
    auto pv = new QVBoxLayout(fmPreviewPanel);
    pv->setContentsMargins(0,0,0,0);
    pv->setSpacing(6);

    // Image/Video preview in a single GraphicsView (enables zoom/pan for both)
    fmImageScene = new QGraphicsScene(fmPreviewPanel);
    fmImageItem = new QGraphicsPixmapItem();
    fmImageScene->addItem(fmImageItem);
    // GraphicsVideoItem for video playback inside the same scene
    fmVideoItem = new QGraphicsVideoItem();
    fmVideoItem->setVisible(false);
    fmImageScene->addItem(fmVideoItem);

    fmImageView = new QGraphicsView(fmImageScene, fmPreviewPanel);
    fmImageView->setDragMode(QGraphicsView::ScrollHandDrag);
    fmImageView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    fmImageView->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    // Install event filters so MainWindow can handle wheel zoom/pan like before
    if (m_host) {
        fmImageView->installEventFilter(m_host);
        fmImageView->viewport()->installEventFilter(m_host);
    }
    // Disable DnD in preview pane to avoid conflicts with pan gesture
    fmImageView->setAcceptDrops(false);
    if (fmImageView->viewport()) fmImageView->viewport()->setAcceptDrops(false);

    pv->addWidget(fmImageView, 1);

    // Legacy QVideoWidget (kept hidden as fallback)
    fmVideoWidget = new QVideoWidget(fmPreviewPanel);
    fmVideoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
    fmVideoWidget->hide();
    pv->addWidget(fmVideoWidget, 1);

    // Media backend
    fmMediaPlayer = new QMediaPlayer(fmPreviewPanel);
    fmAudioOutput = new QAudioOutput(fmPreviewPanel);
    // Create a QVideoSink for optional software processing (inactive by default)
    fmVideoSink = new QVideoSink(fmPreviewPanel);
    // Default to hardware video path for stability
    fmMediaPlayer->setVideoOutput(fmVideoSink);
    fmMediaPlayer->setAudioOutput(fmAudioOutput);

    // Handle incoming video frames and apply color transform
    QObject::connect(fmVideoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame){
        if (!frame.isValid()) return;
        QImage img = frame.toImage();
        if (img.isNull()) return;
        fmLastVideoFrameRaw = img; // cache raw frame (unscaled)

        // Scale to viewport first to keep per-frame cost manageable
        int targetW = 0, targetH = 0;
        if (fmImageView && fmImageView->viewport()) {
            targetW = fmImageView->viewport()->width();
            targetH = fmImageView->viewport()->height();
        }
        if (targetW <= 0 || targetH <= 0) { targetW = 1600; targetH = 1200; }

        QImage out = fmLastVideoFrameRaw.scaled(targetW, targetH, Qt::KeepAspectRatio, Qt::FastTransformation);

        auto toLinear709 = [](float v){ return (v < 0.081f) ? (v / 4.5f) : std::pow((v + 0.099f) / 1.099f, 1.0f/0.45f); };
        auto linearToSRGB = [](float v){ v = std::clamp(v, 0.0f, 1.0f); return (v <= 0.0031308f) ? 12.92f*v : 1.055f*std::pow(v, 1.0f/2.4f) - 0.055f; };

        OIIOImageLoader::ColorSpace cs = OIIOImageLoader::ColorSpace::Rec709;
        if (fmColorSpaceCombo) {
            switch (fmColorSpaceCombo->currentIndex()) {
                case 0: cs = OIIOImageLoader::ColorSpace::Linear; break;
                case 1: cs = OIIOImageLoader::ColorSpace::sRGB;  break;
                case 2: cs = OIIOImageLoader::ColorSpace::Rec709; break;
            }
        }

        if (cs != OIIOImageLoader::ColorSpace::Rec709) {
            out = out.convertToFormat(QImage::Format_RGBA8888);
            const int w = out.width(), h = out.height();
            for (int y = 0; y < h; ++y) {
                uchar* p = out.scanLine(y);
                for (int x = 0; x < w; ++x) {
                    float r = p[4*x+0] / 255.0f;
                    float g = p[4*x+1] / 255.0f;
                    float b = p[4*x+2] / 255.0f;
                    // Rec709 -> Linear
                    r = toLinear709(r); g = toLinear709(g); b = toLinear709(b);
                    // Linear -> target space
                    if (cs == OIIOImageLoader::ColorSpace::sRGB) {
                        r = linearToSRGB(r); g = linearToSRGB(g); b = linearToSRGB(b);
                    } else { // Linear target: clamp
                        r = std::clamp(r, 0.0f, 1.0f);
                        g = std::clamp(g, 0.0f, 1.0f);
                        b = std::clamp(b, 0.0f, 1.0f);
                    }
                    p[4*x+0] = uchar(r * 255.0f + 0.5f);
                    p[4*x+1] = uchar(g * 255.0f + 0.5f);
                    p[4*x+2] = uchar(b * 255.0f + 0.5f);
                }
            }
        }

        if (fmVideoItem) fmVideoItem->setVisible(false); // ensure legacy path hidden
        if (fmImageItem) {
            fmImageItem->setPixmap(QPixmap::fromImage(out));
            fmImageItem->setTransformationMode(Qt::SmoothTransformation);
            if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
            if (fmImageView && fmImageFitToView) {
                const QSize sz = out.size();
                if (sz != fmLastVideoPixmapSize) {
                    fmLastVideoPixmapSize = sz;
                    fmImageView->resetTransform();
                    fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                }
            }
        }
    });

    // Normalize video item geometry when native size becomes available and fit in view
    QObject::connect(fmVideoItem, &QGraphicsVideoItem::nativeSizeChanged, this, [this](const QSizeF &sz){
        if (!m_host) return;
        if (m_host->fmImageView && m_host->fmVideoItem && m_host->fmVideoItem->isVisible()) {
            m_host->fmVideoItem->setPos(0, 0);
            if (sz.isValid()) {
                m_host->fmVideoItem->setSize(sz);
                if (m_host->fmImageScene) m_host->fmImageScene->setSceneRect(QRectF(QPointF(0, 0), sz));
            }
            if (m_host->fmImageFitToView) {
                m_host->fmImageView->resetTransform();
                m_host->fmImageView->fitInView(m_host->fmVideoItem, Qt::KeepAspectRatio);
            }
        }
    });
    // Log media errors so we can diagnose unsupported MP4s in the pane
    QObject::connect(fmMediaPlayer, &QMediaPlayer::errorOccurred, this,
                     [this](QMediaPlayer::Error error, const QString &errorString){
                         Q_UNUSED(error);
                         LogManager::instance().addLog(QString("[FM Preview] QMediaPlayer error: %1").arg(errorString), "ERROR");
                         if (fmTimeLabel) fmTimeLabel->setText(QString("Error: %1").arg(errorString));
                     });

    // Text/CSV/PDF/SVG
    fmTextView = new QPlainTextEdit(fmPreviewPanel); fmTextView->setReadOnly(true); fmTextView->hide(); pv->addWidget(fmTextView, 1);
    fmCsvModel = new QStandardItemModel(this); fmCsvView = new QTableView(fmPreviewPanel); fmCsvView->setModel(fmCsvModel); fmCsvView->hide(); pv->addWidget(fmCsvView, 1);
    fmPdfDoc = new QPdfDocument(fmPreviewPanel); fmPdfView = new QPdfView(fmPreviewPanel); fmPdfView->setDocument(fmPdfDoc); fmPdfView->hide(); pv->addWidget(fmPdfView, 1);
    fmSvgScene = new QGraphicsScene(fmPreviewPanel); fmSvgView = new QGraphicsView(fmSvgScene, fmPreviewPanel); fmSvgView->hide(); pv->addWidget(fmSvgView, 1);

    // Media controls layout
    QWidget *ctrl = new QWidget(fmPreviewPanel);
    auto cr = new QVBoxLayout(ctrl);
    cr->setContentsMargins(6,0,6,6);
    cr->setSpacing(4);

    // Row 1: Full-width timeline slider
    fmPositionSlider = new QSlider(Qt::Horizontal, ctrl);
    fmPositionSlider->setRange(0, 0);
    cr->addWidget(fmPositionSlider);

    // Row 2: Transport + time + color + audio
    auto row2 = new QHBoxLayout();
    row2->setContentsMargins(0,0,0,0);
    row2->setSpacing(6);

    fmPrevFrameBtn = new QPushButton(ctrl);
    fmPrevFrameBtn->setIcon(icoMediaPrevFrame());

    fmPlayPauseBtn = new QPushButton(ctrl);
    fmPlayPauseBtn->setIcon(icoMediaPlay());

    fmNextFrameBtn = new QPushButton(ctrl);
    fmNextFrameBtn->setIcon(icoMediaNextFrame());

    fmTimeLabel = new QLabel("--:-- / --:--", ctrl);

    // Color space selector (HDR sequences)
    fmColorSpaceLabel = new QLabel("Color:", ctrl);
    fmColorSpaceCombo = new QComboBox(ctrl);
    fmColorSpaceCombo->addItem("Linear");
    fmColorSpaceCombo->addItem("sRGB");
    fmColorSpaceCombo->addItem("Rec.709");
    fmColorSpaceLabel->hide();
    fmColorSpaceCombo->hide();

    fmMuteBtn = new QPushButton(ctrl);
    fmMuteBtn->setIcon(icoMediaAudio());

    fmVolumeSlider = new QSlider(Qt::Horizontal, ctrl);
    fmVolumeSlider->setRange(0, 100);
    fmVolumeSlider->setValue(50);
    if (fmAudioOutput) fmAudioOutput->setVolume(0.5);

    row2->addWidget(fmPrevFrameBtn);
    row2->addWidget(fmPlayPauseBtn);
    row2->addWidget(fmNextFrameBtn);
    row2->addStretch(1);
    row2->addWidget(fmTimeLabel);
    row2->addSpacing(10);
    row2->addWidget(fmColorSpaceLabel);
    row2->addWidget(fmColorSpaceCombo);
    row2->addSpacing(10);
    row2->addWidget(fmMuteBtn);
    row2->addWidget(fmVolumeSlider);

    cr->addLayout(row2);
    pv->addWidget(ctrl);

    // Wire media controls and player signals
    auto fmtTime = [](qint64 ms) {
        if (ms < 0) ms = 0;
        int h = int(ms / 3600000); ms %= 3600000;
        int m = int(ms / 60000);   ms %= 60000;
        int s = int(ms / 1000);
        return h > 0
            ? QString::asprintf("%d:%02d:%02d", h, m, s)
            : QString::asprintf("%02d:%02d", m, s);
    };

    QObject::connect(fmPlayPauseBtn, &QPushButton::clicked, this, [this]() {
        if (fmIsSequence) {
            if (fmSequencePlaying) pauseFmSequence();
            else playFmSequence();
            return;
        }
#ifdef HAVE_FFMPEG
        if (fmUsingFallbackVideo) {
            fmFallbackPaused = !fmFallbackPaused;
            if (fmFallbackReader) fmFallbackReader->setPaused(fmFallbackPaused);
            fmPlayPauseBtn->setIcon(fmFallbackPaused ? icoMediaPlay() : icoMediaPause());
            return;
        }
#endif
        if (!fmMediaPlayer) return;
        const auto st = fmMediaPlayer->playbackState();
        if (st == QMediaPlayer::PlayingState) {
            fmMediaPlayer->pause();
            fmPlayPauseBtn->setIcon(icoMediaPlay());
        } else {
            fmMediaPlayer->play();
            fmPlayPauseBtn->setIcon(icoMediaPause());
        }
    });

    QObject::connect(fmPrevFrameBtn, &QPushButton::clicked, this, [this]() {
        if (fmIsSequence) { stepFmSequence(-1); return; }
#ifdef HAVE_FFMPEG
        if (fmUsingFallbackVideo) {
            fmFallbackPaused = true;
            if (fmFallbackReader) {
                fmFallbackReader->setPaused(true);
                const qint64 step = (fmFallbackFps > 0.0) ? qint64(1000.0 / fmFallbackFps) : 41;
                qint64 target = fmPositionSlider ? qMax(0, fmPositionSlider->value() - int(step)) : 0;
                fmFallbackReader->seekToMs(target);
                fmFallbackReader->stepOnce();
            }
            if (fmPlayPauseBtn) fmPlayPauseBtn->setIcon(icoMediaPlay());
            return;
        }
#endif
        if (!fmMediaPlayer) return;
        qint64 pos = fmMediaPlayer->position();
        qint64 step = 41; // ~24 fps fallback
        fmMediaPlayer->setPosition(pos > step ? pos - step : 0);
    });

    QObject::connect(fmNextFrameBtn, &QPushButton::clicked, this, [this]() {
        if (fmIsSequence) { stepFmSequence(1); return; }
#ifdef HAVE_FFMPEG
        if (fmUsingFallbackVideo) {
            fmFallbackPaused = true;
            if (fmFallbackReader) {
                fmFallbackReader->setPaused(true);
                const qint64 step = (fmFallbackFps > 0.0) ? qint64(1000.0 / fmFallbackFps) : 41;
                qint64 target = fmPositionSlider ? fmPositionSlider->value() + int(step) : step;
                fmFallbackReader->seekToMs(target);
                fmFallbackReader->stepOnce();
            }
            if (fmPlayPauseBtn) fmPlayPauseBtn->setIcon(icoMediaPlay());
            return;
        }
#endif
        if (!fmMediaPlayer) return;
        qint64 pos = fmMediaPlayer->position();
        qint64 step = 41; // ~24 fps fallback
        fmMediaPlayer->setPosition(pos + step);
    });

    QObject::connect(fmPositionSlider, &QSlider::sliderPressed, this, [this]() {
        fmWasPlayingBeforeSeek = false;
        if (fmIsSequence) {
            fmWasPlayingBeforeSeek = fmSequencePlaying;
            if (fmSequencePlaying) pauseFmSequence();
        }
#ifdef HAVE_FFMPEG
        else if (fmUsingFallbackVideo) {
            fmWasPlayingBeforeSeek = !fmFallbackPaused;
            fmFallbackPaused = true;
            if (fmFallbackReader) fmFallbackReader->setPaused(true);
        }
#endif
        else if (fmMediaPlayer) {
            fmWasPlayingBeforeSeek = (fmMediaPlayer->playbackState() == QMediaPlayer::PlayingState);
            if (fmWasPlayingBeforeSeek) fmMediaPlayer->pause();
        }
    });

    QObject::connect(fmPositionSlider, &QSlider::sliderMoved, this, [this](int v) {
        if (fmIsSequence) {
            loadFmSequenceFrame(v);
        }
#ifdef HAVE_FFMPEG
        else if (fmUsingFallbackVideo) {
            if (fmFallbackReader) {
                fmFallbackReader->seekToMs(v);
                fmFallbackReader->stepOnce();
            }
        }
#endif
        else if (fmMediaPlayer) {
            fmMediaPlayer->setPosition(v);
        }
    });

    QObject::connect(fmPositionSlider, &QSlider::sliderReleased, this, [this]() {
        if (fmIsSequence) {
            if (fmWasPlayingBeforeSeek) playFmSequence();
        }
#ifdef HAVE_FFMPEG
        else if (fmUsingFallbackVideo) {
            if (fmWasPlayingBeforeSeek) {
                fmFallbackPaused = false;
                if (fmFallbackReader) fmFallbackReader->setPaused(false);
            }
        }
#endif
        else if (fmMediaPlayer) {
            if (fmWasPlayingBeforeSeek) fmMediaPlayer->play();
        }
    });

    QObject::connect(fmMediaPlayer, &QMediaPlayer::durationChanged, this, [this, fmtTime](qint64 d) {
        if (fmPositionSlider) fmPositionSlider->setRange(0, int(d));
        if (fmTimeLabel) fmTimeLabel->setText(QString("%1 / %2").arg(fmtTime(0), fmtTime(d)));
    });

    QObject::connect(fmMediaPlayer, &QMediaPlayer::positionChanged, this, [this, fmtTime](qint64 p) {
        if (fmPositionSlider && !fmPositionSlider->isSliderDown()) fmPositionSlider->setValue(int(p));

        if (fmTimeLabel) {
            qint64 d = fmMediaPlayer ? fmMediaPlayer->duration() : 0;
            fmTimeLabel->setText(QString("%1 / %2").arg(fmtTime(p), fmtTime(d)));
        }
    });

    QObject::connect(fmMediaPlayer, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        if (!fmPlayPauseBtn) return;
        fmPlayPauseBtn->setIcon(s == QMediaPlayer::PlayingState ? icoMediaPause() : icoMediaPlay());
    });
    // React to color space changes (affects sequences and HDR still images in embedded preview)
    QObject::connect(fmColorSpaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){
        if (fmIsSequence) {
            loadFmSequenceFrame(fmSequenceCurrentIndex);
            return;
        }
        if (!fmCurrentPreviewPath.isEmpty()) {
            const QString ext = QFileInfo(fmCurrentPreviewPath).suffix().toLower();
            // For videos, force stable hardware pipeline (Rec.709). Color transforms disabled for now.

            // Video: re-render last captured frame with new color space
            if (isVideoFile(ext) && !fmLastVideoFrameRaw.isNull() && fmImageItem) {
                // Scale to viewport first
                int targetW = fmImageView && fmImageView->viewport() ? fmImageView->viewport()->width() : 1600;
                int targetH = fmImageView && fmImageView->viewport() ? fmImageView->viewport()->height() : 1200;
                QImage out = fmLastVideoFrameRaw.scaled(targetW, targetH, Qt::KeepAspectRatio, Qt::FastTransformation);
                auto toLinear709 = [](float v){ return (v < 0.081f) ? (v / 4.5f) : std::pow((v + 0.099f) / 1.099f, 1.0f/0.45f); };
                auto linearToSRGB = [](float v){ v = std::clamp(v, 0.0f, 1.0f); return (v <= 0.0031308f) ? 12.92f*v : 1.055f*std::pow(v, 1.0f/2.4f) - 0.055f; };
                OIIOImageLoader::ColorSpace cs = OIIOImageLoader::ColorSpace::Rec709;
                if (fmColorSpaceCombo) {
                    switch (fmColorSpaceCombo->currentIndex()) {
                        case 0: cs = OIIOImageLoader::ColorSpace::Linear; break;
                        case 1: cs = OIIOImageLoader::ColorSpace::sRGB;  break;
                        case 2: cs = OIIOImageLoader::ColorSpace::Rec709; break;
                    }
                }
                if (cs != OIIOImageLoader::ColorSpace::Rec709) {
                    out = out.convertToFormat(QImage::Format_RGBA8888);
                    const int w = out.width(), h = out.height();
                    for (int y = 0; y < h; ++y) {
                        uchar* p = out.scanLine(y);
                        for (int x = 0; x < w; ++x) {
                            float r = p[4*x+0] / 255.0f;
                            float g = p[4*x+1] / 255.0f;
                            float b = p[4*x+2] / 255.0f;
                            r = toLinear709(r); g = toLinear709(g); b = toLinear709(b);
                            if (cs == OIIOImageLoader::ColorSpace::sRGB) {
                                r = linearToSRGB(r); g = linearToSRGB(g); b = linearToSRGB(b);
                            } else {
                                r = std::clamp(r, 0.0f, 1.0f);
                                g = std::clamp(g, 0.0f, 1.0f);
                                b = std::clamp(b, 0.0f, 1.0f);
                            }
                            p[4*x+0] = uchar(r * 255.0f + 0.5f);
                            p[4*x+1] = uchar(g * 255.0f + 0.5f);
                            p[4*x+2] = uchar(b * 255.0f + 0.5f);
                        }
                    }
                }
                fmImageItem->setPixmap(QPixmap::fromImage(out));
                fmImageItem->setTransformationMode(Qt::SmoothTransformation);
                if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                if (fmImageView && fmImageFitToView) { fmImageView->resetTransform(); fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio); }
                fmImageView->show();
                return;
            }
            // HDR-capable still image via OIIO
            const bool hdrByExt = (ext == "exr" || ext == "hdr" || ext == "tif" || ext == "tiff" || ext == "psd");
            if (hdrByExt && fmImageView && fmImageItem) {
                int targetW = fmImageView->viewport() ? fmImageView->viewport()->width() : 1600;
                int targetH = fmImageView->viewport() ? fmImageView->viewport()->height() : 1200;
                if (targetW <= 0 || targetH <= 0) { targetW = 1600; targetH = 1200; }
                if (OIIOImageLoader::isOIIOSupported(fmCurrentPreviewPath)) {
                    OIIOImageLoader::ColorSpace cs = OIIOImageLoader::ColorSpace::sRGB;
                    if (fmColorSpaceCombo) {
                        switch (fmColorSpaceCombo->currentIndex()) {
                            case 0: cs = OIIOImageLoader::ColorSpace::Linear; break;
                            case 1: cs = OIIOImageLoader::ColorSpace::sRGB;  break;
                            case 2: cs = OIIOImageLoader::ColorSpace::Rec709; break;
                        }
                    }
                    QImage img = OIIOImageLoader::loadImage(fmCurrentPreviewPath, 0, 0, cs);
                    if (!img.isNull()) {
                        fmOriginalImage = img;
                        fmImageItem->setPixmap(QPixmap::fromImage(img));
                        fmImageItem->setTransformationMode(Qt::SmoothTransformation);
                        if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                        fmImageView->resetTransform();
                        fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                        fmImageFitToView = true;
                        fmImageView->show();
                    }
                }
            }
        }
    });


    QObject::connect(fmVolumeSlider, &QSlider::valueChanged, this, [this](int v) {
        if (fmAudioOutput) fmAudioOutput->setVolume(qBound(0, v, 100) / 100.0);
        if (fmMuteBtn) fmMuteBtn->setIcon((fmAudioOutput && fmAudioOutput->isMuted()) || v == 0 ? icoMediaMute() : icoMediaAudio());
    });

    QObject::connect(fmMuteBtn, &QPushButton::clicked, this, [this]() {
        if (!fmAudioOutput) return;
        bool to = !fmAudioOutput->isMuted();

        fmAudioOutput->setMuted(to);
        if (fmMuteBtn) fmMuteBtn->setIcon(to ? icoMediaMute() : icoMediaAudio());
    });

    if (!fmSequenceTimer) {
        fmSequenceTimer = new QTimer(fmPreviewPanel);
        fmSequenceTimer->setTimerType(Qt::PreciseTimer);
        QObject::connect(fmSequenceTimer, &QTimer::timeout, this, [this]() {
            if (!fmIsSequence) return;
            // Avoid starting a new decode while a frame is still loading to ensure UI updates during playback
            if (fmSeqWorkerThread && fmSeqWorkerThread->isRunning()) return;
            int idx = fmSequenceCurrentIndex + 1;
            if (idx >= fmSequenceFramePaths.size()) idx = 0;
            loadFmSequenceFrame(idx);
        });
    }

    // Info panel
    fmInfoPanel = new QWidget(fmRightSplitter);
    fmInfoPanel->setMinimumWidth(260);
    auto info = new QVBoxLayout(fmInfoPanel);
    info->setContentsMargins(8,8,8,8);
    info->setSpacing(4);
    auto makeRow = [&](const char *label, QLabel* &val){
        QWidget *w = new QWidget(fmInfoPanel);
        auto vl = new QVBoxLayout(w); vl->setContentsMargins(0,0,0,0); vl->setSpacing(2);
        auto l = new QLabel(QString::fromLatin1(label), w); l->setStyleSheet("color:#9aa0a6;");
        val = new QLabel("-", w); val->setStyleSheet("color:white;");
        vl->addWidget(l); vl->addWidget(val); info->addWidget(w);
    };
    makeRow("Name", fmInfoFileName);
    makeRow("Path", fmInfoFilePath);
    makeRow("Size", fmInfoFileSize);
    makeRow("Type", fmInfoFileType);
    makeRow("Dimensions", fmInfoDimensions);
    makeRow("Created", fmInfoCreated);
    makeRow("Modified", fmInfoModified);
    makeRow("Permissions", fmInfoPermissions);
    info->addStretch(1);

    // Vertical splitter for Preview | Info
    fmPreviewInfoSplitter = new QSplitter(Qt::Vertical, fmRightSplitter);
    fmPreviewInfoSplitter->addWidget(fmPreviewPanel);
    fmPreviewInfoSplitter->addWidget(fmInfoPanel);
    fmPreviewInfoSplitter->setStretchFactor(0, 2);
    fmPreviewInfoSplitter->setStretchFactor(1, 1);
    fmPreviewInfoSplitter->setChildrenCollapsible(false);
    fmPreviewInfoSplitter->setHandleWidth(6);
    fmPreviewInfoSplitter->setOpaqueResize(true);

    // Assemble and add to layout
    fmRightSplitter->addWidget(viewContainer);
    fmRightSplitter->addWidget(fmPreviewInfoSplitter);
    fmRightSplitter->setStretchFactor(0, 3);
    fmRightSplitter->setStretchFactor(1, 1);
    fmRightSplitter->setChildrenCollapsible(false);
    fmRightSplitter->setHandleWidth(6);
    fmRightSplitter->setOpaqueResize(true);
    rightLayout->addWidget(fmRightSplitter);

    // Restore right splitter states and preview visibility
    {
        QSettings s("AugmentCode", "KAssetManager");
        if (s.contains("FileManager/RightSplitter")) fmRightSplitter->restoreState(s.value("FileManager/RightSplitter").toByteArray());
        if (s.contains("FileManager/PreviewInfoSplitter")) fmPreviewInfoSplitter->restoreState(s.value("FileManager/PreviewInfoSplitter").toByteArray());
        // Apply explicit sizes if available (ensures collapsed panes persist)
        const QVariantList rightSizes = s.value("FileManager/RightSplitterSizes").toList();
        if (!rightSizes.isEmpty()) { QList<int> sz; for (const QVariant &v : rightSizes) sz << v.toInt(); fmRightSplitter->setSizes(sz); }
        const QVariantList pivSizes = s.value("FileManager/PreviewInfoSplitterSizes").toList();
        if (!pivSizes.isEmpty()) { QList<int> sz; for (const QVariant &v : pivSizes) sz << v.toInt(); fmPreviewInfoSplitter->setSizes(sz); }
        const bool previewVisible = s.value("FileManager/PreviewVisible", true).toBool();
        fmPreviewInfoSplitter->setVisible(previewVisible);
    }

    bindHostPointers();
    LogManager::instance().addLog("[TRACE] FM: ensurePreviewInfoLayout leave", "DEBUG");
}


void FileManagerWidget::onFmSelectionChanged()
{
    // Update preview and info when selection changes
    QAbstractItemView* view = fmIsGridMode ? static_cast<QAbstractItemView*>(fmGridView)
                                           : static_cast<QAbstractItemView*>(fmListView);
    if (!view || !view->selectionModel()) { clearFmPreview(); updateFmInfoPanel(); return; }
    QModelIndex idx;
    const auto sel = view->selectionModel()->selectedIndexes();
    if (!sel.isEmpty()) idx = sel.first();

    fmOverlayCurrentIndex = QPersistentModelIndex(idx);
    fmOverlaySourceView = view;

    if (fmPreviewInfoSplitter && fmPreviewInfoSplitter->isVisible()) {
        updateFmPreviewForIndex(idx);
    }
    updateFmInfoPanel();
}

void FileManagerWidget::onFmTogglePreview(bool checked)
{
    if (!fmPreviewInfoSplitter) return;
    const bool show = checked;
    fmPreviewInfoSplitter->setVisible(show);
    if (!show) {
        if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
#ifdef HAVE_FFMPEG
        stopFmFallbackVideo();
#endif
    } else {
        onFmSelectionChanged();
    }
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/PreviewVisible", show);
}

void FileManagerWidget::onFmOpenOverlay()
{
    if (!m_host) return;
    // Toggle: if overlay is visible, close it
    if (m_host->previewOverlay && m_host->previewOverlay->isVisible()) { m_host->closePreview(); return; }

    // Determine current selection in FM and open full-screen overlay
    QModelIndex idx;
    QAbstractItemView* focusedView = nullptr;
    if (fmGridView && fmGridView->hasFocus()) { idx = fmGridView->currentIndex(); focusedView = fmGridView; }
    else if (fmListView && fmListView->hasFocus()) { idx = fmListView->currentIndex(); focusedView = fmListView; }
    if (!idx.isValid()) return;
    idx = idx.sibling(idx.row(), 0);

    // Record overlay navigation context
    fmOverlayCurrentIndex = QPersistentModelIndex(idx);
    fmOverlaySourceView = focusedView;

    // If sequence grouping is enabled and the selection is a representative, open as sequence
    if (fmProxyModel && fmGroupSequences && idx.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(idx)) {
        auto info = fmProxyModel->infoForProxyIndex(idx);
        // Build all frame paths
        QStringList frames;
        // Reuse MainWindow helper if available, else reconstruct naively
        if (m_host) {
            frames = m_host->reconstructSequenceFramePaths(info.reprPath, info.start, info.end);
        }
        if (!frames.isEmpty()) {
            if (!m_host->previewOverlay) {
                m_host->previewOverlay = new PreviewOverlay(nullptr);
                connect(m_host->previewOverlay, &PreviewOverlay::closed, m_host, &MainWindow::closePreview);
                connect(m_host->previewOverlay, &PreviewOverlay::navigateRequested, this, [this](int d){ this->changeFmPreview(d); });
            } else {

            }
            int pad = 0;
            auto m = SequenceDetector::mainPattern().match(QFileInfo(info.reprPath).fileName());
            if (m.hasMatch()) pad = m.captured(3).length(); else pad = QString::number(info.start).length();
            QString s0 = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
            QString s1 = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
            QString seqName = QString("%1.[%2-%3].%4").arg(info.base, s0, s1, info.ext);
            m_host->previewOverlay->showSequence(frames, seqName, info.start, info.end);
            return;
        }
    }

    // Otherwise open single asset
    QModelIndex srcIdx = idx;
    if (fmProxyModel && idx.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(idx);
    const QString path = fmDirModel ? fmDirModel->filePath(srcIdx) : QString();
    if (path.isEmpty()) return;
    QFileInfo finfo(path);
    if (!finfo.exists()) return;
    if (!m_host->previewOverlay) {
        m_host->previewOverlay = new PreviewOverlay(nullptr);
        connect(m_host->previewOverlay, &PreviewOverlay::closed, m_host, &MainWindow::closePreview);
        connect(m_host->previewOverlay, &PreviewOverlay::navigateRequested, this, [this](int d){ this->changeFmPreview(d); });
    } else {

    }
    m_host->previewOverlay->showAsset(path, finfo.fileName(), finfo.suffix());
}

void FileManagerWidget::clearFmPreview()
{
    if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
#ifdef HAVE_FFMPEG
    stopFmFallbackVideo();
#endif
    if (fmVideoWidget) fmVideoWidget->hide();
    if (fmVideoItem) fmVideoItem->setVisible(false);
    if (fmPrevFrameBtn) fmPrevFrameBtn->hide();
    if (fmPlayPauseBtn) fmPlayPauseBtn->hide();
    if (fmNextFrameBtn) fmNextFrameBtn->hide();
    if (fmPositionSlider) fmPositionSlider->hide();
    if (fmTimeLabel) fmTimeLabel->hide();
    if (fmVolumeSlider) fmVolumeSlider->hide();
    if (fmMuteBtn) fmMuteBtn->hide();

    if (fmColorSpaceLabel) fmColorSpaceLabel->hide();
    if (fmColorSpaceCombo) fmColorSpaceCombo->hide();

    // Reset sequence state
    if (fmSequenceTimer) fmSequenceTimer->stop();
    fmIsSequence = false;
    fmSequenceFramePaths.clear();
    fmSequenceCurrentIndex = 0;

    if (fmTextView) { fmTextView->clear(); fmTextView->hide(); }
    if (fmCsvView) fmCsvView->hide();
    if (fmCsvModel) fmCsvModel->clear();
#ifdef HAVE_QT_PDF_WIDGETS
    if (fmPdfView) fmPdfView->hide();
#endif
#ifdef HAVE_QT_PDF
    if (fmPdfDoc) fmPdfDoc->close();
#endif
    if (fmPdfPrevBtn) fmPdfPrevBtn->hide();
    if (fmPdfNextBtn) fmPdfNextBtn->hide();
    if (fmPdfPageLabel) fmPdfPageLabel->hide();
    if (fmSvgItem) { fmSvgScene->removeItem(fmSvgItem); delete fmSvgItem; fmSvgItem = nullptr; }
    if (fmSvgView) fmSvgView->hide();

    if (fmImageItem) {
        fmImageItem->setPixmap(QPixmap());
    }
    if (fmAlphaCheck) fmAlphaCheck->hide();
#ifdef HAVE_FFMPEG
    // Stop any existing FFmpeg reader when changing selection
    stopFmFallbackVideo();
#endif

}

void FileManagerWidget::updateFmPreviewForIndex(const QModelIndex &idx)
{
    if (!fmPreviewPanel || !fmPreviewPanel->isVisible()) return;
    if (!idx.isValid()) { clearFmPreview(); return; }

#ifdef HAVE_FFMPEG
    // Stop any running FFmpeg reader before switching content
    stopFmFallbackVideo();
#endif

    QModelIndex viewIdx = idx.sibling(idx.row(), 0);

    // Representative sequence item -> full sequence preview
    if (fmProxyModel && fmGroupSequences && viewIdx.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(viewIdx)) {
        auto info = fmProxyModel->infoForProxyIndex(viewIdx);
        const QString reprPath = info.reprPath;
        if (reprPath.isEmpty() || !QFileInfo::exists(reprPath)) { clearFmPreview(); return; }
        QStringList frames = m_host ? m_host->reconstructSequenceFramePaths(reprPath, info.start, info.end) : QStringList();
        if (frames.isEmpty()) { clearFmPreview(); return; }
        if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
        if (fmVideoWidget) fmVideoWidget->hide();
        if (fmImageView) fmImageView->show();
        fmIsSequence = true;
        fmSequenceFramePaths = frames;
        fmSequenceStartFrame = info.start;
        fmSequenceEndFrame = info.end;
        fmSequenceCurrentIndex = 0;
        fmSequenceFps = 24.0;
        if (fmSequenceTimer) fmSequenceTimer->stop();
        fmSequencePlaying = false;
        if (fmPrevFrameBtn) fmPrevFrameBtn->show();
        if (fmPlayPauseBtn) { fmPlayPauseBtn->show(); fmPlayPauseBtn->setIcon(icoMediaPlay()); }
        if (fmNextFrameBtn) fmNextFrameBtn->show();
        if (fmPositionSlider) { fmPositionSlider->show(); fmPositionSlider->setRange(0, frames.size()-1); fmPositionSlider->setValue(0); }
        if (fmTimeLabel) { fmTimeLabel->show(); fmTimeLabel->setText(QString("Frame %1 / %2").arg(info.start).arg(info.end)); }
        if (fmColorSpaceCombo && fmColorSpaceLabel) {
            // Always show for image sequences as requested
            fmColorSpaceLabel->setVisible(true);
            fmColorSpaceCombo->setVisible(true);
            fmColorSpaceCombo->setEnabled(true);
        }
        if (fmMuteBtn) { fmMuteBtn->show(); fmMuteBtn->setEnabled(false); fmMuteBtn->setIcon(icoMediaNoAudio()); }
        if (fmVolumeSlider) { fmVolumeSlider->show(); fmVolumeSlider->setEnabled(false); }
        loadFmSequenceFrame(0);
        return;
    }

    // Map through proxy if needed
    QModelIndex srcIdx = viewIdx;
    if (fmProxyModel && viewIdx.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(viewIdx);
    const QString path = fmDirModel ? fmDirModel->filePath(srcIdx) : QString();
    if (path.isEmpty()) { clearFmPreview(); return; }
    QFileInfo info(path);
    if (!info.exists() || info.isDir()) { clearFmPreview(); return; }

    const QString ext = info.suffix();

    auto hideNonImageWidgets = [this]{
        if (fmTextView) fmTextView->hide();
        if (fmCsvView) fmCsvView->hide();
#ifdef HAVE_QT_PDF_WIDGETS
        if (fmPdfView) fmPdfView->hide();
#endif
        if (fmPdfPrevBtn) fmPdfPrevBtn->hide();
        if (fmPdfNextBtn) fmPdfNextBtn->hide();
        if (fmPdfPageLabel) fmPdfPageLabel->hide();
        if (fmSvgView) fmSvgView->hide();
        if (fmVideoWidget) fmVideoWidget->hide();
        if (fmPrevFrameBtn) fmPrevFrameBtn->hide();
        if (fmPlayPauseBtn) fmPlayPauseBtn->hide();
        if (fmNextFrameBtn) fmNextFrameBtn->hide();
        if (fmPositionSlider) fmPositionSlider->hide();
        if (fmTimeLabel) fmTimeLabel->hide();
        if (fmVolumeSlider) fmVolumeSlider->hide();
        if (fmMuteBtn) fmMuteBtn->hide();
        if (fmImageView) fmImageView->hide();
        if (fmAlphaCheck) fmAlphaCheck->hide();
    };

    if (isImageFile(ext)) {
        if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
        hideNonImageWidgets();
        QImage img;
        int targetW = 0, targetH = 0;
        if (fmImageView && fmImageView->viewport()) {
            targetW = fmImageView->viewport()->width();
            targetH = fmImageView->viewport()->height();
        }
        if (targetW <= 0 || targetH <= 0) { targetW = 1600; targetH = 1200; }
        if (OIIOImageLoader::isOIIOSupported(path)) {
            OIIOImageLoader::ColorSpace cs = OIIOImageLoader::ColorSpace::sRGB;
            if (fmColorSpaceCombo) {
                switch (fmColorSpaceCombo->currentIndex()) {
                    case 0: cs = OIIOImageLoader::ColorSpace::Linear; break;
                    case 1: cs = OIIOImageLoader::ColorSpace::sRGB;  break;
                    case 2: cs = OIIOImageLoader::ColorSpace::Rec709; break;
                }
            }
            img = OIIOImageLoader::loadImage(path, 0, 0, cs);
        }
        if (img.isNull()) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            img = reader.read();
        }
        if (img.isNull()) { clearFmPreview(); return; }
        fmCurrentPreviewPath = path;
        fmOriginalImage = img;
        fmPreviewHasAlpha = img.hasAlphaChannel();
        if (fmAlphaCheck) { fmAlphaCheck->setVisible(fmPreviewHasAlpha); fmAlphaCheck->setChecked(false); }
        QImage disp = fmOriginalImage;
        if (fmAlphaOnlyMode && disp.hasAlphaChannel()) {
            QImage a(disp.size(), QImage::Format_Grayscale8);
            for (int y=0;y<disp.height();++y){
                for (int x=0;x<disp.width();++x){
                    uchar alpha = qAlpha(reinterpret_cast<const QRgb*>(disp.constScanLine(y))[x]);
                    a.scanLine(y)[x] = alpha;
                }
            }
            disp = a;
        }
        if (fmImageItem) {
            fmImageItem->setPixmap(QPixmap::fromImage(disp));
            fmImageItem->setTransformationMode(Qt::SmoothTransformation);
        }
        if (fmImageScene && fmImageItem) fmImageScene->setSceneRect(fmImageItem->boundingRect());
        if (fmImageView) {
            fmImageView->resetTransform();
            fmImageView->centerOn(fmImageItem);
            fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
            fmImageFitToView = true;
            fmImageView->setBackgroundBrush(QColor("#0a0a0a"));
            fmImageView->show();
        // Show/hide color space controls for HDR still images (EXR/HDR/TIF/PSD)
        if (fmColorSpaceLabel && fmColorSpaceCombo) {
            const QString extLower = ext.toLower();
            const bool hdr = (extLower == "exr" || extLower == "hdr" || extLower == "tif" || extLower == "tiff" || extLower == "psd");
            fmColorSpaceLabel->setVisible(hdr);
            fmColorSpaceCombo->setVisible(hdr);
        }

        }
        return;
    }

#ifdef HAVE_QT_PDF
    if (isPdfFile(ext)) {
        hideNonImageWidgets();
        if (fmPdfDoc) {
            fmCurrentPreviewPath = path;
            auto err = fmPdfDoc->load(path);
            if (err == QPdfDocument::Error::None && fmPdfDoc->pageCount() > 0) {
                fmPdfCurrentPage = 0;
                const QSizeF pts = fmPdfDoc->pagePointSize(fmPdfCurrentPage);
                int vw = fmImageView ? fmImageView->viewport()->width() : 800; if (vw < 1) vw = 800;
                int w = vw; int h = pts.width() > 0 ? int(pts.height() * (w / pts.width())) : w;
                QImage img = fmPdfDoc->render(fmPdfCurrentPage, QSize(w, h));
                if (!img.isNull() && fmImageItem) {
                    if (img.hasAlphaChannel()) { QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white); QPainter p(&bg); p.drawImage(0, 0, img); p.end(); img = bg; }
                    fmImageItem->setPixmap(QPixmap::fromImage(img));
                    if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                    if (fmImageView) { fmImageView->resetTransform(); fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio); fmImageFitToView = true; fmImageView->setBackgroundBrush(Qt::white); fmImageView->show(); }
                }
                if (fmPdfPrevBtn) fmPdfPrevBtn->show(); if (fmPdfNextBtn) fmPdfNextBtn->show(); if (fmPdfPageLabel) { fmPdfPageLabel->show(); fmPdfPageLabel->setText(QString("%1/%2").arg(1).arg(fmPdfDoc->pageCount())); }
#ifdef HAVE_QT_PDF_WIDGETS
                if (fmPdfView) fmPdfView->hide();
#endif
            } else {
                if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); }
            }
        }
        return;
    }
#else
    if (isPdfFile(ext)) { hideNonImageWidgets(); if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); } return; }
#endif

    if (isSvgFile(ext)) {
        hideNonImageWidgets();
        if (fmSvgScene && fmSvgView) {
            if (fmSvgItem) { fmSvgScene->removeItem(fmSvgItem); delete fmSvgItem; fmSvgItem = nullptr; }
            fmCurrentPreviewPath = path;
            auto *item = new QGraphicsSvgItem(path);
            item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
            fmSvgItem = item;
            fmSvgScene->addItem(fmSvgItem);
            fmSvgView->fitInView(fmSvgItem, Qt::KeepAspectRatio);
            fmSvgView->show();
        }
        return;
    }

    if (isTextFile(ext)) {
        hideNonImageWidgets();
        if (fmTextView) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                fmCurrentPreviewPath = path;
                QByteArray data = f.read(2*1024*1024);
                auto decodeText = [](const QByteArray &data) -> QString {
                    if (data.isEmpty()) return QString();
                    const uchar *b = reinterpret_cast<const uchar*>(data.constData());
                    const int n = data.size();
                    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { return QString::fromUtf8(reinterpret_cast<const char*>(b + 3), n - 3); }
                    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) { return QString::fromUtf16(reinterpret_cast<const ushort*>(b + 2), (n - 2) / 2); }
                    if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
                        const int ulen = (n - 2) / 2; QVector<ushort> buf; buf.resize(ulen);
                        for (int i = 0; i < ulen; ++i) buf[i] = (ushort(b[2 + 2*i]) << 8) | ushort(b[2 + 2*i + 1]);
                        return QString::fromUtf16(buf.constData(), ulen);
                    }
                    const int sample = qMin(n, 4096); int zeroEven = 0, zeroOdd = 0; for (int i = 0; i < sample; ++i) { if (b[i] == 0) { if ((i & 1) == 0) ++zeroEven; else ++zeroOdd; } }
                    if ((zeroOdd + zeroEven) > sample / 16) {
                        const bool le = (zeroOdd > zeroEven); const int ulen = n / 2; if (le) return QString::fromUtf16(reinterpret_cast<const ushort*>(b), ulen);
                        QVector<ushort> buf; buf.resize(ulen); for (int i = 0; i < ulen; ++i) buf[i] = (ushort(b[2*i]) << 8) | ushort(b[2*i + 1]); return QString::fromUtf16(buf.constData(), ulen);
                    }
                    QString s = QString::fromUtf8(reinterpret_cast<const char*>(b), n);
                    int bad = 0; const int check = qMin(s.size(), 4096);
                    for (int i = 0; i < check; ++i) if (s.at(i).unicode() == 0xFFFD) ++bad;
                    if (bad > check / 16) s = QString::fromLocal8Bit(reinterpret_cast<const char*>(b), n);
                    return s;
                };
                fmTextView->setPlainText(decodeText(data));
                fmTextView->show();
            } else {
                fmTextView->setPlainText("Preview not available"); fmTextView->show();
            }
        }
        return;
    }

    // Office: DOCX/DOC (text), XLSX (table)
    if (isDocxFile(ext)) {
        hideNonImageWidgets(); fmCurrentPreviewPath = path;
        if (fmTextView) { const QString text = extractDocxText(path); fmTextView->setFont(QFont("Segoe UI")); fmTextView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); fmTextView->setPlainText(text.isEmpty() ? QStringLiteral("Preview not available") : text); fmTextView->show(); }
        return;
    }
    if (isDocFile(ext)) {
        hideNonImageWidgets(); fmCurrentPreviewPath = path;
        if (fmTextView) { const QString text = extractDocBinaryText(path, 2*1024*1024); fmTextView->setFont(QFont("Segoe UI")); fmTextView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); fmTextView->setPlainText(text.isEmpty() ? QStringLiteral("Preview not available") : text); fmTextView->show(); }
        return;
    }
    if (isExcelFile(ext)) {
        hideNonImageWidgets(); fmCurrentPreviewPath = path;
        if (fmCsvModel && fmCsvView) { fmCsvModel->clear(); if (loadXlsxSheet(path, fmCsvModel, 2000)) { fmCsvView->resizeColumnsToContents(); fmCsvView->show(); } else if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); } }
        return;
    }

    if (isCsvFile(ext)) {
        hideNonImageWidgets();
        if (fmCsvModel && fmCsvView) {
            fmCsvModel->clear(); fmCurrentPreviewPath = path; QFile f(path);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream ts(&f); int row=0; QChar delim = ',';
                while (!ts.atEnd() && row<2000) {
                    const QString line = ts.readLine(); if (row == 0) { int cComma = line.count(','); int cSemi = line.count(';'); int cTab = line.count('\t'); if (cSemi > cComma && cSemi >= cTab) delim = ';'; else if (cTab > cComma && cTab >= cSemi) delim = '\t'; }
                    const QStringList cols = line.split(delim); if (row==0) fmCsvModel->setColumnCount(cols.size());
                    QList<QStandardItem*> items; items.reserve(cols.size()); for (const QString &c : cols) items << new QStandardItem(c.trimmed()); fmCsvModel->appendRow(items); ++row;
                }
                fmCsvView->resizeColumnsToContents(); fmCsvView->show();
            } else { if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); } }
        }
        return;
    }

    if (isAudioFile(ext) || isVideoFile(ext)) {
        fmCurrentPreviewPath = path;
#ifdef HAVE_FFMPEG
        // Stop any previous FFmpeg reader when switching assets
        stopFmFallbackVideo();
#endif
        if (isVideoFile(ext)) {
            if (fmVideoWidget) fmVideoWidget->hide();
            if (fmImageView) fmImageView->show();
            if (fmImageItem) { fmImageItem->setVisible(true); fmImageItem->setPixmap(QPixmap()); }
            if (fmVideoItem) fmVideoItem->setVisible(false);
            if (fmImageView) {
                fmImageView->resetTransform();
                if (fmImageItem)
                    fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                fmImageFitToView = true;
            }
            if (fmPrevFrameBtn) fmPrevFrameBtn->show();
            if (fmNextFrameBtn) fmNextFrameBtn->show();
            // Reset timeline until duration is known to avoid stale range from sequences
            if (fmPositionSlider) { fmPositionSlider->setRange(0, 0); fmPositionSlider->setValue(0); }
            if (fmTimeLabel) { fmTimeLabel->setText("00:00 / 00:00"); }
            // Show color space controls for videos (disabled for now; Rec.709)
            if (fmColorSpaceLabel) fmColorSpaceLabel->show();
            if (fmColorSpaceCombo) { fmColorSpaceCombo->show(); fmColorSpaceCombo->setEnabled(true); }

#ifdef HAVE_FFMPEG
            // Probe and route deterministically
            MediaInfo::VideoMetadata vm; QString perr;
            bool probed = MediaInfo::probeVideoFile(path, vm, &perr);
            bool useFFmpeg = false;
            if (probed) {
                const QString codec = vm.videoCodec.toLower();
                const QString container = QFileInfo(path).suffix().toLower();
                if (codec == "prores" || codec.startsWith("dnx") || codec == "qtrle" || codec == "png" || container == "mxf") {
                    useFFmpeg = true;
                }
            }
            if (useFFmpeg) {
                startFmFallbackVideo(path);
                if (fmPlayPauseBtn) fmPlayPauseBtn->setIcon(icoMediaPlay());
                return;
            }
#endif
            // Hardware path (WMF/D3D11 via QMediaPlayer)
            if (fmVideoItem) {
                fmVideoItem->setPos(0, 0);
                const QSizeF ns = fmVideoItem->nativeSize();
                if (ns.isValid()) {
                    fmVideoItem->setSize(ns);
                    if (fmImageScene) fmImageScene->setSceneRect(QRectF(QPointF(0, 0), ns));
                }
            }
            fmMediaPlayer->setVideoOutput(fmVideoSink);
            if (fmImageItem) fmImageItem->setVisible(true);
            if (fmVideoItem) fmVideoItem->setVisible(false);
            if (fmVideoItem && fmImageView && fmVideoItem->nativeSize().isValid() && fmImageFitToView) {
                fmImageView->resetTransform();
                fmImageView->fitInView(fmVideoItem, Qt::KeepAspectRatio);
            }
        } else {
            // audio-only: hide frame step buttons
            if (fmVideoWidget) fmVideoWidget->hide();
            if (fmImageView) fmImageView->hide();
            if (fmVideoItem) fmVideoItem->setVisible(false);
            if (fmPrevFrameBtn) fmPrevFrameBtn->hide();
            if (fmNextFrameBtn) fmNextFrameBtn->hide();
        }
        if (fmPlayPauseBtn) fmPlayPauseBtn->show(); if (fmPositionSlider) fmPositionSlider->show(); if (fmTimeLabel) fmTimeLabel->show(); if (fmVolumeSlider) fmVolumeSlider->show(); if (fmMuteBtn) fmMuteBtn->show();
        if (fmMediaPlayer) { fmMediaPlayer->setSource(QUrl::fromLocalFile(path)); fmMediaPlayer->pause(); if (fmPlayPauseBtn) fmPlayPauseBtn->setIcon(icoMediaPlay()); }
        return;
    }

#ifdef HAVE_QT_PDF
    if (isAiFile(ext)) {
        auto err = fmPdfDoc ? fmPdfDoc->load(path) : QPdfDocument::Error::Unknown;
        if (fmPdfDoc && err == QPdfDocument::Error::None && fmPdfDoc->pageCount()>0) {
            hideNonImageWidgets(); fmPdfCurrentPage = 0; const QSizeF pts = fmPdfDoc->pagePointSize(fmPdfCurrentPage);
            int vw = fmImageView ? fmImageView->viewport()->width() : 800; if (vw < 1) vw = 800; int w = vw; int h = pts.width() > 0 ? int(pts.height() * (w / pts.width())) : w;
            QImage img = fmPdfDoc->render(fmPdfCurrentPage, QSize(w, h)); if (!img.isNull() && fmImageItem) {
                if (img.hasAlphaChannel()) { QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied); bg.fill(Qt::white); QPainter p(&bg); p.drawImage(0, 0, img); p.end(); img = bg; }
                fmImageItem->setPixmap(QPixmap::fromImage(img)); if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect()); if (fmImageView) { fmImageView->resetTransform(); fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio); fmImageFitToView = true; fmImageView->setBackgroundBrush(Qt::white); fmImageView->show(); }
            }
            if (fmPdfPrevBtn) fmPdfPrevBtn->show(); if (fmPdfNextBtn) fmPdfNextBtn->show(); if (fmPdfPageLabel) { fmPdfPageLabel->show(); fmPdfPageLabel->setText(QString("%1/%2").arg(1).arg(fmPdfDoc->pageCount())); }
#ifdef HAVE_QT_PDF_WIDGETS
            if (fmPdfView) fmPdfView->hide();
#endif
            return;
        }
        hideNonImageWidgets(); if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); } return;
    }
#else
    if (isAiFile(ext)) { hideNonImageWidgets(); if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); } return; }
#endif

    clearFmPreview();
}

void FileManagerWidget::changeFmPreview(int delta)
{
    if (!m_host || !m_host->previewOverlay) return;
    QModelIndex cur = fmOverlayCurrentIndex;
    if (!cur.isValid()) {
        QAbstractItemView* view = fmIsGridMode ? static_cast<QAbstractItemView*>(fmGridView)
                                               : static_cast<QAbstractItemView*>(fmListView);
        if (view) cur = view->currentIndex();
        if (!cur.isValid()) return;
        cur = cur.sibling(cur.row(), 0);
        fmOverlayCurrentIndex = QPersistentModelIndex(cur);
        fmOverlaySourceView = view;
    }
    QAbstractItemModel* model = const_cast<QAbstractItemModel*>(cur.model());
    if (!model) return;
    int newRow = cur.row() + delta;
    if (newRow < 0 || newRow >= model->rowCount(cur.parent())) return;
    QModelIndex next = model->index(newRow, 0, cur.parent());
    if (!next.isValid()) return;

    // Update context
    fmOverlayCurrentIndex = QPersistentModelIndex(next);
    if (fmOverlaySourceView) {
        fmOverlaySourceView->setCurrentIndex(next);
        fmOverlaySourceView->scrollTo(next, QAbstractItemView::PositionAtCenter);
    }

    // Grouped sequence representative
    if (fmProxyModel && fmGroupSequences && next.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(next)) {
        auto info = fmProxyModel->infoForProxyIndex(next);
        QStringList frames = m_host ? m_host->reconstructSequenceFramePaths(info.reprPath, info.start, info.end) : QStringList();
        if (!frames.isEmpty()) {

            int pad = 0;
            auto m = SequenceDetector::mainPattern().match(QFileInfo(info.reprPath).fileName());
            if (m.hasMatch()) pad = m.captured(3).length(); else pad = QString::number(info.start).length();
            QString s0 = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
            QString s1 = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
            QString seqName = QString("%1.[%2-%3].%4").arg(info.base, s0, s1, info.ext);
            m_host->previewOverlay->showSequence(frames, seqName, info.start, info.end);
            return;
        }
    }

    // Map to source and show asset
    QModelIndex srcIdx = next;
    if (fmProxyModel && next.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(next);
    const QString path = fmDirModel ? fmDirModel->filePath(srcIdx) : QString();
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (!fi.exists()) return;

    m_host->previewOverlay->showAsset(path, fi.fileName(), fi.suffix());
}



void FileManagerWidget::loadFmSequenceFrame(int index)
{
    if (fmSequenceFramePaths.isEmpty()) return;
    if (index < 0) index = 0;
    if (index >= fmSequenceFramePaths.size()) index = fmSequenceFramePaths.size() - 1;
    fmSequenceCurrentIndex = index;
    const QString path = fmSequenceFramePaths.at(index);

    if (fmPositionSlider) fmPositionSlider->setValue(index);
    if (fmTimeLabel) {
        const int frameNum = fmSequenceStartFrame + index;
        fmTimeLabel->setText(QString("Frame %1 / %2").arg(frameNum).arg(fmSequenceEndFrame));
    }

    // Coalesce requests: if a worker is still running, queue the latest index and return
    const int epoch = ++fmSeqLoadEpoch;
    if (fmSeqWorkerThread && fmSeqWorkerThread->isRunning()) {
        fmSeqHasPending = true;
        fmSeqPendingIndex = index;
        return;
    }
    if (fmSeqWorkerThread) { // finished previously
        fmSeqWorkerThread->deleteLater();
        fmSeqWorkerThread = nullptr;
    }

    // Prepare UI
    if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
    if (fmVideoWidget) fmVideoWidget->hide();
    if (fmImageView) fmImageView->show();

    // Worker thread to load the image frame (scaled to viewport)
    int targetW = 0, targetH = 0;
    if (fmImageView && fmImageView->viewport()) {
        targetW = fmImageView->viewport()->width();
        targetH = fmImageView->viewport()->height();
    }
    if (targetW <= 0 || targetH <= 0) { targetW = 1600; targetH = 1200; }

    // Determine color space ON UI THREAD to avoid racing UI from worker
    OIIOImageLoader::ColorSpace csToUse = OIIOImageLoader::ColorSpace::sRGB;
    if (fmColorSpaceCombo) {
        switch (fmColorSpaceCombo->currentIndex()) {
            case 0: csToUse = OIIOImageLoader::ColorSpace::Linear; break;
            case 1: csToUse = OIIOImageLoader::ColorSpace::sRGB;  break;
            case 2: csToUse = OIIOImageLoader::ColorSpace::Rec709; break;
        }
    }

    QThread* t = new QThread();
    fmSeqWorkerThread = t;
    QObject* ctx = new QObject();
    ctx->moveToThread(t);

    connect(t, &QThread::started, ctx, [this, ctx, path, epoch, targetW, targetH, csToUse]() {
        QImage img;
        if (OIIOImageLoader::isOIIOSupported(path)) {
            img = OIIOImageLoader::loadImage(path, targetW, targetH, csToUse);
        }
        if (img.isNull()) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            reader.setScaledSize(QSize(targetW, targetH));
            img = reader.read();
        }
        QMetaObject::invokeMethod(this, [this, img, epoch]() {
            if (epoch != fmSeqLoadEpoch) return; // stale
            if (img.isNull()) return;
            fmOriginalImage = img;
            if (fmImageItem) {
                fmImageItem->setPixmap(QPixmap::fromImage(img));
                fmImageItem->setTransformationMode(Qt::SmoothTransformation);
                if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                if (fmImageView) { fmImageView->resetTransform(); fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio); fmImageFitToView = true; fmImageView->show(); }
            }
        }, Qt::QueuedConnection);
        QMetaObject::invokeMethod(ctx, [ctx]() { ctx->deleteLater(); }, Qt::QueuedConnection);
        QThread::currentThread()->quit();
    });

    connect(t, &QThread::finished, this, [this, t]() {
        if (fmSeqWorkerThread == t) fmSeqWorkerThread = nullptr;
        t->deleteLater();
        if (fmSeqHasPending) {
            int idx = fmSeqPendingIndex;
            fmSeqHasPending = false;
            // Kick off the most recent pending frame
            QMetaObject::invokeMethod(this, [this, idx]() { loadFmSequenceFrame(idx); }, Qt::QueuedConnection);
        }
    });

    t->start();
}

void FileManagerWidget::playFmSequence()
{
    if (!fmSequenceTimer || fmSequenceFramePaths.isEmpty()) return;
    fmSequencePlaying = true;
    if (fmPlayPauseBtn) fmPlayPauseBtn->setIcon(icoMediaPause());
    const int intervalMs = int(1000.0 / qMax(1.0, fmSequenceFps));
    fmSequenceTimer->start(intervalMs);
}

void FileManagerWidget::pauseFmSequence()
{
    if (!fmSequenceTimer) return;
    fmSequencePlaying = false;
    fmSequenceTimer->stop();
    if (fmPlayPauseBtn) fmPlayPauseBtn->setIcon(icoMediaPlay());
}

void FileManagerWidget::stepFmSequence(int delta)
{
    if (fmSequenceFramePaths.isEmpty()) return;
    int next = fmSequenceCurrentIndex + delta;
    if (next < 0) next = 0;
    if (next >= fmSequenceFramePaths.size()) next = fmSequenceFramePaths.size() - 1;
    loadFmSequenceFrame(next);
}


void FileManagerWidget::updateFmInfoPanel()
{
    if (!fmInfoPanel) return;

    QAbstractItemView* view = fmIsGridMode ? static_cast<QAbstractItemView*>(fmGridView)
                                           : static_cast<QAbstractItemView*>(fmListView);
    if (!view || !view->selectionModel()) {
        if (fmInfoFileName) fmInfoFileName->setText("No selection");
        if (fmInfoFilePath) fmInfoFilePath->clear();
        if (fmInfoFileSize) fmInfoFileSize->clear();
        if (fmInfoFileType) fmInfoFileType->clear();
        if (fmInfoDimensions) fmInfoDimensions->clear();
        if (fmInfoCreated) fmInfoCreated->clear();
        if (fmInfoModified) fmInfoModified->clear();
        if (fmInfoPermissions) fmInfoPermissions->clear();
        return;
    }

    QModelIndexList sel = view->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) {
        if (fmInfoFileName) fmInfoFileName->setText("No selection");
        if (fmInfoFilePath) fmInfoFilePath->clear();
        if (fmInfoFileSize) fmInfoFileSize->clear();
        if (fmInfoFileType) fmInfoFileType->clear();
        if (fmInfoDimensions) fmInfoDimensions->clear();
        if (fmInfoCreated) fmInfoCreated->clear();
        if (fmInfoModified) fmInfoModified->clear();
        if (fmInfoPermissions) fmInfoPermissions->clear();
        return;
    }

    QModelIndex viewIdx = sel.first().sibling(sel.first().row(), 0);
    QModelIndex srcIdx = viewIdx;
    if (fmProxyModel && viewIdx.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(viewIdx);

    const QString path = fmDirModel ? fmDirModel->filePath(srcIdx) : QString();
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (!fi.exists()) return;

    if (fmInfoFileName) fmInfoFileName->setText(fi.fileName());
    if (fmInfoFilePath) fmInfoFilePath->setText(QDir::toNativeSeparators(fi.absoluteFilePath()));
    if (fmInfoFileSize) fmInfoFileSize->setText(fi.isDir() ? QString("<dir>") : humanReadableSize(fi.size()));
    if (fmInfoFileType) fmInfoFileType->setText(fi.suffix().toUpper());

    QString dimText;
    const QString ext = fi.suffix();
    if (isImageFile(ext)) {
        QImageReader r(path);
        QSize s = r.size();
        if (s.isValid()) dimText = QString("%1 x %2").arg(s.width()).arg(s.height());
    } else if (isVideoFile(ext)) {
        // Optional: lightweight metadata probe
#ifdef HAVE_FFMPEG_METADATA
        auto m = VideoMetadata::probe(path);
        if (m.width > 0 && m.height > 0) dimText = QString("%1 x %2").arg(m.width).arg(m.height);
#endif
    }
    if (fmInfoDimensions) fmInfoDimensions->setText(dimText);

    auto fmt = [](const QDateTime& dt) { return dt.toString("yyyy-MM-dd hh:mm:ss"); };
    if (fmInfoCreated) fmInfoCreated->setText(fmt(fi.birthTime()));
    if (fmInfoModified) fmInfoModified->setText(fmt(fi.lastModified()));

    if (fmInfoPermissions) {
        QFile::Permissions p = fi.permissions();
        auto f = [&](QFile::Permissions m, QChar c){ return (p & m) ? c : QChar('-'); };
        QString perm = QString("%1%2%3 %4%5%6 %7%8%9")
            .arg(f(QFile::ReadOwner, 'r')).arg(f(QFile::WriteOwner, 'w')).arg(f(QFile::ExeOwner, 'x'))
            .arg(f(QFile::ReadUser, 'r')).arg(f(QFile::WriteUser, 'w')).arg(f(QFile::ExeUser, 'x'))
            .arg(f(QFile::ReadGroup, 'r')).arg(f(QFile::WriteGroup, 'w')).arg(f(QFile::ExeGroup, 'x'));
        fmInfoPermissions->setText(perm);
    }
}

#ifdef HAVE_FFMPEG
void FileManagerWidget::startFmFallbackVideo(const QString &path)
{
    // Stop media player path
    if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }

    // Basic UI prep
    if (fmVideoItem) fmVideoItem->setVisible(false);
    if (fmImageView) fmImageView->show();
    if (fmImageItem) { fmImageItem->setVisible(true); fmImageItem->setPixmap(QPixmap()); }
    if (fmPrevFrameBtn) fmPrevFrameBtn->show();
    if (fmNextFrameBtn) fmNextFrameBtn->show();
    if (fmPlayPauseBtn) { fmPlayPauseBtn->show(); fmPlayPauseBtn->setIcon(icoMediaPlay()); }
    if (fmPositionSlider) { fmPositionSlider->show(); fmPositionSlider->setRange(0, 0); fmPositionSlider->setValue(0); }
    if (fmTimeLabel) { fmTimeLabel->show(); fmTimeLabel->setText("00:00 / 00:00"); }
    if (fmColorSpaceLabel) fmColorSpaceLabel->show();
    if (fmColorSpaceCombo) { fmColorSpaceCombo->show(); fmColorSpaceCombo->setEnabled(true); }

    // Probe duration and fps quickly
    fmFallbackDurationMs = 0;
    fmFallbackFps = 0.0;
    {
        AVFormatContext *fmt = nullptr;
        QByteArray nb = QFile::encodeName(path);
        if (avformat_open_input(&fmt, nb.constData(), nullptr, nullptr) == 0) {
            if (avformat_find_stream_info(fmt, nullptr) >= 0) {
                int vindex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
                if (vindex >= 0) {
                    AVStream *st = fmt->streams[vindex];
                    AVRational fr = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
                    if (fr.num && fr.den) fmFallbackFps = av_q2d(fr);
                    if (fmt->duration > 0) {
                        fmFallbackDurationMs = qint64(fmt->duration / (AV_TIME_BASE / 1000));
                    } else if (st->duration > 0) {
                        fmFallbackDurationMs = qint64(av_rescale_q(st->duration, st->time_base, AVRational{1,1000}));
                    }
                }
            }
            avformat_close_input(&fmt);
        }
    }
    if (fmPositionSlider && fmFallbackDurationMs > 0) fmPositionSlider->setRange(0, int(fmFallbackDurationMs));

    // Launch reader thread
    QThread *t = new QThread();
    fmFallbackThread = t;
    {
        QSettings s("AugmentCode", "KAssetManager");
        const bool drop = s.value("Playback/DropLateFrames", true).toBool();
        fmFallbackReader = new FfmpegVideoReader(path, drop);
    }
    fmFallbackReader->moveToThread(t);

    connect(t, &QThread::started, fmFallbackReader, &FfmpegVideoReader::start);
    connect(fmFallbackReader, &FfmpegVideoReader::frameReady, this, &FileManagerWidget::onFmFallbackFrameReady, Qt::QueuedConnection);
    connect(fmFallbackReader, &FfmpegVideoReader::finished, this, &FileManagerWidget::onFmFallbackFinished, Qt::QueuedConnection);
    connect(fmFallbackReader, &FfmpegVideoReader::finished, t, &QThread::quit);
    connect(t, &QThread::finished, fmFallbackReader, &QObject::deleteLater);
    connect(t, &QThread::finished, t, &QObject::deleteLater);

    // Ensure cleanup on widget destruction
    connect(this, &QObject::destroyed, fmFallbackReader, &FfmpegVideoReader::stop, Qt::DirectConnection);
    connect(this, &QObject::destroyed, t, &QThread::quit);

    fmUsingFallbackVideo = true;
    fmFallbackPaused = true;
    t->start();
}

void FileManagerWidget::stopFmFallbackVideo()
{
    if (!fmUsingFallbackVideo) return;
    fmUsingFallbackVideo = false;
    if (fmFallbackReader) {
        fmFallbackReader->stop();
    }
    if (fmFallbackThread) {
        fmFallbackThread->quit();
    }
    fmFallbackReader = nullptr;
    fmFallbackThread = nullptr;
}

static inline QString fmFmtTime(qint64 ms) {
    if (ms < 0) ms = 0;
    int h = int(ms / 3600000); ms %= 3600000;
    int m = int(ms / 60000);   ms %= 60000;
    int s = int(ms / 1000);
    return h > 0 ? QString::asprintf("%d:%02d:%02d", h, m, s)
                 : QString::asprintf("%02d:%02d", m, s);
}

void FileManagerWidget::onFmFallbackFrameReady(const QImage &image, qint64 ptsMs)
{
    if (!fmImageItem || !fmImageView) return;

    // Update pixmap; refit on size change
    const QSize sz = image.size();
    const bool sizeChanged = (sz != fmLastVideoPixmapSize);
    fmLastVideoPixmapSize = sz;
    fmLastVideoFrameRaw = image;
    fmImageItem->setPixmap(QPixmap::fromImage(image));
    fmImageItem->setTransformationMode(Qt::SmoothTransformation);

    if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
    if (sizeChanged && fmImageFitToView) {
        fmImageView->resetTransform();
        fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
    }

    // Timeline updates
    if (fmPositionSlider && !fmPositionSlider->isSliderDown()) fmPositionSlider->setValue(int(qMax<qint64>(0, ptsMs)));
    if (fmTimeLabel) {
        const qint64 d = fmFallbackDurationMs;
        fmTimeLabel->setText(QString("%1 / %2").arg(fmFmtTime(ptsMs), fmFmtTime(d)));
    }
}

void FileManagerWidget::onFmFallbackFinished()
{
    // Keep UI consistent
    if (fmPlayPauseBtn) fmPlayPauseBtn->setIcon(icoMediaPlay());
}
#endif // HAVE_FFMPEG

