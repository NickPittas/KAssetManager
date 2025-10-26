#include "mainwindow.h"
#include "virtual_folders.h"
#include "assets_model.h"
#include "assets_table_model.h"
#include "tags_model.h"
#include "importer.h"
#include "db.h"
#include "preview_overlay.h"
#include "thumbnail_generator.h"
#include "import_progress_dialog.h"
#include "settings_dialog.h"
#include "star_rating_widget.h"
#include "project_folder_watcher.h"
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
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QProgressDialog>
#include <QProgressBar>
#include <QDirIterator>
#include <QStatusBar>
#include <QDrag>
#include <QMouseEvent>
#include <QFuture>
#include <QFutureWatcher>
#include <QTimer>
#include <QScrollBar>
#include <QTableView>
#include <QStackedWidget>
#include <QCheckBox>
#include <QFileDialog>

// Custom QListView with compact drag pixmap
class AssetGridView : public QListView
{
public:
    explicit AssetGridView(QWidget *parent = nullptr) : QListView(parent) {}

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        QModelIndexList indexes = selectionModel()->selectedIndexes();
        if (indexes.isEmpty()) {
            return;
        }

        // Create mime data
        QMimeData *mimeData = model()->mimeData(indexes);
        if (!mimeData) {
            return;
        }

        // Create a compact drag pixmap showing count
        int count = indexes.size();
        QPixmap pixmap(80, 80);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw a rounded rectangle background
        painter.setBrush(QColor(88, 166, 255, 200));
        painter.setPen(QPen(QColor(255, 255, 255), 2));
        painter.drawRoundedRect(5, 5, 70, 70, 8, 8);

        // Draw count text
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPixelSize(32);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRect(5, 5, 70, 70), Qt::AlignCenter, QString::number(count));

        painter.end();

        // Start the drag with custom pixmap
        QDrag *drag = new QDrag(this);
        drag->setMimeData(mimeData);
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(40, 40)); // Center of the pixmap

        Qt::DropAction defaultAction = Qt::MoveAction;
        if (supportedActions & Qt::MoveAction) {
            defaultAction = Qt::MoveAction;
        }

        drag->exec(supportedActions, defaultAction);
    }
};

// Custom delegate for asset grid view with thumbnails
class AssetItemDelegate : public QStyledItemDelegate
{
public:
    explicit AssetItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent), m_thumbnailSize(180) {}

    void setThumbnailSize(int size) { m_thumbnailSize = size; }
    int thumbnailSize() const { return m_thumbnailSize; }

    // Cache for loaded pixmaps to avoid repeated file I/O
    mutable QHash<QString, QPixmap> pixmapCache;

private:
    int m_thumbnailSize;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        try {
            painter->save();

            // Get thumbnail path
            QString thumbnailPath = index.data(AssetsModel::ThumbnailPathRole).toString();

            // PERFORMANCE: Lazy loading - if thumbnail doesn't exist, request it
            if (thumbnailPath.isEmpty()) {
                QString filePath = index.data(AssetsModel::FilePathRole).toString();
                if (!filePath.isEmpty()) {
                    // Request thumbnail generation asynchronously
                    ThumbnailGenerator::instance().requestThumbnail(filePath);
                }
                painter->restore();
                return; // Don't draw anything yet - thumbnail is being generated
            }

            // CRITICAL FIX: If thumbnail exists on disk but not in cache, load it now
            if (!pixmapCache.contains(thumbnailPath)) {
                QFileInfo thumbInfo(thumbnailPath);
                if (thumbInfo.exists() && thumbInfo.size() > 0) {
                    QPixmap pixmap(thumbnailPath);
                    if (!pixmap.isNull()) {
                        // PERFORMANCE: Increased cache size from 200 to 1000 for better performance
                        // At 256x256 thumbnails, this is ~250MB of memory which is reasonable
                        if (pixmapCache.size() > 1000) {
                            pixmapCache.clear();
                        }
                        pixmapCache.insert(thumbnailPath, pixmap);
                    }
                }
            }

            // If thumbnail is still not in cache, don't draw anything
            if (!pixmapCache.contains(thumbnailPath)) {
                painter->restore();
                return; // Don't draw anything - not even the card background
            }

            QPixmap pixmap = pixmapCache.value(thumbnailPath);

            if (pixmap.isNull()) {
                painter->restore();
                return; // Invalid pixmap - don't draw anything
            }

            // Now we have a valid thumbnail - draw the card
            // Background
            if (option.state & QStyle::State_Selected) {
                painter->fillRect(option.rect, QColor(47, 58, 74)); // Accent background
            } else if (option.state & QStyle::State_MouseOver) {
                painter->fillRect(option.rect, QColor(32, 32, 32)); // Hover
            } else {
                painter->fillRect(option.rect, QColor(18, 18, 18)); // Default
            }

            // Border
            if (option.state & QStyle::State_Selected) {
                painter->setPen(QPen(QColor(88, 166, 255), 2));
                painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
            }

            // Draw thumbnail
            QRect thumbRect = option.rect.adjusted(8, 8, -8, -8);
            QPixmap scaled = pixmap.scaled(thumbRect.size(), Qt::KeepAspectRatio, Qt::FastTransformation);
            int x = thumbRect.x() + (thumbRect.width() - scaled.width()) / 2;
            int y = thumbRect.y() + (thumbRect.height() - scaled.height()) / 2;
            painter->drawPixmap(x, y, scaled);

            // File name overlay at the bottom with semi-transparent background
            QString fileName = index.data(AssetsModel::FileNameRole).toString();
            QString fileType = index.data(AssetsModel::FileTypeRole).toString().toUpper();
            int rating = index.data(AssetsModel::RatingRole).toInt();

            // Calculate text height needed
            QFont nameFont("Segoe UI", 9);
            QFontMetrics nameFm(nameFont);
            QFont typeFont("Segoe UI", 8);
            QFontMetrics typeFm(typeFont);
            QFont starFont("Segoe UI", 10);
            QFontMetrics starFm(starFont);

            // Use elided text to fit in one line
            int availableWidth = thumbRect.width() - 16; // 8px padding on each side
            QString elidedName = nameFm.elidedText(fileName, Qt::ElideRight, availableWidth);

            int nameHeight = nameFm.height();
            int typeHeight = typeFm.height();
            int totalTextHeight = nameHeight + typeHeight + 8; // 8px padding

            // Draw semi-transparent background for text
            QRect textBgRect(thumbRect.left(), thumbRect.bottom() - totalTextHeight, thumbRect.width(), totalTextHeight);
            painter->fillRect(textBgRect, QColor(0, 0, 0, 180));

            // Draw file name
            QRect nameRect = textBgRect.adjusted(8, 4, -8, -typeHeight - 4);
            painter->setPen(QColor(255, 255, 255));
            painter->setFont(nameFont);
            painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

            // Draw file type
            QRect typeRect = textBgRect.adjusted(8, nameHeight + 4, -8, -4);
            painter->setPen(QColor(160, 160, 160));
            painter->setFont(typeFont);
            painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignVCenter, fileType);

            // Draw sequence badge if this is a sequence
            bool isSequence = index.data(AssetsModel::IsSequenceRole).toBool();
            if (isSequence) {
                int frameCount = index.data(AssetsModel::SequenceFrameCountRole).toInt();
                int startFrame = index.data(AssetsModel::SequenceStartFrameRole).toInt();
                int endFrame = index.data(AssetsModel::SequenceEndFrameRole).toInt();

                // Draw badge in top-right corner
                QString badgeText = QString("%1 frames").arg(frameCount);
                QFont badgeFont("Segoe UI", 8, QFont::Bold);
                QFontMetrics badgeFm(badgeFont);
                int badgeWidth = badgeFm.horizontalAdvance(badgeText) + 12;
                int badgeHeight = 18;
                int badgeX = thumbRect.right() - badgeWidth - 4;
                int badgeY = thumbRect.top() + 4;

                QRect badgeRect(badgeX, badgeY, badgeWidth, badgeHeight);
                painter->fillRect(badgeRect, QColor(70, 130, 180, 220)); // Steel blue
                painter->setPen(QPen(QColor(255, 255, 255, 200), 1));
                painter->drawRect(badgeRect);

                painter->setFont(badgeFont);
                painter->setPen(Qt::white);
                painter->drawText(badgeRect, Qt::AlignCenter, badgeText);
            }

            // Draw rating stars (if rated)
            if (rating > 0 && rating <= 5) {
                QString stars;
                for (int i = 0; i < 5; i++) {
                    stars += (i < rating) ? "★" : "☆";
                }
                QRect starRect(thumbRect.left() + 4, thumbRect.top() + 4, thumbRect.width() - 8, starFm.height() + 4);
                painter->fillRect(starRect, QColor(0, 0, 0, 180));
                painter->setPen(QColor(255, 215, 0)); // Gold color
                painter->setFont(starFont);
                painter->drawText(starRect, Qt::AlignCenter, stars);
            }

            // Selection checkmark
            if (option.state & QStyle::State_Selected) {
                QRect checkRect(option.rect.right() - 28, option.rect.top() + 4, 24, 24);
                painter->setBrush(QColor(88, 166, 255));
                painter->setPen(Qt::NoPen);
                painter->drawEllipse(checkRect);
                painter->setPen(QColor(255, 255, 255));
                painter->setFont(QFont("Segoe UI", 12, QFont::Bold));
                painter->drawText(checkRect, Qt::AlignCenter, "✓");
            }

            painter->restore();
        } catch (const std::exception& e) {
            qCritical() << "[AssetItemDelegate] Exception in paint():" << e.what();
            painter->restore();
        } catch (...) {
            qCritical() << "[AssetItemDelegate] Unknown exception in paint()";
            painter->restore();
        }
    }

public:
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        int height = m_thumbnailSize + 60; // Add space for text overlay
        return QSize(m_thumbnailSize, height);
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , anchorIndex(-1)
    , currentAssetId(-1)
    , previewIndex(-1)
    , previewOverlay(nullptr)
    , importer(nullptr)
    , projectFolderWatcher(nullptr)
    , assetsLocked(true) // Locked by default
{
    setupUi();
    setupConnections();

    setWindowTitle("KAsset Manager");
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

    // Setup thumbnail progress bar in status bar
    thumbnailProgressLabel = new QLabel(this);
    thumbnailProgressLabel->setVisible(false);
    thumbnailProgressBar = new QProgressBar(this);
    thumbnailProgressBar->setVisible(false);
    thumbnailProgressBar->setMaximumWidth(200);
    thumbnailProgressBar->setTextVisible(true);
    statusBar()->addPermanentWidget(thumbnailProgressLabel);
    statusBar()->addPermanentWidget(thumbnailProgressBar);

    // Connect thumbnail generator progress
    connect(&ThumbnailGenerator::instance(), &ThumbnailGenerator::progressChanged,
            this, &MainWindow::onThumbnailProgress);

    // Load thumbnail into cache when generated, then refresh view
    connect(&ThumbnailGenerator::instance(), &ThumbnailGenerator::thumbnailGenerated,
            this, [this](const QString& filePath, const QString& thumbnailPath) {
        try {
            // Load thumbnail into delegate cache
            AssetItemDelegate *delegate = static_cast<AssetItemDelegate*>(assetGridView->itemDelegate());

            if (delegate && !thumbnailPath.isEmpty()) {
                QFileInfo thumbInfo(thumbnailPath);

                if (thumbInfo.exists() && thumbInfo.size() > 0) {
                    QPixmap pixmap(thumbnailPath);

                    if (!pixmap.isNull()) {
                        // PERFORMANCE: Increased cache size from 200 to 1000 for better performance
                        if (delegate->pixmapCache.size() > 1000) {
                            delegate->pixmapCache.clear();
                        }
                        delegate->pixmapCache.insert(thumbnailPath, pixmap);
                    }
                }
            }
            // Refresh view to show the new thumbnail
            assetGridView->viewport()->update();
        } catch (const std::exception& e) {
            qCritical() << "[MainWindow] Exception loading thumbnail:" << e.what();
        } catch (...) {
            qCritical() << "[MainWindow] Unknown exception loading thumbnail";
        }
    });
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUi()
{
    // Menu bar
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    QMenu* fileMenu = menuBar->addMenu("&File");
    fileMenu->setStyleSheet(
        "QMenu { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; }"
        "QMenu::item:selected { background-color: #2f3a4a; }"
    );

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

    // Main splitter: left (folders) | center (assets) | right (filters+info)
    mainSplitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(mainSplitter);
    
    // Left panel: Folder tree
    folderTreeView = new QTreeView(this);
    folderModel = new VirtualFolderTreeModel(this);
    folderTreeView->setModel(folderModel);
    folderTreeView->setHeaderHidden(true);
    folderTreeView->setContextMenuPolicy(Qt::CustomContextMenu);
    folderTreeView->setStyleSheet(
        "QTreeView { background-color: #121212; color: #ffffff; border: none; }"
        "QTreeView::item:selected { background-color: #2f3a4a; }"
        "QTreeView::item:hover { background-color: #202020; }"
    );

    // Expand root folder by default
    folderTreeView->expandToDepth(0);

    // Center panel: Asset grid with toolbar
    QWidget* centerPanel = new QWidget(this);
    QVBoxLayout* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    // Toolbar for view controls
    QWidget* toolbar = new QWidget(centerPanel);
    toolbar->setStyleSheet("QWidget { background-color: #1a1a1a; border-bottom: 1px solid #333; }");
    QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);
    toolbarLayout->setSpacing(8);

    // View mode toggle button
    isGridMode = true;
    viewModeButton = new QPushButton("⊞ Grid", toolbar);
    viewModeButton->setFixedSize(80, 28);
    viewModeButton->setStyleSheet(
        "QPushButton { background-color: #2a2a2a; color: #ffffff; border: 1px solid #333; border-radius: 4px; font-size: 12px; }"
        "QPushButton:hover { background-color: #333; }"
    );
    viewModeButton->setToolTip("Toggle between Grid and List view");
    connect(viewModeButton, &QPushButton::clicked, this, &MainWindow::onViewModeChanged);
    toolbarLayout->addWidget(viewModeButton);

    // Thumbnail size label
    QLabel* sizeLabel = new QLabel("Size:", toolbar);
    sizeLabel->setStyleSheet("color: #ffffff; font-size: 12px;");
    toolbarLayout->addWidget(sizeLabel);

    // Thumbnail size slider
    thumbnailSizeSlider = new QSlider(Qt::Horizontal, toolbar);
    thumbnailSizeSlider->setRange(100, 400);
    thumbnailSizeSlider->setValue(180);
    thumbnailSizeSlider->setFixedWidth(150);
    thumbnailSizeSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #333; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #58a6ff; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: #4a8fd9; }"
    );
    thumbnailSizeSlider->setToolTip("Adjust thumbnail size");
    connect(thumbnailSizeSlider, &QSlider::valueChanged, this, &MainWindow::onThumbnailSizeChanged);
    toolbarLayout->addWidget(thumbnailSizeSlider);

    // Size value label
    QLabel* sizeValueLabel = new QLabel("180px", toolbar);
    sizeValueLabel->setStyleSheet("color: #999; font-size: 11px; min-width: 45px;");
    connect(thumbnailSizeSlider, &QSlider::valueChanged, [sizeValueLabel](int value) {
        sizeValueLabel->setText(QString("%1px").arg(value));
    });
    toolbarLayout->addWidget(sizeValueLabel);

    toolbarLayout->addStretch();

    // Lock checkbox for project folders
    lockCheckBox = new QCheckBox("🔒 Lock Assets", toolbar);
    lockCheckBox->setChecked(true); // Locked by default
    lockCheckBox->setStyleSheet(
        "QCheckBox { color: #ff4444; font-size: 12px; font-weight: bold; }"
        "QCheckBox::indicator { width: 16px; height: 16px; }"
        "QCheckBox::indicator:checked { background-color: #ff4444; border: 1px solid #ff4444; }"
        "QCheckBox::indicator:unchecked { background-color: #2a2a2a; border: 1px solid #666; }"
    );
    lockCheckBox->setToolTip("When locked, assets can only be moved within their project folder");
    connect(lockCheckBox, &QCheckBox::toggled, this, &MainWindow::onLockToggled);
    toolbarLayout->addWidget(lockCheckBox);

    // Refresh button
    refreshButton = new QPushButton("🔄 Refresh", toolbar);
    refreshButton->setFixedSize(90, 28);
    refreshButton->setStyleSheet(
        "QPushButton { background-color: #2a2a2a; color: #ffffff; border: 1px solid #333; border-radius: 4px; font-size: 12px; }"
        "QPushButton:hover { background-color: #333; }"
    );
    refreshButton->setToolTip("Refresh assets from project folders");
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshAssets);
    toolbarLayout->addWidget(refreshButton);

    centerLayout->addWidget(toolbar);

    // Stacked widget to switch between grid and table views
    viewStack = new QStackedWidget(centerPanel);

    // Asset grid view (using custom AssetGridView with compact drag pixmap)
    assetGridView = new AssetGridView(viewStack);
    assetsModel = new AssetsModel(viewStack);
    assetGridView->setModel(assetsModel);
    assetGridView->setViewMode(QListView::IconMode);
    assetGridView->setResizeMode(QListView::Adjust);
    assetGridView->setSpacing(8);
    assetGridView->setUniformItemSizes(true);
    assetGridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    assetGridView->setContextMenuPolicy(Qt::CustomContextMenu);
    assetGridView->setItemDelegate(new AssetItemDelegate(viewStack));
    assetGridView->setIconSize(QSize(180, 180));
    assetGridView->setStyleSheet(
        "QListView { background-color: #0a0a0a; border: none; }"
    );
    viewStack->addWidget(assetGridView); // Index 0

    // Asset table view for list mode
    assetTableView = new QTableView(viewStack);
    AssetsTableModel* tableModel = new AssetsTableModel(assetsModel, viewStack);
    assetTableView->setModel(tableModel);
    assetTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    assetTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    assetTableView->setContextMenuPolicy(Qt::CustomContextMenu);
    assetTableView->setSortingEnabled(true);
    assetTableView->setAlternatingRowColors(true);
    assetTableView->verticalHeader()->setVisible(false);
    assetTableView->horizontalHeader()->setStretchLastSection(true);
    assetTableView->setStyleSheet(
        "QTableView { background-color: #0a0a0a; color: #ffffff; border: none; gridline-color: #1a1a1a; }"
        "QTableView::item:selected { background-color: #2f3a4a; }"
        "QTableView::item:hover { background-color: #1a1a1a; }"
        "QHeaderView::section { background-color: #1a1a1a; color: #ffffff; border: none; padding: 4px; }"
    );
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

    // Enable drag-and-drop on folder tree for moving assets to folders AND reorganizing folders
    folderTreeView->setDragEnabled(true);
    folderTreeView->setAcceptDrops(true);
    folderTreeView->setDropIndicatorShown(true);
    folderTreeView->setDragDropMode(QAbstractItemView::DragDrop);
    folderTreeView->setDefaultDropAction(Qt::MoveAction);
    folderTreeView->viewport()->installEventFilter(this);
    
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
    filtersTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #ffffff;");
    filtersLayout->addWidget(filtersTitle);
    
    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText("Search...");
    searchBox->setStyleSheet(
        "QLineEdit { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; padding: 6px; border-radius: 4px; }"
    );
    filtersLayout->addWidget(searchBox);
    
    QLabel *ratingLabel = new QLabel("Rating:", this);
    ratingLabel->setStyleSheet("color: #ffffff; margin-top: 8px;");
    filtersLayout->addWidget(ratingLabel);
    
    ratingFilter = new QComboBox(this);
    ratingFilter->addItems({"All", "5 Stars", "4+ Stars", "3+ Stars", "Unrated"});
    ratingFilter->setStyleSheet(
        "QComboBox { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; padding: 6px; border-radius: 4px; }"
    );
    connect(ratingFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        assetsModel->setRatingFilter(index);
    });
    filtersLayout->addWidget(ratingFilter);

    // Tags section with + button
    QHBoxLayout *tagsHeaderLayout = new QHBoxLayout();
    QLabel *tagsLabel = new QLabel("Tags:", this);
    tagsLabel->setStyleSheet("color: #ffffff; margin-top: 8px;");
    tagsHeaderLayout->addWidget(tagsLabel);
    tagsHeaderLayout->addStretch();

    QPushButton *addTagBtn = new QPushButton("+", this);
    addTagBtn->setFixedSize(24, 24);
    addTagBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: #ffffff; border: none; border-radius: 12px; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #4a8fd9; }"
    );
    addTagBtn->setToolTip("Create new tag");
    connect(addTagBtn, &QPushButton::clicked, this, &MainWindow::onCreateTag);
    tagsHeaderLayout->addWidget(addTagBtn);

    filtersLayout->addLayout(tagsHeaderLayout);

    tagsListView = new QListView(this);
    tagsModel = new TagsModel(this);
    tagsListView->setModel(tagsModel);
    tagsListView->setSelectionMode(QAbstractItemView::MultiSelection);
    tagsListView->setContextMenuPolicy(Qt::CustomContextMenu);
    tagsListView->setStyleSheet(
        "QListView { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; }"
        "QListView::item:selected { background-color: #2f3a4a; }"
        "QListView::item:hover { background-color: #202020; }"
    );
    tagsListView->setMaximumHeight(150);

    // Enable drops on tags list for assigning tags to assets
    tagsListView->setAcceptDrops(true);
    tagsListView->setDropIndicatorShown(true);
    tagsListView->setDragDropMode(QAbstractItemView::DropOnly);
    tagsListView->viewport()->installEventFilter(this);

    filtersLayout->addWidget(tagsListView);

    // Tag action buttons
    QHBoxLayout *tagButtonsLayout = new QHBoxLayout();

    applyTagsBtn = new QPushButton("Apply", this);
    applyTagsBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: #ffffff; border: none; padding: 6px 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8fd9; }"
        "QPushButton:disabled { background-color: #333; color: #666; }"
    );
    applyTagsBtn->setToolTip("Apply selected tags to selected assets");
    applyTagsBtn->setEnabled(false);
    connect(applyTagsBtn, &QPushButton::clicked, this, &MainWindow::onApplyTags);
    tagButtonsLayout->addWidget(applyTagsBtn);

    filterByTagsBtn = new QPushButton("Filter", this);
    filterByTagsBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: #ffffff; border: none; padding: 6px 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8fd9; }"
        "QPushButton:disabled { background-color: #333; color: #666; }"
    );
    filterByTagsBtn->setToolTip("Filter assets by selected tags");
    filterByTagsBtn->setEnabled(false);
    connect(filterByTagsBtn, &QPushButton::clicked, this, &MainWindow::onFilterByTags);
    tagButtonsLayout->addWidget(filterByTagsBtn);

    // AND/OR mode selector
    tagFilterModeCombo = new QComboBox(this);
    tagFilterModeCombo->addItems({"AND", "OR"});
    tagFilterModeCombo->setCurrentIndex(0); // Default to AND
    tagFilterModeCombo->setStyleSheet(
        "QComboBox { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; padding: 4px 8px; border-radius: 4px; }"
    );
    tagFilterModeCombo->setToolTip("AND: Assets must have ALL selected tags\nOR: Assets must have ANY selected tag");
    tagButtonsLayout->addWidget(tagFilterModeCombo);

    filtersLayout->addLayout(tagButtonsLayout);
    
    QPushButton *applyFiltersBtn = new QPushButton("Apply Filters", this);
    applyFiltersBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: #ffffff; border: none; padding: 8px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8fd9; }"
    );
    connect(applyFiltersBtn, &QPushButton::clicked, this, &MainWindow::applyFilters);
    filtersLayout->addWidget(applyFiltersBtn);
    
    QPushButton *clearFiltersBtn = new QPushButton("Clear Filters", this);
    clearFiltersBtn->setStyleSheet(
        "QPushButton { background-color: #333; color: #ffffff; border: none; padding: 8px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #444; }"
    );
    connect(clearFiltersBtn, &QPushButton::clicked, this, &MainWindow::clearFilters);
    filtersLayout->addWidget(clearFiltersBtn);
    
    filtersLayout->addStretch();
    filtersPanel->setStyleSheet("background-color: #121212;");
    
    // Info panel
    infoPanel = new QWidget(this);
    QVBoxLayout *infoLayout = new QVBoxLayout(infoPanel);
    infoLayout->setContentsMargins(8, 8, 8, 8);
    
    QLabel *infoTitle = new QLabel("Asset Info", this);
    infoTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #ffffff;");
    infoLayout->addWidget(infoTitle);
    
    infoFileName = new QLabel("No selection", this);
    infoFileName->setStyleSheet("color: #ffffff; margin-top: 8px;");
    infoFileName->setWordWrap(true);
    infoLayout->addWidget(infoFileName);
    
    infoFilePath = new QLabel("", this);
    infoFilePath->setStyleSheet("color: #999; font-size: 10px;");
    infoFilePath->setWordWrap(true);
    infoLayout->addWidget(infoFilePath);
    
    infoFileSize = new QLabel("", this);
    infoFileSize->setStyleSheet("color: #ccc;");
    infoLayout->addWidget(infoFileSize);
    
    infoFileType = new QLabel("", this);
    infoFileType->setStyleSheet("color: #ccc;");
    infoLayout->addWidget(infoFileType);
    
    infoModified = new QLabel("", this);
    infoModified->setStyleSheet("color: #ccc;");
    infoLayout->addWidget(infoModified);

    // Rating widget
    infoRatingLabel = new QLabel("Rating:", this);
    infoRatingLabel->setStyleSheet("color: #ccc; margin-top: 8px;");
    infoLayout->addWidget(infoRatingLabel);

    infoRatingWidget = new StarRatingWidget(this);
    infoLayout->addWidget(infoRatingWidget);
    connect(infoRatingWidget, &StarRatingWidget::ratingChanged, this, &MainWindow::onRatingChanged);

    infoTags = new QLabel("", this);
    infoTags->setStyleSheet("color: #ccc; margin-top: 8px;");
    infoTags->setWordWrap(true);
    infoLayout->addWidget(infoTags);
    
    infoLayout->addStretch();
    infoPanel->setStyleSheet("background-color: #121212;");
    
    rightLayout->addWidget(filtersPanel, 1);
    rightLayout->addWidget(infoPanel, 1);
    
    // Add panels to main splitter
    mainSplitter->addWidget(folderTreeView);
    mainSplitter->addWidget(centerPanel);
    mainSplitter->addWidget(rightPanel);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 3);
    mainSplitter->setStretchFactor(2, 1);
    
    // Load initial data
    folderModel->reload();
    tagsModel->reload();
    if (folderModel->rowCount(QModelIndex()) > 0) {
        QModelIndex firstFolder = folderModel->index(0, 0, QModelIndex());
        folderTreeView->setCurrentIndex(firstFolder);
        onFolderSelected(firstFolder);
    }
}

void MainWindow::setupConnections()
{
    connect(folderTreeView, &QTreeView::clicked, this, &MainWindow::onFolderSelected);
    connect(folderTreeView, &QTreeView::customContextMenuRequested, this, &MainWindow::onFolderContextMenu);

    // Save/restore folder expansion state when model reloads
    connect(folderModel, &VirtualFolderTreeModel::modelAboutToBeReset, this, &MainWindow::saveFolderExpansionState);
    connect(folderModel, &VirtualFolderTreeModel::modelReset, this, &MainWindow::restoreFolderExpansionState);

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

    // Connect search box for real-time filtering
    connect(searchBox, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);

}

void MainWindow::onFolderSelected(const QModelIndex &index)
{
    qDebug() << "===== MainWindow::onFolderSelected - START";

    if (!index.isValid()) {
        qWarning() << "MainWindow::onFolderSelected - Invalid index";
        return;
    }

    int folderId = index.data(VirtualFolderTreeModel::IdRole).toInt();
    QString folderName = index.data(VirtualFolderTreeModel::NameRole).toString();
    qDebug() << "MainWindow::onFolderSelected - Folder ID:" << folderId << "Name:" << folderName;

    if (folderId <= 0) {
        qWarning() << "MainWindow::onFolderSelected - Invalid folder ID:" << folderId;
        return;
    }

    try {
        qDebug() << "MainWindow::onFolderSelected - Calling assetsModel->setFolderId()...";
        assetsModel->setFolderId(folderId);
        qDebug() << "MainWindow::onFolderSelected - setFolderId() completed";

        qDebug() << "MainWindow::onFolderSelected - Clearing selection...";
        clearSelection();
        qDebug() << "MainWindow::onFolderSelected - Selection cleared";

        qDebug() << "MainWindow::onFolderSelected - Updating info panel...";
        updateInfoPanel();
        qDebug() << "MainWindow::onFolderSelected - Info panel updated";

        qDebug() << "===== MainWindow::onFolderSelected - SUCCESS";
    } catch (const std::exception& e) {
        qCritical() << "===== MainWindow::onFolderSelected - EXCEPTION:" << e.what();
        QMessageBox::critical(this, "Error", QString("Failed to load folder: %1").arg(e.what()));
    } catch (...) {
        qCritical() << "===== MainWindow::onFolderSelected - UNKNOWN EXCEPTION";
        QMessageBox::critical(this, "Error", "Failed to load folder: Unknown error");
    }
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
    if (isGridMode) {
        index = assetGridView->indexAt(pos);
    } else {
        index = assetTableView->indexAt(pos);
    }

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; }"
        "QMenu::item:selected { background-color: #2f3a4a; }"
    );

    if (index.isValid()) {
        // Asset context menu
        QAction *openAction = menu.addAction("Open Preview");
        QAction *showInExplorerAction = menu.addAction("Show in Explorer");
        menu.addSeparator();

        // Move to Folder submenu
        QMenu *moveToMenu = menu.addMenu("Move to Folder");
        moveToMenu->setStyleSheet(menu.styleSheet());

        // Get all folders from the model
        QList<QPair<int, QString>> folders;
        std::function<void(const QModelIndex&, int)> collectFolders = [&](const QModelIndex& parent, int depth) {
            int rowCount = folderModel->rowCount(parent);
            for (int i = 0; i < rowCount; ++i) {
                QModelIndex idx = folderModel->index(i, 0, parent);
                int folderId = folderModel->data(idx, VirtualFolderTreeModel::IdRole).toInt();
                QString folderName = folderModel->data(idx, Qt::DisplayRole).toString();
                QString indent = QString("  ").repeated(depth);
                folders.append({folderId, indent + folderName});
                collectFolders(idx, depth + 1);
            }
        };
        collectFolders(QModelIndex(), 0);

        for (const auto& folder : folders) {
            QAction *folderAction = moveToMenu->addAction(folder.second);
            folderAction->setData(folder.first);
        }

        // Assign Tag submenu
        QMenu *assignTagMenu = menu.addMenu("Assign Tag");
        assignTagMenu->setStyleSheet(menu.styleSheet());

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
        setRatingMenu->setStyleSheet(menu.styleSheet());

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
        QAction *removeAction = menu.addAction("Remove from App");

        QAction *selected = menu.exec(assetGridView->mapToGlobal(pos));

        if (selected == openAction) {
            showPreview(index.row());
        } else if (selected == showInExplorerAction) {
            QString filePath = index.data(AssetsModel::FilePathRole).toString();
            QFileInfo fileInfo(filePath);
            QStringList args;
            args << "/select," + QDir::toNativeSeparators(fileInfo.absoluteFilePath());
            QProcess::startDetached("explorer", args);
        } else if (selected && moveToMenu->actions().contains(selected)) {
            // Move to folder action
            int targetFolderId = selected->data().toInt();
            QSet<int> selectedIds = getSelectedAssetIds();

            // Check if locked and if move is allowed
            if (assetsLocked) {
                // Check if all assets and target are in the same project folder
                bool canMove = true;
                int sourceProjectFolderId = -1;
                int targetProjectFolderId = -1;

                // Get target folder's project folder ID
                QModelIndex targetIdx = QModelIndex();
                for (int row = 0; row < folderModel->rowCount(QModelIndex()); ++row) {
                    QModelIndex idx = folderModel->index(row, 0, QModelIndex());
                    if (folderModel->data(idx, VirtualFolderTreeModel::IdRole).toInt() == targetFolderId) {
                        targetIdx = idx;
                        break;
                    }
                }

                // Walk up to find if target is in a project folder
                QModelIndex current = targetIdx;
                while (current.isValid()) {
                    if (folderModel->data(current, VirtualFolderTreeModel::IsProjectFolderRole).toBool()) {
                        targetProjectFolderId = folderModel->data(current, VirtualFolderTreeModel::ProjectFolderIdRole).toInt();
                        break;
                    }
                    current = folderModel->parent(current);
                }

                // Check source assets
                for (int assetId : selectedIds) {
                    // Get asset's current folder
                    QSqlQuery q(DB::instance().database());
                    q.prepare("SELECT virtual_folder_id FROM assets WHERE id=?");
                    q.addBindValue(assetId);
                    if (q.exec() && q.next()) {
                        int assetFolderId = q.value(0).toInt();

                        // Find if asset is in a project folder
                        int assetProjectFolderId = -1;
                        for (int row = 0; row < folderModel->rowCount(QModelIndex()); ++row) {
                            QModelIndex idx = folderModel->index(row, 0, QModelIndex());
                            if (folderModel->data(idx, VirtualFolderTreeModel::IdRole).toInt() == assetFolderId) {
                                QModelIndex cur = idx;
                                while (cur.isValid()) {
                                    if (folderModel->data(cur, VirtualFolderTreeModel::IsProjectFolderRole).toBool()) {
                                        assetProjectFolderId = folderModel->data(cur, VirtualFolderTreeModel::ProjectFolderIdRole).toInt();
                                        break;
                                    }
                                    cur = folderModel->parent(cur);
                                }
                                break;
                            }
                        }

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
                    return;
                }
            }

            for (int assetId : selectedIds) {
                DB::instance().setAssetFolder(assetId, targetFolderId);
            }

            assetsModel->reload();
            statusBar()->showMessage(QString("Moved %1 asset(s) to folder").arg(selectedIds.size()), 3000);
        } else if (selected && assignTagMenu->actions().contains(selected)) {
            // Assign tag action
            int tagId = selected->data().toInt();
            QSet<int> selectedIds = getSelectedAssetIds();
            QList<int> assetIdsList = selectedIds.values();
            QList<int> tagIds = {tagId};

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
    QAction *renameAction = menu.addAction("Rename");
    QAction *deleteAction = nullptr;

    // Only allow deletion of non-project folders
    if (!isProjectFolder) {
        deleteAction = menu.addAction("Delete");
    } else {
        deleteAction = menu.addAction("Remove Project Folder");
    }

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
                folderModel->reload();
                statusBar()->showMessage(QString("Created subfolder '%1'").arg(name), 3000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to create subfolder");
            }
        }
    } else if (selected == renameAction) {
        // Rename folder
        bool ok;
        QString newName = QInputDialog::getText(this, "Rename Folder",
                                               "Enter new name:",
                                               QLineEdit::Normal, folderName, &ok);
        if (ok && !newName.isEmpty() && newName != folderName) {
            // If it's a project folder, use the project folder rename method
            if (isProjectFolder) {
                if (DB::instance().renameProjectFolder(projectFolderId, newName)) {
                    folderModel->reload();
                    statusBar()->showMessage(QString("Renamed project folder to '%1'").arg(newName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to rename project folder");
                }
            } else {
                if (DB::instance().renameFolder(folderId, newName)) {
                    folderModel->reload();
                    statusBar()->showMessage(QString("Renamed folder to '%1'").arg(newName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to rename folder");
                }
            }
        }
    } else if (selected == deleteAction) {
        if (isProjectFolder) {
            // Remove project folder
            QMessageBox::StandardButton reply = QMessageBox::question(
                this, "Remove Project Folder",
                QString("Are you sure you want to remove project folder '%1'?\n\nThis will remove the folder and all its assets from the library, but will not delete the actual files.").arg(folderName),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                // Remove from watcher first
                projectFolderWatcher->removeProjectFolder(projectFolderId);

                if (DB::instance().deleteProjectFolder(projectFolderId)) {
                    folderModel->reload();
                    assetsModel->reload();
                    statusBar()->showMessage(QString("Removed project folder '%1'").arg(folderName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to remove project folder");
                }
            }
        } else {
            // Delete regular folder
            QMessageBox::StandardButton reply = QMessageBox::question(
                this, "Delete Folder",
                QString("Are you sure you want to delete '%1' and all its contents?").arg(folderName),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                if (DB::instance().deleteFolder(folderId)) {
                    folderModel->reload();
                    assetsModel->reload();
                    statusBar()->showMessage(QString("Deleted folder '%1'").arg(folderName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to delete folder");
                }
            }
        }
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

    QString filePath = modelIndex.data(AssetsModel::FilePathRole).toString();
    QString fileName = modelIndex.data(AssetsModel::FileNameRole).toString();
    QString fileType = modelIndex.data(AssetsModel::FileTypeRole).toString();
    bool isSequence = modelIndex.data(AssetsModel::IsSequenceRole).toBool();

    qDebug() << "[MainWindow::showPreview] File:" << fileName << "Type:" << fileType << "IsSequence:" << isSequence;

    if (!previewOverlay) {
        qDebug() << "[MainWindow::showPreview] Creating new PreviewOverlay";
        previewOverlay = new PreviewOverlay(this);
        previewOverlay->setGeometry(rect());

        connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
        connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changePreview);
    } else {
        // CRITICAL FIX: Stop any playing media before loading new content
        qDebug() << "[MainWindow::showPreview] Reusing existing PreviewOverlay - stopping current playback";
        previewOverlay->stopPlayback();
    }

    if (isSequence) {
        qDebug() << "[MainWindow::showPreview] Opening as sequence";

        // Get sequence information
        QString sequencePattern = modelIndex.data(AssetsModel::SequencePatternRole).toString();
        int startFrame = modelIndex.data(AssetsModel::SequenceStartFrameRole).toInt();
        int endFrame = modelIndex.data(AssetsModel::SequenceEndFrameRole).toInt();
        int frameCount = modelIndex.data(AssetsModel::SequenceFrameCountRole).toInt();

        qDebug() << "[MainWindow::showPreview] Sequence info - Pattern:" << sequencePattern << "Start:" << startFrame << "End:" << endFrame << "Count:" << frameCount;

        // Reconstruct frame paths from first frame path and pattern
        QStringList framePaths = reconstructSequenceFramePaths(filePath, startFrame, endFrame);

        qDebug() << "[MainWindow] Opening sequence:" << sequencePattern << "frames:" << startFrame << "-" << endFrame << "paths:" << framePaths.size();

        if (framePaths.isEmpty()) {
            qWarning() << "[MainWindow::showPreview] No frame paths reconstructed! Cannot show sequence.";
            QMessageBox::warning(this, "Error", "Failed to reconstruct sequence frame paths.");
            return;
        }

        previewOverlay->showSequence(framePaths, sequencePattern, startFrame, endFrame);
        qDebug() << "[MainWindow::showPreview] showSequence() called";
    } else {
        qDebug() << "[MainWindow::showPreview] Opening as regular asset";
        previewOverlay->showAsset(filePath, fileName, fileType);
        qDebug() << "[MainWindow::showPreview] showAsset() called";
    }

    qDebug() << "[MainWindow::showPreview] Preview overlay visible:" << previewOverlay->isVisible();
}

void MainWindow::closePreview()
{
    previewIndex = -1;
    if (previewOverlay) {
        previewOverlay->hide();
        previewOverlay->deleteLater();
        previewOverlay = nullptr;
    }
}

void MainWindow::changePreview(int delta)
{
    if (previewIndex < 0) return;

    int newIndex = previewIndex + delta;
    if (newIndex >= 0 && newIndex < assetsModel->rowCount(QModelIndex())) {
        showPreview(newIndex);
    }
}



QItemSelectionModel* MainWindow::getCurrentSelectionModel()
{
    return isGridMode ? assetGridView->selectionModel() : assetTableView->selectionModel();
}

void MainWindow::updateInfoPanel()
{
    QModelIndexList selected = getCurrentSelectionModel()->selectedIndexes();

    if (selected.isEmpty()) {
        infoFileName->setText("No selection");
        infoFilePath->clear();
        infoFileSize->clear();
        infoFileType->clear();
        infoModified->clear();
        infoRatingLabel->setVisible(false);
        infoRatingWidget->setVisible(false);
        infoTags->clear();
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

        infoFileName->setText(fileName);
        infoFilePath->setText(filePath);

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
        infoFileSize->setText("Size: " + sizeStr);

        infoFileType->setText("Type: " + fileType.toUpper());
        infoModified->setText("Modified: " + modified.toString("yyyy-MM-dd hh:mm"));

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
        } else {
            infoTags->setText("Tags: " + tags.join(", "));
        }
    } else {
        infoFileName->setText(QString("%1 assets selected").arg(selected.size()));
        infoFilePath->clear();
        infoFileSize->clear();
        infoFileType->clear();
        infoModified->clear();
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
    qDebug() << "Applying tags" << tagIds << "to assets" << assetIdList;
    if (DB::instance().assignTagsToAssets(assetIdList, tagIds)) {
        statusBar()->showMessage(QString("Applied %1 tag(s) to %2 asset(s)").arg(tagIds.size()).arg(assetIds.size()), 3000);
        updateInfoPanel();
        qDebug() << "Tags applied successfully";
    } else {
        qDebug() << "Failed to apply tags";
        QMessageBox::warning(this, "Error", "Failed to apply tags");
    }
}

void MainWindow::onFilterByTags()
{
    // Get selected tags
    QModelIndexList selectedTagIndexes = tagsListView->selectionModel()->selectedIndexes();
    if (selectedTagIndexes.isEmpty()) {
        // Clear tag filter
        assetsModel->setSelectedTagNames(QStringList());
        statusBar()->showMessage("Tag filter cleared", 2000);
        return;
    }

    // Collect tag names
    QStringList tagNames;
    for (const QModelIndex &index : selectedTagIndexes) {
        QString tagName = index.data(TagsModel::NameRole).toString();
        if (!tagName.isEmpty()) {
            tagNames.append(tagName);
        }
    }

    if (tagNames.isEmpty()) {
        return;
    }

    // Get selected filter mode (AND or OR)
    int mode = tagFilterModeCombo->currentIndex(); // 0 = AND, 1 = OR
    QString modeText = (mode == AssetsModel::And) ? "AND" : "OR";

    // Apply filter
    assetsModel->setSelectedTagNames(tagNames);
    assetsModel->setTagFilterMode(mode);

    QString message;
    if (tagNames.size() == 1) {
        message = QString("Filtering by tag: %1").arg(tagNames.first());
    } else {
        message = QString("Filtering by %1 tag(s) (%2 logic)").arg(tagNames.size()).arg(modeText);
    }
    statusBar()->showMessage(message, 3000);
}

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
        // Rename tag
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
        // Delete tag with confirmation
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
    qDebug() << "MainWindow::dragEnterEvent - hasUrls:" << event->mimeData()->hasUrls();

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

    qDebug() << "MainWindow::dropEvent - hasUrls:" << mimeData->hasUrls();

    if (mimeData->hasUrls()) {
        QStringList filePaths;
        QStringList folderPaths;
        QList<QUrl> urls = mimeData->urls();

        qDebug() << "Drop URLs count:" << urls.size();

        // Get currently selected folder ID
        QModelIndex currentFolderIndex = folderTreeView->currentIndex();
        int parentFolderId = 0;
        if (currentFolderIndex.isValid()) {
            parentFolderId = folderModel->data(currentFolderIndex, VirtualFolderTreeModel::IdRole).toInt();
        }
        if (parentFolderId <= 0) {
            parentFolderId = folderModel->rootId();
        }

        for (const QUrl &url : urls) {
            qDebug() << "Processing URL:" << url.toString();

            if (url.isLocalFile()) {
                QString path = url.toLocalFile();
                QFileInfo info(path);

                qDebug() << "Local file path:" << path << "isFile:" << info.isFile() << "isDir:" << info.isDir();

                if (info.isFile()) {
                    filePaths.append(path);
                } else if (info.isDir()) {
                    // Keep directories separate to preserve structure
                    folderPaths.append(path);
                }
            }
        }

        qDebug() << "Files to import:" << filePaths.size() << "Folders to import:" << folderPaths.size();

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
            qDebug() << "Importing folder with structure:" << folderPath;
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

    // Start thumbnail generation for all assets in current folder
    QList<int> assetIds;
    for (int row = 0; row < assetsModel->rowCount(QModelIndex()); ++row) {
        QModelIndex index = assetsModel->index(row, 0);
        int assetId = index.data(AssetsModel::IdRole).toInt();
        assetIds.append(assetId);
    }

    if (!assetIds.isEmpty()) {
        qDebug() << "[MainWindow] Starting thumbnail generation for" << assetIds.size() << "assets";

        // Get file paths for all assets
        QStringList filePaths;
        for (int assetId : assetIds) {
            QString filePath = DB::instance().getAssetFilePath(assetId);
            if (!filePath.isEmpty()) {
                filePaths.append(filePath);
            }
        }

        // Start thumbnail generation
        ThumbnailGenerator::instance().startProgress(filePaths.size());
        for (const QString &filePath : filePaths) {
            ThumbnailGenerator::instance().requestThumbnail(filePath);
        }
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Handle drops on folder tree
    if (watched == folderTreeView->viewport()) {
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

                // Handle asset drops
                if (mimeData->hasFormat("application/x-kasset-asset-ids")) {
                    // Decode asset IDs
                    QByteArray encodedData = mimeData->data("application/x-kasset-asset-ids");
                    QDataStream stream(&encodedData, QIODevice::ReadOnly);
                    QList<int> assetIds;
                    stream >> assetIds;

                    qDebug() << "Drop assets on folder" << targetFolderId << "- moving" << assetIds.size() << "assets";

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

                    // Move assets to folder
                    for (int assetId : assetIds) {
                        assetsModel->moveAssetToFolder(assetId, targetFolderId);
                    }

                    statusBar()->showMessage(QString("Moved %1 asset(s) to folder").arg(assetIds.size()), 3000);
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

                    qDebug() << "Drop folders on folder" << targetFolderId << "- moving" << folderIds.size() << "folders";

                    // Move folders to new parent
                    bool success = true;
                    for (int folderId : folderIds) {
                        // Don't allow moving a folder into itself or its descendants
                        if (folderId == targetFolderId) {
                            QMessageBox::warning(this, "Error", "Cannot move a folder into itself");
                            success = false;
                            continue;
                        }

                        if (folderModel->moveFolder(folderId, targetFolderId)) {
                            qDebug() << "Moved folder" << folderId << "to parent" << targetFolderId;
                        } else {
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

                    qDebug() << "Drop assets on tag" << tagName << "- assigning to" << assetIds.size() << "assets";

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

                    qDebug() << "Drop folders on tag" << tagName << "- assigning to all assets in" << folderIds.size() << "folders";

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
    qDebug() << "Restored expansion state for" << expandedFolderIds.size() << "folders";
}

void MainWindow::onOpenSettings()
{
    SettingsDialog dialog(this);
    dialog.exec();
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

    qDebug() << "Thumbnail size changed to:" << size;
}

void MainWindow::onViewModeChanged()
{
    isGridMode = !isGridMode;

    if (isGridMode) {
        // Switch to grid mode
        viewModeButton->setText("⊞ Grid");
        viewStack->setCurrentIndex(0); // Show grid view
        thumbnailSizeSlider->setEnabled(true);

        qDebug() << "Switched to Grid view";
    } else {
        // Switch to list mode (table view)
        viewModeButton->setText("☰ List");
        viewStack->setCurrentIndex(1); // Show table view
        thumbnailSizeSlider->setEnabled(false);

        qDebug() << "Switched to List view (table)";
    }
}

// Called when thumbnail generation progress updates
void MainWindow::onThumbnailProgress(int current, int total)
{
    if (total > 0) {
        thumbnailProgressLabel->setText(QString("Generating thumbnails:"));
        thumbnailProgressLabel->setVisible(true);
        thumbnailProgressBar->setMaximum(total);
        thumbnailProgressBar->setValue(current);
        thumbnailProgressBar->setFormat(QString("%1/%2 (%p%)").arg(current).arg(total));
        thumbnailProgressBar->setVisible(true);

        // Hide when complete
        if (current >= total) {
            QTimer::singleShot(2000, this, [this]() {
                thumbnailProgressLabel->setVisible(false);
                thumbnailProgressBar->setVisible(false);
            });
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

    qDebug() << "[MainWindow] Reconstructing sequence from:" << fileName;
    qDebug() << "[MainWindow] Base:" << baseName << "Padding:" << paddingLength << "Suffix:" << suffix;
    qDebug() << "[MainWindow] Frame range:" << startFrame << "-" << endFrame;

    // Reconstruct all frame paths
    for (int frame = startFrame; frame <= endFrame; ++frame) {
        QString frameNum = QString("%1").arg(frame, paddingLength, 10, QChar('0'));
        QString framePath = QDir(dirPath).filePath(baseName + frameNum + suffix);

        // Only add if file exists
        if (QFile::exists(framePath)) {
            framePaths.append(framePath);
        }
    }

    qDebug() << "[MainWindow] Reconstructed" << framePaths.size() << "frame paths for sequence";
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
    qDebug() << "MainWindow::onLockToggled - Assets locked:" << assetsLocked;

    if (checked) {
        statusBar()->showMessage("Assets locked - can only move within project folders", 3000);
    } else {
        statusBar()->showMessage("Assets unlocked - can move freely", 3000);
    }
}

void MainWindow::onProjectFolderChanged(int projectFolderId, const QString& path)
{
    qDebug() << "MainWindow::onProjectFolderChanged" << projectFolderId << path;

    // Re-import the folder to pick up new/changed files
    // This will update existing assets and add new ones
    statusBar()->showMessage(QString("Refreshing project folder: %1").arg(QFileInfo(path).fileName()), 2000);

    // Import the folder (this will upsert assets)
    importFiles(QStringList() << path);
}
