#include "mainwindow.h"
#include "virtual_folders.h"
#include "assets_model.h"
#include "assets_table_model.h"
#include "tags_model.h"
#include "importer.h"
#include "db.h"
#include "preview_overlay.h"
#include "image_preview_overlay.h"
#include "oiio_image_loader.h"
#include "live_preview_manager.h"
#include "import_progress_dialog.h"
#include "settings_dialog.h"
#include "user_guide_dialog.h"
#include "star_rating_widget.h"
#include "project_folder_watcher.h"
#include "log_viewer_widget.h"
#include "progress_manager.h"
#include "file_ops.h"
#include "file_ops_dialog.h"
#include "log_manager.h"
#include "sequence_detector.h"
#include "context_preserver.h"
#include "database_health_agent.h"
#include "database_health_dialog.h"
#include "bulk_rename_dialog.h"
#include "everything_search_dialog.h"
#include "everything_folder_model.h"
#include "everything_search.h"
#include "theme_manager.h"
#include "projects_model.h"
#include "project_assets_model.h"
#include "project_folders_model.h"
#include "project_sequence_grouping_proxy_model.h"
#include "project_item_delegate.h"
#include "project_db.h"
#include "project_version_detector.h"
#include "project_manager_watcher.h"

#include "office_preview.h"
#include "file_manager_pane.h"

#include "media_convert_dialog.h"
#include "thumbnail_cache_manager.h"
#include "thumbnail_generator_dialog.h"
#include "thumbnail_generator_dialog.h"

#include <QTableWidget>
#include <QDialog>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenuBar>
#include <QSlider>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QItemSelectionModel>
#include <QSignalBlocker>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QCloseEvent>
#include <QMimeData>
#include <QUrl>
#include <QDataStream>
#include <cmath>
#include <limits>
#include <QProgressDialog>

#include "file_utils.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <psapi.h>
static size_t currentWorkingSetMB() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
        return static_cast<size_t>(pmc.WorkingSetSize / (1024ULL * 1024ULL));
    return 0;
}
#endif

#include <QProgressBar>
#include <QDirIterator>
#include <QStatusBar>
#include <QDrag>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QMoveEvent>
#include <QFuture>
#include <QFutureWatcher>
#include <QBrush>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>
#include <algorithm>
#include <QMenu>
#include <QAction>
#include <QActionGroup>

#include <QFutureWatcher>
#include <QFileSystemWatcher>

#include <QTimer>
#include <QScrollBar>
#include <QTableView>
#include <QStackedWidget>
#include <QStack>
#include <QCheckBox>
#include <QFileDialog>
#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <QToolButton>

#include <QStandardPaths>
#include <QTextOption>

#include <QVector>


#include <QPlainTextEdit>
#include <algorithm>
#include <QStandardItemModel>
#include <QDebug>
#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#endif
#ifdef HAVE_QT_PDF_WIDGETS
#include <QPdfView>
#endif
#include <QSvgRenderer>
#include <QGraphicsSvgItem>

#include <QImageReader>
#include <QThreadPool>
#include <QRunnable>

#include <QPointer>
#include <QHash>
#include <QSet>
#include <QCursor>

#include "asset_item_delegate.h"
#include "fm_item_delegate.h"
#include "grid_scrub.h"
#include "fm_views_ex.h"

namespace {

QHash<QString, QString> g_lastPreviewError;

constexpr qreal kScrubDefaultPosition = 0.0;
constexpr int kPreviewInset = 1; // minimize border between thumbnail and preview
constexpr auto kGithubUrl = "https://github.com/NickPittas/KAssetManager";

QRect insetPreviewRect(const QRect &source)
{
    QRect result = source.adjusted(kPreviewInset, kPreviewInset, -kPreviewInset, -kPreviewInset);
    if (result.width() <= 0 || result.height() <= 0) {
        return source;
    }
    return result;
}

}


// Forward declare helper
// Lightweight icon painters (no external resources) for toolbar buttons
#include <functional>
#include "icon_utils.h"
#include "sequence_grouping_proxy_model.h"
#include "asset_sequence_grouping_proxy_model.h"
#include <QDockWidget>

#include <QEventLoop>

#include <QDesktopServices>
#include <QSize>
#include <QShortcut>
#include <QKeySequence>
#include <QSettings>

#include <QFileIconProvider>



#include "video_metadata.h"
#include "drag_utils.h"
#include <QSet>


#include "virtual_drag.h"

// Custom QListView with compact drag pixmap
#include "asset_grid_view.h"





// Custom delegate for asset grid view with live previews





MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , mainSplitter(nullptr)
    , rightSplitter(nullptr)
    , anchorIndex(-1)
    , currentAssetId(-1)
    , previewIndex(-1)
    , previewOverlay(nullptr)
    , importer(nullptr)
    , projectFolderWatcher(nullptr)
    , assetsLocked(true) // Locked by default
    , assetsModel(nullptr)
{
    fileOpsDialog = nullptr;
    LogManager::instance().addLog("[MAINWINDOW] ctor begin");

    // Load theme before UI setup
    ThemeManager::instance().loadTheme();

    // CRITICAL: Initialize GStreamer early to avoid 8+ second delay on first video
    // This must be done before any thumbnail generation or video playback
    GStreamerPlayer::initialize();

    // Load LivePreview cache size setting
    {
        QSettings s("AugmentCode", "KAssetManager");
        int cacheSize = s.value("LivePreview/MaxCacheEntries", 256).toInt();
        LivePreviewManager::instance().setMaxCacheEntries(cacheSize);
    }

    // Load theme from settings before setting up UI
    ThemeManager::instance().loadTheme();

    m_initializing = true;
    setupUi();
    setupConnections();
    m_initializing = false;

    // Connect to theme changes
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &MainWindow::applyTheme);
#ifdef QT_DEBUG
    qDebug() << "[INIT] [PREVIEW_CAPS] QtPdf="
#ifdef HAVE_QT_PDF
             << "ON";
#else
             << "OFF";
#endif
    qDebug() << "[INIT] [PREVIEW_CAPS] ActiveQt="
#ifdef HAVE_QT_AX
             << "ON";
#else
             << "OFF";
#endif
#endif

    setWindowTitle(QString("KAsset Manager %1").arg(QCoreApplication::applicationVersion()));
    resize(1400, 900);

    // Enable drag and drop
    setAcceptDrops(true);

    // Create importer
    importer = new Importer(this);
    connect(importer, &Importer::progressChanged, this, &MainWindow::onImportProgress);
    connect(importer, &Importer::currentFileChanged, this, &MainWindow::onImportFileChanged);
    connect(importer, &Importer::currentFolderChanged, this, &MainWindow::onImportFolderChanged);
    connect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);

    // Create project folder watcher
    projectFolderWatcher = new ProjectFolderWatcher(this);
    connect(projectFolderWatcher, &ProjectFolderWatcher::projectFolderChanged,
            this, &MainWindow::onProjectFolderChanged);

    // Load existing project folders into watcher
    auto projectFolders = DB::instance().listProjectFolders();
    for (const auto& pf : projectFolders) {
        int projectFolderId = pf.first;
        QString path = pf.second.second;
        projectFolderWatcher->addProjectFolder(projectFolderId, path);
    }

    // Create import progress dialog (will be shown when needed)
    importProgressDialog = nullptr;

    // Setup live preview progress bar in status bar
    thumbnailProgressLabel = new QLabel(this);
    thumbnailProgressLabel->setVisible(false);
    thumbnailProgressBar = new QProgressBar(this);
    thumbnailProgressBar->setVisible(false);
    thumbnailProgressBar->setMaximumWidth(200);
    thumbnailProgressBar->setTextVisible(true);
    statusBar()->addPermanentWidget(thumbnailProgressLabel);
    statusBar()->addPermanentWidget(thumbnailProgressBar);

    // Debounced timer for visible-only preview progress

    // File Manager auto-refresh: watch current directory and debounce refreshes
    fmDirectoryWatcher = new QFileSystemWatcher(this);
    fmDirChangeTimer.setSingleShot(true);
    // Light refresh on FS events to avoid flicker and massive re-requests
    connect(&fmDirChangeTimer, &QTimer::timeout, this, &MainWindow::onFmLightRefresh);
    connect(fmDirectoryWatcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString&){ fmDirChangeTimer.start(1200); });

    visibleThumbTimer.setSingleShot(true);
    connect(&visibleThumbTimer, &QTimer::timeout, this, &MainWindow::updateVisibleThumbProgress);

    // Timer to detect when window resize/move has stopped
    m_resizeSettleTimer.setSingleShot(true);
    m_resizeSettleTimer.setInterval(50); // Short delay to batch rapid events
    connect(&m_resizeSettleTimer, &QTimer::timeout, this, [this]() {
        m_windowResizing = false;
        // Resume thumbnail requests now that resize is done
        LivePreviewManager::instance().resumeRequests();
        // Trigger a single viewport update
        if (assetGridView && assetGridView->viewport()) assetGridView->viewport()->update();
        if (fmGridView && fmGridView->viewport()) fmGridView->viewport()->update();
        // Schedule progress update (debounced)
        scheduleVisibleThumbProgressUpdate();
    });

    // Debounce timer for splitter state persistence (avoids disk I/O during resize)
    m_splitterSaveTimer.setSingleShot(true);
    m_splitterSaveTimer.setInterval(300); // Save after 300ms of no splitter movement
    connect(&m_splitterSaveTimer, &QTimer::timeout, this, [this]() {
        QSettings s("AugmentCode", "KAssetManager");
        // Save all splitter states at once
        if (fmSplitter) {
            s.setValue("FileManager/MainSplitter", fmSplitter->saveState());
            QVariantList sizes; for (int v : fmSplitter->sizes()) sizes << v;
            s.setValue("FileManager/MainSplitterSizes", sizes);
        }
        if (fmLeftSplitter) {
            s.setValue("FileManager/LeftSplitter", fmLeftSplitter->saveState());
            QVariantList sizes; for (int v : fmLeftSplitter->sizes()) sizes << v;
            s.setValue("FileManager/LeftSplitterSizes", sizes);
        }
        if (fmRightSplitter) {
            s.setValue("FileManager/RightSplitter", fmRightSplitter->saveState());
            QVariantList sizes; for (int v : fmRightSplitter->sizes()) sizes << v;
            s.setValue("FileManager/RightSplitterSizes", sizes);
        }
        if (fmPreviewInfoSplitter) {
            s.setValue("FileManager/PreviewInfoSplitter", fmPreviewInfoSplitter->saveState());
            QVariantList sizes; for (int v : fmPreviewInfoSplitter->sizes()) sizes << v;
            s.setValue("FileManager/PreviewInfoSplitterSizes", sizes);
        }
        if (fmDualPaneSplitter && fmDualPaneSplitter->isVisible()) {
            s.setValue("FileManager/DualPaneSplitter", fmDualPaneSplitter->saveState());
            QVariantList sizes; for (int v : fmDualPaneSplitter->sizes()) sizes << v;
            s.setValue("FileManager/DualPaneSplitterSizes", sizes);
        }
        if (pmSplitter) s.setValue("ProjectManager/MainSplitter", pmSplitter->saveState());
        if (pmLeftSplitter) s.setValue("ProjectManager/LeftSplitter", pmLeftSplitter->saveState());
        if (pmRightSplitter) s.setValue("ProjectManager/RightSplitter", pmRightSplitter->saveState());
        if (pmPreviewInfoSplitter) s.setValue("ProjectManager/PreviewInfoSplitter", pmPreviewInfoSplitter->saveState());
        s.sync();
    });

    // Update views when live preview frames arrive
    connect(&LivePreviewManager::instance(), &LivePreviewManager::frameReady,
            this, [this](const QString& filePath, qreal, QSize, const QPixmap& pixmap) {
        g_lastPreviewError.remove(filePath);
        // Skip viewport updates during resize to avoid UI lag
        if (!m_windowResizing) {
            if (assetGridView && assetGridView->viewport()) assetGridView->viewport()->update();
            if (fmGridView && fmGridView->viewport()) fmGridView->viewport()->update();
            if (pmAssetsGridView && pmAssetsGridView->viewport()) pmAssetsGridView->viewport()->update();
            if (pmAssetsTableView && pmAssetsTableView->viewport()) pmAssetsTableView->viewport()->update();
        }
        versionPreviewCache[filePath] = pixmap;
        if (versionTable) {
            for (int row = 0; row < versionTable->rowCount(); ++row) {
                QTableWidgetItem *iconItem = versionTable->item(row, 0);
                if (iconItem && iconItem->data(Qt::UserRole).toString() == filePath) {
                    iconItem->setIcon(QIcon(pixmap));
                    iconItem->setText(QString());
                }
            }
        }
        // Update PM preview panel if this is the currently selected item
        if (!pmCurrentPreviewPath.isEmpty() && filePath == pmCurrentPreviewPath) {
            if (pmImageScene && pmImageView) {
                pmImageScene->clear();
                pmImageItem = pmImageScene->addPixmap(pixmap);
                pmImageScene->setSceneRect(pmImageItem->boundingRect());
                pmImageView->fitInView(pmImageItem, Qt::KeepAspectRatio);
            }
        }
        visibleThumbTimer.start(50);
    });
    connect(&LivePreviewManager::instance(), &LivePreviewManager::frameFailed,
            this, [](const QString& path, const QString& error) {
        QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            return;
        }
        if (!FileUtils::isPreviewableSuffix(info.suffix())) {
            return;
        }
        const QString last = g_lastPreviewError.value(path);
        if (last == error) {
            return;
        }
        g_lastPreviewError.insert(path, error);
        qWarning() << "[LivePreview] failed for" << path << ':' << error;
    });

}

MainWindow::~MainWindow()
{
}

void MainWindow::onShowUserGuide()
{
    UserGuideDialog dialog(this);
    dialog.exec();
}

void MainWindow::onAboutKAssetManager()
{
    const QString version = QCoreApplication::applicationVersion();

    QString text;
    text += "<h3>KAsset Manager</h3>";
    if (!version.isEmpty()) {
        text += QString("<p><b>Version:</b> %1</p>").arg(version.toHtmlEscaped());
    }
    text += "<p><b>Author:</b> Nick Pittas</p>";
    text += "<p><b>License:</b> MIT License</p>";
    text += "<p><b>Documentation:</b> Press F1 or select Help → User Guide</p>";
    text += QString("<p><b>GitHub:</b> <a href=\"%1\">%1</a></p>")
                .arg(QString::fromUtf8(kGithubUrl));

    QMessageBox aboutBox(this);
    aboutBox.setWindowTitle("About KAsset Manager");
    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setText(text);
    aboutBox.setIcon(QMessageBox::Information);
    aboutBox.exec();
}

void MainWindow::setupUi()
{
    LogManager::instance().addLog("[TRACE] setupUi enter", "DEBUG");
    // Menu bar
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    QMenu* fileMenu = menuBar->addMenu("&File");

    QAction* addProjectFolderAction = fileMenu->addAction("Add &Project Folder...");
    addProjectFolderAction->setShortcut(QKeySequence("Ctrl+P"));
    connect(addProjectFolderAction, &QAction::triggered, this, &MainWindow::onAddProjectFolder);

    fileMenu->addSeparator();

    QAction* settingsAction = fileMenu->addAction("&Settings");
    settingsAction->setShortcut(QKeySequence("Ctrl+,"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence("Ctrl+Q"));
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);

    // View menu
    QMenu* viewMenu = menuBar->addMenu("&View");

    toggleLogViewerAction = viewMenu->addAction("Show &Log Viewer");
    toggleLogViewerAction->setShortcut(QKeySequence("Ctrl+L"));
    toggleLogViewerAction->setCheckable(true);
    toggleLogViewerAction->setChecked(false);
    connect(toggleLogViewerAction, &QAction::triggered, this, &MainWindow::onToggleLogViewer);

    // Tools menu
    QMenu* toolsMenu = menuBar->addMenu("&Tools");

    QAction* dbHealthAction = toolsMenu->addAction("Database &Health...");
    dbHealthAction->setShortcut(QKeySequence("Ctrl+H"));
    connect(dbHealthAction, &QAction::triggered, this, &MainWindow::showDatabaseHealthDialog);

    // Help menu
    QMenu* helpMenu = menuBar->addMenu("&Help");

    QAction* userGuideAction = helpMenu->addAction("&User Guide...");
    userGuideAction->setShortcut(QKeySequence::HelpContents);
    connect(userGuideAction, &QAction::triggered, this, &MainWindow::onShowUserGuide);

    QAction* aboutAction = helpMenu->addAction("&About KAsset Manager...");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAboutKAssetManager);

    // Tabs: Asset Manager | File Manager
    mainTabs = new QTabWidget(this);
    mainTabs->setDocumentMode(true);
    mainTabs->setTabsClosable(false);
    setCentralWidget(mainTabs);

    // Asset Manager page
    assetManagerPage = new QWidget(this);
    QVBoxLayout* amLayout = new QVBoxLayout(assetManagerPage);
    amLayout->setContentsMargins(0, 0, 0, 0);

    // Main splitter: left (folders) | center (assets) | right (filters+info)
    mainSplitter = new QSplitter(Qt::Horizontal, assetManagerPage);
    amLayout->addWidget(mainSplitter);

    // Left panel: Folder tree with recursive checkbox
    QWidget* leftPanel = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    folderTreeView = new QTreeView(leftPanel);
    folderModel = new VirtualFolderTreeModel(leftPanel);
    LogManager::instance().addLog("[TRACE] folder model created", "DEBUG");
    folderTreeView->setModel(folderModel);
    LogManager::instance().addLog("[TRACE] folder model set on tree", "DEBUG");

    // Connect model reset signals to preserve tree state across reloads
    connect(folderModel, &QAbstractItemModel::modelAboutToBeReset, this, &MainWindow::onAssetFoldersModelAboutToReset);
    connect(folderModel, &QAbstractItemModel::modelReset, this, &MainWindow::onAssetFoldersModelReset);

    folderTreeView->setHeaderHidden(true);
    folderTreeView->setContextMenuPolicy(Qt::CustomContextMenu);

    // Enable multi-selection with Ctrl+Click and Shift+Click
    folderTreeView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // Allow normal expand/collapse behavior like Windows Explorer
    folderTreeView->setExpandsOnDoubleClick(false);


    leftLayout->addWidget(folderTreeView);

    // Recursive checkbox at bottom of folder pane
    recursiveCheckBox = new QCheckBox("Include subfolder contents", leftPanel);
    recursiveCheckBox->setToolTip("When checked, shows assets from selected folder and all its subfolders");
    {
        QSettings s("AugmentCode","KAssetManager");
        const bool saved = s.value("AssetManager/IncludeSubfolders", false).toBool();
        recursiveCheckBox->setChecked(saved);
        if (assetsModel) assetsModel->setRecursiveMode(saved);
    }
    connect(recursiveCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (assetsModel) assetsModel->setRecursiveMode(checked);
        QSettings s("AugmentCode","KAssetManager");
        s.setValue("AssetManager/IncludeSubfolders", checked);
    });

    leftLayout->addWidget(recursiveCheckBox);

    // Center panel: Asset grid with toolbar
    QWidget* centerPanel = new QWidget(this);
    QVBoxLayout* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    // Toolbar for view controls
    amToolbar = new QWidget(centerPanel);
    QHBoxLayout* toolbarLayout = new QHBoxLayout(amToolbar);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);
    toolbarLayout->setSpacing(6);

    // Navigation buttons (Back and Up) at the left
    amBackButton = new QToolButton(amToolbar);
    amBackButton->setIcon(icoBack(ThemeManager::instance().iconColor()));
    amBackButton->setToolTip("Back");
    amBackButton->setAutoRaise(true);
    amBackButton->setIconSize(QSize(20, 20));
    amBackButton->setEnabled(false);
    connect(amBackButton, &QToolButton::clicked, this, &MainWindow::onAssetNavigateBack);
    toolbarLayout->addWidget(amBackButton);

    amUpButton = new QToolButton(amToolbar);
    amUpButton->setIcon(icoUp(ThemeManager::instance().iconColor()));
    amUpButton->setToolTip("Up");
    amUpButton->setAutoRaise(true);
    amUpButton->setIconSize(QSize(20, 20));
    amUpButton->setEnabled(false);
    connect(amUpButton, &QToolButton::clicked, this, &MainWindow::onAssetNavigateUp);
    toolbarLayout->addWidget(amUpButton);

    // New Folder button
    amNewFolderButton = new QToolButton(amToolbar);
    amNewFolderButton->setIcon(icoFolderNew(ThemeManager::instance().iconColor()));
    amNewFolderButton->setToolTip("New Folder");
    amNewFolderButton->setAutoRaise(true);
    amNewFolderButton->setIconSize(QSize(20, 20));
    connect(amNewFolderButton, &QToolButton::clicked, this, &MainWindow::onAssetNewFolder);
    toolbarLayout->addWidget(amNewFolderButton);

    // View mode toggle button
    isGridMode = true;
    viewModeButton = new QToolButton(amToolbar);
    viewModeButton->setIcon(icoGrid(ThemeManager::instance().iconColor()));
    viewModeButton->setToolTip("Toggle Grid/List");
    viewModeButton->setAutoRaise(true);
    viewModeButton->setIconSize(QSize(20,20));
    connect(viewModeButton, &QToolButton::clicked, this, &MainWindow::onViewModeChanged);
    toolbarLayout->addWidget(viewModeButton);

    // Thumbnail size label
    amSizeLabel = new QLabel("Size:", amToolbar);
    toolbarLayout->addWidget(amSizeLabel);

    // Thumbnail size slider
    thumbnailSizeSlider = new QSlider(Qt::Horizontal, amToolbar);
    thumbnailSizeSlider->setRange(100, 400);
    thumbnailSizeSlider->setValue(180);
    thumbnailSizeSlider->setFixedWidth(150);
    thumbnailSizeSlider->setToolTip("Adjust thumbnail size");
    connect(thumbnailSizeSlider, &QSlider::valueChanged, this, &MainWindow::onThumbnailSizeChanged);
    toolbarLayout->addWidget(thumbnailSizeSlider);

    // Size value label
    amSizeValueLabel = new QLabel("180px", amToolbar);
    amSizeValueLabel->setMinimumWidth(45);
    connect(thumbnailSizeSlider, &QSlider::valueChanged, [this](int value) {
        if (amSizeValueLabel) amSizeValueLabel->setText(QString("%1px").arg(value));
    });
    toolbarLayout->addWidget(amSizeValueLabel);

    toolbarLayout->addStretch();

    // Group image sequences toggle
    amGroupSequencesButton = new QToolButton(amToolbar);
    amGroupSequencesButton->setIcon(icoGroup(ThemeManager::instance().iconColor()));
    amGroupSequencesButton->setToolTip("Group image sequences into single entries");
    amGroupSequencesButton->setCheckable(true);
    amGroupSequencesButton->setAutoRaise(true);
    amGroupSequencesButton->setIconSize(QSize(20, 20));
    amGroupSequencesButton->setProperty("class", "toggle");
    connect(amGroupSequencesButton, &QToolButton::toggled, this, &MainWindow::onAssetGroupSequencesToggled);
    toolbarLayout->addWidget(amGroupSequencesButton);

    // Lock checkbox for project folders
    lockCheckBox = new QCheckBox("🔒 Lock Assets", amToolbar);
    lockCheckBox->setChecked(true); // Locked by default
    lockCheckBox->setToolTip("When locked, assets can only be moved within their project folder");
    connect(lockCheckBox, &QCheckBox::toggled, this, &MainWindow::onLockToggled);
    toolbarLayout->addWidget(lockCheckBox);

    // Live preview prefetch button (with menu)
    thumbGenButton = new QToolButton(amToolbar);
    thumbGenButton->setIcon(icoRefresh(ThemeManager::instance().iconColor()));
    thumbGenButton->setToolTip("Prefetch live previews");
    thumbGenButton->setAutoRaise(true);
    thumbGenButton->setIconSize(QSize(20,20));
    QMenu *genMenu = new QMenu(thumbGenButton);
    QAction *actGen = genMenu->addAction("Prefetch for this folder");
    QAction *actRegen = genMenu->addAction("Refresh for this folder");
    genMenu->addSeparator();
    QAction *actGenRec = genMenu->addAction("Prefetch recursive");
    QAction *actRegenRec = genMenu->addAction("Refresh recursive");
    connect(actGen, &QAction::triggered, this, &MainWindow::onPrefetchLivePreviewsForFolder);
    connect(actRegen, &QAction::triggered, this, &MainWindow::onRefreshLivePreviewsForFolder);
    connect(actGenRec, &QAction::triggered, this, &MainWindow::onPrefetchLivePreviewsRecursive);
    connect(actRegenRec, &QAction::triggered, this, &MainWindow::onRefreshLivePreviewsRecursive);
    thumbGenButton->setMenu(genMenu);
    thumbGenButton->setPopupMode(QToolButton::MenuButtonPopup);
    toolbarLayout->addWidget(thumbGenButton);

    // Refresh button
    refreshButton = new QPushButton(amToolbar);
    refreshButton->setIcon(icoRefresh());
    refreshButton->setToolTip("Refresh assets from project folders");
    refreshButton->setFixedSize(28, 28);
    refreshButton->setFlat(true);
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshAssets);
    toolbarLayout->addWidget(refreshButton);

    // Everything Search button
    QPushButton* searchButton = new QPushButton(this);
    searchButton->setIcon(icoSearch());
    searchButton->setToolTip("Everything Search - Search entire disk");
    searchButton->setFixedSize(28, 28);
    searchButton->setFlat(true);
    connect(searchButton, &QPushButton::clicked, this, &MainWindow::onEverythingSearchAssetManager);
    toolbarLayout->addWidget(searchButton);

    centerLayout->addWidget(amToolbar);


    // Stacked widget to switch between grid and table views
    viewStack = new QStackedWidget(centerPanel);

    // Asset grid view (using custom AssetGridView with compact drag pixmap)
    assetGridView = new AssetGridView(viewStack);
    assetsModel = new AssetsModel(viewStack);

    // Create proxy model for sequence grouping/ungrouping
    amProxyModel = new AssetSequenceGroupingProxyModel(viewStack);
    amProxyModel->setSourceModel(assetsModel);

    assetGridView->setModel(amProxyModel);
    LogManager::instance().addLog("[TRACE] assetGridView + model wired", "DEBUG");
    assetGridView->setViewMode(QListView::IconMode);
    assetGridView->setResizeMode(QListView::Adjust);
    assetGridView->setSpacing(4);
    assetGridView->setUniformItemSizes(true);
    assetGridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    assetGridView->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Create delegate and set the view for scrub frame support
    auto *assetDelegate = new AssetItemDelegate(viewStack);
    assetDelegate->setView(assetGridView);
    assetGridView->setItemDelegate(assetDelegate);

    assetGridView->setIconSize(QSize(180, 180));
    // Batched layout mode for better resize performance
    assetGridView->setLayoutMode(QListView::Batched);
    assetGridView->setBatchSize(100);
    viewStack->addWidget(assetGridView); // Index 0

    // Asset table view for list mode
    assetTableView = new QTableView(viewStack);
    // Use the proxy model for table view as well to support grouping/ungrouping
    AssetsTableModel* tableModel = new AssetsTableModel(amProxyModel, viewStack);
    assetTableView->setModel(tableModel);
    assetTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    assetTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    assetTableView->setContextMenuPolicy(Qt::CustomContextMenu);
    assetTableView->setSortingEnabled(true);
    assetTableView->setAlternatingRowColors(false);
    assetTableView->setShowGrid(false);
    assetTableView->verticalHeader()->setVisible(false);
    assetTableView->verticalHeader()->setDefaultSectionSize(22);
    assetTableView->verticalHeader()->setMinimumSectionSize(18);
    assetTableView->horizontalHeader()->setStretchLastSection(true);
    // Persist assetTableView column widths immediately when resized
    connect(assetTableView->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logical, int /*oldSize*/, int newSize){
        QSettings s("AugmentCode", "KAssetManager");
        s.setValue(QString("AssetManager/AssetTable/Col%1").arg(logical), newSize);
    });

    // Set column widths
    assetTableView->setColumnWidth(AssetsTableModel::NameColumn, 300);
    assetTableView->setColumnWidth(AssetsTableModel::ExtensionColumn, 80);
    assetTableView->setColumnWidth(AssetsTableModel::SizeColumn, 100);

    assetTableView->setColumnWidth(AssetsTableModel::DateColumn, 150);

    assetTableView->setColumnWidth(AssetsTableModel::RatingColumn, 100);
    viewStack->addWidget(assetTableView); // Index 1


    // Set grid view as default
    viewStack->setCurrentIndex(0);

    centerLayout->addWidget(viewStack);

    // Enable drag-and-drop
    assetGridView->setDragEnabled(true);
    assetGridView->setAcceptDrops(false);
    assetGridView->setDragDropMode(QAbstractItemView::DragOnly);
    assetGridView->setDefaultDropAction(Qt::MoveAction);
    assetGridView->setSelectionRectVisible(false);

    // Asset table view drag (list mode)
    assetTableView->setDragEnabled(true);
    assetTableView->setAcceptDrops(false);
    assetTableView->setDragDropMode(QAbstractItemView::DragOnly);
    assetTableView->setDefaultDropAction(Qt::MoveAction);

    // Enable drag-and-drop on folder tree for moving assets to folders AND reorganizing folders
    folderTreeView->setDragEnabled(true);
    folderTreeView->setAcceptDrops(true);
    folderTreeView->setDropIndicatorShown(true);
    folderTreeView->setDragDropMode(QAbstractItemView::DragDrop);
    folderTreeView->setDefaultDropAction(Qt::MoveAction);

    folderTreeView->viewport()->installEventFilter(this);

    // Install event filter on asset views to handle Space key for preview
    assetGridView->installEventFilter(this);
    assetTableView->installEventFilter(this);
    // Also monitor viewport resize to update visible-only progress
    assetGridView->viewport()->installEventFilter(this);
    assetTableView->viewport()->installEventFilter(this);


    assetScrubController = new GridScrubController(
        assetGridView,
        [](const QModelIndex& idx) -> QString {
            // Use idx.data() which properly accesses data through the model hierarchy
            // (works correctly with proxy models)
            return idx.data(AssetsModel::FilePathRole).toString();
        },
        this);
    LogManager::instance().addLog("[TRACE] assetScrubController ready", "DEBUG");

    // Right panel: Filters + Info
    rightPanel = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // Filters panel
    filtersPanel = new QWidget(this);
    QVBoxLayout *filtersLayout = new QVBoxLayout(filtersPanel);
    filtersLayout->setContentsMargins(8, 8, 8, 8);

    QLabel *filtersTitle = new QLabel("Filters", this);
    QFont filtersTitleFont = filtersTitle->font();
    filtersTitleFont.setPointSize(14);
    filtersTitleFont.setBold(true);
    filtersTitle->setFont(filtersTitleFont);
    filtersLayout->addWidget(filtersTitle);

    // Search box with button
    QHBoxLayout *searchLayout = new QHBoxLayout();

    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText("Search... (Press Enter)");
    searchLayout->addWidget(searchBox);

    filterSearchButton = new QPushButton(this);
    filterSearchButton->setIcon(icoSearch());
    filterSearchButton->setToolTip("Search assets");
    filterSearchButton->setFixedSize(28, 28);
    filterSearchButton->setProperty("class", "accent");
    connect(filterSearchButton, &QPushButton::clicked, this, [this]() {
        onSearchTextChanged(searchBox->text());
    });
    searchLayout->addWidget(filterSearchButton);

    filtersLayout->addLayout(searchLayout);

    // Search scope override: Search Entire Database
    searchEntireDbCheckBox = new QCheckBox("Search Entire Database", filtersPanel);
    searchEntireDbCheckBox->setCheckable(true);
    searchEntireDbCheckBox->setEnabled(true);
    searchEntireDbCheckBox->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    searchEntireDbCheckBox->setFocusPolicy(Qt::StrongFocus);
    {
        QSettings s("AugmentCode","KAssetManager");
        const bool saved = s.value("AssetManager/SearchEntireDatabase", false).toBool();
        searchEntireDbCheckBox->setChecked(saved);
        if (assetsModel) assetsModel->setSearchEntireDatabase(saved);
    }
    connect(searchEntireDbCheckBox, &QCheckBox::toggled, this, [this](bool on){
        if (assetsModel) assetsModel->setSearchEntireDatabase(on);

        QSettings s("AugmentCode","KAssetManager");
        s.setValue("AssetManager/SearchEntireDatabase", on);
    });
    filtersLayout->addWidget(searchEntireDbCheckBox);

    searchEntireDbCheckBox->setEnabled(true);

    QLabel *ratingLabel = new QLabel("Rating:", this);
    filtersLayout->addWidget(ratingLabel);

    ratingFilter = new QComboBox(this);
    ratingFilter->addItems({"All", "5 Stars", "4+ Stars", "3+ Stars", "Unrated"});
    connect(ratingFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        assetsModel->setRatingFilter(index);
    });
    filtersLayout->addWidget(ratingFilter);

    // Tags section with + button
    QHBoxLayout *tagsHeaderLayout = new QHBoxLayout();
    QLabel *tagsLabel = new QLabel("Tags:", this);
    tagsHeaderLayout->addWidget(tagsLabel);
    tagsHeaderLayout->addStretch();

    QPushButton *addTagBtn = new QPushButton("+", this);
    addTagBtn->setFixedSize(24, 24);
    addTagBtn->setProperty("class", "accent");
    addTagBtn->setToolTip("Create new tag");
    connect(addTagBtn, &QPushButton::clicked, this, &MainWindow::onCreateTag);
    tagsHeaderLayout->addWidget(addTagBtn);

    filtersLayout->addLayout(tagsHeaderLayout);

    // Tag action buttons (moved before tags list)
    QHBoxLayout *tagButtonsLayout = new QHBoxLayout();

    applyTagsBtn = new QPushButton("Apply", this);
    applyTagsBtn->setProperty("class", "accent");
    applyTagsBtn->setToolTip("Apply selected tags to selected assets");
    applyTagsBtn->setEnabled(false);
    connect(applyTagsBtn, &QPushButton::clicked, this, &MainWindow::onApplyTags);
    tagButtonsLayout->addWidget(applyTagsBtn);

    filterByTagsBtn = new QPushButton("Filter", this);
    filterByTagsBtn->setProperty("class", "accent");
    filterByTagsBtn->setToolTip("Filter assets by selected tags");
    filterByTagsBtn->setEnabled(false);
    connect(filterByTagsBtn, &QPushButton::clicked, this, &MainWindow::onFilterByTags);
    tagButtonsLayout->addWidget(filterByTagsBtn);

    // AND/OR mode selector
    tagFilterModeCombo = new QComboBox(this);
    tagFilterModeCombo->addItems({"AND", "OR"});
    tagFilterModeCombo->setCurrentIndex(0); // Default to AND
    qDebug() << "[INIT] Tag buttons and mode added";
    tagFilterModeCombo->setToolTip("AND: Assets must have ALL selected tags\nOR: Assets must have ANY selected tag");
    tagButtonsLayout->addWidget(tagFilterModeCombo);

    filtersLayout->addLayout(tagButtonsLayout);

    // Tags list view (moved after tag buttons)
    tagsListView = new QListView(filtersPanel);
    tagsModel = new TagsModel(this);
    tagsListView->setModel(tagsModel);
    tagsListView->setSelectionMode(QAbstractItemView::MultiSelection);
    tagsListView->setContextMenuPolicy(Qt::CustomContextMenu);
    tagsListView->setMaximumHeight(150);

    // Enable drops on tags list for assigning tags to assets
    tagsListView->setAcceptDrops(true);
    tagsListView->setDropIndicatorShown(true);
    tagsListView->setDragDropMode(QAbstractItemView::DropOnly);

    filtersLayout->addWidget(tagsListView);

    QPushButton *applyFiltersBtn = new QPushButton("Apply Filters", this);
    applyFiltersBtn->setProperty("class", "accent");
    connect(applyFiltersBtn, &QPushButton::clicked, this, &MainWindow::applyFilters);
    filtersLayout->addWidget(applyFiltersBtn);

    QPushButton *clearFiltersBtn = new QPushButton("Clear Filters", this);
    connect(clearFiltersBtn, &QPushButton::clicked, this, &MainWindow::clearFilters);
    filtersLayout->addWidget(clearFiltersBtn);

    filtersLayout->addStretch();

    // Info panel with scrollable area for all metadata
    infoPanel = new QWidget(this);
    QVBoxLayout *infoPanelLayout = new QVBoxLayout(infoPanel);
    infoPanelLayout->setContentsMargins(0, 0, 0, 0);
    infoPanelLayout->setSpacing(0);

    QLabel *infoTitle = new QLabel("Asset Info", this);
    QFont infoTitleFont = infoTitle->font();
    infoTitleFont.setPointSize(14);
    infoTitleFont.setBold(true);
    infoTitle->setFont(infoTitleFont);
    infoPanelLayout->addWidget(infoTitle);

    // Scrollable area for metadata
    QScrollArea *infoScrollArea = new QScrollArea(this);
    infoScrollArea->setWidgetResizable(true);
    infoScrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *infoScrollWidget = new QWidget();
    QVBoxLayout *infoLayout = new QVBoxLayout(infoScrollWidget);
    infoLayout->setContentsMargins(8, 8, 8, 8);
    infoLayout->setSpacing(4);

    infoFileName = new QLabel("No selection", this);
    QFont infoFileNameFont = infoFileName->font();
    infoFileNameFont.setBold(true);
    infoFileName->setFont(infoFileNameFont);
    infoFileName->setWordWrap(true);
    infoLayout->addWidget(infoFileName);

    infoFilePath = new QLabel("", this);
    infoFilePath->setWordWrap(true);
    infoLayout->addWidget(infoFilePath);

    // Add separator
    QFrame *separator1 = new QFrame(this);
    separator1->setFrameShape(QFrame::HLine);
    separator1->setFixedHeight(1);
    infoLayout->addWidget(separator1);

    infoFileSize = new QLabel("", this);
    infoFileSize->setWordWrap(true);
    infoLayout->addWidget(infoFileSize);

    infoFileType = new QLabel("", this);
    infoFileType->setWordWrap(true);
    infoLayout->addWidget(infoFileType);

    infoDimensions = new QLabel("", this);
    infoDimensions->setWordWrap(true);
    infoLayout->addWidget(infoDimensions);

    infoCreated = new QLabel("", this);
    infoCreated->setWordWrap(true);
    infoLayout->addWidget(infoCreated);

    infoModified = new QLabel("", this);
    infoModified->setWordWrap(true);
    infoLayout->addWidget(infoModified);

    infoPermissions = new QLabel("", this);
    infoPermissions->setWordWrap(true);
    infoLayout->addWidget(infoPermissions);

    // Rating widget
    QFrame *separator2 = new QFrame(this);
    separator2->setFrameShape(QFrame::HLine);
    separator2->setFixedHeight(1);
    infoLayout->addWidget(separator2);

    infoRatingLabel = new QLabel("Rating:", this);
    infoLayout->addWidget(infoRatingLabel);

    infoRatingWidget = new StarRatingWidget(this);
    infoLayout->addWidget(infoRatingWidget);
    connect(infoRatingWidget, &StarRatingWidget::ratingChanged, this, &MainWindow::onRatingChanged);

    infoTags = new QLabel("", this);
    infoTags->setWordWrap(true);
    infoLayout->addWidget(infoTags);

    // Separator before versions
    QFrame *separator3 = new QFrame(this);
    separator3->setFrameShape(QFrame::HLine);
    separator3->setFixedHeight(1);
    infoLayout->addWidget(separator3);

    // Version history section
    versionsTitleLabel = new QLabel("Version History", this);
    QFont versionsTitleFont = versionsTitleLabel->font();
    versionsTitleFont.setPointSize(13);
    versionsTitleFont.setBold(true);
    versionsTitleLabel->setFont(versionsTitleFont);
    infoLayout->addWidget(versionsTitleLabel);

    versionTable = new QTableWidget(this);
    versionTable->setColumnCount(5);
    QStringList headers; headers << "" << "Version" << "Date" << "Size" << "Notes";
    versionTable->setHorizontalHeaderLabels(headers);
    versionTable->verticalHeader()->setVisible(false);
    versionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    versionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    versionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    versionTable->setShowGrid(false);
    versionTable->setIconSize(QSize(48, 48));
    // Persist versionTable column widths immediately when resized
    connect(versionTable->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logical, int /*oldSize*/, int newSize){
        QSettings s("AugmentCode", "KAssetManager");
        s.setValue(QString("AssetManager/VersionTable/Col%1").arg(logical), newSize);
    });

    versionTable->setMaximumHeight(220);
    versionTable->setColumnWidth(0, 56);
    versionTable->setColumnWidth(1, 70);
    versionTable->setColumnWidth(2, 150);
    versionTable->setColumnWidth(3, 90);
    versionTable->horizontalHeader()->setStretchLastSection(true);
    infoLayout->addWidget(versionTable);

    QHBoxLayout *versionButtonsLayout = new QHBoxLayout();
    backupVersionCheck = new QCheckBox("Backup current version", this);
    backupVersionCheck->setChecked(true);
    revertVersionButton = new QPushButton("Revert to Selected", this);
    revertVersionButton->setProperty("class", "danger");
    revertVersionButton->setEnabled(false);
    connect(revertVersionButton, &QPushButton::clicked, this, &MainWindow::onRevertSelectedVersion);
    connect(versionTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection&, const QItemSelection&){
        revertVersionButton->setEnabled(versionTable->currentRow() >= 0);
    });
    versionButtonsLayout->addWidget(backupVersionCheck);
    versionButtonsLayout->addStretch();
    versionButtonsLayout->addWidget(revertVersionButton);
    infoLayout->addLayout(versionButtonsLayout);

    infoLayout->addStretch();
    infoScrollWidget->setLayout(infoLayout);
    infoScrollArea->setWidget(infoScrollWidget);
    infoPanelLayout->addWidget(infoScrollArea);

    rightLayout->addWidget(filtersPanel, 1);
    rightLayout->addWidget(infoPanel, 1);

    // Add panels to main splitter
    mainSplitter->addWidget(leftPanel);
    mainSplitter->addWidget(centerPanel);
    mainSplitter->addWidget(rightPanel);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 3);
    mainSplitter->setStretchFactor(2, 1);

    // Add Asset Manager page to tabs
    // File Manager page
    LogManager::instance().addLog("[TRACE] Creating File Manager page...", "DEBUG");
    fileManagerPage = new QWidget(this);
    LogManager::instance().addLog("[TRACE] Calling setupFileManagerUi()...", "DEBUG");
    setupFileManagerUi();
    LogManager::instance().addLog("[TRACE] setupFileManagerUi() completed", "DEBUG");
    mainTabs->addTab(fileManagerPage, "File Manager");

    // Add Asset Manager page to tabs
    mainTabs->addTab(assetManagerPage, "Asset Manager");

    // Project Manager page
    LogManager::instance().addLog("[TRACE] Creating Project Manager page...", "DEBUG");
    projectManagerPage = new QWidget(this);
    LogManager::instance().addLog("[TRACE] Calling setupProjectManagerUi()...", "DEBUG");
    setupProjectManagerUi();
    LogManager::instance().addLog("[TRACE] setupProjectManagerUi() completed", "DEBUG");
    mainTabs->addTab(projectManagerPage, "Project Manager");

    // Log viewer as dock widget at bottom (hidden by default)
    QDockWidget* logDock = new QDockWidget("Application Log", this);
    LogManager::instance().addLog("[TRACE] logDock created", "DEBUG");
    logDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    logDock->setFeatures(QDockWidget::DockWidgetClosable);
    logViewerWidget = new LogViewerWidget(logDock);
    logDock->setWidget(logViewerWidget);
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    logDock->hide(); // Hidden by default
    LogManager::instance().addLog("[TRACE] logDock initialised", "DEBUG");

    // Connect dock visibility to menu action
    connect(logDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        toggleLogViewerAction->setChecked(visible);
        if (visible) {
            toggleLogViewerAction->setText("Hide &Log Viewer");
        } else {
            toggleLogViewerAction->setText("Show &Log Viewer");
        }
    });
    LogManager::instance().addLog("[TRACE] logDock visibility hook set", "DEBUG");


    // Restore window and workspace state
    {
        QSettings s("AugmentCode", "KAssetManager");
        LogManager::instance().addLog("[TRACE] restore settings begin", "DEBUG");

        // Restore geometry and validate it's visible on screen
        bool geometryRestored = false;
        if (s.contains("Window/Geometry")) {
            geometryRestored = restoreGeometry(s.value("Window/Geometry").toByteArray());
            LogManager::instance().addLog(QString("[TRACE] restoreGeometry returned: %1").arg(geometryRestored), "DEBUG");
        }

        // Validate window is visible on at least one screen
        bool isVisible = false;
        QRect windowRect = frameGeometry();
        LogManager::instance().addLog(QString("[TRACE] Window geometry: x=%1 y=%2 w=%3 h=%4")
            .arg(windowRect.x()).arg(windowRect.y()).arg(windowRect.width()).arg(windowRect.height()), "DEBUG");

        for (QScreen *screen : QGuiApplication::screens()) {
            QRect screenGeometry = screen->geometry();
            LogManager::instance().addLog(QString("[TRACE] Screen '%1': x=%2 y=%3 w=%4 h=%5")
                .arg(screen->name()).arg(screenGeometry.x()).arg(screenGeometry.y())
                .arg(screenGeometry.width()).arg(screenGeometry.height()), "DEBUG");

            if (screenGeometry.intersects(windowRect)) {
                isVisible = true;
                LogManager::instance().addLog(QString("[TRACE] Window is visible on screen '%1'").arg(screen->name()), "DEBUG");
                break;
            }
        }

        // If window is not visible on any screen, reset to default position
        if (!isVisible || !geometryRestored) {
            LogManager::instance().addLog("[WARN] Window geometry is off-screen or invalid, resetting to default", "WARN");
            QScreen *primaryScreen = QGuiApplication::primaryScreen();
            if (primaryScreen) {
                QRect screenGeometry = primaryScreen->availableGeometry();
                // Center window on primary screen with default size
                int width = 1400;
                int height = 900;
                int x = screenGeometry.x() + (screenGeometry.width() - width) / 2;
                int y = screenGeometry.y() + (screenGeometry.height() - height) / 2;
                setGeometry(x, y, width, height);
                LogManager::instance().addLog(QString("[TRACE] Reset window to: x=%1 y=%2 w=%3 h=%4")
                    .arg(x).arg(y).arg(width).arg(height), "DEBUG");
            }
        }

        if (s.contains("Window/State")) restoreState(s.value("Window/State").toByteArray());
        LogManager::instance().addLog("[TRACE] restore window geometry/state done", "DEBUG");
        if (mainSplitter && s.contains("AssetManager/MainSplitter")) mainSplitter->restoreState(s.value("AssetManager/MainSplitter").toByteArray());
        LogManager::instance().addLog("[TRACE] restore mainSplitter state done", "DEBUG");
        if (rightSplitter && s.contains("AssetManager/RightSplitter")) rightSplitter->restoreState(s.value("AssetManager/RightSplitter").toByteArray());
        LogManager::instance().addLog("[TRACE] restore rightSplitter state done", "DEBUG");
        if (s.contains("AssetManager/ViewMode")) {
            bool grid = s.value("AssetManager/ViewMode").toBool();
            LogManager::instance().addLog(QString("[TRACE] restore view mode flag: %1").arg(grid), "DEBUG");
        if (versionTable) {
            auto hh = versionTable->horizontalHeader();
            for (int c = 0; c < versionTable->columnCount(); ++c) {
                QVariant v = s.value(QString("AssetManager/VersionTable/Col%1").arg(c));
                if (v.isValid()) hh->resizeSection(c, v.toInt());
            }
        }
            LogManager::instance().addLog("[TRACE] restored version table columns", "DEBUG");

            isGridMode = grid;
            viewStack->setCurrentIndex(grid ? 0 : 1);
            viewModeButton->setIcon(grid ? icoGrid() : icoList());
            thumbnailSizeSlider->setEnabled(grid);
            LogManager::instance().addLog("[TRACE] applied view mode toggle", "DEBUG");
        }
        LogManager::instance().addLog("[TRACE] restore asset manager view", "DEBUG");
        if (assetTableView && assetTableView->model()) {
            auto hh = assetTableView->horizontalHeader();
            for (int c = 0; c < assetTableView->model()->columnCount(); ++c) {
                QVariant v = s.value(QString("AssetManager/AssetTable/Col%1").arg(c));
                if (v.isValid()) hh->resizeSection(c, v.toInt());
            }
        }
        LogManager::instance().addLog("[TRACE] restore asset table columns", "DEBUG");
    }
    LogManager::instance().addLog("[TRACE] window state restored", "DEBUG");
    restoreProjectManagerState();

    // Load initial data
    folderModel->reload();
    tagsModel->reload();

    // Expand root folder AFTER model is loaded with actual data
    if (folderTreeView) {
        folderTreeView->expandToDepth(0);
    }

    // Restore last active tab
    int lastTab = ContextPreserver::instance().loadLastActiveTab();
    if (mainTabs && lastTab >= 0 && lastTab < mainTabs->count()) {
        mainTabs->setCurrentIndex(lastTab);
    }

    // Restore last active folder or select first folder
    int lastFolderId = ContextPreserver::instance().loadLastActiveFolder();
    bool folderRestored = false;

    if (lastFolderId > 0 && assetsModel) {
        // Use unified navigation helper so view mode, filters and scroll are restored
        navigateToFolder(lastFolderId, false);
        folderRestored = true;
        qDebug() << "[ContextPreserver] Restored last active folder:" << lastFolderId;
    }

    // Fallback to first folder if restoration failed
    if (!folderRestored && folderModel->rowCount(QModelIndex()) > 0 && assetsModel) {
        QModelIndex firstFolder = folderModel->index(0, 0, QModelIndex());
        if (firstFolder.isValid()) {
            int firstFolderId = firstFolder.data(VirtualFolderTreeModel::IdRole).toInt();
            if (firstFolderId > 0) {
                navigateToFolder(firstFolderId, false);
            }
        }
    }

    // Restore Asset Manager folder tree expansion and scroll position
    {
        QSettings treeSettings("AugmentCode", "KAssetManager");
        treeSettings.beginGroup("AssetManager/Tree");
        const QVariantList expanded = treeSettings.value("ExpandedFolders").toList();
        const int savedScroll = treeSettings.value("ScrollPosition", -1).toInt();
        treeSettings.endGroup();

        expandedFolderIds.clear();
        for (const QVariant &v : expanded) {
            int id = v.toInt();
            if (id > 0) {
                expandedFolderIds.insert(id);
            }
        }

        restoreFolderExpansionState();

        if (savedScroll >= 0 && folderTreeView && folderTreeView->verticalScrollBar()) {
            folderTreeView->verticalScrollBar()->setValue(savedScroll);
        }
    }

    // Set Everything model now that all UI is constructed
    if (fmEverythingTreeModel && fmTree) {
        LogManager::instance().addLog("[FileManager] Setting Everything model on tree", "INFO");
        fmTree->setModel(fmEverythingTreeModel);
        
        // Connect to async fetch completion for deferred tree scrolling
        connect(fmEverythingTreeModel, &EverythingFolderModel::childrenFetched,
                this, &MainWindow::onFmTreeChildrenFetched);
        
        LogManager::instance().addLog("[FileManager] Everything model activated", "INFO");
    }

    // React to tree single-click (selection change) to change right view root
    // IMPORTANT: This must be done AFTER setting the model, because setModel() creates a new selection model
    if (fmTree && fmTree->selectionModel()) {
        LogManager::instance().addLog("[FileManager] Connecting tree selection signal", "INFO");
        connect(fmTree->selectionModel(), &QItemSelectionModel::currentChanged,
                this, &MainWindow::onFmTreeCurrentChanged);
    } else {
        LogManager::instance().addLog("[FileManager] WARNING: fmTree or selectionModel is null!", "WARN");
    }

    LogManager::instance().addLog("[TRACE] mainwindow ctor finished", "DEBUG");

    // Apply theme after all UI is set up
    applyTheme();

    // Schedule database health check on startup (delayed to avoid blocking UI)
    QTimer::singleShot(2000, this, &MainWindow::performStartupHealthCheck);
}

void MainWindow::performStartupHealthCheck()
{
    DatabaseHealthAgent& agent = DatabaseHealthAgent::instance();
    DatabaseStats stats = agent.getDatabaseStats();

    // Check if VACUUM is recommended
    if (agent.shouldVacuum()) {
        QString recommendation = agent.getVacuumRecommendation();

        // Show notification in status bar
        statusBar()->showMessage(QString("Database maintenance recommended: %1").arg(recommendation), 10000);

        // Log the recommendation
        qInfo() << "[DatabaseHealth] Startup check:" << recommendation;
    }

    // Check for critical issues (orphaned records, missing files)
    if (stats.orphanedAssets > 0 || stats.missingFiles > 10) {
        QString message = QString("Database health issues detected: ");
        if (stats.orphanedAssets > 0) {
            message += QString("%1 orphaned asset(s) ").arg(stats.orphanedAssets);
        }
        if (stats.missingFiles > 10) {
            message += QString("%1 missing file(s) ").arg(stats.missingFiles);
        }
        message += "- Open Tools > Database Health to review.";

        statusBar()->showMessage(message, 15000);
        qWarning() << "[DatabaseHealth]" << message;
    }
}

void MainWindow::setupFileManagerUi()
{
    // Splitter: left (tree) | right (view)
    fmSplitter = new QSplitter(Qt::Horizontal, fileManagerPage);

    // Left: Favorites (top) | Folder tree (bottom) in a vertical splitter
    QWidget *left = new QWidget(fmSplitter);
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0,0,0,0);
    leftLayout->setSpacing(0);

    LogManager::instance().addLog("[FileManager] Initializing Everything SDK...", "INFO");
    const bool everythingTreeReady = EverythingSearch::instance().initialize();
    if (everythingTreeReady) {
        // Create Everything model - will be set on tree after window is shown
        fmEverythingTreeModel = new EverythingFolderModel(this);
        LogManager::instance().addLog("[FileManager] Everything model created - will be set after window shown", "INFO");
        // DON'T create QFileSystemModel - it's slow and we don't need it
    } else {
        LogManager::instance().addLog("[FileManager] Everything SDK not available - falling back to QFileSystemModel", "WARN");
        fmTreeModel = new QFileSystemModel(left);
        fmTreeModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);
        fmTreeModel->setIconProvider(new FmTreeIconProvider());
        fmTreeModel->setRootPath(""); // show drives at root
    }

    fmLeftSplitter = new QSplitter(Qt::Vertical, left);
    LogManager::instance().addLog("[TRACE] fmLeftSplitter created", "DEBUG");

    // Favorites container
    LogManager::instance().addLog("[TRACE] creating favorites container", "DEBUG");
    QWidget *favContainer = new QWidget(fmLeftSplitter);
    QVBoxLayout *favLayout = new QVBoxLayout(favContainer);
    favLayout->setContentsMargins(0,0,0,0);
    favLayout->setSpacing(0);
    LogManager::instance().addLog("[TRACE] favorites layout ready", "DEBUG");
    QLabel *favHeader = new QLabel("★ Favorites", favContainer);
    QFont favHeaderFont = favHeader->font();
    favHeaderFont.setBold(true);
    favHeader->setFont(favHeaderFont);
    favLayout->addWidget(favHeader);
    LogManager::instance().addLog("[TRACE] favorites header added", "DEBUG");

    fmFavoritesList = new QListWidget(favContainer);
    fmFavoritesList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fmFavoritesList, &QListWidget::itemDoubleClicked, this, &MainWindow::onFmFavoriteActivated);
    connect(fmFavoritesList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!fmFavoritesList) return;
        QPoint gp = fmFavoritesList->viewport()->mapToGlobal(pos);
        QMenu m; QAction *rem = m.addAction("Remove Favorite", this, &MainWindow::onFmRemoveFavorite);
        rem->setEnabled(fmFavoritesList->currentItem()!=nullptr);
        m.exec(gp);
    });
    favLayout->addWidget(fmFavoritesList);
    LogManager::instance().addLog("[TRACE] favorites list widget added", "DEBUG");
    LogManager::instance().addLog("[TRACE] invoking loadFmFavorites", "DEBUG");
    loadFmFavorites();
    LogManager::instance().addLog("[TRACE] fmFavorites populated", "DEBUG");

    // Folder tree
    fmTree = new QTreeView(fmLeftSplitter);

    // Set model: use Everything if available, otherwise QFileSystemModel
    if (fmEverythingTreeModel) {
        // Model will be set after window is shown
    } else if (fmTreeModel) {
        fmTree->setModel(fmTreeModel);
    }
    fmTree->setHeaderHidden(false);
    fmTree->header()->setStretchLastSection(true);
    fmTree->header()->setSectionResizeMode(QHeaderView::Interactive);
    // Persist fmTree column widths with debouncing to avoid disk I/O storm during resize
    connect(fmTree->header(), &QHeaderView::sectionResized, this, [this](int logical, int /*oldSize*/, int newSize){
        // Skip saving during window resize to avoid sluggishness
        if (m_windowResizing) return;
        QSettings s("AugmentCode", "KAssetManager");
        s.setValue(QString("FileManager/Tree/Col%1").arg(logical), newSize);
    });

    fmTree->setContextMenuPolicy(Qt::CustomContextMenu);
    fmTree->setExpandsOnDoubleClick(true);
    fmTree->setSelectionMode(QAbstractItemView::SingleSelection);
    // Prevent auto-scrolling right for long folder names - keep horizontal scroll at left
    connect(fmTree->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value != 0) {
            // Reset to left edge whenever tree tries to auto-scroll horizontally
            QTimer::singleShot(0, this, [this]() {
                if (fmTree && fmTree->horizontalScrollBar()) {
                    fmTree->horizontalScrollBar()->setValue(0);
                }
            });
        }
    });
    // set root to the "Computer" level
    connect(fmTree, &QTreeView::customContextMenuRequested, this, &MainWindow::onFmTreeContextMenu);
    // Enable drag and drop on folder tree
    fmTree->setDragEnabled(true);
    fmTree->setAcceptDrops(true);
    fmTree->setDropIndicatorShown(true);
    fmTree->setDragDropMode(QAbstractItemView::DragDrop);
    fmTree->viewport()->installEventFilter(this);
    LogManager::instance().addLog("[TRACE] drag/drop configured", "DEBUG");

    LogManager::instance().addLog("[TRACE] fmTree configured", "DEBUG");

    if (fmTreeModel) {
        fmTree->setRootIndex(fmTreeModel->index(fmTreeModel->rootPath()));
    }

    // Add to left layout
    leftLayout->addWidget(fmLeftSplitter);
    LogManager::instance().addLog("[TRACE] fmLeft layout ready", "DEBUG");

    // Right: toolbar + stacked views (grid/list)
    QWidget *right = new QWidget(fmSplitter);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0,0,0,0);
    rightLayout->setSpacing(0);

    // Toolbar
    fmToolbar = new QWidget(right);
    fmToolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fmToolbar->setFixedHeight(40);

    QHBoxLayout *tb = new QHBoxLayout(fmToolbar);
    tb->setContentsMargins(4,4,4,4);
    tb->setSpacing(4);

    fmIsGridMode = true;

    auto mkTb = [&](const QIcon &ic, const QString &tip) {
        QToolButton *b = new QToolButton(fmToolbar);
        b->setIcon(ic); b->setToolTip(tip); b->setAutoRaise(true); b->setIconSize(QSize(20,20));
        return b;
    };

    // Navigation buttons (Back and Up) at the left
    fmBackButton = mkTb(icoBack(ThemeManager::instance().iconColor()), "Back");
    connect(fmBackButton, &QToolButton::clicked, this, &MainWindow::onFmNavigateBack);
    tb->addWidget(fmBackButton);

    fmUpButton = mkTb(icoUp(ThemeManager::instance().iconColor()), "Up");
    connect(fmUpButton, &QToolButton::clicked, this, &MainWindow::onFmNavigateUp);
    tb->addWidget(fmUpButton);

    // Separator
    QFrame *sep1 = new QFrame(fmToolbar);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFrameShadow(QFrame::Sunken);
    tb->addWidget(sep1);

    // Left-aligned: New Folder, Copy, Cut, Paste, Delete, Rename, Add to Library, List/Grid Toggle, Grid Size bar, Group Sequences
    fmNewFolderBtn = mkTb(icoFolderNew(ThemeManager::instance().iconColor()), "New Folder");
    connect(fmNewFolderBtn, &QToolButton::clicked, this, &MainWindow::onFmNewFolder);
    tb->addWidget(fmNewFolderBtn);

    fmCopyBtn = mkTb(icoCopy(ThemeManager::instance().iconColor()), "Copy");       connect(fmCopyBtn, &QToolButton::clicked, this, &MainWindow::onFmCopy); tb->addWidget(fmCopyBtn);
    fmCutBtn = mkTb(icoCut(ThemeManager::instance().iconColor()), "Cut");          connect(fmCutBtn, &QToolButton::clicked, this, &MainWindow::onFmCut); tb->addWidget(fmCutBtn);
    fmPasteBtn = mkTb(icoPaste(ThemeManager::instance().iconColor()), "Paste");    connect(fmPasteBtn, &QToolButton::clicked, this, &MainWindow::onFmPaste); tb->addWidget(fmPasteBtn);
    fmDeleteBtn = mkTb(icoDelete(ThemeManager::instance().iconColor()), "Delete"); connect(fmDeleteBtn, &QToolButton::clicked, this, &MainWindow::onFmDelete); tb->addWidget(fmDeleteBtn);
    fmRenameBtn = mkTb(icoRename(ThemeManager::instance().iconColor()), "Rename"); connect(fmRenameBtn, &QToolButton::clicked, this, &MainWindow::onFmRename); tb->addWidget(fmRenameBtn);

    fmAddToLibraryBtn = mkTb(icoAdd(ThemeManager::instance().iconColor()), "Add to Library");
    connect(fmAddToLibraryBtn, &QToolButton::clicked, this, &MainWindow::onAddSelectionToAssetLibrary);
    tb->addWidget(fmAddToLibraryBtn);

    fmViewModeButton = new QToolButton(fmToolbar);
    fmViewModeButton->setIcon(icoGrid(ThemeManager::instance().iconColor()));
    fmViewModeButton->setToolTip("Toggle Grid/List");
    fmViewModeButton->setAutoRaise(true);
    fmViewModeButton->setIconSize(QSize(20,20));
    connect(fmViewModeButton, &QToolButton::clicked, this, &MainWindow::onFmViewModeToggled);
    tb->addWidget(fmViewModeButton);

    // Thumbnail size slider (File Manager)
    fmSizeLabel = new QLabel("Size:", fmToolbar); tb->addWidget(fmSizeLabel);
    fmThumbnailSizeSlider = new QSlider(Qt::Horizontal, fmToolbar);
    fmThumbnailSizeSlider->setRange(64, 320);
    fmThumbnailSizeSlider->setFixedWidth(80);
    fmThumbnailSizeSlider->setToolTip("Adjust thumbnail size");
    tb->addWidget(fmThumbnailSizeSlider);
    connect(fmThumbnailSizeSlider, &QSlider::valueChanged, this, &MainWindow::onFmThumbnailSizeChanged);

    // Right-aligned controls
    tb->addStretch();

    fmGroupSequencesCheckBox = new QToolButton(fmToolbar);
    fmGroupSequencesCheckBox->setIcon(icoGroup(ThemeManager::instance().iconColor()));
    fmGroupSequencesCheckBox->setToolTip("Group image sequences into single entries");
    fmGroupSequencesCheckBox->setCheckable(true);
    fmGroupSequencesCheckBox->setAutoRaise(true);
    fmGroupSequencesCheckBox->setIconSize(QSize(20,20));
    connect(fmGroupSequencesCheckBox, &QToolButton::toggled, this, &MainWindow::onFmGroupSequencesToggled);
    tb->addWidget(fmGroupSequencesCheckBox);

    fmHideFoldersCheckBox = new QToolButton(fmToolbar);
    fmHideFoldersCheckBox->setIcon(icoHide(ThemeManager::instance().iconColor()));
    fmHideFoldersCheckBox->setToolTip("Hide folders in Grid view (show files only)");
    fmHideFoldersCheckBox->setCheckable(true);
    fmHideFoldersCheckBox->setAutoRaise(true);
    fmHideFoldersCheckBox->setIconSize(QSize(20,20));
    connect(fmHideFoldersCheckBox, &QToolButton::toggled, this, &MainWindow::onFmHideFoldersToggled);
    tb->addWidget(fmHideFoldersCheckBox);

    // Everything Search button
    fmSearchButton = new QToolButton(fmToolbar);
    fmSearchButton->setIcon(icoSearch(ThemeManager::instance().iconColor()));
    fmSearchButton->setToolTip("Everything Search - Search entire disk");
    fmSearchButton->setAutoRaise(true);
    fmSearchButton->setIconSize(QSize(20,20));
    connect(fmSearchButton, &QToolButton::clicked, this, &MainWindow::onEverythingSearchFileManager);
    tb->addWidget(fmSearchButton);

    fmPreviewToggleButton = new QToolButton(fmToolbar);
    fmPreviewToggleButton->setIcon(icoEye(ThemeManager::instance().iconColor()));
    fmPreviewToggleButton->setToolTip("Show/Hide preview panel");
    fmPreviewToggleButton->setCheckable(true);
    fmPreviewToggleButton->setChecked(true);
    fmPreviewToggleButton->setAutoRaise(true);
    fmPreviewToggleButton->setIconSize(QSize(20,20));
    connect(fmPreviewToggleButton, &QToolButton::toggled, this, &MainWindow::onFmTogglePreview);
    tb->addWidget(fmPreviewToggleButton);

    // Dual-pane toggle button
    fmDualPaneToggle = mkTb(icoDualPane(ThemeManager::instance().iconColor()), "Show/Hide second pane (dual-pane mode)");
    fmDualPaneToggle->setCheckable(true);
    fmDualPaneToggle->setChecked(false);
    connect(fmDualPaneToggle, &QToolButton::toggled, this, &MainWindow::onFmToggleSecondPane);
    tb->addWidget(fmDualPaneToggle);

    // Synced navigation toggle (hidden until dual-pane is enabled)
    fmSyncNavButton = mkTb(QIcon(), "Sync navigation between panes");
    fmSyncNavButton->setText("⇆");  // Unicode arrows
    fmSyncNavButton->setCheckable(true);
    fmSyncNavButton->setChecked(false);
    fmSyncNavButton->setStyleSheet("QToolButton { font-size: 16px; }");
    fmSyncNavButton->hide();  // Hidden until dual-pane enabled
    connect(fmSyncNavButton, &QToolButton::toggled, this, &MainWindow::onFmSyncNavToggled);
    tb->addWidget(fmSyncNavButton);

    rightLayout->addWidget(fmToolbar);

    // Editable path bar (like Windows Explorer)
    fmPathBar = new QLineEdit(right);
    fmPathBar->setPlaceholderText("Enter path...");
    fmPathBar->setClearButtonEnabled(true);
    connect(fmPathBar, &QLineEdit::returnPressed, this, [this]() {
        QString path = fmPathBar->text().trimmed();
        if (path.isEmpty()) return;
        
        // Expand environment variables like %USERPROFILE%
        path = QDir::fromNativeSeparators(path);
        
        // Check if path exists
        QFileInfo fi(path);
        if (!fi.exists()) {
            QMessageBox::warning(this, "Invalid Path", 
                QString("The path '%1' does not exist.").arg(path));
            // Restore the current path
            if (fmDirModel) {
                fmPathBar->setText(QDir::toNativeSeparators(fmDirModel->rootPath()));
            }
            return;
        }
        
        // If it's a file, navigate to its parent directory
        if (fi.isFile()) {
            path = fi.absolutePath();
        }
        
        fmNavigateToPath(path, true);
    });
    rightLayout->addWidget(fmPathBar);

    // Models/views
    fmViewStack = new QStackedWidget(right);

    fmDirModel = new QFileSystemModel(fmViewStack);
    fmDirModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    fmDirModel->setRootPath("");
    fmDirModel->setIconProvider(new FmIconProvider());
    LogManager::instance().addLog("[TRACE] fmDirModel created", "DEBUG");

    // Grid view
    // Sequence grouping proxy
    fmProxyModel = new SequenceGroupingProxyModel(fmViewStack);
    fmProxyModel->setSourceModel(fmDirModel);
    // Enable sorting to ensure folders appear first
    fmProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    fmProxyModel->setSortRole(Qt::DisplayRole);
    fmProxyModel->setDynamicSortFilter(true);
    fmProxyModel->sort(0, Qt::AscendingOrder);
    LogManager::instance().addLog("[TRACE] fmProxyModel ready", "DEBUG");

    // Grid view
    fmGridView = new FmGridViewEx(fmProxyModel, fmDirModel, fmViewStack);
    fmGridView->setModel(fmProxyModel);
    fmGridView->setViewMode(QListView::IconMode);
    fmGridView->setResizeMode(QListView::Adjust);
    fmGridView->setSpacing(1); // minimize empty space between thumbnails
    // Use uniform item sizes for much better resize performance (avoids per-item sizeHint calls)
    fmGridView->setUniformItemSizes(true);
    // Batched layout mode for better resize performance
    fmGridView->setLayoutMode(QListView::Batched);
    fmGridView->setBatchSize(100);
    fmGridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fmGridView->setContextMenuPolicy(Qt::CustomContextMenu);

    // Minimalist delegate to remove cell color separation
    {
        auto *d = new FmItemDelegate(fmGridView);
        d->setView(fmGridView);  // Set view for scrub frame support
        fmGridView->setItemDelegate(d);
        // Restore thumbnail size from settings (default 120)
        QSettings s("AugmentCode", "KAssetManager");
        int fmThumb = s.value("FileManager/GridThumbSize", 120).toInt();
        d->setThumbnailSize(fmThumb);
        fmGridView->setIconSize(QSize(fmThumb, fmThumb));
        fmGridView->setGridSize(QSize(fmThumb + 8, fmThumb + 36));
        if (fmThumbnailSizeSlider) fmThumbnailSizeSlider->setValue(fmThumb);
    }
    fmGridView->setDragEnabled(true);
    fmGridView->setAcceptDrops(true);
    fmGridView->setDropIndicatorShown(true);
    fmGridView->setDragDropMode(QAbstractItemView::DragDrop);
    fmGridView->setDefaultDropAction(Qt::CopyAction);
    // Batched layout mode for better resize performance
    fmGridView->setLayoutMode(QListView::Batched);
    fmGridView->setBatchSize(100);
    if (fmGridView->viewport()) fmGridView->viewport()->installEventFilter(this);
    if (fmGridView->verticalScrollBar())
        connect(fmGridView->verticalScrollBar(), &QScrollBar::valueChanged,
                this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    if (fmGridView->horizontalScrollBar())
        connect(fmGridView->horizontalScrollBar(), &QScrollBar::valueChanged,
                this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(fmGridView, &QListView::doubleClicked, this, &MainWindow::onFmItemDoubleClicked);
    LogManager::instance().addLog("[TRACE] fmGridView configured", "DEBUG");
    fmViewStack->addWidget(fmGridView); // 0

    fmScrubController = new GridScrubController(
        fmGridView,
        [this](const QModelIndex& idx) -> QString {
            if (!fmDirModel) {
                return QString();
            }
            QModelIndex srcIdx = idx;
            if (fmProxyModel && idx.model() == fmProxyModel) {
                srcIdx = fmProxyModel->mapToSource(idx);
            }
            if (!srcIdx.isValid()) {
                return QString();
            }
            if (fmDirModel->isDir(srcIdx)) {
                return QString();
            }
            return fmDirModel->filePath(srcIdx);
        },
        this);
    LogManager::instance().addLog("[TRACE] fmScrubController ready", "DEBUG");

    // List view
    fmListView = new FmListViewEx(fmProxyModel, fmDirModel, fmViewStack);
    fmListView->setModel(fmProxyModel);
    LogManager::instance().addLog("[TRACE] fmListView created", "DEBUG");
    // Persist fmListView column widths immediately when resized
    connect(fmListView->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logical, int /*oldSize*/, int newSize){
        QSettings s("AugmentCode", "KAssetManager");
        s.setValue(QString("FileManager/ListView/Col%1").arg(logical), newSize);
    });

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
    // Set default sort: column 0 (Name), ascending order, folders first
    fmListView->sortByColumn(0, Qt::AscendingOrder);
    fmListView->setDragEnabled(true);
    fmListView->setAcceptDrops(true);
    fmListView->setDropIndicatorShown(true);
    fmListView->setDragDropMode(QAbstractItemView::DragDrop);
    fmListView->setDefaultDropAction(Qt::CopyAction);
    if (fmListView->viewport()) fmListView->viewport()->installEventFilter(this);
    connect(fmListView, &QTableView::doubleClicked, this, &MainWindow::onFmItemDoubleClicked);
    fmViewStack->addWidget(fmListView); // 1
    LogManager::instance().addLog("[TRACE] fmListView wired", "DEBUG");

    fmViewStack->setCurrentIndex(0);
    LogManager::instance().addLog("[TRACE] fmViewStack initialised", "DEBUG");

    // Right-side splitter: views | preview panel
    fmRightSplitter = new QSplitter(Qt::Horizontal, right);
    LogManager::instance().addLog("[TRACE] fmRightSplitter created", "DEBUG");
    QWidget *viewContainer = new QWidget(fmRightSplitter);
    QVBoxLayout *viewContainerLayout = new QVBoxLayout(viewContainer);
    viewContainerLayout->setContentsMargins(0,0,0,0);
    viewContainerLayout->setSpacing(0);
    viewContainerLayout->addWidget(fmViewStack);
    LogManager::instance().addLog("[TRACE] fm view container ready", "DEBUG");

    // Preview panel (embedded)
    fmPreviewPanel = new QWidget(fmRightSplitter);
    fmPreviewPanel->setMinimumWidth(260);
    QVBoxLayout *pv = new QVBoxLayout(fmPreviewPanel);
    pv->setContentsMargins(8,8,8,8);
    pv->setSpacing(6);
    QLabel *pvTitle = new QLabel("Preview", fmPreviewPanel);
    QFont pvTitleFont = pvTitle->font();
    pvTitleFont.setBold(true);
    pvTitle->setFont(pvTitleFont);
    pv->addWidget(pvTitle);
    LogManager::instance().addLog("[TRACE] fm preview header ready", "DEBUG");

    // Image view with zoom/pan
    fmImageScene = new QGraphicsScene(fmPreviewPanel);
    fmImageItem = new QGraphicsPixmapItem();
    fmImageScene->addItem(fmImageItem);
    fmImageView = new QGraphicsView(fmImageScene, fmPreviewPanel);
    fmImageView->setDragMode(QGraphicsView::ScrollHandDrag);
    fmImageView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    fmImageView->setMinimumHeight(160);
    fmImageView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    fmImageView->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    fmImageView->setAlignment(Qt::AlignCenter);
    fmImageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Additional preview widgets (hidden by default)
    fmTextView = new QPlainTextEdit(fmPreviewPanel);
    fmTextView->setReadOnly(true);
    fmTextView->setWordWrapMode(QTextOption::NoWrap);
    fmTextView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    // Ensure white background and black text for text/DOCX previews
    fmTextView->setStyleSheet("QPlainTextEdit { background-color: #ffffff; color: #000000; border: none; }");
    fmTextView->hide();

    fmCsvModel = new QStandardItemModel(fmPreviewPanel);
    fmCsvView = new QTableView(fmPreviewPanel);
    fmCsvView->setModel(fmCsvModel);
    fmCsvView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fmCsvView->setSelectionMode(QAbstractItemView::NoSelection);
    fmCsvView->setAlternatingRowColors(true);
    // Ensure white background and black text for CSV/XLSX previews
    fmCsvView->setStyleSheet(
        "QTableView { background-color: #ffffff; color: #000000; gridline-color: #cccccc; border: none; }"
        "QHeaderView::section { background-color: #f0f0f0; color: #000000; border: none; padding: 4px; }"
    );
    fmCsvView->hide();

#ifdef HAVE_QT_PDF
    fmPdfDoc = new QPdfDocument(fmPreviewPanel);
#endif
#ifdef HAVE_QT_PDF_WIDGETS
    fmPdfView = new QPdfView(fmPreviewPanel);
    fmPdfView->setPageMode(QPdfView::PageMode::SinglePage);
    fmPdfView->setDocument(fmPdfDoc);
    fmPdfView->hide();
#endif

    fmSvgScene = new QGraphicsScene(fmPreviewPanel);
    fmSvgItem = nullptr;
    fmSvgView = new QGraphicsView(fmSvgScene, fmPreviewPanel);
    fmSvgView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    fmSvgView->setAlignment(Qt::AlignCenter);
    fmSvgView->hide();

    fmImageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    fmImageView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    fmImageView->viewport()->installEventFilter(this);
    fmImageView->installEventFilter(this);



    // Alpha toggle row (for images with alpha)
    QHBoxLayout* alphaRow = new QHBoxLayout();
    fmAlphaCheck = new QCheckBox("Alpha", fmPreviewPanel);
    fmAlphaCheck->setToolTip("Show alpha channel (grayscale)");
    fmAlphaCheck->hide();
    connect(fmAlphaCheck, &QCheckBox::toggled, this, [this](bool on){
        fmAlphaOnlyMode = on;
        if (!fmOriginalImage.isNull() && fmImageItem) {
            QImage disp = fmOriginalImage;
            if (fmAlphaOnlyMode && disp.hasAlphaChannel()) {
                QImage a(disp.size(), QImage::Format_Grayscale8);
                for (int y=0;y<disp.height();++y){
                    const uchar* al = disp.constScanLine(y);
                    // convert alpha channel quickly by reading from pixel's alpha
                    for (int x=0;x<disp.width();++x){
                        uchar alpha = qAlpha(reinterpret_cast<const QRgb*>(disp.constScanLine(y))[x]);
                        a.scanLine(y)[x] = alpha;
                    }
                }
                disp = a.convertToFormat(QImage::Format_Grayscale8);
            }
            fmImageItem->setPixmap(QPixmap::fromImage(disp));
            if (fmImageFitToView) {
                fmImageScene->setSceneRect(fmImageItem->boundingRect());
                fmImageView->centerOn(fmImageItem);
                fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
            }
        }
    });
    alphaRow->addWidget(fmAlphaCheck);
    alphaRow->addStretch();

    // PDF page controls (hidden by default)
    QHBoxLayout* docc = new QHBoxLayout();
    fmPdfPrevBtn = new QToolButton(fmPreviewPanel);
    fmPdfPrevBtn->setText("◀");
    fmPdfNextBtn = new QToolButton(fmPreviewPanel);
    fmPdfNextBtn->setText("▶");
    fmPdfPageLabel = new QLabel("--/--", fmPreviewPanel);
    docc->addWidget(fmPdfPrevBtn);
    docc->addWidget(fmPdfPageLabel);
    docc->addWidget(fmPdfNextBtn);
    docc->addStretch();
    fmPdfPrevBtn->hide(); fmPdfNextBtn->hide(); fmPdfPageLabel->hide();
#if defined(HAVE_QT_PDF)
    connect(fmPdfPrevBtn, &QToolButton::clicked, this, [this]{
        bool handled = false;
        #ifdef HAVE_QT_PDF
        if (fmPdfDoc && fmPdfDoc->pageCount() > 0) {
            handled = true;
            if (fmPdfCurrentPage > 0) fmPdfCurrentPage--;
            const QSizeF pts = fmPdfDoc->pagePointSize(fmPdfCurrentPage);
            int vw = fmImageView ? fmImageView->viewport()->width() : 800;
            if (vw < 1) vw = 800;
            int w = vw;
            int h = pts.width() > 0 ? int(pts.height() * (w / pts.width())) : w;
            QImage img = fmPdfDoc->render(fmPdfCurrentPage, QSize(w, h));
            if (!img.isNull() && fmImageItem) {
                // Composite onto white to avoid dark theme bleeding through
                if (img.hasAlphaChannel()) {
                    QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied);
                    bg.fill(Qt::white);
                    QPainter p(&bg);
                    p.drawImage(0, 0, img);
                    p.end();
                    img = bg;
                }
                fmImageItem->setPixmap(QPixmap::fromImage(img));
                if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                if (fmImageView) {
                    fmImageView->resetTransform();
                    fmImageView->centerOn(fmImageItem);
                    fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                    fmImageFitToView = true;
                    fmImageView->setBackgroundBrush(Qt::white);
                    fmImageView->show();
                }
            }
            if (fmPdfPageLabel) fmPdfPageLabel->setText(QString("%1/%2").arg(fmPdfCurrentPage+1).arg(fmPdfDoc->pageCount()));
        }
        #endif
    });
    connect(fmPdfNextBtn, &QToolButton::clicked, this, [this]{
        bool handled = false;
        #ifdef HAVE_QT_PDF
        if (fmPdfDoc && fmPdfDoc->pageCount() > 0) {
            handled = true;
            if (fmPdfCurrentPage + 1 < fmPdfDoc->pageCount()) fmPdfCurrentPage++;
            const QSizeF pts = fmPdfDoc->pagePointSize(fmPdfCurrentPage);
            int vw = fmImageView ? fmImageView->viewport()->width() : 800;
            if (vw < 1) vw = 800;
            int w = vw;
            int h = pts.width() > 0 ? int(pts.height() * (w / pts.width())) : w;
            QImage img = fmPdfDoc->render(fmPdfCurrentPage, QSize(w, h));
            if (!img.isNull() && fmImageItem) {
                if (img.hasAlphaChannel()) {
                    QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied);
                    bg.fill(Qt::white);
                    QPainter p(&bg);
                    p.drawImage(0, 0, img);
                    p.end();
                    img = bg;
                }
                fmImageItem->setPixmap(QPixmap::fromImage(img));
                if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                if (fmImageView) {
                    fmImageView->resetTransform();
                    fmImageView->centerOn(fmImageItem);
                    fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                    fmImageFitToView = true;
                    fmImageView->setBackgroundBrush(Qt::white);
                    fmImageView->show();
                }
            }
            if (fmPdfPageLabel) fmPdfPageLabel->setText(QString("%1/%2").arg(fmPdfCurrentPage+1).arg(fmPdfDoc->pageCount()));
        }
        #endif
    });
#endif

    pv->addLayout(alphaRow);

    // Video widget for GStreamer rendering
    fmVideoWidget = new QWidget(fmPreviewPanel);
    fmVideoWidget->setMinimumHeight(160);
    fmVideoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // CRITICAL: Set widget attributes for native window embedding
    // These attributes tell Qt to create a native window that GStreamer can render into
    fmVideoWidget->setAttribute(Qt::WA_NativeWindow);
    fmVideoWidget->setAttribute(Qt::WA_PaintOnScreen);
    fmVideoWidget->setAttribute(Qt::WA_OpaquePaintEvent);

    fmVideoWidget->hide();

    // Create GStreamer player for video playback
    fmGStreamerPlayer = new GStreamerPlayer(fmPreviewPanel);

    // Media controls (Explorer-like): Prev - Play/Pause - Next - Slider - Time - Audio
    QHBoxLayout *mc = new QHBoxLayout();
    mc->setAlignment(Qt::AlignVCenter);

    fmPrevFrameBtn = new QPushButton(fmPreviewPanel);
    fmPrevFrameBtn->setIcon(icoMediaPrevFrame(ThemeManager::instance().iconColor()));
    fmPrevFrameBtn->setIconSize(QSize(16,16));
    fmPrevFrameBtn->setToolTip("Previous Frame");

    fmPlayPauseBtn = new QPushButton(fmPreviewPanel);
    fmPlayPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor()));
    fmPlayPauseBtn->setIconSize(QSize(18,18));

    fmNextFrameBtn = new QPushButton(fmPreviewPanel);
    fmNextFrameBtn->setIcon(icoMediaNextFrame(ThemeManager::instance().iconColor()));
    fmNextFrameBtn->setIconSize(QSize(16,16));
    fmNextFrameBtn->setToolTip("Next Frame");

    fmPositionSlider = new QSlider(Qt::Horizontal, fmPreviewPanel);
    fmPositionSlider->setMinimum(0); fmPositionSlider->setMaximum(1000);

    fmTimeLabel = new QLabel("00:00 / 00:00", fmPreviewPanel);

    fmMuteBtn = new QPushButton(fmPreviewPanel);
    fmMuteBtn->setIcon(icoMediaAudio(ThemeManager::instance().iconColor()));
    fmMuteBtn->setFlat(true);
    fmMuteBtn->setIconSize(QSize(16,16));
    fmMuteBtn->setFocusPolicy(Qt::NoFocus);
    fmMuteBtn->setToolTip("Mute/Unmute");

    fmVolumeSlider = new QSlider(Qt::Horizontal, fmPreviewPanel);
    fmVolumeSlider->setRange(0, 100); fmVolumeSlider->setValue(50);

    // Order: Prev - Play/Pause - Next - Slider - Time - Audio controls
    mc->addWidget(fmPrevFrameBtn);
    mc->addWidget(fmPlayPauseBtn);
    mc->addWidget(fmNextFrameBtn);
    mc->addWidget(fmPositionSlider, 1);
    mc->addWidget(fmTimeLabel);
    mc->addSpacing(8);
    mc->addWidget(fmMuteBtn);
    mc->addWidget(fmVolumeSlider);

    // Sequence timer
    fmSequenceTimer = new QTimer(fmPreviewPanel);
    connect(fmSequenceTimer, &QTimer::timeout, this, [this]{
        if (!fmIsSequence || fmSequenceFramePaths.isEmpty()) return;
        int next = fmSequenceCurrentIndex + 1;
        if (next >= fmSequenceFramePaths.size()) next = 0; // loop
        loadFmSequenceFrame(next);
    });

    // Controls behavior
    connect(fmPrevFrameBtn, &QPushButton::clicked, this, [this]{ if (fmIsSequence) stepFmSequence(-1); });
    connect(fmNextFrameBtn, &QPushButton::clicked, this, [this]{ if (fmIsSequence) stepFmSequence(1); });

    connect(fmPlayPauseBtn, &QPushButton::clicked, this, [this]{
        if (fmIsSequence) {
            if (fmSequencePlaying) pauseFmSequence(); else playFmSequence();
            return;
        }
        if (!fmGStreamerPlayer) return;
        if (fmGStreamerPlayer->state() == GStreamerPlayer::PlaybackState::Playing) {
            fmGStreamerPlayer->pause();
            fmPlayPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor()));
        } else {
            fmGStreamerPlayer->play();
            fmPlayPauseBtn->setIcon(icoMediaPause(ThemeManager::instance().iconColor()));
        }
    });

    connect(fmGStreamerPlayer, &GStreamerPlayer::positionChanged, this, [this](qint64 pos){
        if (fmIsSequence) return; // sequence updates handled separately
        qint64 duration = fmGStreamerPlayer->duration();
        if (fmGStreamerPlayer && duration > 0){
            fmPositionSlider->blockSignals(true);
            fmPositionSlider->setValue(int(pos*1000/duration));
            fmPositionSlider->blockSignals(false);
            fmTimeLabel->setText(QString("%1 / %2").arg(QTime::fromMSecsSinceStartOfDay(int(pos)).toString("mm:ss")).arg(QTime::fromMSecsSinceStartOfDay(int(duration)).toString("mm:ss")));
        }
    });

    connect(fmPositionSlider, &QSlider::sliderMoved, this, [this](int v){
        if (fmIsSequence) {
            loadFmSequenceFrame(v);
            return;
        }
        qint64 duration = fmGStreamerPlayer->duration();
        if (fmGStreamerPlayer && duration > 0) fmGStreamerPlayer->seek(qint64(v) * duration / 1000);
    });

    connect(fmVolumeSlider, &QSlider::valueChanged, this, [this](int v){ if (fmGStreamerPlayer) fmGStreamerPlayer->setVolume(v/100.0); });
    connect(fmMuteBtn, &QPushButton::clicked, this, [this]{
        if (!fmGStreamerPlayer) return;
        bool newMuted = !fmGStreamerPlayer->isMuted();
        fmGStreamerPlayer->setMuted(newMuted);
        fmMuteBtn->setIcon(newMuted ? icoMediaMute() : icoMediaAudio());
    });


    // Center the preview content between title and controls
    QWidget *previewContent = new QWidget(fmPreviewPanel);
    QVBoxLayout *pc = new QVBoxLayout(previewContent);
    pc->setContentsMargins(0,0,0,0);
    pc->setSpacing(6);
    pc->addWidget(fmImageView, 1);
    pc->addWidget(fmVideoWidget, 1);
    pv->addWidget(previewContent);
    pv->addLayout(mc);
    // Hide media controls by default (only show for video/audio)
    if (fmPrevFrameBtn) fmPrevFrameBtn->hide();
    fmPlayPauseBtn->hide();
    if (fmNextFrameBtn) fmNextFrameBtn->hide();
    fmPositionSlider->hide();
    fmTimeLabel->hide();
    pc->addWidget(fmTextView, 1);
    pv->addLayout(docc);

    pc->addWidget(fmCsvView, 1);
#ifdef HAVE_QT_PDF_WIDGETS
    pc->addWidget(fmPdfView, 1);
#endif
    pc->addWidget(fmSvgView, 1);

    fmVolumeSlider->hide();
    if (fmMuteBtn) fmMuteBtn->hide();


    // Info panel (similar to Asset Manager's info panel)
    fmInfoPanel = new QWidget(fmRightSplitter);
    fmInfoPanel->setMinimumWidth(260);
    QVBoxLayout *infoPanelLayout = new QVBoxLayout(fmInfoPanel);
    infoPanelLayout->setContentsMargins(0, 0, 0, 0);
    infoPanelLayout->setSpacing(0);

    QLabel *infoTitle = new QLabel("File Info", fmInfoPanel);
    QFont fmInfoTitleFont = infoTitle->font();
    fmInfoTitleFont.setPointSize(14);
    fmInfoTitleFont.setBold(true);
    infoTitle->setFont(fmInfoTitleFont);
    infoPanelLayout->addWidget(infoTitle);

    // Scrollable area for metadata
    QScrollArea *infoScrollArea = new QScrollArea(fmInfoPanel);
    infoScrollArea->setWidgetResizable(true);
    infoScrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *infoScrollWidget = new QWidget();
    QVBoxLayout *infoLayout = new QVBoxLayout(infoScrollWidget);
    infoLayout->setContentsMargins(8, 8, 8, 8);
    infoLayout->setSpacing(4);

    fmInfoFileName = new QLabel("No selection", fmInfoPanel);
    QFont fmInfoFileNameFont = fmInfoFileName->font();
    fmInfoFileNameFont.setBold(true);
    fmInfoFileName->setFont(fmInfoFileNameFont);
    fmInfoFileName->setWordWrap(true);
    infoLayout->addWidget(fmInfoFileName);

    fmInfoFilePath = new QLabel("", fmInfoPanel);
    fmInfoFilePath->setWordWrap(true);
    infoLayout->addWidget(fmInfoFilePath);

    // Add separator
    QFrame *separator1 = new QFrame(fmInfoPanel);
    separator1->setFrameShape(QFrame::HLine);
    separator1->setFixedHeight(1);
    infoLayout->addWidget(separator1);

    fmInfoFileSize = new QLabel("", fmInfoPanel);
    fmInfoFileSize->setWordWrap(true);
    infoLayout->addWidget(fmInfoFileSize);

    fmInfoFileType = new QLabel("", fmInfoPanel);
    fmInfoFileType->setWordWrap(true);
    infoLayout->addWidget(fmInfoFileType);

    fmInfoDimensions = new QLabel("", fmInfoPanel);
    fmInfoDimensions->setWordWrap(true);
    infoLayout->addWidget(fmInfoDimensions);

    fmInfoCreated = new QLabel("", fmInfoPanel);
    fmInfoCreated->setWordWrap(true);
    infoLayout->addWidget(fmInfoCreated);

    fmInfoModified = new QLabel("", fmInfoPanel);
    fmInfoModified->setWordWrap(true);
    infoLayout->addWidget(fmInfoModified);

    fmInfoPermissions = new QLabel("", fmInfoPanel);
    fmInfoPermissions->setWordWrap(true);
    infoLayout->addWidget(fmInfoPermissions);

    infoLayout->addStretch();
    infoScrollWidget->setLayout(infoLayout);
    infoScrollArea->setWidget(infoScrollWidget);
    infoPanelLayout->addWidget(infoScrollArea);

    // Create vertical splitter for Preview | Info
    fmPreviewInfoSplitter = new QSplitter(Qt::Vertical, fmRightSplitter);
    fmPreviewInfoSplitter->addWidget(fmPreviewPanel);
    fmPreviewInfoSplitter->addWidget(fmInfoPanel);
    fmPreviewInfoSplitter->setStretchFactor(0, 2);
    fmPreviewInfoSplitter->setStretchFactor(1, 1);

    // Assemble right side
    fmRightSplitter->addWidget(viewContainer);
    fmRightSplitter->addWidget(fmPreviewInfoSplitter);
    fmRightSplitter->setStretchFactor(0, 3);
    fmRightSplitter->setStretchFactor(1, 1);
    rightLayout->addWidget(fmRightSplitter);
    rightLayout->setStretch(0, 0); // toolbar
    rightLayout->setStretch(1, 1); // main content


    // Create File Manager shortcuts (default key sequences), store them, then apply custom mappings
    {
        auto sc = new QShortcut(QKeySequence(Qt::Key_Space), fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmOpenOverlay);
        fmShortcutObjs.insert("OpenOverlay", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence::Copy, fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmCopy);
        fmShortcutObjs.insert("Copy", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence::Cut, fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmCut);
        fmShortcutObjs.insert("Cut", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence::Paste, fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmPaste);
        fmShortcutObjs.insert("Paste", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence::Delete, fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmDelete);
        fmShortcutObjs.insert("Delete", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::Key_F2), fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmRename);
        fmShortcutObjs.insert("Rename", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete), fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmDeletePermanent);
        fmShortcutObjs.insert("DeletePermanent", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence::New, fileManagerPage); // Ctrl+N
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmNewFolder);
        fmShortcutObjs.insert("NewFolder", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmCreateFolderWithSelected);
        fmShortcutObjs.insert("CreateFolderWithSelected", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::Key_Backspace), fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmBackToParent);
        fmShortcutObjs.insert("BackToParent", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::Key_F5), fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmRefresh);
        fmShortcutObjs.insert("Refresh", sc);
    }


    // Apply custom shortcuts from settings (overrides defaults)
    applyFmShortcuts();

    // Connect selection changes to preview
    connect(fmGridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onFmSelectionChanged);
    connect(fmListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onFmSelectionChanged);
    
    // Connect click signals for primary pane activation in dual-pane mode
    connect(fmGridView, &QListView::clicked, this, [this](const QModelIndex &) {
        if (fmSecondaryPane && fmSecondaryPane->isVisible()) {
            setActiveFmPane(true);
        }
    });
    connect(fmListView, &QTreeView::clicked, this, [this](const QModelIndex &) {
        if (fmSecondaryPane && fmSecondaryPane->isVisible()) {
            setActiveFmPane(true);
        }
    });

    // Wire splitter widgets
    fmSplitter->addWidget(left);
    fmSplitter->addWidget(right);

    // Context menus
    fmGridView->setContextMenuPolicy(Qt::CustomContextMenu);
    fmListView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fmGridView, &QWidget::customContextMenuRequested, this, &MainWindow::onFmShowContextMenu);
    connect(fmListView, &QWidget::customContextMenuRequested, this, &MainWindow::onFmShowContextMenu);


    fmSplitter->setStretchFactor(0, 0);  // Tree pane: fixed size (doesn't grow with window)
    fmSplitter->setStretchFactor(1, 1);  // Right pane: expands with window

    // Root: select first drive if exists
    QFileInfoList drives = QDir::drives();
    if (!drives.isEmpty()) {
        QString path = QDir::toNativeSeparators(drives.first().absoluteFilePath());
        QModelIndex idx = fmIndexForPath(path);
        if (idx.isValid() && fmTree) {
            fmTree->setCurrentIndex(idx);
                    fmDirModel->setRootPath(path);
            QModelIndex srcRoot = fmDirModel->index(path);
            if (fmProxyModel) {
                fmProxyModel->rebuildForRoot(path);
                QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
                fmGridView->setRootIndex(proxyRoot);
                fmListView->setRootIndex(proxyRoot);
            } else {
                fmGridView->setRootIndex(srcRoot);
                fmListView->setRootIndex(srcRoot);
            }
        }
    }

    // Initialize navigation button states
    fmUpdateNavigationButtons();

    // Install page layout
    QVBoxLayout *pageLayout = new QVBoxLayout(fileManagerPage);
    pageLayout->setContentsMargins(0,0,0,0);
    pageLayout->addWidget(fmSplitter);

    // Persist splitter positions with debouncing (avoid disk I/O during drag)
    if (fmSplitter) {
        connect(fmSplitter, &QSplitter::splitterMoved, this, [this](int, int){
            m_splitterSaveTimer.start();
        });
    }
    if (fmLeftSplitter) {
        connect(fmLeftSplitter, &QSplitter::splitterMoved, this, [this](int, int){
            m_splitterSaveTimer.start();
        });
    }
    if (fmRightSplitter) {
        connect(fmRightSplitter, &QSplitter::splitterMoved, this, [this](int, int){
            m_splitterSaveTimer.start();
        });
    }
    if (fmPreviewInfoSplitter) {
        connect(fmPreviewInfoSplitter, &QSplitter::splitterMoved, this, [this](int, int){
            m_splitterSaveTimer.start();
        });
    }

    // Restore persisted workspace for File Manager (after widgets are shown)
    // Use a longer delay to ensure window is fully laid out before restoring splitter sizes
    QTimer::singleShot(100, this, [this]{
        QSettings s("AugmentCode", "KAssetManager");
        // View mode and preview visibility first
        if (s.contains("FileManager/ViewMode")) {
            bool grid = s.value("FileManager/ViewMode").toBool();
            fmIsGridMode = grid;
            fmViewStack->setCurrentIndex(grid ? 0 : 1);
            if (fmViewModeButton) fmViewModeButton->setIcon(grid ? icoGrid() : icoList());

            // Always ensure correct sort order on startup
            // Grid view: always A-Z with folders first
            // List view: default to A-Z with folders first (user can change via column headers)
            if (fmProxyModel) {
                fmProxyModel->sort(0, Qt::AscendingOrder);
            }
            if (fmListView) {
                fmListView->sortByColumn(0, Qt::AscendingOrder);
            }
        }
        if (s.contains("FileManager/PreviewVisible")) {
            bool vis = s.value("FileManager/PreviewVisible").toBool();
            if (fmPreviewToggleButton) fmPreviewToggleButton->setChecked(vis);
            if (fmPreviewInfoSplitter) fmPreviewInfoSplitter->setVisible(vis);
        }
        // Group sequences toggle for File Manager
        fmGroupSequences = s.value("FileManager/GroupSequences", true).toBool();
        setSequenceGroupingEnabled(fmGroupSequences);

        // Group sequences toggle for Asset Manager (separate setting)
        bool amGroupSequences = s.value("AssetManager/GroupSequences", true).toBool();
        setAssetManagerSequenceGroupingEnabled(amGroupSequences);

        // Hide folders toggle
        fmHideFolders = s.value("FileManager/HideFolders", false).toBool();
        if (fmHideFoldersCheckBox) fmHideFoldersCheckBox->setChecked(fmHideFolders);
        if (fmDirModel) {
            QDir::Filters filters = QDir::NoDotAndDotDot | (fmHideFolders ? QDir::Files : QDir::AllEntries);
            fmDirModel->setFilter(filters);
            // Ensure current root is preserved
            QString pathNow = fmDirModel->rootPath();
            if (!pathNow.isEmpty()) {
                QModelIndex srcRoot = fmDirModel->index(pathNow);
                if (fmProxyModel) {
                    fmProxyModel->rebuildForRoot(pathNow);
                    QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
                    if (fmGridView) fmGridView->setRootIndex(proxyRoot);
                    if (fmListView) fmListView->setRootIndex(proxyRoot);
                } else {
                    if (fmGridView) fmGridView->setRootIndex(srcRoot);
                    if (fmListView) fmListView->setRootIndex(srcRoot);
                }
            }
        }

        // Splitters - restore state
        if (fmSplitter && s.contains("FileManager/MainSplitter")) fmSplitter->restoreState(s.value("FileManager/MainSplitter").toByteArray());
        if (fmLeftSplitter && s.contains("FileManager/LeftSplitter")) fmLeftSplitter->restoreState(s.value("FileManager/LeftSplitter").toByteArray());
        if (fmRightSplitter && s.contains("FileManager/RightSplitter")) fmRightSplitter->restoreState(s.value("FileManager/RightSplitter").toByteArray());
        if (fmPreviewInfoSplitter && s.contains("FileManager/PreviewInfoSplitter")) fmPreviewInfoSplitter->restoreState(s.value("FileManager/PreviewInfoSplitter").toByteArray());
        // Fallback: explicit sizes if present (skip if any size is 0 or too small - indicates collapsed panel)
        auto applySizes = [](QSplitter* sp, const QVariant& v) {
            if (!sp) return;
            if (!v.isValid()) return;
            QList<int> sizes;
            const auto list = v.toList();
            bool hasZero = false;
            for (const QVariant &x : list) {
                int sz = x.toInt();
                sizes << sz;
                if (sz < 10) hasZero = true;  // Skip if any panel is essentially collapsed
            }
            if (!sizes.isEmpty() && !hasZero) sp->setSizes(sizes);
        };
        applySizes(fmSplitter, s.value("FileManager/MainSplitterSizes"));
        applySizes(fmLeftSplitter, s.value("FileManager/LeftSplitterSizes"));
        applySizes(fmRightSplitter, s.value("FileManager/RightSplitterSizes"));
        applySizes(fmPreviewInfoSplitter, s.value("FileManager/PreviewInfoSplitterSizes"));

        // Headers
        if (fmListView && fmListView->model()) {
            auto hh = fmListView->horizontalHeader();
            for (int c = 0; c < fmListView->model()->columnCount(); ++c) {
                QVariant v = s.value(QString("FileManager/ListView/Col%1").arg(c));
                if (v.isValid()) hh->resizeSection(c, v.toInt());
            }
        }
        if (fmTree && fmTree->model()) {
            auto th = fmTree->header();
            for (int c = 0; c < fmTree->model()->columnCount(); ++c) {
                QVariant v = s.value(QString("FileManager/Tree/Col%1").arg(c));
                if (v.isValid()) th->resizeSection(c, v.toInt());
            }
        }
        // Restore current navigation path (use unified navigator to keep buttons/history consistent)
        if (s.contains("FileManager/CurrentPath")) {
            QString savedPath = s.value("FileManager/CurrentPath").toString();
            if (QFileInfo::exists(savedPath)) {
                fmNavigateToPath(savedPath, false);
            }
        }
        
        // Restore dual-pane state if it was enabled
        if (s.value("FileManager/DualPane", false).toBool()) {
            if (fmDualPaneToggle) {
                fmDualPaneToggle->setChecked(true);  // This triggers onFmToggleSecondPane
                
                // Restore dual-pane splitter sizes after a short delay to ensure layout is ready
                QTimer::singleShot(100, this, [this]() {
                    QSettings s("AugmentCode", "KAssetManager");
                    if (fmDualPaneSplitter && s.contains("FileManager/DualPaneSplitterSizes")) {
                        QList<int> sizes;
                        const auto list = s.value("FileManager/DualPaneSplitterSizes").toList();
                        bool hasZero = false;
                        for (const QVariant &x : list) {
                            int sz = x.toInt();
                            sizes << sz;
                            if (sz < 10) hasZero = true;
                        }
                        if (!sizes.isEmpty() && !hasZero) {
                            fmDualPaneSplitter->setSizes(sizes);
                        }
                    }
                });
            }
        }
    });

    LogManager::instance().addLog("[TRACE] setupFileManagerUi exit", "DEBUG");
}

void MainWindow::onFmTreeCurrentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);
    LogManager::instance().addLog("[FileManager] Tree selection changed", "INFO");
    if (!current.isValid()) {
        LogManager::instance().addLog("[FileManager] Invalid index in tree selection", "WARN");
        return;
    }
    
    // Debounce rapid tree selection changes to avoid blocking the UI
    // Store the pending path and reset the timer
    QString path = fmPathForIndex(current);
    if (path.isEmpty()) {
        LogManager::instance().addLog("[FileManager] Empty path from tree index", "WARN");
        return;
    }
    
    fmPendingNavigationPath = path;
    
    // Configure debounce timer if not already done
    if (!fmNavigationDebounceTimer.isSingleShot()) {
        fmNavigationDebounceTimer.setSingleShot(true);
        fmNavigationDebounceTimer.setInterval(50); // 50ms debounce
        connect(&fmNavigationDebounceTimer, &QTimer::timeout, this, [this]() {
            if (!fmPendingNavigationPath.isEmpty()) {
                QString path = fmPendingNavigationPath;
                fmPendingNavigationPath.clear();
                
                fmSuppressTreeSync = true;
                // Navigate the active pane (primary or secondary)
                if (fmSecondaryPane && fmSecondaryPane->isVisible() && !fmPrimaryPaneActive) {
                    fmSecondaryPane->navigateToPath(path, true);
                } else {
                    fmNavigateToPath(path, true);
                }
                fmSuppressTreeSync = false;
            }
        });
    }
    
    // Restart the timer - this cancels any pending navigation and starts fresh
    fmNavigationDebounceTimer.start();
}


void MainWindow::onFmTreeActivated(const QModelIndex &index)
{
    QString path = fmPathForIndex(index);
    LogManager::instance().addLog(QString("[FileManager] Tree activated, path: %1").arg(path), "INFO");
    if (path.isEmpty()) {
        LogManager::instance().addLog("[FileManager] Empty path from tree index", "WARN");
        return;
    }

    fmSuppressTreeSync = true;
    fmNavigateToPath(path, true);
    fmSuppressTreeSync = false;
}

// Forward declarations for file-type helpers used by File Manager handlers
static inline bool isImageFile(const QString &ext);
static inline bool isVideoFile(const QString &ext);
static bool isPreviewOverlayViewable(const QString &ext);

void MainWindow::onFmItemDoubleClicked(const QModelIndex &index)
{
    QModelIndex idx = index.sibling(index.row(), 0);
    // If view uses proxy, map to source when needed
    QModelIndex srcIdx = idx;
    if (fmProxyModel && idx.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(idx);

    QString path = fmDirModel->filePath(srcIdx);
    if (path.isEmpty()) return;

    // If grouping is enabled and this is a representative, open sequence in overlay
    if (fmProxyModel && fmGroupSequences && idx.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(idx)) {
        auto info = fmProxyModel->infoForProxyIndex(idx);
        QStringList frames = reconstructSequenceFramePaths(info.reprPath, info.start, info.end);
        if (!frames.isEmpty()) {
            if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
                imagePreviewOverlay->hide();
            }
            if (!previewOverlay) {
                previewOverlay = new PreviewOverlay(this);
                // Center overlay to the app window instead of screen top-left
                previewOverlay->setGeometry(geometry());
                connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
            } else {
                previewOverlay->stopPlayback();
            }
            // Remember source view/index for focus restoration on close
            QAbstractItemView* srcView = (fmGridView && fmGridView->isVisible() && fmGridView->hasFocus()) ? static_cast<QAbstractItemView*>(fmGridView) : static_cast<QAbstractItemView*>(fmListView);
            fmOverlayCurrentIndex = QPersistentModelIndex(idx);
            fmOverlaySourceView = srcView;
            // Build display name
            int pad = 0;
            auto m = SequenceDetector::mainPattern().match(QFileInfo(info.reprPath).fileName());
            if (m.hasMatch()) pad = m.captured(3).length(); else pad = QString::number(info.start).length();
            QString s0 = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
            QString s1 = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
            QString seqName = QString("%1.[%2-%3].%4").arg(info.base, s0, s1, info.ext);
            previewOverlay->showSequence(frames, seqName, info.start, info.end);
            return;
        }
    }

    QFileInfo fi(path);
    if (fi.isDir()) {
        fmNavigateToPath(path, true);
        return;
    }

    const QString ext = fi.suffix();
    const bool isImage = FileUtils::isImageFile(ext);
    const bool isVideo = FileUtils::isVideoFile(ext);
    if (isImage || isVideo) {
        // Remember source view/index for focus restoration on close
        QAbstractItemView* srcView = (fmGridView && fmGridView->isVisible() && fmGridView->hasFocus()) ? static_cast<QAbstractItemView*>(fmGridView) : static_cast<QAbstractItemView*>(fmListView);
        fmOverlayCurrentIndex = QPersistentModelIndex(idx);
        fmOverlaySourceView = srcView;

        if (isImage && !isVideo) {
            if (previewOverlay && previewOverlay->isVisible()) {
                previewOverlay->stopPlayback();
                previewOverlay->hide();
            }
            if (!imagePreviewOverlay) {
                imagePreviewOverlay = new ImagePreviewOverlay(this);
                imagePreviewOverlay->setGeometry(geometry());
                connect(imagePreviewOverlay, &ImagePreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(imagePreviewOverlay, &ImagePreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
            } else {
                imagePreviewOverlay->stopPlayback();
            }
            imagePreviewOverlay->showImage(path, fi.fileName(), fi.suffix());
        } else {
            if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
                imagePreviewOverlay->stopPlayback();
                imagePreviewOverlay->hide();
            }
            if (!previewOverlay) {
                previewOverlay = new PreviewOverlay(this);
                // Center overlay to the app window instead of screen top-left
                previewOverlay->setGeometry(geometry());
                connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
            } else {
                previewOverlay->stopPlayback();
            }
            previewOverlay->showAsset(path, fi.fileName(), fi.suffix());
        }
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

static QString uniqueNameInDir(const QString &dirPath, const QString &baseName)
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


QStringList getSelectedFileManagerPaths(QFileSystemModel *model, QListView *grid, QTableView *list, QStackedWidget *stack)
{
    QStringList out;
    auto mapToSource = [](const QModelIndex &viewIdx) -> QModelIndex {
        if (!viewIdx.isValid()) return viewIdx;
        auto proxy = qobject_cast<const QSortFilterProxyModel*>(viewIdx.model());
        if (proxy) return proxy->mapToSource(viewIdx);
        return viewIdx;
    };

    if (stack->currentIndex() == 0) {
        const auto idxs = grid->selectionModel()->selectedIndexes();
        for (const QModelIndex &idx : idxs) {
            if (idx.column() != 0) continue;
            QModelIndex src = mapToSource(idx);
            out << model->filePath(src);
        }
    } else {
        const auto rows = list->selectionModel()->selectedRows();
        for (const QModelIndex &idx : rows) {
            QModelIndex src = mapToSource(idx);
            out << model->filePath(src);
        }
    }
    out.removeDuplicates();
    return out;
}

void MainWindow::onFmCopy()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    
    // In dual-pane mode, copy from the active pane
    if (fmSecondaryPane && fmSecondaryPane->isVisible() && !fmPrimaryPaneActive) {
        fmSecondaryPane->copySelectedToClipboard();
        // Also store in MainWindow's clipboard for paste
        fmClipboard = fmSecondaryPane->clipboardPaths();
        fmClipboardCutMode = false;
    } else {
        fmClipboard = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
        fmClipboardCutMode = false;
    }
}

void MainWindow::onFmCut()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    
    // In dual-pane mode, cut from the active pane
    if (fmSecondaryPane && fmSecondaryPane->isVisible() && !fmPrimaryPaneActive) {
        fmSecondaryPane->cutSelectedToClipboard();
        // Also store in MainWindow's clipboard for paste
        fmClipboard = fmSecondaryPane->clipboardPaths();
        fmClipboardCutMode = true;
    } else {
        fmClipboard = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
        fmClipboardCutMode = true;
    }
}

void MainWindow::onFmPaste()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    if (fmClipboard.isEmpty()) return;
    
    // Determine destination directory based on active pane
    QString destDir;
    if (fmSecondaryPane && fmSecondaryPane->isVisible() && !fmPrimaryPaneActive) {
        destDir = fmSecondaryPane->currentPath();
    } else {
        destDir = fmDirModel->rootPath();
    }

    // Ensure any preview locks are released before file ops
    if (fmGStreamerPlayer) { fmGStreamerPlayer->stop(); }
    // Release any locks held by previews
    releaseAnyPreviewLocksForPaths(fmClipboard);
    // Enqueue async operation
    auto &q = FileOpsQueue::instance();
    if (fmClipboardCutMode) q.enqueueMove(fmClipboard, destDir);
    else q.enqueueCopy(fmClipboard, destDir);

    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();

    fmClipboard.clear();
    fmClipboardCutMode = false;
}

void MainWindow::onFmDelete()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    QStringList paths = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    if (paths.isEmpty()) return;


    // Ensure any preview locks are released before file ops
    releaseAnyPreviewLocksForPaths(paths);
    // Enqueue async delete
    auto &q = FileOpsQueue::instance();
    q.enqueueDelete(paths);

}


void MainWindow::onFmDeletePermanent()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    QStringList paths = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    if (paths.isEmpty()) return;


    // Ensure any preview locks are released before file ops
    releaseAnyPreviewLocksForPaths(paths);
    auto &q = FileOpsQueue::instance();
    q.enqueueDeletePermanent(paths);

}

void MainWindow::onFmBackToParent()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    onFmNavigateUp();
}

void MainWindow::onFmRefresh()
{
    if (!fmDirModel) return;

    // Get the current directory path
    QString currentPath = fmDirModel->rootPath();

    if (currentPath.isEmpty()) {
        // If no specific path is set, just update viewports
        if (fmGridView) fmGridView->viewport()->update();
        if (fmListView) fmListView->viewport()->update();
        statusBar()->showMessage("File Manager refreshed", 2000);
        return;
    }

    // Manual F5 refresh: force QFileSystemModel to re-read the directory.
    // QFileSystemModel has an internal file watcher but manual refresh can be useful
    // if the watcher missed something or for external drive changes.
    // 
    // Note: We intentionally do NOT clear LivePreviewManager cache here.
    // Cache is keyed by file path, so stale entries won't be displayed.
    // Clearing it causes all visible thumbnails to flicker and re-decode.

    // Re-read directory by flipping root path (only for manual F5 refresh)
    QString tempPath = QDir::tempPath();
    fmDirModel->setRootPath(tempPath);
    fmDirModel->setRootPath(currentPath);

    // Rebuild sequence grouping proxy if enabled (required after model reset)
    if (fmProxyModel && fmProxyModel->groupingEnabled()) {
        fmProxyModel->rebuildForRoot(currentPath);
    }

    // Force viewport updates
    if (fmGridView) fmGridView->viewport()->update();
    if (fmListView) fmListView->viewport()->update();

    statusBar()->showMessage("Folder refreshed", 2000);
}

// Lightweight refresh used by QFileSystemWatcher; avoid heavy model resets
void MainWindow::onFmLightRefresh()
{
    if (fmGridView && fmGridView->viewport()) fmGridView->viewport()->update();
    if (fmListView && fmListView->viewport()) fmListView->viewport()->update();
    // Do not rebuild models, clear caches, or flip root paths here.
}

void MainWindow::onFmRename()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    QStringList paths = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);

    if (paths.size() != 1) return;
    QString p = paths.first();
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

void MainWindow::onFmBulkRename()
{
    QStringList paths = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    if (paths.size() < 2) return;


    releaseAnyPreviewLocksForPaths(paths);

    BulkRenameDialog dialog(paths, this);
    if (dialog.exec() == QDialog::Accepted) {
        // Refresh the file manager view
        if (fmDirModel) {
            QString currentPath = fmDirModel->rootPath();
            fmDirModel->setRootPath("");
            fmDirModel->setRootPath(currentPath);
        }
        statusBar()->showMessage("Bulk rename completed", 3000);
    }
}

void MainWindow::onFmNewFolder()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    const QString destDir = fmDirModel->rootPath();
    QString path = uniqueNameInDir(destDir, "New Folder");
    QDir().mkpath(path);
}

void MainWindow::onFmAddToFavorites()
{
    QStringList sel = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    sel << getSelectedFmTreePaths();
    sel.removeDuplicates();
    if (sel.isEmpty()) return;
    bool changed = false;
    for (const QString &p : sel) {
        if (!fmFavorites.contains(p)) {
            fmFavorites << p;
            changed = true;
        }
    }
    if (changed) {
        fmFavorites.removeDuplicates();
        saveFmFavorites();
        // refresh list
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
}

void MainWindow::onFmRemoveFavorite()
{
    if (!fmFavoritesList) return;
    QListWidgetItem *it = fmFavoritesList->currentItem();
    if (!it) return;
    QString path = it->data(Qt::UserRole).toString();
    fmFavorites.removeAll(path);
    delete it;
    saveFmFavorites();
}

void MainWindow::onFmFavoriteActivated(QListWidgetItem* item)
{
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;
    fmNavigateToPath(path, true);
}

void MainWindow::loadFmFavorites()
{
    LogManager::instance().addLog("[TRACE] loadFmFavorites begin", "DEBUG");
    fmFavorites.clear();
    QSettings s("AugmentCode", "KAssetManager");
    int size = s.beginReadArray("FileManager/Favorites");
    LogManager::instance().addLog(QString("[TRACE] loadFmFavorites stored entries=%1").arg(size), "DEBUG");
    for (int i=0;i<size;++i) {
        s.setArrayIndex(i);
        QString p = s.value("path").toString();
        if (!p.isEmpty()) fmFavorites << p;
    }
    s.endArray();
    fmFavorites.removeDuplicates();
    if (fmFavoritesList) {
        fmFavoritesList->clear();
        for (const QString &p : fmFavorites) {
            LogManager::instance().addLog(QString("[TRACE] favorite raw=%1").arg(p), "DEBUG");
            QListWidgetItem *it = new QListWidgetItem(QIcon::fromTheme("star"), QFileInfo(p).fileName());
            it->setToolTip(p);
            it->setData(Qt::UserRole, p);
            fmFavoritesList->addItem(it);
            LogManager::instance().addLog(QString("[TRACE] favorite item added=%1").arg(p), "DEBUG");
        }
    }
    LogManager::instance().addLog(QString("[TRACE] loadFmFavorites count=%1").arg(fmFavorites.size()), "DEBUG");
}

void MainWindow::saveFmFavorites()
{
    QSettings s("AugmentCode", "KAssetManager");
    s.beginWriteArray("FileManager/Favorites");
    for (int i=0;i<fmFavorites.size();++i) {
        s.setArrayIndex(i);
        s.setValue("path", fmFavorites.at(i));
    }
    s.endArray();
}


void MainWindow::onFmShowContextMenu(const QPoint &pos)
{
    QWidget *senderW = qobject_cast<QWidget*>(sender());
    QPoint globalPos = senderW->mapToGlobal(pos);
    QMenu menu;
    QAction *refreshA = menu.addAction("Refresh", this, &MainWindow::onFmRefresh, QKeySequence(Qt::Key_F5));
    menu.addSeparator();
    QAction *copyA = menu.addAction("Copy", this, &MainWindow::onFmCopy, QKeySequence::Copy);
    QAction *cutA = menu.addAction("Cut", this, &MainWindow::onFmCut, QKeySequence::Cut);
    QAction *pasteA = menu.addAction("Paste", this, &MainWindow::onFmPaste, QKeySequence::Paste);
    menu.addSeparator();
    QAction *renameA = menu.addAction("Rename", this, &MainWindow::onFmRename, QKeySequence(Qt::Key_F2));
    QAction *bulkRenameA = menu.addAction("Bulk Rename...", this, &MainWindow::onFmBulkRename);
    QAction *delA = menu.addAction("Delete", this, &MainWindow::onFmDelete, QKeySequence::Delete);
    QAction *createFolderWithSel = menu.addAction("Create Folder with Selected Files", this, &MainWindow::onFmCreateFolderWithSelected);
    menu.addSeparator();
    QAction *addLibA = menu.addAction("Add to Asset Library", this, &MainWindow::onAddSelectionToAssetLibrary);

    QAction *favA = menu.addAction("Add to Favorites", this, &MainWindow::onFmAddToFavorites);

    menu.addSeparator();
    QAction *openInExplorerA = menu.addAction("Open in Explorer");
    QAction *propertiesA = menu.addAction("Properties");
    QAction *openWithA = menu.addAction("Open With...");

    // Enable/disable depending on selection
    QStringList selectedPaths = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    bool hasSel = !selectedPaths.isEmpty();
    int selCount = selectedPaths.size();

    copyA->setEnabled(hasSel);
    cutA->setEnabled(hasSel);
    renameA->setEnabled(selCount == 1);
    bulkRenameA->setEnabled(selCount >= 2);
    delA->setEnabled(hasSel);
    pasteA->setEnabled(!fmClipboard.isEmpty());
    addLibA->setEnabled(hasSel);
    favA->setEnabled(hasSel);
    createFolderWithSel->setEnabled(hasSel);
    openInExplorerA->setEnabled(selCount == 1);
    propertiesA->setEnabled(selCount == 1);
    // Add Convert to Format... when selection are supported media files
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
        if (allSupported) {
            convertA = menu.addAction("Convert to Format...");
        }
    }

    openWithA->setEnabled(selCount == 1);

    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;
    if (chosen == convertA) {
        // Non-modal conversion dialog
        releaseAnyPreviewLocksForPaths(selectedPaths);
        auto *dlg = new MediaConvertDialog(selectedPaths, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::accepted, this, &MainWindow::onFmRefresh);
        connect(dlg, &QObject::destroyed, this, [this](){ QTimer::singleShot(100, this, &MainWindow::onFmRefresh); });
        dlg->show(); dlg->raise(); dlg->activateWindow();
        return;
    }


    // Handle new context menu actions
    if (chosen == openInExplorerA && selCount == 1) {
        QString path = selectedPaths.first();
        // Use explorer.exe /select to open Explorer and select the file
        QProcess::startDetached("explorer.exe", QStringList() << "/select," << QDir::toNativeSeparators(path));
    } else if (chosen == propertiesA && selCount == 1) {
        QString path = selectedPaths.first();
        // Use Windows Shell API to show Properties dialog
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
        // Use Windows Shell API to show Open With dialog
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

void MainWindow::onFmTreeContextMenu(const QPoint &pos)
{
    if (!fmTree) return;
    QModelIndex idx = fmTree->indexAt(pos);
    if (!idx.isValid()) return;
    QString path = fmPathForIndex(idx);
    if (path.isEmpty()) return;

    QStringList selectedPaths = getSelectedFmTreePaths();

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
    menu.addSeparator();
    QAction *addLibA = menu.addAction("Add to Asset Library");
    QAction *importPmA = menu.addAction("Import to Project Manager");
    QAction *favA = menu.addAction("Add to Favorites");
    menu.addSeparator();
    QAction *openInExplorerA = menu.addAction("Open in Explorer");
    QAction *propertiesA = menu.addAction("Properties");

    // Enable states
    const bool hasClipboard = !fmClipboard.isEmpty();
    const bool hasSelection = !selectedPaths.isEmpty();
    const bool isSingleFolder = hasSelection && selectedPaths.size() == 1 && QFileInfo(selectedPaths.first()).isDir();
    pasteA->setEnabled(hasClipboard);
    copyA->setEnabled(hasSelection);
    cutA->setEnabled(hasSelection);
    delA->setEnabled(hasSelection);
    permDelA->setEnabled(hasSelection);
    addLibA->setEnabled(hasSelection);
    importPmA->setEnabled(isSingleFolder);
    favA->setEnabled(hasSelection);
    openInExplorerA->setEnabled(hasSelection && selectedPaths.size() == 1);
    propertiesA->setEnabled(hasSelection && selectedPaths.size() == 1);

    QAction *chosen = menu.exec(fmTree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == refreshA) {
        onFmRefresh();
    } else if (chosen == copyA) {
        fmClipboard = getSelectedFmTreePaths();
        fmClipboardCutMode = false;
    } else if (chosen == cutA) {
        fmClipboard = getSelectedFmTreePaths();
        fmClipboardCutMode = true;
    } else if (chosen == pasteA) {
        onFmPasteInto(path);
    } else if (chosen == delA) {
        QStringList paths = getSelectedFmTreePaths();
        if (paths.isEmpty()) return;

        releaseAnyPreviewLocksForPaths(paths);
        FileOpsQueue::instance().enqueueDelete(paths);

    } else if (chosen == permDelA) {
        QStringList paths = getSelectedFmTreePaths();
        releaseAnyPreviewLocksForPaths(paths);
        doPermanentDelete(paths);
    } else if (chosen == renameA) {
        QStringList paths = getSelectedFmTreePaths();
        if (paths.size() != 1) return;
        QFileInfo fi(paths.first());
        bool ok=false;
        QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, fi.fileName(), &ok);
        if (!ok || newName.trimmed().isEmpty()) return;
        QDir parent(fi.absolutePath());
        parent.rename(fi.fileName(), newName.trimmed());
    } else if (chosen == newFolderA) {
        QDir dir(path);
        QString newPath = uniqueNameInDir(path, "New Folder");
        dir.mkpath(newPath);
    } else if (chosen == createFolderWithSelA) {
        // Use selection from main view, create folder inside tree path
        QStringList files = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
        if (files.isEmpty()) return;
        bool ok=false;
        QString folderName = QInputDialog::getText(this, "Create Folder", "Enter folder name:", QLineEdit::Normal, "New Folder", &ok);
        if (!ok) return;
        folderName = folderName.trimmed();
        if (folderName.isEmpty()) return;
        QDir dd(path);
        QString folderPath = dd.filePath(folderName);
        if (QFileInfo::exists(folderPath)) {
            int i=2; QString base = folderName;
            while (QFileInfo::exists(folderPath)) { folderName = QString("%1 (%2)").arg(base).arg(i++); folderPath = dd.filePath(folderName); }
        }
        if (!dd.mkpath(folderPath)) {
            QMessageBox::warning(this, "Error", QString("Failed to create folder: %1").arg(folderPath));
            return;
        }
        releaseAnyPreviewLocksForPaths(files);
        FileOpsQueue::instance().enqueueMove(files, folderPath);
        if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
        fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
    } else if (chosen == addLibA) {
        onAddTreeSelectionToAssetLibrary();
    } else if (chosen == importPmA && isSingleFolder) {
        QString folderPath = selectedPaths.first();
        QString folderName = QDir(folderPath).dirName();
        pmImportToProject(folderName, folderPath);
        // Switch to Project Manager tab
        if (mainTabs) mainTabs->setCurrentWidget(projectManagerPage);
    } else if (chosen == favA) {
        onFmAddToFavorites();
    } else if (chosen == openInExplorerA && hasSelection && selectedPaths.size() == 1) {
        const QString p = selectedPaths.first();
        QProcess::startDetached("explorer.exe", QStringList() << "/select," << QDir::toNativeSeparators(p));
    } else if (chosen == propertiesA && hasSelection && selectedPaths.size() == 1) {
        const QString p = selectedPaths.first();
#ifdef Q_OS_WIN
        std::wstring wpath = p.toStdWString();
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"properties";
        sei.lpFile = wpath.c_str();
        sei.nShow = SW_SHOW;
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        ShellExecuteExW(&sei);
#endif
    }
}

QStringList MainWindow::getSelectedFmTreePaths() const
{
    QStringList out;
    if (!fmTree) return out;
    auto sel = fmTree->selectionModel();
    if (!sel) return out;
    const auto rows = sel->selectedRows();
    for (const QModelIndex &idx : rows) {
        out << fmPathForIndex(idx);
    }
    out.removeDuplicates();
    return out;
}

void MainWindow::onFmPasteInto(const QString& destDir)
{
    if (fmClipboard.isEmpty()) return;
    // Ensure any preview locks are released before file ops
    releaseAnyPreviewLocksForPaths(fmClipboard);
    auto &q = FileOpsQueue::instance();
    if (fmClipboardCutMode) q.enqueueMove(fmClipboard, destDir);
    else q.enqueueCopy(fmClipboard, destDir);
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
    fmClipboard.clear();
    fmClipboardCutMode = false;
}

void MainWindow::doPermanentDelete(const QStringList& paths)
{
    if (paths.isEmpty()) return;
    // Ensure any preview locks are released before file ops
    releaseAnyPreviewLocksForPaths(paths);
    FileOpsQueue::instance().enqueueDeletePermanent(paths);

}


void MainWindow::releaseAnyPreviewLocksForPaths(const QStringList& paths)
{
    QSet<QString> s; for (const QString &p : paths) s.insert(QFileInfo(p).absoluteFilePath());
    // Embedded FM preview: stop media and clear if current preview is among paths
    if (fmGStreamerPlayer) { fmGStreamerPlayer->stop(); }
    if (!fmCurrentPreviewPath.isEmpty()) {
        QString abs = QFileInfo(fmCurrentPreviewPath).absoluteFilePath();
        if (s.contains(abs)) {
            clearFmPreview();
        }
    }
    // Overlay: if showing one of these files, close it to fully release handles
    if (previewOverlay) {
        QString cur = previewOverlay->currentPath();
        if (s.contains(QFileInfo(cur).absoluteFilePath())) {
            closePreview();
        } else {
            // still release any handles
            previewOverlay->stopPlayback();
        }
    }
}


void MainWindow::onFmCreateFolderWithSelected()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    QStringList paths = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    if (paths.isEmpty()) return;
    // Destination directory is current root of fmDirModel
    const QString destDir = fmDirModel->rootPath();
    bool ok=false;
    QString folderName = QInputDialog::getText(this, "Create Folder", "Enter folder name:", QLineEdit::Normal, "New Folder", &ok);
    if (!ok) return;
    folderName = folderName.trimmed();
    if (folderName.isEmpty()) return;
    QDir dd(destDir);
    QString folderPath = dd.filePath(folderName);
    if (FileUtils::pathExists(folderPath)) {
        // attempt unique suffix
        int i=2; QString base = folderName; while (FileUtils::pathExists(folderPath)) { folderName = QString("%1 (%2)").arg(base).arg(i++); folderPath = dd.filePath(folderName);}
    }
    if (!dd.mkpath(folderPath)) {
        QMessageBox::warning(this, "Error", QString("Failed to create folder: %1").arg(folderPath));
        return;
    }
    // Enqueue async move of selected into the new folder
    auto &q = FileOpsQueue::instance();
    q.enqueueMove(paths, folderPath);
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
}

void MainWindow::onFmViewModeToggled()
{
    fmIsGridMode = !fmIsGridMode;
    fmViewStack->setCurrentIndex(fmIsGridMode ? 0 : 1);
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
    // regardless of List view's current sort order
    if (fmIsGridMode && fmProxyModel) {
        fmProxyModel->sort(0, Qt::AscendingOrder);
    }

    // Persist immediately
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/ViewMode", fmIsGridMode);
    s.sync();
}

void MainWindow::onFmThumbnailSizeChanged(int size)
{
    if (fmGridView) {
        fmGridView->setIconSize(QSize(size, size));
        fmGridView->setGridSize(QSize(size + 8, size + 36));
        if (auto *d = dynamic_cast<FmItemDelegate*>(fmGridView->itemDelegate())) d->setThumbnailSize(size);
        // Trigger viewport update without resetting view state (which would clear root index)
        fmGridView->viewport()->update();
    }
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/GridThumbSize", size);
}

void MainWindow::onFmToggleSecondPane(bool checked)
{
    if (checked) {
        // Create secondary pane if it doesn't exist
        if (!fmSecondaryPane) {
            fmSecondaryPane = new FileManagerPane(this);
            
            // Connect path changes for synced navigation and tree sync
            connect(fmSecondaryPane, &FileManagerPane::pathChanged, this, &MainWindow::onSecondaryPanePathChanged);
            
            // Connect activated signal for active pane tracking
            connect(fmSecondaryPane, &FileManagerPane::activated, this, &MainWindow::onSecondaryPaneActivated);
            
            // Connect filesDropped for copy/move operations
            connect(fmSecondaryPane, &FileManagerPane::filesDropped, this, [this](const QStringList &paths, const QString &destDir) {
                // Determine if this is a cut (move) or copy operation
                bool isCut = fmSecondaryPane->isClipboardCutMode();
                
                // Use FileOpsQueue for the operation
                FileOpsQueue &queue = FileOpsQueue::instance();
                if (isCut) {
                    queue.enqueueMove(paths, destDir);
                } else {
                    queue.enqueueCopy(paths, destDir);
                }
            });
            
            // Connect fileDoubleClicked to open preview overlay
            connect(fmSecondaryPane, &FileManagerPane::fileDoubleClicked, this, &MainWindow::onSecondaryPaneFileDoubleClicked);
            
            // Navigate to same path as primary pane initially
            QString currentPath = fmDirModel ? fmDirModel->rootPath() : QString();
            if (!currentPath.isEmpty()) {
                fmSecondaryPane->navigateToPath(currentPath, false);
            }
            
            // Mark secondary pane as inactive initially
            fmSecondaryPane->setActive(false);
            
            // Hide the preview panel by default for the secondary pane
            fmSecondaryPane->setPreviewVisible(false);
        }
        
        // Create the dual-pane splitter if it doesn't exist
        if (!fmDualPaneSplitter) {
            // Find the right widget in fmSplitter
            if (fmSplitter && fmSplitter->count() >= 2) {
                QWidget *right = fmSplitter->widget(1);
                
                // Create a new horizontal splitter for dual-pane
                fmDualPaneSplitter = new QSplitter(Qt::Horizontal, this);
                fmDualPaneSplitter->setChildrenCollapsible(true);  // Allow free resizing
                
                // Reparent the existing right widget to the dual-pane splitter
                right->setParent(fmDualPaneSplitter);
                fmDualPaneSplitter->addWidget(right);
                fmDualPaneSplitter->addWidget(fmSecondaryPane);
                fmDualPaneSplitter->setStretchFactor(0, 1);
                fmDualPaneSplitter->setStretchFactor(1, 1);
                
                // Replace the right widget in fmSplitter with the dual-pane splitter
                fmSplitter->insertWidget(1, fmDualPaneSplitter);
            }
        }
        
        // Show the secondary pane
        if (fmSecondaryPane) {
            fmSecondaryPane->show();
        }
        
        // Show sync navigation button if not already present
        if (fmSyncNavButton) {
            fmSyncNavButton->show();
        }
    } else {
        // Hide the secondary pane
        if (fmSecondaryPane) {
            fmSecondaryPane->hide();
        }
        
        // Hide sync navigation button
        if (fmSyncNavButton) {
            fmSyncNavButton->hide();
        }
        
        // Reset to primary pane active
        setActiveFmPane(true);
    }
    
    // Persist state
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/DualPane", checked);
}

void MainWindow::onSecondaryPanePathChanged(const QString &path)
{
    // Handle synced navigation - only sync if explicitly enabled
    if (fmSyncNavigation && fmDirModel) {
        QString currentPrimaryPath = fmDirModel->rootPath();
        if (currentPrimaryPath != path) {
            fmSuppressTreeSync = true;
            fmNavigateToPath(path, true);
            fmSuppressTreeSync = false;
        }
    }
    
    // Update tree to show the secondary pane's current folder (if secondary is active)
    // Use fmSuppressTreeSync to prevent the tree change from triggering navigation
    if (!fmPrimaryPaneActive && fmTree) {
        fmSuppressTreeSync = true;
        QModelIndex idx = fmIndexForPath(path);
        if (idx.isValid()) {
            // Block signals to prevent tree selection from triggering another navigation
            fmTree->blockSignals(true);
            fmTree->setCurrentIndex(idx);
            fmTree->expand(idx);
            fmTree->blockSignals(false);
            // Keep horizontal scroll at the left
            if (fmTree->horizontalScrollBar()) {
                fmTree->horizontalScrollBar()->setValue(0);
            }
        }
        fmSuppressTreeSync = false;
    }
}

void MainWindow::onSecondaryPaneActivated()
{
    setActiveFmPane(false);
}

void MainWindow::onSecondaryPaneFileDoubleClicked(const QString &path)
{
    // Open preview overlay for file double-clicked in secondary pane
    QFileInfo info(path);
    if (!info.exists() || info.isDir()) return;
    
    const bool isImage = FileUtils::isImageFile(info.suffix());
    const bool isVideo = FileUtils::isVideoFile(info.suffix());

    // Mark that overlay was opened from secondary pane
    fmOverlayFromSecondaryPane = true;
    fmOverlayCurrentIndex = QPersistentModelIndex(fmSecondaryPane->currentIndex());
    fmOverlaySourceView = nullptr;  // Will use secondary pane's view

    if (isImage && !isVideo) {
        if (previewOverlay && previewOverlay->isVisible()) {
            previewOverlay->stopPlayback();
            previewOverlay->hide();
        }
        if (!imagePreviewOverlay) {
            imagePreviewOverlay = new ImagePreviewOverlay(this);
            imagePreviewOverlay->setGeometry(geometry());
            connect(imagePreviewOverlay, &ImagePreviewOverlay::closed, this, &MainWindow::closePreview);
            connect(imagePreviewOverlay, &ImagePreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
        } else {
            imagePreviewOverlay->stopPlayback();
        }
        imagePreviewOverlay->showImage(path, info.fileName(), info.suffix());
    } else {
        if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
            imagePreviewOverlay->stopPlayback();
            imagePreviewOverlay->hide();
        }
        if (!previewOverlay) {
            previewOverlay = new PreviewOverlay(this);
            previewOverlay->setGeometry(geometry());
            connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
            connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
        } else {
            previewOverlay->stopPlayback();
        }
        previewOverlay->showAsset(path, info.fileName(), info.suffix());
    }
}

void MainWindow::setActiveFmPane(bool primary)
{
    if (fmPrimaryPaneActive == primary) return;  // No change
    
    fmPrimaryPaneActive = primary;
    
    // Update visual states
    if (fmSecondaryPane) {
        fmSecondaryPane->setActive(!primary);
    }
    
    // Update primary pane visual state (using border on the view stack)
    if (fmViewStack) {
        if (primary) {
            // Active: show blue border
            fmViewStack->setStyleSheet(
                "QStackedWidget { "
                "   border: 2px solid #0078D4; "
                "   border-radius: 4px; "
                "}"
            );
        } else {
            // Inactive: transparent border
            fmViewStack->setStyleSheet(
                "QStackedWidget { "
                "   border: 2px solid transparent; "
                "   border-radius: 4px; "
                "}"
            );
        }
    }
    
    LogManager::instance().addLog(
        QString("[FileManager] Active pane: %1").arg(primary ? "Primary" : "Secondary"), "DEBUG");
}

void MainWindow::onFmSyncNavToggled(bool checked)
{
    fmSyncNavigation = checked;
    
    // If enabling sync, navigate secondary to match primary
    if (checked && fmSecondaryPane && fmDirModel) {
        QString currentPath = fmDirModel->rootPath();
        if (!currentPath.isEmpty()) {
            fmSecondaryPane->navigateToPath(currentPath, false);
        }
    }
    
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/SyncNav", checked);
}


void MainWindow::onAddSelectionToAssetLibrary()
{
    // Collect selected paths (files and folders) from the active File Manager view.
    // Map proxy indexes to source before using fmDirModel APIs.
    QStringList filePaths;
    QStringList folderPaths;

    const bool isGrid = (fmViewStack->currentIndex() == 0);
    if (isGrid) {
        if (!fmGridView || !fmGridView->selectionModel()) return;
        const auto indexes = fmGridView->selectionModel()->selectedIndexes();
        for (const QModelIndex &idx : indexes) {
            QModelIndex srcIdx = idx;
            if (fmProxyModel && idx.model() == fmProxyModel)
                srcIdx = fmProxyModel->mapToSource(idx);
            if (!srcIdx.isValid()) continue;
            const QString path = fmDirModel->filePath(srcIdx);
            if (path.isEmpty()) continue;
            if (fmDirModel->isDir(srcIdx)) folderPaths << path; else filePaths << path;
        }
    } else {
        if (!fmListView || !fmListView->selectionModel()) return;
        const auto rows = fmListView->selectionModel()->selectedRows();
        for (const QModelIndex &idx : rows) {
            QModelIndex srcIdx = idx;
            if (fmProxyModel && idx.model() == fmProxyModel)
                srcIdx = fmProxyModel->mapToSource(idx);
            if (!srcIdx.isValid()) continue;
            const QString path = fmDirModel->filePath(srcIdx);
            if (path.isEmpty()) continue;
            if (fmDirModel->isDir(srcIdx)) folderPaths << path; else filePaths << path;
        }
    }

    importToAssetLibrary(filePaths, folderPaths);
}

void MainWindow::onAddTreeSelectionToAssetLibrary()
{
    // Import the selected folders from the File Manager tree into the Asset Library.
    const QStringList folderPaths = getSelectedFmTreePaths();
    importToAssetLibrary(QStringList(), folderPaths);
}

void MainWindow::importToAssetLibrary(const QStringList& filePathsIn, const QStringList& folderPathsIn)
{
    QStringList filePaths = filePathsIn;
    QStringList folderPaths = folderPathsIn;

    filePaths.removeDuplicates();
    folderPaths.removeDuplicates();

    if (filePaths.isEmpty() && folderPaths.isEmpty()) return;

    // Ensure a destination asset folder is selected
    if (!folderTreeView || !folderTreeView->currentIndex().isValid()) {
        QMessageBox::warning(this, "No Folder Selected", "Please select a folder in the Asset Library before importing.");
        return;
    }
    const int targetFolderId = folderTreeView->currentIndex().data(VirtualFolderTreeModel::IdRole).toInt();

    // Show progress dialog
    if (!importProgressDialog) importProgressDialog = new ImportProgressDialog(this);
    importProgressDialog->show();
    importProgressDialog->raise();
    importProgressDialog->activateWindow();

    // Prevent the dialog from closing between multiple import calls
    disconnect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);

    int totalImported = 0;

    // Import folders preserving subfolder structure
    for (const QString &dir : folderPaths) {
        if (importer->importFolder(dir, targetFolderId)) totalImported++;
    }

    // Import individual files
    if (!filePaths.isEmpty()) {
        importer->importFiles(filePaths, targetFolderId); // emits importFinished
        totalImported += filePaths.size();
    }

    // Reconnect and close dialog
    connect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);
    onImportComplete();

    if (totalImported > 0) {
        statusBar()->showMessage(QString("Imported %1 item(s)").arg(totalImported), 3000);
    }
}

// ============= PROJECT MANAGER UI =============

void MainWindow::setupProjectManagerUi()
{
    LogManager::instance().addLog("[ProjectManager] Setting up Project Manager UI...", "INFO");
    
    // Initialize ProjectDB with separate database file
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString projectsDbPath = dataDir + "/projects.db";
    if (!ProjectDB::instance().init(projectsDbPath)) {
        LogManager::instance().addLog("[ProjectManager] Failed to initialize ProjectDB at: " + projectsDbPath, "ERROR");
        return;
    }
    LogManager::instance().addLog("[ProjectManager] ProjectDB initialized at: " + projectsDbPath, "INFO");
    
    // Main layout for project manager page
    QVBoxLayout *pmLayout = new QVBoxLayout(projectManagerPage);
    pmLayout->setContentsMargins(0, 0, 0, 0);
    
    // Main splitter: left (project list + folders) | right (toolbar + assets + preview)
    pmSplitter = new QSplitter(Qt::Horizontal, projectManagerPage);
    pmLayout->addWidget(pmSplitter);
    
    // ===== LEFT PANEL: Projects + Folder Tree =====
    QWidget *leftPanel = new QWidget(pmSplitter);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);
    
    // Left splitter: projects list | folder tree
    pmLeftSplitter = new QSplitter(Qt::Vertical, leftPanel);
    
    // -- Projects section --
    QWidget *projectsContainer = new QWidget(pmLeftSplitter);
    QVBoxLayout *projectsLayout = new QVBoxLayout(projectsContainer);
    projectsLayout->setContentsMargins(0, 0, 0, 0);
    projectsLayout->setSpacing(0);
    
    // Project list header
    QWidget *projectHeader = new QWidget(projectsContainer);
    QHBoxLayout *headerLayout = new QHBoxLayout(projectHeader);
    headerLayout->setContentsMargins(8, 8, 8, 8);
    
    QLabel *projectsLabel = new QLabel("Projects", projectHeader);
    QFont projectsLabelFont = projectsLabel->font();
    projectsLabelFont.setPointSize(14);
    projectsLabelFont.setBold(true);
    projectsLabel->setFont(projectsLabelFont);
    headerLayout->addWidget(projectsLabel);
    headerLayout->addStretch();
    
    // Add project button
    QToolButton *addProjectBtn = new QToolButton(projectHeader);
    addProjectBtn->setIcon(icoAdd(ThemeManager::instance().iconColor()));
    addProjectBtn->setToolTip("Add New Project");
    addProjectBtn->setAutoRaise(true);
    connect(addProjectBtn, &QToolButton::clicked, this, &MainWindow::onPmCreateProject);
    headerLayout->addWidget(addProjectBtn);
    
    // Notification badge button
    pmNotificationBadge = new QPushButton("0", projectHeader);
    pmNotificationBadge->setFixedSize(28, 28);
    pmNotificationBadge->setProperty("class", "danger");
    pmNotificationBadge->setToolTip("New files detected - click to view");
    pmNotificationBadge->hide();
    connect(pmNotificationBadge, &QPushButton::clicked, this, &MainWindow::onPmShowNotifications);
    headerLayout->addWidget(pmNotificationBadge);
    
    projectsLayout->addWidget(projectHeader);
    
    // Projects list view
    pmProjectsListView = new QListView(projectsContainer);
    pmProjectsModel = new ProjectsModel(this);
    pmProjectsListView->setModel(pmProjectsModel);
    pmProjectsListView->setContextMenuPolicy(Qt::CustomContextMenu);
    pmProjectsListView->setMaximumHeight(200);
    connect(pmProjectsListView, &QListView::clicked, this, &MainWindow::onPmProjectSelected);
    connect(pmProjectsListView, &QListView::customContextMenuRequested, this, &MainWindow::onPmProjectContextMenu);
    projectsLayout->addWidget(pmProjectsListView);
    
    pmLeftSplitter->addWidget(projectsContainer);
    
    // -- Folder tree section (like FM's folder tree) --
    QWidget *foldersContainer = new QWidget(pmLeftSplitter);
    QVBoxLayout *foldersLayout = new QVBoxLayout(foldersContainer);
    foldersLayout->setContentsMargins(0, 0, 0, 0);
    foldersLayout->setSpacing(0);
    
    QLabel *foldersLabel = new QLabel("Folders", foldersContainer);
    QFont foldersLabelFont = foldersLabel->font();
    foldersLabelFont.setPointSize(13);
    foldersLabelFont.setBold(true);
    foldersLabel->setFont(foldersLabelFont);
    foldersLayout->addWidget(foldersLabel);
    
    pmFolderTree = new QTreeView(foldersContainer);
    pmFoldersModel = new ProjectFoldersModel(this);
    pmFolderTree->setModel(pmFoldersModel);
    pmFolderTree->setHeaderHidden(true);
    pmFolderTree->setExpandsOnDoubleClick(true);
    pmFolderTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(pmFolderTree, &QTreeView::clicked, this, &MainWindow::onPmFolderSelected);
    connect(pmFolderTree, &QTreeView::customContextMenuRequested, this, &MainWindow::onPmFolderContextMenu);
    foldersLayout->addWidget(pmFolderTree);
    
    pmLeftSplitter->addWidget(foldersContainer);
    pmLeftSplitter->setStretchFactor(0, 1);
    pmLeftSplitter->setStretchFactor(1, 2);
    
    leftLayout->addWidget(pmLeftSplitter);
    
    // ===== RIGHT PANEL: Toolbar + Assets + Preview/Info =====
    QWidget *rightPanel = new QWidget(pmSplitter);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    
    // Toolbar (mirrors FM toolbar)
    pmToolbar = new QWidget(rightPanel);
    pmToolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    pmToolbar->setFixedHeight(48);
    
    QHBoxLayout *tb = new QHBoxLayout(pmToolbar);
    tb->setContentsMargins(8, 6, 8, 6);
    tb->setSpacing(8);
    
    auto mkTb = [&](const QIcon &ic, const QString &tip) {
        QToolButton *b = new QToolButton(pmToolbar);
        b->setIcon(ic);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setIconSize(QSize(28, 28));
        return b;
    };
    
    // Navigation buttons
    pmBackButton = mkTb(icoBack(ThemeManager::instance().iconColor()), "Back");
    connect(pmBackButton, &QToolButton::clicked, this, &MainWindow::onPmNavigateBack);
    tb->addWidget(pmBackButton);
    
    pmUpButton = mkTb(icoUp(ThemeManager::instance().iconColor()), "Up");
    connect(pmUpButton, &QToolButton::clicked, this, &MainWindow::onPmNavigateUp);
    tb->addWidget(pmUpButton);
    
    // Separator
    QFrame *sep1 = new QFrame(pmToolbar);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFrameShadow(QFrame::Sunken);
    tb->addWidget(sep1);
    
    // File operation buttons
    pmNewFolderBtn = mkTb(icoFolderNew(ThemeManager::instance().iconColor()), "New Folder");
    connect(pmNewFolderBtn, &QToolButton::clicked, this, &MainWindow::onPmNewFolder);
    tb->addWidget(pmNewFolderBtn);
    
    pmCopyBtn = mkTb(icoCopy(ThemeManager::instance().iconColor()), "Copy");
    connect(pmCopyBtn, &QToolButton::clicked, this, &MainWindow::onPmCopy);
    tb->addWidget(pmCopyBtn);
    
    pmCutBtn = mkTb(icoCut(ThemeManager::instance().iconColor()), "Cut");
    connect(pmCutBtn, &QToolButton::clicked, this, &MainWindow::onPmCut);
    tb->addWidget(pmCutBtn);
    
    pmPasteBtn = mkTb(icoPaste(ThemeManager::instance().iconColor()), "Paste");
    connect(pmPasteBtn, &QToolButton::clicked, this, &MainWindow::onPmPaste);
    tb->addWidget(pmPasteBtn);
    
    pmDeleteBtn = mkTb(icoDelete(ThemeManager::instance().iconColor()), "Delete");
    connect(pmDeleteBtn, &QToolButton::clicked, this, &MainWindow::onPmDelete);
    tb->addWidget(pmDeleteBtn);
    
    pmRenameBtn = mkTb(icoRename(ThemeManager::instance().iconColor()), "Rename");
    connect(pmRenameBtn, &QToolButton::clicked, this, &MainWindow::onPmRename);
    tb->addWidget(pmRenameBtn);
    
    pmOpenExternalBtn = mkTb(icoMediaPlay(ThemeManager::instance().iconColor()), "Open in External App");
    connect(pmOpenExternalBtn, &QToolButton::clicked, this, &MainWindow::onPmOpenExternal);
    tb->addWidget(pmOpenExternalBtn);
    
    // Separator
    QFrame *sep2 = new QFrame(pmToolbar);
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFrameShadow(QFrame::Sunken);
    tb->addWidget(sep2);
    
    // View mode toggle
    pmViewModeButton = mkTb(icoGrid(ThemeManager::instance().iconColor()), "Toggle Grid/List");
    connect(pmViewModeButton, &QToolButton::clicked, this, &MainWindow::onPmViewModeToggled);
    tb->addWidget(pmViewModeButton);
    
    // Thumbnail size slider
    pmSizeLabel = new QLabel("Size:", pmToolbar);
    tb->addWidget(pmSizeLabel);
    
    pmThumbnailSizeSlider = new QSlider(Qt::Horizontal, pmToolbar);
    pmThumbnailSizeSlider->setRange(64, 320);
    pmThumbnailSizeSlider->setValue(180);
    pmThumbnailSizeSlider->setFixedWidth(140);
    pmThumbnailSizeSlider->setToolTip("Adjust thumbnail size");
    connect(pmThumbnailSizeSlider, &QSlider::valueChanged, this, &MainWindow::onPmThumbnailSizeChanged);
    tb->addWidget(pmThumbnailSizeSlider);
    
    tb->addStretch();
    
    // Group sequences toggle
    pmGroupSequencesBtn = new QToolButton(pmToolbar);
    pmGroupSequencesBtn->setIcon(icoGroup(ThemeManager::instance().iconColor()));
    pmGroupSequencesBtn->setToolTip("Group image sequences");
    pmGroupSequencesBtn->setCheckable(true);
    pmGroupSequencesBtn->setChecked(true);  // Default to grouped
    pmGroupSequencesBtn->setAutoRaise(true);
    pmGroupSequencesBtn->setIconSize(QSize(28, 28));
    pmGroupSequencesBtn->setProperty("class", "toggle");
    connect(pmGroupSequencesBtn, &QToolButton::toggled, this, &MainWindow::onPmGroupSequencesToggled);
    tb->addWidget(pmGroupSequencesBtn);
    
    // Show all versions toggle
    pmShowAllVersionsButton = new QToolButton(pmToolbar);
    pmShowAllVersionsButton->setText("All Versions");
    pmShowAllVersionsButton->setCheckable(true);
    pmShowAllVersionsButton->setChecked(false);
    pmShowAllVersionsButton->setToolTip("Show all versions instead of grouping by highest version");
    pmShowAllVersionsButton->setProperty("class", "toggle");
    connect(pmShowAllVersionsButton, &QToolButton::toggled, this, &MainWindow::onPmToggleShowAllVersions);
    tb->addWidget(pmShowAllVersionsButton);
    
    // Preview panel toggle
    pmPreviewToggleButton = mkTb(icoEye(ThemeManager::instance().iconColor()), "Show/Hide Preview Panel");
    pmPreviewToggleButton->setCheckable(true);
    pmPreviewToggleButton->setChecked(true);
    connect(pmPreviewToggleButton, &QToolButton::toggled, this, &MainWindow::onPmTogglePreview);
    tb->addWidget(pmPreviewToggleButton);
    
    // Refresh button
    pmRefreshButton = mkTb(icoRefresh(ThemeManager::instance().iconColor()), "Refresh");
    connect(pmRefreshButton, &QToolButton::clicked, this, &MainWindow::onPmRefresh);
    tb->addWidget(pmRefreshButton);
    
    rightLayout->addWidget(pmToolbar);
    
    // ===== ASSETS VIEW + PREVIEW/INFO =====
    pmRightSplitter = new QSplitter(Qt::Horizontal, rightPanel);
    
    // Assets view stack (grid/list)
    pmViewStack = new QStackedWidget(pmRightSplitter);
    
    // Create models and proxy
    pmAssetsModel = new ProjectAssetsModel(this);
    pmSequenceProxy = new ProjectSequenceGroupingProxyModel(this);
    pmSequenceProxy->setSourceModel(pmAssetsModel);
    pmSequenceProxy->setGroupingEnabled(true); // Default to grouped
    pmItemDelegate = new ProjectItemDelegate(this);
    pmItemDelegate->setSelectedVersions(&pmSelectedVersions);
    
    // Grid view
    pmAssetsGridView = new QListView(pmViewStack);
    pmAssetsGridView->setModel(pmSequenceProxy);  // Use proxy model
    pmItemDelegate->setView(pmAssetsGridView);  // Set view for scrub frame support
    pmAssetsGridView->setItemDelegate(pmItemDelegate);
    pmAssetsGridView->setViewMode(QListView::IconMode);
    pmAssetsGridView->setResizeMode(QListView::Adjust);
    pmAssetsGridView->setSpacing(1);
    // Use uniform item sizes for much better resize performance
    pmAssetsGridView->setUniformItemSizes(true);
    pmAssetsGridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    pmAssetsGridView->setContextMenuPolicy(Qt::CustomContextMenu);
    pmAssetsGridView->setDragEnabled(true);
    // Batched layout mode for better resize performance
    pmAssetsGridView->setLayoutMode(QListView::Batched);
    pmAssetsGridView->setBatchSize(100);
    pmAssetsGridView->viewport()->installEventFilter(this);  // For version badge click detection
    connect(pmAssetsGridView, &QListView::doubleClicked, this, &MainWindow::onPmAssetDoubleClicked);
    connect(pmAssetsGridView, &QListView::customContextMenuRequested, this, &MainWindow::onPmAssetContextMenu);
    connect(pmAssetsGridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onPmAssetSelectionChanged);
    pmViewStack->addWidget(pmAssetsGridView);
    
    // PM Scrub controller for thumbnail scrubbing (like FM and AM)
    pmScrubController = new GridScrubController(
        pmAssetsGridView,
        [this](const QModelIndex& idx) -> QString {
            if (!idx.isValid()) return QString();
            // Map through proxy if needed
            QModelIndex srcIdx = idx;
            if (pmSequenceProxy && idx.model() == pmSequenceProxy) {
                srcIdx = pmSequenceProxy->mapToSource(idx);
            }
            if (!srcIdx.isValid()) return QString();
            // Get file path from model
            QString path = srcIdx.data(ProjectAssetsModel::FilePathRole).toString();
            return path;
        },
        this);
    pmScrubController->setSequenceGroupingEnabled(true);  // Default to enabled
    
    // Table/List view
    pmAssetsTableView = new QTableView(pmViewStack);
    pmAssetsTableView->setModel(pmSequenceProxy);  // Use proxy model
    pmAssetsTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    pmAssetsTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    pmAssetsTableView->setSortingEnabled(true);
    pmAssetsTableView->setAlternatingRowColors(false);
    pmAssetsTableView->setShowGrid(false);
    pmAssetsTableView->verticalHeader()->setVisible(false);
    pmAssetsTableView->verticalHeader()->setDefaultSectionSize(22);
    pmAssetsTableView->setIconSize(QSize(18, 18));
    pmAssetsTableView->horizontalHeader()->setStretchLastSection(true);
    pmAssetsTableView->setContextMenuPolicy(Qt::CustomContextMenu);
    pmAssetsTableView->setDragEnabled(true);
    connect(pmAssetsTableView, &QTableView::doubleClicked, this, &MainWindow::onPmAssetDoubleClicked);
    connect(pmAssetsTableView, &QTableView::customContextMenuRequested, this, &MainWindow::onPmAssetContextMenu);
    connect(pmAssetsTableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onPmAssetSelectionChanged);
    pmViewStack->addWidget(pmAssetsTableView);
    
    pmViewStack->setCurrentIndex(0);
    
    // ===== PREVIEW + INFO PANEL (mirrors FM) =====
    pmPreviewInfoSplitter = new QSplitter(Qt::Vertical, pmRightSplitter);
    
    // Preview panel
    pmPreviewPanel = new QWidget(pmPreviewInfoSplitter);
    pmPreviewPanel->setMinimumWidth(260);
    QVBoxLayout *pv = new QVBoxLayout(pmPreviewPanel);
    pv->setContentsMargins(8, 8, 8, 8);
    pv->setSpacing(6);
    
    QLabel *pvTitle = new QLabel("Preview", pmPreviewPanel);
    QFont pmPvTitleFont = pvTitle->font();
    pmPvTitleFont.setBold(true);
    pvTitle->setFont(pmPvTitleFont);
    pv->addWidget(pvTitle);
    
    // Image view with zoom/pan
    pmImageScene = new QGraphicsScene(pmPreviewPanel);
    pmImageItem = new QGraphicsPixmapItem();
    pmImageScene->addItem(pmImageItem);
    pmImageView = new QGraphicsView(pmImageScene, pmPreviewPanel);
    pmImageView->setDragMode(QGraphicsView::ScrollHandDrag);
    pmImageView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    pmImageView->setMinimumHeight(160);
    pmImageView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    pmImageView->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    pmImageView->setAlignment(Qt::AlignCenter);
    pmImageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    // Video widget (hidden by default)
    pmVideoWidget = new QWidget(pmPreviewPanel);
    pmVideoWidget->setMinimumHeight(160);
    pmVideoWidget->setAutoFillBackground(true);
    {
        QPalette vPal = pmVideoWidget->palette();
        vPal.setColor(QPalette::Window, Qt::black);
        pmVideoWidget->setPalette(vPal);
    }
    pmVideoWidget->hide();
    
    // Preview content container
    QWidget *previewContent = new QWidget(pmPreviewPanel);
    QVBoxLayout *pc = new QVBoxLayout(previewContent);
    pc->setContentsMargins(0, 0, 0, 0);
    pc->setSpacing(6);
    pc->addWidget(pmImageView, 1);
    pc->addWidget(pmVideoWidget, 1);
    pv->addWidget(previewContent);
    
    // Media controls
    QHBoxLayout *mc = new QHBoxLayout();
    mc->setSpacing(4);
    
    pmPrevFrameBtn = new QPushButton(pmPreviewPanel);
    pmPrevFrameBtn->setIcon(icoMediaPrevFrame(ThemeManager::instance().iconColor()));
    pmPrevFrameBtn->setFixedSize(28, 28);
    pmPrevFrameBtn->setToolTip("Previous frame");
    pmPrevFrameBtn->hide();
    mc->addWidget(pmPrevFrameBtn);
    
    pmPlayPauseBtn = new QPushButton(pmPreviewPanel);
    pmPlayPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor()));
    pmPlayPauseBtn->setFixedSize(28, 28);
    pmPlayPauseBtn->setToolTip("Play/Pause");
    pmPlayPauseBtn->hide();
    mc->addWidget(pmPlayPauseBtn);
    
    pmNextFrameBtn = new QPushButton(pmPreviewPanel);
    pmNextFrameBtn->setIcon(icoMediaNextFrame(ThemeManager::instance().iconColor()));
    pmNextFrameBtn->setFixedSize(28, 28);
    pmNextFrameBtn->setToolTip("Next frame");
    pmNextFrameBtn->hide();
    mc->addWidget(pmNextFrameBtn);
    
    pmPositionSlider = new QSlider(Qt::Horizontal, pmPreviewPanel);
    pmPositionSlider->setRange(0, 1000);
    pmPositionSlider->hide();
    mc->addWidget(pmPositionSlider, 1);
    
    pmTimeLabel = new QLabel("00:00 / 00:00", pmPreviewPanel);
    {
        QFont f = pmTimeLabel->font();
        f.setPointSize(9);
        pmTimeLabel->setFont(f);
    }
    pmTimeLabel->hide();
    mc->addWidget(pmTimeLabel);
    
    pmVolumeSlider = new QSlider(Qt::Horizontal, pmPreviewPanel);
    pmVolumeSlider->setRange(0, 100);
    pmVolumeSlider->setValue(100);
    pmVolumeSlider->setFixedWidth(60);
    pmVolumeSlider->hide();
    mc->addWidget(pmVolumeSlider);
    
    pmMuteBtn = new QPushButton(pmPreviewPanel);
    pmMuteBtn->setIcon(icoMediaAudio(ThemeManager::instance().iconColor()));
    pmMuteBtn->setFixedSize(28, 28);
    pmMuteBtn->setToolTip("Mute/Unmute");
    pmMuteBtn->hide();
    mc->addWidget(pmMuteBtn);
    
    pv->addLayout(mc);
    
    // Create GStreamer player for video playback
    pmGStreamerPlayer = new GStreamerPlayer(pmPreviewPanel);
    
    // Sequence timer for image sequence playback
    pmSequenceTimer = new QTimer(pmPreviewPanel);
    connect(pmSequenceTimer, &QTimer::timeout, this, [this]{
        if (!pmIsSequence || pmSequenceFramePaths.isEmpty()) return;
        int next = pmSequenceCurrentIndex + 1;
        if (next >= pmSequenceFramePaths.size()) next = 0; // loop
        loadPmSequenceFrame(next);
    });
    
    // Wire up media controls
    connect(pmPrevFrameBtn, &QPushButton::clicked, this, [this]{ 
        if (pmIsSequence) stepPmSequence(-1); 
        else if (pmGStreamerPlayer) pmGStreamerPlayer->stepBackward();
    });
    connect(pmNextFrameBtn, &QPushButton::clicked, this, [this]{ 
        if (pmIsSequence) stepPmSequence(1); 
        else if (pmGStreamerPlayer) pmGStreamerPlayer->stepForward();
    });
    
    connect(pmPlayPauseBtn, &QPushButton::clicked, this, [this]{
        if (pmIsSequence) {
            if (pmSequencePlaying) pausePmSequence(); else playPmSequence();
            return;
        }
        if (!pmGStreamerPlayer) return;
        if (pmGStreamerPlayer->state() == GStreamerPlayer::PlaybackState::Playing) {
            pmGStreamerPlayer->pause();
            pmPlayPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor()));
        } else {
            pmGStreamerPlayer->play();
            pmPlayPauseBtn->setIcon(icoMediaPause(ThemeManager::instance().iconColor()));
        }
    });
    
    connect(pmGStreamerPlayer, &GStreamerPlayer::positionChanged, this, [this](qint64 pos){
        if (pmIsSequence) return;
        qint64 duration = pmGStreamerPlayer->duration();
        if (pmGStreamerPlayer && duration > 0){
            pmPositionSlider->blockSignals(true);
            pmPositionSlider->setValue(int(pos*1000/duration));
            pmPositionSlider->blockSignals(false);
            pmTimeLabel->setText(QString("%1 / %2")
                .arg(QTime::fromMSecsSinceStartOfDay(int(pos)).toString("mm:ss"))
                .arg(QTime::fromMSecsSinceStartOfDay(int(duration)).toString("mm:ss")));
        }
    });
    
    connect(pmGStreamerPlayer, &GStreamerPlayer::playbackStateChanged, this, [this](GStreamerPlayer::PlaybackState state){
        if (pmPlayPauseBtn) {
            pmPlayPauseBtn->setIcon(state == GStreamerPlayer::PlaybackState::Playing 
                ? icoMediaPause(ThemeManager::instance().iconColor()) 
                : icoMediaPlay(ThemeManager::instance().iconColor()));
        }
    });
    
    connect(pmPositionSlider, &QSlider::sliderMoved, this, [this](int v){
        if (pmIsSequence) {
            loadPmSequenceFrame(v);
            return;
        }
        if (pmGStreamerPlayer) {
            qint64 duration = pmGStreamerPlayer->duration();
            if (duration > 0) {
                pmGStreamerPlayer->seek(v * duration / 1000);
            }
        }
    });
    
    connect(pmVolumeSlider, &QSlider::valueChanged, this, [this](int v){
        if (pmGStreamerPlayer) pmGStreamerPlayer->setVolume(v / 100.0);
    });
    
    connect(pmMuteBtn, &QPushButton::clicked, this, [this]{
        if (!pmGStreamerPlayer) return;
        bool newMuted = !pmGStreamerPlayer->isMuted();
        pmGStreamerPlayer->setMuted(newMuted);
        pmMuteBtn->setIcon(newMuted ? icoMediaMute(ThemeManager::instance().iconColor()) 
                                    : icoMediaAudio(ThemeManager::instance().iconColor()));
    });
    
    // ===== INFO PANEL (mirrors FM info panel) =====
    pmInfoPanel = new QWidget(pmPreviewInfoSplitter);
    pmInfoPanel->setMinimumWidth(260);
    QVBoxLayout *infoPanelLayout = new QVBoxLayout(pmInfoPanel);
    infoPanelLayout->setContentsMargins(0, 0, 0, 0);
    infoPanelLayout->setSpacing(0);
    
    QLabel *infoTitle = new QLabel("File Info", pmInfoPanel);
    {
        QFont f = infoTitle->font();
        f.setBold(true);
        f.setPointSize(11);
        infoTitle->setFont(f);
    }
    infoTitle->setContentsMargins(8, 8, 8, 8);
    infoPanelLayout->addWidget(infoTitle);
    
    // Scrollable area for metadata
    QScrollArea *infoScrollArea = new QScrollArea(pmInfoPanel);
    infoScrollArea->setWidgetResizable(true);
    infoScrollArea->setFrameShape(QFrame::NoFrame);
    
    QWidget *infoScrollWidget = new QWidget();
    QVBoxLayout *infoLayout = new QVBoxLayout(infoScrollWidget);
    infoLayout->setContentsMargins(8, 8, 8, 8);
    infoLayout->setSpacing(4);
    
    pmInfoFileName = new QLabel("No selection", pmInfoPanel);
    {
        QFont f = pmInfoFileName->font();
        f.setBold(true);
        pmInfoFileName->setFont(f);
    }
    pmInfoFileName->setWordWrap(true);
    pmInfoFileName->setContentsMargins(0, 4, 0, 0);
    infoLayout->addWidget(pmInfoFileName);
    
    pmInfoFilePath = new QLabel("", pmInfoPanel);
    {
        QFont f = pmInfoFilePath->font();
        f.setPointSize(8);
        pmInfoFilePath->setFont(f);
    }
    pmInfoFilePath->setWordWrap(true);
    infoLayout->addWidget(pmInfoFilePath);
    
    // Separator
    QFrame *separator1 = new QFrame(pmInfoPanel);
    separator1->setFrameShape(QFrame::HLine);
    separator1->setStyleSheet("background-color: #333;");
    separator1->setFixedHeight(1);
    infoLayout->addWidget(separator1);
    
    pmInfoFileSize = new QLabel("", pmInfoPanel);
    {
        QFont f = pmInfoFileSize->font();
        f.setPointSize(9);
        pmInfoFileSize->setFont(f);
    }
    pmInfoFileSize->setWordWrap(true);
    infoLayout->addWidget(pmInfoFileSize);
    
    pmInfoFileType = new QLabel("", pmInfoPanel);
    {
        QFont f = pmInfoFileType->font();
        f.setPointSize(9);
        pmInfoFileType->setFont(f);
    }
    pmInfoFileType->setWordWrap(true);
    infoLayout->addWidget(pmInfoFileType);
    
    pmInfoDimensions = new QLabel("", pmInfoPanel);
    {
        QFont f = pmInfoDimensions->font();
        f.setPointSize(9);
        pmInfoDimensions->setFont(f);
    }
    pmInfoDimensions->setWordWrap(true);
    infoLayout->addWidget(pmInfoDimensions);
    
    pmInfoCreated = new QLabel("", pmInfoPanel);
    {
        QFont f = pmInfoCreated->font();
        f.setPointSize(9);
        pmInfoCreated->setFont(f);
    }
    pmInfoCreated->setWordWrap(true);
    infoLayout->addWidget(pmInfoCreated);
    
    pmInfoModified = new QLabel("", pmInfoPanel);
    {
        QFont f = pmInfoModified->font();
        f.setPointSize(9);
        pmInfoModified->setFont(f);
    }
    pmInfoModified->setWordWrap(true);
    infoLayout->addWidget(pmInfoModified);
    
    pmInfoPermissions = new QLabel("", pmInfoPanel);
    {
        QFont f = pmInfoPermissions->font();
        f.setPointSize(9);
        pmInfoPermissions->setFont(f);
    }
    pmInfoPermissions->setWordWrap(true);
    infoLayout->addWidget(pmInfoPermissions);
    
    pmInfoVersions = new QLabel("", pmInfoPanel);
    {
        QFont f = pmInfoVersions->font();
        f.setPointSize(9);
        pmInfoVersions->setFont(f);
        QPalette p = pmInfoVersions->palette();
        p.setColor(QPalette::WindowText, QColor("#58a6ff"));
        pmInfoVersions->setPalette(p);
    }
    pmInfoVersions->setWordWrap(true);
    infoLayout->addWidget(pmInfoVersions);
    
    infoLayout->addStretch();
    infoScrollWidget->setLayout(infoLayout);
    infoScrollArea->setWidget(infoScrollWidget);
    infoPanelLayout->addWidget(infoScrollArea);
    
    // Assemble preview/info splitter
    pmPreviewInfoSplitter->addWidget(pmPreviewPanel);
    pmPreviewInfoSplitter->addWidget(pmInfoPanel);
    pmPreviewInfoSplitter->setStretchFactor(0, 2);
    pmPreviewInfoSplitter->setStretchFactor(1, 1);
    
    // Assemble right splitter
    pmRightSplitter->addWidget(pmViewStack);
    pmRightSplitter->addWidget(pmPreviewInfoSplitter);
    pmRightSplitter->setStretchFactor(0, 3);
    pmRightSplitter->setStretchFactor(1, 1);
    
    rightLayout->addWidget(pmRightSplitter);
    
    // Assemble main splitter
    pmSplitter->addWidget(leftPanel);
    pmSplitter->addWidget(rightPanel);
    pmSplitter->setStretchFactor(0, 1);
    pmSplitter->setStretchFactor(1, 4);

    // Persist Project Manager splitter positions with debouncing
    if (pmSplitter) {
        connect(pmSplitter, &QSplitter::splitterMoved, this, [this](int, int){
            m_splitterSaveTimer.start();
        });
    }
    if (pmLeftSplitter) {
        connect(pmLeftSplitter, &QSplitter::splitterMoved, this, [this](int, int){
            m_splitterSaveTimer.start();
        });
    }
    if (pmRightSplitter) {
        connect(pmRightSplitter, &QSplitter::splitterMoved, this, [this](int, int){
            m_splitterSaveTimer.start();
        });
    }
    if (pmPreviewInfoSplitter) {
        connect(pmPreviewInfoSplitter, &QSplitter::splitterMoved, this, [this](int, int){
            m_splitterSaveTimer.start();
        });
    }
    
    // Connect version selection from delegate
    connect(pmItemDelegate, &ProjectItemDelegate::versionSelected, this, &MainWindow::onPmVersionSelected);
    connect(pmItemDelegate, &ProjectItemDelegate::versionDropdownRequested, this, &MainWindow::onPmVersionDropdownRequested);
    
    // Initialize project watcher
    pmWatcher = new ProjectManagerWatcher(this);
    connect(pmWatcher, &ProjectManagerWatcher::newFilesDetected, this, &MainWindow::onPmNewFilesDetected);
    connect(pmWatcher, &ProjectManagerWatcher::filesRemoved, this, &MainWindow::onPmFilesRemoved);
    connect(&ProjectDB::instance(), &ProjectDB::projectFoldersChanged, this, [this](int projectId) {
        if (pmFoldersModel && pmFoldersModel->projectId() == projectId) {
            pmFoldersModel->setProjectId(projectId);
        }
    });
    connect(&ProjectDB::instance(), &ProjectDB::notificationsChanged, this, &MainWindow::updatePmNotificationBadge);
    
    // Start watching all existing projects
    QVector<Project> projects = ProjectDB::instance().listProjects();
    for (const Project& p : projects) {
        if (!p.watchPath.isEmpty()) {
            pmWatcher->watchProject(p.id, p.watchPath);
        }
    }
    
    // Load initial notification count
    updatePmNotificationBadge();
    
    // Create Project Manager shortcuts
    {
        auto sc = new QShortcut(QKeySequence(Qt::Key_Space), projectManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onPmOpenOverlay);
    }
    {
        auto sc = new QShortcut(QKeySequence::Copy, projectManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onPmCopy);
    }
    {
        auto sc = new QShortcut(QKeySequence::Cut, projectManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onPmCut);
    }
    {
        auto sc = new QShortcut(QKeySequence::Paste, projectManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onPmPaste);
    }
    {
        auto sc = new QShortcut(QKeySequence::Delete, projectManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onPmDelete);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::Key_F2), projectManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onPmRename);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::Key_F5), projectManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onPmRefresh);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::Key_Backspace), projectManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onPmNavigateUp);
    }
    
    restoreProjectManagerState();
    LogManager::instance().addLog("[ProjectManager] UI setup complete", "INFO");
}

void MainWindow::onPmProjectSelected(const QModelIndex &index)
{
    if (!index.isValid()) return;
    
    pmCurrentProjectId = index.data(ProjectsModel::IdRole).toInt();
    QString watchPath = index.data(ProjectsModel::WatchPathRole).toString();
    
    LogManager::instance().addLog(QString("[ProjectManager] Selected project %1 with watch path: %2")
        .arg(pmCurrentProjectId).arg(watchPath), "DEBUG");
    
    // Update folder tree model
    if (pmFoldersModel) {
        pmFoldersModel->setProjectId(pmCurrentProjectId);
        qDebug() << "[PM] Folder model row count:" << pmFoldersModel->rowCount();
        // Expand the root folder
        if (pmFolderTree && pmFoldersModel->rowCount() > 0) {
            QModelIndex rootIdx = pmFoldersModel->index(0, 0);
            qDebug() << "[PM] Root index valid:" << rootIdx.isValid() << "data:" << rootIdx.data().toString();
            pmFolderTree->expand(rootIdx);
        }
    }
    
    // Reset folder filter and history
    pmCurrentFolderId = -1;
    pmFolderHistory.clear();
    pmFolderHistoryIndex = -1;
    
    if (pmAssetsModel) {
        pmAssetsModel->setProjectId(pmCurrentProjectId);
        pmAssetsModel->setFolderId(-1);  // Show all assets initially
    }
}

void MainWindow::onPmFolderSelected(const QModelIndex &index)
{
    if (!index.isValid() || !pmFoldersModel) return;
    
    int folderId = pmFoldersModel->folderIdForIndex(index);
    if (folderId <= 0) return;
    
    // Add to history
    if (pmFolderHistoryIndex < pmFolderHistory.size() - 1) {
        pmFolderHistory = pmFolderHistory.mid(0, pmFolderHistoryIndex + 1);
    }
    pmFolderHistory.append(folderId);
    pmFolderHistoryIndex = pmFolderHistory.size() - 1;
    pmCurrentFolderId = folderId;
    
    // Filter assets to selected folder
    if (pmAssetsModel) {
        pmAssetsModel->setFolderId(folderId);
    }
    
    LogManager::instance().addLog(QString("[ProjectManager] Selected folder id %1").arg(folderId), "DEBUG");
}

void MainWindow::onPmAssetSelectionChanged()
{
    // Get selection from the currently active view
    QModelIndexList selected;
    if (pmIsGridMode && pmAssetsGridView && pmAssetsGridView->selectionModel()) {
        selected = pmAssetsGridView->selectionModel()->selectedIndexes();
    } else if (!pmIsGridMode && pmAssetsTableView && pmAssetsTableView->selectionModel()) {
        selected = pmAssetsTableView->selectionModel()->selectedIndexes();
    }
    
    if (selected.isEmpty()) {
        clearPmPreview();
        return;
    }
    
    updatePmPreviewForIndex(selected.first());
    updatePmInfoPanel();
}

void MainWindow::onPmAssetDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;
    
    // Check if it's a folder - navigate into it
    bool isFolder = index.data(ProjectAssetsModel::IsFolderRole).toBool();
    if (isFolder) {
        int subFolderId = index.data(ProjectAssetsModel::SubFolderIdRole).toInt();
        if (subFolderId > 0) {
            // Navigate into the folder
            pmNavigateToFolder(subFolderId);
            
            // Also select this folder in the tree view
            if (pmFolderTree && pmFoldersModel) {
                QModelIndex folderIdx = pmFoldersModel->indexForFolderId(subFolderId);
                if (folderIdx.isValid()) {
                    pmFolderTree->setCurrentIndex(folderIdx);
                    pmFolderTree->expand(folderIdx);
                }
            }
        }
        return;
    }
    
    QString filePath = index.data(ProjectAssetsModel::FilePathRole).toString();
    qint64 assetId = index.data(ProjectAssetsModel::IdRole).toLongLong();
    
    // Check if user has selected a different version for this asset
    if (pmSelectedVersions.contains(assetId)) {
        QString selectedVersion = pmSelectedVersions.value(assetId);
        QString currentVersion = index.data(ProjectAssetsModel::VersionStringRole).toString();
        
        // If selected version differs from the default (highest), find the file path for selected version
        if (selectedVersion != currentVersion && pmAssetsModel) {
            int versionAssetId = pmAssetsModel->getAssetIdForVersion(static_cast<int>(assetId), selectedVersion);
            if (versionAssetId > 0 && versionAssetId != assetId) {
                QString versionPath = ProjectDB::instance().getAssetFilePath(versionAssetId);
                if (!versionPath.isEmpty()) {
                    filePath = versionPath;
                }
            }
        }
    }
    
    // Check if it's a project file that should open in external app
    if (ProjectVersionDetector::isProjectFile(filePath)) {
        QSettings s("AugmentCode", "KAssetManager");
        QString exePath;
        
        if (ProjectVersionDetector::isAfterEffectsFile(filePath)) {
            exePath = s.value("ExternalApps/AfterEffectsPath").toString();
        } else if (ProjectVersionDetector::isNukeFile(filePath)) {
            exePath = s.value("ExternalApps/NukeXPath").toString();
        }
        
        if (!exePath.isEmpty() && QFile::exists(exePath)) {
            QProcess::startDetached(exePath, QStringList() << filePath);
            LogManager::instance().addLog(QString("[ProjectManager] Launching: %1 %2").arg(exePath, filePath), "INFO");
        } else {
            // Fall back to system default
            QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        }
    } else {
        // Open media files in preview overlay
        onPmOpenOverlay();
    }
}

void MainWindow::onPmOpenOverlay()
{
    // Toggle: if overlay is visible, close it
    if ((previewOverlay && previewOverlay->isVisible()) ||
        (imagePreviewOverlay && imagePreviewOverlay->isVisible())) {
        closePreview();
        return;
    }
    
    // Get current selection
    QModelIndex idx;
    if (pmIsGridMode && pmAssetsGridView) {
        idx = pmAssetsGridView->currentIndex();
    } else if (pmAssetsTableView) {
        idx = pmAssetsTableView->currentIndex();
    }
    
    if (!idx.isValid()) return;
    
    QString filePath = idx.data(ProjectAssetsModel::FilePathRole).toString();
    bool isSequence = idx.data(ProjectAssetsModel::IsSequenceRole).toBool();
    
    if (filePath.isEmpty()) return;
    
    if (isSequence) {
        if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
            imagePreviewOverlay->stopPlayback();
            imagePreviewOverlay->hide();
        }
        if (!previewOverlay) {
            previewOverlay = new PreviewOverlay(this);
            previewOverlay->setGeometry(geometry());
            connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
            connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changePmPreview);
        } else {
            previewOverlay->stopPlayback();
        }
        QString pattern = idx.data(ProjectAssetsModel::SequencePatternRole).toString();
        int startFrame = idx.data(ProjectAssetsModel::SequenceStartFrameRole).toInt();
        int endFrame = idx.data(ProjectAssetsModel::SequenceEndFrameRole).toInt();
        
        // Use reconstructSequenceFramePaths which uses filePath (first frame) for directory
        // The pattern is just a filename like "render.####.exr", not a full path
        QStringList frames = reconstructSequenceFramePaths(filePath, startFrame, endFrame);
        
        if (!frames.isEmpty()) {
            QString seqName = pattern.isEmpty() ? QFileInfo(filePath).fileName() : pattern;
            previewOverlay->showSequence(frames, seqName, startFrame, endFrame);
            return;
        }
    }
    
    // Single file preview
    QFileInfo info(filePath);
    if (info.exists()) {
        const bool isImage = isImageFile(info.suffix());
        const bool isVideo = isVideoFile(info.suffix());
        if (isImage && !isVideo) {
            if (previewOverlay && previewOverlay->isVisible()) {
                previewOverlay->stopPlayback();
                previewOverlay->hide();
            }
            if (!imagePreviewOverlay) {
                imagePreviewOverlay = new ImagePreviewOverlay(this);
                imagePreviewOverlay->setGeometry(geometry());
                connect(imagePreviewOverlay, &ImagePreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(imagePreviewOverlay, &ImagePreviewOverlay::navigateRequested, this, &MainWindow::changePmPreview);
            } else {
                imagePreviewOverlay->stopPlayback();
            }
            imagePreviewOverlay->showImage(filePath, info.fileName(), info.suffix());
        } else {
            if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
                imagePreviewOverlay->stopPlayback();
                imagePreviewOverlay->hide();
            }
            if (!previewOverlay) {
                previewOverlay = new PreviewOverlay(this);
                previewOverlay->setGeometry(geometry());
                connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changePmPreview);
            } else {
                previewOverlay->stopPlayback();
            }
            previewOverlay->showAsset(filePath, info.fileName(), info.suffix());
        }
    }
}

void MainWindow::onPmAssetContextMenu(const QPoint &pos)
{
    QMenu menu;
    
    QModelIndex index;
    QAbstractItemView *view = pmIsGridMode ? static_cast<QAbstractItemView*>(pmAssetsGridView) : static_cast<QAbstractItemView*>(pmAssetsTableView);
    if (!view) return;
    
    index = view->indexAt(pos);
    
    // Get all selected items
    QModelIndexList selectedIndexes = view->selectionModel()->selectedIndexes();
    QStringList selectedPaths;
    QList<int> selectedAssetIds;
    
    for (const QModelIndex &idx : selectedIndexes) {
        QString path = idx.data(ProjectAssetsModel::FilePathRole).toString();
        int assetId = idx.data(ProjectAssetsModel::IdRole).toInt();
        if (!path.isEmpty() && !selectedPaths.contains(path)) {
            selectedPaths.append(path);
            selectedAssetIds.append(assetId);
        }
    }
    
    bool hasSel = !selectedPaths.isEmpty();
    int selCount = selectedPaths.size();
    
    // Standard actions like FM
    QAction *openA = menu.addAction("Open");
    openA->setEnabled(selCount == 1);
    
    if (index.isValid()) {
        QString filePath = index.data(ProjectAssetsModel::FilePathRole).toString();
        if (ProjectVersionDetector::isProjectFile(filePath)) {
            QString appName = ProjectVersionDetector::isAfterEffectsFile(filePath) ? "After Effects" : "NukeX";
            QAction *openInAppA = menu.addAction(QString("Open in %1").arg(appName));
            connect(openInAppA, &QAction::triggered, this, [this, index]() {
                onPmAssetDoubleClicked(index);
            });
        }
    }
    
    menu.addSeparator();
    
    QAction *copyA = menu.addAction("Copy");
    copyA->setShortcut(QKeySequence::Copy);
    copyA->setEnabled(hasSel);
    
    QAction *cutA = menu.addAction("Cut");
    cutA->setShortcut(QKeySequence::Cut);
    cutA->setEnabled(hasSel);
    
    QAction *pasteA = menu.addAction("Paste");
    pasteA->setShortcut(QKeySequence::Paste);
    pasteA->setEnabled(!pmClipboard.isEmpty());
    
    menu.addSeparator();
    
    QAction *renameA = menu.addAction("Rename");
    renameA->setShortcut(QKeySequence(Qt::Key_F2));
    renameA->setEnabled(selCount == 1);
    
    QAction *deleteA = menu.addAction("Delete");
    deleteA->setShortcut(QKeySequence::Delete);
    deleteA->setEnabled(hasSel);
    
    menu.addSeparator();
    
    QAction *showInExplorerA = menu.addAction("Show in Explorer");
    showInExplorerA->setEnabled(selCount == 1);
    
    QAction *propertiesA = menu.addAction("Properties");
    propertiesA->setEnabled(selCount == 1);
    
    QAction *generateThumbA = nullptr;
    if (index.isValid()) {
        generateThumbA = menu.addAction("Generate Thumbnail");
    }
    
    QAction *chosen = menu.exec(view->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    
    if (chosen == openA && selCount == 1) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(selectedPaths.first()));
    } else if (chosen == copyA) {
        pmClipboard = selectedPaths;
        pmClipboardCutMode = false;
        statusBar()->showMessage(QString("Copied %1 file(s)").arg(selCount), 2000);
    } else if (chosen == cutA) {
        pmClipboard = selectedPaths;
        pmClipboardCutMode = true;
        statusBar()->showMessage(QString("Cut %1 file(s)").arg(selCount), 2000);
    } else if (chosen == pasteA) {
        onPmPaste();
    } else if (chosen == renameA && selCount == 1) {
        onPmRename();
    } else if (chosen == deleteA) {
        onPmDelete();
    } else if (chosen == showInExplorerA && selCount == 1) {
        QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(selectedPaths.first())});
    } else if (chosen == propertiesA && selCount == 1) {
        #ifdef Q_OS_WIN
        std::wstring wpath = selectedPaths.first().toStdWString();
        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.lpVerb = L"properties";
        sei.lpFile = wpath.c_str();
        sei.nShow = SW_SHOW;
        sei.fMask = SEE_MASK_INVOKEIDLIST;
        ShellExecuteExW(&sei);
        #endif
    } else if (chosen == generateThumbA && index.isValid()) {
        QString filePath = index.data(ProjectAssetsModel::FilePathRole).toString();
        bool isSequence = index.data(ProjectAssetsModel::IsSequenceRole).toBool();
        QString fileType = index.data(ProjectAssetsModel::FileTypeRole).toString().toLower();

        if (!filePath.isEmpty()) {
            QVector<ThumbnailGeneratorWorker::Task> tasks;
            ThumbnailGeneratorWorker::Task task;
            task.filePath = filePath;

            static const QSet<QString> videoExts = {
                "mov", "qt", "mp4", "m4v", "mxf", "avi", "mkv", "webm",
                "mpg", "mpeg", "m2v", "m2ts", "mts", "wmv", "asf", "flv"
            };

            if (isSequence) {
                task.isSequence = true;
                task.isVideo = false;

                const int startFrame = index.data(ProjectAssetsModel::SequenceStartFrameRole).toInt();
                const int endFrame = index.data(ProjectAssetsModel::SequenceEndFrameRole).toInt();
                QStringList framePaths = reconstructSequenceFramePaths(filePath, startFrame, endFrame);
                task.sequenceFrames = framePaths;
            } else if (videoExts.contains(fileType)) {
                task.isVideo = true;
                task.isSequence = false;
            } else {
                task.isVideo = false;
                task.isSequence = false;
            }

            tasks.append(task);

            auto *worker = new ThumbnailGeneratorWorker();
            auto *thread = new QThread(this);

            worker->moveToThread(thread);
            connect(thread, &QThread::finished, worker, &QObject::deleteLater);
            connect(this, &QObject::destroyed, thread, &QThread::quit);

            connect(worker, &ThumbnailGeneratorWorker::queueFinished, this, [this, thread]() {
                if (pmAssetsGridView && pmAssetsGridView->viewport()) pmAssetsGridView->viewport()->update();
                if (pmAssetsTableView && pmAssetsTableView->viewport()) pmAssetsTableView->viewport()->update();
                statusBar()->showMessage("Thumbnail generation completed", 3000);
                thread->quit();
            });

            connect(worker, &ThumbnailGeneratorWorker::logLine, this, [](const QString &line) {
                qDebug() << "[ThumbnailGeneratorWorker]" << line;
            });

            thread->start(QThread::LowPriority);

            ThumbnailCacheManager &cache = ThumbnailCacheManager::instance();
            const QSize thumbnailSize = cache.getThumbnailSize();

            QMetaObject::invokeMethod(worker, [worker, tasks, thumbnailSize]() {
                worker->start(tasks, thumbnailSize);
            }, Qt::QueuedConnection);

            statusBar()->showMessage("Generating thumbnail...", 2000);
        }
    }
}

void MainWindow::onPmProjectContextMenu(const QPoint &pos)
{
    QMenu menu;
    
    QModelIndex index = pmProjectsListView->indexAt(pos);
    
    if (index.isValid()) {
        int projectId = index.data(ProjectsModel::IdRole).toInt();
        
        menu.addAction("Rename Project", this, &MainWindow::onPmRenameProject);
        menu.addAction("Change Watch Folder...", this, &MainWindow::onPmAddWatchFolder);
        menu.addSeparator();
        
        // Generate Thumbnails submenu
        QMenu *thumbMenu = menu.addMenu("Generate Thumbnails");
        QAction *actPrefetch = thumbMenu->addAction("Prefetch (skip cached)");
        QAction *actRefresh = thumbMenu->addAction("Refresh (regenerate all)");
        
        connect(actPrefetch, &QAction::triggered, this, [this, projectId]() {
            generateProjectThumbnails(projectId, false);
        });
        connect(actRefresh, &QAction::triggered, this, [this, projectId]() {
            generateProjectThumbnails(projectId, true);
        });
        
        menu.addSeparator();
        menu.addAction("Re-sync Asset Folders", this, [this, index]() {
            int projectId = index.data(ProjectsModel::IdRole).toInt();
            if (projectId <= 0) return;
            
            statusBar()->showMessage("Re-syncing asset folders (background)...");
            
            // Run resync on background thread to avoid UI freeze
            QPointer<MainWindow> self = this;
            QtConcurrent::run([projectId]() {
                return ProjectDB::instance().resyncAssetFolders(projectId);
            }).then(this, [self, projectId](int fixed) {
                if (!self) return;
                self->statusBar()->showMessage(QString("Re-sync complete: %1 assets updated").arg(fixed), 5000);
                
                if (fixed > 0 && self->pmAssetsModel && self->pmAssetsModel->projectId() == projectId) {
                    self->pmAssetsModel->reload();
                }
                if (self->pmFoldersModel) {
                    self->pmFoldersModel->setProjectId(self->pmFoldersModel->projectId()); // Force refresh
                }
            });
        });
        menu.addSeparator();
        menu.addAction("Delete Project", this, &MainWindow::onPmDeleteProject);
    } else {
        menu.addAction("New Project...", this, &MainWindow::onPmCreateProject);
    }
    
    menu.exec(pmProjectsListView->viewport()->mapToGlobal(pos));
}

void MainWindow::onPmCreateProject()
{
    // First ask for folder - then derive name from it
    QString watchPath = QFileDialog::getExistingDirectory(this, "Select Project Folder");
    if (watchPath.isEmpty()) return;
    
    // Default name from folder
    QString defaultName = QDir(watchPath).dirName();
    
    bool ok;
    QString name = QInputDialog::getText(this, "New Project", "Project name:", QLineEdit::Normal, defaultName, &ok);
    if (!ok || name.isEmpty()) return;
    
    pmImportToProject(name, watchPath);
}

void MainWindow::pmImportToProject(const QString& name, const QString& watchPath)
{
    if (!pmProjectsModel) return;
    
    int projectId = pmProjectsModel->createProject(name, watchPath);
    if (projectId <= 0) {
        LogManager::instance().addLog("[ProjectManager] Failed to create project: " + name, "ERROR");
        QMessageBox::warning(this, "Error", "Failed to create project. Check logs for details.");
        return;
    }
    
    LogManager::instance().addLog(QString("[ProjectManager] Created project '%1' (id=%2) at %3")
        .arg(name).arg(projectId).arg(watchPath), "INFO");
    
    // Start watching the new project
    if (pmWatcher) {
        pmWatcher->watchProject(projectId, watchPath);
    }
    
    // Get the root folder for this project to use as import target
    int folderId = ProjectDB::instance().getProjectRootFolderId(projectId);
    if (folderId <= 0) {
        LogManager::instance().addLog("[ProjectManager] Failed to get root folder for project", "ERROR");
        return;
    }
    
    // Create importer if needed - uses same proven Importer as Asset Manager
    if (!pmImporter) {
        pmImporter = new Importer(this, &ProjectDB::instance());
        pmImporter->setSequenceDetectionEnabled(true);  // Enable sequence detection like Asset Manager
        connect(pmImporter, &Importer::progressChanged, this, &MainWindow::onPmImportProgress);
        connect(pmImporter, &Importer::currentFileChanged, this, &MainWindow::onPmImportFileChanged);
        connect(pmImporter, &Importer::currentFolderChanged, this, &MainWindow::onPmImportFolderChanged);
        connect(pmImporter, &Importer::importFinished, this, &MainWindow::onPmImportFinished);
    }
    
    // Show progress dialog
    if (!pmImportProgressDialog) {
        pmImportProgressDialog = new ImportProgressDialog(this);
    }
    pmImportProgressDialog->setWindowTitle("Importing Project Assets");
    pmImportProgressDialog->show();
    pmImportProgressDialog->raise();
    pmImportProgressDialog->activateWindow();
    
    // Store project ID for refresh after import
    pmPendingImportProjectId = projectId;
    
    LogManager::instance().addLog(QString("[ProjectManager] Starting import from %1 into folder id %2").arg(watchPath).arg(folderId), "INFO");
    
    // Use Importer::importFolderContents - same approach as Asset Manager
    // Progress updates are throttled (every 50ms) to keep UI responsive
    pmImporter->importFolderContents(watchPath, folderId);
    
    // Import is synchronous but with throttled UI updates - call finished handler
    onPmImportFinished();
}

void MainWindow::onPmImportProgress(int current, int total)
{
    if (pmImportProgressDialog) {
        pmImportProgressDialog->setProgress(current, total);
    }
}

void MainWindow::onPmImportFileChanged(const QString& fileName)
{
    if (pmImportProgressDialog) {
        pmImportProgressDialog->setCurrentFile(fileName);
    }
}

void MainWindow::onPmImportFolderChanged(const QString& folderName)
{
    if (pmImportProgressDialog) {
        pmImportProgressDialog->setCurrentFolder(folderName);
    }
}

void MainWindow::onPmImportFinished()
{
    LogManager::instance().addLog("[ProjectManager] Import finished", "INFO");
    
    if (pmImportProgressDialog) {
        pmImportProgressDialog->setComplete();
    }
    
    // Refresh views with the new project data
    int projectId = pmPendingImportProjectId;
    pmPendingImportProjectId = -1;
    
    if (projectId > 0) {
        // Refresh the assets view
        if (pmAssetsModel) {
            pmAssetsModel->setProjectId(projectId);
        }
        // Refresh folder tree
        if (pmFoldersModel) {
            pmFoldersModel->setProjectId(projectId);
        }
        
        statusBar()->showMessage("Import completed successfully", 5000);
    }
}

void MainWindow::onPmRenameProject()
{
    QModelIndex index = pmProjectsListView->currentIndex();
    if (!index.isValid()) return;
    
    QString oldName = index.data(ProjectsModel::NameRole).toString();
    bool ok;
    QString newName = QInputDialog::getText(this, "Rename Project", "New name:", QLineEdit::Normal, oldName, &ok);
    if (!ok || newName.isEmpty() || newName == oldName) return;
    
    int projectId = index.data(ProjectsModel::IdRole).toInt();
    if (pmProjectsModel) {
        pmProjectsModel->renameProject(projectId, newName);
    }
}

void MainWindow::onPmDeleteProject()
{
    QModelIndex index = pmProjectsListView->currentIndex();
    if (!index.isValid()) return;
    
    QString name = index.data(ProjectsModel::NameRole).toString();
    int projectId = index.data(ProjectsModel::IdRole).toInt();
    
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, "Delete Project",
        QString("Are you sure you want to delete project '%1'?\n\nThis will not delete any files, only the project entry.").arg(name),
        QMessageBox::Yes | QMessageBox::No
    );
    
    if (reply == QMessageBox::Yes && pmProjectsModel) {
        // Stop watching before deleting
        if (pmWatcher) {
            pmWatcher->unwatchProject(projectId);
        }
        
        pmProjectsModel->deleteProject(projectId);
        pmCurrentProjectId = -1;
        if (pmAssetsModel) {
            pmAssetsModel->setProjectId(-1);
        }
    }
}

void MainWindow::onPmAddWatchFolder()
{
    QModelIndex index = pmProjectsListView->currentIndex();
    if (!index.isValid()) return;
    
    QString currentPath = index.data(ProjectsModel::WatchPathRole).toString();
    QString newPath = QFileDialog::getExistingDirectory(this, "Select Watch Folder", currentPath);
    if (newPath.isEmpty()) return;
    
    int projectId = index.data(ProjectsModel::IdRole).toInt();
    ProjectDB::instance().updateProjectWatchPath(projectId, newPath);
    
    // Update watcher with new path
    if (pmWatcher) {
        pmWatcher->watchProject(projectId, newPath);
    }
    
    if (pmProjectsModel) {
        pmProjectsModel->refresh();
    }
}

void MainWindow::onPmViewModeToggled()
{
    pmIsGridMode = !pmIsGridMode;
    if (pmViewStack) {
        pmViewStack->setCurrentIndex(pmIsGridMode ? 0 : 1);
    }
    if (pmViewModeButton) {
        pmViewModeButton->setIcon(pmIsGridMode ? icoList(ThemeManager::instance().iconColor()) : icoGrid(ThemeManager::instance().iconColor()));
    }
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("ProjectManager/ViewMode", pmIsGridMode);
}

void MainWindow::onPmThumbnailSizeChanged(int size)
{
    if (pmItemDelegate) {
        pmItemDelegate->setThumbnailSize(size);
    }
    if (pmAssetsGridView) {
        pmAssetsGridView->setGridSize(QSize(size + 24, size + 45));
        pmAssetsGridView->viewport()->update();
    }
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("ProjectManager/ThumbnailSize", size);
}

void MainWindow::onPmToggleShowAllVersions(bool checked)
{
    if (pmAssetsModel) {
        pmAssetsModel->setShowAllVersions(checked);
    }
}

void MainWindow::onPmVersionSelected(qint64 assetId, const QString &versionString)
{
    // Store the selected version for this asset
    pmSelectedVersions[assetId] = versionString;
    LogManager::instance().addLog(QString("[ProjectManager] Version selected for asset %1: %2")
        .arg(assetId).arg(versionString), "DEBUG");
    
    // Update the preview if this asset is currently selected
    QModelIndex currentIdx;
    if (pmIsGridMode && pmAssetsGridView) {
        currentIdx = pmAssetsGridView->currentIndex();
    } else if (pmAssetsTableView) {
        currentIdx = pmAssetsTableView->currentIndex();
    }
    
    if (currentIdx.isValid()) {
        qint64 currentAssetId = currentIdx.data(ProjectAssetsModel::IdRole).toLongLong();
        if (currentAssetId == assetId) {
            // Refresh the preview with the new version
            updatePmPreviewForIndex(currentIdx);
        }
    }
    
    // Update the view to reflect the version change
    if (pmAssetsGridView) {
        pmAssetsGridView->viewport()->update();
    }
    if (pmAssetsTableView) {
        pmAssetsTableView->viewport()->update();
    }
}

void MainWindow::onPmVersionDropdownRequested(const QModelIndex &index, const QPoint &globalPos)
{
    if (!index.isValid()) return;
    
    // Get version info from the model
    QStringList versions = index.data(ProjectAssetsModel::VersionListRole).toStringList();
    QList<int> versionAssetIds = index.data(ProjectAssetsModel::VersionAssetIdsRole).value<QList<int>>();
    QString currentVersion = index.data(ProjectAssetsModel::VersionStringRole).toString();
    qint64 assetId = index.data(ProjectAssetsModel::IdRole).toLongLong();
    
    if (versions.isEmpty()) return;
    
    // Check if we have a previously selected version
    QString selectedVersion = pmSelectedVersions.value(assetId, currentVersion);
    
    // Create popup menu
    QMenu menu(this);
    
    QActionGroup *actionGroup = new QActionGroup(&menu);
    actionGroup->setExclusive(true);
    
    for (int i = 0; i < versions.size(); ++i) {
        const QString &version = versions[i];
        QAction *action = menu.addAction(version);
        action->setCheckable(true);
        action->setChecked(version == selectedVersion);
        actionGroup->addAction(action);
        
        // Store version asset ID as action data if available
        if (i < versionAssetIds.size()) {
            action->setData(versionAssetIds[i]);
        }
    }
    
    // Show menu and handle selection
    QAction *selected = menu.exec(globalPos);
    if (selected) {
        QString chosenVersion = selected->text();
        if (chosenVersion != selectedVersion) {
            onPmVersionSelected(assetId, chosenVersion);
        }
    }
}

void MainWindow::onPmRefresh()
{
    if (pmProjectsModel) {
        pmProjectsModel->refresh();
    }
    if (pmAssetsModel && pmCurrentProjectId > 0) {
        pmAssetsModel->setProjectId(pmCurrentProjectId);
    }
}

void MainWindow::generateProjectThumbnails(int projectId, bool forceRefresh)
{
    if (projectId <= 0) {
        statusBar()->showMessage("No project selected", 2000);
        return;
    }
    
    // Get all assets in the project
    QList<int> assetIds = ProjectDB::instance().getAssetIdsInProject(projectId);
    
    if (assetIds.isEmpty()) {
        statusBar()->showMessage("No assets in project", 2000);
        return;
    }
    
    // Build task list for ThumbnailGeneratorWorker
    ThumbnailCacheManager &cacheManager = ThumbnailCacheManager::instance();
    const QSize thumbnailSize = cacheManager.getThumbnailSize();
    
    static const QSet<QString> videoExts = {
        "mov", "qt", "mp4", "m4v", "mxf", "avi", "mkv", "webm",
        "mpg", "mpeg", "m2v", "m2ts", "mts", "wmv", "asf", "flv"
    };
    
    QVector<ThumbnailGeneratorWorker::Task> tasks;
    int skipped = 0;
    
    for (int assetId : assetIds) {
        QString filePath = ProjectDB::instance().getAssetFilePath(assetId);
        if (filePath.isEmpty()) continue;
        
        // Check if cached (skip if not forcing refresh)
        if (!forceRefresh && cacheManager.isCached(filePath, thumbnailSize)) {
            ++skipped;
            continue;
        }
        
        // Clear cache if refreshing
        if (forceRefresh) {
            cacheManager.clearCacheForFile(filePath);
            LivePreviewManager::instance().invalidate(filePath);
        }
        
        ThumbnailGeneratorWorker::Task task;
        task.filePath = filePath;
        
        // Determine if it's a video from extension
        QString ext = QFileInfo(filePath).suffix().toLower();
        if (videoExts.contains(ext)) {
            task.isVideo = true;
            task.isSequence = false;
        } else {
            // For images, we'll treat them as single files
            // Sequences are harder to detect here without model data
            task.isVideo = false;
            task.isSequence = false;
        }
        
        tasks.append(task);
    }
    
    if (tasks.isEmpty()) {
        statusBar()->showMessage(QString("All %1 thumbnails already cached").arg(skipped), 2000);
        return;
    }
    
    // Create worker and thread
    auto *worker = new ThumbnailGeneratorWorker();
    auto *thread = new QThread(this);
    
    worker->moveToThread(thread);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &QObject::destroyed, thread, &QThread::quit);
    
    QString action = forceRefresh ? "Refreshing" : "Generating";
    int totalTasks = tasks.size();
    
    connect(worker, &ThumbnailGeneratorWorker::queueStarted, this, [this, action, totalTasks](int) {
        statusBar()->showMessage(QString("%1 thumbnails for %2 assets...").arg(action).arg(totalTasks));
    });
    
    connect(worker, &ThumbnailGeneratorWorker::queueFinished, this, [this, thread, action, totalTasks](bool) {
        if (pmAssetsGridView && pmAssetsGridView->viewport()) pmAssetsGridView->viewport()->update();
        if (pmAssetsTableView && pmAssetsTableView->viewport()) pmAssetsTableView->viewport()->update();
        statusBar()->showMessage(QString("%1 complete: %2 thumbnails processed").arg(action).arg(totalTasks), 5000);
        thread->quit();
    });
    
    connect(worker, &ThumbnailGeneratorWorker::logLine, this, [](const QString &line) {
        qDebug() << "[ThumbnailGeneratorWorker]" << line;
    });
    
    thread->start(QThread::LowPriority);
    
    QMetaObject::invokeMethod(worker, [worker, tasks, thumbnailSize]() {
        worker->start(tasks, thumbnailSize);
    }, Qt::QueuedConnection);
    
    statusBar()->showMessage(QString("%1 %2 thumbnails (skipped %3 cached)...").arg(action).arg(tasks.size()).arg(skipped), 3000);
}

void MainWindow::onPmMarkNotificationsRead()
{
    if (pmCurrentProjectId > 0) {
        ProjectDB::instance().markNotificationsRead(pmCurrentProjectId);
        updatePmNotificationBadge();
    }
}

void MainWindow::onPmShowNotifications()
{
    int scopedProjectId = pmCurrentProjectId > 0 ? pmCurrentProjectId : -1;
    QVector<ProjectNotification> notifications = ProjectDB::instance().getUnacknowledgedNotifications(scopedProjectId);

    if (notifications.isEmpty()) {
        QMessageBox::information(this, "Notifications", "No new files detected.");
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Project Notifications");
    dialog.resize(800, 420);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);

    QLabel *hint = new QLabel("Click 'Go To Asset' to jump directly to the file inside the Project Manager.", &dialog);
    layout->addWidget(hint);

    QTableWidget *table = new QTableWidget(notifications.size(), 4, &dialog);
    table->setHorizontalHeaderLabels({"Asset", "Location", "Detected", ""});
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table->verticalHeader()->setVisible(false);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    for (int row = 0; row < notifications.size(); ++row) {
        const ProjectNotification notif = notifications.at(row);
        QFileInfo info(notif.filePath);

        auto *nameItem = new QTableWidgetItem(info.fileName());
        table->setItem(row, 0, nameItem);

        auto *pathItem = new QTableWidgetItem(info.absolutePath());
        table->setItem(row, 1, pathItem);

        const QDateTime detected = QDateTime::fromString(notif.detectedAt, Qt::ISODate);
        auto *detectedItem = new QTableWidgetItem(detected.isValid() ? detected.toString("yyyy-MM-dd hh:mm:ss") : notif.detectedAt);
        table->setItem(row, 2, detectedItem);

        QPushButton *openBtn = new QPushButton("Go To Asset", table);
        openBtn->setAutoDefault(false);
        table->setCellWidget(row, 3, openBtn);

        connect(openBtn, &QPushButton::clicked, this, [this, notif, openBtn, nameItem]() {
            navigateToProjectAsset(notif.projectId, notif.assetId, notif.filePath);
            ProjectDB::instance().acknowledgeNotification(notif.id);
            updatePmNotificationBadge();
            if (nameItem) {
                nameItem->setText(nameItem->text() + " ✓");
                nameItem->setForeground(QBrush(Qt::gray));
            }
            openBtn->setEnabled(false);
        });
    }

    layout->addWidget(table, 1);

    QHBoxLayout *buttonsLayout = new QHBoxLayout();
    QPushButton *markAllBtn = new QPushButton("Mark All Read", &dialog);
    QPushButton *closeBtn = new QPushButton("Close", &dialog);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(markAllBtn);
    buttonsLayout->addWidget(closeBtn);
    layout->addLayout(buttonsLayout);

    connect(markAllBtn, &QPushButton::clicked, this, [this, scopedProjectId, &dialog]() {
        ProjectDB::instance().acknowledgeAllNotifications(scopedProjectId);
        updatePmNotificationBadge();
        dialog.accept();
    });
    connect(closeBtn, &QPushButton::clicked, &dialog, &QDialog::reject);

    dialog.exec();
}

void MainWindow::onPmNewFilesDetected(int projectId, const QStringList &newFiles)
{
    Q_UNUSED(projectId);
    const int total = newFiles.size();
    const int sampleCount = qMin(total, 10);
    const QStringList sample = newFiles.mid(0, sampleCount);
    const QString suffix = (total > sampleCount)
        ? QString(" (+%1 more)").arg(total - sampleCount)
        : QString();
    LogManager::instance().addLog(
        QString("[ProjectManager] New files detected (%1): %2%3")
            .arg(total).arg(sample.join(", ")).arg(suffix),
        "INFO");
    
    // Update notification badge
    updatePmNotificationBadge();
    
    // Refresh the current view if viewing this project
    if (projectId == pmCurrentProjectId && pmAssetsModel) {
        pmAssetsModel->setProjectId(pmCurrentProjectId);
    }
}

void MainWindow::onPmFilesRemoved(int projectId, const QStringList &removedFiles)
{
    const int total = removedFiles.size();
    const int sampleCount = qMin(total, 10);
    const QStringList sample = removedFiles.mid(0, sampleCount);
    const QString suffix = (total > sampleCount)
        ? QString(" (+%1 more)").arg(total - sampleCount)
        : QString();
    LogManager::instance().addLog(
        QString("[ProjectManager] Files removed (%1): %2%3")
            .arg(total).arg(sample.join(", ")).arg(suffix),
        "INFO");
    
    // Remove assets from database
    int removed = ProjectDB::instance().removeAssetsByPath(removedFiles);
    
    if (removed > 0) {
        statusBar()->showMessage(QString("%1 file(s) removed from project").arg(removed), 3000);
        
        // Refresh the current view if viewing this project
        if (projectId == pmCurrentProjectId && pmAssetsModel) {
            pmAssetsModel->setProjectId(pmCurrentProjectId);
        }
    }
}

void MainWindow::updatePmNotificationBadge()
{
    pmUnreadNotificationCount = ProjectDB::instance().getUnreadNotificationCount();
    
    if (pmNotificationBadge) {
        if (pmUnreadNotificationCount > 0) {
            QString text = pmUnreadNotificationCount > 99 ? "99+" : QString::number(pmUnreadNotificationCount);
            pmNotificationBadge->setText(text);
            pmNotificationBadge->show();
        } else {
            pmNotificationBadge->hide();
        }
    }
}

void MainWindow::updatePmInfoPanel()
{
    if (!pmInfoFileName) return;
    
    // Get selection from the currently active view
    QModelIndexList selected;
    if (pmIsGridMode && pmAssetsGridView && pmAssetsGridView->selectionModel()) {
        selected = pmAssetsGridView->selectionModel()->selectedIndexes();
    } else if (!pmIsGridMode && pmAssetsTableView && pmAssetsTableView->selectionModel()) {
        selected = pmAssetsTableView->selectionModel()->selectedIndexes();
    }
    
    if (selected.isEmpty()) {
        pmInfoFileName->setText("No selection");
        if (pmInfoFilePath) pmInfoFilePath->clear();
        if (pmInfoFileSize) pmInfoFileSize->clear();
        if (pmInfoFileType) pmInfoFileType->clear();
        if (pmInfoDimensions) pmInfoDimensions->clear();
        if (pmInfoCreated) pmInfoCreated->clear();
        if (pmInfoModified) pmInfoModified->clear();
        if (pmInfoPermissions) pmInfoPermissions->clear();
        if (pmInfoVersions) pmInfoVersions->clear();
        return;
    }
    
    QModelIndex index = selected.first();
    
    // Check if it's a folder
    bool isFolder = index.data(ProjectAssetsModel::IsFolderRole).toBool();
    if (isFolder) {
        QString folderName = index.data(ProjectAssetsModel::FileNameRole).toString();
        pmInfoFileName->setText(folderName);
        if (pmInfoFilePath) pmInfoFilePath->setText("Folder");
        if (pmInfoFileSize) pmInfoFileSize->clear();
        if (pmInfoFileType) pmInfoFileType->setText("Type: Folder");
        if (pmInfoDimensions) pmInfoDimensions->clear();
        if (pmInfoCreated) pmInfoCreated->clear();
        if (pmInfoModified) pmInfoModified->clear();
        if (pmInfoPermissions) pmInfoPermissions->clear();
        if (pmInfoVersions) pmInfoVersions->clear();
        return;
    }
    
    QString fileName = index.data(ProjectAssetsModel::FileNameRole).toString();
    QString filePath = index.data(ProjectAssetsModel::FilePathRole).toString();
    QString versionStr = index.data(ProjectAssetsModel::VersionStringRole).toString();
    bool hasMultiple = index.data(ProjectAssetsModel::HasMultipleVersionsRole).toBool();
    QStringList versions = index.data(ProjectAssetsModel::VersionListRole).toStringList();
    
    pmInfoFileName->setText(fileName);
    if (pmInfoFilePath) pmInfoFilePath->setText(filePath);
    
    QFileInfo fi(filePath);
    if (fi.exists()) {
        // File size
        qint64 size = fi.size();
        QString sizeStr;
        if (size < 1024) sizeStr = QString("Size: %1 bytes").arg(size);
        else if (size < 1024*1024) sizeStr = QString("Size: %1 KB").arg(size/1024.0, 0, 'f', 1);
        else if (size < 1024*1024*1024) sizeStr = QString("Size: %1 MB").arg(size/(1024.0*1024.0), 0, 'f', 2);
        else sizeStr = QString("Size: %1 GB").arg(size/(1024.0*1024.0*1024.0), 0, 'f', 2);
        if (pmInfoFileSize) pmInfoFileSize->setText(sizeStr);
        
        // File type
        QString ext = fi.suffix().toUpper();
        if (pmInfoFileType) pmInfoFileType->setText(QString("Type: %1 File").arg(ext.isEmpty() ? "Unknown" : ext));
        
        // Timestamps
        if (pmInfoCreated) pmInfoCreated->setText(QString("Created: %1").arg(fi.birthTime().toString("yyyy-MM-dd hh:mm:ss")));
        if (pmInfoModified) pmInfoModified->setText(QString("Modified: %1").arg(fi.lastModified().toString("yyyy-MM-dd hh:mm:ss")));
        
        // Permissions
        QString perms;
        if (fi.isReadable()) perms += "R";
        if (fi.isWritable()) perms += "W";
        if (fi.isExecutable()) perms += "X";
        if (pmInfoPermissions) pmInfoPermissions->setText(QString("Permissions: %1").arg(perms.isEmpty() ? "None" : perms));
        
        // Try to get image dimensions
        if (pmInfoDimensions) {
            QString dimStr;
            QImageReader reader(filePath);
            if (reader.canRead()) {
                QSize imgSize = reader.size();
                if (imgSize.isValid()) {
                    dimStr = QString("Dimensions: %1 x %2").arg(imgSize.width()).arg(imgSize.height());
                }
            }
            pmInfoDimensions->setText(dimStr);
        }
    }
    
    // Version info
    if (pmInfoVersions) {
        if (hasMultiple) {
            pmInfoVersions->setText(QString("Version: %1 (%2 versions)").arg(versionStr).arg(versions.count()));
        } else if (!versionStr.isEmpty()) {
            pmInfoVersions->setText(QString("Version: %1").arg(versionStr));
        } else {
            pmInfoVersions->clear();
        }
    }
}

void MainWindow::updatePmPreviewForIndex(const QModelIndex &idx)
{
    if (!idx.isValid()) {
        clearPmPreview();
        return;
    }
    
    // Skip folders - they don't have previews
    bool isFolder = idx.data(ProjectAssetsModel::IsFolderRole).toBool();
    if (isFolder) {
        clearPmPreview();
        return;
    }
    
    QString filePath = idx.data(ProjectAssetsModel::FilePathRole).toString();
    bool isSequenceFlag = idx.data(ProjectAssetsModel::IsSequenceRole).toBool();
    QString fileType = idx.data(ProjectAssetsModel::FileTypeRole).toString().toLower();
    
    // Store the current preview path for async callback matching
    pmCurrentPreviewPath = filePath;
    
    if (filePath.isEmpty()) {
        clearPmPreview();
        return;
    }
    
    // Handle sequences
    if (isSequenceFlag) {
        // Note: filePath points to the first frame, pattern is just the filename with ### placeholders
        int startFrame = idx.data(ProjectAssetsModel::SequenceStartFrameRole).toInt();
        int endFrame = idx.data(ProjectAssetsModel::SequenceEndFrameRole).toInt();
        
        // Reconstruct sequence frame paths using filePath (full path to first frame)
        QStringList framePaths = reconstructSequenceFramePaths(filePath, startFrame, endFrame);
        if (!framePaths.isEmpty()) {
            showPmSequence(framePaths);
            return;
        }
    }
    
    if (!QFileInfo::exists(filePath)) {
        clearPmPreview();
        return;
    }
    
    // Check if it's a video file
    static const QStringList videoExts = {"mp4", "mov", "avi", "mkv", "webm", "wmv", "flv", "m4v", "mpg", "mpeg", "mxf"};
    QString ext = QFileInfo(filePath).suffix().toLower();
    
    if (videoExts.contains(ext)) {
        showPmVideo(filePath);
    } else {
        showPmImage(filePath);
    }
}

void MainWindow::clearPmPreview()
{
    pmCurrentPreviewPath.clear();
    
    // Stop video playback
    if (pmGStreamerPlayer) {
        pmGStreamerPlayer->stop();
    }
    
    // Stop sequence playback
    pausePmSequence();
    pmIsSequence = false;
    pmSequenceFramePaths.clear();
    
    // Clear image
    if (pmImageScene) {
        pmImageScene->clear();
        pmImageItem = nullptr;
    }
    
    // Hide video widget, show image view
    if (pmVideoWidget) pmVideoWidget->hide();
    if (pmImageView) pmImageView->show();
    
    // Hide media controls
    if (pmPrevFrameBtn) pmPrevFrameBtn->hide();
    if (pmPlayPauseBtn) pmPlayPauseBtn->hide();
    if (pmNextFrameBtn) pmNextFrameBtn->hide();
    if (pmPositionSlider) pmPositionSlider->hide();
    if (pmTimeLabel) pmTimeLabel->hide();
    if (pmVolumeSlider) pmVolumeSlider->hide();
    if (pmMuteBtn) pmMuteBtn->hide();
}

void MainWindow::playPmSequence()
{
    if (pmSequenceFramePaths.isEmpty()) return;
    pmSequencePlaying = true;
    if (pmPlayPauseBtn) pmPlayPauseBtn->setIcon(icoMediaPause(ThemeManager::instance().iconColor()));
    if (pmSequenceTimer) pmSequenceTimer->start(42); // ~24fps
}

void MainWindow::pausePmSequence()
{
    pmSequencePlaying = false;
    if (pmPlayPauseBtn) pmPlayPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor()));
    if (pmSequenceTimer) pmSequenceTimer->stop();
}

void MainWindow::stepPmSequence(int delta)
{
    if (pmSequenceFramePaths.isEmpty()) return;
    pausePmSequence();
    int newIdx = pmSequenceCurrentIndex + delta;
    if (newIdx < 0) newIdx = pmSequenceFramePaths.size() - 1;
    if (newIdx >= pmSequenceFramePaths.size()) newIdx = 0;
    loadPmSequenceFrame(newIdx);
}

void MainWindow::loadPmSequenceFrame(int index)
{
    if (index < 0 || index >= pmSequenceFramePaths.size()) return;
    pmSequenceCurrentIndex = index;
    
    QString framePath = pmSequenceFramePaths.at(index);
    QPixmap pixmap;
    QSize size(512, 512);
    
    auto& lpm = LivePreviewManager::instance();
    auto handle = lpm.cachedFrame(framePath, size);
    if (handle.isValid()) {
        pixmap = handle.pixmap;
    } else {
        lpm.requestFrame(framePath, size, 0.0);
        return; // Will be updated via signal
    }
    
    if (!pixmap.isNull() && pmImageScene && pmImageView) {
        pmImageScene->clear();
        pmImageItem = pmImageScene->addPixmap(pixmap);
        pmImageView->fitInView(pmImageItem, Qt::KeepAspectRatio);
    }
    
    // Update slider and time
    if (pmPositionSlider) {
        pmPositionSlider->blockSignals(true);
        pmPositionSlider->setValue(index);
        pmPositionSlider->blockSignals(false);
    }
    if (pmTimeLabel) {
        pmTimeLabel->setText(QString("%1 / %2").arg(index + 1).arg(pmSequenceFramePaths.size()));
    }
}

void MainWindow::showPmVideo(const QString &filePath)
{
    if (!pmGStreamerPlayer || !pmVideoWidget) return;
    
    // Stop any previous playback
    pmGStreamerPlayer->stop();
    pausePmSequence();
    pmIsSequence = false;
    
    // Show video widget, hide image view
    if (pmImageView) pmImageView->hide();
    pmVideoWidget->show();
    
    // Set video widget for GStreamer (needs valid window handle)
    pmGStreamerPlayer->setVideoWidget(pmVideoWidget);
    
    // Show media controls
    if (pmPrevFrameBtn) pmPrevFrameBtn->show();
    if (pmPlayPauseBtn) pmPlayPauseBtn->show();
    if (pmNextFrameBtn) pmNextFrameBtn->show();
    if (pmPositionSlider) { pmPositionSlider->setRange(0, 1000); pmPositionSlider->show(); }
    if (pmTimeLabel) pmTimeLabel->show();
    if (pmVolumeSlider) pmVolumeSlider->show();
    if (pmMuteBtn) pmMuteBtn->show();
    
    // Load and play
    pmGStreamerPlayer->loadMedia(filePath);
    pmGStreamerPlayer->play();
}

void MainWindow::showPmImage(const QString &filePath)
{
    // Stop any previous playback
    if (pmGStreamerPlayer) pmGStreamerPlayer->stop();
    pausePmSequence();
    pmIsSequence = false;
    
    // Show image view, hide video widget
    if (pmVideoWidget) pmVideoWidget->hide();
    if (pmImageView) pmImageView->show();
    
    // Hide media controls for static images
    if (pmPrevFrameBtn) pmPrevFrameBtn->hide();
    if (pmPlayPauseBtn) pmPlayPauseBtn->hide();
    if (pmNextFrameBtn) pmNextFrameBtn->hide();
    if (pmPositionSlider) pmPositionSlider->hide();
    if (pmTimeLabel) pmTimeLabel->hide();
    if (pmVolumeSlider) pmVolumeSlider->hide();
    if (pmMuteBtn) pmMuteBtn->hide();
    
    // Load image
    QPixmap pixmap;
    QSize size(512, 512);
    
    auto& lpm = LivePreviewManager::instance();
    auto handle = lpm.cachedFrame(filePath, size);
    if (handle.isValid()) {
        pixmap = handle.pixmap;
    } else {
        lpm.requestFrame(filePath, size, 0.0);
        ThumbnailCacheManager& cache = ThumbnailCacheManager::instance();
        if (cache.isCached(filePath, size, 0.0)) {
            pixmap = cache.getCachedThumbnail(filePath, size, 0.0);
        }
    }
    
    if (!pixmap.isNull() && pmImageScene && pmImageView) {
        pmImageScene->clear();
        pmImageItem = pmImageScene->addPixmap(pixmap);
        pmImageView->fitInView(pmImageItem, Qt::KeepAspectRatio);
    }
}

void MainWindow::showPmSequence(const QStringList &framePaths)
{
    if (framePaths.isEmpty()) return;
    
    // Stop any previous playback
    if (pmGStreamerPlayer) pmGStreamerPlayer->stop();
    
    pmIsSequence = true;
    pmSequenceFramePaths = framePaths;
    pmSequenceCurrentIndex = 0;
    pmSequencePlaying = false;
    
    // Show image view, hide video widget
    if (pmVideoWidget) pmVideoWidget->hide();
    if (pmImageView) pmImageView->show();
    
    // Show media controls (no audio controls for sequences)
    if (pmPrevFrameBtn) pmPrevFrameBtn->show();
    if (pmPlayPauseBtn) { pmPlayPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor())); pmPlayPauseBtn->show(); }
    if (pmNextFrameBtn) pmNextFrameBtn->show();
    if (pmPositionSlider) {
        pmPositionSlider->setRange(0, framePaths.size() - 1);
        pmPositionSlider->setValue(0);
        pmPositionSlider->show();
    }
    if (pmTimeLabel) { pmTimeLabel->setText(QString("1 / %1").arg(framePaths.size())); pmTimeLabel->show(); }
    if (pmVolumeSlider) pmVolumeSlider->hide();
    if (pmMuteBtn) pmMuteBtn->hide();
    
    // Load first frame
    loadPmSequenceFrame(0);
}

void MainWindow::changePmPreview(int delta)
{
    // Navigate through Project Manager assets in overlay
    QAbstractItemView* view = pmIsGridMode ? static_cast<QAbstractItemView*>(pmAssetsGridView) : static_cast<QAbstractItemView*>(pmAssetsTableView);
    if (!view || !view->model()) return;
    
    QModelIndex current = view->currentIndex();
    if (!current.isValid()) return;

    const int rowCount = view->model()->rowCount();
    const int step = (delta > 0) ? 1 : -1;
    int searchRow = current.row() + delta;

    for (int i = 0; i < rowCount; ++i) {
        if (searchRow < 0) searchRow = rowCount - 1;
        if (searchRow >= rowCount) searchRow = 0;

        QModelIndex newIdx = view->model()->index(searchRow, 0);
        if (!newIdx.isValid()) {
            searchRow += step;
            continue;
        }

        QString filePath = newIdx.data(ProjectAssetsModel::FilePathRole).toString();
        bool isSequence = newIdx.data(ProjectAssetsModel::IsSequenceRole).toBool();

        view->setCurrentIndex(newIdx);
        view->scrollTo(newIdx);

        if (previewOverlay && previewOverlay->isVisible()) {
            previewOverlay->stopPlayback();
        }
        if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
            imagePreviewOverlay->stopPlayback();
        }

        if (isSequence) {
            if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
                imagePreviewOverlay->hide();
            }
            if (!previewOverlay) {
                previewOverlay = new PreviewOverlay(this);
                previewOverlay->setGeometry(geometry());
                connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changePmPreview);
            }

            QString pattern = newIdx.data(ProjectAssetsModel::SequencePatternRole).toString();
            int startFrame = newIdx.data(ProjectAssetsModel::SequenceStartFrameRole).toInt();
            int endFrame = newIdx.data(ProjectAssetsModel::SequenceEndFrameRole).toInt();

            QStringList frames;
            QFileInfo fi(pattern);
            QString base = fi.completeBaseName();
            QString ext = fi.suffix();
            QString dir = QFileInfo(filePath).absolutePath();
            int hashCount = base.count('#');

            if (hashCount > 0) {
                int hashStart = base.indexOf('#');
                QString prefix = base.left(hashStart);
                QString suffix = base.mid(hashStart + hashCount);

                for (int f = startFrame; f <= endFrame; ++f) {
                    QString frameStr = QString("%1").arg(f, hashCount, 10, QLatin1Char('0'));
                    QString framePath = dir + "/" + prefix + frameStr + suffix + "." + ext;
                    if (QFileInfo::exists(framePath)) {
                        frames.append(framePath);
                    }
                }
            }

            if (!frames.isEmpty()) {
                QString seqName = QFileInfo(pattern).fileName();
                previewOverlay->showSequence(frames, seqName, startFrame, endFrame);
                return;
            }
        }

        QFileInfo info(filePath);
        if (info.exists()) {
            const bool isImage = isImageFile(info.suffix());
            const bool isVideo = isVideoFile(info.suffix());
            if (isImage && !isVideo) {
                if (previewOverlay && previewOverlay->isVisible()) {
                    previewOverlay->hide();
                }
                if (!imagePreviewOverlay) {
                    imagePreviewOverlay = new ImagePreviewOverlay(this);
                    imagePreviewOverlay->setGeometry(geometry());
                    connect(imagePreviewOverlay, &ImagePreviewOverlay::closed, this, &MainWindow::closePreview);
                    connect(imagePreviewOverlay, &ImagePreviewOverlay::navigateRequested, this, &MainWindow::changePmPreview);
                }
                imagePreviewOverlay->showImage(filePath, info.fileName(), info.suffix());
            } else {
                if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
                    imagePreviewOverlay->hide();
                }
                if (!previewOverlay) {
                    previewOverlay = new PreviewOverlay(this);
                    previewOverlay->setGeometry(geometry());
                    connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
                    connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changePmPreview);
                }
                previewOverlay->showAsset(filePath, info.fileName(), info.suffix());
            }
        }
        return;
    }
}

// ===== PM Navigation and File Operations =====

void MainWindow::onPmNavigateBack()
{
    if (pmFolderHistoryIndex > 0) {
        pmFolderHistoryIndex--;
        pmCurrentFolderId = pmFolderHistory.at(pmFolderHistoryIndex);
        
        // Update folder tree selection
        if (pmFoldersModel && pmFolderTree) {
            QModelIndex idx = pmFoldersModel->indexForFolderId(pmCurrentFolderId);
            if (idx.isValid()) {
                pmFolderTree->setCurrentIndex(idx);
            }
        }
        
        // Filter assets
        if (pmAssetsModel) {
            pmAssetsModel->setFolderId(pmCurrentFolderId);
        }
    }
}

void MainWindow::onPmNavigateUp()
{
    if (!pmFoldersModel || pmCurrentFolderId <= 0) return;
    
    int parentId = pmFoldersModel->parentFolderId(pmCurrentFolderId);
    if (parentId > 0) {
        pmNavigateToFolder(parentId);
    } else {
        // Go to all assets (root level)
        pmCurrentFolderId = -1;
        if (pmAssetsModel) {
            pmAssetsModel->setFolderId(-1);
        }
    }
}

void MainWindow::pmNavigateToFolder(int folderId)
{
    if (folderId <= 0) return;
    
    // Add to history
    if (pmFolderHistoryIndex < pmFolderHistory.size() - 1) {
        pmFolderHistory = pmFolderHistory.mid(0, pmFolderHistoryIndex + 1);
    }
    pmFolderHistory.append(folderId);
    pmFolderHistoryIndex = pmFolderHistory.size() - 1;
    pmCurrentFolderId = folderId;
    
    // Update folder tree selection
    if (pmFoldersModel && pmFolderTree) {
        QModelIndex idx = pmFoldersModel->indexForFolderId(folderId);
        if (idx.isValid()) {
            pmFolderTree->setCurrentIndex(idx);
        }
    }
    
    // Filter assets
    if (pmAssetsModel) {
        pmAssetsModel->setFolderId(folderId);
    }
}

void MainWindow::pmNavigateBack()
{
    onPmNavigateBack();
}

void MainWindow::pmNavigateUp()
{
    onPmNavigateUp();
}

QStringList MainWindow::getSelectedPmAssetPaths() const
{
    QStringList paths;
    QModelIndexList selected;
    
    if (pmIsGridMode && pmAssetsGridView && pmAssetsGridView->selectionModel()) {
        selected = pmAssetsGridView->selectionModel()->selectedIndexes();
    } else if (!pmIsGridMode && pmAssetsTableView && pmAssetsTableView->selectionModel()) {
        selected = pmAssetsTableView->selectionModel()->selectedRows();
    }
    
    for (const QModelIndex &idx : selected) {
        QString path = idx.data(ProjectAssetsModel::FilePathRole).toString();
        if (!path.isEmpty()) {
            paths.append(path);
        }
    }
    
    return paths;
}

void MainWindow::restoreProjectManagerState()
{
    QSettings s("AugmentCode", "KAssetManager");

    if (pmSplitter && s.contains("ProjectManager/MainSplitter")) {
        pmSplitter->restoreState(s.value("ProjectManager/MainSplitter").toByteArray());
    }
    if (pmLeftSplitter && s.contains("ProjectManager/LeftSplitter")) {
        pmLeftSplitter->restoreState(s.value("ProjectManager/LeftSplitter").toByteArray());
    }
    if (pmRightSplitter && s.contains("ProjectManager/RightSplitter")) {
        pmRightSplitter->restoreState(s.value("ProjectManager/RightSplitter").toByteArray());
    }
    if (pmPreviewInfoSplitter && s.contains("ProjectManager/PreviewInfoSplitter")) {
        pmPreviewInfoSplitter->restoreState(s.value("ProjectManager/PreviewInfoSplitter").toByteArray());
    }

    if (s.contains("ProjectManager/PreviewVisible")) {
        bool previewVisible = s.value("ProjectManager/PreviewVisible").toBool();
        if (pmPreviewToggleButton) {
            QSignalBlocker block(pmPreviewToggleButton);
            pmPreviewToggleButton->setChecked(previewVisible);
        }
        if (pmPreviewPanel) pmPreviewPanel->setVisible(previewVisible);
        if (pmInfoPanel) pmInfoPanel->setVisible(previewVisible);
    }

    if (s.contains("ProjectManager/ViewMode")) {
        pmIsGridMode = s.value("ProjectManager/ViewMode").toBool();
        if (pmViewStack) pmViewStack->setCurrentIndex(pmIsGridMode ? 0 : 1);
        if (pmViewModeButton) {
            pmViewModeButton->setIcon(pmIsGridMode ? icoList(ThemeManager::instance().iconColor())
                                                   : icoGrid(ThemeManager::instance().iconColor()));
        }
    }

    if (s.contains("ProjectManager/ThumbnailSize")) {
        int thumbSize = s.value("ProjectManager/ThumbnailSize").toInt();
        if (pmThumbnailSizeSlider) {
            QSignalBlocker block(pmThumbnailSizeSlider);
            pmThumbnailSizeSlider->setValue(thumbSize);
        }
        if (pmItemDelegate) pmItemDelegate->setThumbnailSize(thumbSize);
        if (pmAssetsGridView) {
            pmAssetsGridView->setGridSize(QSize(thumbSize + 24, thumbSize + 45));
            pmAssetsGridView->viewport()->update();
        }
    }

    if (s.contains("ProjectManager/GroupSequences")) {
        bool grouping = s.value("ProjectManager/GroupSequences").toBool();
        if (pmGroupSequencesBtn) {
            QSignalBlocker block(pmGroupSequencesBtn);
            pmGroupSequencesBtn->setChecked(grouping);
        }
        if (pmSequenceProxy) pmSequenceProxy->setGroupingEnabled(grouping);
    }
}

void MainWindow::saveProjectManagerState(QSettings& s)
{
    if (pmSplitter) s.setValue("ProjectManager/MainSplitter", pmSplitter->saveState());
    if (pmLeftSplitter) s.setValue("ProjectManager/LeftSplitter", pmLeftSplitter->saveState());
    if (pmRightSplitter) s.setValue("ProjectManager/RightSplitter", pmRightSplitter->saveState());
    if (pmPreviewInfoSplitter) s.setValue("ProjectManager/PreviewInfoSplitter", pmPreviewInfoSplitter->saveState());
    if (pmPreviewToggleButton) s.setValue("ProjectManager/PreviewVisible", pmPreviewToggleButton->isChecked());
    s.setValue("ProjectManager/ViewMode", pmIsGridMode);
    if (pmThumbnailSizeSlider) s.setValue("ProjectManager/ThumbnailSize", pmThumbnailSizeSlider->value());
    if (pmGroupSequencesBtn) s.setValue("ProjectManager/GroupSequences", pmGroupSequencesBtn->isChecked());
}

QModelIndex MainWindow::pmIndexForProjectId(int projectId) const
{
    if (!pmProjectsModel || projectId <= 0) return QModelIndex();
    for (int row = 0; row < pmProjectsModel->rowCount(); ++row) {
        QModelIndex idx = pmProjectsModel->index(row, 0);
        if (idx.data(ProjectsModel::IdRole).toInt() == projectId) {
            return idx;
        }
    }
    return QModelIndex();
}

void MainWindow::navigateToProjectAsset(int projectId, int assetId, const QString& filePath)
{
    if (projectId <= 0 || !pmProjectsModel) return;

    QModelIndex projectIdx = pmIndexForProjectId(projectId);
    if (!projectIdx.isValid()) return;

    if (pmProjectsListView) {
        pmProjectsListView->setCurrentIndex(projectIdx);
    }
    onPmProjectSelected(projectIdx);

    if (pmFoldersModel && pmFoldersModel->projectId() != pmCurrentProjectId) {
        pmFoldersModel->setProjectId(pmCurrentProjectId);
    }

    QFileInfo fi(filePath);
    QString folderPath = fi.absolutePath();

    int folderId = assetId > 0 ? ProjectDB::instance().getAssetFolderId(assetId) : 0;
    if (folderId <= 0) {
        folderId = ProjectDB::instance().ensureFolderForPath(projectId, folderPath);
    }
    if (folderId > 0) {
        pmNavigateToFolder(folderId);
        if (pmFolderTree && pmFoldersModel) {
            QModelIndex idx = pmFoldersModel->indexForFolderId(folderId);
            if (idx.isValid()) {
                pmFolderTree->expand(idx);
                pmFolderTree->scrollTo(idx, QAbstractItemView::PositionAtCenter);
            }
        }
    }

    if (!pmAssetsModel) return;

    int targetRow = -1;
    const int totalRows = pmAssetsModel->rowCount();
    for (int row = 0; row < totalRows; ++row) {
        QModelIndex srcIdx = pmAssetsModel->index(row, 0);
        bool match = false;
        if (assetId > 0) {
            match = (srcIdx.data(ProjectAssetsModel::IdRole).toInt() == assetId);
        }
        if (!match && !filePath.isEmpty()) {
            match = srcIdx.data(ProjectAssetsModel::FilePathRole).toString().compare(filePath, Qt::CaseInsensitive) == 0;
        }
        if (match) {
            targetRow = row;
            break;
        }
    }

    if (targetRow < 0) return;

    QModelIndex sourceIndex = pmAssetsModel->index(targetRow, 0);
    QModelIndex viewIndex = pmSequenceProxy ? pmSequenceProxy->mapFromSource(sourceIndex) : sourceIndex;
    if (!viewIndex.isValid()) return;

    QAbstractItemView *view = pmIsGridMode ? static_cast<QAbstractItemView*>(pmAssetsGridView)
                                           : static_cast<QAbstractItemView*>(pmAssetsTableView);
    if (view && view->selectionModel()) {
        view->selectionModel()->select(viewIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        view->scrollTo(viewIndex, QAbstractItemView::PositionAtCenter);
        updatePmPreviewForIndex(viewIndex);
        updatePmInfoPanel();
    }
}

void MainWindow::onPmCopy()
{
    pmClipboard = getSelectedPmAssetPaths();
    pmClipboardCutMode = false;
    if (!pmClipboard.isEmpty()) {
        statusBar()->showMessage(QString("Copied %1 item(s)").arg(pmClipboard.size()), 2000);
    }
}

void MainWindow::onPmCut()
{
    pmClipboard = getSelectedPmAssetPaths();
    pmClipboardCutMode = true;
    if (!pmClipboard.isEmpty()) {
        statusBar()->showMessage(QString("Cut %1 item(s)").arg(pmClipboard.size()), 2000);
    }
}

void MainWindow::onPmPaste()
{
    if (pmClipboard.isEmpty()) return;
    
    // Get current folder path from the first item's directory or project watch path
    QString destDir;
    if (pmCurrentProjectId > 0) {
        QVector<Project> projects = ProjectDB::instance().listProjects();
        for (const Project& p : projects) {
            if (p.id == pmCurrentProjectId) {
                destDir = p.watchPath;
                break;
            }
        }
    }
    
    if (destDir.isEmpty()) {
        QMessageBox::warning(this, "Paste", "No destination folder available");
        return;
    }
    
    auto &q = FileOpsQueue::instance();
    if (pmClipboardCutMode) {
        // For move operations, remove from database first (will be re-added by watcher at new location)
        ProjectDB::instance().removeAssetsByPath(pmClipboard);
        q.enqueueMove(pmClipboard, destDir);
    } else {
        q.enqueueCopy(pmClipboard, destDir);
    }
    
    // Refresh view immediately for cut operations
    if (pmClipboardCutMode && pmAssetsModel && pmCurrentProjectId > 0) {
        pmAssetsModel->setProjectId(pmCurrentProjectId);
    }
    
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show();
    fileOpsDialog->raise();
    
    pmClipboard.clear();
    pmClipboardCutMode = false;
}

void MainWindow::onPmDelete()
{
    QStringList paths = getSelectedPmAssetPaths();
    if (paths.isEmpty()) return;
    
    int ret = QMessageBox::question(this, "Delete Files", 
        QString("Move %1 item(s) to Recycle Bin?").arg(paths.size()),
        QMessageBox::Yes | QMessageBox::No);
    
    if (ret != QMessageBox::Yes) return;
    
    // Remove from database first (files will be moved to recycle bin)
    ProjectDB::instance().removeAssetsByPath(paths);
    
    // Refresh the view immediately
    if (pmAssetsModel && pmCurrentProjectId > 0) {
        pmAssetsModel->setProjectId(pmCurrentProjectId);
    }
    
    FileOpsQueue::instance().enqueueDelete(paths);
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show();
}

void MainWindow::onPmRename()
{
    QStringList paths = getSelectedPmAssetPaths();
    if (paths.size() != 1) return;
    
    QFileInfo fi(paths.first());
    bool ok = false;
    QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, fi.fileName(), &ok);
    if (!ok || newName.trimmed().isEmpty() || newName == fi.fileName()) return;
    
    QString oldPath = paths.first();
    QString newPath = fi.absolutePath() + "/" + newName.trimmed();
    
    QDir parent(fi.absolutePath());
    if (parent.rename(fi.fileName(), newName.trimmed())) {
        // Update database with new path
        ProjectDB::instance().updateAssetPath(oldPath, newPath);
        
        statusBar()->showMessage("Renamed successfully", 2000);
        if (pmAssetsModel && pmCurrentProjectId > 0) {
            pmAssetsModel->setProjectId(pmCurrentProjectId);
        }
    } else {
        QMessageBox::warning(this, "Rename", "Failed to rename file");
    }
}

void MainWindow::onPmNewFolder()
{
    // Create folder in current project's watch path
    QString destDir;
    if (pmCurrentProjectId > 0) {
        QVector<Project> projects = ProjectDB::instance().listProjects();
        for (const Project& p : projects) {
            if (p.id == pmCurrentProjectId) {
                destDir = p.watchPath;
                break;
            }
        }
    }
    
    if (destDir.isEmpty()) {
        QMessageBox::warning(this, "New Folder", "No project selected");
        return;
    }
    
    bool ok = false;
    QString folderName = QInputDialog::getText(this, "New Folder", "Folder name:", QLineEdit::Normal, "New Folder", &ok);
    if (!ok || folderName.trimmed().isEmpty()) return;
    
    QDir dir(destDir);
    if (dir.mkdir(folderName.trimmed())) {
        statusBar()->showMessage("Folder created", 2000);
        onPmRefresh();
    } else {
        QMessageBox::warning(this, "New Folder", "Failed to create folder");
    }
}

void MainWindow::onPmOpenExternal()
{
    QStringList paths = getSelectedPmAssetPaths();
    if (paths.isEmpty()) return;
    
    for (const QString &path : paths) {
        if (ProjectVersionDetector::isProjectFile(path)) {
            QSettings s("AugmentCode", "KAssetManager");
            QString exePath;
            
            if (ProjectVersionDetector::isAfterEffectsFile(path)) {
                exePath = s.value("ExternalApps/AfterEffectsPath").toString();
            } else if (ProjectVersionDetector::isNukeFile(path)) {
                exePath = s.value("ExternalApps/NukeXPath").toString();
            }
            
            if (!exePath.isEmpty() && QFile::exists(exePath)) {
                QProcess::startDetached(exePath, QStringList() << path);
                continue;
            }
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void MainWindow::onPmTogglePreview(bool checked)
{
    if (pmPreviewInfoSplitter) {
        pmPreviewPanel->setVisible(checked);
        pmInfoPanel->setVisible(checked);
    }
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("ProjectManager/PreviewVisible", checked);
}

void MainWindow::onPmGroupSequencesToggled(bool checked)
{
    // Toggle sequence grouping in the proxy model
    if (pmSequenceProxy) {
        pmSequenceProxy->setGroupingEnabled(checked);
    }
    statusBar()->showMessage("Sequence grouping: " + QString(checked ? "ON" : "OFF"), 2000);
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("ProjectManager/GroupSequences", checked);
}

void MainWindow::onPmFolderContextMenu(const QPoint &pos)
{
    if (!pmFolderTree) return;
    
    QModelIndex index = pmFolderTree->indexAt(pos);
    
    QMenu menu;
    QAction *refreshA = menu.addAction("Refresh");
    QAction *newFolderA = menu.addAction("New Folder");
    QAction *generateThumbsA = nullptr;
    QAction *generateThumbsRecA = nullptr;

    int folderId = -1;
    if (index.isValid()) {
        folderId = index.data(ProjectFoldersModel::IdRole).toInt();
        menu.addSeparator();
        generateThumbsA = menu.addAction("Generate Thumbnails");
        generateThumbsRecA = menu.addAction("Generate Thumbnails (Recursive)");
    }
    
    QAction *chosen = menu.exec(pmFolderTree->viewport()->mapToGlobal(pos));
    if (!chosen) return;
    
    if (chosen == refreshA) {
        onPmRefresh();
    } else if (chosen == newFolderA) {
        onPmNewFolder();
    } else if (chosen == generateThumbsA && folderId > 0) {
        auto *dialog = new ThumbnailGeneratorDialog(folderId, false, this, true);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    } else if (chosen == generateThumbsRecA && folderId > 0) {
        auto *dialog = new ThumbnailGeneratorDialog(folderId, true, this, true);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
}


void MainWindow::setupConnections()
{
    if (mainTabs) {
        connect(mainTabs, &QTabWidget::currentChanged, this, &MainWindow::onTabChanged);
    }

    // Debounced folder selection for Asset Manager
    folderSelectTimer.setSingleShot(true);
    connect(&folderSelectTimer, &QTimer::timeout, this, [this]{
        const int fid = pendingFolderId;
        if (fid <= 0) return;
        navigateToFolder(fid, true);
    });

    connect(folderTreeView, &QTreeView::clicked, this, &MainWindow::onFolderSelected);
    connect(folderTreeView, &QTreeView::customContextMenuRequested, this, &MainWindow::onFolderContextMenu);
    connect(folderTreeView, &QTreeView::collapsed, this, [this](const QModelIndex &idx){
        if (!folderModel || !folderTreeView) return;
        if (!idx.isValid()) return;
        if (!idx.parent().isValid()) {
            // Root nodes must always remain expanded
            folderTreeView->setExpanded(idx, true);
        }
    });

    connect(assetGridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onAssetSelectionChanged);
    connect(assetGridView, &QListView::doubleClicked, this, &MainWindow::onAssetDoubleClicked);
    connect(assetGridView, &QListView::customContextMenuRequested, this, &MainWindow::onAssetContextMenu);

    // Connect table view signals
    connect(assetTableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onAssetSelectionChanged);
    connect(assetTableView, &QTableView::doubleClicked, this, &MainWindow::onAssetDoubleClicked);
    connect(assetTableView, &QTableView::customContextMenuRequested, this, &MainWindow::onAssetContextMenu);

    // Update tag button states when selections change
    connect(tagsListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateTagButtonStates);
    connect(assetGridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateTagButtonStates);
    connect(assetTableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateTagButtonStates);

    // Tag context menu
    connect(tagsListView, &QListView::customContextMenuRequested, this, &MainWindow::onTagContextMenu);

    // Install event filter on tags viewport after UI is fully built
    if (tagsListView && tagsListView->viewport()) {
        tagsListView->viewport()->installEventFilter(this);
        qDebug() << "[INIT] tagsListView viewport event filter installed (late)";
    }

    // Connect search box for manual search (Enter key)
    connect(searchBox, &QLineEdit::returnPressed, this, [this]() {
        onSearchTextChanged(searchBox->text());
    });

    // Visible-only live preview progress wiring
    connect(assetsModel, &QAbstractItemModel::modelReset, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(assetGridView->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(assetGridView->horizontalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(assetTableView->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(assetTableView->horizontalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(viewStack, &QStackedWidget::currentChanged, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    if (fmGridView && fmGridView->verticalScrollBar()) {
        connect(fmGridView->verticalScrollBar(), &QScrollBar::valueChanged,
                this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    }
    if (fmGridView && fmGridView->horizontalScrollBar()) {
        connect(fmGridView->horizontalScrollBar(), &QScrollBar::valueChanged,
                this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    }
    if (fmListView && fmListView->verticalScrollBar()) {
        connect(fmListView->verticalScrollBar(), &QScrollBar::valueChanged,
                this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    }
    if (fmListView && fmListView->horizontalScrollBar()) {
        connect(fmListView->horizontalScrollBar(), &QScrollBar::valueChanged,
                this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    }
    connect(&ProgressManager::instance(), &ProgressManager::isActiveChanged, this, [this]() {

        if (ProgressManager::instance().isActive()) {
            // Hide our visible-only progress while an import/global progress is active
            thumbnailProgressLabel->setVisible(false);
            thumbnailProgressBar->setVisible(false);
        } else {
            scheduleVisibleThumbProgressUpdate();
        }
    });
    // Update version table when versions change
    connect(&DB::instance(), &DB::assetVersionsChanged,
            this, &MainWindow::onAssetVersionsChanged);

    // Note: We intentionally do NOT refresh File Manager after file operations.
    // QFileSystemModel has its own internal file watcher that auto-updates.
    // Triggering onFmRefresh() would cause unnecessary 1sec+ delays after every operation.
    // Use F5 or View > Refresh for manual refresh if needed.
}


void MainWindow::onTabChanged(int index)
{
    Q_UNUSED(index);
    // We already persist the last active tab from closeEvent; this slot is kept
    // only to allow future per-tab hooks without resetting state on tab switch.
}


void MainWindow::onFolderSelected(const QModelIndex &index)
{
    if (!index.isValid()) {
        qWarning() << "MainWindow::onFolderSelected - Invalid index";
        return;
    }

    int folderId = index.data(VirtualFolderTreeModel::IdRole).toInt();
    if (folderId <= 0) {
        qWarning() << "MainWindow::onFolderSelected - Invalid folder ID:" << folderId;
        return;
    }

    // Debounce rapid selections; actual load happens on timer to allow cleanup/cancel
    pendingFolderId = folderId;
    folderSelectTimer.start(150);
}

void MainWindow::onAssetNavigateBack()
{
    if (amNavigationIndex <= 0 || amNavigationHistory.isEmpty()) {
        return;
    }

    amNavigationIndex--;
    int folderId = amNavigationHistory[amNavigationIndex];

    // Simply navigate to the folder - don't add to history since we're going back
    navigateToFolder(folderId, false);
}

void MainWindow::onAssetNavigateUp()
{
    if (!assetsModel || !folderModel) {
        return;
    }

    int currentFolderId = assetsModel->folderId();
    if (currentFolderId <= 0) {
        return;
    }

    // Check if we're already at root
    int rootId = folderModel->rootId();
    if (currentFolderId == rootId) {
        return;
    }

    // Query the database to find the parent folder ID directly
    QSqlQuery q(DB::instance().database());
    q.prepare("SELECT parent_id FROM virtual_folders WHERE id = ?");
    q.addBindValue(currentFolderId);
    if (!q.exec() || !q.next()) {
        return;
    }

    int parentFolderId = q.value(0).toInt();
    if (parentFolderId <= 0) {
        parentFolderId = rootId; // If parent_id is NULL or 0, parent is root
    }

    // Simply navigate to the parent folder
    navigateToFolder(parentFolderId, true);
}

void MainWindow::onAssetNewFolder()
{
    if (!folderModel || !folderTreeView) return;

    QModelIndex currentIndex = folderTreeView->currentIndex();
    int parentFolderId = 0;
    if (currentIndex.isValid()) {
        parentFolderId = currentIndex.data(VirtualFolderTreeModel::IdRole).toInt();
    }
    if (parentFolderId <= 0) {
        parentFolderId = folderModel->rootId();
    }

    bool ok = false;
    QString name = QInputDialog::getText(this, "Create Subfolder",
                                         "Enter subfolder name:",
                                         QLineEdit::Normal, QString(), &ok);
    name = name.trimmed();
    if (!ok || name.isEmpty()) {
        return;
    }

    int newId = DB::instance().createFolder(name, parentFolderId);
    if (newId > 0) {
        // Set pending selection so the new folder is selected after model reset
        pendingSelectFolderIdAfterReload = newId;
        folderModel->reload();
        statusBar()->showMessage(QString("Created subfolder '%1'").arg(name), 3000);
    } else {
        QMessageBox::warning(this, "Error", "Failed to create subfolder");
    }
}

void MainWindow::onAssetGroupSequencesToggled(bool checked)
{
    setAssetManagerSequenceGroupingEnabled(checked);
}

void MainWindow::onAssetFoldersModelAboutToReset()
{
    if (!folderModel || !folderTreeView) return;

    qDebug() << "[Asset Manager] Folder model about to reset - saving tree state";

    // Save expansion state
    saveFolderExpansionState();

    // Save scroll position
    QScrollBar *vbar = folderTreeView->verticalScrollBar();
    if (vbar) {
        savedTreeScrollPosition = vbar->value();
    }

    // Save current selection (may be multiple folders)
    savedSelectedFolderIds.clear();
    QModelIndexList selected = folderTreeView->selectionModel()->selectedIndexes();
    for (const QModelIndex &idx : selected) {
        if (idx.isValid()) {
            int folderId = idx.data(VirtualFolderTreeModel::IdRole).toInt();
            if (folderId > 0) {
                savedSelectedFolderIds.append(folderId);
            }
        }
    }

    qDebug() << "[Asset Manager] Saved" << expandedFolderIds.size() << "expanded folders,"
             << savedSelectedFolderIds.size() << "selected folders, scroll position" << savedTreeScrollPosition;
}

void MainWindow::onAssetFoldersModelReset()
{
    if (!folderModel || !folderTreeView) return;

    qDebug() << "[Asset Manager] Folder model reset - restoring tree state";

    // Restore expansion state
    restoreFolderExpansionState();

    // Restore selection
    QItemSelectionModel *selModel = folderTreeView->selectionModel();
    if (selModel) {
        selModel->clearSelection();

        // Priority 1: If we have a pending folder ID from an operation (e.g., just created), select that
        if (pendingSelectFolderIdAfterReload > 0) {
            QModelIndex idx = folderModel->findIndexById(pendingSelectFolderIdAfterReload);
            if (idx.isValid()) {
                // Expand ancestors to make it visible
                QModelIndex parent = idx.parent();
                while (parent.isValid()) {
                    folderTreeView->setExpanded(parent, true);
                    parent = parent.parent();
                }
                selModel->setCurrentIndex(idx, QItemSelectionModel::Select);
                folderTreeView->scrollTo(idx, QAbstractItemView::PositionAtCenter);
                qDebug() << "[Asset Manager] Selected pending folder ID" << pendingSelectFolderIdAfterReload;
            }
            pendingSelectFolderIdAfterReload = -1; // Clear pending
        }
        // Priority 2: Restore previously selected folders
        else if (!savedSelectedFolderIds.isEmpty()) {
            bool anyRestored = false;
            for (int folderId : savedSelectedFolderIds) {
                QModelIndex idx = folderModel->findIndexById(folderId);
                if (idx.isValid()) {
                    // Expand ancestors
                    QModelIndex parent = idx.parent();
                    while (parent.isValid()) {
                        folderTreeView->setExpanded(parent, true);
                        parent = parent.parent();
                    }
                    selModel->select(idx, QItemSelectionModel::Select);
                    if (!anyRestored) {
                        selModel->setCurrentIndex(idx, QItemSelectionModel::Current);
                        folderTreeView->scrollTo(idx, QAbstractItemView::PositionAtCenter);
                        anyRestored = true;
                    }
                }
            }
            if (anyRestored) {
                qDebug() << "[Asset Manager] Restored selection for" << savedSelectedFolderIds.size() << "folders";
            } else {
                // Fallback to root if none of the saved folders exist anymore
                QModelIndex rootIdx = folderModel->index(0, 0, QModelIndex());
                if (rootIdx.isValid()) {
                    selModel->setCurrentIndex(rootIdx, QItemSelectionModel::Select);
                    qDebug() << "[Asset Manager] Fallback to root (saved folders no longer exist)";
                }
            }
        }
    }

    // Restore scroll position (do this after selection to avoid fighting with scrollTo)
    QTimer::singleShot(50, this, [this]() {
        QScrollBar *vbar = folderTreeView->verticalScrollBar();
        if (vbar && savedTreeScrollPosition >= 0) {
            vbar->setValue(savedTreeScrollPosition);
            qDebug() << "[Asset Manager] Restored scroll position to" << savedTreeScrollPosition;
        }
    });
}


void MainWindow::onAssetSelectionChanged()
{
    updateSelectionInfo();
    updateInfoPanel();
}

void MainWindow::onAssetDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;
    showPreview(index.row());
}

void MainWindow::onAssetContextMenu(const QPoint &pos)
{
    // Get index from the currently active view
    QModelIndex index;
    QAbstractItemView *activeView = nullptr;
    if (isGridMode) {
        activeView = assetGridView;
        if (assetGridView)
            index = assetGridView->indexAt(pos);
    } else {
        activeView = assetTableView;
        if (assetTableView)
            index = assetTableView->indexAt(pos);
    }

    if (!activeView)
        return;

    QMenu menu(this);

    if (index.isValid()) {
        // Asset context menu
        QAction *openAction = menu.addAction("Open Preview");
        QAction *showInExplorerAction = menu.addAction("Show in Explorer");
        QAction *generateThumbAction = menu.addAction("Generate Thumbnail");
        menu.addSeparator();

        // Assign Tag submenu
        QMenu *assignTagMenu = menu.addMenu("Assign Tag");

        QVector<QPair<int, QString>> tags = DB::instance().listTags();
        for (const auto& tag : tags) {
            QAction *tagAction = assignTagMenu->addAction(tag.second);
            tagAction->setData(tag.first);
        }
        if (tags.isEmpty()) {
            QAction *noTagsAction = assignTagMenu->addAction("(No tags available)");
            noTagsAction->setEnabled(false);
        }

        // Set Rating submenu
        QMenu *setRatingMenu = menu.addMenu("Set Rating");

        QAction *rating0 = setRatingMenu->addAction("☆☆☆☆☆ (Clear rating)");
        rating0->setData(-1);
        setRatingMenu->addSeparator();
        QAction *rating1 = setRatingMenu->addAction("★☆☆☆☆");
        rating1->setData(1);
        QAction *rating2 = setRatingMenu->addAction("★★☆☆☆");
        rating2->setData(2);
        QAction *rating3 = setRatingMenu->addAction("★★★☆☆");
        rating3->setData(3);
        QAction *rating4 = setRatingMenu->addAction("★★★★☆");
        rating4->setData(4);
        QAction *rating5 = setRatingMenu->addAction("★★★★★");
        rating5->setData(5);

        menu.addSeparator();

        // Bulk rename action (only show if multiple assets selected)
        QAction *bulkRenameAction = nullptr;
        QSet<int> selectedIds = getSelectedAssetIds();
        if (selectedIds.size() > 1) {
            bulkRenameAction = menu.addAction(QString("Bulk Rename (%1 assets)...").arg(selectedIds.size()));
        }

        // Convert to Format... only when all selected assets are supported media files
        QAction *convertAction = nullptr;
        QStringList selectedAssetFilePaths;
        {
            QSet<int> ids = getSelectedAssetIds();
            if (!ids.isEmpty() && assetsModel) {
                const int rows = assetsModel->rowCount(QModelIndex());
                for (int r = 0; r < rows; ++r) {
                    QModelIndex mi = assetsModel->index(r, 0);
                    int id = mi.data(AssetsModel::IdRole).toInt();
                    if (ids.contains(id)) {
                        const QString fp = mi.data(AssetsModel::FilePathRole).toString();
                        if (!fp.isEmpty()) selectedAssetFilePaths << fp;
                    }
                }
                auto isSupportedExt = [](const QString &ext) {
                    static const QSet<QString> img{ "png","jpg","jpeg","tif","tiff","exr","iff","psd" };
                    static const QSet<QString> vid{ "mov","mxf","mp4","avi","mp5" };
                    return img.contains(ext) || vid.contains(ext);
                };
                bool allSupported = !selectedAssetFilePaths.isEmpty();
                for (const QString &p : selectedAssetFilePaths) {
                    QFileInfo fi(p);
                    if (!fi.exists() || fi.isDir() || !isSupportedExt(fi.suffix().toLower())) {
                        allSupported = false;
                        break;
                    }
                }
                if (allSupported) convertAction = menu.addAction("Convert to Format...");
            }
        }

        QAction *removeAction = menu.addAction("Remove from App");

        QAction *selected = menu.exec(activeView->viewport()->mapToGlobal(pos));

        if (selected == openAction) {
            showPreview(index.row());
        } else if (selected == showInExplorerAction) {
#ifdef Q_OS_WIN
            QString filePath = index.data(AssetsModel::FilePathRole).toString();
            if (!filePath.isEmpty()) {
                QFileInfo fileInfo(filePath);
                QStringList args;
                args << "/select," << QDir::toNativeSeparators(fileInfo.absoluteFilePath());
                QProcess::startDetached("explorer", args);
            }
#endif
        } else if (selected == generateThumbAction) {
            const QString filePath = index.data(AssetsModel::FilePathRole).toString();
            const bool isSequence = index.data(AssetsModel::IsSequenceRole).toBool();
            const QString fileType = index.data(AssetsModel::FileTypeRole).toString().toLower();

            if (!filePath.isEmpty()) {
                QVector<ThumbnailGeneratorWorker::Task> tasks;
                ThumbnailGeneratorWorker::Task task;
                task.filePath = filePath;

                static const QSet<QString> videoExts = {
                    "mov", "qt", "mp4", "m4v", "mxf", "avi", "mkv", "webm",
                    "mpg", "mpeg", "m2v", "m2ts", "mts", "wmv", "asf", "flv"
                };

                if (isSequence) {
                    task.isSequence = true;
                    task.isVideo = false;

                    const int startFrame = index.data(AssetsModel::SequenceStartFrameRole).toInt();
                    const int endFrame = index.data(AssetsModel::SequenceEndFrameRole).toInt();
                    QStringList framePaths = reconstructSequenceFramePaths(filePath, startFrame, endFrame);
                    task.sequenceFrames = framePaths;
                } else if (videoExts.contains(fileType)) {
                    task.isVideo = true;
                    task.isSequence = false;
                } else {
                    task.isVideo = false;
                    task.isSequence = false;
                }

                tasks.append(task);

                auto *worker = new ThumbnailGeneratorWorker();
                auto *thread = new QThread(this);

                worker->moveToThread(thread);
                connect(thread, &QThread::finished, worker, &QObject::deleteLater);
                connect(this, &QObject::destroyed, thread, &QThread::quit);

                connect(worker, &ThumbnailGeneratorWorker::queueFinished, this, [this, thread]() {
                    if (assetGridView) assetGridView->viewport()->update();
                    if (assetTableView) assetTableView->viewport()->update();
                    statusBar()->showMessage("Thumbnail generation completed", 3000);
                    thread->quit();
                });

                connect(worker, &ThumbnailGeneratorWorker::logLine, this, [](const QString &line) {
                    qDebug() << "[ThumbnailGeneratorWorker]" << line;
                });

                thread->start(QThread::LowPriority);

                ThumbnailCacheManager &cache = ThumbnailCacheManager::instance();
                const QSize thumbnailSize = cache.getThumbnailSize();

                QMetaObject::invokeMethod(worker, [worker, tasks, thumbnailSize]() {
                    worker->start(tasks, thumbnailSize);
                }, Qt::QueuedConnection);

                statusBar()->showMessage("Generating thumbnail...", 2000);
            }
        } else if (selected == convertAction) {
            releaseAnyPreviewLocksForPaths(selectedAssetFilePaths);
            auto *dlg = new MediaConvertDialog(selectedAssetFilePaths, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &QDialog::accepted, this, &MainWindow::onFmRefresh);
            connect(dlg, &QObject::destroyed, this, [this]() { QTimer::singleShot(100, this, &MainWindow::onFmRefresh); });
            dlg->show(); dlg->raise(); dlg->activateWindow();
        } else if (selected && assignTagMenu->actions().contains(selected)) {
            // Assign tag action
            int tagId = selected->data().toInt();
            QSet<int> selectedIds = getSelectedAssetIds();
            QList<int> assetIdsList = selectedIds.values();
            QList<int> tagIds = { tagId };

            if (DB::instance().assignTagsToAssets(assetIdsList, tagIds)) {
                updateInfoPanel();
                statusBar()->showMessage(QString("Assigned tag to %1 asset(s)").arg(assetIdsList.size()), 3000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to assign tag");
            }
        } else if (selected && setRatingMenu->actions().contains(selected)) {
            // Set rating action
            int rating = selected->data().toInt();
            QSet<int> selectedIds = getSelectedAssetIds();
            QList<int> assetIdsList = selectedIds.values();

            if (DB::instance().setAssetsRating(assetIdsList, rating)) {
                assetsModel->reload();
                updateInfoPanel();
                QString ratingText = (rating < 0) ? "cleared" : QString::number(rating) + " star(s)";
                statusBar()->showMessage(QString("Set rating to %1 for %2 asset(s)").arg(ratingText).arg(assetIdsList.size()), 3000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to set rating");
            }
        } else if (bulkRenameAction && selected == bulkRenameAction) {
            // Bulk rename action
            QSet<int> selectedIds = getSelectedAssetIds();
            QVector<int> assetIdsVec = selectedIds.values().toVector();

            BulkRenameDialog dialog(assetIdsVec, this);
            if (dialog.exec() == QDialog::Accepted) {
                assetsModel->reload();
                statusBar()->showMessage("Bulk rename completed", 3000);
            }
        } else if (selected == removeAction) {
            // Remove selected assets from database
            QSet<int> selectedIds = getSelectedAssetIds();
            QList<int> assetIdsList = selectedIds.values();

            QMessageBox::StandardButton reply = QMessageBox::question(
                this, "Remove Assets",
                QString("Are you sure you want to remove %1 asset(s) from the library?\n\nThis will not delete the actual files.").arg(assetIdsList.size()),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                if (DB::instance().removeAssets(assetIdsList)) {
                    assetsModel->reload();
                    clearSelection();
                    statusBar()->showMessage(QString("Removed %1 asset(s) from library").arg(assetIdsList.size()), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to remove assets");
                }
            }
        }
    } else {
        // Empty space context menu
        QAction *clearSelectionAction = menu.addAction("Clear Selection");

        QAction *selected = menu.exec(assetGridView->mapToGlobal(pos));

        if (selected == clearSelectionAction) {
            clearSelection();
        }
    }
}

void MainWindow::onFolderContextMenu(const QPoint &pos)
{
    QModelIndex index = folderTreeView->indexAt(pos);
    if (!index.isValid()) return;

    // Get all selected folders
    QModelIndexList selectedIndexes = folderTreeView->selectionModel()->selectedIndexes();
    if (selectedIndexes.isEmpty()) return;

    // Get info from the clicked folder
    int folderId = folderModel->data(index, VirtualFolderTreeModel::IdRole).toInt();
    QString folderName = folderModel->data(index, Qt::DisplayRole).toString();
    bool isProjectFolder = folderModel->data(index, VirtualFolderTreeModel::IsProjectFolderRole).toBool();
    int projectFolderId = folderModel->data(index, VirtualFolderTreeModel::ProjectFolderIdRole).toInt();

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; }"
        "QMenu::item:selected { background-color: #2f3a4a; }"
    );

    QAction *createAction = menu.addAction("Create Subfolder");
    QAction *renameAction = nullptr;
    QAction *deleteAction = nullptr;

    // Only show rename for single selection
    if (selectedIndexes.size() == 1) {
        renameAction = menu.addAction("Rename");
    }

    // Only allow deletion of non-project folders
    if (!isProjectFolder) {
        deleteAction = menu.addAction("Delete");
    } else {
        deleteAction = menu.addAction("Remove Project Folder");
    }

    // Add thumbnail generation actions
    menu.addSeparator();
    QAction *generateThumbsAction = menu.addAction("Generate Thumbnails");
    QAction *generateThumbsRecAction = menu.addAction("Generate Thumbnails (Recursive)");

    QAction *selected = menu.exec(folderTreeView->mapToGlobal(pos));

    if (selected == createAction) {
        // Create subfolder
        bool ok;
        QString name = QInputDialog::getText(this, "Create Subfolder",
                                            "Enter subfolder name:",
                                            QLineEdit::Normal, "", &ok);
        if (ok && !name.isEmpty()) {
            int newId = DB::instance().createFolder(name, folderId);
            if (newId > 0) {
                // Set pending selection so the new folder is selected after model reset
                pendingSelectFolderIdAfterReload = newId;
                folderModel->reload();
                statusBar()->showMessage(QString("Created subfolder '%1'").arg(name), 3000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to create subfolder");
            }
        }
    } else if (renameAction && selected == renameAction) {
        // Rename folder (only for single selection)
        bool ok;
        QString newName = QInputDialog::getText(this, "Rename Folder",
                                               "Enter new name:",
                                               QLineEdit::Normal, folderName, &ok);
        if (ok && !newName.isEmpty() && newName != folderName) {
            // If it's a project folder, use the project folder rename method
            if (isProjectFolder) {
                if (DB::instance().renameProjectFolder(projectFolderId, newName)) {
                    // Keep the same folder selected after rename (savedSelectedFolderIds will handle this)
                    folderModel->reload();
                    statusBar()->showMessage(QString("Renamed project folder to '%1'").arg(newName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to rename project folder");
                }
            } else {
                if (DB::instance().renameFolder(folderId, newName)) {
                    // Keep the same folder selected after rename (savedSelectedFolderIds will handle this)
                    folderModel->reload();
                    statusBar()->showMessage(QString("Renamed folder to '%1'").arg(newName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to rename folder");
                }
            }
        }
    } else if (selected == deleteAction) {
        // Collect all selected folders
        QList<int> folderIds;
        QList<int> projectFolderIds;
        QStringList folderNames;

        for (const QModelIndex &idx : selectedIndexes) {
            int id = folderModel->data(idx, VirtualFolderTreeModel::IdRole).toInt();
            QString name = folderModel->data(idx, Qt::DisplayRole).toString();
            bool isProjFolder = folderModel->data(idx, VirtualFolderTreeModel::IsProjectFolderRole).toBool();
            int projFolderId = folderModel->data(idx, VirtualFolderTreeModel::ProjectFolderIdRole).toInt();

            if (isProjFolder) {
                projectFolderIds.append(projFolderId);
            } else {
                folderIds.append(id);
            }
            folderNames.append(name);
        }

        // Show confirmation dialog
        QString message;
        if (selectedIndexes.size() == 1) {
            if (!projectFolderIds.isEmpty()) {
                message = QString("Are you sure you want to remove project folder '%1'?\n\nThis will remove the folder and all its assets from the library, but will not delete the actual files.").arg(folderNames.first());
            } else {
                message = QString("Are you sure you want to delete '%1' and all its contents?").arg(folderNames.first());
            }
        } else {
            message = QString("Are you sure you want to delete %1 folders and all their contents?\n\nFolders: %2")
                .arg(selectedIndexes.size())
                .arg(folderNames.join(", "));
        }

        QMessageBox::StandardButton reply = QMessageBox::question(
            this, selectedIndexes.size() == 1 ? "Delete Folder" : "Delete Folders",
            message,
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            int deletedCount = 0;

            // Delete project folders
            for (int projFolderId : projectFolderIds) {
                projectFolderWatcher->removeProjectFolder(projFolderId);
                if (DB::instance().deleteProjectFolder(projFolderId)) {
                    deletedCount++;
                }
            }

            // Delete regular folders
            for (int id : folderIds) {
                if (DB::instance().deleteFolder(id)) {
                    deletedCount++;
                }
            }

            folderModel->reload();
            assetsModel->reload();

            if (deletedCount == selectedIndexes.size()) {
                statusBar()->showMessage(QString("Deleted %1 folder(s)").arg(deletedCount), 3000);
            } else {
                QMessageBox::warning(this, "Error",
                    QString("Failed to delete some folders. Deleted %1 of %2").arg(deletedCount).arg(selectedIndexes.size()));
            }
        }
    } else if (selected == generateThumbsAction) {
        // Generate thumbnails for this folder (non-recursive)
        auto *dialog = new ThumbnailGeneratorDialog(folderId, false, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    } else if (selected == generateThumbsRecAction) {
        // Generate thumbnails for this folder (recursive)
        auto *dialog = new ThumbnailGeneratorDialog(folderId, true, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
}

void MainWindow::navigateToFolder(int folderId, bool addToHistory)
{
    if (folderId <= 0) return;

    // Save context for current folder before switching
    if (currentAssetId > 0 || !selectedAssetIds.isEmpty()) {
        int currentFolderId = assetsModel ? assetsModel->folderId() : -1;
        if (currentFolderId > 0) {
            ContextPreserver::FolderContext ctx;
            // Save scroll position
            if (isGridMode && assetGridView) {
                ctx.scrollPosition = assetGridView->verticalScrollBar()->value();
            } else if (!isGridMode && assetTableView) {
                ctx.scrollPosition = assetTableView->verticalScrollBar()->value();
            }
            ctx.isGridMode = isGridMode;
            if (searchBox) ctx.searchText = searchBox->text();
            if (ratingFilter) ctx.ratingFilter = ratingFilter->currentIndex() - 1; // -1 for "All"
            ctx.selectedAssetIds = selectedAssetIds;
            if (recursiveCheckBox) ctx.recursiveMode = recursiveCheckBox->isChecked();

            // Save selected tags
            if (tagsListView && tagsListView->selectionModel()) {
                QModelIndexList tagSelection = tagsListView->selectionModel()->selectedIndexes();
                for (const QModelIndex& idx : tagSelection) {
                    int tagId = idx.data(TagsModel::IdRole).toInt();
                    if (tagId > 0) ctx.selectedTagIds.insert(tagId);
                }
            }

            ContextPreserver::instance().saveFolderContext(currentFolderId, ctx);
        }
    }

    // Stop any preview playback and cancel pending thumbnail generation
    // Cancelling pending requests ensures user clicks take priority over background work
    if (previewOverlay) previewOverlay->stopPlayback();
    LivePreviewManager::instance().cancelPending();

    // Update navigation history
    if (addToHistory) {
        // Remove any forward history if we're not at the end
        while (amNavigationIndex < amNavigationHistory.size() - 1) {
            amNavigationHistory.removeLast();
        }
        // Add the new folder to history
        amNavigationHistory.append(folderId);
        amNavigationIndex = amNavigationHistory.size() - 1;
    }

    // Apply folder change
    if (assetsModel) {
        assetsModel->setFolderId(folderId);
    }

    // Try to restore context for new folder
    if (ContextPreserver::instance().hasFolderContext(folderId)) {
        QTimer::singleShot(50, this, [this, folderId](){
            ContextPreserver::FolderContext ctx = ContextPreserver::instance().loadFolderContext(folderId);

            // Restore view mode
            if (ctx.isGridMode != isGridMode) {
                onViewModeChanged();
            }

            // Restore filters
            if (searchBox && !ctx.searchText.isEmpty()) {
                searchBox->setText(ctx.searchText);
            }
            if (ratingFilter && ctx.ratingFilter >= -1) {
                ratingFilter->setCurrentIndex(ctx.ratingFilter + 1);
            }
            if (recursiveCheckBox) {
                recursiveCheckBox->setChecked(ctx.recursiveMode);
            }

            // Restore scroll position
            if (ctx.scrollPosition > 0) {
                if (isGridMode && assetGridView) {
                    assetGridView->verticalScrollBar()->setValue(ctx.scrollPosition);
                } else if (!isGridMode && assetTableView) {
                    assetTableView->verticalScrollBar()->setValue(ctx.scrollPosition);
                }
            }

            // Note: Asset selection restoration would need to wait for model to load
            // This is a future enhancement
        });
    } else {
        // No saved context - ensure the asset views start at the top for every new folder
        QTimer::singleShot(0, this, [this](){
            if (assetGridView) assetGridView->scrollToTop();
            if (assetTableView) assetTableView->scrollToTop();
        });
    }

#ifdef Q_OS_WIN
    // Log memory usage before/after applying folder change
    qDebug() << "[NAV] Folder change applied to id=" << folderId << ", working set (MB)=" << (qulonglong)currentWorkingSetMB();
    QTimer::singleShot(1000, this, [](){ qDebug() << "[NAV] Post-change working set (MB)=" << (qulonglong)currentWorkingSetMB(); });
#endif

    clearSelection();
    updateInfoPanel();

    // Save as last active folder
    ContextPreserver::instance().saveLastActiveFolder(folderId);

    amUpdateNavigationButtons();

    // Sync tree selection to show current folder
    if (folderModel && folderTreeView && folderTreeView->selectionModel()) {
        // Find the index for this folder
        QModelIndex idx = folderModel->findIndexById(folderId);

        if (idx.isValid()) {
            // Expand all ancestors to make the folder visible
            QModelIndex parent = idx.parent();
            while (parent.isValid()) {
                folderTreeView->setExpanded(parent, true);
                parent = parent.parent();
            }

            // Set the selection - this moves the highlight
            // Note: onFolderSelected is connected to 'clicked' signal, not selection changes,
            // so this won't trigger recursive navigation
            folderTreeView->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
            folderTreeView->scrollTo(idx, QAbstractItemView::PositionAtCenter);
        }
    }
}

void MainWindow::amUpdateNavigationButtons()
{
    if (amBackButton) {
        amBackButton->setEnabled(amNavigationIndex > 0 && !amNavigationHistory.isEmpty());
    }

    if (amUpButton) {
        bool canGoUp = false;
        if (assetsModel && folderModel) {
            int currentFolderId = assetsModel->folderId();
            int rootId = folderModel->rootId();
            // Can go up if we're not at root
            if (currentFolderId > 0 && currentFolderId != rootId) {
                canGoUp = true;
            }
        }
        amUpButton->setEnabled(canGoUp);
    }
}


void MainWindow::onEmptySpaceContextMenu(const QPoint &pos)
{
    Q_UNUSED(pos);
    clearSelection();
}

void MainWindow::showPreview(int index)
{
    qDebug() << "[MainWindow::showPreview] Called with index:" << index;

    if (index < 0 || index >= assetsModel->rowCount(QModelIndex())) {
        qWarning() << "[MainWindow::showPreview] Invalid index:" << index << "rowCount:" << assetsModel->rowCount(QModelIndex());
        return;
    }

    previewIndex = index;
    QModelIndex modelIndex = assetsModel->index(index, 0);

    // Keep underlying selection in sync while navigating in overlay
    // so that when the overlay is closed, the last-viewed item is already selected.
    if (assetGridView && assetGridView->model() == assetsModel) {
        if (QItemSelectionModel* sel = assetGridView->selectionModel()) {
            sel->setCurrentIndex(modelIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        } else {
            assetGridView->setCurrentIndex(modelIndex);
        }
        assetGridView->scrollTo(modelIndex, QAbstractItemView::PositionAtCenter);
    }
    if (assetTableView && assetTableView->model()) {
        // Table view may use a different model; fall back to row index
        QModelIndex tIdx = assetTableView->model()->index(index, 0);
        if (tIdx.isValid()) {
            if (QItemSelectionModel* sel = assetTableView->selectionModel()) {
                sel->setCurrentIndex(tIdx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            } else {
                assetTableView->setCurrentIndex(tIdx);
            }
            assetTableView->scrollTo(tIdx, QAbstractItemView::PositionAtCenter);
        }
    }

    QString filePath = modelIndex.data(AssetsModel::FilePathRole).toString();
    QString fileName = modelIndex.data(AssetsModel::FileNameRole).toString();
    QString fileType = modelIndex.data(AssetsModel::FileTypeRole).toString();
    bool isSequence = modelIndex.data(AssetsModel::IsSequenceRole).toBool();

    const bool useImageOverlay = !isSequence && isImageFile(fileType) && !isVideoFile(fileType);

    if (useImageOverlay) {
        if (previewOverlay && previewOverlay->isVisible()) {
            previewOverlay->stopPlayback();
            previewOverlay->hide();
        }
        if (!imagePreviewOverlay) {
            imagePreviewOverlay = new ImagePreviewOverlay(this);
            imagePreviewOverlay->setGeometry(geometry());
            connect(imagePreviewOverlay, &ImagePreviewOverlay::closed, this, &MainWindow::closePreview);
            connect(imagePreviewOverlay, &ImagePreviewOverlay::navigateRequested, this, &MainWindow::changePreview);
        } else {
            imagePreviewOverlay->stopPlayback();
        }
        imagePreviewOverlay->showImage(filePath, fileName, fileType);
        return;
    }

    if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
        imagePreviewOverlay->stopPlayback();
        imagePreviewOverlay->hide();
    }

    if (!previewOverlay) {
        previewOverlay = new PreviewOverlay(this);
        // Center overlay to the app window instead of screen top-left
        previewOverlay->setGeometry(geometry());

        connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
        connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changePreview);
    } else {
        // Stop any playing media before loading new content
        // Note: This may be called twice during navigation (once in changePreview, once here)
        // but that's safe - stopPlayback is idempotent
        previewOverlay->stopPlayback();
    }

    if (isSequence) {
        // Get sequence information
        QString sequencePattern = modelIndex.data(AssetsModel::SequencePatternRole).toString();
        int startFrame = modelIndex.data(AssetsModel::SequenceStartFrameRole).toInt();
        int endFrame = modelIndex.data(AssetsModel::SequenceEndFrameRole).toInt();
        int frameCount = modelIndex.data(AssetsModel::SequenceFrameCountRole).toInt();

        // Reconstruct frame paths from first frame path and pattern
        QStringList framePaths = reconstructSequenceFramePaths(filePath, startFrame, endFrame);

        if (framePaths.isEmpty()) {
            qWarning() << "[MainWindow::showPreview] No frame paths reconstructed! Cannot show sequence.";
            QMessageBox::warning(this, "Error", "Failed to reconstruct sequence frame paths.");
            return;
        }

        previewOverlay->showSequence(framePaths, sequencePattern, startFrame, endFrame);
    } else {
        previewOverlay->showAsset(filePath, fileName, fileType);
    }
}

void MainWindow::closePreview()
{
    // Preserve the last asset index for restoring Asset Manager focus
    const int lastAssetIndex = previewIndex;
    previewIndex = -1;

    if (previewOverlay) {
        // Stop any playback (video, fallback, sequence) before hiding/deleting
        previewOverlay->stopPlayback();
        previewOverlay->hide();
        previewOverlay->deleteLater();
        previewOverlay = nullptr;
    }
    if (imagePreviewOverlay) {
        imagePreviewOverlay->stopPlayback();
        imagePreviewOverlay->hide();
        imagePreviewOverlay->deleteLater();
        imagePreviewOverlay = nullptr;
    }

    // 1) If preview was opened from File Manager, restore focus/selection there
    if (fmOverlaySourceView && fmOverlayCurrentIndex.isValid()) {
        if (QItemSelectionModel *sel = fmOverlaySourceView->selectionModel()) {
            sel->setCurrentIndex(fmOverlayCurrentIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        } else {
            fmOverlaySourceView->setCurrentIndex(fmOverlayCurrentIndex);
        }
        fmOverlaySourceView->setFocus();
        return; // Done
    }

    // 2) Otherwise, restore focus/selection to Asset Manager
    if (lastAssetIndex >= 0) {
        if (isGridMode && assetGridView && assetsModel) {
            QModelIndex idx = assetsModel->index(lastAssetIndex, 0);
            if (idx.isValid()) {
                assetGridView->setCurrentIndex(idx);
                assetGridView->setFocus();
            }
        } else if (assetTableView && assetTableView->model()) {
            QModelIndex idx = assetTableView->model()->index(lastAssetIndex, 0);
            if (idx.isValid()) {
                assetTableView->setCurrentIndex(idx);
                assetTableView->setFocus();
            }
        }
    }
}

void MainWindow::changePreview(int delta)
{
    if (previewIndex < 0) return;

    // Stop playback ONCE before searching for next file
    if (previewOverlay && previewOverlay->isVisible()) {
        previewOverlay->stopPlayback();
    }
    if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
        imagePreviewOverlay->stopPlayback();
    }

    const int rowCount = assetsModel->rowCount(QModelIndex());
    const int step = (delta > 0) ? 1 : -1;
    int searchIndex = previewIndex + delta;

    // Search for next viewable file
    while (searchIndex >= 0 && searchIndex < rowCount) {
        QModelIndex modelIndex = assetsModel->index(searchIndex, 0);
        if (!modelIndex.isValid()) {
            searchIndex += step;
            continue;
        }

        // Check if it's a sequence (always viewable) or a viewable file type
        bool isSequence = modelIndex.data(AssetsModel::IsSequenceRole).toBool();
        QString fileType = modelIndex.data(AssetsModel::FileTypeRole).toString();

        if (isSequence || isPreviewOverlayViewable(fileType)) {
            showPreview(searchIndex);
            return;
        }

        // Not viewable, continue searching
        searchIndex += step;
    }

    // No viewable file found in the direction - do nothing
}


void MainWindow::changeFmPreview(int delta)
{
    if ((!previewOverlay || !previewOverlay->isVisible()) &&
        (!imagePreviewOverlay || !imagePreviewOverlay->isVisible())) {
        return;
    }

    // Stop playback ONCE before searching for next file
    if (previewOverlay && previewOverlay->isVisible()) {
        previewOverlay->stopPlayback();
    }
    if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
        imagePreviewOverlay->stopPlayback();
    }

    auto showFmPath = [this](const QString &path) {
        QFileInfo info(path);
        const QString ext = info.suffix();
        const bool isImage = FileUtils::isImageFile(ext);
        const bool isVideo = FileUtils::isVideoFile(ext);

        if (isImage && !isVideo) {
            if (previewOverlay && previewOverlay->isVisible()) {
                previewOverlay->stopPlayback();
                previewOverlay->hide();
            }
            if (!imagePreviewOverlay) {
                imagePreviewOverlay = new ImagePreviewOverlay(this);
                imagePreviewOverlay->setGeometry(geometry());
                connect(imagePreviewOverlay, &ImagePreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(imagePreviewOverlay, &ImagePreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
            }
            imagePreviewOverlay->showImage(path, info.fileName(), info.suffix());
        } else {
            if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
                imagePreviewOverlay->stopPlayback();
                imagePreviewOverlay->hide();
            }
            if (!previewOverlay) {
                previewOverlay = new PreviewOverlay(this);
                previewOverlay->setGeometry(geometry());
                connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
            }
            previewOverlay->showAsset(path, info.fileName(), info.suffix());
        }
    };

    // If preview was opened from secondary pane, use its data for navigation
    if (fmOverlayFromSecondaryPane && fmSecondaryPane) {
        QModelIndex cur = fmOverlayCurrentIndex;
        if (!cur.isValid()) {
            cur = fmSecondaryPane->currentIndex();
            if (!cur.isValid()) return;
            fmOverlayCurrentIndex = QPersistentModelIndex(cur);
        }
        
        // Get the model and navigate within secondary pane
        QAbstractItemModel* model = const_cast<QAbstractItemModel*>(cur.model());
        if (!model) return;
        
        int searchRow = cur.row() + delta;
        const int rowCount = model->rowCount(cur.parent());
        const int step = (delta > 0) ? 1 : -1;
        
        while (searchRow >= 0 && searchRow < rowCount) {
            QModelIndex next = model->index(searchRow, 0, cur.parent());
            if (!next.isValid()) {
                searchRow += step;
                continue;
            }
            
            // Get file path from secondary pane
            QString path = fmSecondaryPane->pathForIndex(next);
            if (!path.isEmpty()) {
                QFileInfo fi(path);
                if (fi.exists() && fi.isFile() && isPreviewOverlayViewable(fi.suffix())) {
                    // Found a viewable file - update context and show it
                    fmOverlayCurrentIndex = QPersistentModelIndex(next);
                    fmSecondaryPane->setCurrentIndex(next);
                    showFmPath(path);
                    return;
                }
            }
            searchRow += step;
        }
        return;  // No viewable file found
    }

    // Primary pane navigation (original logic)
    QModelIndex cur = fmOverlayCurrentIndex;
    if (!cur.isValid()) {
        // fallback: try current selection from focused view
        if (fmGridView && fmGridView->hasFocus()) cur = fmGridView->currentIndex();
        else if (fmListView && fmListView->hasFocus()) cur = fmListView->currentIndex();
        if (!cur.isValid()) return;
        cur = cur.sibling(cur.row(), 0);
        fmOverlayCurrentIndex = QPersistentModelIndex(cur);
        fmOverlaySourceView = (fmGridView && fmGridView->hasFocus()) ? static_cast<QAbstractItemView*>(fmGridView) : static_cast<QAbstractItemView*>(fmListView);
    }
    QAbstractItemModel* model = const_cast<QAbstractItemModel*>(cur.model());
    if (!model) return;

    // Search for next viewable file in the direction of delta
    int searchRow = cur.row() + delta;
    const int rowCount = model->rowCount(cur.parent());
    const int step = (delta > 0) ? 1 : -1;

    // Search up to the end/beginning of the list
    while (searchRow >= 0 && searchRow < rowCount) {
        QModelIndex next = model->index(searchRow, 0, cur.parent());
        if (!next.isValid()) {
            searchRow += step;
            continue;
        }

        // Check if this is a sequence representative (always viewable)
        if (fmProxyModel && fmGroupSequences && next.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(next)) {
            auto info = fmProxyModel->infoForProxyIndex(next);
            QStringList frames = reconstructSequenceFramePaths(info.reprPath, info.start, info.end);
            if (!frames.isEmpty()) {
                // Update context
                fmOverlayCurrentIndex = QPersistentModelIndex(next);
                if (fmOverlaySourceView) {
                    fmOverlaySourceView->setCurrentIndex(next);
                    fmOverlaySourceView->scrollTo(next, QAbstractItemView::PositionAtCenter);
                }

                // Show sequence (stopPlayback already called at function start)
                int pad = 0;
                auto m = SequenceDetector::mainPattern().match(QFileInfo(info.reprPath).fileName());
                if (m.hasMatch()) pad = m.captured(3).length(); else pad = QString::number(info.start).length();
                QString s0 = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
                QString s1 = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
                QString seqName = QString("%1.[%2-%3].%4").arg(info.base, s0, s1, info.ext);
                if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
                    imagePreviewOverlay->hide();
                }
                if (!previewOverlay) {
                    previewOverlay = new PreviewOverlay(this);
                    previewOverlay->setGeometry(geometry());
                    connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
                    connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
                }
                previewOverlay->showSequence(frames, seqName, info.start, info.end);
                return;
            }
        }

        // Map to source if needed and check if file is viewable
        QModelIndex srcIdx = next;
        if (fmProxyModel && next.model() == fmProxyModel)
            srcIdx = fmProxyModel->mapToSource(next);
        QString path = fmDirModel ? fmDirModel->filePath(srcIdx) : QString();
        if (!path.isEmpty()) {
            QFileInfo fi(path);
            if (fi.exists() && fi.isFile() && isPreviewOverlayViewable(fi.suffix())) {
                // Found a viewable file - update context and show it
                fmOverlayCurrentIndex = QPersistentModelIndex(next);
                if (fmOverlaySourceView) {
                    fmOverlaySourceView->setCurrentIndex(next);
                    fmOverlaySourceView->scrollTo(next, QAbstractItemView::PositionAtCenter);
                }

                // Show asset (stopPlayback already called at function start)
                showFmPath(path);
                return;
            }
        }

        // Not viewable, continue searching
        searchRow += step;
    }

    // No viewable file found in the direction - do nothing
}


QItemSelectionModel* MainWindow::getCurrentSelectionModel()
{
    return isGridMode ? assetGridView->selectionModel() : assetTableView->selectionModel();
}

void MainWindow::updateInfoPanel()
{
    // Use selectedRows() to get one index per row, avoiding duplicate counts in table view
    QModelIndexList selected = isGridMode
        ? getCurrentSelectionModel()->selectedIndexes()
        : getCurrentSelectionModel()->selectedRows();

    if (selected.isEmpty()) {
        infoFileName->setText("No selection");
        infoFilePath->clear();
        infoFileSize->clear();
        infoFileType->clear();
        infoDimensions->clear();
        infoCreated->clear();
        infoModified->clear();
        infoPermissions->clear();
        infoRatingLabel->setVisible(false);
        infoRatingWidget->setVisible(false);
        infoTags->clear();
        if (versionTable) { versionTable->setRowCount(0); }
        if (versionsTitleLabel) { versionsTitleLabel->setText("Version History"); }
        if (revertVersionButton) { revertVersionButton->setEnabled(false); }

        return;
    }

    if (selected.size() == 1) {
        QModelIndex index = selected.first();
        QString fileName = index.data(AssetsModel::FileNameRole).toString();
        QString filePath = index.data(AssetsModel::FilePathRole).toString();
        qint64 fileSize = index.data(AssetsModel::FileSizeRole).toLongLong();
        QString fileType = index.data(AssetsModel::FileTypeRole).toString();
        QDateTime modified = index.data(AssetsModel::LastModifiedRole).toDateTime();
        int rating = index.data(AssetsModel::RatingRole).toInt();
        bool isSequence = index.data(AssetsModel::IsSequenceRole).toBool();

        infoFileName->setText(fileName);
        infoFilePath->setText(filePath);

        QFileInfo fileInfo(filePath);

        // Format file size
        QString sizeStr;
        if (fileSize < 1024) {
            sizeStr = QString::number(fileSize) + " B";
        } else if (fileSize < 1024 * 1024) {
            sizeStr = QString::number(fileSize / 1024.0, 'f', 1) + " KB";
        } else if (fileSize < 1024 * 1024 * 1024) {
            sizeStr = QString::number(fileSize / (1024.0 * 1024.0), 'f', 1) + " MB";
        } else {
            sizeStr = QString::number(fileSize / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
        }
        infoFileSize->setText("Size: " + sizeStr.toLower());

        infoFileType->setText("Type: " + fileType.toUpper());

        // Extract dimensions for images and videos
        QString dimensionsStr;
        if (isSequence) {
            int frameCount = index.data(AssetsModel::SequenceFrameCountRole).toInt();
            int startFrame = index.data(AssetsModel::SequenceStartFrameRole).toInt();
            int endFrame = index.data(AssetsModel::SequenceEndFrameRole).toInt();
            bool hasGaps = index.data(AssetsModel::SequenceHasGapsRole).toBool();
            int gapCount = index.data(AssetsModel::SequenceGapCountRole).toInt();
            QString version = index.data(AssetsModel::SequenceVersionRole).toString();

            // Try to get dimensions from first frame
            QImageReader reader(filePath);
            if (reader.canRead()) {
                QSize size = reader.size();
                dimensionsStr = QString("Dimensions: %1 x %2 (%3 frames: %4-%5)")
                    .arg(size.width()).arg(size.height())
                    .arg(frameCount).arg(startFrame).arg(endFrame);
            } else {
                dimensionsStr = QString("Sequence: %1 frames (%2-%3)")
                    .arg(frameCount).arg(startFrame).arg(endFrame);
            }

            // Add gap warning if present
            if (hasGaps) {
                int expectedFrames = endFrame - startFrame + 1;
                int missingFrames = expectedFrames - frameCount;
                dimensionsStr += QString("\n⚠ WARNING: %1 gap(s), %2 missing frame(s)")
                    .arg(gapCount).arg(missingFrames);
            }

            // Add version info if present
            if (!version.isEmpty()) {
                dimensionsStr += QString("\nVersion: %1").arg(version);
            }
        } else {
            // Check if it's an image
            QStringList imageExts = {"jpg", "jpeg", "png", "gif", "bmp", "tiff", "tif", "webp",
                                     "exr", "hdr", "psd", "psb", "tga", "dng", "cr2", "cr3",
                                     "nef", "arw", "orf", "rw2", "pef", "srw", "raf", "raw"};

            if (imageExts.contains(fileType.toLower())) {
                QImageReader reader(filePath);
                if (reader.canRead()) {
                    QSize size = reader.size();
                    QString format = reader.format();
                    dimensionsStr = QString("Dimensions: %1 x %2 (%3)")
                        .arg(size.width()).arg(size.height()).arg(QString(format).toUpper());
                } else {
                    dimensionsStr = "Dimensions: Unable to read";
                }
            }
            // Check if it's a video
            else {
                QStringList videoExts = {"mp4", "mov", "avi", "mkv", "wmv", "flv", "webm",
                                        "m4v", "mpg", "mpeg", "3gp", "mts", "m2ts", "mxf"};
                if (videoExts.contains(fileType.toLower())) {
                    // TODO: Extract video metadata using GStreamer
                    // For now, just show "Video file"
                    dimensionsStr = "Video file";
                }
            }
        }

        if (!dimensionsStr.isEmpty()) {
            infoDimensions->setText(dimensionsStr);
            infoDimensions->setVisible(true);
        } else {
            infoDimensions->clear();
            infoDimensions->setVisible(false);
        }

        // Creation and modification dates
        if (fileInfo.exists()) {
            QDateTime created = fileInfo.birthTime();
            if (created.isValid()) {
                infoCreated->setText("Created: " + created.toString("dd-MM-yyyy"));
                infoCreated->setVisible(true);
            } else {
                infoCreated->clear();
                infoCreated->setVisible(false);
            }

            infoModified->setText("Modified: " + modified.toString("dd-MM-yyyy"));

            // File permissions
            QStringList perms;
            if (fileInfo.isReadable()) perms << "R";
            if (fileInfo.isWritable()) perms << "W";
            if (fileInfo.isExecutable()) perms << "X";
            if (fileInfo.isHidden()) perms << "Hidden";

            infoPermissions->setText("Permissions: " + perms.join(", "));
            infoPermissions->setVisible(true);
        } else {
            infoCreated->clear();
            infoCreated->setVisible(false);
            infoModified->setText("Modified: File not found");
            infoPermissions->clear();
            infoPermissions->setVisible(false);
        }

        // Show rating widget
        infoRatingLabel->setVisible(true);
        infoRatingWidget->setVisible(true);
        infoRatingWidget->setReadOnly(false);
        infoRatingWidget->setRating(rating);

        // Load tags for this asset
        int assetId = index.data(AssetsModel::IdRole).toInt();
        QStringList tags = DB::instance().tagsForAsset(assetId);
        if (tags.isEmpty()) {
            infoTags->setText("Tags: None");


        // Load version history for this asset
        reloadVersionHistory();


        } else {
            infoTags->setText("Tags: " + tags.join(", "));

        // Load version history for this asset
        reloadVersionHistory();


        }
    } else {
        if (versionTable) { versionTable->setRowCount(0); }
        if (versionsTitleLabel) { versionsTitleLabel->setText("Version History"); }
        if (revertVersionButton) { revertVersionButton->setEnabled(false); }

        infoFileName->setText(QString("%1 assets selected").arg(selected.size()));
        infoFilePath->clear();

        infoFileSize->clear();
        infoFileType->clear();
        infoDimensions->clear();
        infoCreated->clear();
        infoModified->clear();
        infoPermissions->clear();
        infoRatingLabel->setVisible(false);
        infoRatingWidget->setVisible(false);
        infoTags->clear();
    }
}

void MainWindow::onRatingChanged(int rating)
{
    // Get currently selected asset
    QModelIndexList selected = getCurrentSelectionModel()->selectedIndexes();
    if (selected.size() != 1) return;

    int assetId = selected.first().data(AssetsModel::IdRole).toInt();

    // Update rating in database
    if (DB::instance().setAssetsRating({assetId}, rating)) {
        assetsModel->reload();
        statusBar()->showMessage(QString("Rating set to %1 star%2").arg(rating).arg(rating == 1 ? "" : "s"), 2000);
    } else {
        QMessageBox::warning(this, "Error", "Failed to set rating");
    }
}

void MainWindow::updateSelectionInfo()
{
    // Update internal selection tracking
    selectedAssetIds.clear();
    QModelIndexList selected = getCurrentSelectionModel()->selectedIndexes();
    for (const QModelIndex &index : selected) {
        int assetId = index.data(AssetsModel::IdRole).toInt();
        selectedAssetIds.insert(assetId);
    }
}

QSet<int> MainWindow::getSelectedAssetIds() const
{
    return selectedAssetIds;
}

int MainWindow::getAnchorIndex() const
{
    return anchorIndex;
}

void MainWindow::selectAsset(int assetId, int index, Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(assetId);

    if (modifiers & Qt::ControlModifier) {
        // Toggle selection
        QModelIndex idx = assetsModel->index(index, 0);
        assetGridView->selectionModel()->select(idx, QItemSelectionModel::Toggle);
    } else if (modifiers & Qt::ShiftModifier) {
        // Range selection
        if (anchorIndex >= 0) {
            selectRange(anchorIndex, index);
        } else {
            selectSingle(assetId, index);
        }
    } else {
        selectSingle(assetId, index);
    }
}

void MainWindow::selectSingle(int assetId, int index)
{
    Q_UNUSED(assetId);
    assetGridView->selectionModel()->clearSelection();
    QModelIndex idx = assetsModel->index(index, 0);
    assetGridView->selectionModel()->select(idx, QItemSelectionModel::Select);
    anchorIndex = index;
}

void MainWindow::toggleSelection(int assetId, int index)
{
    Q_UNUSED(assetId);
    QModelIndex idx = assetsModel->index(index, 0);
    assetGridView->selectionModel()->select(idx, QItemSelectionModel::Toggle);
}

void MainWindow::selectRange(int fromIndex, int toIndex)
{
    assetGridView->selectionModel()->clearSelection();

    int start = qMin(fromIndex, toIndex);
    int end = qMax(fromIndex, toIndex);

    for (int i = start; i <= end; i++) {
        QModelIndex idx = assetsModel->index(i, 0);
        assetGridView->selectionModel()->select(idx, QItemSelectionModel::Select);
    }
}

void MainWindow::clearSelection()
{
    assetGridView->selectionModel()->clearSelection();
    selectedAssetIds.clear();
    anchorIndex = -1;
    currentAssetId = -1;
}

void MainWindow::applyFilters()
{
    // Filters are applied automatically via:
    // - Search box (onSearchTextChanged)
    // - Rating filter (connected to model)
    // - Tags (Filter by Tags button)
    // This button is kept for future batch filter application if needed
    statusBar()->showMessage("Filters are active", 2000);
}

void MainWindow::clearFilters()
{
    searchBox->clear();
    ratingFilter->setCurrentIndex(0);
    tagsListView->clearSelection();

    // Clear tag filter in model
    assetsModel->setSelectedTagNames(QStringList());

    statusBar()->showMessage("Filters cleared", 2000);
}

void MainWindow::onSearchTextChanged(const QString &text)
{
    assetsModel->setSearchQuery(text);

    if (text.isEmpty()) {
        statusBar()->showMessage("Search cleared", 1000);
    } else {
        statusBar()->showMessage(QString("Searching for: %1").arg(text), 2000);
    }
}

void MainWindow::onCreateTag()
{
    bool ok;
    QString tagName = QInputDialog::getText(this, "Create Tag", "Tag name:", QLineEdit::Normal, "", &ok);

    if (ok && !tagName.isEmpty()) {
        int tagId = tagsModel->createTag(tagName);
        if (tagId > 0) {
            statusBar()->showMessage(QString("Tag '%1' created").arg(tagName), 2000);
        } else {
            QMessageBox::warning(this, "Error", "Failed to create tag. Tag may already exist.");
        }
    }
}

void MainWindow::onApplyTags()
{
    // Get selected tags
    QModelIndexList selectedTagIndexes = tagsListView->selectionModel()->selectedIndexes();
    if (selectedTagIndexes.isEmpty()) {
        statusBar()->showMessage("No tags selected", 2000);
        return;
    }

    // Get selected assets
    QSet<int> assetIds = getSelectedAssetIds();
    if (assetIds.isEmpty()) {
        statusBar()->showMessage("No assets selected", 2000);
        return;
    }

    // Collect tag IDs
    QList<int> tagIds;
    for (const QModelIndex &index : selectedTagIndexes) {
        int tagId = index.data(TagsModel::IdRole).toInt();
        if (tagId > 0) {
            tagIds.append(tagId);
        }
    }

    if (tagIds.isEmpty()) {
        return;
    }

    // Apply tags to assets
    QList<int> assetIdList = assetIds.values();
    if (DB::instance().assignTagsToAssets(assetIdList, tagIds)) {
        statusBar()->showMessage(QString("Applied %1 tag(s) to %2 asset(s)").arg(tagIds.size()).arg(assetIds.size()), 3000);
        updateInfoPanel();
    } else {
        QMessageBox::warning(this, "Error", "Failed to apply tags");
    }
}

void MainWindow::onFilterByTags()
{
    // Get selected tags
    QModelIndexList selectedTagIndexes = tagsListView->selectionModel()->selectedIndexes();
    if (selectedTagIndexes.isEmpty()) {
        assetsModel->setSelectedTagNames(QStringList());
        statusBar()->showMessage("Tag filter cleared", 2000);
        return;
    }

    QStringList tagNames;
    for (const QModelIndex &index : selectedTagIndexes) {
        QString tagName = index.data(TagsModel::NameRole).toString();
        if (!tagName.isEmpty()) tagNames.append(tagName);
    }
    if (tagNames.isEmpty()) return;

    int mode = tagFilterModeCombo->currentIndex(); // 0 = AND, 1 = OR
    QString modeText = (mode == AssetsModel::And) ? "AND" : "OR";

    assetsModel->setSelectedTagNames(tagNames);
    assetsModel->setTagFilterMode(mode);

    QString message = (tagNames.size() == 1)
        ? QString("Filtering by tag: %1").arg(tagNames.first())
        : QString("Filtering by %1 tag(s) (%2 logic)").arg(tagNames.size()).arg(modeText);
    statusBar()->showMessage(message, 3000);
}

#if 0 // cleanup: removed misplaced version history block

            iconItem->setText(" 39d7");

            if (tagsModel->deleteTag(tagId)) {
                statusBar()->showMessage(QString("Tag '%1' deleted").arg(tagName), 2000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to delete tag.");
            }
        }
    } else if (selected == mergeAction) {
        // Merge tag into another
        QVector<QPair<int, QString>> allTags = DB::instance().listTags();
        QStringList tagNames;
        QList<int> tagIds;

        for (const auto& tag : allTags) {
            if (tag.first != tagId) { // Exclude current tag
                tagNames.append(tag.second);
                tagIds.append(tag.first);
            }
        }

        if (tagNames.isEmpty()) {
            QMessageBox::information(this, "Merge Tag", "No other tags available to merge into.");
            return;
        }

        bool ok;
        QString targetTagName = QInputDialog::getItem(this, "Merge Tag",
            QString("Merge tag '%1' into:").arg(tagName),
            tagNames, 0, false, &ok);

        if (ok && !targetTagName.isEmpty()) {
            int targetTagId = tagIds[tagNames.indexOf(targetTagName)];

            QMessageBox::StandardButton reply = QMessageBox::question(this, "Merge Tag",
                QString("Merge tag '%1' into '%2'?\n\nAll assets tagged with '%1' will be tagged with '%2' instead, and '%1' will be deleted.")
                    .arg(tagName).arg(targetTagName),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                if (DB::instance().mergeTags(tagId, targetTagId)) {
                    tagsModel->reload();
                    assetsModel->reload();
                    statusBar()->showMessage(QString("Tag '%1' merged into '%2'").arg(tagName).arg(targetTagName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to merge tags.");
                }
            }
        }
#endif // cleanup

void MainWindow::onTagContextMenu(const QPoint &pos)
{
    QModelIndex index = tagsListView->indexAt(pos);
    if (!index.isValid()) return;

    int tagId = index.data(TagsModel::IdRole).toInt();
    QString tagName = index.data(TagsModel::NameRole).toString();

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #2a2a2a; color: #ffffff; border: 1px solid #444; }"
        "QMenu::item:selected { background-color: #3a3a3a; }"
    );

    QAction *renameAction = menu.addAction("Rename Tag");
    QAction *deleteAction = menu.addAction("Delete Tag");
    menu.addSeparator();
    QAction *mergeAction = menu.addAction("Merge Into...");

    QAction *selected = menu.exec(tagsListView->mapToGlobal(pos));

    if (selected == renameAction) {
        bool ok;
        QString newName = QInputDialog::getText(this, "Rename Tag",
            QString("Rename tag '%1' to:").arg(tagName),
            QLineEdit::Normal, tagName, &ok);
        if (ok && !newName.isEmpty() && newName != tagName) {
            if (tagsModel->renameTag(tagId, newName)) {
                statusBar()->showMessage(QString("Tag renamed to '%1'").arg(newName), 2000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to rename tag. Tag name may already exist.");
            }
        }
    } else if (selected == deleteAction) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Delete Tag",
            QString("Are you sure you want to delete tag '%1'?\n\nThis will remove the tag from all assets.").arg(tagName),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            if (tagsModel->deleteTag(tagId)) {
                statusBar()->showMessage(QString("Tag '%1' deleted").arg(tagName), 2000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to delete tag.");
            }
        }
    } else if (selected == mergeAction) {
        QVector<QPair<int, QString>> allTags = DB::instance().listTags();
        QStringList tagNamesList;
        QList<int> tagIds;
        for (const auto& tag : allTags) {
            if (tag.first != tagId) {
                tagNamesList.append(tag.second);
                tagIds.append(tag.first);
            }
        }
        if (tagNamesList.isEmpty()) {
            QMessageBox::information(this, "Merge Tag", "No other tags available to merge into.");
            return;
        }
        bool ok;
        QString targetTagName = QInputDialog::getItem(this, "Merge Tag",
            QString("Merge tag '%1' into:").arg(tagName),
            tagNamesList, 0, false, &ok);
        if (ok && !targetTagName.isEmpty()) {
            int targetTagId = tagIds[tagNamesList.indexOf(targetTagName)];
            QMessageBox::StandardButton reply = QMessageBox::question(this, "Merge Tag",
                QString("Merge tag '%1' into '%2'?\n\nAll assets tagged with '%1' will be tagged with '%2' instead, and '%1' will be deleted.")
                    .arg(tagName).arg(targetTagName),
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                if (DB::instance().mergeTags(tagId, targetTagId)) {
                    tagsModel->reload();
                    assetsModel->reload();
                    statusBar()->showMessage(QString("Tag '%1' merged into '%2'").arg(tagName).arg(targetTagName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to merge tags.");
                }
            }
        }
    }
}


void MainWindow::updateTagButtonStates()
{
    bool hasSelectedTags = !tagsListView->selectionModel()->selectedIndexes().isEmpty();
    bool hasSelectedAssets = !getSelectedAssetIds().isEmpty();

    // Apply button: enabled only when both tags AND assets are selected
    applyTagsBtn->setEnabled(hasSelectedTags && hasSelectedAssets);

    // Filter button: enabled when tags are selected
    filterByTagsBtn->setEnabled(hasSelectedTags);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        statusBar()->showMessage("Drop files here to import...");
    } else {
        event->ignore();
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event);
    statusBar()->clearMessage();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    statusBar()->clearMessage();

    const QMimeData *mimeData = event->mimeData();

    if (mimeData->hasUrls()) {
        QStringList filePaths;
        QStringList folderPaths;
        QList<QUrl> urls = mimeData->urls();
        
        for (const QUrl &url : urls) {
            if (url.isLocalFile()) {
                QString path = url.toLocalFile();
                QFileInfo info(path);

                if (info.isFile()) {
                    filePaths.append(path);
                } else if (info.isDir()) {
                    // Keep directories separate to preserve structure
                    folderPaths.append(path);
                }
            }
        }

        // Check if we're on the Project Manager tab
        if (mainTabs && mainTabs->currentWidget() == projectManagerPage) {
            // Handle Project Manager drops - only folders make sense here
            if (folderPaths.size() == 1) {
                QString folderPath = folderPaths.first();
                QString folderName = QDir(folderPath).dirName();
                pmImportToProject(folderName, folderPath);
                event->acceptProposedAction();
                return;
            } else if (folderPaths.size() > 1) {
                statusBar()->showMessage("Please drop only one folder at a time for Project Manager", 3000);
                event->ignore();
                return;
            } else {
                statusBar()->showMessage("Drop a folder to create a project", 3000);
                event->ignore();
                return;
            }
        }

        // Asset Manager drop handling
        // Get currently selected folder ID
        QModelIndex currentFolderIndex = folderTreeView->currentIndex();
        int parentFolderId = 0;
        if (currentFolderIndex.isValid()) {
            parentFolderId = folderModel->data(currentFolderIndex, VirtualFolderTreeModel::IdRole).toInt();
        }
        if (parentFolderId <= 0) {
            parentFolderId = folderModel->rootId();
        }

        int totalImported = 0;

        // Create and show import progress dialog
        if (!importProgressDialog) {
            importProgressDialog = new ImportProgressDialog(this);
        }
        importProgressDialog->show();
        importProgressDialog->raise();
        importProgressDialog->activateWindow();

        // Disconnect importFinished temporarily to prevent premature dialog closure
        disconnect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);

        // Import folders with structure preservation
        for (const QString &folderPath : folderPaths) {
            if (importer->importFolder(folderPath, parentFolderId)) {
                totalImported++;
            }
        }

        // Import individual files
        if (!filePaths.isEmpty()) {
            importFiles(filePaths);
            totalImported += filePaths.size();
        }

        // Reconnect importFinished signal
        connect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);

        // Manually trigger import complete
        onImportComplete();

        if (totalImported > 0) {
            statusBar()->showMessage(QString("Import complete: %1 item(s)").arg(totalImported), 3000);
        } else {
            statusBar()->showMessage("No valid files to import", 3000);
        }

        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MainWindow::importFiles(const QStringList &filePaths)
{
    if (!folderTreeView->currentIndex().isValid()) {
        QMessageBox::warning(this, "No Folder Selected", "Please select a folder before importing files.");
        return;
    }

    int folderId = folderTreeView->currentIndex().data(VirtualFolderTreeModel::IdRole).toInt();

    // Create and show import progress dialog
    if (!importProgressDialog) {
        importProgressDialog = new ImportProgressDialog(this);
    }
    importProgressDialog->show();
    importProgressDialog->raise();
    importProgressDialog->activateWindow();

    // Start import
    importer->importFiles(filePaths, folderId);
}

void MainWindow::onImportProgress(int current, int total)
{
    // Update progress dialog
    if (importProgressDialog) {
        importProgressDialog->setProgress(current, total);
    }

    // Also update status bar
    statusBar()->showMessage(QString("Importing: %1 of %2 files...").arg(current).arg(total));
}

void MainWindow::onImportFileChanged(const QString& fileName)
{
    // Update progress dialog with current file
    if (importProgressDialog) {
        importProgressDialog->setCurrentFile(fileName);
    }
}

void MainWindow::onImportFolderChanged(const QString& folderName)
{
    // Update progress dialog with current folder
    if (importProgressDialog) {
        importProgressDialog->setCurrentFolder(folderName);
    }
}

void MainWindow::onImportComplete()
{
    // Close and delete the import progress dialog
    if (importProgressDialog) {
        importProgressDialog->accept();  // Close the dialog
        importProgressDialog->deleteLater();
        importProgressDialog = nullptr;
    }

    statusBar()->showMessage("Import complete", 3000);

    // Reload assets model to show new imports
    assetsModel->reload();

    // Warm live preview cache for all assets in current folder
    QList<int> assetIds;
    for (int row = 0; row < assetsModel->rowCount(QModelIndex()); ++row) {
        QModelIndex index = assetsModel->index(row, 0);
        int assetId = index.data(AssetsModel::IdRole).toInt();
        assetIds.append(assetId);
    }

    if (!assetIds.isEmpty()) {
        qDebug() << "[MainWindow] Prefetching live previews for" << assetIds.size() << "assets";

        QStringList filePaths;
        for (int assetId : assetIds) {
            QString filePath = DB::instance().getAssetFilePath(assetId);
            if (!filePath.isEmpty()) {
                filePaths.append(filePath);
            }
        }

        LivePreviewManager &previewMgr = LivePreviewManager::instance();
        QSize targetSize = assetGridView ? assetGridView->iconSize() : QSize(180, 180);
        if (!targetSize.isValid()) targetSize = QSize(180, 180);
        for (const QString &filePath : filePaths) {
            previewMgr.requestFrame(filePath, targetSize);
        }
        scheduleVisibleThumbProgressUpdate();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // During UI construction, ignore heavy logic in event filter
    if (m_initializing) {
        return false; // do not intercept; let normal processing continue
    }
    
    // Handle version badge clicks on Project Manager grid view
    if (pmAssetsGridView && watched == pmAssetsGridView->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                QPoint pos = mouseEvent->pos();
                QModelIndex index = pmAssetsGridView->indexAt(pos);
                if (index.isValid() && pmItemDelegate) {
                    QRect itemRect = pmAssetsGridView->visualRect(index);
                    if (pmItemDelegate->isPointOnVersionBadge(itemRect, pos, index)) {
                        // Show version dropdown
                        onPmVersionDropdownRequested(index, mouseEvent->globalPosition().toPoint());
                        return true;  // Event handled
                    }
                }
            }
        }
        // Block double-click on version badge
        if (event->type() == QEvent::MouseButtonDblClick) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            QPoint pos = mouseEvent->pos();
            QModelIndex index = pmAssetsGridView->indexAt(pos);
            if (index.isValid() && pmItemDelegate) {
                QRect itemRect = pmAssetsGridView->visualRect(index);
                if (pmItemDelegate->isPointOnVersionBadge(itemRect, pos, index)) {
                    return true;  // Block the double-click
                }
            }
        }
    }
    
    // Update visible-only progress when asset or File Manager viewports resize
    // Skip during window resize/move to avoid cascading expensive updates
    if (!m_windowResizing &&
        ((assetGridView && watched == assetGridView->viewport()) ||
        (assetTableView && watched == assetTableView->viewport()) ||
        (fmGridView && watched == fmGridView->viewport()) ||
        (fmListView && watched == fmListView->viewport()))) {
        if (event->type() == QEvent::Resize) {
            scheduleVisibleThumbProgressUpdate();
        }
    }

    // Handle Space key on asset views to toggle preview (open/close)
    if ((watched == assetGridView || watched == assetTableView) && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Space && !keyEvent->isAutoRepeat()) {
            if ((previewOverlay && previewOverlay->isVisible()) ||
                (imagePreviewOverlay && imagePreviewOverlay->isVisible())) {
                closePreview();
                return true;
            }
            // Get the current selection
            QItemSelectionModel *selectionModel = isGridMode ? assetGridView->selectionModel() : assetTableView->selectionModel();
            QModelIndexList selected = selectionModel->selectedIndexes();
            if (!selected.isEmpty()) {
                // Open preview for the first selected item
                QModelIndex index = selected.first();
                showPreview(index.row());
                return true; // Event handled
            }
        }
    }

    // Mouse wheel zoom for File Manager image preview
    if ((watched == fmImageView || (fmImageView && watched == fmImageView->viewport())) && event->type() == QEvent::Wheel) {
        QWheelEvent *wheel = static_cast<QWheelEvent*>(event);
        const int delta = wheel->angleDelta().y();
        double factor = (delta > 0) ? 1.15 : 0.85;

        if (fmImageView && fmImageItem && !fmImageItem->pixmap().isNull()) {
            // Get current transform scale
            QTransform currentTransform = fmImageView->transform();
            double currentScale = currentTransform.m11(); // horizontal scale factor

            // Apply zoom
            fmImageView->scale(factor, factor);

            // If zooming out (factor < 1.0), check if we should reset to fit-to-view
            if (delta < 0) {
                double newScale = currentScale * factor;

                // Calculate what the fit-to-view scale would be
                QRectF itemRect = fmImageItem->boundingRect();
                QRectF viewRect = fmImageView->viewport()->rect();
                double fitScale = qMin(viewRect.width() / itemRect.width(),
                                      viewRect.height() / itemRect.height());

                // If zoomed out beyond fit-to-view, reset to fit-to-view and center
                if (newScale <= fitScale * 0.95) { // 0.95 threshold to avoid flickering
                    fmImageScene->setSceneRect(fmImageItem->boundingRect());
                    fmImageView->resetTransform();
                    fmImageView->centerOn(fmImageItem);
                    fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                    fmImageFitToView = true;
                    return true;
                }
            }

            // User performed manual zoom; stop auto-fit
            fmImageFitToView = false;
        }
        return true;
    }

    // Start drag from File Manager preview pane (image/video): include full sequence if present
    if (((fmImageView && (watched == fmImageView || watched == fmImageView->viewport())) || (fmVideoWidget && watched == fmVideoWidget))) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                fmPreviewDragStartPos = me->pos();
                fmPreviewDragPending = true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (fmPreviewDragPending && (me->buttons() & Qt::LeftButton)) {
                if ((me->pos() - fmPreviewDragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
                    // Build adaptive native drag: frames for Explorer/self, folder for sequences in DCCs
                    QVector<QString> frameVec;
                    QVector<QString> folderVec;
                    if (fmIsSequence && !fmSequenceFramePaths.isEmpty()) {
                        for (const QString &p : fmSequenceFramePaths) frameVec.push_back(p);
                        const QString dirPath = QFileInfo(fmSequenceFramePaths.first()).absolutePath();
                        folderVec.push_back(dirPath);
                    } else if (!fmCurrentPreviewPath.isEmpty()) {
                        frameVec.push_back(fmCurrentPreviewPath);
                        folderVec.push_back(fmCurrentPreviewPath); // allow direct file drop to DCCs
                    }
                    if (!frameVec.isEmpty() || !folderVec.isEmpty()) {
                        VirtualDrag::startAdaptivePathsDrag(frameVec, folderVec);
                    }
                    fmPreviewDragPending = false;
                    return true;
                }
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                fmPreviewDragPending = false;
            }
        }
    }

    // Keep image fitted on view resize if auto-fit is active
    if ((watched == fmImageView || (fmImageView && watched == fmImageView->viewport())) && event->type() == QEvent::Resize) {
        if (fmImageFitToView && fmImageView && fmImageItem && !fmImageItem->pixmap().isNull()) {
            fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
        }
    }

    // Handle drops on File Manager views (copy files/folders or assets into current directory)
    if ((fmGridView && watched == fmGridView->viewport()) || (fmListView && watched == fmListView->viewport())) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasUrls() || dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") || dragEvent->mimeData()->hasFormat("application/x-kasset-sequence-urls")) {
                // Determine destination: subfolder under cursor (if any and is a folder), otherwise current root
                QAbstractItemView* view = (fmGridView && watched == fmGridView->viewport())
                                            ? static_cast<QAbstractItemView*>(fmGridView)
                                            : static_cast<QAbstractItemView*>(fmListView);
                QPoint pos = dragEvent->position().toPoint();
                QModelIndex idx = view ? view->indexAt(pos) : QModelIndex();
                QModelIndex srcIdx = idx;
                if (idx.isValid() && fmProxyModel && idx.model() == fmProxyModel) srcIdx = fmProxyModel->mapToSource(idx);

                QString destDir;
                if (idx.isValid() && fmDirModel && fmDirModel->isDir(srcIdx)) destDir = fmDirModel->filePath(srcIdx);
                else destDir = fmDirModel ? fmDirModel->rootPath() : QString();

                bool sameFolderOnly = false;
                if (!destDir.isEmpty()) {
                    QStringList tmpSources;
                    const QMimeData* md = dragEvent->mimeData();
                    if (md->hasFormat("application/x-kasset-sequence-urls")) {
                        QByteArray enc = md->data("application/x-kasset-sequence-urls");
                        QDataStream ds(&enc, QIODevice::ReadOnly);
                        ds >> tmpSources;
                    } else if (md->hasUrls()) {
                        for (const QUrl &url : md->urls()) if (url.isLocalFile()) tmpSources << url.toLocalFile();
                    } else if (md->hasFormat("application/x-kasset-asset-ids")) {
                        QByteArray encodedData = md->data("application/x-kasset-asset-ids");
                        QDataStream stream(&encodedData, QIODevice::ReadOnly);
                        QList<int> assetIds; stream >> assetIds;
                        for (int id : assetIds) { const QString src = DB::instance().getAssetFilePath(id); if (!src.isEmpty()) tmpSources << src; }
                    }
                    if (!tmpSources.isEmpty()) {
                        const QString normDest = QDir(QDir::cleanPath(destDir)).absolutePath().toLower();
                        sameFolderOnly = std::all_of(tmpSources.cbegin(), tmpSources.cend(), [&](const QString &s){
                            QString parent = QFileInfo(s).absoluteDir().absolutePath(); parent = QDir::cleanPath(parent).toLower();
                            return parent == normDest; });
                    }
                }
                if (sameFolderOnly) {
                    dragEvent->setDropAction(Qt::IgnoreAction);
                    dragEvent->accept();
                    return true;
                }
                const bool shift = dragEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                dragEvent->setDropAction(shift ? Qt::MoveAction : Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::DragMove) {
            QDragMoveEvent *dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasUrls() || dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") || dragEvent->mimeData()->hasFormat("application/x-kasset-sequence-urls")) {
                QAbstractItemView* view = (fmGridView && watched == fmGridView->viewport())
                                            ? static_cast<QAbstractItemView*>(fmGridView)
                                            : static_cast<QAbstractItemView*>(fmListView);
                QPoint pos = dragEvent->position().toPoint();
                QModelIndex idx = view ? view->indexAt(pos) : QModelIndex();
                QModelIndex srcIdx = idx;
                if (idx.isValid() && fmProxyModel && idx.model() == fmProxyModel) srcIdx = fmProxyModel->mapToSource(idx);

                QString destDir;
                if (idx.isValid() && fmDirModel && fmDirModel->isDir(srcIdx)) destDir = fmDirModel->filePath(srcIdx);
                else destDir = fmDirModel ? fmDirModel->rootPath() : QString();

                bool sameFolderOnly = false;
                if (!destDir.isEmpty()) {
                    QStringList tmpSources;
                    const QMimeData* md = dragEvent->mimeData();
                    if (md->hasFormat("application/x-kasset-sequence-urls")) {
                        QByteArray enc = md->data("application/x-kasset-sequence-urls");
                        QDataStream ds(&enc, QIODevice::ReadOnly);
                        ds >> tmpSources;
                    } else if (md->hasUrls()) {
                        for (const QUrl &url : md->urls()) if (url.isLocalFile()) tmpSources << url.toLocalFile();
                    } else if (md->hasFormat("application/x-kasset-asset-ids")) {
                        QByteArray encodedData = md->data("application/x-kasset-asset-ids");
                        QDataStream stream(&encodedData, QIODevice::ReadOnly);
                        QList<int> assetIds; stream >> assetIds;
                        for (int id : assetIds) { const QString src = DB::instance().getAssetFilePath(id); if (!src.isEmpty()) tmpSources << src; }
                    }
                    if (!tmpSources.isEmpty()) {
                        const QString normDest = QDir(QDir::cleanPath(destDir)).absolutePath().toLower();
                        sameFolderOnly = std::all_of(tmpSources.cbegin(), tmpSources.cend(), [&](const QString &s){
                            QString parent = QFileInfo(s).absoluteDir().absolutePath(); parent = QDir::cleanPath(parent).toLower();
                            return parent == normDest; });
                    }
                }
                if (sameFolderOnly) {
                    dragEvent->setDropAction(Qt::IgnoreAction);
                    dragEvent->accept();
                    return true;
                }
                const bool shift = dragEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                dragEvent->setDropAction(shift ? Qt::MoveAction : Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData *mimeData = dropEvent->mimeData();

            // Determine destination: subfolder under cursor (if any and is a folder), otherwise current root
            QAbstractItemView* view = (fmGridView && watched == fmGridView->viewport())
                                        ? static_cast<QAbstractItemView*>(fmGridView)
                                        : static_cast<QAbstractItemView*>(fmListView);
            QPoint pos = dropEvent->position().toPoint();
            QModelIndex idx = view ? view->indexAt(pos) : QModelIndex();
            QModelIndex srcIdx = idx;
            if (idx.isValid() && fmProxyModel && idx.model() == fmProxyModel) srcIdx = fmProxyModel->mapToSource(idx);

            QString destDir;
            if (idx.isValid() && fmDirModel && fmDirModel->isDir(srcIdx)) destDir = fmDirModel->filePath(srcIdx);
            else destDir = fmDirModel ? fmDirModel->rootPath() : QString();

            if (destDir.isEmpty()) return false;
            QStringList sources;
            if (mimeData->hasFormat("application/x-kasset-sequence-urls")) {
                QByteArray enc = mimeData->data("application/x-kasset-sequence-urls");
                QDataStream ds(&enc, QIODevice::ReadOnly);
                ds >> sources;
            } else if (mimeData->hasUrls()) {
                for (const QUrl &url : mimeData->urls()) {
                    if (url.isLocalFile()) sources << url.toLocalFile();
                }
            } else if (mimeData->hasFormat("application/x-kasset-asset-ids")) {
                QByteArray encodedData = mimeData->data("application/x-kasset-asset-ids");
                QDataStream stream(&encodedData, QIODevice::ReadOnly);
                QList<int> assetIds; stream >> assetIds;
                for (int id : assetIds) {
                    const QString src = DB::instance().getAssetFilePath(id);
                    if (!src.isEmpty()) sources << src;
                }
            }

            if (!sources.isEmpty()) {
                const QString normDest = QDir(QDir::cleanPath(destDir)).absolutePath().toLower();
                const bool sameFolderOnly = std::all_of(sources.cbegin(), sources.cend(), [&](const QString &s){
                    QString parent = QFileInfo(s).absoluteDir().absolutePath(); parent = QDir::cleanPath(parent).toLower();
                    return parent == normDest; });
                if (sameFolderOnly) {
                    dropEvent->setDropAction(Qt::IgnoreAction);
                    dropEvent->accept();
                    statusBar()->showMessage("Drop ignored (same folder)", 2000);
                    return true;
                }

                const bool shift = dropEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                // Ensure any preview locks are released before file ops
                if (fmGStreamerPlayer) { fmGStreamerPlayer->stop(); }
                if (shift) FileOpsQueue::instance().enqueueMove(sources, destDir);
                else FileOpsQueue::instance().enqueueCopy(sources, destDir);
                if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
                fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
                statusBar()->showMessage(QString("Queued %1 item(s) for %2").arg(sources.size()).arg(shift ? "move" : "copy"), 3000);
            }
            dropEvent->setDropAction(dropEvent->keyboardModifiers().testFlag(Qt::ShiftModifier) ? Qt::MoveAction : Qt::CopyAction);
            dropEvent->accept();
            return true;
        }
    }
    // Handle drops on File Manager folder tree (filesystem)
    if (fmTree && watched == fmTree->viewport()) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasUrls() || dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") || dragEvent->mimeData()->hasFormat("application/x-kasset-sequence-urls")) {
                const bool shift = dragEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                dragEvent->setDropAction(shift ? Qt::MoveAction : Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::DragMove) {
            QDragMoveEvent *dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasUrls() || dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") || dragEvent->mimeData()->hasFormat("application/x-kasset-sequence-urls")) {
                // Highlight folder under cursor
                QPoint pos = dragEvent->position().toPoint();
                QModelIndex idx = fmTree->indexAt(pos);
                if (idx.isValid()) {
                    fmTree->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect);
                }
                // Prevent dropping into the same folder as the sources
                QString destDir = idx.isValid() ? fmPathForIndex(idx) : QString();
                bool sameFolderOnly = false;
                if (!destDir.isEmpty()) {
                    QStringList tmpSources;
                    const QMimeData* md = dragEvent->mimeData();
                    if (md->hasFormat("application/x-kasset-sequence-urls")) {
                        QByteArray enc = md->data("application/x-kasset-sequence-urls");
                        QDataStream ds(&enc, QIODevice::ReadOnly);
                        ds >> tmpSources;
                    } else if (md->hasUrls()) {
                        for (const QUrl &url : md->urls()) if (url.isLocalFile()) tmpSources << url.toLocalFile();
                    } else if (md->hasFormat("application/x-kasset-asset-ids")) {
                        QByteArray encodedData = md->data("application/x-kasset-asset-ids");
                        QDataStream stream(&encodedData, QIODevice::ReadOnly);
                        QList<int> assetIds; stream >> assetIds;
                        for (int id : assetIds) { const QString src = DB::instance().getAssetFilePath(id); if (!src.isEmpty()) tmpSources << src; }
                    }
                    if (!tmpSources.isEmpty()) {
                        const QString normDest = QDir(QDir::cleanPath(destDir)).absolutePath().toLower();
                        sameFolderOnly = std::all_of(tmpSources.cbegin(), tmpSources.cend(), [&](const QString &s){
                            QString parent = QFileInfo(s).absoluteDir().absolutePath(); parent = QDir::cleanPath(parent).toLower();
                            return parent == normDest; });
                    }
                }
                if (sameFolderOnly) {
                    dragEvent->setDropAction(Qt::IgnoreAction);
                    dragEvent->accept();
                    return true;
                }
                const bool shift = dragEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                dragEvent->setDropAction(shift ? Qt::MoveAction : Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData *mimeData = dropEvent->mimeData();
            QPoint pos = dropEvent->position().toPoint();
            QModelIndex idx = fmTree->indexAt(pos);
            if (!idx.isValid()) return false;
            const QString destDir = fmPathForIndex(idx);
            if (destDir.isEmpty()) return false;
            QStringList sources;
            if (mimeData->hasFormat("application/x-kasset-sequence-urls")) {
                QByteArray enc = mimeData->data("application/x-kasset-sequence-urls");
                QDataStream ds(&enc, QIODevice::ReadOnly);
                ds >> sources;
            } else if (mimeData->hasUrls()) {
                for (const QUrl &url : mimeData->urls()) {
                    if (url.isLocalFile()) sources << url.toLocalFile();
                }
            } else if (mimeData->hasFormat("application/x-kasset-asset-ids")) {
                QByteArray encodedData = mimeData->data("application/x-kasset-asset-ids");
                QDataStream stream(&encodedData, QIODevice::ReadOnly);
                QList<int> assetIds; stream >> assetIds;
                for (int id : assetIds) {
                    const QString src = DB::instance().getAssetFilePath(id);
                    if (!src.isEmpty()) sources << src;
                }
            }
            if (!sources.isEmpty()) {
                const QString normDest = QDir(QDir::cleanPath(destDir)).absolutePath().toLower();
                const bool sameFolderOnly = std::all_of(sources.cbegin(), sources.cend(), [&](const QString &s){
                    QString parent = QFileInfo(s).absoluteDir().absolutePath(); parent = QDir::cleanPath(parent).toLower();
                    return parent == normDest; });
                if (sameFolderOnly) {
                    dropEvent->setDropAction(Qt::IgnoreAction);
                    dropEvent->accept();
                    statusBar()->showMessage("Drop ignored (same folder)", 2000);
                    return true;
                }

                const bool shift = dropEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                // Ensure any preview locks are released before file ops
                if (fmGStreamerPlayer) { fmGStreamerPlayer->stop(); }
                if (shift) FileOpsQueue::instance().enqueueMove(sources, destDir);
                else       FileOpsQueue::instance().enqueueCopy(sources, destDir);
                if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
                fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
                statusBar()->showMessage(QString("Queued %1 item(s) for %2").arg(sources.size()).arg(shift ? "move" : "copy"), 3000);
            }
            dropEvent->setDropAction(dropEvent->keyboardModifiers().testFlag(Qt::ShiftModifier) ? Qt::MoveAction : Qt::CopyAction);
            dropEvent->accept();
            return true;
        }
    }


    // Handle drops on folder tree
    if (watched == folderTreeView->viewport()) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") ||
                dragEvent->mimeData()->hasFormat("application/x-kasset-folder-ids") ||
                dragEvent->mimeData()->hasUrls()) {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragMove) {
            QDragMoveEvent *dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") ||
                dragEvent->mimeData()->hasFormat("application/x-kasset-folder-ids") ||
                dragEvent->mimeData()->hasUrls()) {
                // Highlight the folder under cursor using selection
                QPoint pos = dragEvent->position().toPoint();
                QModelIndex index = folderTreeView->indexAt(pos);
                if (index.isValid()) {
                    folderTreeView->selectionModel()->select(index,
                        QItemSelectionModel::ClearAndSelect);
                }
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragLeave) {
            // Clear highlight when drag leaves
            folderTreeView->clearSelection();
            return false;
        }
        else if (event->type() == QEvent::Drop) {
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData *mimeData = dropEvent->mimeData();

            // Get the folder at drop position
            QPoint pos = dropEvent->position().toPoint();
            QModelIndex folderIndex = folderTreeView->indexAt(pos);

            if (folderIndex.isValid()) {
                int targetFolderId = folderModel->data(folderIndex, VirtualFolderTreeModel::IdRole).toInt();

                // Handle file URL drops (import into target folder)
                if (mimeData->hasUrls()) {
                    QStringList filePaths;
                    QStringList folderPaths;
                    for (const QUrl &url : mimeData->urls()) {
                        if (!url.isLocalFile()) continue;
                        const QString path = url.toLocalFile();
                        QFileInfo info(path);
                        if (info.isDir()) folderPaths << path; else if (info.isFile()) filePaths << path;
                    }
                    if (!filePaths.isEmpty() || !folderPaths.isEmpty()) {
                        if (!importProgressDialog) importProgressDialog = new ImportProgressDialog(this);
                        importProgressDialog->show();
                        importProgressDialog->raise();
                        importProgressDialog->activateWindow();

                        // Avoid premature dialog closure when importing folders then files
                        disconnect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);

                        for (const QString &dir : folderPaths) {
                            importer->importFolder(dir, targetFolderId);
                        }
                        if (!filePaths.isEmpty()) {
                            importer->importFiles(filePaths, targetFolderId);
                        }

                        // Reconnect and finalize
                        connect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);
                        onImportComplete();

                        dropEvent->acceptProposedAction();
                        return true;
                    }
                }
                // Handle asset drops
                else if (mimeData->hasFormat("application/x-kasset-asset-ids")) {
                    // Decode asset IDs
                    QByteArray encodedData = mimeData->data("application/x-kasset-asset-ids");
                    QDataStream stream(&encodedData, QIODevice::ReadOnly);
                    QList<int> assetIds;
                    stream >> assetIds;

                    // Check if locked and if move is allowed
                    if (assetsLocked) {
                        // Check if target is in a project folder
                        int targetProjectFolderId = -1;
                        QModelIndex current = folderIndex;
                        while (current.isValid()) {
                            if (folderModel->data(current, VirtualFolderTreeModel::IsProjectFolderRole).toBool()) {
                                targetProjectFolderId = folderModel->data(current, VirtualFolderTreeModel::ProjectFolderIdRole).toInt();
                                break;
                            }
                            current = folderModel->parent(current);
                        }

                        // Check if all assets are from the same project folder
                        bool canMove = true;
                        int sourceProjectFolderId = -1;

                        for (int assetId : assetIds) {
                            QSqlQuery q(DB::instance().database());
                            q.prepare("SELECT virtual_folder_id FROM assets WHERE id=?");
                            q.addBindValue(assetId);
                            if (q.exec() && q.next()) {
                                int assetFolderId = q.value(0).toInt();

                                // Find if asset is in a project folder
                                int assetProjectFolderId = -1;
                                std::function<void(const QModelIndex&)> findProjectFolder = [&](const QModelIndex& idx) {
                                    if (!idx.isValid()) return;
                                    if (folderModel->data(idx, VirtualFolderTreeModel::IdRole).toInt() == assetFolderId) {
                                        QModelIndex cur = idx;
                                        while (cur.isValid()) {
                                            if (folderModel->data(cur, VirtualFolderTreeModel::IsProjectFolderRole).toBool()) {
                                                assetProjectFolderId = folderModel->data(cur, VirtualFolderTreeModel::ProjectFolderIdRole).toInt();
                                                return;
                                            }
                                            cur = folderModel->parent(cur);
                                        }
                                        return;
                                    }
                                    for (int row = 0; row < folderModel->rowCount(idx); ++row) {
                                        findProjectFolder(folderModel->index(row, 0, idx));
                                        if (assetProjectFolderId != -1) return;
                                    }
                                };
                                findProjectFolder(QModelIndex());

                                if (sourceProjectFolderId == -1) {
                                    sourceProjectFolderId = assetProjectFolderId;
                                } else if (sourceProjectFolderId != assetProjectFolderId) {
                                    canMove = false;
                                    break;
                                }
                            }
                        }

                        if (!canMove || (sourceProjectFolderId != -1 && sourceProjectFolderId != targetProjectFolderId)) {
                            QMessageBox::warning(this, "Move Restricted",
                                "Assets are locked. You can only move assets within their project folder.\n"
                                "Uncheck the 'Lock Assets' checkbox to move assets freely.");
                            dropEvent->ignore();
                            return false;
                        }
                    }

                    // Move assets to folder (batch operation to avoid multiple reloads)
                    bool success = true;
                    for (int assetId : assetIds) {
                        if (!DB::instance().setAssetFolder(assetId, targetFolderId)) {
                            success = false;
                        }
                    }

                    // Reload once after all moves are complete
                    if (success) {
                        assetsModel->reload();
                        statusBar()->showMessage(QString("Moved %1 asset(s) to folder").arg(assetIds.size()), 3000);
                    } else {
                        assetsModel->reload();
                        statusBar()->showMessage("Failed to move some assets", 3000);
                    }

                    dropEvent->acceptProposedAction();
                    return true;
                }
                // Handle folder drops (reorganize hierarchy)
                else if (mimeData->hasFormat("application/x-kasset-folder-ids")) {
                    // Decode folder IDs
                    QByteArray encodedData = mimeData->data("application/x-kasset-folder-ids");
                    QDataStream stream(&encodedData, QIODevice::ReadOnly);
                    QList<int> folderIds;
                    stream >> folderIds;

                    // Move folders to new parent
                    bool success = true;
                    for (int folderId : folderIds) {
                        // Don't allow moving a folder into itself or its descendants
                        if (folderId == targetFolderId) {
                            QMessageBox::warning(this, "Error", "Cannot move a folder into itself");
                            success = false;
                            continue;
                        }

                        if (!folderModel->moveFolder(folderId, targetFolderId)) {
                            success = false;
                        }
                    }

                    if (success) {
                        folderModel->reload();
                        statusBar()->showMessage(QString("Moved %1 folder(s)").arg(folderIds.size()), 3000);
                    } else {
                        statusBar()->showMessage("Failed to move some folders", 3000);
                    }

                    dropEvent->acceptProposedAction();
                    return true;
                }
            }
        }
    }

    // Handle drops on tags list
    if (watched == tagsListView->viewport()) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") ||
                dragEvent->mimeData()->hasFormat("application/x-kasset-folder-ids")) {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragMove) {
            QDragMoveEvent *dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") ||
                dragEvent->mimeData()->hasFormat("application/x-kasset-folder-ids")) {
                // Highlight the tag under cursor using selection
                QPoint pos = dragEvent->position().toPoint();
                QModelIndex index = tagsListView->indexAt(pos);
                if (index.isValid()) {
                    tagsListView->selectionModel()->select(index,
                        QItemSelectionModel::ClearAndSelect);
                }
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragLeave) {
            // Clear highlight when drag leaves
            tagsListView->clearSelection();
            return false;
        }
        else if (event->type() == QEvent::Drop) {
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData *mimeData = dropEvent->mimeData();

            // Get the tag at drop position
            QPoint pos = dropEvent->position().toPoint();
            QModelIndex tagIndex = tagsListView->indexAt(pos);

            if (tagIndex.isValid()) {
                int tagId = tagsModel->data(tagIndex, TagsModel::IdRole).toInt();
                QString tagName = tagsModel->data(tagIndex, TagsModel::NameRole).toString();

                // Handle asset drops
                if (mimeData->hasFormat("application/x-kasset-asset-ids")) {
                    // Decode asset IDs
                    QByteArray encodedData = mimeData->data("application/x-kasset-asset-ids");
                    QDataStream stream(&encodedData, QIODevice::ReadOnly);
                    QList<int> assetIds;
                    stream >> assetIds;

                    // Assign tag to assets
                    QList<int> tagIds;
                    tagIds.append(tagId);
                    if (DB::instance().assignTagsToAssets(assetIds, tagIds)) {
                        statusBar()->showMessage(QString("Assigned tag '%1' to %2 asset(s)").arg(tagName).arg(assetIds.size()), 3000);
                        updateInfoPanel();
                    } else {
                        statusBar()->showMessage("Failed to assign tag", 3000);
                    }

                    dropEvent->acceptProposedAction();
                    return true;
                }
                // Handle folder drops (assign tag to all assets in folder)
                else if (mimeData->hasFormat("application/x-kasset-folder-ids")) {
                    // Decode folder IDs
                    QByteArray encodedData = mimeData->data("application/x-kasset-folder-ids");
                    QDataStream stream(&encodedData, QIODevice::ReadOnly);
                    QList<int> folderIds;
                    stream >> folderIds;

                    // Get all assets in these folders (recursive)
                    QList<int> allAssetIds;
                    for (int folderId : folderIds) {
                        QList<int> assetIds = DB::instance().getAssetIdsInFolder(folderId, true);
                        allAssetIds.append(assetIds);
                    }

                    if (!allAssetIds.isEmpty()) {
                        // Assign tag to all assets
                        QList<int> tagIds;
                        tagIds.append(tagId);
                        if (DB::instance().assignTagsToAssets(allAssetIds, tagIds)) {
                            statusBar()->showMessage(QString("Assigned tag '%1' to %2 asset(s) in %3 folder(s)")
                                .arg(tagName).arg(allAssetIds.size()).arg(folderIds.size()), 3000);
                            updateInfoPanel();
                        } else {
                            statusBar()->showMessage("Failed to assign tag", 3000);
                        }
                    } else {
                        statusBar()->showMessage("No assets found in selected folder(s)", 3000);
                    }

                    dropEvent->acceptProposedAction();
                    return true;
                }
            }
        }

    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::saveFolderExpansionState()
{
    expandedFolderIds.clear();

    if (!folderModel || !folderTreeView) {
        return;
    }

    // Recursively save expanded state
    std::function<void(const QModelIndex&)> saveExpanded = [&](const QModelIndex& parent) {
        int rowCount = folderModel->rowCount(parent);
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex index = folderModel->index(i, 0, parent);
            if (index.isValid()) {
                if (folderTreeView->isExpanded(index)) {
                    int folderId = index.data(VirtualFolderTreeModel::IdRole).toInt();
                    expandedFolderIds.insert(folderId);
                }
                // Recurse into children
                saveExpanded(index);
            }
        }
    };

    saveExpanded(QModelIndex());
    qDebug() << "Saved expansion state for" << expandedFolderIds.size() << "folders";
}

void MainWindow::restoreFolderExpansionState()
{
    if (!folderModel || !folderTreeView) {
        return;
    }

    // Always ensure roots are expanded
    folderTreeView->expandToDepth(0);

    // Recursively restore expanded state
    std::function<void(const QModelIndex&)> restoreExpanded = [&](const QModelIndex& parent) {
        int rowCount = folderModel->rowCount(parent);
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex index = folderModel->index(i, 0, parent);
            if (index.isValid()) {
                int folderId = index.data(VirtualFolderTreeModel::IdRole).toInt();
                if (expandedFolderIds.contains(folderId)) {
                    folderTreeView->setExpanded(index, true);
                }
                // Recurse into children
                restoreExpanded(index);
            }
        }
    };

    restoreExpanded(QModelIndex());
}

void MainWindow::onOpenSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        applyFmShortcuts();
        // Apply theme in case it was changed
        applyTheme();
    }
}

void MainWindow::onThumbnailSizeChanged(int size)
{
    // Update delegate thumbnail size
    AssetItemDelegate* delegate = static_cast<AssetItemDelegate*>(assetGridView->itemDelegate());
    if (delegate) {
        delegate->setThumbnailSize(size);
    }

    // Update icon size for the view
    assetGridView->setIconSize(QSize(size, size));

    // Force view to update by resetting the model
    assetGridView->reset();

    // Recompute visible-only progress since layout changed
    scheduleVisibleThumbProgressUpdate();
}

void MainWindow::onViewModeChanged()
{
    isGridMode = !isGridMode;

    if (isGridMode) {
        // Switch to grid mode
        viewModeButton->setIcon(icoGrid(ThemeManager::instance().iconColor()));
        viewStack->setCurrentIndex(0); // Show grid view
        thumbnailSizeSlider->setEnabled(true);
    } else {
        // Switch to list mode (table view)
        viewModeButton->setIcon(icoList(ThemeManager::instance().iconColor()));
        viewStack->setCurrentIndex(1); // Show table view
        thumbnailSizeSlider->setEnabled(false);
    }

    // Recompute visible-only progress for the new view
    scheduleVisibleThumbProgressUpdate();
}


void MainWindow::onPrefetchLivePreviewsForFolder()
{
    if (!assetsModel) return;
    LivePreviewManager &previewMgr = LivePreviewManager::instance();
    QSize targetSize = assetGridView ? assetGridView->iconSize() : QSize(180, 180);
    if (!targetSize.isValid()) targetSize = QSize(180, 180);

    int requested = 0;
    const int rows = assetsModel->rowCount(QModelIndex());
    for (int r = 0; r < rows; ++r) {
        QModelIndex idx = assetsModel->index(r, 0);
        const QString fp = assetsModel->data(idx, AssetsModel::FilePathRole).toString();
        if (fp.isEmpty()) continue;
        auto handle = previewMgr.cachedFrame(fp, targetSize);
        if (!handle.isValid()) {
            previewMgr.requestFrame(fp, targetSize);
            ++requested;
        }
    }
    if (requested > 0) {
        statusBar()->showMessage(QString("Prefetching %1 live previews...").arg(requested), 2000);
    }
}

void MainWindow::onRefreshLivePreviewsForFolder()
{
    if (!assetsModel) return;
    LivePreviewManager &previewMgr = LivePreviewManager::instance();
    QSize targetSize = assetGridView ? assetGridView->iconSize() : QSize(180, 180);
    if (!targetSize.isValid()) targetSize = QSize(180, 180);

    int requested = 0;
    const int rows = assetsModel->rowCount(QModelIndex());
    for (int r = 0; r < rows; ++r) {
        QModelIndex idx = assetsModel->index(r, 0);
        const QString fp = assetsModel->data(idx, AssetsModel::FilePathRole).toString();
        if (fp.isEmpty()) continue;
        previewMgr.invalidate(fp);
        previewMgr.requestFrame(fp, targetSize);
        ++requested;
    }
    if (requested > 0) {
        statusBar()->showMessage(QString("Refreshing %1 live previews...").arg(requested), 2000);
    }
}

void MainWindow::onPrefetchLivePreviewsRecursive()
{
    if (!assetsModel) return;
    int fid = assetsModel->folderId();
    if (fid <= 0) return;
    QList<int> ids = DB::instance().getAssetIdsInFolder(fid, /*recursive*/true);
    if (ids.isEmpty()) return;
    LivePreviewManager &previewMgr = LivePreviewManager::instance();
    QSize targetSize = assetGridView ? assetGridView->iconSize() : QSize(180, 180);
    if (!targetSize.isValid()) targetSize = QSize(180, 180);

    int requested = 0;
    for (int id : ids) {
        const QString fp = DB::instance().getAssetFilePath(id);
        if (fp.isEmpty()) continue;
        auto handle = previewMgr.cachedFrame(fp, targetSize);
        if (!handle.isValid()) {
            previewMgr.requestFrame(fp, targetSize);
            ++requested;
        }
    }
    if (requested > 0) {
        statusBar()->showMessage(QString("Prefetching %1 live previews (recursive)...").arg(requested), 2000);
    }
}

void MainWindow::onRefreshLivePreviewsRecursive()
{
    if (!assetsModel) return;
    int fid = assetsModel->folderId();
    if (fid <= 0) return;
    QList<int> ids = DB::instance().getAssetIdsInFolder(fid, /*recursive*/true);
    if (ids.isEmpty()) return;
    LivePreviewManager &previewMgr = LivePreviewManager::instance();
    QSize targetSize = assetGridView ? assetGridView->iconSize() : QSize(180, 180);
    if (!targetSize.isValid()) targetSize = QSize(180, 180);

    int requested = 0;
    for (int id : ids) {
        const QString fp = DB::instance().getAssetFilePath(id);
        if (fp.isEmpty()) continue;
        previewMgr.invalidate(fp);
        previewMgr.requestFrame(fp, targetSize);
        ++requested;
    }
    if (requested > 0) {
        statusBar()->showMessage(QString("Refreshing %1 live previews (recursive)...").arg(requested), 2000);
    }
}

void MainWindow::scheduleVisibleThumbProgressUpdate()
{
    if (m_initializing) return;
    // Skip entirely during window resize/move to avoid sluggishness
    if (m_windowResizing) return;
    // Do not show our visible-only progress while a global/import progress is active
    if (ProgressManager::instance().isActive()) {
        return;
    }
    // Debounce frequent scroll/resize updates
    visibleThumbTimer.start(100);
}

void MainWindow::updateVisibleThumbProgress()
{
    if (m_initializing) return;
    // Skip during active window resize/move
    if (m_windowResizing) return;
    if (ProgressManager::instance().isActive()) {
        if (thumbnailProgressLabel) thumbnailProgressLabel->setVisible(false);
        if (thumbnailProgressBar) thumbnailProgressBar->setVisible(false);
        return;
    }

    int visibleTotal = 0;
    int readyCount = 0;
    bool anyViewConsidered = false;
    bool assetViewConsidered = false;
    bool fileManagerViewConsidered = false;

    if (!thumbnailProgressLabel || !thumbnailProgressBar) {
        if (thumbnailProgressLabel) thumbnailProgressLabel->setVisible(false);
        if (thumbnailProgressBar) thumbnailProgressBar->setVisible(false);
        return;
    }

    auto accumulateFromAssets = [&](QAbstractItemView* view) {
        if (!assetsModel || !view || !view->isVisible() || !view->viewport()) {
            return;
        }
        const QRect viewportRect = view->viewport()->rect();
        const int totalRows = assetsModel->rowCount(QModelIndex());
        if (totalRows <= 0) return;
        anyViewConsidered = true;
        assetViewConsidered = true;
        const int thumbSide = view->iconSize().isValid() ? view->iconSize().width() : 180;
        const QSize targetSize(thumbSide, thumbSide);
        LivePreviewManager &previewMgr = LivePreviewManager::instance();
        for (int row = 0; row < totalRows; ++row) {
            const QModelIndex idx = assetsModel->index(row, 0);
            const QRect itemRect = view->visualRect(idx);
            if (!itemRect.isValid() || !itemRect.intersects(viewportRect)) {
                continue;
            }
            ++visibleTotal;
            const QString filePath = assetsModel->data(idx, AssetsModel::FilePathRole).toString();
            auto handle = previewMgr.cachedFrame(filePath, targetSize);
            if (handle.isValid()) {
                ++readyCount;
            } else {
                previewMgr.requestFrame(filePath, targetSize);
            }
        }
    };

    auto accumulateFromFileManager = [&](QAbstractItemView* view) {
        if (!view || !view->isVisible() || !view->viewport() || !fmDirModel) {
            return;
        }
        QAbstractItemModel* model = view->model();
        if (!model) return;
        const QRect viewportRect = view->viewport()->rect();
        const int rows = model->rowCount();
        if (rows <= 0) return;
        const int thumbSide = view->iconSize().isValid() ? view->iconSize().width() : 120;
        const QSize targetSize(thumbSide, thumbSide);
        LivePreviewManager &previewMgr = LivePreviewManager::instance();
        anyViewConsidered = true;
        fileManagerViewConsidered = true;
        for (int row = 0; row < rows; ++row) {
            const QModelIndex idx = model->index(row, 0);
            const QRect itemRect = view->visualRect(idx);
            if (!itemRect.isValid() || !itemRect.intersects(viewportRect)) {
                continue;
            }
            ++visibleTotal;
            QModelIndex srcIdx = idx;
            if (fmProxyModel && idx.model() == fmProxyModel) {
                srcIdx = fmProxyModel->mapToSource(idx);
            }
            const QString filePath = fmDirModel->filePath(srcIdx);
            if (filePath.isEmpty()) continue;
            auto handle = previewMgr.cachedFrame(filePath, targetSize);
            if (handle.isValid()) {
                ++readyCount;
            }
        }
    };

    if (isGridMode) {
        accumulateFromAssets(assetGridView);
    } else {
        accumulateFromAssets(assetTableView);
    }
    accumulateFromFileManager(fmGridView);
    accumulateFromFileManager(fmListView);

    if (!anyViewConsidered || visibleTotal == 0 || readyCount >= visibleTotal) {
        thumbnailProgressLabel->setVisible(false);
        thumbnailProgressBar->setVisible(false);
        return;
    }

    QString labelText;
    if (fileManagerViewConsidered && !assetViewConsidered) {
        labelText = "File Manager previews (visible):";
    } else if (assetViewConsidered && !fileManagerViewConsidered) {
        labelText = "Asset previews (visible):";
    } else {
        labelText = "Live previews (visible):";
    }

    thumbnailProgressLabel->setText(labelText);
    thumbnailProgressLabel->setVisible(true);
    thumbnailProgressBar->setMaximum(visibleTotal);
    thumbnailProgressBar->setValue(readyCount);
    thumbnailProgressBar->setFormat(QString("%1/%2 (%p%)").arg(readyCount).arg(visibleTotal));
    thumbnailProgressBar->setVisible(true);
}

void MainWindow::onToggleLogViewer()
{
    // Find the log dock widget
    QList<QDockWidget*> docks = findChildren<QDockWidget*>();
    for (QDockWidget* dock : docks) {
        if (dock->windowTitle() == "Application Log") {
            dock->setVisible(!dock->isVisible());
            break;
        }
    }
}

QStringList MainWindow::reconstructSequenceFramePaths(const QString& firstFramePath, int startFrame, int endFrame)
{
    QStringList framePaths;
    QFileInfo firstFrameInfo(firstFramePath);
    QString fileName = firstFrameInfo.fileName();
    QString dirPath = firstFrameInfo.absolutePath();
    QString extension = firstFrameInfo.suffix();

    // Find the LAST frame number pattern in the first frame filename
    // Use globalMatch to find all occurrences, then take the last one
    QRegularExpression re("(\\d{3,})");
    QRegularExpressionMatchIterator it = re.globalMatch(fileName);

    QRegularExpressionMatch lastMatch;
    bool hasMatch = false;

    while (it.hasNext()) {
        lastMatch = it.next();
        hasMatch = true;
    }

    if (!hasMatch) {
        qWarning() << "[MainWindow] Could not find frame number pattern in:" << fileName;
        return framePaths;
    }

    QString frameNumberStr = lastMatch.captured(1);
    int paddingLength = frameNumberStr.length();
    int matchPos = lastMatch.capturedStart(1);

    // Extract the base name (everything before the frame number)
    QString baseName = fileName.left(matchPos);

    // Extract the suffix (everything after the frame number, including extension)
    QString suffix = fileName.mid(matchPos + paddingLength);

    // Reconstruct all frame paths
    for (int frame = startFrame; frame <= endFrame; ++frame) {
        QString frameNum = QString("%1").arg(frame, paddingLength, 10, QChar('0'));
        QString framePath = QDir(dirPath).filePath(baseName + frameNum + suffix);

        // Only add if file exists
        if (FileUtils::fileExists(framePath)) {
            framePaths.append(framePath);
        }
    }

    return framePaths;
}

void MainWindow::onAddProjectFolder()
{
    // Ask user to select a folder
    QString folderPath = QFileDialog::getExistingDirectory(
        this,
        "Select Project Folder",
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );


    if (folderPath.isEmpty()) {
        return;
    }

    // Ask for a name for this project folder
    bool ok;
    QString folderName = QInputDialog::getText(
        this,
        "Project Folder Name",
        "Enter a name for this project folder:",
        QLineEdit::Normal,
        QFileInfo(folderPath).fileName(),
        &ok
    );

    if (!ok || folderName.isEmpty()) {
        return;
    }

    // Create the project folder in the database
    int projectFolderId = DB::instance().createProjectFolder(folderName, folderPath);
    if (projectFolderId <= 0) {
        QMessageBox::warning(this, "Error", "Failed to create project folder. The name or path may already exist.");
        return;
    }

    // Add to watcher
    projectFolderWatcher->addProjectFolder(projectFolderId, folderPath);

    // Reload folder tree
    folderModel->reload();

    // Import the folder contents
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Import Assets",
        "Do you want to import all assets from this folder now?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        // Get the virtual folder ID for this project
        auto projectFolders = DB::instance().listProjectFolders();
        for (const auto& pf : projectFolders) {
            if (pf.first == projectFolderId) {
                // Import the folder
                importFiles(QStringList() << folderPath);
                break;
            }
        }
    }

    statusBar()->showMessage(QString("Added project folder '%1'").arg(folderName), 3000);
}

void MainWindow::onRefreshAssets()
{
    qDebug() << "MainWindow::onRefreshAssets";

    // Get all project folders
    auto projectFolders = DB::instance().listProjectFolders();

    if (projectFolders.isEmpty()) {
        statusBar()->showMessage("No project folders to refresh", 3000);
        return;
    }

    // Manually trigger refresh for all project folders
    for (const auto& pf : projectFolders) {
        int projectFolderId = pf.first;
        projectFolderWatcher->refreshProjectFolder(projectFolderId);
    }

    statusBar()->showMessage("Refreshing all project folders...", 3000);
}

void MainWindow::onLockToggled(bool checked)
{
    assetsLocked = checked;

    if (checked) {
        statusBar()->showMessage("Assets locked - can only move within project folders", 3000);
    } else {
        statusBar()->showMessage("Assets unlocked - can move freely", 3000);
    }
}

void MainWindow::onProjectFolderChanged(int projectFolderId, const QString& path)
{
    // Re-import the folder to pick up new/changed files
    // This will update existing assets and add new ones
    statusBar()->showMessage(QString("Refreshing project folder: %1").arg(QFileInfo(path).fileName()), 2000);

    // Import the folder (this will upsert assets)
    importFiles(QStringList() << path);
}


// ===== Asset Versioning UI Handlers =====
void MainWindow::reloadVersionHistory()
{
    // Default state
    if (!versionTable) return;
    revertVersionButton->setEnabled(false);
    versionTable->setRowCount(0);

    // Determine current single-selected asset
    QModelIndexList selected = getCurrentSelectionModel()->selectedIndexes();
    if (selected.size() != 1) {
        if (versionsTitleLabel) versionsTitleLabel->setText("Version History");
        return;
    }

    QModelIndex idx = selected.first();
    currentAssetId = idx.data(AssetsModel::IdRole).toInt();
    if (currentAssetId <= 0) return;



    QVector<AssetVersionRow> versions = DB::instance().listAssetVersions(currentAssetId);
    versionTable->setRowCount(versions.size());

    // Fill rows
    int row = 0;
    for (const auto& v : versions) {
        // Icon column
        QTableWidgetItem *iconItem = new QTableWidgetItem();
        const QSize targetSize(96, 96);
        QPixmap cached = versionPreviewCache.value(v.filePath);
        if (!cached.isNull()) {
            iconItem->setIcon(QIcon(cached));
        } else {
            auto handle = LivePreviewManager::instance().cachedFrame(v.filePath, targetSize);
            if (handle.isValid()) {
                iconItem->setIcon(QIcon(handle.pixmap));
            } else {
                LivePreviewManager::instance().requestFrame(v.filePath, targetSize);
                iconItem->setText("...");
            }
        }
        iconItem->setData(Qt::UserRole, v.filePath);
        versionTable->setItem(row, 0, iconItem);

        // Version column (store id in UserRole)
        QTableWidgetItem *verItem = new QTableWidgetItem(v.versionName);
        verItem->setData(Qt::UserRole, v.id);
        versionTable->setItem(row, 1, verItem);

        // Date column
        QTableWidgetItem *dateItem = new QTableWidgetItem(v.createdAt);
        versionTable->setItem(row, 2, dateItem);

        // Size column
        QString sizeStr;
        if (v.fileSize < 1024) sizeStr = QString::number(v.fileSize) + " B";
        else if (v.fileSize < 1024 * 1024) sizeStr = QString::number(v.fileSize / 1024.0, 'f', 1) + " KB";
        else if (v.fileSize < 1024ll * 1024ll * 1024ll) sizeStr = QString::number(v.fileSize / (1024.0 * 1024.0), 'f', 1) + " MB";
        else sizeStr = QString::number(v.fileSize / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
        QTableWidgetItem *sizeItem = new QTableWidgetItem(sizeStr.toLower());
        versionTable->setItem(row, 3, sizeItem);

        // Notes column
        QTableWidgetItem *notesItem = new QTableWidgetItem(v.notes);
        versionTable->setItem(row, 4, notesItem);

        ++row;
    }

    if (!versions.isEmpty()) {
        versionTable->selectRow(versionTable->rowCount() - 1); // Select latest by default
        revertVersionButton->setEnabled(true);
        if (versionsTitleLabel) versionsTitleLabel->setText(QString("Version History (%1)").arg(versions.size()));
    } else {
        if (versionsTitleLabel) versionsTitleLabel->setText("Version History (0)");
    }
}

void MainWindow::onRevertSelectedVersion()
{
    if (!versionTable) return;
    int row = versionTable->currentRow();
    if (row < 0 || currentAssetId <= 0) return;

    int versionId = 0;
    if (QTableWidgetItem *item = versionTable->item(row, 1)) {
        versionId = item->data(Qt::UserRole).toInt();
    }
    if (versionId <= 0) return;

    const bool makeBackup = backupVersionCheck && backupVersionCheck->isChecked();
    const QString question = makeBackup
        ? "Revert this asset to the selected version? A backup of the current file will be saved as a new version."
        : "Revert this asset to the selected version? This will overwrite the current file.";

    if (QMessageBox::question(this, "Revert to Version", question, QMessageBox::Yes|QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    if (!DB::instance().revertAssetToVersion(currentAssetId, versionId, makeBackup)) {
        QMessageBox::warning(this, "Revert Failed", "Failed to revert to the selected version.");
        return;
    }

    // Refresh UI
    reloadVersionHistory();
    updateInfoPanel();

    // Prefetch live preview for the asset file
    const QString assetPath = DB::instance().getAssetFilePath(currentAssetId);
    if (!assetPath.isEmpty()) {
        LivePreviewManager &previewMgr = LivePreviewManager::instance();
        previewMgr.invalidate(assetPath);
        previewMgr.requestFrame(assetPath, QSize(180, 180));
    }

    QMessageBox::information(this, "Reverted", "Asset has been reverted to the selected version.");
}

void MainWindow::onAssetVersionsChanged(int assetId)
{
    if (assetId == currentAssetId) {
        reloadVersionHistory();
    }
}


// ===== File Manager Preview handlers =====
void MainWindow::clearFmPreview()
{
    if (fmGStreamerPlayer) { fmGStreamerPlayer->stop(); }
    if (fmVideoWidget) fmVideoWidget->hide();
    if (fmPrevFrameBtn) fmPrevFrameBtn->hide();
    if (fmPlayPauseBtn) fmPlayPauseBtn->hide();
    if (fmNextFrameBtn) fmNextFrameBtn->hide();
    if (fmPositionSlider) fmPositionSlider->hide();
    if (fmTimeLabel) fmTimeLabel->hide();
    if (fmVolumeSlider) fmVolumeSlider->hide();
    if (fmMuteBtn) fmMuteBtn->hide();

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
    if (fmImageView) fmImageView->show();
}

static inline bool isImageFile(const QString &ext)
{
    static const QSet<QString> exts = {"png","jpg","jpeg","bmp","gif","tif","tiff","webp","heic","heif","exr","psd"};
    return exts.contains(ext.toLower());
}
static inline bool isVideoFile(const QString &ext)
{
    static const QSet<QString> exts = {"mp4","mov","avi","mkv","wmv","m4v","mpg","mpeg","mxf"};
    return exts.contains(ext.toLower());
}
static inline bool isAudioFile(const QString &ext)
{
    static const QSet<QString> exts = {"mp3","wav","aac","flac","ogg","m4a"};
    return exts.contains(ext.toLower());
}
static inline bool isPdfFile(const QString &ext)
{
    return ext.compare("pdf", Qt::CaseInsensitive) == 0;
}
static inline bool isSvgFile(const QString &ext)
{
    static const QSet<QString> exts = {"svg","svgz"};
    return exts.contains(ext.toLower());
}
static inline bool isTextFile(const QString &ext)
{
    static const QSet<QString> exts = {"txt","log"};
    return exts.contains(ext.toLower());
}
static inline bool isCsvFile(const QString &ext)
{
    return ext.compare("csv", Qt::CaseInsensitive) == 0;
}
static inline bool isExcelFile(const QString &ext)
{
    static const QSet<QString> exts = {"xls","xlsx"};
    return exts.contains(ext.toLower());
}
static inline bool isDocxFile(const QString &ext)
{
    return ext.compare("docx", Qt::CaseInsensitive) == 0;
}
static inline bool isDocFile(const QString &ext)
{
    return ext.compare("doc", Qt::CaseInsensitive) == 0;
}
static inline bool isAiFile(const QString &ext)
{
    return ext.compare("ai", Qt::CaseInsensitive) == 0;
}
static inline bool isPreviewOverlayViewable(const QString &ext)
{
    // Check if file type is viewable in PreviewOverlay
    // Includes: images, videos, PDFs, SVG, text files, CSV, Office docs
    return isImageFile(ext) || isVideoFile(ext) || isPdfFile(ext) ||
           isSvgFile(ext) || isTextFile(ext) || isCsvFile(ext) ||
           isExcelFile(ext) || isDocxFile(ext) || isDocFile(ext) || isAiFile(ext);
}
static inline bool isPptxFile(const QString &ext)
{
    // Treat both .pptx and legacy .ppt as PowerPoint documents for preview handling
    return ext.compare("pptx", Qt::CaseInsensitive) == 0 || ext.compare("ppt", Qt::CaseInsensitive) == 0;
}






void MainWindow::updateFmPreviewForIndex(const QModelIndex &idx)
{
    if (!fmPreviewPanel || !fmPreviewPanel->isVisible()) return;
    if (!idx.isValid()) { clearFmPreview(); return; }

    QModelIndex viewIdx = idx.sibling(idx.row(), 0);

    // If this is a representative sequence item, set up full sequence playback in the preview pane

    if (fmProxyModel && fmGroupSequences && viewIdx.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(viewIdx)) {
        auto info = fmProxyModel->infoForProxyIndex(viewIdx);
        QString reprPath = info.reprPath;
        if (reprPath.isEmpty()) { clearFmPreview(); return; }
        QFileInfo infoFi(reprPath);
        if (!infoFi.exists()) { clearFmPreview(); return; }

        // Build all frame paths
        QStringList frames = reconstructSequenceFramePaths(reprPath, info.start, info.end);
        if (frames.isEmpty()) { clearFmPreview(); return; }

        // Stop any video playback and show image-based sequence view
        if (fmGStreamerPlayer) { fmGStreamerPlayer->stop(); }
        if (fmVideoWidget) fmVideoWidget->hide();
        if (fmImageView) fmImageView->show();

        // Configure sequence state
        fmIsSequence = true;
        fmSequenceFramePaths = frames;
        fmSequenceStartFrame = info.start;
        fmSequenceEndFrame = info.end;
        fmSequenceCurrentIndex = 0;
        fmSequenceFps = 24.0; // default; could be inferred later if needed
        if (fmSequenceTimer) fmSequenceTimer->stop();
        fmSequencePlaying = false;

        // Show media controls for sequence (Prev/Play/Next, Slider, Time)
        if (fmPrevFrameBtn) fmPrevFrameBtn->show();
        if (fmPlayPauseBtn) { fmPlayPauseBtn->show(); fmPlayPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor())); }
        if (fmNextFrameBtn) fmNextFrameBtn->show();
        if (fmPositionSlider) { fmPositionSlider->show(); fmPositionSlider->setRange(0, frames.size()-1); fmPositionSlider->setValue(0); }
        if (fmTimeLabel) { fmTimeLabel->show(); fmTimeLabel->setText(QString("Frame %1 / %2").arg(info.start).arg(info.end)); }

        // Audio: sequences have no audio - disable and show No Audio icon
        if (fmMuteBtn) { fmMuteBtn->show(); fmMuteBtn->setEnabled(false); fmMuteBtn->setIcon(icoMediaNoAudio(ThemeManager::instance().iconColor())); }
        if (fmVolumeSlider) { fmVolumeSlider->show(); fmVolumeSlider->setEnabled(false); }

        // Load first frame
        loadFmSequenceFrame(0);
        return;
    }

    QModelIndex srcIdx = viewIdx;
    if (fmProxyModel && viewIdx.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(viewIdx);
    const QString path = fmDirModel->filePath(srcIdx);
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
        // Stop any media playback and hide media-specific widgets/controls
        if (fmGStreamerPlayer) { fmGStreamerPlayer->stop(); }
        hideNonImageWidgets();

        // Try OpenImageIO first for advanced formats (PSD/EXR/TIFF/etc.)
        QImage img;
        if (OIIOImageLoader::isOIIOSupported(path)) {
            img = OIIOImageLoader::loadImage(path, 0, 0, OIIOImageLoader::ColorSpace::sRGB);
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
        fmImageItem->setPixmap(QPixmap::fromImage(disp));
        fmImageItem->setTransformationMode(Qt::SmoothTransformation);
        fmImageScene->setSceneRect(fmImageItem->boundingRect());
        fmImageView->resetTransform();
        fmImageView->centerOn(fmImageItem);
        fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
        fmImageFitToView = true;
        fmImageView->setBackgroundBrush(QColor("#0a0a0a"));
        fmImageView->show();
        return;
    }

#ifdef HAVE_QT_PDF
    if (isPdfFile(ext)) {
        hideNonImageWidgets();
        if (fmPdfDoc) {
            fmCurrentPreviewPath = path;
            auto err = fmPdfDoc->load(path);
            if (err == QPdfDocument::Error::None && fmPdfDoc->pageCount() > 0) {
            // Always render PDF pages into the image view for consistent zoom/pan
                fmPdfCurrentPage = 0;
                const QSizeF pts = fmPdfDoc->pagePointSize(fmPdfCurrentPage);
                int vw = fmImageView ? fmImageView->viewport()->width() : 800;
                if (vw < 1) vw = 800;
                int w = vw;
                int h = pts.width() > 0 ? int(pts.height() * (w / pts.width())) : w;
                QImage img = fmPdfDoc->render(fmPdfCurrentPage, QSize(w, h));
                if (!img.isNull() && fmImageItem) {
                    if (img.hasAlphaChannel()) {
                        QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied);
                        bg.fill(Qt::white);
                        QPainter p(&bg);
                        p.drawImage(0, 0, img);
                        p.end();
                        img = bg;
                    }
                    fmImageItem->setPixmap(QPixmap::fromImage(img));
                    if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                    if (fmImageView) {
                        fmImageView->resetTransform();
                        fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                        fmImageFitToView = true;
                        fmImageView->setBackgroundBrush(Qt::white);
                        fmImageView->show();
                    }
                }
                if (fmPdfPrevBtn) fmPdfPrevBtn->show();
                if (fmPdfNextBtn) fmPdfNextBtn->show();
                if (fmPdfPageLabel) { fmPdfPageLabel->show(); fmPdfPageLabel->setText(QString("%1/%2").arg(1).arg(fmPdfDoc->pageCount())); }
                #ifdef HAVE_QT_PDF_WIDGETS
                if (fmPdfView) fmPdfView->hide();
                #endif
            } else {
                qWarning() << "[PREVIEW] PDF load failed" << int(err) << "pages=" << (fmPdfDoc ? fmPdfDoc->pageCount() : -1) << path;
                // Fallback: show not available message in text view
                if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); }
            }
        }
        return;
    }
#else
    if (isPdfFile(ext)) {
        hideNonImageWidgets();
        if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); }
        return;
    }
#endif

    if (isSvgFile(ext)) {
        hideNonImageWidgets();
        if (fmSvgScene && fmSvgView) {
            // Remove previous item
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
                QByteArray data = f.read(2*1024*1024); // cap to 2MB

                auto decodeText = [](const QByteArray &data) -> QString {
                    if (data.isEmpty()) return QString();
                    const uchar *b = reinterpret_cast<const uchar*>(data.constData());
                    const int n = data.size();
                    // UTF-8 BOM
                    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) {
                        return QString::fromUtf8(reinterpret_cast<const char*>(b + 3), n - 3);
                    }
                    // UTF-16 LE BOM
                    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
                        return QString::fromUtf16(reinterpret_cast<const ushort*>(b + 2), (n - 2) / 2);
                    }
                    // UTF-16 BE BOM
                    if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
                        const int ulen = (n - 2) / 2;
                        QVector<ushort> buf; buf.resize(ulen);
                        for (int i = 0; i < ulen; ++i) buf[i] = (ushort(b[2 + 2*i]) << 8) | ushort(b[2 + 2*i + 1]);
                        return QString::fromUtf16(buf.constData(), ulen);
                    }
                    // Heuristic: UTF-16 without BOM (look for lots of NULs at odd/even positions)
                    const int sample = qMin(n, 4096);
                    int zeroEven = 0, zeroOdd = 0;
                    for (int i = 0; i < sample; ++i) {
                        if (b[i] == 0) { if ((i & 1) == 0) ++zeroEven; else ++zeroOdd; }
                    }
                    if ((zeroOdd + zeroEven) > sample / 16) {
                        const bool le = (zeroOdd > zeroEven);
                        const int ulen = n / 2;
                        if (le) {
                            return QString::fromUtf16(reinterpret_cast<const ushort*>(b), ulen);
                        } else {
                            QVector<ushort> buf; buf.resize(ulen);
                            for (int i = 0; i < ulen; ++i) buf[i] = (ushort(b[2*i]) << 8) | ushort(b[2*i + 1]);
                            return QString::fromUtf16(buf.constData(), ulen);
                        }
                    }
                    // Default: UTF-8, fallback to local 8-bit if many replacement chars
                    QString s = QString::fromUtf8(reinterpret_cast<const char*>(b), n);
                    int bad = 0; const int check = qMin(s.size(), 4096);
                    for (int i = 0; i < check; ++i) if (s.at(i).unicode() == 0xFFFD) ++bad;
                    if (bad > check / 16) s = QString::fromLocal8Bit(reinterpret_cast<const char*>(b), n);
                    return s;
                };

                fmTextView->setPlainText(decodeText(data));
                fmTextView->show();
            } else {
                if (fmTextView) {
                    fmTextView->setPlainText("Preview not available");
                    fmTextView->show();
                }
            }
        }
        return;
    }

// Office formats (DOC/DOCX/XLSX): lightweight, parse-only previews (no WYSIWYG)
if (isDocxFile(ext)) {
    hideNonImageWidgets();
    fmCurrentPreviewPath = path;
    if (fmTextView) {
        const QString text = extractDocxText(path);
        if (!text.isEmpty()) {
            fmTextView->setFont(QFont("Segoe UI"));
            fmTextView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            fmTextView->setPlainText(text);
        } else {
            fmTextView->setFont(QFont("Segoe UI"));
            fmTextView->setPlainText("Preview not available");
        }
        fmTextView->show();
    }
    return;
}
if (isDocFile(ext)) {
    hideNonImageWidgets();
    fmCurrentPreviewPath = path;
    if (fmTextView) {
        const QString text = extractDocBinaryText(path, 2 * 1024 * 1024);
        if (!text.isEmpty()) {
            fmTextView->setFont(QFont("Segoe UI"));
            fmTextView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            fmTextView->setPlainText(text);
        } else {
            fmTextView->setFont(QFont("Segoe UI"));
            fmTextView->setPlainText("Preview not available");
        }
        fmTextView->show();
    }
    return;
}

if (isExcelFile(ext)) {
    hideNonImageWidgets();
    fmCurrentPreviewPath = path;
    if (fmCsvModel && fmCsvView) {
        fmCsvModel->clear();
        if (loadXlsxSheet(path, fmCsvModel, 2000)) {
            fmCsvView->resizeColumnsToContents();
            fmCsvView->show();
        } else if (fmTextView) {
            fmTextView->setPlainText("Preview not available");
            fmTextView->show();
        }
    }
    return;
}

    if (isCsvFile(ext)) {
        hideNonImageWidgets();
        if (fmCsvModel && fmCsvView) {
            fmCsvModel->clear();
            fmCurrentPreviewPath = path;
            QFile f(path);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream ts(&f);
                int row=0; QChar delim = ',';
                while (!ts.atEnd() && row<2000) {
                    const QString line = ts.readLine();
                    if (row == 0) {
                        // Auto-detect delimiter: ',', ';', or tab
                        int cComma = line.count(',');
                        int cSemi  = line.count(';');
                        int cTab   = line.count('\t');
                        if (cSemi > cComma && cSemi >= cTab) delim = ';';
                        else if (cTab > cComma && cTab >= cSemi) delim = '\t';
                    }
                    const QStringList cols = line.split(delim);
                    if (row==0) fmCsvModel->setColumnCount(cols.size());
                    QList<QStandardItem*> items; items.reserve(cols.size());
                    for (const QString &c : cols) items << new QStandardItem(c.trimmed());
                    fmCsvModel->appendRow(items);
                    ++row;
                }
                fmCsvView->resizeColumnsToContents();
                fmCsvView->show();
            } else {
                if (fmTextView) {
                    fmTextView->setPlainText("Preview not available");
                    fmTextView->show();
                }
            }
        }
        return;
    }

    if (isAudioFile(ext) || isVideoFile(ext)) {
        // Media branch: audio/video
        if (isVideoFile(ext)) {
            fmCurrentPreviewPath = path;
            if (fmVideoWidget) {
                fmVideoWidget->show();
                // CRITICAL: Set video widget AFTER show() to ensure valid window handle
                if (fmGStreamerPlayer) fmGStreamerPlayer->setVideoWidget(fmVideoWidget);
            }
            if (fmImageView) fmImageView->hide();
        } else {
            fmCurrentPreviewPath = path;
            if (fmVideoWidget) fmVideoWidget->hide();
            if (fmImageView) fmImageView->hide();
        }
        if (fmPlayPauseBtn) fmPlayPauseBtn->show();
        if (fmPositionSlider) fmPositionSlider->show();
        if (fmTimeLabel) fmTimeLabel->show();
        if (fmVolumeSlider) fmVolumeSlider->show();
        if (fmMuteBtn) fmMuteBtn->show();

        if (fmGStreamerPlayer) {
            fmGStreamerPlayer->loadMedia(path);
            fmGStreamerPlayer->pause();
            if (fmPlayPauseBtn) fmPlayPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor()));
        }
        return;
    }

    if (isExcelFile(ext) || isDocxFile(ext) || isDocFile(ext)) {
        hideNonImageWidgets();
        if (fmTextView) {
            fmTextView->setPlainText("Preview not available");
            fmTextView->show();
        }
        return;
    }

#ifdef HAVE_QT_PDF
    if (isAiFile(ext)) {
        // Many .ai files embed PDF — try to render with PDF engine
        auto err = fmPdfDoc ? fmPdfDoc->load(path) : QPdfDocument::Error::Unknown;
        if (fmPdfDoc && err == QPdfDocument::Error::None && fmPdfDoc->pageCount()>0) {
            hideNonImageWidgets();
        // Always render PDF pages into the image view for consistent zoom/pan
            fmPdfCurrentPage = 0;
            const QSizeF pts = fmPdfDoc->pagePointSize(fmPdfCurrentPage);
            int vw = fmImageView ? fmImageView->viewport()->width() : 800;
            if (vw < 1) vw = 800;
            int w = vw;
            int h = pts.width() > 0 ? int(pts.height() * (w / pts.width())) : w;
            QImage img = fmPdfDoc->render(fmPdfCurrentPage, QSize(w, h));
            if (!img.isNull() && fmImageItem) {
                // Composite onto white to avoid dark theme bleeding through
                if (img.hasAlphaChannel()) {
                    QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied);
                    bg.fill(Qt::white);
                    QPainter p(&bg);
                    p.drawImage(0, 0, img);
                    p.end();
                    img = bg;
                }
                fmImageItem->setPixmap(QPixmap::fromImage(img));
                if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                if (fmImageView) {
                    fmImageView->resetTransform();
                    fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                    fmImageFitToView = true;
                    fmImageView->setBackgroundBrush(Qt::white);
                    fmImageView->show();
                }
            }
            if (fmPdfPrevBtn) fmPdfPrevBtn->show(); if (fmPdfNextBtn) fmPdfNextBtn->show(); if (fmPdfPageLabel) { fmPdfPageLabel->show(); fmPdfPageLabel->setText(QString("%1/%2").arg(1).arg(fmPdfDoc->pageCount())); }
            #ifdef HAVE_QT_PDF_WIDGETS
            if (fmPdfView) fmPdfView->hide();
            #endif
            return;
        } else {
            qWarning() << "[PREVIEW] AI (PDF-embedded) load failed or no pages" << path;
        }
        hideNonImageWidgets();
        if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); }
        return;
    }
#else
    if (isAiFile(ext)) {
        hideNonImageWidgets();
        if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); }
        return;
    }
#endif

    clearFmPreview();
}


void MainWindow::loadFmSequenceFrame(int index)
{
    if (fmSequenceFramePaths.isEmpty()) return;
    if (index < 0) index = 0;
    if (index >= fmSequenceFramePaths.size()) index = fmSequenceFramePaths.size()-1;
    fmSequenceCurrentIndex = index;
    const QString path = fmSequenceFramePaths.at(index);
    QPixmap px;
    if (OIIOImageLoader::isOIIOSupported(path)) {
        QImage img = OIIOImageLoader::loadImage(path, 0, 0, OIIOImageLoader::ColorSpace::sRGB);
        if (!img.isNull()) px = QPixmap::fromImage(img);
    }
    if (px.isNull()) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QImage img = reader.read();
        if (!img.isNull()) px = QPixmap::fromImage(img);
    }
    if (!px.isNull()) {
        if (fmImageItem) {
            fmImageItem->setPixmap(px);
            fmImageItem->setTransformationMode(Qt::SmoothTransformation);
        }
        if (fmImageScene && fmImageItem) fmImageScene->setSceneRect(fmImageItem->boundingRect());
        if (fmImageView && fmImageItem) {
            fmImageView->resetTransform();
            fmImageView->centerOn(fmImageItem);
            fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
            fmImageFitToView = true;
            fmImageView->setBackgroundBrush(QColor("#0a0a0a"));
            fmImageView->show();
        }
    }
    if (fmPositionSlider) { fmPositionSlider->blockSignals(true); fmPositionSlider->setRange(0, fmSequenceFramePaths.size()-1); fmPositionSlider->setValue(index); fmPositionSlider->blockSignals(false); }
    if (fmTimeLabel) { fmTimeLabel->setText(QString("Frame %1 / %2").arg(fmSequenceStartFrame + index).arg(fmSequenceEndFrame)); }
}

void MainWindow::playFmSequence()
{
    if (!fmSequenceTimer) return;
    fmSequencePlaying = true;
    int intervalMs = qMax(1, int(1000.0 / (fmSequenceFps > 0.0 ? fmSequenceFps : 24.0)));
    fmSequenceTimer->start(intervalMs);
    if (fmPlayPauseBtn) fmPlayPauseBtn->setIcon(icoMediaPause(ThemeManager::instance().iconColor()));
}

void MainWindow::pauseFmSequence()
{
    if (!fmSequenceTimer) return;
    fmSequencePlaying = false;
    fmSequenceTimer->stop();
    if (fmPlayPauseBtn) fmPlayPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor()));
}

void MainWindow::stepFmSequence(int delta)
{
    if (!fmIsSequence) return;
    pauseFmSequence();
    int idx = fmSequenceCurrentIndex + delta;
    if (idx < 0) idx = 0;
    if (idx >= fmSequenceFramePaths.size()) idx = fmSequenceFramePaths.size()-1;
    loadFmSequenceFrame(idx);
}

void MainWindow::onFmSelectionChanged()
{
    QModelIndex idx;
    if (fmGridView->hasFocus()) {
        idx = fmGridView->currentIndex();
    } else if (fmListView->hasFocus()) {
        idx = fmListView->currentIndex();
    }
    if (!idx.isValid()) {
        auto sel = fmGridView->selectionModel()->selectedIndexes();
        if (!sel.isEmpty()) idx = sel.first();
    }
    if (!idx.isValid()) {
        auto sel = fmListView->selectionModel()->selectedIndexes();
        if (!sel.isEmpty()) idx = sel.first();
    }
    updateFmPreviewForIndex(idx);
    updateFmInfoPanel();
}

void MainWindow::onFmTogglePreview()
{
    if (!fmPreviewInfoSplitter) return;
    const bool show = fmPreviewToggleButton ? fmPreviewToggleButton->isChecked() : !fmPreviewInfoSplitter->isVisible();
    fmPreviewInfoSplitter->setVisible(show);
    if (!show) {
        if (fmGStreamerPlayer) { fmGStreamerPlayer->stop(); }
    } else {
        onFmSelectionChanged();
    }
    // Persist immediately
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/PreviewVisible", show);
}

void MainWindow::updateFmInfoPanel()
{
    if (!fmInfoPanel || !fmInfoPanel->isVisible()) return;

    // Get current selection
    QModelIndex idx;
    if (fmGridView && fmGridView->hasFocus()) {
        idx = fmGridView->currentIndex();
    } else if (fmListView && fmListView->hasFocus()) {
        idx = fmListView->currentIndex();
    }
    if (!idx.isValid()) {
        auto sel = fmGridView->selectionModel()->selectedIndexes();
        if (!sel.isEmpty()) idx = sel.first();
    }
    if (!idx.isValid()) {
        auto sel = fmListView->selectionModel()->selectedIndexes();
        if (!sel.isEmpty()) idx = sel.first();
    }

    if (!idx.isValid()) {
        // No selection - clear info panel
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

    // Map to source model if using proxy
    QModelIndex srcIdx = idx;
    if (fmProxyModel && idx.model() == fmProxyModel) {
        srcIdx = fmProxyModel->mapToSource(idx);
    }

    // Get file path from model
    QString filePath = fmDirModel ? fmDirModel->filePath(srcIdx) : QString();
    if (filePath.isEmpty()) return;

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) return;

    // Update file name
    if (fmInfoFileName) {
        fmInfoFileName->setText(fileInfo.fileName());
    }

    // Update file path
    if (fmInfoFilePath) {
        fmInfoFilePath->setText(fileInfo.absoluteFilePath());
    }

    // Update file size
    if (fmInfoFileSize) {
        qint64 fileSize = fileInfo.size();
        QString sizeStr;
        if (fileSize < 1024) {
            sizeStr = QString::number(fileSize) + " B";
        } else if (fileSize < 1024 * 1024) {
            sizeStr = QString::number(fileSize / 1024.0, 'f', 1) + " KB";
        } else if (fileSize < 1024 * 1024 * 1024) {
            sizeStr = QString::number(fileSize / (1024.0 * 1024.0), 'f', 1) + " MB";
        } else {
            sizeStr = QString::number(fileSize / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
        }
        fmInfoFileSize->setText("Size: " + sizeStr);
    }

    // Update file type
    if (fmInfoFileType) {
        QString fileType = fileInfo.suffix();
        fmInfoFileType->setText("Type: " + fileType.toUpper());
    }

    // Update dimensions for images and videos
    if (fmInfoDimensions) {
        QString dimensionsStr;
        QString ext = fileInfo.suffix().toLower();

        QStringList imageExts = {"jpg", "jpeg", "png", "gif", "bmp", "tiff", "tif", "webp",
                                 "exr", "hdr", "psd", "psb", "tga", "dng", "cr2", "cr3",
                                 "nef", "arw", "orf", "rw2", "pef", "srw", "raf", "raw"};

        if (imageExts.contains(ext)) {
            QImageReader reader(filePath);
            if (reader.canRead()) {
                QSize size = reader.size();
                QString format = reader.format();
                dimensionsStr = QString("Dimensions: %1 x %2 (%3)")
                    .arg(size.width()).arg(size.height()).arg(QString(format).toUpper());
            } else {
                dimensionsStr = "Dimensions: Unable to read";
            }
        } else {
            QStringList videoExts = {"mp4", "mov", "avi", "mkv", "wmv", "flv", "webm",
                                    "m4v", "mpg", "mpeg", "3gp", "mts", "m2ts", "mxf"};
            if (videoExts.contains(ext)) {
                // TODO: Extract video metadata using GStreamer
                // For now, just show "Video file"
                dimensionsStr = "Video file";
            }
        }

        fmInfoDimensions->setText(dimensionsStr);
    }

    // Update created date
    if (fmInfoCreated) {
        QDateTime created = fileInfo.birthTime();
        if (!created.isValid()) created = fileInfo.metadataChangeTime();
        fmInfoCreated->setText("Created: " + created.toString("yyyy-MM-dd hh:mm:ss"));
    }

    // Update modified date
    if (fmInfoModified) {
        QDateTime modified = fileInfo.lastModified();
        fmInfoModified->setText("Modified: " + modified.toString("yyyy-MM-dd hh:mm:ss"));
    }

    // Update permissions
    if (fmInfoPermissions) {
        QStringList perms;
        if (fileInfo.isReadable()) perms << "Read";
        if (fileInfo.isWritable()) perms << "Write";
        if (fileInfo.isExecutable()) perms << "Execute";
        fmInfoPermissions->setText("Permissions: " + perms.join(", "));
    }
}

void MainWindow::onFmOpenOverlay()
{
    // Toggle: if overlay is visible, close it
    if ((previewOverlay && previewOverlay->isVisible()) ||
        (imagePreviewOverlay && imagePreviewOverlay->isVisible())) {
        closePreview();
        return;
    }

    auto showFmOverlayForPath = [this](const QString &path) {
        QFileInfo info(path);
        const QString ext = info.suffix();
        const bool isImage = FileUtils::isImageFile(ext);
        const bool isVideo = FileUtils::isVideoFile(ext);

        if (isImage && !isVideo) {
            if (previewOverlay && previewOverlay->isVisible()) {
                previewOverlay->stopPlayback();
                previewOverlay->hide();
            }
            if (!imagePreviewOverlay) {
                imagePreviewOverlay = new ImagePreviewOverlay(this);
                imagePreviewOverlay->setGeometry(geometry());
                connect(imagePreviewOverlay, &ImagePreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(imagePreviewOverlay, &ImagePreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
            }
            imagePreviewOverlay->showImage(path, info.fileName(), info.suffix());
        } else {
            if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
                imagePreviewOverlay->stopPlayback();
                imagePreviewOverlay->hide();
            }
            if (!previewOverlay) {
                previewOverlay = new PreviewOverlay(this);
                previewOverlay->setGeometry(geometry());
                connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
            } else {
                previewOverlay->stopPlayback();
            }
            previewOverlay->showAsset(path, info.fileName(), info.suffix());
        }
    };

    // Check if secondary pane is active and has focus
    if (fmSecondaryPane && fmSecondaryPane->isVisible() && !fmPrimaryPaneActive) {
        QModelIndex idx = fmSecondaryPane->currentIndex();
        if (idx.isValid()) {
            // Get the file path from secondary pane
            QStringList paths = fmSecondaryPane->selectedPaths();
            if (!paths.isEmpty()) {
                QString path = paths.first();
                QFileInfo info(path);
                if (info.exists() && !info.isDir()) {
                    // Mark that overlay was opened from secondary pane for navigation
                    fmOverlayFromSecondaryPane = true;
                    fmOverlayCurrentIndex = QPersistentModelIndex(idx);
                    fmOverlaySourceView = nullptr;
                    showFmOverlayForPath(path);
                    return;
                }
            }
        }
    }

    // Determine current selection in FM and open full-screen overlay (primary pane)
    QModelIndex idx;
    if (fmGridView && fmGridView->hasFocus()) idx = fmGridView->currentIndex();
    else if (fmListView && fmListView->hasFocus()) idx = fmListView->currentIndex();
    if (!idx.isValid()) return;
    idx = idx.sibling(idx.row(), 0);

    // Record overlay navigation context (primary pane)
    fmOverlayFromSecondaryPane = false;  // Opening from primary pane
    fmOverlayCurrentIndex = QPersistentModelIndex(idx);
    fmOverlaySourceView = (fmGridView && fmGridView->hasFocus()) ? static_cast<QAbstractItemView*>(fmGridView) : static_cast<QAbstractItemView*>(fmListView);

    // If sequence grouping is enabled and the selection is a representative, open as sequence
    if (fmProxyModel && fmGroupSequences && idx.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(idx)) {
        auto info = fmProxyModel->infoForProxyIndex(idx);
        QStringList frames = reconstructSequenceFramePaths(info.reprPath, info.start, info.end);
        if (!frames.isEmpty()) {
            if (imagePreviewOverlay && imagePreviewOverlay->isVisible()) {
                imagePreviewOverlay->hide();
            }
            if (!previewOverlay) {
                previewOverlay = new PreviewOverlay(this);
                // Center overlay to the app window instead of screen top-left
                previewOverlay->setGeometry(geometry());
                connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
            } else {
                previewOverlay->stopPlayback();
            }
            int pad = 0;
            auto m = SequenceDetector::mainPattern().match(QFileInfo(info.reprPath).fileName());
            if (m.hasMatch()) pad = m.captured(3).length(); else pad = QString::number(info.start).length();
            QString s0 = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
            QString s1 = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
            QString seqName = QString("%1.[%2-%3].%4").arg(info.base, s0, s1, info.ext);
            previewOverlay->showSequence(frames, seqName, info.start, info.end);
            return;
        }
    }

    // Otherwise open single asset
    QModelIndex srcIdx = idx;
    if (fmProxyModel && idx.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(idx);
    const QString path = fmDirModel ? fmDirModel->filePath(srcIdx) : QString();
    if (path.isEmpty()) return;
    QFileInfo info(path);
    if (!info.exists()) return;
    showFmOverlayForPath(path);
}


void MainWindow::resizeEvent(QResizeEvent *event)
{
    // Mark that we're in a resize operation to skip expensive updates
    if (!m_windowResizing) {
        m_windowResizing = true;
        // Suspend new thumbnail decode requests for fluid resizing
        LivePreviewManager::instance().suspendRequests();
    }
    m_resizeSettleTimer.start(); // Restart timer - will fire when resize stops

    QMainWindow::resizeEvent(event);
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    // Mark that we're in a move operation to skip expensive updates
    if (!m_windowResizing) {
        m_windowResizing = true;
        // Suspend new thumbnail decode requests for fluid moving
        LivePreviewManager::instance().suspendRequests();
    }
    m_resizeSettleTimer.start(); // Restart timer - will fire when move stops

    QMainWindow::moveEvent(event);
}


void MainWindow::closeEvent(QCloseEvent* event)
{
    // Save current folder context before closing
    int currentFolderId = assetsModel->folderId();
    if (currentFolderId > 0) {
        ContextPreserver::FolderContext ctx;
        // Save scroll position
        if (isGridMode && assetGridView) {
            ctx.scrollPosition = assetGridView->verticalScrollBar()->value();
        } else if (!isGridMode && assetTableView) {
            ctx.scrollPosition = assetTableView->verticalScrollBar()->value();
        }
        ctx.isGridMode = isGridMode;
        ctx.searchText = searchBox->text();
        ctx.ratingFilter = ratingFilter->currentIndex() - 1;
        ctx.selectedAssetIds = selectedAssetIds;
        ctx.recursiveMode = recursiveCheckBox->isChecked();

        // Save selected tags
        QModelIndexList tagSelection = tagsListView->selectionModel()->selectedIndexes();
        for (const QModelIndex& idx : tagSelection) {
            int tagId = idx.data(TagsModel::IdRole).toInt();
            if (tagId > 0) ctx.selectedTagIds.insert(tagId);
        }

        ContextPreserver::instance().saveFolderContext(currentFolderId, ctx);
    }

    // Save current tab
    if (mainTabs) {
        ContextPreserver::instance().saveLastActiveTab(mainTabs->currentIndex());
    }

    // Persist Asset Manager folder tree expansion and scroll position
    saveFolderExpansionState();
    {
        QSettings treeSettings("AugmentCode", "KAssetManager");
        treeSettings.beginGroup("AssetManager/Tree");

        QVariantList expandedList;
        for (int id : expandedFolderIds) {
            expandedList << id;
        }
        treeSettings.setValue("ExpandedFolders", expandedList);

        int scrollPos = -1;
        if (folderTreeView && folderTreeView->verticalScrollBar()) {
            scrollPos = folderTreeView->verticalScrollBar()->value();
        }
        treeSettings.setValue("ScrollPosition", scrollPos);

        treeSettings.endGroup();
    }

    QSettings s("AugmentCode", "KAssetManager");
    // Window
    s.setValue("Window/Geometry", saveGeometry());
    s.setValue("Window/State", saveState());

    // Asset Manager
    if (mainSplitter) s.setValue("AssetManager/MainSplitter", mainSplitter->saveState());
    if (rightSplitter) s.setValue("AssetManager/RightSplitter", rightSplitter->saveState());
    s.setValue("AssetManager/ViewMode", isGridMode);
    if (assetTableView && assetTableView->model()) {

        auto hh = assetTableView->horizontalHeader();
        for (int c = 0; c < assetTableView->model()->columnCount(); ++c) {
            s.setValue(QString("AssetManager/AssetTable/Col%1").arg(c), hh->sectionSize(c));
        }
    }
    // Persist current File Manager path
    if (fmDirModel) s.setValue("FileManager/CurrentPath", fmDirModel->rootPath());


    // File Manager

    if (versionTable) {
        auto hh = versionTable->horizontalHeader();
        for (int c = 0; c < versionTable->columnCount(); ++c) {
            s.setValue(QString("AssetManager/VersionTable/Col%1").arg(c), hh->sectionSize(c));
        }
    }

    if (fmSplitter) {
        s.setValue("FileManager/MainSplitter", fmSplitter->saveState());
        QVariantList sizes; for (int v : fmSplitter->sizes()) sizes << v; s.setValue("FileManager/MainSplitterSizes", sizes);
    }
    if (fmLeftSplitter) {
        s.setValue("FileManager/LeftSplitter", fmLeftSplitter->saveState());
        QVariantList sizes; for (int v : fmLeftSplitter->sizes()) sizes << v; s.setValue("FileManager/LeftSplitterSizes", sizes);
    }
    if (fmRightSplitter) {
        s.setValue("FileManager/RightSplitter", fmRightSplitter->saveState());
        QVariantList sizes; for (int v : fmRightSplitter->sizes()) sizes << v; s.setValue("FileManager/RightSplitterSizes", sizes);
    }
    if (fmPreviewInfoSplitter) {
        s.setValue("FileManager/PreviewInfoSplitter", fmPreviewInfoSplitter->saveState());
        QVariantList sizes; for (int v : fmPreviewInfoSplitter->sizes()) sizes << v; s.setValue("FileManager/PreviewInfoSplitterSizes", sizes);
    }
    s.setValue("FileManager/ViewMode", fmIsGridMode);
    if (fmPreviewInfoSplitter) s.setValue("FileManager/PreviewVisible", fmPreviewInfoSplitter->isVisible());
    s.setValue("FileManager/GroupSequences", fmGroupSequences);
    if (fmListView && fmListView->model()) {
        auto hh = fmListView->horizontalHeader();
        for (int c = 0; c < fmListView->model()->columnCount(); ++c) {
            s.setValue(QString("FileManager/ListView/Col%1").arg(c), hh->sectionSize(c));
        }
    }
    if (fmTree && fmTree->model()) {
        auto th = fmTree->header();
        for (int c = 0; c < fmTree->model()->columnCount(); ++c) {
            s.setValue(QString("FileManager/Tree/Col%1").arg(c), th->sectionSize(c));
        }
    }
    saveProjectManagerState(s);

    s.sync();
    QMainWindow::closeEvent(event);
}


void MainWindow::applyFmShortcuts()
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


void MainWindow::setSequenceGroupingEnabled(bool enabled)
{
    fmGroupSequences = enabled;

    // File Manager: proxy grouping
    if (fmProxyModel) {
        fmProxyModel->setGroupingEnabled(enabled);
    }

    // Global sequence detection state for previews
    LivePreviewManager::instance().setSequenceDetectionEnabled(enabled);

    // Update scrub controllers to know about grouping state (for selective scrubbing)
    if (fmScrubController) {
        fmScrubController->setSequenceGroupingEnabled(enabled);
    }
    if (assetScrubController) {
        assetScrubController->setSequenceGroupingEnabled(enabled);
    }

    // Clear the cache to force regeneration of thumbnails with new settings
    LivePreviewManager::instance().clear();

    // Rebuild File Manager for current root
    if (fmDirModel && fmProxyModel) {
        const QString rootPath = fmDirModel->rootPath();
        if (!rootPath.isEmpty()) {
            fmProxyModel->rebuildForRoot(rootPath);
        }
    }

    // Force complete repaint of File Manager grid view to regenerate thumbnails
    if (fmGridView) {
        fmGridView->viewport()->update();
        // Also schedule delayed updates to catch async thumbnail loads
        QTimer::singleShot(100, fmGridView->viewport(), [this]() {
            if (fmGridView) fmGridView->viewport()->update();
        });
        QTimer::singleShot(500, fmGridView->viewport(), [this]() {
            if (fmGridView) fmGridView->viewport()->update();
        });
    }

    // Update File Manager toolbar button (without re-emitting signal)
    if (fmGroupSequencesCheckBox) {
        QSignalBlocker blockFm(fmGroupSequencesCheckBox);
        fmGroupSequencesCheckBox->setChecked(enabled);
    }

    // Persist File Manager group sequences setting
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/GroupSequences", enabled);
}

void MainWindow::setAssetManagerSequenceGroupingEnabled(bool enabled)
{
    // Asset Manager: proxy grouping (affects both grid and table views)
    if (amProxyModel) {
        amProxyModel->setGroupingEnabled(enabled);
    }

    // Update Asset Manager toolbar button (without re-emitting signal)
    if (amGroupSequencesButton) {
        QSignalBlocker blockAm(amGroupSequencesButton);
        amGroupSequencesButton->setChecked(enabled);
    }

    // Force repaint of Asset Manager views
    if (assetGridView) {
        assetGridView->viewport()->update();
        QTimer::singleShot(100, assetGridView->viewport(), [this]() {
            if (assetGridView) assetGridView->viewport()->update();
        });
    }
    if (assetTableView) {
        assetTableView->viewport()->update();
        QTimer::singleShot(100, assetTableView->viewport(), [this]() {
            if (assetTableView) assetTableView->viewport()->update();
        });
    }

    // Persist Asset Manager group sequences setting (separate from File Manager)
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("AssetManager/GroupSequences", enabled);
}

void MainWindow::onFmGroupSequencesToggled(bool checked)
{
    setSequenceGroupingEnabled(checked);
}

void MainWindow::onFmHideFoldersToggled(bool checked)
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

QKeySequence MainWindow::fmShortcutFor(const QString& actionName, const QKeySequence& def)
{
    QSettings s("AugmentCode", "KAssetManager");
    s.beginGroup("FileManager/Shortcuts");
    const QString stored = s.value(actionName).toString();
    s.endGroup();
    if (stored.isEmpty()) return def;
    return QKeySequence(stored);
}

void MainWindow::showDatabaseHealthDialog()
{
    DatabaseHealthDialog dialog(this);
    dialog.exec();
}

void MainWindow::onEverythingSearchAssetManager()
{
    EverythingSearchDialog dialog(EverythingSearchDialog::AssetManagerMode, this);
    connect(&dialog, &EverythingSearchDialog::importRequested, this, &MainWindow::onEverythingImportRequested);
    dialog.exec();
}

void MainWindow::onEverythingSearchFileManager()
{
    EverythingSearchDialog dialog(EverythingSearchDialog::FileManagerMode, this);
    if (dialog.exec() == QDialog::Accepted) {
        QStringList selectedPaths = dialog.getSelectedPaths();
        if (!selectedPaths.isEmpty() && fmDirModel) {
            // Navigate to the first selected file's directory
            QString firstPath = selectedPaths.first();
            QFileInfo fi(firstPath);
            if (fi.exists()) {
                QString dirPath = fi.absolutePath();

                // Set the root path in the file manager
                fmDirModel->setRootPath(dirPath);
                QModelIndex srcRoot = fmDirModel->index(dirPath);
                if (fmProxyModel) {
                    fmProxyModel->rebuildForRoot(dirPath);
                    QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
                    if (fmGridView) fmGridView->setRootIndex(proxyRoot);
                    if (fmListView) fmListView->setRootIndex(proxyRoot);
                }

                // Update tree selection
                if (fmTree) {
                    QModelIndex idx = fmIndexForPath(dirPath);
                    if (idx.isValid()) fmTree->setCurrentIndex(idx);
                }

                // Select the files in the view
                QTimer::singleShot(100, this, [this, selectedPaths]() {
                    // TODO: Select the files in fmGridView/fmListView
                    statusBar()->showMessage(QString("Found %1 file(s)").arg(selectedPaths.size()), 3000);
                });
            }
        }
    }
}

void MainWindow::onEverythingImportRequested(const QStringList& paths)
{
    if (paths.isEmpty()) {
        return;
    }

    // Import files into the asset library
    int successCount = 0;
    int failCount = 0;

    QProgressDialog progress("Importing files...", "Cancel", 0, paths.size(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    for (int i = 0; i < paths.size(); ++i) {
        if (progress.wasCanceled()) {
            break;
        }

        progress.setValue(i);
        progress.setLabelText(QString("Importing %1 of %2:\n%3")
            .arg(i + 1)
            .arg(paths.size())
            .arg(QFileInfo(paths[i]).fileName()));

        QApplication::processEvents();

        // Import the file
        QString filePath = paths[i];
        QFileInfo fi(filePath);

        if (fi.exists() && fi.isFile()) {
            // Get or create root folder
            int rootFolderId = DB::instance().ensureRootFolder();

            // Add asset to database using fast metadata insert
            int assetId = DB::instance().insertAssetMetadataFast(filePath, rootFolderId);
            if (assetId > 0) {
                successCount++;
            } else {
                failCount++;
            }
        } else {
            failCount++;
        }
    }

    progress.setValue(paths.size());

    // Refresh asset view
    if (assetsModel) {
        assetsModel->reload();
    }

    // Show result
    QString message = QString("Import complete:\n%1 succeeded, %2 failed")
        .arg(successCount)
        .arg(failCount);

    if (successCount > 0) {
        QMessageBox::information(this, "Import Complete", message);
    } else {
        QMessageBox::warning(this, "Import Failed", message);
    }

    statusBar()->showMessage(QString("Imported %1 file(s)").arg(successCount), 5000);
}

QString MainWindow::fmPathForIndex(const QModelIndex& idx) const
{
    if (!idx.isValid()) {
        return QString();
    }

    if (fmEverythingTreeModel && idx.model() == fmEverythingTreeModel) {
        return fmEverythingTreeModel->pathForIndex(idx);
    }
    if (fmTreeModel && idx.model() == fmTreeModel) {
        return fmTreeModel->filePath(idx);
    }

    if (fmEverythingTreeModel) {
        return fmEverythingTreeModel->pathForIndex(idx);
    }
    if (fmTreeModel) {
        return fmTreeModel->filePath(idx);
    }
    return QString();
}

QModelIndex MainWindow::fmIndexForPath(const QString& path)
{
    if (path.isEmpty()) {
        return QModelIndex();
    }
    QString normalized = QDir::toNativeSeparators(path);
    if (fmEverythingTreeModel) {
        QModelIndex idx = fmEverythingTreeModel->indexForPath(normalized);
        if (idx.isValid()) {
            return idx;
        }
    }
    if (fmTreeModel) {
        return fmTreeModel->index(normalized);
    }
    return QModelIndex();
}

void MainWindow::fmRefreshTreeModel()
{
    if (fmEverythingTreeModel) {
        const QString currentPath = fmDirModel ? fmDirModel->rootPath() : QString();
        fmEverythingTreeModel->refresh();
        if (!currentPath.isEmpty()) {
            QTimer::singleShot(0, this, [this, currentPath]() {
                fmScrollTreeToPath(currentPath);
            });
        }
    } else if (fmTreeModel) {
        fmTreeModel->setRootPath("");
    }
}

// File Manager Navigation Implementation
void MainWindow::fmNavigateToPath(const QString& path, bool addToHistory)
{
    if (path.isEmpty()) return;

    // Cancel any pending thumbnail generation immediately
    // This ensures user clicks take priority over background work
    LivePreviewManager::instance().cancelPending();

    // Add current path to history before navigating (if requested)
    if (addToHistory && fmDirModel) {
        QString currentPath = fmDirModel->rootPath();
        if (!currentPath.isEmpty() && currentPath != path) {
            // Remove any forward history when navigating to a new location
            while (fmNavigationIndex < fmNavigationHistory.size() - 1) {
                fmNavigationHistory.removeLast();
            }
            // Add current path to history
            fmNavigationHistory.append(currentPath);
            fmNavigationIndex = fmNavigationHistory.size() - 1;
        }
    }

    // Navigate to the new path
    fmDirModel->setRootPath(path);

    // Update path bar
    if (fmPathBar) {
        fmPathBar->setText(QDir::toNativeSeparators(path));
    }

    // Update directory watcher to current path
    if (fmDirectoryWatcher) {
        const QStringList watched = fmDirectoryWatcher->directories();
        if (!watched.isEmpty()) fmDirectoryWatcher->removePaths(watched);
        fmDirectoryWatcher->addPath(path);
    }

    QModelIndex srcRoot = fmDirModel->index(path);
    if (fmProxyModel) {
        fmProxyModel->rebuildForRoot(path);
        QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
        fmGridView->setRootIndex(proxyRoot);
        fmListView->setRootIndex(proxyRoot);
    } else {
        fmGridView->setRootIndex(srcRoot);
        fmListView->setRootIndex(srcRoot);
    }

    // Scroll tree to show the current folder
    fmScrollTreeToPath(path);

    // Persist current path
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/CurrentPath", path);

    // Update navigation button states
    fmUpdateNavigationButtons();
}

void MainWindow::fmScrollTreeToPath(const QString& path)
{
    if (fmSuppressTreeSync) return;
    if (!fmTree || path.isEmpty()) return;

    QModelIndex treeIdx = fmIndexForPath(path);
    if (treeIdx.isValid()) {
        // Check if this is the full path or just a partial resolution
        QString resolvedPath;
        if (fmEverythingTreeModel) {
            resolvedPath = fmEverythingTreeModel->pathForIndex(treeIdx);
        } else if (fmTreeModel) {
            resolvedPath = fmTreeModel->filePath(treeIdx);
        }
        
        // Normalize for comparison
        QString normalizedPath = QDir::toNativeSeparators(path);
        QString normalizedResolved = QDir::toNativeSeparators(resolvedPath);
        
        if (normalizedResolved.compare(normalizedPath, Qt::CaseInsensitive) != 0) {
            // Partial resolution - the model needs to fetch more data
            // Store the pending path and wait for childrenFetched signal
            fmPendingTreeScrollPath = path;
            qDebug() << "[FileManager] fmScrollTreeToPath: partial resolution, waiting for async fetch"
                     << "requested:" << path << "resolved:" << resolvedPath;
            
            // Trigger async fetch for the resolved node
            if (fmEverythingTreeModel) {
                fmEverythingTreeModel->resolvePathAsync(path);
            }
            return;
        }
        
        // Full resolution - proceed with tree selection
        fmPendingTreeScrollPath.clear();
        
        QItemSelectionModel *sel = fmTree->selectionModel();
        QSignalBlocker blocker(sel);

        // Expand all parent folders
        QModelIndex p = treeIdx.parent();
        while (p.isValid()) {
            fmTree->expand(p);
            p = p.parent();
        }
        // Select and scroll to the folder (vertically only)
        fmTree->setCurrentIndex(treeIdx);
        fmTree->scrollTo(treeIdx, QAbstractItemView::PositionAtCenter);
        // Keep horizontal scroll at the left - user doesn't want auto-scroll right for long names
        if (fmTree->horizontalScrollBar()) {
            fmTree->horizontalScrollBar()->setValue(0);
        }
    } else {
        // Index not valid - may need async fetch
        fmPendingTreeScrollPath = path;
        qDebug() << "[FileManager] fmScrollTreeToPath: index not valid, triggering async resolve for" << path;
        if (fmEverythingTreeModel) {
            fmEverythingTreeModel->resolvePathAsync(path);
        }
    }
}

void MainWindow::onFmTreeChildrenFetched(const QModelIndex &parent)
{
    Q_UNUSED(parent);
    
    // If we have a pending path to scroll to, try again
    if (!fmPendingTreeScrollPath.isEmpty()) {
        QString pendingPath = fmPendingTreeScrollPath;
        qDebug() << "[FileManager] onFmTreeChildrenFetched: retrying scroll to" << pendingPath;
        
        // Clear first to avoid infinite loops
        fmPendingTreeScrollPath.clear();
        
        // Retry the scroll - use a short delay to let the model update
        QTimer::singleShot(10, this, [this, pendingPath]() {
            fmScrollTreeToPath(pendingPath);
        });
    }
}

void MainWindow::fmUpdateNavigationButtons()
{
    if (fmBackButton) {
        fmBackButton->setEnabled(fmNavigationIndex > 0);
    }
    if (fmUpButton) {
        QString currentPath = fmDirModel ? fmDirModel->rootPath() : QString();
        if (!currentPath.isEmpty()) {
            QDir dir(currentPath);
            fmUpButton->setEnabled(dir.cdUp());
        } else {
            fmUpButton->setEnabled(false);
        }
    }
}

void MainWindow::onFmNavigateBack()
{
    if (fmNavigationIndex <= 0 || fmNavigationHistory.isEmpty()) return;

    fmNavigationIndex--;
    QString path = fmNavigationHistory[fmNavigationIndex];

    // Navigate without adding to history
    fmNavigateToPath(path, false);
}

void MainWindow::applyTheme()
{
    // Apply theme to main window and all child widgets
    // Most styling is handled by the global stylesheet in ThemeManager
    // This method updates icons and special palette cases

    // Get icon color for current theme
    QColor iconColor = ThemeManager::instance().iconColor();

    // Update all toolbar button icons with theme-appropriate colors
    if (amBackButton) amBackButton->setIcon(icoBack(iconColor));
    if (amUpButton) amUpButton->setIcon(icoUp(iconColor));
    if (amNewFolderButton) amNewFolderButton->setIcon(icoFolderNew(iconColor));
    if (viewModeButton) viewModeButton->setIcon(isGridMode ? icoGrid(iconColor) : icoList(iconColor));
    if (amGroupSequencesButton) amGroupSequencesButton->setIcon(icoGroup(iconColor));
    if (thumbGenButton) thumbGenButton->setIcon(icoRefresh(iconColor));
    if (refreshButton) refreshButton->setIcon(icoRefresh(iconColor));
    if (fmBackButton) fmBackButton->setIcon(icoBack(iconColor));
    if (fmUpButton) fmUpButton->setIcon(icoUp(iconColor));
    if (fmViewModeButton) fmViewModeButton->setIcon(fmIsGridMode ? icoGrid(iconColor) : icoList(iconColor));
    if (fmGroupSequencesCheckBox) fmGroupSequencesCheckBox->setIcon(icoGroup(iconColor));
    if (fmHideFoldersCheckBox) fmHideFoldersCheckBox->setIcon(icoHide(iconColor));
    if (fmPreviewToggleButton) fmPreviewToggleButton->setIcon(icoEye(iconColor));
    if (fmPrevFrameBtn) fmPrevFrameBtn->setIcon(icoMediaPrevFrame(iconColor));
    if (fmPlayPauseBtn) fmPlayPauseBtn->setIcon(icoMediaPlay(iconColor));
    if (fmNextFrameBtn) fmNextFrameBtn->setIcon(icoMediaNextFrame(iconColor));
    if (fmMuteBtn) fmMuteBtn->setIcon(icoMediaAudio(iconColor));

    // Update File Manager toolbar button icons
    if (fmNewFolderBtn) fmNewFolderBtn->setIcon(icoFolderNew(iconColor));
    if (fmCopyBtn) fmCopyBtn->setIcon(icoCopy(iconColor));
    if (fmCutBtn) fmCutBtn->setIcon(icoCut(iconColor));
    if (fmPasteBtn) fmPasteBtn->setIcon(icoPaste(iconColor));
    if (fmDeleteBtn) fmDeleteBtn->setIcon(icoDelete(iconColor));
    if (fmRenameBtn) fmRenameBtn->setIcon(icoRename(iconColor));
    if (fmAddToLibraryBtn) fmAddToLibraryBtn->setIcon(icoAdd(iconColor));
    if (fmSearchButton) fmSearchButton->setIcon(icoSearch(iconColor));

    // Update Project Manager icons
    if (pmPrevFrameBtn) pmPrevFrameBtn->setIcon(icoMediaPrevFrame(iconColor));
    if (pmPlayPauseBtn) pmPlayPauseBtn->setIcon(icoMediaPlay(iconColor));
    if (pmNextFrameBtn) pmNextFrameBtn->setIcon(icoMediaNextFrame(iconColor));
    if (pmMuteBtn) pmMuteBtn->setIcon(icoMediaAudio(iconColor));
    if (pmGroupSequencesBtn) pmGroupSequencesBtn->setIcon(icoGroup(iconColor));
    // pmShowAllVersionsButton uses text, no icon

    // Update pmInfoVersions label with accent color (special case for link-style text)
    if (pmInfoVersions) {
        QPalette p = pmInfoVersions->palette();
        p.setColor(QPalette::WindowText, QColor("#58a6ff"));
        pmInfoVersions->setPalette(p);
    }

    // Force repaint
    update();

    LogManager::instance().addLog("[THEME] Theme applied successfully", "INFO");
}

void MainWindow::onFmNavigateUp()
{
    if (!fmDirModel) return;
    QString currentPath = fmDirModel->rootPath();
    if (currentPath.isEmpty()) return;

    QDir dir(currentPath);
    if (dir.cdUp()) {
        QString parentPath = dir.absolutePath();
        fmNavigateToPath(parentPath, true);
    }
}
