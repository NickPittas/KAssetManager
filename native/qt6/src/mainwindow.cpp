#include "mainwindow.h"
#include "virtual_folders.h"
#include "assets_model.h"
#include "tags_model.h"
#include "importer.h"
#include "db.h"
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QMessageBox>
#include <QInputDialog>
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
#include <QDirIterator>
#include <QStatusBar>
#include <QDrag>
#include <QMouseEvent>

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
    explicit AssetItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        
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
        
        // Thumbnail
        QString thumbnailPath = index.data(AssetsModel::ThumbnailPathRole).toString();
        QRect thumbRect = option.rect.adjusted(8, 8, -8, -60);
        
        if (!thumbnailPath.isEmpty()) {
            QPixmap pixmap(thumbnailPath);
            if (!pixmap.isNull()) {
                QPixmap scaled = pixmap.scaled(thumbRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                int x = thumbRect.x() + (thumbRect.width() - scaled.width()) / 2;
                int y = thumbRect.y() + (thumbRect.height() - scaled.height()) / 2;
                painter->drawPixmap(x, y, scaled);
            }
        }
        
        // File name
        QString fileName = index.data(AssetsModel::FileNameRole).toString();
        QRect textRect = option.rect.adjusted(8, option.rect.height() - 50, -8, -30);
        painter->setPen(QColor(255, 255, 255));
        painter->setFont(QFont("Segoe UI", 9));
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, fileName);
        
        // File type
        QString fileType = index.data(AssetsModel::FileTypeRole).toString().toUpper();
        QRect typeRect = option.rect.adjusted(8, option.rect.height() - 25, -8, -8);
        painter->setPen(QColor(160, 160, 160));
        painter->setFont(QFont("Segoe UI", 8));
        painter->drawText(typeRect, Qt::AlignLeft | Qt::AlignVCenter, fileType);
        
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
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(180, 200);
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , anchorIndex(-1)
    , currentAssetId(-1)
    , previewIndex(-1)
    , previewOverlay(nullptr)
    , importer(nullptr)
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
    connect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);
}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUi()
{
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
    
    // Center panel: Asset grid (using custom AssetGridView with compact drag pixmap)
    assetGridView = new AssetGridView(this);
    assetsModel = new AssetsModel(this);
    assetGridView->setModel(assetsModel);
    assetGridView->setViewMode(QListView::IconMode);
    assetGridView->setResizeMode(QListView::Adjust);
    assetGridView->setSpacing(8);
    assetGridView->setUniformItemSizes(true);
    assetGridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    assetGridView->setContextMenuPolicy(Qt::CustomContextMenu);
    assetGridView->setItemDelegate(new AssetItemDelegate(this));
    assetGridView->setStyleSheet(
        "QListView { background-color: #0a0a0a; border: none; }"
    );

    // Enable drag-and-drop
    assetGridView->setDragEnabled(true);
    assetGridView->setAcceptDrops(false);
    assetGridView->setDragDropMode(QAbstractItemView::DragOnly);
    assetGridView->setDefaultDropAction(Qt::MoveAction);
    assetGridView->setSelectionRectVisible(false);

    // Enable drops on folder tree for moving assets to folders
    folderTreeView->setAcceptDrops(true);
    folderTreeView->setDropIndicatorShown(true);
    folderTreeView->setDragDropMode(QAbstractItemView::DropOnly);
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
    filterByTagsBtn->setToolTip("Filter assets by selected tags (AND logic)");
    filterByTagsBtn->setEnabled(false);
    connect(filterByTagsBtn, &QPushButton::clicked, this, &MainWindow::onFilterByTags);
    tagButtonsLayout->addWidget(filterByTagsBtn);

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
    
    infoRating = new QLabel("", this);
    infoRating->setStyleSheet("color: #ccc;");
    infoLayout->addWidget(infoRating);
    
    infoTags = new QLabel("", this);
    infoTags->setStyleSheet("color: #ccc;");
    infoTags->setWordWrap(true);
    infoLayout->addWidget(infoTags);
    
    infoLayout->addStretch();
    infoPanel->setStyleSheet("background-color: #121212;");
    
    rightLayout->addWidget(filtersPanel, 1);
    rightLayout->addWidget(infoPanel, 1);
    
    // Add panels to main splitter
    mainSplitter->addWidget(folderTreeView);
    mainSplitter->addWidget(assetGridView);
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

    connect(assetGridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onAssetSelectionChanged);
    connect(assetGridView, &QListView::doubleClicked, this, &MainWindow::onAssetDoubleClicked);
    connect(assetGridView, &QListView::customContextMenuRequested, this, &MainWindow::onAssetContextMenu);

    // Update tag button states when selections change
    connect(tagsListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateTagButtonStates);
    connect(assetGridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateTagButtonStates);
}

void MainWindow::onFolderSelected(const QModelIndex &index)
{
    if (!index.isValid()) return;

    int folderId = index.data(VirtualFolderTreeModel::IdRole).toInt();
    assetsModel->setFolderId(folderId);
    clearSelection();
    updateInfoPanel();
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
    QModelIndex index = assetGridView->indexAt(pos);

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

        QMenu *moveToMenu = menu.addMenu("Move to Folder");
        QMenu *assignTagMenu = menu.addMenu("Assign Tag");
        QMenu *setRatingMenu = menu.addMenu("Set Rating");

        menu.addSeparator();
        QAction *removeAction = menu.addAction("Remove from App");

        QAction *selected = menu.exec(assetGridView->mapToGlobal(pos));

        if (selected == openAction) {
            showPreview(index.row());
        } else if (selected == showInExplorerAction) {
            QString filePath = index.data(AssetsModel::FilePathRole).toString();
            QFileInfo fileInfo(filePath);
            QString explorerPath = "explorer /select,\"" + QDir::toNativeSeparators(fileInfo.absoluteFilePath()) + "\"";
            QProcess::startDetached(explorerPath);
        } else if (selected == removeAction) {
            // TODO: Implement remove
            QMessageBox::information(this, "Remove", "Remove functionality not yet implemented");
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

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; }"
        "QMenu::item:selected { background-color: #2f3a4a; }"
    );

    QAction *renameAction = menu.addAction("Rename");
    QAction *deleteAction = menu.addAction("Delete");

    QAction *selected = menu.exec(folderTreeView->mapToGlobal(pos));

    if (selected == renameAction) {
        // TODO: Implement rename
        QMessageBox::information(this, "Rename", "Rename functionality not yet implemented");
    } else if (selected == deleteAction) {
        // TODO: Implement delete
        QMessageBox::information(this, "Delete", "Delete functionality not yet implemented");
    }
}

void MainWindow::onEmptySpaceContextMenu(const QPoint &pos)
{
    Q_UNUSED(pos);
    clearSelection();
}

void MainWindow::showPreview(int index)
{
    // TODO: Implement preview overlay
    previewIndex = index;
    QMessageBox::information(this, "Preview", "Preview functionality not yet implemented");
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
        previewIndex = newIndex;
        // TODO: Update preview content
    }
}



void MainWindow::updateInfoPanel()
{
    QModelIndexList selected = assetGridView->selectionModel()->selectedIndexes();

    if (selected.isEmpty()) {
        infoFileName->setText("No selection");
        infoFilePath->clear();
        infoFileSize->clear();
        infoFileType->clear();
        infoModified->clear();
        infoRating->clear();
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

        QString ratingStr = "Rating: ";
        for (int i = 0; i < 5; i++) {
            ratingStr += (i < rating) ? "★" : "☆";
        }
        infoRating->setText(ratingStr);

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
        infoRating->clear();
        infoTags->clear();
    }
}

void MainWindow::updateSelectionInfo()
{
    // Update internal selection tracking
    selectedAssetIds.clear();
    QModelIndexList selected = assetGridView->selectionModel()->selectedIndexes();
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
    // TODO: Implement filtering logic
    // - Filter by search text
    // - Filter by rating
    // - Filter by selected tags
    statusBar()->showMessage("Filters applied", 2000);
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

    // Apply filter (AND logic)
    assetsModel->setSelectedTagNames(tagNames);
    assetsModel->setTagFilterMode(AssetsModel::And);
    statusBar()->showMessage(QString("Filtering by %1 tag(s) (AND logic)").arg(tagNames.size()), 3000);
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

    // Start import in background
    importer->importFiles(filePaths, folderId);
}

void MainWindow::onImportProgress(int current, int total)
{
    // Update status bar or show progress dialog
    statusBar()->showMessage(QString("Importing: %1 of %2 files...").arg(current).arg(total));
}

void MainWindow::onImportComplete()
{
    statusBar()->showMessage("Import complete", 3000);

    // Reload assets model to show new imports
    assetsModel->reload();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Handle drops on folder tree
    if (watched == folderTreeView->viewport()) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids")) {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragMove) {
            QDragMoveEvent *dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids")) {
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

            if (mimeData->hasFormat("application/x-kasset-asset-ids")) {
                // Get the folder at drop position
                QPoint pos = dropEvent->position().toPoint();
                QModelIndex folderIndex = folderTreeView->indexAt(pos);

                if (folderIndex.isValid()) {
                    int targetFolderId = folderModel->data(folderIndex, VirtualFolderTreeModel::IdRole).toInt();

                    // Decode asset IDs
                    QByteArray encodedData = mimeData->data("application/x-kasset-asset-ids");
                    QDataStream stream(&encodedData, QIODevice::ReadOnly);
                    QList<int> assetIds;
                    stream >> assetIds;

                    qDebug() << "Drop on folder" << targetFolderId << "- moving" << assetIds.size() << "assets";

                    // Move assets to folder
                    for (int assetId : assetIds) {
                        assetsModel->moveAssetToFolder(assetId, targetFolderId);
                    }

                    statusBar()->showMessage(QString("Moved %1 asset(s) to folder").arg(assetIds.size()), 3000);
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
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids")) {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragMove) {
            QDragMoveEvent *dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids")) {
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

            if (mimeData->hasFormat("application/x-kasset-asset-ids")) {
                // Get the tag at drop position
                QPoint pos = dropEvent->position().toPoint();
                QModelIndex tagIndex = tagsListView->indexAt(pos);

                if (tagIndex.isValid()) {
                    int tagId = tagsModel->data(tagIndex, TagsModel::IdRole).toInt();
                    QString tagName = tagsModel->data(tagIndex, TagsModel::NameRole).toString();

                    // Decode asset IDs
                    QByteArray encodedData = mimeData->data("application/x-kasset-asset-ids");
                    QDataStream stream(&encodedData, QIODevice::ReadOnly);
                    QList<int> assetIds;
                    stream >> assetIds;

                    qDebug() << "Drop on tag" << tagName << "- assigning to" << assetIds.size() << "assets";

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
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

