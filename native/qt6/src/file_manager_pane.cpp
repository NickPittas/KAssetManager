#include "file_manager_pane.h"
#include "sequence_grouping_proxy_model.h"
#include "grid_scrub.h"
#include "fm_item_delegate.h"
#include "fm_views_ex.h"
#include "live_preview_manager.h"
#include "theme_manager.h"
#include "icon_utils.h"
#include "oiio_image_loader.h"
#include "media/tlrender_player.h"
#include "media/tlrender_viewport.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QHeaderView>
#include <QFileInfo>
#include <QDir>
#include <QMimeData>
#include <QSettings>
#include <QFrame>
#include <QFontDatabase>
#include <QTime>
#include <QUrl>
#include <QImageReader>

#include <algorithm>

#if defined(HAVE_QT_PDF)
#include <QPdfDocument>
#endif
#if defined(HAVE_QT_PDF_WIDGETS)
#include <QPdfView>
#endif

FileManagerPane::FileManagerPane(QWidget *parent)
    : QWidget(parent)
{
    // Allow free resizing - no minimum size constraint
    setMinimumSize(0, 0);
    
    setupUi();
    setupConnections();
    
    // Default to first drive
    QFileInfoList drives = QDir::drives();
    if (!drives.isEmpty()) {
        QString path = QDir::toNativeSeparators(drives.first().absoluteFilePath());
        navigateToPath(path, false);
    }
}

FileManagerPane::~FileManagerPane()
{
    if (m_sequenceTimer) {
        m_sequenceTimer->stop();
    }
}

void FileManagerPane::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Toolbar
    setupToolbar();
    mainLayout->addWidget(m_toolbar);

    // Path bar
    m_pathBar = new QLineEdit(this);
    m_pathBar->setPlaceholderText("Enter path...");
    m_pathBar->setClearButtonEnabled(true);
    m_pathBar->setMinimumWidth(0);  // Allow free resizing
    connect(m_pathBar, &QLineEdit::returnPressed, this, [this]() {
        QString path = m_pathBar->text().trimmed();
        if (path.isEmpty()) return;
        path = QDir::fromNativeSeparators(path);
        QFileInfo fi(path);
        if (!fi.exists()) {
            // Restore current path
            if (m_dirModel) {
                m_pathBar->setText(QDir::toNativeSeparators(m_dirModel->rootPath()));
            }
            return;
        }
        if (fi.isFile()) {
            path = fi.absolutePath();
        }
        navigateToPath(path, true);
    });
    mainLayout->addWidget(m_pathBar);

    // Main content area with views and preview
    setupViews();
    setupPreviewPanel();

    // Splitter: Views | Preview+Info
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setChildrenCollapsible(true);  // Allow collapsing children
    
    QWidget *viewContainer = new QWidget(m_mainSplitter);
    viewContainer->setMinimumWidth(0);  // Allow free resizing
    viewContainer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QVBoxLayout *viewContainerLayout = new QVBoxLayout(viewContainer);
    viewContainerLayout->setContentsMargins(0, 0, 0, 0);
    viewContainerLayout->setSpacing(0);
    viewContainerLayout->addWidget(m_viewStack);

    m_mainSplitter->addWidget(viewContainer);
    m_mainSplitter->addWidget(m_previewInfoSplitter);
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 1);

    mainLayout->addWidget(m_mainSplitter);
}

void FileManagerPane::setupToolbar()
{
    m_toolbar = new QWidget(this);
    m_toolbar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // Ignored allows shrinking below sizeHint
    m_toolbar->setMinimumWidth(0);  // Allow free resizing
    m_toolbar->setFixedHeight(40);

    QHBoxLayout *tb = new QHBoxLayout(m_toolbar);
    tb->setContentsMargins(4, 4, 4, 4);
    tb->setSpacing(4);

    auto mkTb = [this](const QIcon &ic, const QString &tip) {
        QToolButton *b = new QToolButton(m_toolbar);
        b->setIcon(ic);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setIconSize(QSize(20, 20));
        return b;
    };

    // Navigation buttons
    m_backButton = mkTb(icoBack(ThemeManager::instance().iconColor()), "Back");
    connect(m_backButton, &QToolButton::clicked, this, &FileManagerPane::navigateBack);
    tb->addWidget(m_backButton);

    m_upButton = mkTb(icoUp(ThemeManager::instance().iconColor()), "Up");
    connect(m_upButton, &QToolButton::clicked, this, &FileManagerPane::navigateUp);
    tb->addWidget(m_upButton);

    // Separator
    QFrame *sep1 = new QFrame(m_toolbar);
    sep1->setFrameShape(QFrame::VLine);
    sep1->setFrameShadow(QFrame::Sunken);
    tb->addWidget(sep1);

    // View mode toggle
    m_viewModeButton = new QToolButton(m_toolbar);
    m_viewModeButton->setIcon(icoGrid(ThemeManager::instance().iconColor()));
    m_viewModeButton->setToolTip("Toggle Grid/List");
    m_viewModeButton->setAutoRaise(true);
    m_viewModeButton->setIconSize(QSize(20, 20));
    connect(m_viewModeButton, &QToolButton::clicked, this, &FileManagerPane::onViewModeToggled);
    tb->addWidget(m_viewModeButton);

    // Thumbnail size slider
    m_sizeLabel = new QLabel("Size:", m_toolbar);
    tb->addWidget(m_sizeLabel);

    m_thumbnailSizeSlider = new QSlider(Qt::Horizontal, m_toolbar);
    m_thumbnailSizeSlider->setRange(64, 320);
    m_thumbnailSizeSlider->setValue(120);
    m_thumbnailSizeSlider->setFixedWidth(80);
    m_thumbnailSizeSlider->setToolTip("Adjust thumbnail size");
    connect(m_thumbnailSizeSlider, &QSlider::valueChanged, this, &FileManagerPane::onThumbnailSizeChanged);
    tb->addWidget(m_thumbnailSizeSlider);

    tb->addStretch();

    // Group sequences toggle
    m_groupSequencesButton = new QToolButton(m_toolbar);
    m_groupSequencesButton->setIcon(icoGroup(ThemeManager::instance().iconColor()));
    m_groupSequencesButton->setToolTip("Group image sequences");
    m_groupSequencesButton->setCheckable(true);
    m_groupSequencesButton->setChecked(true);
    m_groupSequencesButton->setAutoRaise(true);
    m_groupSequencesButton->setIconSize(QSize(20, 20));
    connect(m_groupSequencesButton, &QToolButton::toggled, this, &FileManagerPane::onGroupSequencesToggled);
    tb->addWidget(m_groupSequencesButton);

    // Hide folders toggle
    m_hideFoldersButton = new QToolButton(m_toolbar);
    m_hideFoldersButton->setIcon(icoHide(ThemeManager::instance().iconColor()));
    m_hideFoldersButton->setToolTip("Hide folders (files only)");
    m_hideFoldersButton->setCheckable(true);
    m_hideFoldersButton->setAutoRaise(true);
    m_hideFoldersButton->setIconSize(QSize(20, 20));
    connect(m_hideFoldersButton, &QToolButton::toggled, this, &FileManagerPane::onHideFoldersToggled);
    tb->addWidget(m_hideFoldersButton);

    // Preview toggle
    m_previewToggleButton = new QToolButton(m_toolbar);
    m_previewToggleButton->setIcon(icoEye(ThemeManager::instance().iconColor()));
    m_previewToggleButton->setToolTip("Show/Hide preview panel");
    m_previewToggleButton->setCheckable(true);
    m_previewToggleButton->setChecked(true);
    m_previewToggleButton->setAutoRaise(true);
    m_previewToggleButton->setIconSize(QSize(20, 20));
    connect(m_previewToggleButton, &QToolButton::toggled, this, &FileManagerPane::onPreviewToggled);
    tb->addWidget(m_previewToggleButton);
}

void FileManagerPane::setupViews()
{
    m_viewStack = new QStackedWidget(this);
    m_viewStack->setMinimumWidth(0);  // Allow free resizing

    // Directory model
    m_dirModel = new QFileSystemModel(m_viewStack);
    m_dirModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    m_dirModel->setRootPath("");

    // Sequence grouping proxy
    m_proxyModel = new SequenceGroupingProxyModel(m_viewStack);
    m_proxyModel->setSourceModel(m_dirModel);
    m_proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setSortRole(Qt::DisplayRole);
    m_proxyModel->setDynamicSortFilter(true);
    m_proxyModel->sort(0, Qt::AscendingOrder);

    // Grid view
    m_gridView = new FmGridViewEx(m_proxyModel, m_dirModel, m_viewStack);
    m_gridView->setModel(m_proxyModel);
    m_gridView->setViewMode(QListView::IconMode);
    m_gridView->setResizeMode(QListView::Adjust);
    m_gridView->setSpacing(1);
    m_gridView->setUniformItemSizes(true);
    m_gridView->setLayoutMode(QListView::Batched);
    m_gridView->setBatchSize(100);
    m_gridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_gridView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_gridView->setMinimumWidth(0);  // Allow free resizing

    // Grid delegate
    auto *gridDelegate = new FmItemDelegate(m_gridView);
    gridDelegate->setView(m_gridView);
    gridDelegate->setThumbnailSize(120);
    m_gridView->setItemDelegate(gridDelegate);
    m_gridView->setIconSize(QSize(120, 120));
    m_gridView->setGridSize(QSize(128, 156));

    // Drag-drop
    m_gridView->setDragEnabled(true);
    m_gridView->setAcceptDrops(true);
    m_gridView->setDropIndicatorShown(true);
    m_gridView->setDragDropMode(QAbstractItemView::DragDrop);
    m_gridView->setDefaultDropAction(Qt::CopyAction);

    m_gridView->viewport()->installEventFilter(this);
    connect(m_gridView, &QListView::doubleClicked, this, &FileManagerPane::onItemDoubleClicked);
    connect(m_gridView, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        emit contextMenuRequested(m_gridView->viewport()->mapToGlobal(pos));
    });

    m_viewStack->addWidget(m_gridView); // index 0

    // Scrub controller for grid
    m_scrubController = new GridScrubController(
        m_gridView,
        [this](const QModelIndex &idx) -> QString {
            if (!m_dirModel) return QString();
            QModelIndex srcIdx = idx;
            if (m_proxyModel && idx.model() == m_proxyModel) {
                srcIdx = m_proxyModel->mapToSource(idx);
            }
            if (!srcIdx.isValid()) return QString();
            if (m_dirModel->isDir(srcIdx)) return QString();
            return m_dirModel->filePath(srcIdx);
        },
        this);

    // List view
    m_listView = new FmListViewEx(m_proxyModel, m_dirModel, m_viewStack);
    m_listView->setModel(m_proxyModel);
    m_listView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_listView->setSortingEnabled(true);
    m_listView->setAlternatingRowColors(false);
    m_listView->setShowGrid(false);
    m_listView->verticalHeader()->setVisible(false);
    m_listView->verticalHeader()->setDefaultSectionSize(22);
    m_listView->setIconSize(QSize(18, 18));
    m_listView->horizontalHeader()->setStretchLastSection(true);
    m_listView->sortByColumn(0, Qt::AscendingOrder);
    m_listView->setMinimumWidth(0);  // Allow free resizing

    // Drag-drop
    m_listView->setDragEnabled(true);
    m_listView->setAcceptDrops(true);
    m_listView->setDropIndicatorShown(true);
    m_listView->setDragDropMode(QAbstractItemView::DragDrop);
    m_listView->setDefaultDropAction(Qt::CopyAction);

    m_listView->viewport()->installEventFilter(this);
    connect(m_listView, &QTableView::doubleClicked, this, &FileManagerPane::onItemDoubleClicked);
    connect(m_listView, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        emit contextMenuRequested(m_listView->viewport()->mapToGlobal(pos));
    });

    m_viewStack->addWidget(m_listView); // index 1
    m_viewStack->setCurrentIndex(0);
}

void FileManagerPane::setupPreviewPanel()
{
    // Preview panel
    m_previewPanel = new QWidget(this);
    m_previewPanel->setMinimumWidth(0);  // Allow free resizing

    QVBoxLayout *pv = new QVBoxLayout(m_previewPanel);
    pv->setContentsMargins(4, 4, 4, 4);
    pv->setSpacing(4);

    QLabel *pvTitle = new QLabel("Preview", m_previewPanel);
    QFont pvTitleFont = pvTitle->font();
    pvTitleFont.setBold(true);
    pvTitle->setFont(pvTitleFont);
    pv->addWidget(pvTitle);

    // Image view
    m_imageScene = new QGraphicsScene(m_previewPanel);
    m_imageItem = new QGraphicsPixmapItem();
    m_imageScene->addItem(m_imageItem);
    m_imageView = new QGraphicsView(m_imageScene, m_previewPanel);
    m_imageView->setDragMode(QGraphicsView::ScrollHandDrag);
    m_imageView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_imageView->setMinimumHeight(0);  // Allow free resizing
    m_imageView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_imageView->setAlignment(Qt::AlignCenter);
    m_imageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_imageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_imageView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_imageView->viewport()->installEventFilter(this);
    m_imageView->installEventFilter(this);

    // Text preview
    m_textView = new QPlainTextEdit(m_previewPanel);
    m_textView->setReadOnly(true);
    m_textView->setWordWrapMode(QTextOption::NoWrap);
    m_textView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_textView->setStyleSheet("QPlainTextEdit { background-color: #ffffff; color: #000000; border: none; }");
    m_textView->hide();

    // CSV preview
    m_csvModel = new QStandardItemModel(m_previewPanel);
    m_csvView = new QTableView(m_previewPanel);
    m_csvView->setModel(m_csvModel);
    m_csvView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_csvView->setSelectionMode(QAbstractItemView::NoSelection);
    m_csvView->setAlternatingRowColors(true);
    m_csvView->setStyleSheet(
        "QTableView { background-color: #ffffff; color: #000000; gridline-color: #cccccc; border: none; }"
        "QHeaderView::section { background-color: #f0f0f0; color: #000000; border: none; padding: 4px; }");
    m_csvView->hide();

#if defined(HAVE_QT_PDF)
    m_pdfDoc = new QPdfDocument(m_previewPanel);
#endif
#if defined(HAVE_QT_PDF_WIDGETS)
    m_pdfView = new QPdfView(m_previewPanel);
    m_pdfView->setPageMode(QPdfView::PageMode::SinglePage);
    m_pdfView->setDocument(m_pdfDoc);
    m_pdfView->hide();
#endif

    // SVG view
    m_svgScene = new QGraphicsScene(m_previewPanel);
    m_svgItem = nullptr;
    m_svgView = new QGraphicsView(m_svgScene, m_previewPanel);
    m_svgView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_svgView->setAlignment(Qt::AlignCenter);
    m_svgView->hide();

    // Alpha toggle
    QHBoxLayout *alphaRow = new QHBoxLayout();
    m_alphaCheck = new QCheckBox("Alpha", m_previewPanel);
    m_alphaCheck->setToolTip("Show alpha channel (grayscale)");
    m_alphaCheck->hide();
    connect(m_alphaCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_alphaOnlyMode = on;
        if (!m_originalImage.isNull() && m_imageItem) {
            QImage disp = m_originalImage;
            if (m_alphaOnlyMode && disp.hasAlphaChannel()) {
                QImage a(disp.size(), QImage::Format_Grayscale8);
                for (int y = 0; y < disp.height(); ++y) {
                    for (int x = 0; x < disp.width(); ++x) {
                        uchar alpha = qAlpha(reinterpret_cast<const QRgb *>(disp.constScanLine(y))[x]);
                        a.scanLine(y)[x] = alpha;
                    }
                }
                disp = a.convertToFormat(QImage::Format_Grayscale8);
            }
            m_imageItem->setPixmap(QPixmap::fromImage(disp));
            if (m_imageFitToView) {
                m_imageScene->setSceneRect(m_imageItem->boundingRect());
                m_imageView->centerOn(m_imageItem);
                m_imageView->fitInView(m_imageItem, Qt::KeepAspectRatio);
            }
        }
    });
    alphaRow->addWidget(m_alphaCheck);
    alphaRow->addStretch();

    // PDF controls
    QHBoxLayout *docc = new QHBoxLayout();
    m_pdfPrevBtn = new QToolButton(m_previewPanel);
    m_pdfPrevBtn->setText("◀");
    m_pdfNextBtn = new QToolButton(m_previewPanel);
    m_pdfNextBtn->setText("▶");
    m_pdfPageLabel = new QLabel("--/--", m_previewPanel);
    docc->addWidget(m_pdfPrevBtn);
    docc->addWidget(m_pdfPageLabel);
    docc->addWidget(m_pdfNextBtn);
    docc->addStretch();
    m_pdfPrevBtn->hide();
    m_pdfNextBtn->hide();
    m_pdfPageLabel->hide();

    pv->addLayout(alphaRow);

    // Video widget (tlRender viewport)
    // tlRender player
    m_tlrenderPlayer = new TLRenderPlayer(m_previewPanel);

    // Media controls
    QHBoxLayout *mc = new QHBoxLayout();
    mc->setAlignment(Qt::AlignVCenter);

    m_prevFrameBtn = new QPushButton(m_previewPanel);
    m_prevFrameBtn->setIcon(icoMediaPrevFrame(ThemeManager::instance().iconColor()));
    m_prevFrameBtn->setIconSize(QSize(14, 14));
    m_prevFrameBtn->setToolTip("Previous Frame");

    m_playPauseBtn = new QPushButton(m_previewPanel);
    m_playPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor()));
    m_playPauseBtn->setIconSize(QSize(16, 16));

    m_nextFrameBtn = new QPushButton(m_previewPanel);
    m_nextFrameBtn->setIcon(icoMediaNextFrame(ThemeManager::instance().iconColor()));
    m_nextFrameBtn->setIconSize(QSize(14, 14));
    m_nextFrameBtn->setToolTip("Next Frame");

    m_positionSlider = new QSlider(Qt::Horizontal, m_previewPanel);
    m_positionSlider->setMinimum(0);
    m_positionSlider->setMaximum(1000);

    m_timeLabel = new QLabel("00:00 / 00:00", m_previewPanel);

    m_muteBtn = new QPushButton(m_previewPanel);
    m_muteBtn->setIcon(icoMediaAudio(ThemeManager::instance().iconColor()));
    m_muteBtn->setFlat(true);
    m_muteBtn->setIconSize(QSize(14, 14));
    m_muteBtn->setFocusPolicy(Qt::NoFocus);
    m_muteBtn->setToolTip("Mute/Unmute");

    m_volumeSlider = new QSlider(Qt::Horizontal, m_previewPanel);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(50);
    m_volumeSlider->setFixedWidth(60);

    mc->addWidget(m_prevFrameBtn);
    mc->addWidget(m_playPauseBtn);
    mc->addWidget(m_nextFrameBtn);
    mc->addWidget(m_positionSlider, 1);
    mc->addWidget(m_timeLabel);
    mc->addSpacing(4);
    mc->addWidget(m_muteBtn);
    mc->addWidget(m_volumeSlider);

    // Sequence timer
    m_sequenceTimer = new QTimer(m_previewPanel);
    connect(m_sequenceTimer, &QTimer::timeout, this, [this] {
        if (!m_isSequence || m_sequenceFramePaths.isEmpty()) return;
        int next = m_sequenceCurrentIndex + 1;
        if (next >= m_sequenceFramePaths.size()) next = 0;
        loadSequenceFrame(next);
    });

    // Media control connections
    connect(m_prevFrameBtn, &QPushButton::clicked, this, [this] { if (m_isSequence) stepSequence(-1); });
    connect(m_nextFrameBtn, &QPushButton::clicked, this, [this] { if (m_isSequence) stepSequence(1); });

    connect(m_playPauseBtn, &QPushButton::clicked, this, [this] {
        if (m_isSequence) {
            if (m_sequencePlaying) pauseSequence();
            else playSequence();
            return;
        }
        if (!m_tlrenderPlayer) return;
        if (m_tlrenderPlayer->playbackState() == TLRenderPlayer::PlaybackState::Playing) {
            m_tlrenderPlayer->pause();
            m_playPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor()));
        } else {
            m_tlrenderPlayer->play();
            m_playPauseBtn->setIcon(icoMediaPause(ThemeManager::instance().iconColor()));
        }
    });

    connect(m_tlrenderPlayer, &TLRenderPlayer::positionChanged, this, [this](qint64 pos) {
        if (m_isSequence) return;
        qint64 duration = m_tlrenderPlayer->duration();
        if (m_tlrenderPlayer && duration > 0) {
            m_positionSlider->blockSignals(true);
            m_positionSlider->setValue(int(pos * 1000 / duration));
            m_positionSlider->blockSignals(false);
            m_timeLabel->setText(QString("%1 / %2")
                .arg(QTime::fromMSecsSinceStartOfDay(int(pos)).toString("mm:ss"))
                .arg(QTime::fromMSecsSinceStartOfDay(int(duration)).toString("mm:ss")));
        }
    });

    connect(m_positionSlider, &QSlider::sliderMoved, this, [this](int v) {
        if (m_isSequence) {
            loadSequenceFrame(v);
            return;
        }
        qint64 duration = m_tlrenderPlayer->duration();
        if (m_tlrenderPlayer && duration > 0) {
            m_tlrenderPlayer->seek(qint64(v) * duration / 1000);
        }
    });

    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int v) {
        if (m_tlrenderPlayer) m_tlrenderPlayer->setVolume(v / 100.0f);
    });

    connect(m_muteBtn, &QPushButton::clicked, this, [this] {
        if (!m_tlrenderPlayer) return;
        bool newMuted = !m_tlrenderPlayer->isMuted();
        m_tlrenderPlayer->setMuted(newMuted);
        m_muteBtn->setIcon(newMuted ? icoMediaMute() : icoMediaAudio());
    });

    // Preview content
    m_previewContent = new QWidget(m_previewPanel);
    QVBoxLayout *pc = new QVBoxLayout(m_previewContent);
    pc->setContentsMargins(0, 0, 0, 0);
    pc->setSpacing(4);
    pc->addWidget(m_imageView, 1);
    pc->addWidget(m_textView, 1);
    pc->addWidget(m_csvView, 1);
#if defined(HAVE_QT_PDF_WIDGETS)
    pc->addWidget(m_pdfView, 1);
#endif
    pc->addWidget(m_svgView, 1);

    pv->addWidget(m_previewContent);
    pv->addLayout(mc);
    pv->addLayout(docc);

    // Hide media controls by default
    m_prevFrameBtn->hide();
    m_playPauseBtn->hide();
    m_nextFrameBtn->hide();
    m_positionSlider->hide();
    m_timeLabel->hide();
    m_volumeSlider->hide();
    m_muteBtn->hide();

    // Info panel
    m_infoPanel = new QWidget(this);
    m_infoPanel->setMinimumWidth(0);  // Allow free resizing
    QVBoxLayout *infoPanelLayout = new QVBoxLayout(m_infoPanel);
    infoPanelLayout->setContentsMargins(0, 0, 0, 0);
    infoPanelLayout->setSpacing(0);

    QLabel *infoTitle = new QLabel("File Info", m_infoPanel);
    QFont infoTitleFont = infoTitle->font();
    infoTitleFont.setPointSize(11);
    infoTitleFont.setBold(true);
    infoTitle->setFont(infoTitleFont);
    infoPanelLayout->addWidget(infoTitle);

    QScrollArea *infoScrollArea = new QScrollArea(m_infoPanel);
    infoScrollArea->setWidgetResizable(true);
    infoScrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *infoScrollWidget = new QWidget();
    QVBoxLayout *infoLayout = new QVBoxLayout(infoScrollWidget);
    infoLayout->setContentsMargins(4, 4, 4, 4);
    infoLayout->setSpacing(2);

    m_infoFileName = new QLabel("No selection", m_infoPanel);
    QFont infoFileNameFont = m_infoFileName->font();
    infoFileNameFont.setBold(true);
    m_infoFileName->setFont(infoFileNameFont);
    m_infoFileName->setWordWrap(true);
    infoLayout->addWidget(m_infoFileName);

    m_infoFilePath = new QLabel("", m_infoPanel);
    m_infoFilePath->setWordWrap(true);
    infoLayout->addWidget(m_infoFilePath);

    QFrame *separator = new QFrame(m_infoPanel);
    separator->setFrameShape(QFrame::HLine);
    separator->setFixedHeight(1);
    infoLayout->addWidget(separator);

    m_infoFileSize = new QLabel("", m_infoPanel);
    infoLayout->addWidget(m_infoFileSize);

    m_infoFileType = new QLabel("", m_infoPanel);
    infoLayout->addWidget(m_infoFileType);

    m_infoDimensions = new QLabel("", m_infoPanel);
    infoLayout->addWidget(m_infoDimensions);

    m_infoCreated = new QLabel("", m_infoPanel);
    infoLayout->addWidget(m_infoCreated);

    m_infoModified = new QLabel("", m_infoPanel);
    infoLayout->addWidget(m_infoModified);

    m_infoPermissions = new QLabel("", m_infoPanel);
    infoLayout->addWidget(m_infoPermissions);

    infoLayout->addStretch();
    infoScrollWidget->setLayout(infoLayout);
    infoScrollArea->setWidget(infoScrollWidget);
    infoPanelLayout->addWidget(infoScrollArea);

    // Preview | Info splitter
    m_previewInfoSplitter = new QSplitter(Qt::Vertical, this);
    m_previewInfoSplitter->setChildrenCollapsible(true);  // Allow collapsing children
    m_previewInfoSplitter->addWidget(m_previewPanel);
    m_previewInfoSplitter->addWidget(m_infoPanel);
    m_previewInfoSplitter->setStretchFactor(0, 2);
    m_previewInfoSplitter->setStretchFactor(1, 1);
}

void FileManagerPane::setupConnections()
{
    // Selection changes
    connect(m_gridView->selectionModel(), &QItemSelectionModel::selectionChanged, 
            this, &FileManagerPane::onSelectionChanged);
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &FileManagerPane::onSelectionChanged);
}

void FileManagerPane::navigateToPath(const QString &path, bool addToHistory)
{
    if (path.isEmpty()) return;

    QFileInfo fi(path);
    if (!fi.exists()) return;

    QString targetPath = fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath();

    if (addToHistory) {
        // Truncate forward history if we're not at the end
        while (m_navigationHistory.size() > m_navigationIndex + 1) {
            m_navigationHistory.removeLast();
        }
        m_navigationHistory.append(targetPath);
        m_navigationIndex = m_navigationHistory.size() - 1;
    }

    m_dirModel->setRootPath(targetPath);
    QModelIndex srcRoot = m_dirModel->index(targetPath);

    if (m_proxyModel) {
        m_proxyModel->rebuildForRoot(targetPath);
        QModelIndex proxyRoot = m_proxyModel->mapFromSource(srcRoot);
        m_gridView->setRootIndex(proxyRoot);
        m_listView->setRootIndex(proxyRoot);
    } else {
        m_gridView->setRootIndex(srcRoot);
        m_listView->setRootIndex(srcRoot);
    }

    m_pathBar->setText(QDir::toNativeSeparators(targetPath));
    updateNavigationButtons();

    emit pathChanged(targetPath);
}

QString FileManagerPane::currentPath() const
{
    return m_dirModel ? m_dirModel->rootPath() : QString();
}

void FileManagerPane::navigateBack()
{
    if (m_navigationIndex > 0) {
        m_navigationIndex--;
        QString path = m_navigationHistory[m_navigationIndex];
        navigateToPath(path, false);
    }
}

void FileManagerPane::navigateUp()
{
    QString current = currentPath();
    if (current.isEmpty()) return;

    QDir dir(current);
    if (dir.cdUp()) {
        navigateToPath(dir.absolutePath(), true);
    }
}

void FileManagerPane::refresh()
{
    if (!m_dirModel) return;

    QString path = currentPath();
    if (path.isEmpty()) {
        if (m_gridView && m_gridView->viewport()) m_gridView->viewport()->update();
        if (m_listView && m_listView->viewport()) m_listView->viewport()->update();
        return;
    }

    // Force a model refresh by flipping the root path.
    QString tempPath = QCoreApplication::applicationDirPath();
    m_dirModel->setRootPath(tempPath);
    m_dirModel->setRootPath(path);

    QModelIndex srcRoot = m_dirModel->index(path);
    if (m_proxyModel) {
        if (m_proxyModel->groupingEnabled()) {
            m_proxyModel->rebuildForRoot(path);
        }
        QModelIndex proxyRoot = m_proxyModel->mapFromSource(srcRoot);
        if (m_gridView) m_gridView->setRootIndex(proxyRoot);
        if (m_listView) m_listView->setRootIndex(proxyRoot);
    } else {
        if (m_gridView) m_gridView->setRootIndex(srcRoot);
        if (m_listView) m_listView->setRootIndex(srcRoot);
    }

    if (m_gridView && m_gridView->viewport()) m_gridView->viewport()->update();
    if (m_listView && m_listView->viewport()) m_listView->viewport()->update();
}

QStringList FileManagerPane::selectedPaths() const
{
    QStringList paths;
    QAbstractItemView *view = m_isGridMode ? static_cast<QAbstractItemView*>(m_gridView) 
                                            : static_cast<QAbstractItemView*>(m_listView);
    QModelIndexList selected = view->selectionModel()->selectedIndexes();

    QSet<QString> seen;
    for (const QModelIndex &idx : selected) {
        if (idx.column() != 0) continue;
        QModelIndex srcIdx = idx;
        if (m_proxyModel && idx.model() == m_proxyModel) {
            srcIdx = m_proxyModel->mapToSource(idx);
        }
        QString filePath = m_dirModel->filePath(srcIdx);
        if (!filePath.isEmpty() && !seen.contains(filePath)) {
            seen.insert(filePath);
            paths.append(filePath);
        }
    }
    return paths;
}

QModelIndex FileManagerPane::currentIndex() const
{
    QAbstractItemView *view = m_isGridMode ? static_cast<QAbstractItemView*>(m_gridView)
                                            : static_cast<QAbstractItemView*>(m_listView);
    return view->currentIndex();
}

void FileManagerPane::setCurrentIndex(const QModelIndex &index)
{
    QAbstractItemView *view = m_isGridMode ? static_cast<QAbstractItemView*>(m_gridView)
                                            : static_cast<QAbstractItemView*>(m_listView);
    view->setCurrentIndex(index);
    view->scrollTo(index, QAbstractItemView::PositionAtCenter);
}

QString FileManagerPane::pathForIndex(const QModelIndex &index) const
{
    if (!index.isValid() || !m_dirModel) return QString();
    
    QModelIndex srcIdx = index;
    if (m_proxyModel && index.model() == m_proxyModel) {
        srcIdx = m_proxyModel->mapToSource(index);
    }
    return m_dirModel->filePath(srcIdx);
}

void FileManagerPane::clearSelection()
{
    m_gridView->clearSelection();
    m_listView->clearSelection();
}

void FileManagerPane::setGridMode(bool grid)
{
    m_isGridMode = grid;
    m_viewStack->setCurrentIndex(grid ? 0 : 1);
    m_viewModeButton->setIcon(grid ? icoGrid(ThemeManager::instance().iconColor()) 
                                   : icoList(ThemeManager::instance().iconColor()));
}

void FileManagerPane::setThumbnailSize(int size)
{
    if (auto *d = qobject_cast<FmItemDelegate*>(m_gridView->itemDelegate())) {
        d->setThumbnailSize(size);
    }
    m_gridView->setIconSize(QSize(size, size));
    m_gridView->setGridSize(QSize(size + 8, size + 36));
    m_thumbnailSizeSlider->setValue(size);
}

int FileManagerPane::thumbnailSize() const
{
    return m_thumbnailSizeSlider->value();
}

void FileManagerPane::setSequenceGroupingEnabled(bool enabled)
{
    m_groupSequences = enabled;
    m_groupSequencesButton->setChecked(enabled);
    if (m_proxyModel) {
        m_proxyModel->setGroupingEnabled(enabled);
        QString path = currentPath();
        if (!path.isEmpty()) {
            m_proxyModel->rebuildForRoot(path);
        }
    }
}

void FileManagerPane::setHideFolders(bool hide)
{
    m_hideFolders = hide;
    m_hideFoldersButton->setChecked(hide);
    if (m_dirModel) {
        QDir::Filters filters = QDir::NoDotAndDotDot | (hide ? QDir::Files : QDir::AllEntries);
        m_dirModel->setFilter(filters);
        QString path = currentPath();
        if (!path.isEmpty() && m_proxyModel) {
            m_proxyModel->rebuildForRoot(path);
        }
    }
}

void FileManagerPane::setPreviewVisible(bool visible)
{
    m_previewToggleButton->setChecked(visible);
    m_previewInfoSplitter->setVisible(visible);
}

bool FileManagerPane::isPreviewVisible() const
{
    return m_previewInfoSplitter->isVisible();
}

void FileManagerPane::setActive(bool active)
{
    if (m_isActive == active) return;
    m_isActive = active;
    applyActiveStyle();
    if (active) {
        emit activated();
    }
}

void FileManagerPane::setToolbarVisible(bool visible)
{
    if (m_toolbar) {
        m_toolbar->setVisible(visible);
    }
}

bool FileManagerPane::isToolbarVisible() const
{
    return m_toolbar && m_toolbar->isVisible();
}

bool FileManagerPane::canNavigateBack() const
{
    return m_navigationIndex > 0;
}

bool FileManagerPane::canNavigateUp() const
{
    const QString path = currentPath();
    if (path.isEmpty()) return false;
    QDir dir(path);
    return dir.cdUp();
}

void FileManagerPane::applyActiveStyle()
{
    // Apply a visible border to indicate active/inactive state
    if (m_isActive) {
        setStyleSheet(
            "FileManagerPane { "
            "  border: 2px solid #58a6ff; "
            "  border-radius: 4px; "
            "}"
        );
    } else {
        setStyleSheet(
            "FileManagerPane { "
            "  border: 1px solid #555; "
            "  border-radius: 4px; "
            "}"
        );
    }
}

void FileManagerPane::copySelectedToClipboard()
{
    m_clipboardPaths = selectedPaths();
    m_clipboardCutMode = false;
}

void FileManagerPane::cutSelectedToClipboard()
{
    m_clipboardPaths = selectedPaths();
    m_clipboardCutMode = true;
}

void FileManagerPane::pasteFromClipboard()
{
    if (m_clipboardPaths.isEmpty()) return;
    
    QString destDir = currentPath();
    if (destDir.isEmpty()) return;
    
    emit filesDropped(m_clipboardPaths, destDir, m_clipboardCutMode);
    
    // Clear clipboard if it was a cut operation
    if (m_clipboardCutMode) {
        m_clipboardPaths.clear();
        m_clipboardCutMode = false;
    }
}

void FileManagerPane::setClipboard(const QStringList &paths, bool isCut)
{
    m_clipboardPaths = paths;
    m_clipboardCutMode = isCut;
}

void FileManagerPane::updatePreviewForIndex(const QModelIndex &idx)
{
    if (!idx.isValid()) {
        clearPreview();
        return;
    }

    QModelIndex srcIdx = idx;
    if (m_proxyModel && idx.model() == m_proxyModel) {
        srcIdx = m_proxyModel->mapToSource(idx);
    }

    QString filePath = m_dirModel->filePath(srcIdx);
    if (filePath.isEmpty() || m_dirModel->isDir(srcIdx)) {
        clearPreview();
        return;
    }

    m_currentPreviewPath = filePath;
    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();

    // Update info panel
    m_infoFileName->setText(fi.fileName());
    m_infoFilePath->setText(fi.absolutePath());
    m_infoFileSize->setText(QString("Size: %1").arg(QLocale().formattedDataSize(fi.size())));
    m_infoFileType->setText(QString("Type: %1").arg(suffix.toUpper()));
    m_infoCreated->setText(QString("Created: %1").arg(fi.birthTime().toString("yyyy-MM-dd hh:mm")));
    m_infoModified->setText(QString("Modified: %1").arg(fi.lastModified().toString("yyyy-MM-dd hh:mm")));

    // Show appropriate preview widget based on file type
    static const QSet<QString> imageExts = {"png", "jpg", "jpeg", "bmp", "gif", "tga", "tiff", "tif", 
                                             "webp", "exr", "hdr", "psd", "ico"};
    static const QSet<QString> videoExts = {"mp4", "mov", "avi", "mkv", "webm", "mxf", "mpg", "mpeg",
                                             "m4v", "wmv", "flv"};
    static const QSet<QString> textExts = {"txt", "log", "md", "json", "xml", "html", "css", "js",
                                            "py", "cpp", "h", "c", "hpp"};

    // Hide all preview widgets first
    m_imageView->hide();
    m_videoWidget->hide();
    m_textView->hide();
    m_csvView->hide();
    m_svgView->hide();
    m_prevFrameBtn->hide();
    m_playPauseBtn->hide();
    m_nextFrameBtn->hide();
    m_positionSlider->hide();
    m_timeLabel->hide();
    m_volumeSlider->hide();
    m_muteBtn->hide();
    m_alphaCheck->hide();
#if defined(HAVE_QT_PDF_WIDGETS)
    if (m_pdfView) m_pdfView->hide();
#endif
    m_pdfPrevBtn->hide();
    m_pdfNextBtn->hide();
    m_pdfPageLabel->hide();

    if (imageExts.contains(suffix)) {
        // Load image
        QImage img;
        if (OIIOImageLoader::isOIIOSupported(filePath)) {
            img = OIIOImageLoader::loadImage(filePath, 0, 0);
        }
        if (img.isNull()) {
            img = QImage(filePath);
        }
        if (!img.isNull()) {
            m_originalImage = img;
            m_previewHasAlpha = img.hasAlphaChannel();
            m_imageItem->setPixmap(QPixmap::fromImage(img));
            m_imageScene->setSceneRect(m_imageItem->boundingRect());
            m_imageView->centerOn(m_imageItem);
            m_imageView->fitInView(m_imageItem, Qt::KeepAspectRatio);
            m_imageFitToView = true;
            m_imageView->show();
            m_alphaCheck->setVisible(m_previewHasAlpha);

            // Update dimensions
            m_infoDimensions->setText(QString("Dimensions: %1 x %2").arg(img.width()).arg(img.height()));
        }
    } else if (videoExts.contains(suffix)) {
        // Video preview
        if (m_tlrenderPlayer) {
            ensureVideoPreview();
            m_tlrenderPlayer->loadMedia(filePath);
            if (m_videoWidget) m_videoWidget->show();
            m_playPauseBtn->show();
            m_positionSlider->show();
            m_timeLabel->show();
            m_volumeSlider->show();
            m_muteBtn->show();
        }
    } else if (textExts.contains(suffix)) {
        // Text preview
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            QString content = stream.read(100000); // Limit to 100KB
            m_textView->setPlainText(content);
            m_textView->show();
        }
    } else if (suffix == "csv") {
        // CSV preview
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            m_csvModel->clear();
            QTextStream stream(&file);
            int row = 0;
            while (!stream.atEnd() && row < 100) {
                QString line = stream.readLine();
                QStringList fields = line.split(',');
                if (row == 0) {
                    m_csvModel->setHorizontalHeaderLabels(fields);
                } else {
                    for (int col = 0; col < fields.size(); ++col) {
                        m_csvModel->setItem(row - 1, col, new QStandardItem(fields[col]));
                    }
                }
                row++;
            }
            m_csvView->show();
        }
    }
}

void FileManagerPane::clearPreview()
{
    m_currentPreviewPath.clear();
    m_originalImage = QImage();
    m_imageItem->setPixmap(QPixmap());
    
    // Stop video playback
    if (m_tlrenderPlayer) {
        m_tlrenderPlayer->stop();
        m_tlrenderPlayer->unloadMedia();
    }

    // Stop sequence playback
    if (m_sequenceTimer) {
        m_sequenceTimer->stop();
    }
    m_isSequence = false;
    m_sequencePlaying = false;

    // Hide all preview widgets
    m_imageView->hide();
    m_videoWidget->hide();
    m_textView->hide();
    m_csvView->hide();
    m_svgView->hide();
#if defined(HAVE_QT_PDF_WIDGETS)
    if (m_pdfView) m_pdfView->hide();
#endif

    // Hide media controls
    m_prevFrameBtn->hide();
    m_playPauseBtn->hide();
    m_nextFrameBtn->hide();
    m_positionSlider->hide();
    m_timeLabel->hide();
    m_volumeSlider->hide();
    m_muteBtn->hide();
    m_alphaCheck->hide();
    m_pdfPrevBtn->hide();
    m_pdfNextBtn->hide();
    m_pdfPageLabel->hide();

    // Clear info panel
    m_infoFileName->setText("No selection");
    m_infoFilePath->clear();
    m_infoFileSize->clear();
    m_infoFileType->clear();
    m_infoDimensions->clear();
    m_infoCreated->clear();
    m_infoModified->clear();
    m_infoPermissions->clear();
}

void FileManagerPane::ensureVideoPreview()
{
    if (m_videoWidget || !m_previewContent) {
        return;
    }

    auto *layout = qobject_cast<QVBoxLayout*>(m_previewContent->layout());
    if (!layout) {
        return;
    }

    m_videoWidget = new TLRenderViewport(m_previewPanel);
    m_videoWidget->setMinimumHeight(0);
    m_videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoWidget->hide();
    m_videoWidget->setPlayer(m_tlrenderPlayer);
    layout->insertWidget(1, m_videoWidget, 1);
}

void FileManagerPane::saveState(QSettings &settings, const QString &prefix)
{
    settings.setValue(prefix + "/ViewMode", m_isGridMode);
    settings.setValue(prefix + "/ThumbnailSize", thumbnailSize());
    settings.setValue(prefix + "/GroupSequences", m_groupSequences);
    settings.setValue(prefix + "/HideFolders", m_hideFolders);
    settings.setValue(prefix + "/PreviewVisible", isPreviewVisible());
    settings.setValue(prefix + "/CurrentPath", currentPath());
    
    if (m_mainSplitter) {
        settings.setValue(prefix + "/MainSplitter", m_mainSplitter->saveState());
    }
    if (m_previewInfoSplitter) {
        settings.setValue(prefix + "/PreviewInfoSplitter", m_previewInfoSplitter->saveState());
    }
}

void FileManagerPane::restoreState(QSettings &settings, const QString &prefix)
{
    if (settings.contains(prefix + "/ViewMode")) {
        setGridMode(settings.value(prefix + "/ViewMode").toBool());
    }
    if (settings.contains(prefix + "/ThumbnailSize")) {
        setThumbnailSize(settings.value(prefix + "/ThumbnailSize").toInt());
    }
    if (settings.contains(prefix + "/GroupSequences")) {
        setSequenceGroupingEnabled(settings.value(prefix + "/GroupSequences").toBool());
    }
    if (settings.contains(prefix + "/HideFolders")) {
        setHideFolders(settings.value(prefix + "/HideFolders").toBool());
    }
    if (settings.contains(prefix + "/PreviewVisible")) {
        setPreviewVisible(settings.value(prefix + "/PreviewVisible").toBool());
    }
    if (settings.contains(prefix + "/CurrentPath")) {
        QString path = settings.value(prefix + "/CurrentPath").toString();
        if (QFileInfo::exists(path)) {
            navigateToPath(path, false);
        }
    }
    
    if (m_mainSplitter && settings.contains(prefix + "/MainSplitter")) {
        m_mainSplitter->restoreState(settings.value(prefix + "/MainSplitter").toByteArray());
    }
    if (m_previewInfoSplitter && settings.contains(prefix + "/PreviewInfoSplitter")) {
        m_previewInfoSplitter->restoreState(settings.value(prefix + "/PreviewInfoSplitter").toByteArray());
    }
}

void FileManagerPane::focusInEvent(QFocusEvent *event)
{
    QWidget::focusInEvent(event);
    setActive(true);
}

void FileManagerPane::mousePressEvent(QMouseEvent *event)
{
    QWidget::mousePressEvent(event);
    setActive(true);
}

bool FileManagerPane::eventFilter(QObject *watched, QEvent *event)
{
    // Activate pane when any child widget receives focus or mouse click
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress) {
        setActive(true);
    }

    const bool viewViewport = (m_gridView && watched == m_gridView->viewport()) ||
                              (m_listView && watched == m_listView->viewport());
    if (viewViewport) {
        auto destinationForEvent = [this, watched](const QPoint &pos) {
            QAbstractItemView *view = (m_gridView && watched == m_gridView->viewport())
                ? static_cast<QAbstractItemView*>(m_gridView)
                : static_cast<QAbstractItemView*>(m_listView);
            QModelIndex idx = view ? view->indexAt(pos) : QModelIndex();
            QModelIndex srcIdx = idx;
            if (idx.isValid() && m_proxyModel && idx.model() == m_proxyModel) {
                srcIdx = m_proxyModel->mapToSource(idx);
            }

            if (idx.isValid() && m_dirModel && m_dirModel->isDir(srcIdx)) {
                return m_dirModel->filePath(srcIdx);
            }
            return currentPath();
        };

        auto decodeDroppedSources = [](const QMimeData *mimeData) {
            QStringList sources;
            if (!mimeData) return sources;
            if (mimeData->hasFormat("application/x-kasset-sequence-urls")) {
                QByteArray enc = mimeData->data("application/x-kasset-sequence-urls");
                QDataStream ds(&enc, QIODevice::ReadOnly);
                ds >> sources;
            } else if (mimeData->hasUrls()) {
                for (const QUrl &url : mimeData->urls()) {
                    if (url.isLocalFile()) sources << url.toLocalFile();
                }
            }
            sources.removeDuplicates();
            return sources;
        };

        auto sameFolderOnly = [](const QStringList &sources, const QString &destDir) {
            if (sources.isEmpty() || destDir.isEmpty()) return false;
            const QString normDest = QDir(QDir::cleanPath(destDir)).absolutePath().toLower();
            return std::all_of(sources.cbegin(), sources.cend(), [&](const QString &source) {
                QString parent = QFileInfo(source).absoluteDir().absolutePath();
                parent = QDir::cleanPath(parent).toLower();
                return parent == normDest;
            });
        };

        if (event->type() == QEvent::DragEnter) {
            auto *dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasUrls() || dragEvent->mimeData()->hasFormat("application/x-kasset-sequence-urls")) {
                const QString destDir = destinationForEvent(dragEvent->position().toPoint());
                const QStringList sources = decodeDroppedSources(dragEvent->mimeData());
                if (sameFolderOnly(sources, destDir)) {
                    dragEvent->setDropAction(Qt::IgnoreAction);
                    dragEvent->accept();
                    return true;
                }
                const bool moveRequested = dragEvent->modifiers().testFlag(Qt::ShiftModifier);
                dragEvent->setDropAction(moveRequested ? Qt::MoveAction : Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::DragMove) {
            auto *dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasUrls() || dragEvent->mimeData()->hasFormat("application/x-kasset-sequence-urls")) {
                const QString destDir = destinationForEvent(dragEvent->position().toPoint());
                const QStringList sources = decodeDroppedSources(dragEvent->mimeData());
                if (sameFolderOnly(sources, destDir)) {
                    dragEvent->setDropAction(Qt::IgnoreAction);
                    dragEvent->accept();
                    return true;
                }
                const bool moveRequested = dragEvent->modifiers().testFlag(Qt::ShiftModifier);
                dragEvent->setDropAction(moveRequested ? Qt::MoveAction : Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto *dropEvent = static_cast<QDropEvent*>(event);
            const QString destDir = destinationForEvent(dropEvent->position().toPoint());
            const QStringList sources = decodeDroppedSources(dropEvent->mimeData());
            if (destDir.isEmpty() || sources.isEmpty()) {
                return false;
            }
            if (sameFolderOnly(sources, destDir)) {
                dropEvent->setDropAction(Qt::IgnoreAction);
                dropEvent->accept();
                return true;
            }
            const bool moveRequested = dropEvent->modifiers().testFlag(Qt::ShiftModifier)
                || dropEvent->dropAction() == Qt::MoveAction
                || dropEvent->proposedAction() == Qt::MoveAction;
            emit filesDropped(sources, destDir, moveRequested);
            dropEvent->setDropAction(moveRequested ? Qt::MoveAction : Qt::CopyAction);
            dropEvent->accept();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void FileManagerPane::onItemDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;

    QModelIndex srcIdx = index;
    if (m_proxyModel && index.model() == m_proxyModel) {
        srcIdx = m_proxyModel->mapToSource(index);
    }

    if (m_dirModel->isDir(srcIdx)) {
        navigateToPath(m_dirModel->filePath(srcIdx), true);
    } else {
        emit fileDoubleClicked(m_dirModel->filePath(srcIdx));
    }
}

void FileManagerPane::onSelectionChanged()
{
    // Update preview for current selection
    QModelIndex idx = currentIndex();
    if (idx.isValid()) {
        updatePreviewForIndex(idx);
    } else {
        clearPreview();
    }
    emit selectionChanged();
}

void FileManagerPane::onViewModeToggled()
{
    setGridMode(!m_isGridMode);
}

void FileManagerPane::onThumbnailSizeChanged(int size)
{
    setThumbnailSize(size);
}

void FileManagerPane::onGroupSequencesToggled(bool checked)
{
    setSequenceGroupingEnabled(checked);
}

void FileManagerPane::onHideFoldersToggled(bool checked)
{
    setHideFolders(checked);
}

void FileManagerPane::onPreviewToggled(bool checked)
{
    setPreviewVisible(checked);
}

void FileManagerPane::updateNavigationButtons()
{
    m_backButton->setEnabled(m_navigationIndex > 0);
    m_upButton->setEnabled(!currentPath().isEmpty());
}

void FileManagerPane::loadSequenceFrame(int index)
{
    if (index < 0 || index >= m_sequenceFramePaths.size()) return;
    m_sequenceCurrentIndex = index;

    QString framePath = m_sequenceFramePaths[index];
    QImage img;
    if (OIIOImageLoader::isOIIOSupported(framePath)) {
        img = OIIOImageLoader::loadImage(framePath, 0, 0);
    }
    if (img.isNull()) {
        img = QImage(framePath);
    }
    if (!img.isNull()) {
        m_originalImage = img;
        m_imageItem->setPixmap(QPixmap::fromImage(img));
        if (m_imageFitToView) {
            m_imageScene->setSceneRect(m_imageItem->boundingRect());
            m_imageView->centerOn(m_imageItem);
            m_imageView->fitInView(m_imageItem, Qt::KeepAspectRatio);
        }
    }

    // Update slider and time label
    m_positionSlider->blockSignals(true);
    m_positionSlider->setValue(index);
    m_positionSlider->blockSignals(false);
    m_timeLabel->setText(QString("%1 / %2").arg(index + 1).arg(m_sequenceFramePaths.size()));
}

void FileManagerPane::playSequence()
{
    if (!m_isSequence || m_sequenceFramePaths.isEmpty()) return;
    m_sequencePlaying = true;
    int intervalMs = qRound(1000.0 / m_sequenceFps);
    m_sequenceTimer->start(intervalMs);
    m_playPauseBtn->setIcon(icoMediaPause(ThemeManager::instance().iconColor()));
}

void FileManagerPane::pauseSequence()
{
    m_sequencePlaying = false;
    m_sequenceTimer->stop();
    m_playPauseBtn->setIcon(icoMediaPlay(ThemeManager::instance().iconColor()));
}

void FileManagerPane::stepSequence(int delta)
{
    if (!m_isSequence || m_sequenceFramePaths.isEmpty()) return;
    pauseSequence();
    int next = m_sequenceCurrentIndex + delta;
    if (next < 0) next = m_sequenceFramePaths.size() - 1;
    if (next >= m_sequenceFramePaths.size()) next = 0;
    loadSequenceFrame(next);
}
