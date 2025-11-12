#include "preview_overlay.h"
#include "media/ffmpeg_player.h"
#include "oiio_image_loader.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QFileInfo>
#include <QScrollArea>
#include <QStyle>
#include <QApplication>
#include <QGridLayout>
#include "virtual_drag.h"


#include <QCoreApplication>

#include <QWheelEvent>
#include <QMouseEvent>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <QImage>
#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#include <QPdfView>
#endif
#include <QFontDatabase>
#include <QTextOption>
#if __has_include(<QOpenGLWidget>)
#  include <QOpenGLWidget>
#  define KAM_HAVE_QOPENGLWIDGET 1
#endif
#include <QElapsedTimer>

#include <QVector>
#include <QSettings>

#include "office_preview.h"

#include <QGraphicsSvgItem>
#if defined(Q_OS_WIN) && defined(HAVE_QT_AX)
#include <QAxObject>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <atomic>
#include <QMediaMetaData>

#include <QSettings>


#include <QRegularExpression>
#include "video_metadata.h"
#include "drag_utils.h"
#include "sequence_detector.h"
#include <QDrag>
#include <QMimeData>
#include <QUrl>

// Load media icons from disk without recoloring; search common install paths
static QIcon loadMediaIcon(const QString& relative)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList searchPaths = {
        appDir + "/icons/" + relative,
        appDir + "/../icons/" + relative,
        appDir + "/../../icons/" + relative,
        appDir + "/../Resources/icons/" + relative
    };
    for (const QString& p : searchPaths) {
        if (QFileInfo::exists(p)) {
            return QIcon(p);
        }
    }
    qWarning() << "[PreviewOverlay] Icon not found:" << relative;
    return QIcon();
}

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <QImage>
#include <QRegularExpression>

// Unified FFmpeg-based video and image sequence playback backend
// Replaces manual FallbackPngMovReader implementation with FFmpegPlayer
// Features: Hardware acceleration, smart caching, seamless integration
// All video and sequence playback now handled through FFmpegPlayer member variable

// All video playback now handled through unified FFmpegPlayer backend
#endif // HAVE_FFMPEG

PreviewOverlay::PreviewOverlay(QWidget *parent)
    : QWidget(parent)
    , imageView(nullptr)
    , imageScene(nullptr)
    , imageItem(nullptr)
    , videoItem(nullptr)
    , videoWidget(nullptr)
    , mediaPlayer(nullptr)
    , audioOutput(nullptr)
#ifdef HAVE_QT_PDF
    , pdfDoc(nullptr)
    , pdfView(nullptr)
#endif
    , svgItem(nullptr)
    , isVideo(false)
    , currentZoom(1.0)
    , isPanning(false)
    , isSequence(false)
    , currentSequenceFrame(0)
    , sequenceStartFrame(0)
    , sequenceEndFrame(0)
    , sequencePlaying(false)
    , currentColorSpace(OIIOImageLoader::ColorSpace::sRGB)
    , isHDRImage(false)
    , useCacheForSequences(true) // ENABLED - using QRecursiveMutex to fix deadlock
    , m_ffmpegPlayer(nullptr)
{
    // Initialize unified FFmpegPlayer for video and image sequence playback
    m_ffmpegPlayer = new FFmpegPlayer(this);
    if (m_ffmpegPlayer) {
        // Connect FFmpegPlayer signals to PreviewOverlay handlers
        connect(m_ffmpegPlayer, &FFmpegPlayer::frameReady, this, &PreviewOverlay::onFFmpegFrameReady);
        connect(m_ffmpegPlayer, &FFmpegPlayer::mediaInfoReady, this, &PreviewOverlay::onFFmpegMediaInfo);
        connect(m_ffmpegPlayer, &FFmpegPlayer::playbackStateChanged, this, &PreviewOverlay::onFFmpegPlaybackState);
        connect(m_ffmpegPlayer, &FFmpegPlayer::error, this, &PreviewOverlay::onFFmpegError);
        qDebug() << "[PreviewOverlay] FFmpegPlayer initialized with hardware acceleration and smart caching";
    }

    setupUi();
    setFocusPolicy(Qt::StrongFocus);

    // Auto-hide controls timer
    controlsTimer = new QTimer(this);
    controlsTimer->setSingleShot(true);
    controlsTimer->setInterval(3000);
    connect(controlsTimer, &QTimer::timeout, this, &PreviewOverlay::hideControls);

    // Sequence playback timer (target 25 fps minimum for realtime)
    sequenceTimer = new QTimer(this);
    sequenceTimer->setTimerType(Qt::PreciseTimer);
    sequenceTimer->setInterval(40); // 25 fps
    connect(sequenceTimer, &QTimer::timeout, this, &PreviewOverlay::onSequenceTimerTick);

    // Initialize frame cache for image sequence playback
    frameCache = new SequenceFrameCache(this);
    qDebug() << "[PreviewOverlay] Frame cache initialized with QRecursiveMutex (deadlock fix)";

    // tlRender integration removed
}

PreviewOverlay::~PreviewOverlay()
{
    qDebug() << "[PreviewOverlay::~PreviewOverlay] Destructor starting";
    
    // Ensure all playback modes are fully stopped before destruction
    stopPlayback();
    if (sequenceTimer) {
        sequenceTimer->stop();
    }

    // CRITICAL: Delete frame cache explicitly to ensure proper cleanup order
    // This triggers SequenceFrameCache destructor which waits for workers
    if (frameCache) {
        qDebug() << "[PreviewOverlay::~PreviewOverlay] Deleting frame cache";
        delete frameCache;
        frameCache = nullptr;
    }
    
    qDebug() << "[PreviewOverlay::~PreviewOverlay] Destructor complete";
}

void PreviewOverlay::setupUi()
{
    // Full-screen black background
    setStyleSheet("QWidget { background-color: #000000; }");
    setAttribute(Qt::WA_StyledBackground, true);

    // Set window flags to ensure overlay stays on top of parent window
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_DeleteOnClose, false); // Don't auto-delete when closed

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    // No bottom margin - controls will be at the very bottom with internal padding
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top bar with filename and close button
    QWidget *topBar = new QWidget(this);
    topBar->setStyleSheet("QWidget { background-color: rgba(0, 0, 0, 180); }");
    topBar->setFixedHeight(50);
    topBar->setFocusPolicy(Qt::NoFocus); // Ensure top bar doesn't steal focus
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(10, 0, 10, 0);

    // Alpha toggle (for images with alpha) - left side
    alphaCheck = new QCheckBox("Alpha", this);
    alphaCheck->setFocusPolicy(Qt::NoFocus);
    alphaCheck->setToolTip("Show alpha channel (grayscale)");
    alphaCheck->setStyleSheet("QCheckBox { color: white; }");
    alphaCheck->hide();
    connect(alphaCheck, &QCheckBox::toggled, this, [this](bool on){
        alphaOnlyMode = on;
        if (!imageItem || originalPixmap.isNull()) return;
        if (alphaOnlyMode && previewHasAlpha) {
            QImage src = originalPixmap.toImage().convertToFormat(QImage::Format_ARGB32);
            QImage a(src.size(), QImage::Format_Grayscale8);
            for (int y = 0; y < src.height(); ++y) {
                const QRgb* line = reinterpret_cast<const QRgb*>(src.constScanLine(y));
                uchar* dst = a.scanLine(y);
                for (int x = 0; x < src.width(); ++x) {
                    dst[x] = qAlpha(line[x]);
                }
            }
            imageItem->setPixmap(QPixmap::fromImage(a));
        } else {
            imageItem->setPixmap(originalPixmap);
        }
        imageView->viewport()->update();
    });
    topLayout->addWidget(alphaCheck);
    topLayout->addStretch();

    // Centered filename label
    fileNameLabel = new QLabel(this);
    fileNameLabel->setStyleSheet("QLabel { color: white; font-size: 16px; padding: 10px; }");
    fileNameLabel->setAlignment(Qt::AlignCenter);
    fileNameLabel->setFocusPolicy(Qt::NoFocus); // Ensure label doesn't steal focus
    topLayout->addWidget(fileNameLabel);

    topLayout->addStretch();

    // Close button - right side
    closeBtn = new QPushButton("✕", this);
    closeBtn->setFocusPolicy(Qt::NoFocus);
    closeBtn->setStyleSheet(
        "QPushButton { background-color: transparent; color: white; font-size: 24px; "
        "border: none; padding: 10px 20px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 30); }"
    );
    connect(closeBtn, &QPushButton::clicked, this, [this]() { stopPlayback(); emit closed(); });
    topLayout->addWidget(closeBtn);

    mainLayout->addWidget(topBar);

    // Content area (image or video)
    QWidget *contentWidget = new QWidget(this);
    QVBoxLayout *contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // Image view with zoom/pan support (for images and videos)
    imageView = new QGraphicsView(this);
#ifdef KAM_HAVE_QOPENGLWIDGET
    // Use OpenGL-backed viewport for smoother video/image scaling when available
    imageView->setViewport(new QOpenGLWidget());
// else: fall back to default raster viewport
#endif
    imageScene = new QGraphicsScene(this);
    imageView->setScene(imageScene);
    imageView->setStyleSheet("QGraphicsView { background-color: #000000; border: none; }");
    imageView->setRenderHints(QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    imageView->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    imageView->setOptimizationFlags(QGraphicsView::DontSavePainterState);
    imageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    imageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    imageView->setDragMode(QGraphicsView::ScrollHandDrag);
    imageView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    imageView->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    // Consume wheel on the view/viewport to avoid parent scrolling
    imageView->installEventFilter(this);
    imageView->viewport()->installEventFilter(this);
    imageView->hide();
    contentLayout->addWidget(imageView);

    // Create video item for rendering videos in the graphics scene (enables zoom/pan)
    videoItem = new QGraphicsVideoItem();
    videoItem->setAspectRatioMode(Qt::KeepAspectRatio);
    // Connect to nativeSizeChanged to fit video to view when size is known
    connect(videoItem, &QGraphicsVideoItem::nativeSizeChanged, this, [this](const QSizeF &size) {
        if (isVideo && videoItem && videoItem->scene() == imageScene && size.isValid()) {
            // Set scene rect to video size and fit to view
            imageScene->setSceneRect(videoItem->boundingRect());
            imageView->fitInView(videoItem, Qt::KeepAspectRatio);
            currentZoom = imageView->transform().m11(); // Store the fit-to-view zoom level
            fitPending = false;
        }
    });
    // Don't add to scene yet - will be added when showing video
#ifdef HAVE_QT_PDF
    // PDF view
    pdfDoc = new QPdfDocument(this);
    pdfView = new QPdfView(this);
    pdfView->setPageMode(QPdfView::PageMode::SinglePage);
    pdfView->hide();
    contentLayout->addWidget(pdfView);
#endif
    // Text view (TXT/LOG/CSV/DOCX)
    textView = new QPlainTextEdit(this);
    textView->setReadOnly(true);
    textView->setWordWrapMode(QTextOption::NoWrap);
    textView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    // Ensure white background and black text for readability
    textView->setStyleSheet("QPlainTextEdit { background-color: #ffffff; color: #000000; border: none; }");
    textView->hide();
    contentLayout->addWidget(textView);

    // Table view (XLSX)
    tableModel = new QStandardItemModel(this);
    tableView = new QTableView(this);
    tableView->setModel(tableModel);
    tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableView->setSelectionMode(QAbstractItemView::NoSelection);
    tableView->setAlternatingRowColors(true);
    tableView->setStyleSheet(
        "QTableView { background-color: #ffffff; color: #000000; gridline-color: #cccccc; border: none; }"
        "QHeaderView::section { background-color: #f0f0f0; color: #000000; border: none; padding: 4px; }"
    );
    tableView->hide();
    contentLayout->addWidget(tableView);

    // Video widget (for videos)
    videoWidget = new QVideoWidget(this);
    videoWidget->installEventFilter(this);
    videoWidget->hide();
    contentLayout->addWidget(videoWidget);

    mainLayout->addWidget(contentWidget, 1);

    // Bottom controls (for video) - positioned at the very bottom
    controlsWidget = new QWidget(this);
    controlsWidget->setStyleSheet("QWidget { background-color: rgba(0, 0, 0, 180); }");
    controlsWidget->setFixedHeight(110); // Reduced to minimize dead space
    controlsWidget->installEventFilter(this);
    controlsWidget->hide();

    QVBoxLayout *controlsLayout = new QVBoxLayout(controlsWidget);
    // Minimal margins to reduce dead space, with small bottom padding for taskbar
    controlsLayout->setContentsMargins(20, 5, 20, 10);
    controlsLayout->setSpacing(2); // Tight spacing between rows

    // Position slider with cached frame visualization
    positionSlider = new CachedFrameSlider(Qt::Horizontal, this);
    positionSlider->setFocusPolicy(Qt::NoFocus);
    positionSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #555; height: 4px; }"
        "QSlider::handle:horizontal { background: #58a6ff; width: 12px; margin: -4px 0; border-radius: 6px; }"
    );
    connect(positionSlider, &QSlider::sliderMoved, this, &PreviewOverlay::onSliderMoved);
    connect(positionSlider, &QSlider::sliderPressed, this, &PreviewOverlay::onSliderPressed);
    connect(positionSlider, &QSlider::sliderReleased, this, &PreviewOverlay::onSliderReleased);

    // Load media control icons (from icons/media)
    playIcon = loadMediaIcon("media/Play.png");
    pauseIcon = loadMediaIcon("media/Pause.png");
    prevFrameIcon = loadMediaIcon("media/Previous Frame.png");
    nextFrameIcon = loadMediaIcon("media/Next Frame.png");
    audioIcon = loadMediaIcon("media/Audio.png");
    muteIcon = loadMediaIcon("media/Mute.png");
    noAudioIcon = loadMediaIcon("media/No Audio.png");

    // ========== ROW 1: Cache bar (red line) ==========
    cacheBar = new CacheBarWidget(this);
    cacheBar->hide(); // Shown only for sequences
    {
        QHBoxLayout *cacheRow = new QHBoxLayout();
        cacheRow->setContentsMargins(0, 0, 0, 0);
        cacheRow->addWidget(cacheBar);
        controlsLayout->addLayout(cacheRow);
    }

    // ========== ROW 2: Timeline (Current | Slider | Duration | FPS) ==========
    currentTimeLabel = new QLabel("00:00:00:00", this);
    currentTimeLabel->setStyleSheet("QLabel { color: white; font-size: 14px; padding: 0 8px; }");

    durationTimeLabel = new QLabel("00:00:00:00", this);
    durationTimeLabel->setStyleSheet("QLabel { color: white; font-size: 14px; padding: 0 8px; }");

    fpsLabel = new QLabel("-- fps", this);
    fpsLabel->setStyleSheet("QLabel { color: #9aa7b0; font-size: 13px; padding: 0 8px; }");

    positionSlider->setFixedHeight(20);

    {
        QHBoxLayout *timelineRow = new QHBoxLayout();
        timelineRow->setContentsMargins(0, 2, 0, 2); // Reduced vertical margins
        timelineRow->setSpacing(8);
        timelineRow->addWidget(currentTimeLabel);
        timelineRow->addWidget(positionSlider, /*stretch*/ 1);
        timelineRow->addWidget(durationTimeLabel);
        timelineRow->addWidget(fpsLabel);
        controlsLayout->addLayout(timelineRow);
    }

    // Optional colorspace selector (hidden until relevant)
    colorSpaceLabel = new QLabel("Colorspace", this);
    colorSpaceLabel->setStyleSheet("QLabel { color: white; font-size: 14px; padding: 0 5px; }");
    colorSpaceLabel->hide();

    colorSpaceCombo = new QComboBox(this);
    // Order: sRGB, Rec.709, Linear
    colorSpaceCombo->addItem("sRGB");
    colorSpaceCombo->addItem("Rec.709");
    colorSpaceCombo->addItem("Linear");
    colorSpaceCombo->setCurrentIndex(0);
    colorSpaceCombo->setFocusPolicy(Qt::NoFocus);
    colorSpaceCombo->setStyleSheet(
        "QComboBox { background-color: #333; color: white; border: 1px solid #555; "
        "padding: 5px; border-radius: 3px; min-width: 100px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: none; border: none; }"
        "QComboBox QAbstractItemView { background-color: #333; color: white; selection-background-color: #58a6ff; }"
    );
    colorSpaceCombo->hide();
    connect(colorSpaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PreviewOverlay::onColorSpaceChanged);

    // ========== ROW 3: Playback (center) + Audio (right) ==========
    // Transport (Prev - Play/Pause - Next)
    QWidget *transport = new QWidget(this);
    QHBoxLayout *transportLayout = new QHBoxLayout(transport);
    transportLayout->setContentsMargins(0, 0, 0, 0);
    transportLayout->setSpacing(8);
    transportLayout->setAlignment(Qt::AlignVCenter);

    prevFrameBtn = new QPushButton(this);
    prevFrameBtn->setIcon(prevFrameIcon);
    prevFrameBtn->setIconSize(QSize(20, 20));
    prevFrameBtn->setFixedSize(36, 36);
    prevFrameBtn->setFocusPolicy(Qt::NoFocus);
    prevFrameBtn->setToolTip("Previous frame (,)");
    prevFrameBtn->setStyleSheet(
        "QPushButton { background-color: #444; color: white; font-size: 16px; border-radius: 18px; border: none; }"
        "QPushButton:hover { background-color: #555; }"
    );
    connect(prevFrameBtn, &QPushButton::clicked, this, &PreviewOverlay::onStepPrevFrame);
    transportLayout->addWidget(prevFrameBtn);

    playPauseBtn = new QPushButton(this);
    playPauseBtn->setIcon(playIcon);
    playPauseBtn->setIconSize(QSize(24, 24));
    playPauseBtn->setFixedSize(40, 40);
    playPauseBtn->setFocusPolicy(Qt::NoFocus);
    playPauseBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: white; font-size: 18px; "
        "border-radius: 20px; border: none; }"
        "QPushButton:hover { background-color: #4a90e2; }"
    );
    connect(playPauseBtn, &QPushButton::clicked, this, &PreviewOverlay::onPlayPauseClicked);
    transportLayout->addWidget(playPauseBtn);

    nextFrameBtn = new QPushButton(this);
    nextFrameBtn->setIcon(nextFrameIcon);
    nextFrameBtn->setIconSize(QSize(20, 20));
    nextFrameBtn->setFixedSize(36, 36);
    nextFrameBtn->setFocusPolicy(Qt::NoFocus);
    nextFrameBtn->setToolTip("Next frame (.)");
    nextFrameBtn->setStyleSheet(
        "QPushButton { background-color: #444; color: white; font-size: 16px; border-radius: 18px; border: none; }"
        "QPushButton:hover { background-color: #555; }"
    );
    connect(nextFrameBtn, &QPushButton::clicked, this, &PreviewOverlay::onStepNextFrame);
    transportLayout->addWidget(nextFrameBtn);

    // Audio controls group (right)
    QWidget *audioGroup = new QWidget(this);
    QHBoxLayout *audioLayout = new QHBoxLayout(audioGroup);
    audioLayout->setContentsMargins(0, 0, 0, 0);
    audioLayout->setSpacing(8);

    muteBtn = new QPushButton(this);
    muteBtn->setIcon(audioIcon);
    muteBtn->setIconSize(QSize(18, 18));
    muteBtn->setFlat(true);
    muteBtn->setStyleSheet("QPushButton { color: white; }");
    muteBtn->setFocusPolicy(Qt::NoFocus);
    muteBtn->setToolTip("Mute/Unmute");
    connect(muteBtn, &QPushButton::clicked, this, &PreviewOverlay::onToggleMute);
    audioLayout->addWidget(muteBtn);

    volumeSlider = new QSlider(Qt::Horizontal, this);
    volumeSlider->setFixedWidth(100);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(50);
    volumeSlider->setFocusPolicy(Qt::NoFocus);
    volumeSlider->setStyleSheet(positionSlider->styleSheet());
    connect(volumeSlider, &QSlider::valueChanged, this, &PreviewOverlay::onVolumeChanged);
    audioLayout->addWidget(volumeSlider);

    // Bottom row grid to center transport and right-align audio
    QGridLayout *bottomGrid = new QGridLayout();
    bottomGrid->setContentsMargins(0, 0, 0, 0);
    bottomGrid->setHorizontalSpacing(10);

    bottomGrid->setColumnStretch(0, 1); // left stretch
    bottomGrid->setColumnStretch(1, 0); // center content
    bottomGrid->setColumnStretch(2, 1); // right stretch (before audio)
    bottomGrid->setColumnStretch(3, 0); // audio group

    bottomGrid->addWidget(transport, 0, 1, Qt::AlignHCenter | Qt::AlignVCenter);

    // Keep color space controls in layout (hidden) between center and right
    QWidget *csGroup = new QWidget(this);
    QHBoxLayout *csLayout = new QHBoxLayout(csGroup);
    csLayout->setContentsMargins(0, 0, 0, 0);
    csLayout->setSpacing(6);
    csLayout->addWidget(colorSpaceLabel);
    csLayout->addWidget(colorSpaceCombo);
    // Do not forcibly hide the container; children visibility will control it
    bottomGrid->addWidget(csGroup, 0, 2, Qt::AlignVCenter);

    bottomGrid->addWidget(audioGroup, 0, 3, Qt::AlignRight | Qt::AlignVCenter);

    controlsLayout->addLayout(bottomGrid);

    mainLayout->addWidget(controlsWidget);
    // Overlay side navigation arrows
    navPrevBtn = new QPushButton("\u25C0", this); // ◀
    navPrevBtn->setFixedSize(64, 64);
    navPrevBtn->setFlat(true);
    navPrevBtn->setAutoFillBackground(false);
    navPrevBtn->setStyleSheet(
        "QPushButton { background: transparent; background-color: transparent; color: white; font-size: 28px; border: none; }"
        "QPushButton:hover { background: transparent; background-color: transparent; color: white; }"
        "QPushButton:pressed { background: transparent; background-color: transparent; color: white; }"
    );
    navPrevBtn->setFocusPolicy(Qt::NoFocus);
    connect(navPrevBtn, &QPushButton::clicked, this, &PreviewOverlay::navigatePrevious);
    navPrevBtn->raise();

    navNextBtn = new QPushButton("\u25B6", this); // ▶
    navNextBtn->setFixedSize(64, 64);
    navNextBtn->setFlat(true);
    navNextBtn->setAutoFillBackground(false);
    navNextBtn->setStyleSheet(
        "QPushButton { background: transparent; background-color: transparent; color: white; font-size: 28px; border: none; }"
        "QPushButton:hover { background: transparent; background-color: transparent; color: white; }"
        "QPushButton:pressed { background: transparent; background-color: transparent; color: white; }"
    );
    navNextBtn->setFocusPolicy(Qt::NoFocus);
    connect(navNextBtn, &QPushButton::clicked, this, &PreviewOverlay::navigateNext);
    navNextBtn->raise();


    // Initialize media player - use videoItem for zoom/pan support
    mediaPlayer = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    mediaPlayer->setAudioOutput(audioOutput);
    mediaPlayer->setVideoOutput(videoItem);

    connect(mediaPlayer, &QMediaPlayer::positionChanged, this, &PreviewOverlay::onPositionChanged);
    // Initial positioning
    if (navPrevBtn && navNextBtn) {
        int y = height() / 2 - navPrevBtn->height() / 2;
        navPrevBtn->move(20, y);
        navNextBtn->move(width() - 20 - navNextBtn->width(), y);
        navPrevBtn->show();
        navNextBtn->show();
    }

    connect(mediaPlayer, &QMediaPlayer::durationChanged, this, &PreviewOverlay::onDurationChanged);
    connect(mediaPlayer, &QMediaPlayer::errorOccurred, this, &PreviewOverlay::onPlayerError);
    connect(mediaPlayer, &QMediaPlayer::mediaStatusChanged, this, &PreviewOverlay::onMediaStatusChanged);

    audioOutput->setVolume(0.5);
}

void PreviewOverlay::showAsset(const QString &filePath, const QString &fileName, const QString &fileType)
{
    // First, stop any ongoing playback (video, fallback, or sequence)
    stopPlayback();

    // Reset sequence state
    isSequence = false;
    sequencePlaying = false;
    if (sequenceTimer->isActive()) {
        sequenceTimer->stop();
    }

    // Office parse-only previews
    if (fileType.compare("doc", Qt::CaseInsensitive) == 0) {
        currentFilePath = filePath;
        currentFileType = fileType.toLower();
        fileNameLabel->setText(fileName);
        showDoc(filePath);
        return;
    }
    if (fileType.compare("docx", Qt::CaseInsensitive) == 0) {
        currentFilePath = filePath;
        currentFileType = fileType.toLower();
        fileNameLabel->setText(fileName);
        showDocx(filePath);
        return;
    }
    if (fileType.compare("xlsx", Qt::CaseInsensitive) == 0) {
        currentFilePath = filePath;
        currentFileType = fileType.toLower();
        fileNameLabel->setText(fileName);
        showXlsx(filePath);
        return;
    }

    currentFilePath = filePath;
    currentFileType = fileType.toLower();
    fileNameLabel->setText(fileName);

    // Determine content type and route
    QStringList videoFormats = {"mp4", "avi", "mov", "mkv", "webm", "flv", "wmv", "m4v", "mxf"};
    isVideo = videoFormats.contains(currentFileType);

    // Make sure widget is shown and sized before loading content
    show();
    raise();
    setFocus();

    // Process events to ensure window is properly sized

    // Simple text formats shown with a plain text viewer
    if (currentFileType == "txt" || currentFileType == "log" || currentFileType == "csv") {
        showText(filePath);
        return;
    }

    // PDFs and AI (often embedded PDFs)
#ifdef HAVE_QT_PDF
    if (currentFileType == "pdf" || currentFileType == "ai") {
        showPdf(filePath);
        return;
    }
#else
    if (currentFileType == "pdf" || currentFileType == "ai") {
        // No Qt PDF available: show placeholder message
        videoWidget->hide();
        imageView->show();
        imageScene->clear();
        imageScene->addText("Preview not available", QFont("Segoe UI", 14));
        controlsWidget->hide();
        if (alphaCheck) alphaCheck->hide();
        isVideo = false;
        isHDRImage = false;
        originalPixmap = QPixmap();
        return;
    }
#endif
    // SVG vector graphics
    if (currentFileType == "svg" || currentFileType == "svgz") {
        videoWidget->hide();
#ifdef HAVE_QT_PDF
        if (pdfView) pdfView->hide();
#endif
        imageView->show();
        imageScene->clear();
        svgItem = new QGraphicsSvgItem(filePath);
        imageScene->addItem(svgItem);
        imageScene->setSceneRect(svgItem->boundingRect());
        fitImageToView();
        controlsWidget->hide();
        if (alphaCheck) alphaCheck->hide();
        isVideo = false;
        isHDRImage = false;
        originalPixmap = QPixmap();
        return;
    }

    if (isVideo) {
        showVideo(filePath);
    } else {
        showImage(filePath);
    }
}

void PreviewOverlay::showImage(const QString &filePath)
{
    // CRITICAL: Hide other widgets and show image view
    if (videoWidget) videoWidget->hide();
    if (textView) textView->hide();
    if (tableView) tableView->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    imageView->setBackgroundBrush(QColor("#0a0a0a"));
    imageView->show();

    // Anchor nav arrows to the image viewport when showing images
    positionNavButtons(imageView->viewport());

    // CRITICAL: Stop any video playback
    if (mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        mediaPlayer->stop();
    }

    // Check if this is an HDR/EXR image
    QFileInfo fileInfo(filePath);
    QString ext = fileInfo.suffix().toLower();
    isHDRImage = (ext == "exr" || ext == "hdr" || ext == "pic");

    // Try loading with OpenImageIO first for advanced formats
    QImage image;
    QPixmap newPixmap;
    if (OIIOImageLoader::isOIIOSupported(filePath)) {
        // Load at full resolution for preview (no size limit) with current color space
        image = OIIOImageLoader::loadImage(filePath, 0, 0, currentColorSpace);
        if (!image.isNull()) {
            newPixmap = QPixmap::fromImage(image);
        } else {
            qWarning() << "[PreviewOverlay::showImage] OIIO failed to load:" << filePath;
        }
    }

    // Fall back to Qt's native loader if OIIO didn't work or isn't supported
    if (newPixmap.isNull()) {
        newPixmap = QPixmap(filePath);
        isHDRImage = false; // Qt loader doesn't support HDR
    }


    if (!newPixmap.isNull()) {
        // CRITICAL: Update the pixmap on existing item or create new one
        if (imageItem) {
            imageItem->setPixmap(newPixmap);
        } else {
            imageScene->clear();
            imageItem = imageScene->addPixmap(newPixmap);
        }

        // Store the pixmap
        originalPixmap = newPixmap;

        // Determine alpha availability and reset toggle
        previewHasAlpha = (!image.isNull()) ? image.hasAlphaChannel() : newPixmap.hasAlphaChannel();
        if (alphaCheck) { alphaCheck->setVisible(previewHasAlpha); alphaCheck->blockSignals(true); alphaCheck->setChecked(false); alphaOnlyMode = false; alphaCheck->blockSignals(false); }

        // Update scene rect
        imageScene->setSceneRect(newPixmap.rect());

        // Fit to view once on first frame for this media
        fitPending = true;
        fitImageToView();
        fitPending = false;

        // CRITICAL: Force complete view refresh
        imageView->viewport()->update();
        imageView->update();
        imageScene->update();

    // For single images, only show colorspace selector when HDR
    if (isHDRImage) {
        colorSpaceLabel->show();
        colorSpaceCombo->show();
        controlsWidget->show();
    } else {
        colorSpaceLabel->hide();
        colorSpaceCombo->hide();
        controlsWidget->hide();
    }
    } else {
        qWarning() << "[PreviewOverlay::showImage] Failed to load image:" << filePath;
    }
    // Cache bar is only for sequences
    if (cacheBar) cacheBar->hide();
}

void PreviewOverlay::showVideo(const QString &filePath)
{


    // Ensure media player is in a clean state
    if (mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        mediaPlayer->stop();
    }
    // Wait for media player to fully stop before changing source
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    mediaPlayer->setSource(QUrl()); // clear previous source to avoid stray signals
    mediaPlayer->setPosition(0);

    // CRITICAL: Hide other content and show imageView with videoItem for zoom/pan support
    if (textView) textView->hide();
    if (tableView) tableView->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    videoWidget->hide(); // Hide the old video widget, we use videoItem in imageView now
    imageView->show();
    // Anchor nav arrows to the image viewport when showing video
    positionNavButtons(imageView->viewport());
    controlsWidget->show();
    // Cache bar is only for sequences
    if (cacheBar) cacheBar->hide();
    // Enable audio controls for video
    if (muteBtn) {
        muteBtn->setEnabled(true);
        const bool m = (audioOutput && audioOutput->isMuted());
        muteBtn->setIcon(m ? muteIcon : audioIcon);
    }
    if (volumeSlider) volumeSlider->setEnabled(true);


    // Hide alpha toggle for videos
    if (alphaCheck) alphaCheck->hide();

    // CRITICAL: Only clear scene if videoItem is not already in it
    // Removing and re-adding videoItem while media player is active can cause crashes
    if (videoItem->scene() != imageScene) {
        imageScene->clear();
        imageItem = nullptr;
        imageScene->addItem(videoItem);
    } else {
        // videoItem is already in the scene, just remove other items
        QList<QGraphicsItem*> items = imageScene->items();
        for (QGraphicsItem* item : items) {
            if (item != videoItem) {
                imageScene->removeItem(item);
                if (item != imageItem) { // Don't delete imageItem if it's managed elsewhere
                    delete item;
                }
            }
        }
        imageItem = nullptr;
    }

    // If tlRender backend is preferred, attempt to open container via tlRender
    // tlRender container path removed

    // Probe metadata for FPS/timecode via FFmpeg (if available)
    {
        MediaInfo::VideoMetadata vm;
        QString err;
        if (MediaInfo::probeVideoFile(filePath, vm, &err)) {
            if (vm.fps > 0.0) detectedFps = vm.fps;
            hasEmbeddedTimecode = vm.hasTimecode;
            embeddedStartTimecode = vm.timecodeStart;
        }
    }

    originalPixmap = QPixmap(); // Clear the pixmap
    fitPending = true; // next size-known signal will fit; avoid repeated fit calls

    mediaPlayer->setSource(QUrl::fromLocalFile(filePath));

    // Video will be rendered through videoItem which is already in the scene
    // Reset zoom; fit will occur when native size is known / media is loaded
    currentZoom = 1.0;
    imageView->resetTransform();

    mediaPlayer->play();

    // Try to detect FPS from metadata early (may be updated later)
    updateDetectedFps();

    controlsTimer->start();
}

void PreviewOverlay::onPlayPauseClicked()
{
    if (isSequence) {
        // Handle sequence playback
        if (sequencePlaying) {
            pauseSequence();
        } else {
            playSequence();
        }
    } else {
        // Handle video playback
        if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
            mediaPlayer->pause();
        } else {
            mediaPlayer->play();
        }
        updatePlayPauseButton();
    }
    controlsTimer->start();
}

void PreviewOverlay::onPositionChanged(qint64 position)
{
    if (!positionSlider->isSliderDown()) {
        positionSlider->setValue(position);
    }

    qint64 duration = mediaPlayer->duration();
    updateVideoTimeDisplays(position, duration);
}

void PreviewOverlay::onDurationChanged(qint64 duration)
{
    positionSlider->setRange(0, duration);
}

void PreviewOverlay::onSliderMoved(int position)
{
    if (isSequence) {
        // Seek to specific frame in sequence
        loadSequenceFrame(position);
        controlsTimer->start();
        return;
    }
    
    // QMediaPlayer path - always do live scrubbing
    mediaPlayer->setPosition(position);
    qint64 duration = mediaPlayer->duration();
    updateVideoTimeDisplays(position, duration);
    controlsTimer->start();
}

void PreviewOverlay::onVolumeChanged(int value)
{
    audioOutput->setVolume(value / 100.0);
    controlsTimer->start();
}

void PreviewOverlay::onToggleMute()
{
    if (!audioOutput) return;
    const bool newMuted = !audioOutput->isMuted();
    audioOutput->setMuted(newMuted);
    if (muteBtn) {
        muteBtn->setIcon(newMuted ? muteIcon : audioIcon);
    }
    controlsTimer->start();
}

void PreviewOverlay::onSliderPressed()
{
    userSeeking = true;
    if (isSequence) {
        wasPlayingBeforeSeek = sequencePlaying;
        if (sequencePlaying) pauseSequence();
        return;
    }
    
    wasPlayingBeforeSeek = (mediaPlayer->playbackState() == QMediaPlayer::PlayingState);
    mediaPlayer->pause();
}

void PreviewOverlay::onSliderReleased()
{
    const int pos = positionSlider->value();
    if (isSequence) {
        loadSequenceFrame(pos);
        if (wasPlayingBeforeSeek) playSequence();
        userSeeking = false;
        updatePlayPauseButton();
        controlsTimer->start();
        return;
    }
    
    mediaPlayer->setPosition(pos);
    if (wasPlayingBeforeSeek) mediaPlayer->play();
    userSeeking = false;
    updatePlayPauseButton();
    controlsTimer->start();
}

void PreviewOverlay::onStepNextFrame()
{
    if (isSequence) {
        // Always pause playback when stepping frames
        if (sequencePlaying) pauseSequence();
        int nextIdx = qMin(positionSlider->value() + 1, positionSlider->maximum());
        loadSequenceFrame(nextIdx);
        // Keep paused after stepping
        return;
    }
    
    // QMediaPlayer path - always pause when stepping frames
    mediaPlayer->pause();
    qint64 pos = mediaPlayer->position();
    qint64 dt = static_cast<qint64>(qRound64(frameDurationMs()));
    qint64 target = qMin(pos + dt, mediaPlayer->duration());
    mediaPlayer->setPosition(target);
    // Use play/pause trick to force frame update, then keep paused
    mediaPlayer->play();
    QTimer::singleShot(30, this, [this]() {
        mediaPlayer->pause();
        updatePlayPauseButton();
    });
}

void PreviewOverlay::onStepPrevFrame()
{
    if (isSequence) {
        // Always pause playback when stepping frames
        if (sequencePlaying) pauseSequence();
        int prevIdx = qMax(positionSlider->value() - 1, positionSlider->minimum());
        loadSequenceFrame(prevIdx);
        // Keep paused after stepping
        return;
    }
    
    // QMediaPlayer path - always pause when stepping frames
    mediaPlayer->pause();
    qint64 pos = mediaPlayer->position();
    qint64 dt = static_cast<qint64>(qRound64(frameDurationMs()));
    qint64 target = pos - dt; if (target < 0) target = 0;
    mediaPlayer->setPosition(target);
    // Use play/pause trick to force frame update, then keep paused
    mediaPlayer->play();
    QTimer::singleShot(30, this, [this]() {
        mediaPlayer->pause();
        updatePlayPauseButton();
    });
}

double PreviewOverlay::frameDurationMs() const
{
    double fps = detectedFps;
    if (fps <= 0.0) fps = 24.0;
    return 1000.0 / fps;
}

void PreviewOverlay::updateDetectedFps()
{
    detectedFps = 0.0;
    if (!isVideo) return;
    
    if (mediaPlayer) {
        QVariant v = mediaPlayer->metaData().value(QMediaMetaData::VideoFrameRate);
        if (v.isValid()) {
            detectedFps = v.toDouble();
        }
    }
    if (detectedFps <= 0.0) detectedFps = 24.0;
}


void PreviewOverlay::hideControls()
{
    // Never hide playback controls in full-screen previews
    // Requirement: playback buttons must remain visible at all times
    Q_UNUSED(this);
    return; // no-op
}

void PreviewOverlay::updatePlayPauseButton()
{
    if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
        playPauseBtn->setIcon(pauseIcon);
    } else {
        playPauseBtn->setIcon(playIcon);
    }
}
void PreviewOverlay::positionNavButtons(QWidget* container)
{
    if (!navPrevBtn || !navNextBtn || !container) return;

    navContainer = container;

    const int margin = 16;

    // Compute vertical center based on the overlay (this window), not the content widget
    const int overlayCenterY = height() / 2 - navPrevBtn->height() / 2;

    const bool videoCase = (container == videoWidget);

    auto setupTopLevel = [this](QPushButton* b){
        // Convert to a small top-level tool window that can float above native video
        if (b->parentWidget() != this || !(b->windowFlags() & Qt::Tool)) {
            b->setParent(this, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
            b->setAttribute(Qt::WA_TranslucentBackground, true);
            b->setFocusPolicy(Qt::NoFocus);
            b->show();
        }
    };
    auto setupChild = [container](QPushButton* b){
        if (b->parentWidget() != container || (b->windowFlags() & Qt::Window)) {
            b->setParent(container);
            b->setWindowFlags(Qt::Widget);
            b->show();
        }
    };

    if (videoCase) {
        // Top-level tool windows: use global coordinates for Y from the overlay center
        setupTopLevel(navPrevBtn);
        setupTopLevel(navNextBtn);
        const int yGlobal = mapToGlobal(QPoint(0, qMax(0, overlayCenterY))).y();
        const int leftX  = container->mapToGlobal(QPoint(margin, 0)).x();
        const int rightX = container->mapToGlobal(QPoint(qMax(0, container->width() - margin - navNextBtn->width()), 0)).x();
        navPrevBtn->move(QPoint(leftX, yGlobal));
        navNextBtn->move(QPoint(rightX, yGlobal));
        navPrevBtn->raise();
        navNextBtn->raise();
    } else {
        // Child widgets: map overlay center Y into container coordinates and clamp
        setupChild(navPrevBtn);
        setupChild(navNextBtn);
        const int yInContainer = qBound(0,
            container->mapFromGlobal(mapToGlobal(QPoint(0, overlayCenterY))).y(),
            qMax(0, container->height() - navPrevBtn->height()));
        navPrevBtn->move(margin, yInContainer);
        navNextBtn->move(qMax(0, container->width() - margin - navNextBtn->width()), yInContainer);
        navPrevBtn->raise();
        navNextBtn->raise();
    }
}


QString PreviewOverlay::formatTime(qint64 milliseconds)
{
    int seconds = milliseconds / 1000;
    int minutes = seconds / 60;
    seconds = seconds % 60;
    return QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0'));
}


// Helper to format HH:MM:SS:FF given milliseconds and fps
static QString formatHMSF(qint64 ms, int fps)
{
    if (fps <= 0) fps = 24;
    qint64 totalSeconds = ms / 1000;
    int hours = static_cast<int>(totalSeconds / 3600);
    int minutes = static_cast<int>((totalSeconds % 3600) / 60);
    int seconds = static_cast<int>(totalSeconds % 60);
    int frames = static_cast<int>(qRound64((ms % 1000) * (fps / 1000.0)));
    return QString("%1:%2:%3:%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(frames, 2, 10, QChar('0'));
}

// Very simple non-dropframe timecode adder: start + frames @ fps
static QString addFramesToTimecode(const QString& startTc, qint64 framesToAdd, int fps)
{
    QRegularExpression re("^(\\d{2}):(\\d{2}):(\\d{2})[:;](\\d{2})$");
    QRegularExpressionMatch m = re.match(startTc);
    if (!m.hasMatch()) {
        // Fallback: format from frames only
        qint64 ms = (framesToAdd * 1000) / qMax(1, fps);
        return formatHMSF(ms, fps);
    }
    int h = m.captured(1).toInt();
    int min = m.captured(2).toInt();
    int s = m.captured(3).toInt();
    int f = m.captured(4).toInt();
    qint64 totalFrames = ((h * 3600LL) + (min * 60LL) + s) * fps + f + framesToAdd;
    if (totalFrames < 0) totalFrames = 0;
    int oh = static_cast<int>(totalFrames / (fps * 3600LL));
    totalFrames %= (fps * 3600LL);
    int omin = static_cast<int>(totalFrames / (fps * 60LL));
    totalFrames %= (fps * 60LL);
    int os = static_cast<int>(totalFrames / fps);
    int of = static_cast<int>(totalFrames % fps);
    return QString("%1:%2:%3:%4")
        .arg(oh, 2, 10, QChar('0'))
        .arg(omin, 2, 10, QChar('0'))
        .arg(os, 2, 10, QChar('0'))
        .arg(of, 2, 10, QChar('0'));
}

void PreviewOverlay::updateVideoTimeDisplays(qint64 positionMs, qint64 durationMs)
{
    int fpsInt = qMax(1, static_cast<int>(qRound(detectedFps > 0.0 ? detectedFps : 24.0)));
    if (hasEmbeddedTimecode && !embeddedStartTimecode.isEmpty()) {
        qint64 posFrames = qRound64(positionMs * (fpsInt / 1000.0));
        qint64 durFrames = durationMs > 0 ? qRound64(durationMs * (fpsInt / 1000.0)) : -1;
        if (currentTimeLabel)
            currentTimeLabel->setText(addFramesToTimecode(embeddedStartTimecode, posFrames, fpsInt));
        if (durationTimeLabel)
            durationTimeLabel->setText(durFrames >= 0 ? addFramesToTimecode(embeddedStartTimecode, durFrames, fpsInt) : QString("--:--:--:--"));
    } else {
        if (currentTimeLabel) currentTimeLabel->setText(formatHMSF(positionMs, fpsInt));
        if (durationTimeLabel) durationTimeLabel->setText(durationMs > 0 ? formatHMSF(durationMs, fpsInt) : QString("--:--:--:--"));
    }
}

void PreviewOverlay::updateSequenceTimeDisplays(int frameIndex, bool caching)
{
    int actualFrame = sequenceStartFrame + frameIndex;
    if (currentTimeLabel) {
        currentTimeLabel->setText(QString("Frame %1").arg(actualFrame) + (caching ? " [CACHING...]" : ""));
    }
    if (durationTimeLabel) {
        durationTimeLabel->setText(QString("%1").arg(sequenceEndFrame));
    }
}

void PreviewOverlay::navigateNext()
{
    emit navigateRequested(1);
}

void PreviewOverlay::navigatePrevious()
{
    emit navigateRequested(-1);
}

void PreviewOverlay::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_Escape:
            // Ensure playback fully stops before closing overlay
            stopPlayback();
            emit closed();
            break;
        case Qt::Key_Left:
            // CTRL+Left: Step backward one frame
            if (event->modifiers() & Qt::ControlModifier) {
                if (isVideo || isSequence) { onStepPrevFrame(); return; }
                break;
            }
            // Navigate to previous file (MainWindow will handle stopPlayback)
            navigatePrevious();
            break;
        case Qt::Key_Right:
            // CTRL+Right: Step forward one frame
            if (event->modifiers() & Qt::ControlModifier) {
                if (isVideo || isSequence) { onStepNextFrame(); return; }
                break;
            }
            // Navigate to next file (MainWindow will handle stopPlayback)
            navigateNext();
            break;
        case Qt::Key_Period: // '.' next frame
            if (isVideo || isSequence) { onStepNextFrame(); return; }
            break;
        case Qt::Key_Comma: // ',' previous frame
            if (isVideo || isSequence) { onStepPrevFrame(); return; }
            break;
#ifdef HAVE_QT_PDF
        case Qt::Key_Up:
            if ((currentFileType == "pdf" || currentFileType == "ai") && pdfDoc && pdfDoc->pageCount() > 1) {
                if (pdfCurrentPage > 0) { pdfCurrentPage--; renderPdfPageToImage(); }
                return; // consume
            }
            break;
        case Qt::Key_Down:
            if ((currentFileType == "pdf" || currentFileType == "ai") && pdfDoc && pdfDoc->pageCount() > 1) {
                if (pdfCurrentPage + 1 < pdfDoc->pageCount()) { pdfCurrentPage++; renderPdfPageToImage(); }
                return; // consume
            }
            break;
#endif
        case Qt::Key_Space:
            // Space toggles overlay visibility (consistent with File/Asset Manager)
            stopPlayback();
            emit closed();
            break;
        default:
            QWidget::keyPressEvent(event);
    }
}

void PreviewOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    // Refit image if showing an image
    if (!isVideo && !originalPixmap.isNull()) {
        fitImageToView();
    }

    // Reposition nav arrows within their container on overlay resize
    if (navContainer) {
        positionNavButtons(navContainer);
    }
}

void PreviewOverlay::mousePressEvent(QMouseEvent *event)
{
    // Right-click resets zoom for both images and videos
    if (event->button() == Qt::RightButton) {
        resetImageZoom();
        event->accept();
        return;
    }

    if (isVideo) {
        // Show controls on click
        controlsWidget->show();
        controlsTimer->start();
    } else if (event->button() == Qt::MiddleButton) {
        // Start panning for images
        isPanning = true;
        lastPanPoint = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
    QWidget::mousePressEvent(event);
}

void PreviewOverlay::wheelEvent(QWheelEvent *event)
{
    // Enable zoom for images, videos, and sequences (fallback if event reached overlay)
    if (!originalPixmap.isNull() || (videoItem && videoItem->scene() == imageScene) || isSequence) {
        double factor = event->angleDelta().y() > 0 ? 1.15 : 0.85;
        zoomImage(factor);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

bool PreviewOverlay::eventFilter(QObject* watched, QEvent* event)
{
    // Handle wheel events for image/video zoom
    if ((watched == imageView || (imageView && watched == imageView->viewport())) && event->type() == QEvent::Wheel) {
        // Enable zoom for images (when pixmap is loaded) or videos (when videoItem is in scene) or sequences
        if (!originalPixmap.isNull() || (videoItem && videoItem->scene() == imageScene) || isSequence) {
            QWheelEvent* wheel = static_cast<QWheelEvent*>(event);
            double factor = wheel->angleDelta().y() > 0 ? 1.15 : 0.85;
            zoomImage(factor);
            wheel->accept();
            return true; // consume to prevent any scrolling
        }
    }

    // Drag-and-drop from overlay preview is disabled
    // (User requested to disable DnD from full screen preview player)

    // Handle keyboard events from child widgets (videoWidget, imageView, etc.)
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        // Forward CTRL+Left/Right for frame stepping
        if (keyEvent->modifiers() & Qt::ControlModifier) {
            if (keyEvent->key() == Qt::Key_Left) {
                if (isVideo || isSequence) {
                    onStepPrevFrame();
                    return true; // consume event
                }
            } else if (keyEvent->key() == Qt::Key_Right) {
                if (isVideo || isSequence) {
                    onStepNextFrame();
                    return true; // consume event
                }
            }
        }

        // Forward other important keys to the overlay's keyPressEvent
        if (keyEvent->key() == Qt::Key_Escape ||
            keyEvent->key() == Qt::Key_Space ||
            keyEvent->key() == Qt::Key_Left ||
            keyEvent->key() == Qt::Key_Right ||
            keyEvent->key() == Qt::Key_Period ||
            keyEvent->key() == Qt::Key_Comma) {
            keyPressEvent(keyEvent);
            return true; // consume event
        }
    }

    return QWidget::eventFilter(watched, event);
}

void PreviewOverlay::zoomImage(double factor)
{
    currentZoom *= factor;

    // Limit zoom range
    if (currentZoom < 0.1) currentZoom = 0.1;
    if (currentZoom > 10.0) currentZoom = 10.0;

    imageView->resetTransform();
    imageView->scale(currentZoom, currentZoom);
}

void PreviewOverlay::fitImageToView()
{
    if (originalPixmap.isNull()) return;

    // Calculate zoom to fit image in view
    QRectF viewRect = imageView->viewport()->rect();
    QRectF sceneRect = imageScene->sceneRect();

    double xRatio = viewRect.width() / sceneRect.width();
    double yRatio = viewRect.height() / sceneRect.height();

    currentZoom = qMin(xRatio, yRatio);

    imageView->resetTransform();
    imageView->scale(currentZoom, currentZoom);
    imageView->centerOn(imageItem);
}

void PreviewOverlay::resetImageZoom()
{
    // Reset zoom for both images and videos
    if (isVideo && videoItem && videoItem->scene() == imageScene) {
        // For videos, fit to view
        imageScene->setSceneRect(videoItem->boundingRect());
        imageView->fitInView(videoItem, Qt::KeepAspectRatio);
        currentZoom = imageView->transform().m11();
    } else if (imageItem) {
        // For images and sequences, reset to 1:1
        currentZoom = 1.0;
        imageView->resetTransform();
        imageView->centerOn(imageItem);
    }
}

void PreviewOverlay::showSequence(const QStringList &framePaths, const QString &sequenceName, int startFrame, int endFrame)
{
    isSequence = true;
    isVideo = false;
    sequenceFramePaths = framePaths;
    sequenceStartFrame = startFrame;
    sequenceEndFrame = endFrame;
    currentSequenceFrame = 0;
    sequencePlaying = false;

    // Check if this is an HDR/EXR sequence
    if (!framePaths.isEmpty()) {
        QFileInfo fileInfo(framePaths.first());
        QString ext = fileInfo.suffix().toLower();
        isHDRImage = (ext == "exr" || ext == "hdr" || ext == "pic");
    } else {
        isHDRImage = false;
    }

    // Make sure widget is shown and sized before loading content
    show();
    raise();
    setFocus();

    // Anchor nav arrows to the image viewport for sequences
    positionNavButtons(imageView->viewport());

    // Process events to ensure window is properly sized

    // CRITICAL: Clear scene before loading sequence
    imageScene->clear();
    imageItem = nullptr;

    // Show image view and controls
    videoWidget->hide();
    imageView->show();
    controlsWidget->show();
    // Disable audio controls for image sequences (no audio)
    if (muteBtn) { muteBtn->setEnabled(false); muteBtn->setIcon(noAudioIcon); }
    fitPending = true; // ensure first loaded frame fits once
    if (volumeSlider) volumeSlider->setEnabled(false);


    // Stop video player if running
    if (mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        mediaPlayer->stop();
    }

    // Update file name label
    fileNameLabel->setText(sequenceName);

    // Always show colorspace selector for image sequences.
    // Default: EXR -> Linear; others -> sRGB (user-changeable).
    colorSpaceLabel->show();
    colorSpaceCombo->show();
    if (!sequenceFramePaths.isEmpty()) {
        QFileInfo fi(sequenceFramePaths.first());
        const QString extLower = fi.suffix().toLower();
        if (extLower == "exr") {
            currentColorSpace = OIIOImageLoader::ColorSpace::Linear;
            colorSpaceCombo->blockSignals(true);
            colorSpaceCombo->setCurrentIndex(2); // Linear
            colorSpaceCombo->blockSignals(false);
        } else {
            currentColorSpace = OIIOImageLoader::ColorSpace::sRGB;
            colorSpaceCombo->blockSignals(true);
            colorSpaceCombo->setCurrentIndex(0); // sRGB
            colorSpaceCombo->blockSignals(false);
        }
    }

    // Clear cached frame visualization
    positionSlider->clearCachedFrames();

    // Initialize frame cache for this sequence (only if enabled)
    if (frameCache && useCacheForSequences) {
        frameCache->setSequence(framePaths, currentColorSpace);
        qDebug() << "[PreviewOverlay] Frame cache initialized for sequence with" << framePaths.size() << "frames";

        // Prepare to receive cache progress updates (disconnect to avoid duplicates)
        disconnect(frameCache, &SequenceFrameCache::frameCached, nullptr, nullptr);
        // We no longer paint cache on the slider; only the cache bar reflects caching

        // Update the separate cache bar as frames are cached (incremental)
        connect(frameCache, &SequenceFrameCache::frameCached, this, [this](int frameIndex){
            if (!cacheBar) return;
            if (!cacheBarUpdateTimer.isValid()) cacheBarUpdateTimer.start();
            if (cacheBarUpdateTimer.elapsed() >= 16) {
                cacheBar->markFrameCached(frameIndex);
                cacheBar->show();
                cacheBarUpdateTimer.restart();
            }
        });

        // Snapshots replace stale marks when window slides or evictions happen
        connect(frameCache, &SequenceFrameCache::cacheSnapshot, this, [this](const QSet<int>& frames){
            if (!cacheBar) return;
            cacheBar->setCachedFrames(frames);
            cacheBar->show();
        });
        // Start pre-fetching immediately (this will load frames in background)
        frameCache->startPrefetch(0);
        qDebug() << "[PreviewOverlay] Started pre-fetching frames from index 0";
    }

    // tlRender sequence path removed

    // FPS UI reset for sequences
    if (fpsLabel) fpsLabel->setText("-- fps");
    sequenceFpsFrames = 0;
    sequenceFpsTimer.invalidate();

    // Load first frame (synchronously for immediate display)
    if (!sequenceFramePaths.isEmpty()) {
        // For the first frame, load it directly (not from cache) to ensure immediate display
        QString framePath = sequenceFramePaths[0];
        QImage image;
        if (OIIOImageLoader::isOIIOSupported(framePath)) {
            image = OIIOImageLoader::loadImage(framePath, 0, 0, currentColorSpace);
            if (!image.isNull()) {
                originalPixmap = QPixmap::fromImage(image);
            }
        }
        if (originalPixmap.isNull()) {
            originalPixmap = QPixmap(framePath);
        }

        // Display the first frame
        if (!originalPixmap.isNull()) {
            imageScene->clear();
            imageItem = imageScene->addPixmap(originalPixmap);
    // Initialize cache bar for sequence
    if (cacheBar) {
        cacheBar->setTotalFrames(sequenceFramePaths.size());
        cacheBar->clearCachedFrames();
        cacheBar->show();
    }
            imageScene->setSceneRect(originalPixmap.rect());
            fitImageToView();
        }
    }

    // Update slider for sequence
    positionSlider->setRange(0, sequenceFramePaths.size() - 1);
    positionSlider->setValue(0);

    // Update time labels
    updateSequenceTimeDisplays(0);

    // Update play/pause button
    updatePlayPauseButton();

    // Show controls
    controlsWidget->show();
    controlsTimer->start();
}

void PreviewOverlay::loadSequenceFrame(int frameIndex)
{
    if (frameIndex < 0 || frameIndex >= sequenceFramePaths.size()) {
        qWarning() << "[PreviewOverlay::loadSequenceFrame] Invalid frame index:" << frameIndex;
        return;
    }

    currentSequenceFrame = frameIndex;
    QPixmap newPixmap;

    // Try to get frame from cache first (only if cache is enabled)
    if (frameCache && useCacheForSequences) {
        newPixmap = frameCache->getFrame(frameIndex);

        // Update cache's current frame position for pre-fetching
        frameCache->setCurrentFrame(frameIndex);

        // If cache returned empty pixmap (frame not ready), pause playback and wait
        if (newPixmap.isNull()) {
            // Keep realtime cadence: do NOT pause the timer.
            // Maintain last displayed frame and just update UI positions.
            positionSlider->blockSignals(true);
            positionSlider->setValue(frameIndex);
            positionSlider->blockSignals(false);
            updateSequenceTimeDisplays(frameIndex, true);
            return; // skip displaying until frame becomes ready; timer continues
        }

        // Frame is ready from cache, use it
        originalPixmap = newPixmap;
    } else {
        // Original implementation: load directly from disk
        QString framePath = sequenceFramePaths[frameIndex];
        QImage image;
        if (OIIOImageLoader::isOIIOSupported(framePath)) {
            image = OIIOImageLoader::loadImage(framePath, 0, 0, currentColorSpace);
            if (!image.isNull()) {
                originalPixmap = QPixmap::fromImage(image);
            } else {
                qWarning() << "[PreviewOverlay::loadSequenceFrame] OIIO failed to load frame";
            }
        }

        // Fall back to Qt loader
        if (originalPixmap.isNull()) {
            originalPixmap = QPixmap(framePath);
            if (originalPixmap.isNull()) {
                qWarning() << "[PreviewOverlay::loadSequenceFrame] Qt failed to load frame";
            }
        }
    }

    if (!originalPixmap.isNull()) {
        if (!imageItem) {
            imageItem = imageScene->addPixmap(originalPixmap);
        } else {
            imageItem->setPixmap(originalPixmap);
        }
        // Only update scene rect if size changed
        if (lastFrameSize != originalPixmap.size()) {
            imageScene->setSceneRect(originalPixmap.rect());
            lastFrameSize = originalPixmap.size();
            // Ensure fit on first frame or when dimensions change
            fitPending = true;
        }

        // Determine alpha availability and reset toggle for new frame/sequence
        previewHasAlpha = originalPixmap.hasAlphaChannel();
        if (alphaCheck) { alphaCheck->setVisible(previewHasAlpha); alphaCheck->blockSignals(true); alphaCheck->setChecked(false); alphaOnlyMode = false; alphaCheck->blockSignals(false); }

        // Fit once per sequence or when requested
        if (fitPending) {
            fitImageToView();
            fitPending = false;
        }

        // FPS update (real measured)
        if (sequencePlaying) {
            ++sequenceFpsFrames;
            if (!sequenceFpsTimer.isValid()) sequenceFpsTimer.start();
            qint64 elapsed = sequenceFpsTimer.elapsed();
            if (elapsed >= 500) { // update twice per second
                double fps = (sequenceFpsFrames * 1000.0) / qMax<qint64>(1, elapsed);
                currentPlaybackFps = fps;
                if (fpsLabel) fpsLabel->setText(QString::number(fps, 'f', 1) + " fps");
                sequenceFpsFrames = 0;
                sequenceFpsTimer.restart();
            }
        }
    } else {
        qWarning() << "[PreviewOverlay::loadSequenceFrame] Failed to load frame - pixmap is null!";
    }

    // Update slider and time label (throttled)
    bool doUiUpdate = true;
    if (!uiUpdateTimer.isValid()) uiUpdateTimer.start();
    else if (uiUpdateTimer.elapsed() < 30) doUiUpdate = false; // ~33 fps UI updates
    if (doUiUpdate) {
        uiUpdateTimer.restart();
        positionSlider->blockSignals(true);
        positionSlider->setValue(frameIndex);
        positionSlider->blockSignals(false);
        updateSequenceTimeDisplays(frameIndex);
    }
}

void PreviewOverlay::playSequence()
{
    if (!isSequence || sequenceFramePaths.isEmpty()) {
        return;
    }

    // Optional: require full warm before playback (defaults to false)
    bool requireFullWarm = false;
    {
        QSettings s("AugmentCode", "KAssetManager");
        requireFullWarm = s.value("SequenceCache/RequireFullWarmBeforePlay", false).toBool();
    }
    if (requireFullWarm) {
        // If RAM cache is enabled, warm the FULL cache window before starting
        if (frameCache && useCacheForSequences) {
            const int target = qMin(frameCache->maxCacheSize(), positionSlider ? (positionSlider->maximum()+1) : sequenceFramePaths.size());
            if (frameCache->cachedFrameCount() < target) {
                frameCache->startPrefetch(currentSequenceFrame);
                QTimer::singleShot(15, this, &PreviewOverlay::playSequence);
                return; // wait until warmed to target window
            }
        }
    }

    sequencePlaying = true;
    sequenceTimer->start();
    updatePlayPauseButton();

    // Keep prefetching while playing (only if cache is enabled)
    if (frameCache && useCacheForSequences) {
        frameCache->startPrefetch(currentSequenceFrame);
        qDebug() << "[PreviewOverlay] Playing sequence at 25 fps with pre-fetching enabled";
    } else {
        qDebug() << "[PreviewOverlay] Playing sequence at 25 fps (cache disabled)";
    }

    // Start FPS measurement
    sequenceFpsFrames = 0;
    sequenceFpsTimer.restart();
    if (fpsLabel) fpsLabel->setText("-- fps");
}

void PreviewOverlay::pauseSequence()
{
    sequencePlaying = false;
    sequenceTimer->stop();
    updatePlayPauseButton();

    // Keep pre-fetching running when paused so frames continue to load in background
    // This allows smooth scrubbing and instant resume

    qDebug() << "[PreviewOverlay] Paused sequence";
    if (fpsLabel) fpsLabel->setText("Paused");
}

void PreviewOverlay::stopSequence()
{
    sequencePlaying = false;
    sequenceTimer->stop();
    currentSequenceFrame = 0;

    // Stop pre-fetching when stopped (only if cache is enabled)
    if (frameCache && useCacheForSequences) {
        frameCache->stopPrefetch();
    }

    loadSequenceFrame(0);
    updatePlayPauseButton();
    if (fpsLabel) fpsLabel->setText("-- fps");
}

void PreviewOverlay::onSequenceTimerTick()
{
    if (!isSequence || !sequencePlaying) {
        return;
    }

    // Advance to next frame
    currentSequenceFrame++;

    // Loop back to start if at end
    if (currentSequenceFrame >= sequenceFramePaths.size()) {
        currentSequenceFrame = 0;
        // When looping, only kick prefetch if cache isn’t already full
        if (frameCache && useCacheForSequences) {
            const int need = qMin(frameCache->maxCacheSize(), sequenceFramePaths.size());
            if (frameCache->cachedFrameCount() < need) {
                qDebug() << "[PreviewOverlay] Sequence looped; cache not full, restarting prefetch";
                frameCache->startPrefetch(0);
            }
        }
    }

    loadSequenceFrame(currentSequenceFrame);
}

void PreviewOverlay::onColorSpaceChanged(int index)
{
    qDebug() << "[PreviewOverlay] Color space changed to index:" << index;

    // Update current color space
    switch (index) {
        case 0:
            currentColorSpace = OIIOImageLoader::ColorSpace::sRGB;
            qDebug() << "[PreviewOverlay] Switched to sRGB color space";
            break;
        case 1:
            currentColorSpace = OIIOImageLoader::ColorSpace::Rec709;
            qDebug() << "[PreviewOverlay] Switched to Rec.709 color space";
            break;
        case 2:
            currentColorSpace = OIIOImageLoader::ColorSpace::Linear;
            qDebug() << "[PreviewOverlay] Switched to Linear color space";
            break;
        default:
            currentColorSpace = OIIOImageLoader::ColorSpace::sRGB;
            break;
    }

    // Reload current frame/image with new color space
    if (isSequence) {
        qDebug() << "[PreviewOverlay] Reloading sequence frame with new color space";

        // Clear cache and reinitialize with new color space (only if cache is enabled)
        if (frameCache && useCacheForSequences) {
            frameCache->setSequence(sequenceFramePaths, currentColorSpace);
            // Clear cache bar visualization and restart prefetch for accurate redraw
            if (cacheBar) {
                cacheBar->clearCachedFrames();
                cacheBar->setTotalFrames(sequenceFramePaths.size());
                cacheBar->show();
            }
            frameCache->startPrefetch(currentSequenceFrame);
        }

        loadSequenceFrame(currentSequenceFrame);
    } else if (!currentFilePath.isEmpty() && isHDRImage) {
        qDebug() << "[PreviewOverlay] Reloading image with new color space";
        showImage(currentFilePath);
    }
}

void PreviewOverlay::stopPlayback()
{
    qDebug() << "[PreviewOverlay] Stopping playback";

    // Stop video playback
    if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
        mediaPlayer->stop();
    }

    // Stop sequence playback
    if (sequencePlaying) {
        pauseSequence();
    }

    // Stop pre-fetching but keep cache intact (only if cache is enabled)
    if (frameCache && useCacheForSequences) {
        frameCache->stopPrefetch();
    }

    // Clear the media source to release the file (only if it has a source)
    if (!mediaPlayer->source().isEmpty()) {
        mediaPlayer->setSource(QUrl());
    }
}



void PreviewOverlay::onPlayerError(QMediaPlayer::Error error, const QString &errorString)
{
    qWarning() << "[PreviewOverlay] Media player error:" << error << errorString;
}

void PreviewOverlay::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    // Update FPS when metadata becomes available
    updateDetectedFps();

    // Ensure newly loaded videos are fit to the current overlay window
    // when stepping between items or when media finishes loading.
    if (isVideo && imageView && videoItem && videoItem->scene() == imageScene) {
        switch (status) {
        case QMediaPlayer::LoadedMedia:
        case QMediaPlayer::BufferedMedia:
            imageScene->setSceneRect(videoItem->boundingRect());
            imageView->fitInView(videoItem, Qt::KeepAspectRatio);
            currentZoom = imageView->transform().m11();
            break;
        default:
            break;
        }
    }
}

void PreviewOverlay::showText(const QString &filePath)
{
    // Hide other content
    videoWidget->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    imageView->hide();
    controlsWidget->hide();
    if (alphaCheck) alphaCheck->hide();

    if (!textView) return;

    QFile f(filePath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
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

        textView->setPlainText(decodeText(data));
    } else {
        textView->setPlainText("Preview not available");
    }
    textView->show();
    positionNavButtons(textView);
}
void PreviewOverlay::showDocx(const QString &filePath)
{
    // Hide other content
    if (videoWidget) videoWidget->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    if (imageView) imageView->hide();
    if (controlsWidget) controlsWidget->hide();
    if (alphaCheck) alphaCheck->hide();

    // Ensure overlay is visible for Office previews
    show();
    raise();
    setFocus();

    if (!textView) return;

    // Use proportional UI font for Word documents
    textView->setFont(QFont("Segoe UI"));

    const QString text = extractDocxText(filePath);
    textView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    if (!text.isEmpty()) {
        textView->setPlainText(text);
    } else {
        textView->setPlainText("Preview not available");
    }
    textView->show();
    positionNavButtons(textView);
}

void PreviewOverlay::showDoc(const QString &filePath)
{
    // Hide other content
    if (videoWidget) videoWidget->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    if (imageView) imageView->hide();
    if (controlsWidget) controlsWidget->hide();
    if (alphaCheck) alphaCheck->hide();

    // Ensure overlay is visible for Office previews
    show();
    raise();
    setFocus();

    if (!textView) return;

    // Use proportional UI font for Word documents
    textView->setFont(QFont("Segoe UI"));

    const QString text = extractDocBinaryText(filePath, 2 * 1024 * 1024);
    textView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    if (!text.isEmpty()) {
        textView->setPlainText(text);
    } else {
        textView->setPlainText("Preview not available");
    }
    textView->show();
    positionNavButtons(textView);
}


void PreviewOverlay::showXlsx(const QString &filePath)
{
    // Hide other content
    if (videoWidget) videoWidget->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    if (imageView) imageView->hide();
    if (controlsWidget) controlsWidget->hide();
    if (alphaCheck) alphaCheck->hide();

    // Ensure overlay is visible for Office previews
    show();
    raise();
    setFocus();

    if (!tableView || !tableModel) return;

    tableModel->clear();
    if (!loadXlsxSheet(filePath, tableModel, 2000)) {
        // Fallback: simple message in text view
        if (textView) {
            textView->setPlainText("Preview not available");
            textView->show();
        }
        return;
    }

    tableView->resizeColumnsToContents();
    tableView->show();
    positionNavButtons(tableView);
}

#ifdef HAVE_QT_PDF
void PreviewOverlay::showPdf(const QString &filePath)
{
    // Hide other content
    if (videoWidget) videoWidget->hide();
    if (textView) textView->hide();
    if (tableView) tableView->hide();
    controlsWidget->hide();
    if (alphaCheck) alphaCheck->hide();

    // Load PDF (works for many AI files that embed PDF)
    if (!pdfDoc) pdfDoc = new QPdfDocument(this);
    pdfDoc->close();
    QPdfDocument::Error err = pdfDoc->load(filePath);
    if (err == QPdfDocument::Error::None && pdfDoc->pageCount() > 0) {
        pdfCurrentPage = 0;
        imageView->show();
        positionNavButtons(imageView->viewport());
        // Always render into the image view for consistent zoom/pan behavior
        renderPdfPageToImage();
    #ifdef HAVE_QT_PDF_WIDGETS
        if (pdfView) pdfView->hide();
    #endif
    } else {
        // Fallback: show a message
        imageView->hide();
        if (textView) {
            textView->setPlainText("Preview not available");
            textView->show();
            positionNavButtons(textView);
        }
    }
}

void PreviewOverlay::renderPdfPageToImage()
{
    if (!pdfDoc || pdfDoc->pageCount() <= 0) return;
    if (pdfCurrentPage < 0) pdfCurrentPage = 0;
    if (pdfCurrentPage >= pdfDoc->pageCount()) pdfCurrentPage = pdfDoc->pageCount() - 1;

    const QSizeF pts = pdfDoc->pagePointSize(pdfCurrentPage);
    int vw = imageView ? imageView->viewport()->width() : 800;
    if (vw < 1) vw = 800;
    int w = vw;
    int h = pts.width() > 0 ? int(pts.height() * (w / pts.width())) : w;
    QImage img = pdfDoc->render(pdfCurrentPage, QSize(w, h));
    if (img.isNull()) return;

    // Composite onto white to avoid dark theme bleeding
    if (img.hasAlphaChannel()) {
        QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied);
        bg.fill(Qt::white);
        QPainter p(&bg);
        p.drawImage(0, 0, img);
        p.end();
        img = bg;
    }

    originalPixmap = QPixmap::fromImage(img);
    if (!imageItem) {
        imageScene->clear();
        imageItem = imageScene->addPixmap(originalPixmap);
    } else {
        imageItem->setPixmap(originalPixmap);
    }
    imageScene->setSceneRect(originalPixmap.rect());
    imageView->setBackgroundBrush(Qt::white);
    fitImageToView();
    imageView->viewport()->update();
}
#endif


void PreviewOverlay::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    if (navContainer) positionNavButtons(navContainer);
}

// ============================================================================
// SequenceFrameCache Implementation
// ============================================================================

SequenceFrameCache::SequenceFrameCache(QObject *parent)
    : QObject(parent)
    , m_colorSpace(OIIOImageLoader::ColorSpace::sRGB)
    , m_cache(100 * 50 * 1024) // Max cost in KB: will be updated based on settings
    , m_threadPool(QThreadPool::globalInstance())
    , m_maxCacheSize(100)
    , m_currentFrame(0)
    , m_prefetchActive(false)
    , m_epoch(1)
{
    // Using global thread pool; do not modify its thread count here to avoid side effects.

    // Load cache size from settings
    QSettings s("AugmentCode", "KAssetManager");
    bool autoSize = s.value("SequenceCache/AutoSize", true).toBool();

    if (autoSize) {
        // Calculate based on available RAM
        int autoPercent = s.value("SequenceCache/AutoPercent", 70).toInt();
        m_maxCacheSize = calculateOptimalCacheSize(autoPercent);
    } else {
        // Use manual size
        m_maxCacheSize = s.value("SequenceCache/ManualSize", 100).toInt();
    }

    // Update cache max cost based on calculated size
    // Assume 50MB per frame average
    int maxCostKB = m_maxCacheSize * 50 * 1024;
    m_cache.setMaxCost(maxCostKB);

    qDebug() << "[SequenceFrameCache] ========================================";
    qDebug() << "[SequenceFrameCache] INITIALIZATION:";
    qDebug() << "[SequenceFrameCache]   Max cache size:" << m_maxCacheSize << "frames";
    qDebug() << "[SequenceFrameCache]   Max cost:" << maxCostKB << "KB (" << (maxCostKB / 1024) << "MB)";
    qDebug() << "[SequenceFrameCache]   Worker threads:" << m_threadPool->maxThreadCount();
    qDebug() << "[SequenceFrameCache]   Auto-size:" << (autoSize ? "YES" : "NO");
    if (autoSize) {
        int autoPercent = s.value("SequenceCache/AutoPercent", 70).toInt();
        qDebug() << "[SequenceFrameCache]   RAM percentage:" << autoPercent << "%";
    }
    qDebug() << "[SequenceFrameCache] ========================================";
}

SequenceFrameCache::~SequenceFrameCache()
{
    qDebug() << "[SequenceFrameCache::~SequenceFrameCache] Destructor starting";
    
    // Mark all in-flight tasks as cancelled by bumping epoch
    stopPrefetch();
    
    // CRITICAL: Wait for all pending frame loaders to finish before destroying
    // This prevents use-after-free when workers emit signals to destroyed cache
    if (m_threadPool && !m_pendingFrames.isEmpty()) {
        qDebug() << "[SequenceFrameCache] Waiting for" << m_pendingFrames.size() << "pending workers";
        // Wait up to 2 seconds for workers to finish (they check epoch and exit quickly)
        m_threadPool->waitForDone(2000);
    }
    
    clearCache();
    
    qDebug() << "[SequenceFrameCache::~SequenceFrameCache] Destructor complete";
}

void SequenceFrameCache::setSequence(const QStringList &framePaths, OIIOImageLoader::ColorSpace colorSpace)
{
    QMutexLocker locker(&m_mutex);
    stopPrefetch();
    clearCache();
    m_framePaths = framePaths;
    m_colorSpace = colorSpace;
    m_currentFrame = 0;
    m_windowStart = 0;
    m_windowEnd = qMax(-1, qMin((int)framePaths.size()-1, m_maxCacheSize-1));
    m_nextToEnqueue = m_windowStart;
    // Load optional concurrency setting
    {
        QSettings s("AugmentCode", "KAssetManager");
        int conc = s.value("SequenceCache/PrefetchConcurrency", 4).toInt();
        m_prefetchConcurrency = qMax(1, conc);
    }
    qDebug() << "[SequenceFrameCache] Set sequence with" << framePaths.size() << "frames";
}

void SequenceFrameCache::clearCache()
{
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_pendingFrames.clear();
    // Notify listeners that cache is empty
    emit cacheSnapshot(QSet<int>());
}

QPixmap SequenceFrameCache::getFrame(int frameIndex)
{
    QMutexLocker locker(&m_mutex);

    if (frameIndex < 0 || frameIndex >= m_framePaths.size()) {
        qWarning() << "[SequenceFrameCache::getFrame] Invalid frame index:" << frameIndex;
        return QPixmap();
    }

    // Check if frame is in cache
    QPixmap *cached = m_cache.object(frameIndex);
    if (cached) {
        // Cache hit - return the frame
        return *cached;
    }

    // Cache miss - return empty pixmap (non-blocking)
    // The pre-fetcher will load this frame in the background
    return QPixmap();
}

bool SequenceFrameCache::hasFrame(int frameIndex) const
{
    QMutexLocker locker(&m_mutex);
    return m_cache.contains(frameIndex);
}

void SequenceFrameCache::startPrefetch(int currentFrame)
{
    QMutexLocker locker(&m_mutex);
    m_prefetchActive = true;
    m_currentFrame = currentFrame;
    locker.unlock();

    prefetchFrames(currentFrame);
}

void SequenceFrameCache::stopPrefetch()
{
    // Mark prefetch as inactive and invalidate all in-flight workers by bumping the epoch
    QMutexLocker locker(&m_mutex);
    m_prefetchActive = false;
    m_pendingFrames.clear();
    m_epoch.fetch_add(1, std::memory_order_relaxed);
}

void SequenceFrameCache::setCurrentFrame(int frameIndex)
{
    QMutexLocker locker(&m_mutex);

    // Detect if we've jumped backwards significantly (e.g., looping from end to start)
    bool hasLooped = (m_currentFrame > frameIndex) && (m_currentFrame - frameIndex > 50);

    if (hasLooped) {
        // Don't clear cache on loop - just let it continue with existing frames
        // The prefetch will load any missing frames from the new position
    }

    // Strict sliding window forward: [windowStart .. windowEnd]
    const int total = m_framePaths.size();
    const int window = qMin(m_maxCacheSize, total);
    int desiredStart = qBound(0, frameIndex, qMax(0, total - window));
    int desiredEnd = qMin(total - 1, desiredStart + window - 1);
    if (desiredStart != m_windowStart || desiredEnd != m_windowEnd) {
        m_windowStart = desiredStart;
        m_windowEnd = desiredEnd;
        // Ensure next enqueue pointer is at least current frame
        m_nextToEnqueue = qMax(m_nextToEnqueue, m_windowStart);
        // Hard-evict anything outside the window to prevent fragmentation
        QList<int> keys;
        for (int i = 0; i < total; ++i) {
            if (m_cache.contains(i) && (i < m_windowStart || i > m_windowEnd)) keys.append(i);
        }
        for (int k : keys) m_cache.remove(k);
        m_pendingFrames.clear();
        // Emit fresh snapshot limited to window
        QSet<int> snap;
        for (int i = m_windowStart; i <= m_windowEnd; ++i) {
            if (m_cache.contains(i)) snap.insert(i);
        }
        emit cacheSnapshot(snap);
    }

    m_currentFrame = frameIndex;

    if (m_prefetchActive) {
        locker.unlock();
        prefetchFrames(frameIndex);
    }
}

void SequenceFrameCache::setMaxCacheSize(int maxFrames)
{
    QMutexLocker locker(&m_mutex);
    m_maxCacheSize = maxFrames;
    m_cache.setMaxCost(maxFrames * 50 * 1024); // Assume ~50MB per frame (4K images can be large)
}

qint64 SequenceFrameCache::currentMemoryUsageMB() const
{
    QMutexLocker locker(&m_mutex);
    // Estimate memory usage: number of cached frames * average frame size (30MB)
    return static_cast<qint64>(m_cache.count() * 30);
}

int SequenceFrameCache::cachedFrameCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_cache.count();
}

void SequenceFrameCache::prefetchFrames(int startFrame)
{
    QMutexLocker locker(&m_mutex);
    const quint64 epoch = m_epoch.load(std::memory_order_relaxed);

    if (!m_prefetchActive || m_framePaths.isEmpty()) {
        return;
    }

    // Strict sequential fill within [m_windowStart..m_windowEnd]
    const int totalFrames = m_framePaths.size();
    if (m_windowEnd < m_windowStart || totalFrames == 0) return;

    // Back up nextToEnqueue to current frame if user seeked backwards
    if (startFrame < m_nextToEnqueue) m_nextToEnqueue = qMax(m_windowStart, startFrame);

    int inFlight = m_pendingFrames.size();
    while (inFlight < m_prefetchConcurrency && m_nextToEnqueue <= m_windowEnd) {
        const int idx = m_nextToEnqueue;
        ++m_nextToEnqueue;
        if (m_cache.contains(idx) || m_pendingFrames.contains(idx)) continue;
        scheduleFrameIfNeeded(idx, epoch, /*highPriority*/true);
        ++inFlight;
    }
}

bool SequenceFrameCache::isRangeMostlyCached(int start, int end, double threshold) const
{
    int total = qMax(0, end - start + 1);
    if (total == 0) return true;
    int cached = 0;
    for (int i=start; i<=end; ++i) {
        if (m_cache.contains(i)) ++cached;
    }
    return (static_cast<double>(cached) / total) >= threshold;
}

void SequenceFrameCache::scheduleFrameIfNeeded(int frameIndex, quint64 epoch, bool highPriority)
{
    if (m_cache.contains(frameIndex) || m_pendingFrames.contains(frameIndex) || frameIndex < 0 || frameIndex >= m_framePaths.size()) return;
    m_pendingFrames.insert(frameIndex);
    QString framePath = m_framePaths[frameIndex];
    FrameLoaderWorker *worker = new FrameLoaderWorker(this, frameIndex, framePath, m_colorSpace, epoch);
    
    // CRITICAL: Use Qt::QueuedConnection with context object to ensure auto-disconnect
    // This prevents crashes when SequenceFrameCache is destroyed while workers are running
    connect(worker, &FrameLoaderWorker::frameLoaded, this, [this](int idx, QPixmap pixmap) {
        // SAFETY: This lambda won't execute if 'this' is destroyed (Qt auto-disconnect)
        {
            QMutexLocker locker(&m_mutex);
            m_pendingFrames.remove(idx);
            if (!pixmap.isNull() && m_prefetchActive) {
                int cost = pixmap.width() * pixmap.height() * 4 / 1024; // KB
                m_cache.insert(idx, new QPixmap(pixmap), cost);
            } else if (pixmap.isNull()) {
                qWarning() << "[SequenceFrameCache] Failed to load frame" << idx;
            }
        }
        emit frameCached(idx);
        // Optional: emit a throttled snapshot for accurate UI; keep cheap by limiting to window
        {
            QSet<int> snap;
            QMutexLocker locker(&m_mutex);
            for (int i = m_windowStart; i <= m_windowEnd; ++i) {
                if (m_cache.contains(i)) snap.insert(i);
            }
            locker.unlock();
            emit cacheSnapshot(snap);
        }
        // Queue more work to respect concurrency limit
        QMetaObject::invokeMethod(this, [this](){ prefetchFrames(m_currentFrame); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection); // EXPLICIT QueuedConnection for proper cleanup
    
    m_threadPool->start(worker, highPriority ? QThread::HighestPriority : QThread::LowPriority);
}

QPixmap SequenceFrameCache::loadFrame(int frameIndex)
{
    if (frameIndex < 0 || frameIndex >= m_framePaths.size()) {
        return QPixmap();
    }

    QString framePath = m_framePaths[frameIndex];
    QImage image;

    // Try OpenImageIO first for supported formats
    if (OIIOImageLoader::isOIIOSupported(framePath)) {
        image = OIIOImageLoader::loadImage(framePath, 0, 0, m_colorSpace);
    }

    // Fall back to Qt loader
    if (image.isNull()) {
        image.load(framePath);
    }

    if (image.isNull()) {
        qWarning() << "[SequenceFrameCache] Failed to load frame:" << framePath;
        return QPixmap();
    }

    return QPixmap::fromImage(image);
}

// ============================================================================
// FrameLoaderWorker Implementation
// ============================================================================

FrameLoaderWorker::FrameLoaderWorker(SequenceFrameCache *cache, int frameIndex,
                                     const QString &framePath, OIIOImageLoader::ColorSpace colorSpace, quint64 epoch)
    : m_cache(cache)
    , m_frameIndex(frameIndex)
    , m_framePath(framePath)
    , m_colorSpace(colorSpace)
    , m_epoch(epoch)
{
    setAutoDelete(true);
}

void FrameLoaderWorker::run()
{
    // Capture cache pointer; it may be deleted while we run
    SequenceFrameCache* cache = m_cache.data();
    if (!cache) {
        return;
    }

    // If this worker belongs to an older epoch, exit early
    if (!cache->isEpochCurrent(m_epoch)) {
        return;
    }

    QImage image;

    // Try OpenImageIO first for supported formats
    if (OIIOImageLoader::isOIIOSupported(m_framePath)) {
        image = OIIOImageLoader::loadImage(m_framePath, 0, 0, m_colorSpace);
        // Re-check cancellation after potentially expensive I/O
        if (!cache->isEpochCurrent(m_epoch)) {
            return;
        }
    }

    // Fall back to Qt loader if needed
    if (image.isNull()) {
        image.load(m_framePath);
        if (!cache->isEpochCurrent(m_epoch)) {
            return;
        }
    }

    if (!cache->isEpochCurrent(m_epoch)) {
        return;
    }

    if (!image.isNull()) {
        QPixmap pixmap = QPixmap::fromImage(image);
        // Final check before emitting to avoid enqueuing into a stale cache
        if (cache->isEpochCurrent(m_epoch)) {
            emit frameLoaded(m_frameIndex, pixmap);
        }
    } else {
        qWarning() << "[FrameLoaderWorker] Failed to load frame:" << m_framePath;
        // Only notify failure if still current; otherwise earlier stopPrefetch()
        // already cleared pending state for this frame.
        if (cache->isEpochCurrent(m_epoch)) {
            emit frameLoaded(m_frameIndex, QPixmap());
        }
    }
}

// ============================================================================
// Static helper functions for cache size calculation
// ============================================================================

qint64 SequenceFrameCache::getAvailableRAM()
{
#ifdef Q_OS_WIN
    // Windows: Use GlobalMemoryStatusEx
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
        // Return available physical memory in MB
        return static_cast<qint64>(memInfo.ullAvailPhys / (1024 * 1024));
    }
#elif defined(Q_OS_LINUX)
    // Linux: Read from /proc/meminfo
    QFile file("/proc/meminfo");
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine();
            if (line.startsWith("MemAvailable:")) {
                QStringList parts = line.split(QRegularExpression("\\s+"));
                if (parts.size() >= 2) {
                    qint64 kb = parts[1].toLongLong();
                    return kb / 1024; // Convert KB to MB
                }
            }
        }
    }
#elif defined(Q_OS_MAC)
    // macOS: Use sysctl
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t memsize;
    size_t len = sizeof(memsize);
    if (sysctl(mib, 2, &memsize, &len, NULL, 0) == 0) {
        return static_cast<qint64>(memsize / (1024 * 1024));
    }
#endif

    // Fallback: return 8GB as a conservative estimate
    qWarning() << "[SequenceFrameCache] Could not detect available RAM, using 8GB default";
    return 8192;
}

int SequenceFrameCache::calculateOptimalCacheSize(int percentOfFreeRAM)
{
    qint64 availableRAM = getAvailableRAM();
    qDebug() << "[SequenceFrameCache] Available RAM:" << availableRAM << "MB";

    // Calculate cache size based on percentage of available RAM
    // Assume average frame size of 30MB (conservative for 4K EXR)
    const int avgFrameSizeMB = 30;
    qint64 cacheRAM = (availableRAM * percentOfFreeRAM) / 100;
    int cacheFrames = static_cast<int>(cacheRAM / avgFrameSizeMB);

    // Clamp to reasonable range: 10-500 frames
    cacheFrames = qMax(10, qMin(500, cacheFrames));

    qDebug() << "[SequenceFrameCache] Calculated optimal cache size:" << cacheFrames
             << "frames (" << (cacheFrames * avgFrameSizeMB) << "MB) using"
             << percentOfFreeRAM << "% of available RAM";

    return cacheFrames;
}


// ============================================================================
// FFmpegPlayer Signal Handler Implementations
// ============================================================================

void PreviewOverlay::onFFmpegFrameReady(const FFmpegPlayer::VideoFrame& frame)
{
    if (!frame.isValid()) {
        qWarning() << "[PreviewOverlay] Received invalid frame from FFmpegPlayer";
        return;
    }
    
    // Update pixmap in the image scene
    originalPixmap = QPixmap::fromImage(frame.image);
    
    // Hide alpha toggle for videos
    if (alphaCheck) alphaCheck->hide();
    
    if (!imageItem) {
        imageItem = imageScene->addPixmap(originalPixmap);
        // Fit exactly once for the new media to avoid per-frame jank
        if (fitPending) {
            fitImageToView();
            fitPending = false;
        }
    } else {
        imageItem->setPixmap(originalPixmap);
    }
    
    // Update UI time/slider with frame timestamp
    if (frame.timestampMs >= 0) {
        positionSlider->setValue(static_cast<int>(frame.timestampMs));
        updateVideoTimeDisplays(frame.timestampMs, mediaPlayer->duration());
    }
}

void PreviewOverlay::onFFmpegMediaInfo(const FFmpegPlayer::MediaInfo& info)
{
    qDebug() << "[PreviewOverlay] FFmpegPlayer media info:"
             << "Duration:" << info.durationMs << "ms"
             << "FPS:" << info.fps
             << "Resolution:" << info.width << "x" << info.height
             << "Codec:" << info.codec;
    
    // Update duration display
    if (info.durationMs > 0) {
        positionSlider->setRange(0, info.durationMs);
        updateVideoTimeDisplays(0, info.durationMs);
    }
    
    // Update FPS display
    if (info.fps > 0.0) {
        detectedFps = info.fps;
        if (fpsLabel) fpsLabel->setText(QString::number(info.fps, 'f', 1) + " fps");
    }
}

void PreviewOverlay::onFFmpegPlaybackState(FFmpegPlayer::PlaybackState state)
{
    qDebug() << "[PreviewOverlay] FFmpegPlayer state changed to:" << static_cast<int>(state);
    
    switch (state) {
        case FFmpegPlayer::PlaybackState::Playing:
            playPauseBtn->setIcon(pauseIcon);
            break;
        case FFmpegPlayer::PlaybackState::Paused:
        case FFmpegPlayer::PlaybackState::Stopped:
            playPauseBtn->setIcon(playIcon);
            break;
        case FFmpegPlayer::PlaybackState::Loading:
            // Optional: show loading indicator
            break;
        case FFmpegPlayer::PlaybackState::Error:
            playPauseBtn->setIcon(playIcon);
            break;
    }
}

void PreviewOverlay::onFFmpegError(const QString& errorString)
{
    qWarning() << "[PreviewOverlay] FFmpegPlayer error:" << errorString;
    
    // Fallback to standard QMediaPlayer if FFmpegPlayer fails
    if (!mediaPlayer->source().isEmpty()) {
        qDebug() << "[PreviewOverlay] Falling back to QMediaPlayer";
        mediaPlayer->play();
    }
}
