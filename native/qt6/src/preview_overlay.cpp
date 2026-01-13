#include "preview_overlay.h"
#ifdef HAVE_TLRENDER
#include "media/tlrender_player.h"
#include "media/tlrender_viewport.h"
// Compatibility macros for tlRender
#define PLAYER_PTR m_player
#define PLAYER_PLAY() m_player->play()
#define PLAYER_PAUSE() m_player->pause()
#define PLAYER_STOP() m_player->stop()
#define PLAYER_SEEK(pos) m_player->seek(pos)
#define PLAYER_DURATION() m_player->duration()
#define PLAYER_POSITION() m_player->position()
#define PLAYER_SET_URI(uri) m_player->load(uri)
#define PLAYER_STATE() m_player->playbackState()
#define PLAYER_STATE_PLAYING TLRenderPlayer::PlaybackState::Playing
#define PLAYER_STATE_PAUSED TLRenderPlayer::PlaybackState::Paused
#define PLAYER_STATE_STOPPED TLRenderPlayer::PlaybackState::Stopped
#else
#include "media/gstreamer_player.h"
// Compatibility macros for GStreamer
#define PLAYER_PTR m_gstreamerPlayer
#define PLAYER_PLAY() m_gstreamerPlayer->play()
#define PLAYER_PAUSE() m_gstreamerPlayer->pause()
#define PLAYER_STOP() m_gstreamerPlayer->stop()
#define PLAYER_SEEK(pos) m_gstreamerPlayer->seek(pos)
#define PLAYER_DURATION() m_gstreamerPlayer->duration()
#define PLAYER_POSITION() m_gstreamerPlayer->position()
#define PLAYER_SET_URI(uri) m_gstreamerPlayer->setUri(uri)
#define PLAYER_STATE() m_gstreamerPlayer->state()
#define PLAYER_STATE_PLAYING GStreamerPlayer::PlaybackState::Playing
#define PLAYER_STATE_PAUSED GStreamerPlayer::PlaybackState::Paused
#define PLAYER_STATE_STOPPED GStreamerPlayer::PlaybackState::Stopped
#endif
#include "oiio_image_loader.h"
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QPainter>
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
#include <QColorDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QGraphicsSceneMouseEvent>
#include <algorithm>

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

// Simple tint helper to force monochrome icons to a desired color
static QIcon tintIcon(const QIcon& base, const QColor& color)
{
    if (base.isNull()) return base;
    QIcon result;
    const QList<QIcon::Mode> modes = {QIcon::Normal, QIcon::Disabled, QIcon::Active, QIcon::Selected};
    const QList<QIcon::State> states = {QIcon::Off, QIcon::On};
    for (QIcon::Mode mode : modes) {
        for (QIcon::State state : states) {
            QPixmap pm = base.pixmap(32, 32, mode, state);
            if (pm.isNull()) continue;
            QPixmap tinted(pm.size());
            tinted.fill(Qt::transparent);
            QPainter p(&tinted);
            p.drawPixmap(0, 0, pm);
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(tinted.rect(), color);
            p.end();
            result.addPixmap(tinted, mode, state);
        }
    }
    return result.isNull() ? base : result;
}

static constexpr int kControlsMinHeight = 130;
static constexpr int kControlsMaxHeight = 180;
static constexpr int kImageControlsMinHeight = 44;
static constexpr int kImageControlsMaxHeight = 72;
static constexpr int kControlsSideMargin = 20;
static constexpr int kControlsTopMargin = 5;
static constexpr int kControlsBottomMargin = 10;
static constexpr int kControlsSpacing = 2;
static constexpr int kImageControlsSideMargin = 12;
static constexpr int kImageControlsTopMargin = 4;
static constexpr int kImageControlsBottomMargin = 6;
static constexpr int kImageControlsSpacing = 0;

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
    , videoWidget(nullptr)
#ifdef HAVE_TLRENDER
    , m_player(nullptr)
    , m_renderWidget(nullptr)
#else
    , m_gstreamerPlayer(nullptr)
#endif
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
    , annotationLayer(nullptr)
    , annotationModeEnabled(false)
    , annotationToolbar(nullptr)
    , currentAnnotatedFrame(-1)
{
#ifdef HAVE_TLRENDER
    // Initialize tlRender globally (call once)
    TLRenderPlayer::initialize();

    // Create tlRender player for video and image sequence playback with OCIO support
    m_player = new TLRenderPlayer(this);

    // Connect TLRenderPlayer signals to PreviewOverlay handlers
    connect(m_player, &TLRenderPlayer::positionChanged, this, &PreviewOverlay::onPlayerPositionChanged);
    connect(m_player, &TLRenderPlayer::durationChanged, this, &PreviewOverlay::onPlayerDurationChanged);
    connect(m_player, &TLRenderPlayer::mediaInfoReady, this, &PreviewOverlay::onPlayerMediaInfo);
    connect(m_player, &TLRenderPlayer::playbackStateChanged, this, &PreviewOverlay::onPlayerPlaybackStateChanged);
    connect(m_player, &TLRenderPlayer::error, this, &PreviewOverlay::onPlayerError);
    connect(m_player, &TLRenderPlayer::endOfStream, this, &PreviewOverlay::onPlayerEndOfStream);

    qDebug() << "[PreviewOverlay] TLRenderPlayer initialized with OCIO color management";
#else
    // Initialize GStreamer globally (call once)
    GStreamerPlayer::initialize();

    // Create GStreamer player for video and image sequence playback
    m_gstreamerPlayer = new GStreamerPlayer(this);

    // Connect GStreamerPlayer signals to PreviewOverlay handlers
    connect(m_gstreamerPlayer, &GStreamerPlayer::positionChanged, this, &PreviewOverlay::onGStreamerPositionChanged);
    connect(m_gstreamerPlayer, &GStreamerPlayer::durationChanged, this, &PreviewOverlay::onGStreamerDurationChanged);
    connect(m_gstreamerPlayer, &GStreamerPlayer::mediaInfoReady, this, &PreviewOverlay::onGStreamerMediaInfo);
    connect(m_gstreamerPlayer, &GStreamerPlayer::playbackStateChanged, this, &PreviewOverlay::onGStreamerPlaybackStateChanged);
    connect(m_gstreamerPlayer, &GStreamerPlayer::error, this, &PreviewOverlay::onGStreamerError);
    connect(m_gstreamerPlayer, &GStreamerPlayer::endOfStream, this, &PreviewOverlay::onGStreamerEndOfStream);

    qDebug() << "[PreviewOverlay] GStreamerPlayer initialized with hardware-accelerated playback";
#endif

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
    
    // CRITICAL: Clean up annotation layer before scene is destroyed
    if (annotationLayer) {
        qDebug() << "[PreviewOverlay::~PreviewOverlay] Clearing annotation layer";
        annotationLayer->clearAnnotations();
        delete annotationLayer;
        annotationLayer = nullptr;
    }
    
    // Clean up frame annotations map (now uses JSON, no manual cleanup needed)
    frameAnnotations.clear();

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

    // Set window flags - use normal window with title bar for proper move/resize support
    // Removed FramelessWindowHint to fix HighDPI issues and enable window management
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    setWindowTitle(tr("Preview"));
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
    
    // Annotation toggle button with icon
    toggleAnnotationBtn = new QPushButton(this);
    QString annotationIconPath = QCoreApplication::applicationDirPath() + "/Icons/Annotation/Annotate.png";
    toggleAnnotationBtn->setIcon(QIcon(annotationIconPath));
    toggleAnnotationBtn->setIconSize(QSize(24, 24));
    toggleAnnotationBtn->setText(" Annotate");
    toggleAnnotationBtn->setFocusPolicy(Qt::NoFocus);
    toggleAnnotationBtn->setCheckable(true);
    toggleAnnotationBtn->setStyleSheet(
        "QPushButton { background-color: #444; color: white; padding: 8px 15px; "
        "border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #555; }"
        "QPushButton:checked { background-color: #58a6ff; }"
    );
    toggleAnnotationBtn->setToolTip("Toggle annotation mode (A)");
    connect(toggleAnnotationBtn, &QPushButton::clicked, this, &PreviewOverlay::onToggleAnnotation);
    topLayout->addWidget(toggleAnnotationBtn);

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
#if defined(KAM_HAVE_QOPENGLWIDGET) && !(defined(HAVE_TLRENDER) && HAVE_TLRENDER)
    // Use OpenGL-backed viewport for smoother video/image scaling when available.
    // When tlRender is enabled, avoid an extra QOpenGLWidget to reduce context churn.
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

    // Dedicated still image view to keep image-only behavior isolated
    stillImageView = new QGraphicsView(this);
    stillImageView->setScene(imageScene);
    stillImageView->setStyleSheet("QGraphicsView { background-color: #000000; border: none; }");
    stillImageView->setRenderHints(QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    stillImageView->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    stillImageView->setOptimizationFlags(QGraphicsView::DontSavePainterState);
    stillImageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    stillImageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    stillImageView->setDragMode(QGraphicsView::ScrollHandDrag);
    stillImageView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    stillImageView->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    stillImageView->installEventFilter(this);
    stillImageView->viewport()->installEventFilter(this);
    stillImageView->hide();
    contentLayout->addWidget(stillImageView);

    // Video widget for GStreamer direct rendering
    videoWidget = new QWidget(this);
    videoWidget->setStyleSheet("QWidget { background-color: #000000; }");

    // CRITICAL: Set widget attributes for native window embedding
    // These attributes tell Qt to create a native window that GStreamer can render into
    // Do NOT set these attributes when using tlRender/QOpenGLWidget.
#if !(defined(HAVE_TLRENDER) && HAVE_TLRENDER)
    videoWidget->setAttribute(Qt::WA_NativeWindow);
    videoWidget->setAttribute(Qt::WA_PaintOnScreen);
    videoWidget->setAttribute(Qt::WA_OpaquePaintEvent);
#endif

    videoWidget->installEventFilter(this);
    videoWidget->hide();
    contentLayout->addWidget(videoWidget);
    
    // Create transparent annotation overlay for videos
    // This sits on top of videoWidget when annotation mode is enabled
    // IMPORTANT: Don't parent to videoWidget because it has WA_PaintOnScreen which blocks Qt rendering
    annotationOverlayView = new QGraphicsView(this); // Parent to PreviewOverlay instead
    annotationOverlayScene = new QGraphicsScene(this);
    annotationOverlayView->setScene(annotationOverlayScene);
    
    // Make the view and viewport truly transparent
    annotationOverlayView->setStyleSheet("QGraphicsView { background: transparent; border: none; }");
    annotationOverlayView->setAttribute(Qt::WA_TranslucentBackground);
    annotationOverlayView->setAttribute(Qt::WA_TransparentForMouseEvents, false); // Accept mouse events
    annotationOverlayView->setAttribute(Qt::WA_NoSystemBackground);
    annotationOverlayView->viewport()->setAttribute(Qt::WA_TranslucentBackground);
    annotationOverlayView->viewport()->setAutoFillBackground(false);
    
    // Set background brush to transparent
    annotationOverlayView->setBackgroundBrush(Qt::transparent);
    annotationOverlayScene->setBackgroundBrush(Qt::transparent);
    
    annotationOverlayView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    annotationOverlayView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    annotationOverlayView->setFrameShape(QFrame::NoFrame);
    annotationOverlayView->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    annotationOverlayView->setInteractive(true);
    annotationOverlayView->hide();
    // Will position overlay absolutely when annotation mode is enabled
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

    mainLayout->addWidget(contentWidget, 1);

    // Bottom controls (for video) - positioned at the very bottom
    controlsWidget = new QWidget(this);
    controlsWidget->setStyleSheet("QWidget { background-color: rgba(0, 0, 0, 180); }");
    controlsWidget->setMinimumHeight(kControlsMinHeight);
    controlsWidget->setMaximumHeight(kControlsMaxHeight); // Flexible height to fit all controls
    controlsWidget->installEventFilter(this);
    controlsWidget->hide();

    controlsLayout = new QVBoxLayout(controlsWidget);
    // Minimal margins to reduce dead space, with small bottom padding for taskbar
    controlsLayout->setContentsMargins(kControlsSideMargin, kControlsTopMargin,
                                       kControlsSideMargin, kControlsBottomMargin);
    controlsLayout->setSpacing(kControlsSpacing); // Tight spacing between rows

    // Position slider with cached frame visualization
    positionSlider = new CachedFrameSlider(Qt::Horizontal, this);
    positionSlider->setFocusPolicy(Qt::NoFocus);
    positionSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: transparent; height: 28px; margin: 0 12px; }"
        "QSlider::handle:horizontal { background: transparent; width: 18px; margin: -14px 0; }"
    );
    positionSlider->setFixedHeight(42);
    positionSlider->setTimelineContext(true, 24.0);
    connect(positionSlider, &QSlider::sliderMoved, this, &PreviewOverlay::onSliderMoved);
    connect(positionSlider, &QSlider::sliderPressed, this, &PreviewOverlay::onSliderPressed);
    connect(positionSlider, &QSlider::sliderReleased, this, &PreviewOverlay::onSliderReleased);

    // Load media control icons (from icons/media)
    playIcon = loadMediaIcon("media/Play.png");
    pauseIcon = loadMediaIcon("media/Pause.png");
    prevFrameIcon = loadMediaIcon("media/Previous Frame.png");
    nextFrameIcon = loadMediaIcon("media/Next Frame.png");
    audioIcon = tintIcon(loadMediaIcon("media/Audio.png"), Qt::white);
    muteIcon = tintIcon(loadMediaIcon("media/Mute.png"), Qt::white);
    noAudioIcon = tintIcon(loadMediaIcon("media/No Audio.png"), Qt::white);

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

#ifdef HAVE_TLRENDER
    // OCIO controls (hidden until video/sequence playback)
    ocioEnableCheck = new QCheckBox("OCIO", this);
    ocioEnableCheck->setFocusPolicy(Qt::NoFocus);
    ocioEnableCheck->setStyleSheet("QCheckBox { color: white; font-size: 14px; padding: 0 4px; }");
    ocioEnableCheck->setChecked(true);
    ocioEnableCheck->hide();

    ocioConfigBtn = new QPushButton("Config…", this);
    ocioConfigBtn->setFocusPolicy(Qt::NoFocus);
    ocioConfigBtn->setStyleSheet(
        "QPushButton { background-color: #444; color: white; font-size: 13px; border-radius: 6px; padding: 4px 8px; border: none; }"
        "QPushButton:hover { background-color: #555; }"
    );
    ocioConfigBtn->hide();

    ocioDisplayLabel = new QLabel("Display", this);
    ocioDisplayLabel->setStyleSheet("QLabel { color: white; font-size: 14px; padding: 0 4px; }");
    ocioDisplayLabel->hide();

    ocioDisplayCombo = new QComboBox(this);
    ocioDisplayCombo->setFocusPolicy(Qt::NoFocus);
    ocioDisplayCombo->setStyleSheet(
        "QComboBox { background-color: #333; color: white; border: 1px solid #555; padding: 5px; border-radius: 3px; min-width: 120px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: none; border: none; }"
        "QComboBox QAbstractItemView { background-color: #333; color: white; selection-background-color: #58a6ff; }"
    );
    ocioDisplayCombo->hide();

    ocioViewLabel = new QLabel("View", this);
    ocioViewLabel->setStyleSheet("QLabel { color: white; font-size: 14px; padding: 0 4px; }");
    ocioViewLabel->hide();

    ocioViewCombo = new QComboBox(this);
    ocioViewCombo->setFocusPolicy(Qt::NoFocus);
    ocioViewCombo->setStyleSheet(
        "QComboBox { background-color: #333; color: white; border: 1px solid #555; padding: 5px; border-radius: 3px; min-width: 160px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: none; border: none; }"
        "QComboBox QAbstractItemView { background-color: #333; color: white; selection-background-color: #58a6ff; }"
    );
    ocioViewCombo->hide();
    
    // Loop mode combo
    loopModeCombo = new QComboBox(this);
    loopModeCombo->setFocusPolicy(Qt::NoFocus);
    loopModeCombo->addItems({"Loop", "Once", "Ping-Pong"});
    loopModeCombo->setCurrentIndex(0);
    loopModeCombo->setStyleSheet(
        "QComboBox { background-color: #333; color: white; border: 1px solid #555; padding: 5px; border-radius: 3px; min-width: 80px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: none; border: none; }"
        "QComboBox QAbstractItemView { background-color: #333; color: white; selection-background-color: #58a6ff; }"
    );
    // Visibility managed by parent playbackControlsGroup
    
    // Playback rate combo
    playbackRateCombo = new QComboBox(this);
    playbackRateCombo->setFocusPolicy(Qt::NoFocus);
    playbackRateCombo->addItems({"0.25x", "0.5x", "1x", "1.5x", "2x", "4x"});
    playbackRateCombo->setCurrentIndex(2); // Default to 1x
    playbackRateCombo->setStyleSheet(
        "QComboBox { background-color: #333; color: white; border: 1px solid #555; padding: 5px; border-radius: 3px; min-width: 60px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: none; border: none; }"
        "QComboBox QAbstractItemView { background-color: #333; color: white; selection-background-color: #58a6ff; }"
    );
    // Visibility managed by parent playbackControlsGroup
    
    // Exposure slider
    exposureLabel = new QLabel("Exp: 0.0", this);
    exposureLabel->setStyleSheet("QLabel { color: white; font-size: 12px; min-width: 55px; }");
    // Visibility managed by parent playbackControlsGroup
    
    exposureSlider = new QSlider(Qt::Horizontal, this);
    exposureSlider->setFocusPolicy(Qt::NoFocus);
    exposureSlider->setRange(-100, 100); // -10.0 to 10.0 with 0.1 steps
    exposureSlider->setValue(0);
    exposureSlider->setFixedWidth(80);
    exposureSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #555; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #fff; border: 1px solid #58a6ff; width: 10px; margin: -4px 0; border-radius: 5px; }"
    );
    // Visibility managed by parent playbackControlsGroup
    
    // Gamma slider
    gammaLabel = new QLabel("γ: 1.0", this);
    gammaLabel->setStyleSheet("QLabel { color: white; font-size: 12px; min-width: 45px; }");
    // Visibility managed by parent playbackControlsGroup
    
    gammaSlider = new QSlider(Qt::Horizontal, this);
    gammaSlider->setFocusPolicy(Qt::NoFocus);
    gammaSlider->setRange(10, 400); // 0.1 to 4.0 with 0.01 steps
    gammaSlider->setValue(100); // 1.0
    gammaSlider->setFixedWidth(80);
    gammaSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #555; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #fff; border: 1px solid #58a6ff; width: 10px; margin: -4px 0; border-radius: 5px; }"
    );
    // Visibility managed by parent playbackControlsGroup
#endif

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
    volumeSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #555; height: 4px; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #58a6ff; height: 4px; border-radius: 2px; }"
        "QSlider::add-page:horizontal { background: #333; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #ffffff; border: 1px solid #58a6ff; width: 12px; margin: -6px 0; border-radius: 6px; }"
    );
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
#ifdef HAVE_TLRENDER
    csLayout->addSpacing(8);
    csLayout->addWidget(ocioEnableCheck);
    csLayout->addWidget(ocioConfigBtn);
    csLayout->addWidget(ocioDisplayLabel);
    csLayout->addWidget(ocioDisplayCombo);
    csLayout->addWidget(ocioViewLabel);
    csLayout->addWidget(ocioViewCombo);
#endif
    // Do not forcibly hide the container; children visibility will control it
    bottomGrid->addWidget(csGroup, 0, 2, Qt::AlignVCenter);

    bottomGrid->addWidget(audioGroup, 0, 3, Qt::AlignRight | Qt::AlignVCenter);

    controlsLayout->addLayout(bottomGrid);

#ifdef HAVE_TLRENDER
    // Playback controls row (Loop, Rate, Exposure, Gamma) - added as separate row in controlsLayout
    playbackControlsGroup = new QWidget(this);
    QHBoxLayout *playbackControlsLayout = new QHBoxLayout(playbackControlsGroup);
    playbackControlsLayout->setContentsMargins(0, 0, 0, 0);
    playbackControlsLayout->setSpacing(8);
    playbackControlsLayout->addStretch();
    playbackControlsLayout->addWidget(loopModeCombo);
    playbackControlsLayout->addWidget(playbackRateCombo);
    playbackControlsLayout->addSpacing(16);
    playbackControlsLayout->addWidget(exposureLabel);
    playbackControlsLayout->addWidget(exposureSlider);
    playbackControlsLayout->addWidget(gammaLabel);
    playbackControlsLayout->addWidget(gammaSlider);
    playbackControlsLayout->addStretch();
    playbackControlsGroup->setFixedHeight(30);
    playbackControlsGroup->hide(); // Hidden by default
    
    // Add directly to controls layout as a new row
    controlsLayout->addWidget(playbackControlsGroup);
    qDebug() << "[setupUi] playbackControlsGroup created and added to layout";
#endif

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

    // Initial positioning
    if (navPrevBtn && navNextBtn) {
        int y = height() / 2 - navPrevBtn->height() / 2;
        navPrevBtn->move(20, y);
        navNextBtn->move(width() - 20 - navNextBtn->width(), y);
        navPrevBtn->show();
        navNextBtn->show();
    }

#ifdef HAVE_TLRENDER
    // Set video widget for tlRender rendering using native viewport
    m_renderWidget = new TLRenderViewport(this);
    m_renderWidget->setPlayer(m_player);
    // Ensure the overlay remains the focus target for keyboard shortcuts.
    m_renderWidget->setFocusPolicy(Qt::NoFocus);
    m_renderWidget->installEventFilter(this);
    // Replace videoWidget with m_renderWidget in the layout
    if (videoWidget && videoWidget->parentWidget()) {
        QLayout* parentLayout = videoWidget->parentWidget()->layout();
        if (parentLayout) {
            parentLayout->replaceWidget(videoWidget, m_renderWidget);
        }
    }
    // Set initial volume
    m_player->setVolume(0.5);

    // Wire OCIO controls
    if (ocioEnableCheck) {
        connect(ocioEnableCheck, &QCheckBox::toggled, this, [this](bool enabled) {
            m_player->setOCIOEnabled(enabled);
            controlsTimer->start();
        });
    }
    if (ocioConfigBtn) {
        connect(ocioConfigBtn, &QPushButton::clicked, this, [this]() {
            const QString filePath = QFileDialog::getOpenFileName(
                this,
                tr("Select OCIO config"),
                QString(),
                tr("OCIO Config (config.ocio);;All Files (*.*)")
            );
            if (!filePath.isEmpty()) {
                m_player->setOCIOConfig(filePath);
                if (ocioEnableCheck) {
                    ocioEnableCheck->setChecked(true);
                }
            }
        });
    }
    if (ocioDisplayCombo) {
        connect(ocioDisplayCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            if (idx < 0) return;
            const QString display = ocioDisplayCombo->itemText(idx);
            m_player->setDisplay(display);

            if (ocioViewCombo) {
                const QStringList views = m_player->availableViews(display);
                ocioViewCombo->blockSignals(true);
                ocioViewCombo->clear();
                ocioViewCombo->addItems(views);
                // Try to keep the player's current view selected
                const int viewIdx = ocioViewCombo->findText(m_player->view());
                if (viewIdx >= 0) {
                    ocioViewCombo->setCurrentIndex(viewIdx);
                }
                ocioViewCombo->blockSignals(false);
            }
            controlsTimer->start();
        });
    }
    if (ocioViewCombo) {
        connect(ocioViewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            if (idx < 0) return;
            const QString view = ocioViewCombo->itemText(idx);
            m_player->setView(view);
            // Save to settings for persistence
            QSettings settings("AugmentCode", "KAssetManager");
            settings.setValue("OCIO/LastView", view);
            settings.setValue("OCIO/LastDisplay", m_player->display());
            controlsTimer->start();
        });
    }
    
    // Loop mode control
    if (loopModeCombo) {
        connect(loopModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            TLRenderPlayer::LoopMode mode = TLRenderPlayer::LoopMode::Loop;
            switch (idx) {
                case 0: mode = TLRenderPlayer::LoopMode::Loop; break;
                case 1: mode = TLRenderPlayer::LoopMode::Once; break;
                case 2: mode = TLRenderPlayer::LoopMode::PingPong; break;
            }
            m_player->setLoopMode(mode);
            controlsTimer->start();
        });
    }
    
    // Playback rate control
    if (playbackRateCombo) {
        connect(playbackRateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
            static const double rates[] = {0.25, 0.5, 1.0, 1.5, 2.0, 4.0};
            if (idx >= 0 && idx < 6) {
                m_player->setPlaybackRate(rates[idx]);
            }
            controlsTimer->start();
        });
    }
    
    // Exposure slider
    if (exposureSlider) {
        connect(exposureSlider, &QSlider::valueChanged, this, [this](int value) {
            float exposure = value / 10.0f; // -10.0 to 10.0
            m_player->setExposure(exposure);
            if (exposureLabel) {
                exposureLabel->setText(QString("Exp: %1").arg(exposure, 0, 'f', 1));
            }
            controlsTimer->start();
        });
    }
    
    // Gamma slider
    if (gammaSlider) {
        connect(gammaSlider, &QSlider::valueChanged, this, [this](int value) {
            float gamma = value / 100.0f; // 0.1 to 4.0
            m_player->setGamma(gamma);
            if (gammaLabel) {
                gammaLabel->setText(QString("γ: %1").arg(gamma, 0, 'f', 2));
            }
            controlsTimer->start();
        });
    }

    // Populate OCIO lists once we have a config loaded
    connect(m_player, &TLRenderPlayer::displaysChanged, this, [this](const QStringList& displays) {
        if (!ocioDisplayCombo) return;
        ocioDisplayCombo->blockSignals(true);
        ocioDisplayCombo->clear();
        ocioDisplayCombo->addItems(displays);
        
        // Try to restore last used display from settings
        QSettings settings("AugmentCode", "KAssetManager");
        const QString lastDisplay = settings.value("OCIO/LastDisplay").toString();
        int idx = -1;
        if (!lastDisplay.isEmpty()) {
            idx = ocioDisplayCombo->findText(lastDisplay);
        }
        if (idx < 0) {
            idx = ocioDisplayCombo->findText(m_player->display());
        }
        if (idx >= 0) {
            ocioDisplayCombo->setCurrentIndex(idx);
        }
        ocioDisplayCombo->blockSignals(false);

            // Ensure the views combo is populated for the selected display.
            if (ocioViewCombo) {
                const QString display = (idx >= 0) ? ocioDisplayCombo->itemText(idx)
                                                   : (ocioDisplayCombo->count() > 0 ? ocioDisplayCombo->itemText(0) : QString());
                const QStringList views = display.isEmpty() ? QStringList() : m_player->availableViews(display);
                ocioViewCombo->blockSignals(true);
                ocioViewCombo->clear();
                ocioViewCombo->addItems(views);
                
                // Restore view from settings or use smart defaults
                const QString lastView = settings.value("OCIO/LastView").toString();
                int viewIdx = -1;
                if (!lastView.isEmpty()) {
                    viewIdx = ocioViewCombo->findText(lastView);
                }
                
                // If no saved view, check for video file defaults
                if (viewIdx < 0 && !currentFilePath.isEmpty()) {
                    const QString ext = QFileInfo(currentFilePath).suffix().toLower();
                    if (ext == "mov" || ext == "mp4" || ext == "m4v" || ext == "avi" || ext == "mkv" || ext == "webm") {
                        for (int i = 0; i < ocioViewCombo->count(); ++i) {
                            const QString viewName = ocioViewCombo->itemText(i).toLower();
                            if (viewName.contains("rec.709") || viewName.contains("rec709")) {
                                viewIdx = i;
                                break;
                            }
                            if (viewIdx < 0 && viewName.contains("srgb")) {
                                viewIdx = i;
                            }
                        }
                    }
                }
                
                if (viewIdx < 0) {
                    viewIdx = ocioViewCombo->findText(m_player->view());
                }
                if (viewIdx >= 0) {
                    ocioViewCombo->setCurrentIndex(viewIdx);
                }
                ocioViewCombo->blockSignals(false);
            }
    });

        connect(m_player, &TLRenderPlayer::viewsChanged, this, [this](const QStringList& views) {
            if (!ocioViewCombo) return;
            ocioViewCombo->blockSignals(true);
            ocioViewCombo->clear();
            ocioViewCombo->addItems(views);
            
            // Try to restore last used view from settings
            QSettings settings("AugmentCode", "KAssetManager");
            const QString lastView = settings.value("OCIO/LastView").toString();
            int viewIdx = -1;
            
            if (!lastView.isEmpty()) {
                viewIdx = ocioViewCombo->findText(lastView);
            }
            
            // If no saved view or saved view not found, check for video file defaults
            if (viewIdx < 0 && !currentFilePath.isEmpty()) {
                const QString ext = QFileInfo(currentFilePath).suffix().toLower();
                // For video files, default to Rec.709 or sRGB view
                if (ext == "mov" || ext == "mp4" || ext == "m4v" || ext == "avi" || ext == "mkv" || ext == "webm") {
                    for (int i = 0; i < ocioViewCombo->count(); ++i) {
                        const QString viewName = ocioViewCombo->itemText(i).toLower();
                        if (viewName.contains("rec.709") || viewName.contains("rec709")) {
                            viewIdx = i;
                            break;
                        }
                        // Fallback to sRGB if no Rec.709
                        if (viewIdx < 0 && viewName.contains("srgb")) {
                            viewIdx = i;
                        }
                    }
                }
            }
            
            // Final fallback: use player's current view or first item
            if (viewIdx < 0) {
                viewIdx = ocioViewCombo->findText(m_player->view());
            }
            if (viewIdx >= 0) {
                ocioViewCombo->setCurrentIndex(viewIdx);
            }
            ocioViewCombo->blockSignals(false);
        });

    // Default to bundled ACES config if it exists in the deployed bin folder
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString acesConfig = QDir(appDir).filePath("OpenColorIO-Config-ACES-1.2/aces_1.2/config.ocio");
        if (QFileInfo::exists(acesConfig)) {
            m_player->setOCIOConfig(acesConfig);
            m_player->setOCIOEnabled(true);
            
            // Restore last used OCIO display/view from settings
            QSettings settings("AugmentCode", "KAssetManager");
            const QString lastDisplay = settings.value("OCIO/LastDisplay").toString();
            const QString lastView = settings.value("OCIO/LastView").toString();
            if (!lastDisplay.isEmpty()) {
                m_player->setDisplay(lastDisplay);
            }
            if (!lastView.isEmpty()) {
                m_player->setView(lastView);
            }
        }
    }
#else
    // Set video widget for GStreamer rendering
    m_gstreamerPlayer->setVideoWidget(videoWidget);
    // Set initial volume
    m_gstreamerPlayer->setVolume(0.5);
#endif;
    
    // Initialize annotation system
    annotationLayer = new AnnotationLayer(imageScene, this);
    annotationLayer->setParentWidget(this); // Set parent for dialogs
    
    // Connect annotation layer signals for immediate save
    connect(annotationLayer, &AnnotationLayer::annotationAdded, this, [this](AnnotationItem* item) {
        Q_UNUSED(item);
        qDebug() << "[PreviewOverlay] Annotation added signal received - saving frame immediately";
        saveCurrentFrameAnnotations();
    });
    
    setupAnnotationToolbar();
}

void PreviewOverlay::showAsset(const QString &filePath, const QString &fileName, const QString &fileType)
{
    // First, stop any ongoing playback (video, fallback, or sequence)
    stopPlayback();
    detectedFps = 0.0;
    if (positionSlider) {
        positionSlider->clearCachedFrames();
    }

    // Reset sequence state
    isSequence = false;
    sequencePlaying = false;
    sequencePlayDirection = 1; // Default forward
    if (sequenceTimer->isActive()) {
        sequenceTimer->stop();
    }

#ifdef HAVE_TLRENDER
    // Default to hiding OCIO controls; showVideo()/showSequence(tlRender path) will enable them.
    if (ocioEnableCheck) ocioEnableCheck->hide();
    if (ocioConfigBtn) ocioConfigBtn->hide();
    if (ocioDisplayLabel) ocioDisplayLabel->hide();
    if (ocioDisplayCombo) ocioDisplayCombo->hide();
    if (ocioViewLabel) ocioViewLabel->hide();
    if (ocioViewCombo) ocioViewCombo->hide();
#endif

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
    setWindowTitle(tr("Preview - %1").arg(fileName));
    if (closeBtn) closeBtn->show();

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
        if (stillImageView) stillImageView->hide();
        if (imageView) imageView->show();
        imageScene->clear();
        imageScene->addText("Preview not available", QFont("Segoe UI", 14));
        setControlsVisible(false);
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
        if (stillImageView) stillImageView->hide();
        if (imageView) imageView->show();
        imageScene->clear();
        svgItem = new QGraphicsSvgItem(filePath);
        imageScene->addItem(svgItem);
        imageScene->setSceneRect(svgItem->boundingRect());
        fitImageToView();
        setControlsVisible(false);
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
    if (closeBtn) closeBtn->hide();
    if (videoWidget) videoWidget->hide();
    if (textView) textView->hide();
    if (tableView) tableView->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    if (imageView) imageView->hide();
    if (stillImageView) stillImageView->hide();
    if (stillImageView) {
        stillImageView->setBackgroundBrush(QColor("#0a0a0a"));
        stillImageView->show();
    }

    // Hide navigation arrows for still images
    navContainer = nullptr;
    if (navPrevBtn) navPrevBtn->hide();
    if (navNextBtn) navNextBtn->hide();

    // CRITICAL: Stop any video playback
    PLAYER_STOP();

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
        stillFitMode = true;
        fitStillImageToView();

        // CRITICAL: Force complete view refresh
        if (stillImageView) {
            stillImageView->viewport()->update();
            stillImageView->update();
        }
        imageScene->update();

    // No controls for still images
    setPlaybackControlsVisible(false);
    if (colorSpaceLabel) colorSpaceLabel->hide();
    if (colorSpaceCombo) colorSpaceCombo->hide();
    setControlsVisible(false);
    } else {
        qWarning() << "[PreviewOverlay::showImage] Failed to load image:" << filePath;
    }
    // Cache bar is only for sequences
    if (cacheBar) cacheBar->hide();
}

void PreviewOverlay::showVideo(const QString &filePath)
{
    // Stop any existing playback
    PLAYER_STOP();

    // Hide other content and show video widget
    if (textView) textView->hide();
    if (tableView) tableView->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    if (imageView) imageView->hide();

    videoWidget->show();

#ifdef HAVE_TLRENDER
    // CRITICAL: Set render widget AFTER show() to ensure valid window handle
    if (!m_renderWidget) {
        m_renderWidget = new TLRenderViewport(this);
        m_renderWidget->setPlayer(m_player);
    }
    m_renderWidget->setGeometry(videoWidget->geometry());
    m_renderWidget->show();
    videoWidget->hide(); // Hide the placeholder widget
#else
    // CRITICAL: Set video widget AFTER show() to ensure valid window handle
    // GStreamer needs the native window handle (WId) which is only available after the widget is shown
    m_gstreamerPlayer->setVideoWidget(videoWidget);
#endif

    // Anchor nav arrows to the video widget
    positionNavButtons(videoWidget);
    setPlaybackControlsVisible(true);
    setControlsHeightForImage(false);
    setControlsVisible(true);

    // Cache bar is only for sequences
    if (cacheBar) cacheBar->hide();

    // Enable and show audio controls for video
    if (muteBtn) {
        muteBtn->setEnabled(true);
#ifdef HAVE_TLRENDER
        muteBtn->setIcon(m_player->isMuted() ? muteIcon : audioIcon);
#else
        muteBtn->setIcon(m_gstreamerPlayer->isMuted() ? muteIcon : audioIcon);
#endif
        muteBtn->show();
    }
    if (volumeSlider) {
        volumeSlider->setEnabled(true);
        volumeSlider->show();
    }

    // Hide alpha toggle for videos
    if (alphaCheck) alphaCheck->hide();

    // Hide legacy per-image tone-map colorspace selector for videos (OCIO handles color)
    if (colorSpaceLabel) colorSpaceLabel->hide();
    if (colorSpaceCombo) colorSpaceCombo->hide();

#ifdef HAVE_TLRENDER
    // Show OCIO controls for tlRender playback
    if (ocioEnableCheck) ocioEnableCheck->show();
    if (ocioConfigBtn) ocioConfigBtn->show();
    if (ocioDisplayLabel) ocioDisplayLabel->show();
    if (ocioDisplayCombo) ocioDisplayCombo->show();
    if (ocioViewLabel) ocioViewLabel->show();
    if (ocioViewCombo) ocioViewCombo->show();

    // Show playback control widgets row
    qDebug() << "[showVideo] Playback controls check:"
             << "playbackControlsGroup:" << (playbackControlsGroup != nullptr)
             << "loopModeCombo:" << (loopModeCombo != nullptr)
             << "exposureSlider:" << (exposureSlider != nullptr);
    if (playbackControlsGroup) { 
        playbackControlsGroup->show();
        playbackControlsGroup->setVisible(true);
        qDebug() << "[showVideo] playbackControlsGroup isVisible:" << playbackControlsGroup->isVisible()
                 << "size:" << playbackControlsGroup->size()
                 << "parent:" << playbackControlsGroup->parentWidget();
    }
#endif

    originalPixmap = QPixmap(); // Clear the pixmap
    fitPending = true;

    // Default timeline context while media info is loading
    positionSlider->setTimelineContext(true, 24.0);

#ifdef HAVE_TLRENDER
    // Load and play video with tlRender
    m_player->load(filePath);
#else
    // Load and play video with GStreamer
    m_gstreamerPlayer->loadMedia(filePath);
#endif
    PLAYER_PLAY();

    controlsTimer->start();
}

void PreviewOverlay::onPlayPauseClicked()
{
    qDebug() << "[PreviewOverlay] onPlayPauseClicked: isSequence=" << isSequence << "isVideo=" << isVideo;
    
    if (isSequence) {
        // Handle sequence playback
        if (sequencePlaying) {
            pauseSequence();
        } else {
            playSequence();
        }
    } else {
        // Handle video/tlRender playback
        auto state = PLAYER_STATE();
        qDebug() << "[PreviewOverlay] onPlayPauseClicked: current state=" << static_cast<int>(state);
        if (state == PLAYER_STATE_PLAYING) {
            // Capture position before pausing
            lastKnownPosition = PLAYER_POSITION();
            qDebug() << "[PreviewOverlay] Pausing at position" << lastKnownPosition;
            PLAYER_PAUSE();
            // Update timecode immediately
            qint64 durationMs = PLAYER_DURATION();
            updateVideoTimeDisplays(lastKnownPosition, durationMs);
        } else {
            qDebug() << "[PreviewOverlay] Starting playback";
            PLAYER_PLAY();
        }
    }
    controlsTimer->start();
}

void PreviewOverlay::onSliderMoved(int position)
{
    if (isSequence) {
        // Seek to specific frame in sequence
        loadSequenceFrame(position);
        controlsTimer->start();
        return;
    }

    // GStreamer path - always do live scrubbing
    PLAYER_SEEK(position);
    
    // Update timecode immediately during scrubbing
    lastKnownPosition = position;
    qint64 durationMs = PLAYER_DURATION();
    updateVideoTimeDisplays(position, durationMs);
    
    // Don't recapture frame during drag if in annotation mode (too slow)
    // Will update on release
    
    controlsTimer->start();
}

void PreviewOverlay::onVolumeChanged(int value)
{
#ifdef HAVE_TLRENDER
    m_player->setVolume(value / 100.0);
#else
    m_gstreamerPlayer->setVolume(value / 100.0);
#endif
    controlsTimer->start();
}

void PreviewOverlay::onToggleMute()
{
#ifdef HAVE_TLRENDER
    const bool newMuted = !m_player->isMuted();
    m_player->setMuted(newMuted);
#else
    const bool newMuted = !m_gstreamerPlayer->isMuted();
    m_gstreamerPlayer->setMuted(newMuted);
#endif
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

    wasPlayingBeforeSeek = (PLAYER_STATE() == PLAYER_STATE_PLAYING);
    // Capture position before pausing for seek
    lastKnownPosition = PLAYER_POSITION();
    PLAYER_PAUSE();
    // Update timecode immediately
    qint64 durationMs = PLAYER_DURATION();
    updateVideoTimeDisplays(lastKnownPosition, durationMs);
}

void PreviewOverlay::onSliderReleased()
{
    const int pos = positionSlider->value();
    if (isSequence) {
        loadSequenceFrame(pos);
        if (wasPlayingBeforeSeek) playSequence();
        userSeeking = false;
        controlsTimer->start();
        return;
    }

    PLAYER_SEEK(pos);
    
    // Calculate and explicitly track the frame number we seeked to
    double fps = detectedFps > 0.0 ? detectedFps : 24.0;
    lastKnownVideoFrame = static_cast<int>(qRound((pos / 1000.0) * fps));
    qDebug() << "[PreviewOverlay] Seek to position" << pos << "ms - explicitly set frame to:" << lastKnownVideoFrame;
    
    // Update lastKnownPosition immediately for timecode display
    lastKnownPosition = pos;
    qint64 durationMs = PLAYER_DURATION();
    updateVideoTimeDisplays(pos, durationMs);
    
    // If in annotation mode for video, update frame immediately
    if (annotationModeEnabled && isVideo) {
        updateVideoAnnotationFrame();
    } else if (wasPlayingBeforeSeek) {
        PLAYER_PLAY();
    }
    
    userSeeking = false;
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

    // Player path - professional frame stepping
#ifdef HAVE_TLRENDER
    m_player->stepForward();
#else
    m_gstreamerPlayer->stepForward();
#endif
    
    // Explicitly track frame number for frame-accurate annotations
    if (lastKnownVideoFrame >= 0) {
        lastKnownVideoFrame++;
    } else {
        // Initialize from current position
        double fps = detectedFps > 0.0 ? detectedFps : 24.0;
        lastKnownVideoFrame = static_cast<int>(qRound((PLAYER_POSITION() / 1000.0) * fps)) + 1;
    }
    qDebug() << "[PreviewOverlay] Step forward - explicitly set frame to:" << lastKnownVideoFrame;
    
    // Wait briefly for player to update position, then update timecode
    QTimer::singleShot(50, this, [this]() {
        lastKnownPosition = PLAYER_POSITION();
        qint64 durationMs = PLAYER_DURATION();
        updateVideoTimeDisplays(lastKnownPosition, durationMs);
    });
    
    // If in annotation mode for video, update frame immediately
    if (annotationModeEnabled && isVideo) {
        updateVideoAnnotationFrame();
    }
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

    // Player path - professional frame stepping
#ifdef HAVE_TLRENDER
    m_player->stepBackward();
#else
    m_gstreamerPlayer->stepBackward();
#endif
    
    // Explicitly track frame number for frame-accurate annotations
    if (lastKnownVideoFrame >= 0) {
        lastKnownVideoFrame = qMax(0, lastKnownVideoFrame - 1);
    } else {
        // Initialize from current position
        double fps = detectedFps > 0.0 ? detectedFps : 24.0;
        lastKnownVideoFrame = qMax(0, static_cast<int>(qRound((PLAYER_POSITION() / 1000.0) * fps)) - 1);
    }
    qDebug() << "[PreviewOverlay] Step backward - explicitly set frame to:" << lastKnownVideoFrame;
    
    // Wait briefly for GStreamer to update position, then update timecode
    QTimer::singleShot(50, this, [this]() {
        lastKnownPosition = PLAYER_POSITION();
        qint64 durationMs = PLAYER_DURATION();
        updateVideoTimeDisplays(lastKnownPosition, durationMs);
    });
    
    // If in annotation mode for video, update frame immediately
    if (annotationModeEnabled && isVideo) {
        updateVideoAnnotationFrame();
    }
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
    
    // FPS is now provided by GStreamer media info callback
    // No need to query metadata here
    if (detectedFps <= 0.0) detectedFps = 24.0;
}

void PreviewOverlay::setPlaybackControlsVisible(bool visible)
{
    if (positionSlider) positionSlider->setVisible(visible);
    if (currentTimeLabel) currentTimeLabel->setVisible(visible);
    if (durationTimeLabel) durationTimeLabel->setVisible(visible);
    if (fpsLabel) fpsLabel->setVisible(visible);
    if (prevFrameBtn) prevFrameBtn->setVisible(visible);
    if (playPauseBtn) playPauseBtn->setVisible(visible);
    if (nextFrameBtn) nextFrameBtn->setVisible(visible);
    if (muteBtn) muteBtn->setVisible(visible);
    if (volumeSlider) volumeSlider->setVisible(visible);
#ifdef HAVE_TLRENDER
    if (playbackControlsGroup) playbackControlsGroup->setVisible(visible);
#endif
}

void PreviewOverlay::setControlsHeightForImage(bool imageMode)
{
    if (!controlsWidget) return;
    if (imageMode) {
        controlsWidget->setMinimumHeight(kImageControlsMinHeight);
        controlsWidget->setMaximumHeight(kImageControlsMaxHeight);
        if (controlsLayout) {
            controlsLayout->setContentsMargins(kImageControlsSideMargin, kImageControlsTopMargin,
                                               kImageControlsSideMargin, kImageControlsBottomMargin);
            controlsLayout->setSpacing(kImageControlsSpacing);
        }
    } else {
        controlsWidget->setMinimumHeight(kControlsMinHeight);
        controlsWidget->setMaximumHeight(kControlsMaxHeight);
        if (controlsLayout) {
            controlsLayout->setContentsMargins(kControlsSideMargin, kControlsTopMargin,
                                               kControlsSideMargin, kControlsBottomMargin);
            controlsLayout->setSpacing(kControlsSpacing);
        }
    }
    controlsWidget->updateGeometry();
}

void PreviewOverlay::setControlsVisible(bool visible)
{
    if (!controlsWidget) return;
    const bool allow = isVideo || isSequence;
    if (visible && allow) {
        controlsWidget->show();
    } else {
        controlsWidget->hide();
        controlsWidget->setMinimumHeight(0);
        controlsWidget->setMaximumHeight(0);
    }
    controlsWidget->updateGeometry();
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
    if (isSequence) {
        playPauseBtn->setIcon(sequencePlaying ? pauseIcon : playIcon);
    } else {
        playPauseBtn->setIcon(PLAYER_STATE() == PLAYER_STATE_PLAYING ? pauseIcon : playIcon);
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

        // JKL scrubbing (NLE-style playback control)
        case Qt::Key_J:
            // J = Play backward
            if (isSequence) {
                sequencePlayDirection = -1;
                if (!sequencePlaying) {
                    playSequence();
                }
            } else if (isVideo) {
#ifdef HAVE_TLRENDER
                m_player->setPlaybackRate(-1.0);
#else
                m_gstreamerPlayer->setPlaybackRate(-1.0);
#endif
                PLAYER_PLAY();
            }
            return;
        case Qt::Key_K:
            // K = Pause/Stop
            if (isSequence) {
                if (sequencePlaying) {
                    pauseSequence();
                }
            } else if (isVideo) {
                PLAYER_PAUSE();
            }
            return;
        case Qt::Key_L:
            // L = Play forward
            if (isSequence) {
                sequencePlayDirection = 1;
                if (!sequencePlaying) {
                    playSequence();
                }
            } else if (isVideo) {
                // If already playing forward, just ensure rate is 1.0
#ifdef HAVE_TLRENDER
                if (PLAYER_STATE() == TLRenderPlayer::PlaybackState::Playing &&
                    m_player->playbackRate() > 0) {
                    // Already playing forward, do nothing or could increase speed
                } else {
                    m_player->setPlaybackRate(1.0);
                    PLAYER_PLAY();
                }
#else
                if (PLAYER_STATE() == PLAYER_STATE_PLAYING &&
                    m_gstreamerPlayer->playbackRate() > 0) {
                    // Already playing forward, do nothing or could increase speed
                } else {
                    m_gstreamerPlayer->setPlaybackRate(1.0);
                    PLAYER_PLAY();
                }
#endif
            }
            return;

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
        if (stillImageView && stillImageView->isVisible() && stillFitMode) {
            fitStillImageToView();
        } else if (imageView && imageView->isVisible()) {
            fitImageToView();
        }
    }

    // Update video render rectangle if showing a video
    if (isVideo && videoWidget && videoWidget->isVisible()) {
#ifdef HAVE_TLRENDER
        if (m_renderWidget) m_renderWidget->update();
#else
        m_gstreamerPlayer->updateRenderRectangle();
#endif
        
        // Update annotation overlay geometry if enabled
        if (annotationModeEnabled && annotationOverlayView && annotationOverlayView->isVisible()) {
            QPoint overlayPos = videoWidget->mapTo(this, QPoint(0, 0));
            annotationOverlayView->setGeometry(QRect(overlayPos, videoWidget->size()));
            annotationOverlayScene->setSceneRect(0, 0, videoWidget->width(), videoWidget->height());
        }
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
        if (stillImageView && stillImageView->isVisible()) {
            resetStillImageZoom();
        } else {
            resetImageZoom();
        }
        event->accept();
        return;
    }

    if (isVideo) {
        // Show controls on click
        setControlsVisible(true);
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
    // Enable zoom for images and sequences (fallback if event reached overlay)
    if (stillImageView && stillImageView->isVisible() && !originalPixmap.isNull()) {
        double factor = event->angleDelta().y() > 0 ? 1.15 : 0.85;
        zoomStillImage(factor);
        event->accept();
        return;
    }
    if (!originalPixmap.isNull() || isSequence) {
        double factor = event->angleDelta().y() > 0 ? 1.15 : 0.85;
        zoomImage(factor);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

bool PreviewOverlay::eventFilter(QObject* watched, QEvent* event)
{
    // Handle annotation mode mouse events on the scene
    if (annotationModeEnabled && (watched == imageView || (imageView && watched == imageView->viewport()) ||
                                  watched == stillImageView || (stillImageView && watched == stillImageView->viewport()) ||
                                  watched == annotationOverlayView || (annotationOverlayView && watched == annotationOverlayView->viewport()))) {
        // In Select mode, let the scene handle events for item selection/movement
        if (annotationLayer->currentMode() == AnnotationLayer::Select) {
            return false; // Let scene handle it
        }
        
        if (event->type() == QEvent::MouseButtonPress || 
            event->type() == QEvent::MouseMove || 
            event->type() == QEvent::MouseButtonRelease) {
            
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            
            // Only handle left button for annotations
            if (event->type() == QEvent::MouseButtonPress && mouseEvent->button() != Qt::LeftButton) {
                return false; // Let other buttons through
            }
            
            // Only handle move events if left button is pressed
            if (event->type() == QEvent::MouseMove && !(mouseEvent->buttons() & Qt::LeftButton)) {
                return false; // Not drawing
            }
            
            // Convert viewport coordinates to scene coordinates
            QGraphicsView* activeView = nullptr;
            if (watched == annotationOverlayView || watched == annotationOverlayView->viewport()) {
                activeView = annotationOverlayView;
            } else if (watched == stillImageView || (stillImageView && watched == stillImageView->viewport())) {
                activeView = stillImageView;
            } else {
                activeView = imageView;
            }
            QPointF scenePos = activeView->mapToScene(mouseEvent->pos());
            
            // Create a scene mouse event
            QGraphicsSceneMouseEvent sceneMouseEvent(
                event->type() == QEvent::MouseButtonPress ? QEvent::GraphicsSceneMousePress :
                event->type() == QEvent::MouseMove ? QEvent::GraphicsSceneMouseMove :
                QEvent::GraphicsSceneMouseRelease
            );
            sceneMouseEvent.setScenePos(scenePos);
            sceneMouseEvent.setButton(mouseEvent->button());
            sceneMouseEvent.setButtons(mouseEvent->buttons());
            sceneMouseEvent.setModifiers(mouseEvent->modifiers());
            
            // Forward to annotation layer
            if (event->type() == QEvent::MouseButtonPress) {
                annotationLayer->handleMousePress(&sceneMouseEvent);
            } else if (event->type() == QEvent::MouseMove) {
                annotationLayer->handleMouseMove(&sceneMouseEvent);
            } else if (event->type() == QEvent::MouseButtonRelease) {
                annotationLayer->handleMouseRelease(&sceneMouseEvent);
            }
            
            return true; // Consume the event
        }
    }
    
    // Handle wheel events for image/sequence zoom
    if ((watched == stillImageView || (stillImageView && watched == stillImageView->viewport())) &&
        event->type() == QEvent::Wheel) {
        if (!originalPixmap.isNull()) {
            QWheelEvent* wheel = static_cast<QWheelEvent*>(event);
            double factor = wheel->angleDelta().y() > 0 ? 1.15 : 0.85;
            zoomStillImage(factor);
            wheel->accept();
            return true;
        }
    }

    if ((watched == imageView || (imageView && watched == imageView->viewport())) && event->type() == QEvent::Wheel) {
        // Enable zoom for images (when pixmap is loaded) or sequences
        if (!originalPixmap.isNull() || isSequence) {
            QWheelEvent* wheel = static_cast<QWheelEvent*>(event);
            double factor = wheel->angleDelta().y() > 0 ? 1.15 : 0.85;
            zoomImage(factor);
            wheel->accept();
            return true; // consume to prevent any scrolling
        }
    }

    if ((watched == stillImageView || (stillImageView && watched == stillImageView->viewport())) &&
        event->type() == QEvent::MouseButtonDblClick) {
        toggleStillImageFit();
        return true;
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
            keyEvent->key() == Qt::Key_Comma ||
            keyEvent->key() == Qt::Key_J ||
            keyEvent->key() == Qt::Key_K ||
            keyEvent->key() == Qt::Key_L) {
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
    // Use the viewport's visible rect for proper HighDPI support
    QRectF viewRect = imageView->viewport()->rect();
    QRectF sceneRect = imageScene->sceneRect();

    // Guard against empty rects
    if (viewRect.isEmpty() || sceneRect.isEmpty()) return;

    double xRatio = viewRect.width() / sceneRect.width();
    double yRatio = viewRect.height() / sceneRect.height();

    currentZoom = qMin(xRatio, yRatio);

    // Apply transform - fitInView handles HighDPI automatically
    imageView->resetTransform();
    imageView->scale(currentZoom, currentZoom);
    imageView->centerOn(imageItem);

    // Ensure the view updates properly after transform
    imageView->viewport()->update();
}

void PreviewOverlay::resetImageZoom()
{
    // Reset zoom for images and sequences
    if (imageItem) {
        // For images and sequences, reset to 1:1
        currentZoom = 1.0;
        imageView->resetTransform();
        imageView->centerOn(imageItem);
    }
}

void PreviewOverlay::toggleStillImageFit()
{
    if (!stillImageView || !imageItem || originalPixmap.isNull()) return;
    if (stillFitMode) {
        resetStillImageZoom();
        stillFitMode = false;
    } else {
        stillFitMode = true;
        fitStillImageToView();
    }
}

void PreviewOverlay::fitStillImageToView()
{
    if (!stillImageView || !imageItem) return;

    QRectF sceneRect = imageItem->boundingRect();
    if (sceneRect.isEmpty()) return;

    QRectF viewRect = stillImageView->viewport()->rect();
    if (viewRect.isEmpty()) return;

    double xRatio = viewRect.width() / sceneRect.width();
    double yRatio = viewRect.height() / sceneRect.height();

    // Fill the view to eliminate letterboxing for still images.
    stillImageZoom = qMax(xRatio, yRatio);

    stillImageView->resetTransform();
    stillImageView->scale(stillImageZoom, stillImageZoom);
    stillImageView->centerOn(imageItem);
    stillImageView->viewport()->update();
}

void PreviewOverlay::resetStillImageZoom()
{
    if (!stillImageView || !imageItem) return;
    stillImageZoom = 1.0;
    stillImageView->resetTransform();
    stillImageView->centerOn(imageItem);
    stillImageView->viewport()->update();
}

void PreviewOverlay::zoomStillImage(double factor)
{
    if (!stillImageView || !imageItem) return;
    stillFitMode = false;
    stillImageZoom *= factor;
    stillImageView->resetTransform();
    stillImageView->scale(stillImageZoom, stillImageZoom);
    stillImageView->centerOn(imageItem);
    stillImageView->viewport()->update();
}

void PreviewOverlay::showSequence(const QStringList &framePaths, const QString &sequenceName, int startFrame, int endFrame)
{
#ifdef HAVE_TLRENDER
    // tlRender-native sequence playback.
    // We intentionally do not use the legacy RAM cache / per-frame OIIO path here.
    // This fixes slow/blank sequence preview when OIIO is compiled out and enables
    // proper scrubbing/playback via the tlRender engine.

    if (framePaths.isEmpty()) {
        qWarning() << "[PreviewOverlay] showSequence called with empty frame list";
        return;
    }

    // Calculate duration from known frame range (we already know start/end from sequence grouping)
    const int totalFrames = endFrame - startFrame + 1;
    const double defaultFps = 24.0;
    const qint64 knownDurationMs = static_cast<qint64>((totalFrames / defaultFps) * 1000.0);
    qDebug() << "[PreviewOverlay] Sequence range:" << startFrame << "-" << endFrame 
             << "totalFrames:" << totalFrames << "durationMs:" << knownDurationMs;

    // Treat sequences as video-like for timeline/scrubbing/controls.
    isSequence = false;
    isVideo = true;
    sequencePlaying = false;

    // Make sure widget is shown and sized before loading content
    show();
    raise();
    setFocus();

    // Anchor nav arrows to the render widget for sequences
    if (m_renderWidget) {
        positionNavButtons(m_renderWidget);
    }

    // Ensure image view is not used for tlRender sequences
    imageView->hide();
    if (videoWidget) videoWidget->hide();
    if (m_renderWidget) m_renderWidget->show();

    setPlaybackControlsVisible(true);
    setControlsHeightForImage(false);
    setControlsVisible(true);

    // Hide legacy per-image tone-map colorspace controls; OCIO handles color
    if (colorSpaceLabel) colorSpaceLabel->hide();
    if (colorSpaceCombo) colorSpaceCombo->hide();

    // Show OCIO controls for tlRender playback (sequences are treated like video here).
    if (ocioEnableCheck) ocioEnableCheck->show();
    if (ocioConfigBtn) ocioConfigBtn->show();
    if (ocioDisplayLabel) ocioDisplayLabel->show();
    if (ocioDisplayCombo) ocioDisplayCombo->show();
    if (ocioViewLabel) ocioViewLabel->show();
    if (ocioViewCombo) ocioViewCombo->show();

    // Show playback control widgets row
    if (playbackControlsGroup) {
        playbackControlsGroup->show();
    }

    // Stop any existing playback and load the sequence pattern
    PLAYER_STOP();

    fileNameLabel->setText(sequenceName);
    setWindowTitle(tr("Preview - %1").arg(sequenceName));

    const QString patternPath = SequenceDetector::toHashPatternPath(framePaths.first());
    qDebug() << "[PreviewOverlay] tlRender sequence load:" << framePaths.first() << "->" << patternPath;

    // Set slider range immediately using known frame range (don't wait for tlRender)
    positionSlider->setRange(0, static_cast<int>(knownDurationMs));
    positionSlider->setValue(0);
    positionSlider->setTimelineContext(true, defaultFps);
    updateVideoTimeDisplays(0, knownDurationMs);
    detectedFps = defaultFps;
    if (fpsLabel) fpsLabel->setText(QString::number(defaultFps, 'f', 1) + " fps");

    m_player->load(patternPath);
    // Do not auto-play sequences; keep same UX as legacy sequence preview.
    m_player->pause();
    updatePlayPauseButton();

    controlsTimer->start();
    return;
#endif

    isSequence = true;
    isVideo = false;
    sequenceFramePaths = framePaths;
    sequenceStartFrame = startFrame;
    sequenceEndFrame = endFrame;
    currentSequenceFrame = 0;
    sequencePlaying = false;
    sequencePlayDirection = 1; // Default forward

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
    if (stillImageView) stillImageView->hide();
    positionNavButtons(imageView->viewport());

    // Process events to ensure window is properly sized

    // CRITICAL: Clear scene before loading sequence
    imageScene->clear();
    imageItem = nullptr;

    // Show image view and controls
    videoWidget->hide();
    imageView->show();
    setPlaybackControlsVisible(true);
    setControlsHeightForImage(false);
    setControlsVisible(true);
    // Disable audio controls for image sequences (no audio)
    if (muteBtn) {
        muteBtn->setEnabled(false);
        muteBtn->setIcon(noAudioIcon);
        muteBtn->show();
    }
    fitPending = true; // ensure first loaded frame fits once
    if (volumeSlider) {
        volumeSlider->setEnabled(false);
        volumeSlider->show();
    }


    // Stop video player if running
    PLAYER_STOP();

    // Update file name label and window title
    fileNameLabel->setText(sequenceName);
    setWindowTitle(tr("Preview - %1").arg(sequenceName));

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
            if (!positionSlider) return;
            if (!cacheBarUpdateTimer.isValid() || cacheBarUpdateTimer.elapsed() >= 16) {
                positionSlider->markFrameCached(frameIndex);
                cacheBarUpdateTimer.restart();
            }
        });

        // Snapshots replace stale marks when window slides or evictions happen
        connect(frameCache, &SequenceFrameCache::cacheSnapshot, this, [this](const QSet<int>& frames){
            if (!positionSlider) return;
            positionSlider->setCachedFrames(frames);
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
    positionSlider->clearCachedFrames();
            imageScene->setSceneRect(originalPixmap.rect());
            fitImageToView();
        }
    }

    // Update slider for sequence
    positionSlider->setRange(0, sequenceFramePaths.size() - 1);
    positionSlider->setValue(0);
    positionSlider->setTimelineContext(false, 24.0);
    
    // Clear annotation markers when loading new sequence
    positionSlider->clearAnnotatedFrames();

    // Update time labels
    updateSequenceTimeDisplays(0);

    // Update play/pause button
    updatePlayPauseButton();

    // Show controls
    setControlsVisible(true);
    controlsTimer->start();
}

void PreviewOverlay::loadSequenceFrame(int frameIndex)
{
    if (frameIndex < 0 || frameIndex >= sequenceFramePaths.size()) {
        qWarning() << "[PreviewOverlay::loadSequenceFrame] Invalid frame index:" << frameIndex;
        return;
    }
    
    // Save annotations for previous frame before loading new one
    if (currentAnnotatedFrame >= 0 && currentAnnotatedFrame != frameIndex) {
        saveCurrentFrameAnnotations();
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
        // Preserve annotation items when updating frame
        QList<QGraphicsItem*> annotationItems;
        if (annotationLayer) {
            for (AnnotationItem* item : annotationLayer->annotations()) {
                annotationItems.append(item);
            }
        }
        
        if (!imageItem) {
            imageItem = imageScene->addPixmap(originalPixmap);
        } else {
            imageItem->setPixmap(originalPixmap);
        }
        
        // Re-add annotation items to scene after pixmap update
        for (QGraphicsItem* item : annotationItems) {
            if (!item->scene()) {
                imageScene->addItem(item);
            }
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
    
    // Load annotations for the new frame
    if (annotationModeEnabled) {
        currentAnnotatedFrame = frameIndex;
        loadFrameAnnotations(frameIndex);
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

    // Advance frame based on play direction (for JKL scrubbing)
    currentSequenceFrame += sequencePlayDirection;

    // Handle looping
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
    } else if (currentSequenceFrame < 0) {
        // Loop back to end when playing in reverse
        currentSequenceFrame = sequenceFramePaths.size() - 1;
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
            // Clear cache visualization on the timeline and restart prefetch for accurate redraw
            if (positionSlider) {
                positionSlider->clearCachedFrames();
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

    // Stop GStreamer video playback
    PLAYER_STOP();

    // Stop sequence playback
    if (sequencePlaying) {
        pauseSequence();
    }

    // Stop pre-fetching but keep cache intact (only if cache is enabled)
    if (frameCache && useCacheForSequences) {
        frameCache->stopPrefetch();
    }
}



// ============================================================================
// Legacy Signal Handlers (for compatibility)
// ============================================================================

void PreviewOverlay::onPositionChanged(qint64 position)
{
#ifdef HAVE_TLRENDER
    onPlayerPositionChanged(position);
#else
    onGStreamerPositionChanged(position);
#endif
}

void PreviewOverlay::onDurationChanged(qint64 duration)
{
#ifdef HAVE_TLRENDER
    onPlayerDurationChanged(duration);
#else
    onGStreamerDurationChanged(duration);
#endif
}

#ifdef HAVE_TLRENDER
// ============================================================================
// TLRenderPlayer Signal Handlers
// ============================================================================

void PreviewOverlay::onPlayerPositionChanged(qint64 positionMs)
{
    qDebug() << "[PreviewOverlay] onPlayerPositionChanged:"
             << "positionMs=" << positionMs
             << "slider range=[" << positionSlider->minimum() << "," << positionSlider->maximum() << "]"
             << "sliderDown=" << positionSlider->isSliderDown();
             
    if (!positionSlider->isSliderDown()) {
        positionSlider->setValue(static_cast<int>(positionMs));
        qDebug() << "[PreviewOverlay] slider setValue result:" << positionSlider->value();
    }

    qint64 durationMs = m_player->duration();
    updateVideoTimeDisplays(positionMs, durationMs);
    
    // Store last known position for display during pause
    lastKnownPosition = positionMs;
    
    // When playing back, clear the explicit frame tracking so we calculate from position
    if (m_player->playbackState() == TLRenderPlayer::PlaybackState::Playing) {
        lastKnownVideoFrame = -1;
    }
}

void PreviewOverlay::onPlayerDurationChanged(qint64 durationMs)
{
    // Only update if tlRender reports a valid duration AND it's larger than what we have
    // (we may have pre-set the range from known sequence metadata)
    if (durationMs > 0 && durationMs > positionSlider->maximum()) {
        positionSlider->setRange(0, static_cast<int>(durationMs));
    }
}

void PreviewOverlay::onPlayerMediaInfo(const TLRenderPlayer::MediaInfo& info)
{
    qDebug() << "[PreviewOverlay] tlRender media info:"
             << "Duration:" << info.durationMs << "ms"
             << "TotalFrames:" << info.totalFrames
             << "FPS:" << info.fps
             << "Resolution:" << info.width << "x" << info.height
             << "Has Audio:" << info.hasAudio;

    // Only update slider if tlRender reports a valid duration larger than what we have
    // (we may have pre-set the range from known sequence metadata)
    if (info.durationMs > 0 && info.durationMs > positionSlider->maximum()) {
        qDebug() << "[PreviewOverlay] Updating slider range to [0," << info.durationMs << "]";
        positionSlider->setRange(0, info.durationMs);
    } else if (info.durationMs <= 0) {
        qDebug() << "[PreviewOverlay] tlRender reported invalid duration, keeping current range [0," << positionSlider->maximum() << "]";
    }

    if (info.fps > 0.0) {
        detectedFps = info.fps;
        if (fpsLabel) fpsLabel->setText(QString::number(info.fps, 'f', 1) + " fps");
    }

    // Enable/disable audio controls based on actual media
    if (muteBtn) {
        muteBtn->setEnabled(info.hasAudio);
        muteBtn->setIcon(info.hasAudio ? audioIcon : noAudioIcon);
        muteBtn->show();
    }
    if (volumeSlider) {
        volumeSlider->setEnabled(info.hasAudio);
        volumeSlider->show();
    }

    positionSlider->setTimelineContext(true, detectedFps > 0.0 ? detectedFps : info.fps);
}

void PreviewOverlay::onPlayerPlaybackStateChanged(TLRenderPlayer::PlaybackState state)
{
    qDebug() << "[PreviewOverlay] tlRender state changed to:" << static_cast<int>(state);

    switch (state) {
        case TLRenderPlayer::PlaybackState::Playing:
            if (playPauseBtn) playPauseBtn->setIcon(pauseIcon);
            break;
        case TLRenderPlayer::PlaybackState::Paused:
        case TLRenderPlayer::PlaybackState::Stopped:
            if (playPauseBtn) playPauseBtn->setIcon(playIcon);
            if (isVideo && lastKnownPosition >= 0) {
                qint64 durationMs = m_player->duration();
                updateVideoTimeDisplays(lastKnownPosition, durationMs);
            }
            break;
    }
}

void PreviewOverlay::onPlayerError(const QString& errorString)
{
    qWarning() << "[PreviewOverlay] tlRender error:" << errorString;
    QMessageBox::warning(this, "Playback Error",
                        QString("Failed to play media:\n%1").arg(errorString));
}

void PreviewOverlay::onPlayerEndOfStream()
{
    qDebug() << "[PreviewOverlay] tlRender end of stream reached";
    m_player->stop();
    m_player->seek(0);
    if (playPauseBtn) playPauseBtn->setIcon(playIcon);
}

#else
// ============================================================================
// GStreamerPlayer Signal Handlers
// ============================================================================

void PreviewOverlay::onGStreamerPositionChanged(qint64 positionMs)
{
    if (!positionSlider->isSliderDown()) {
        positionSlider->setValue(static_cast<int>(positionMs));
    }

    qint64 durationMs = PLAYER_DURATION();
    updateVideoTimeDisplays(positionMs, durationMs);
    
    // Store last known position for display during pause
    lastKnownPosition = positionMs;
    
    // When playing back, clear the explicit frame tracking so we calculate from position
    // This allows smooth playback while maintaining frame accuracy during pause/seek
    if (PLAYER_STATE() == PLAYER_STATE_PLAYING) {
        lastKnownVideoFrame = -1; // Let calculation take over during playback
    }
}

void PreviewOverlay::onGStreamerDurationChanged(qint64 durationMs)
{
    positionSlider->setRange(0, static_cast<int>(durationMs));
}

void PreviewOverlay::onGStreamerMediaInfo(const GStreamerPlayer::MediaInfo& info)
{
    qDebug() << "[PreviewOverlay] GStreamer media info:"
             << "Duration:" << info.durationMs << "ms"
             << "FPS:" << info.fps
             << "Resolution:" << info.width << "x" << info.height
             << "Codec:" << info.codec
             << "Has Audio:" << info.hasAudio;

    // Update duration slider range
    if (info.durationMs > 0) {
        positionSlider->setRange(0, info.durationMs);
        // Don't reset timecode to 0 here - let GStreamer position queries handle it
        // The position will be updated by onGStreamerPositionChanged
    }

    // Update FPS display
    if (info.fps > 0.0) {
        detectedFps = info.fps;
        if (fpsLabel) fpsLabel->setText(QString::number(info.fps, 'f', 1) + " fps");
    }

    positionSlider->setTimelineContext(true, detectedFps > 0.0 ? detectedFps : info.fps);
}

void PreviewOverlay::onGStreamerPlaybackStateChanged(GStreamerPlayer::PlaybackState state)
{
    qDebug() << "[PreviewOverlay] GStreamer state changed to:" << static_cast<int>(state);

    switch (state) {
        case PLAYER_STATE_PLAYING:
            if (playPauseBtn) playPauseBtn->setIcon(pauseIcon);
            break;
        case PLAYER_STATE_PAUSED:
        case PLAYER_STATE_STOPPED:
            if (playPauseBtn) playPauseBtn->setIcon(playIcon);
            // Update time display with last known position when paused
            if (isVideo && lastKnownPosition >= 0) {
                qint64 durationMs = PLAYER_DURATION();
                updateVideoTimeDisplays(lastKnownPosition, durationMs);
            }
            break;
    }
}

void PreviewOverlay::onGStreamerError(const QString& errorString)
{
    qWarning() << "[PreviewOverlay] GStreamer error:" << errorString;

    // Show error message to user
    QMessageBox::warning(this, "Playback Error",
                        QString("Failed to play media:\n%1").arg(errorString));
}

void PreviewOverlay::onGStreamerEndOfStream()
{
    qDebug() << "[PreviewOverlay] GStreamer end of stream reached";

    // Stop playback and reset to beginning
    PLAYER_STOP();
    PLAYER_SEEK(0);
    if (playPauseBtn) playPauseBtn->setIcon(playIcon);
}
#endif // HAVE_TLRENDER

void PreviewOverlay::showText(const QString &filePath)
{
    // Hide other content
    if (videoWidget) videoWidget->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    if (imageView) imageView->hide();
    if (stillImageView) stillImageView->hide();
    setControlsVisible(false);
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
            // Show OCIO controls for tlRender sequence playback
            if (ocioEnableCheck) ocioEnableCheck->show();
            if (ocioConfigBtn) ocioConfigBtn->show();
            if (ocioDisplayLabel) ocioDisplayLabel->show();
            if (ocioDisplayCombo) ocioDisplayCombo->show();
            if (ocioViewLabel) ocioViewLabel->show();
            if (ocioViewCombo) ocioViewCombo->show();
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
    if (stillImageView) stillImageView->hide();
    setControlsVisible(false);
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
    if (stillImageView) stillImageView->hide();
    setControlsVisible(false);
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
                        PLAYER_PLAY();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    if (imageView) imageView->hide();
    if (stillImageView) stillImageView->hide();
    setControlsVisible(false);
    if (alphaCheck) alphaCheck->hide();

                        PLAYER_PLAY();
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
    if (stillImageView) stillImageView->hide();
    setControlsVisible(false);
    if (alphaCheck) alphaCheck->hide();

    // Load PDF (works for many AI files that embed PDF)
    if (!pdfDoc) pdfDoc = new QPdfDocument(this);
    pdfDoc->close();
    QPdfDocument::Error err = pdfDoc->load(filePath);
    if (err == QPdfDocument::Error::None && pdfDoc->pageCount() > 0) {
        pdfCurrentPage = 0;
        if (stillImageView) stillImageView->hide();
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
    connect(worker, &FrameLoaderWorker::frameLoaded, this, [this](int idx, QImage image) {
        // SAFETY: This lambda won't execute if 'this' is destroyed (Qt auto-disconnect)
        {
            QMutexLocker locker(&m_mutex);
            m_pendingFrames.remove(idx);
            if (!image.isNull() && m_prefetchActive) {
                QPixmap pixmap = QPixmap::fromImage(image);
                if (!pixmap.isNull()) {
                    int cost = pixmap.width() * pixmap.height() * 4 / 1024; // KB
                    m_cache.insert(idx, new QPixmap(pixmap), cost);
                } else {
                    qWarning() << "[SequenceFrameCache] Failed to convert image to pixmap for frame" << idx;
                }
            } else if (image.isNull()) {
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
        // Final check before emitting to avoid enqueuing into a stale cache
        if (cache->isEpochCurrent(m_epoch)) {
            emit frameLoaded(m_frameIndex, image);
        }
    } else {
        qWarning() << "[FrameLoaderWorker] Failed to load frame:" << m_framePath;
        // Only notify failure if still current; otherwise earlier stopPrefetch()
        // already cleared pending state for this frame.
        if (cache->isEpochCurrent(m_epoch)) {
            emit frameLoaded(m_frameIndex, QImage());
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
// Annotation System Implementation
// ============================================================================

void PreviewOverlay::setupAnnotationToolbar()
{
    // Create toolbar widget
    annotationToolbar = new QWidget(this);
    annotationToolbar->setStyleSheet("QWidget { background-color: rgba(0, 0, 0, 180); }");
    annotationToolbar->setFixedHeight(60);
    annotationToolbar->hide(); // Hidden by default
    
    QHBoxLayout* toolbarLayout = new QHBoxLayout(annotationToolbar);
    toolbarLayout->setContentsMargins(10, 5, 10, 5);
    toolbarLayout->setSpacing(10);
    
    // Icon button style - minimal styling to let icons show clearly
    QString buttonStyle = "QPushButton { background-color: #444; border: none; border-radius: 4px; "
                         "padding: 6px; min-width: 36px; min-height: 36px; }"
                         "QPushButton:hover { background-color: #555; }"
                         "QPushButton:checked { background-color: #58a6ff; }";
    
    // Load icons from Icons/Annotation folder
    QString iconPath = QCoreApplication::applicationDirPath() + "/Icons/Annotation/";
    
    // Selection/move tool
    QPushButton* selectToolBtn = new QPushButton(annotationToolbar);
    selectToolBtn->setIcon(QIcon(iconPath + "cursor.png"));
    selectToolBtn->setIconSize(QSize(24, 24));
    selectToolBtn->setCheckable(true);
    selectToolBtn->setChecked(true); // Default mode
    selectToolBtn->setStyleSheet(buttonStyle);
    selectToolBtn->setToolTip("Select and move annotations");
    connect(selectToolBtn, &QPushButton::clicked, this, [this, selectToolBtn]() {
        penToolBtn->setChecked(false);
        textToolBtn->setChecked(false);
        rectangleToolBtn->setChecked(false);
        ellipseToolBtn->setChecked(false);
        arrowToolBtn->setChecked(false);
        selectToolBtn->setChecked(true);
        annotationLayer->setDrawMode(AnnotationLayer::Select);
    });
    toolbarLayout->addWidget(selectToolBtn);
    
    penToolBtn = new QPushButton(annotationToolbar);
    penToolBtn->setIcon(QIcon(iconPath + "paint.png"));
    penToolBtn->setIconSize(QSize(24, 24));
    penToolBtn->setCheckable(true);
    penToolBtn->setStyleSheet(buttonStyle);
    penToolBtn->setToolTip("Freehand drawing");
    connect(penToolBtn, &QPushButton::clicked, this, &PreviewOverlay::onAnnotationToolSelected);
    toolbarLayout->addWidget(penToolBtn);
    
    textToolBtn = new QPushButton(annotationToolbar);
    textToolBtn->setIcon(QIcon(iconPath + "text.png"));
    textToolBtn->setIconSize(QSize(24, 24));
    textToolBtn->setCheckable(true);
    textToolBtn->setStyleSheet(buttonStyle);
    textToolBtn->setToolTip("Add text annotation");
    connect(textToolBtn, &QPushButton::clicked, this, &PreviewOverlay::onAnnotationToolSelected);
    toolbarLayout->addWidget(textToolBtn);
    
    rectangleToolBtn = new QPushButton(annotationToolbar);
    rectangleToolBtn->setIcon(QIcon(iconPath + "Rectangle.png"));
    rectangleToolBtn->setIconSize(QSize(24, 24));
    rectangleToolBtn->setCheckable(true);
    rectangleToolBtn->setStyleSheet(buttonStyle);
    rectangleToolBtn->setToolTip("Draw rectangle");
    connect(rectangleToolBtn, &QPushButton::clicked, this, &PreviewOverlay::onAnnotationToolSelected);
    toolbarLayout->addWidget(rectangleToolBtn);
    
    ellipseToolBtn = new QPushButton(annotationToolbar);
    ellipseToolBtn->setIcon(QIcon(iconPath + "circle.png"));
    ellipseToolBtn->setIconSize(QSize(24, 24));
    ellipseToolBtn->setCheckable(true);
    ellipseToolBtn->setStyleSheet(buttonStyle);
    ellipseToolBtn->setToolTip("Draw circle/ellipse");
    connect(ellipseToolBtn, &QPushButton::clicked, this, &PreviewOverlay::onAnnotationToolSelected);
    toolbarLayout->addWidget(ellipseToolBtn);
    
    arrowToolBtn = new QPushButton(annotationToolbar);
    arrowToolBtn->setIcon(QIcon(iconPath + "arrow.png"));
    arrowToolBtn->setIconSize(QSize(24, 24));
    arrowToolBtn->setCheckable(true);
    arrowToolBtn->setStyleSheet(buttonStyle);
    arrowToolBtn->setToolTip("Draw arrow");
    connect(arrowToolBtn, &QPushButton::clicked, this, &PreviewOverlay::onAnnotationToolSelected);
    toolbarLayout->addWidget(arrowToolBtn);
    
    toolbarLayout->addSpacing(20);
    
    // Color picker button - use colored square as visual indicator
    colorPickerBtn = new QPushButton(annotationToolbar);
    colorPickerBtn->setFixedSize(36, 36);
    colorPickerBtn->setStyleSheet(QString("QPushButton { background-color: %1; border: 2px solid #fff; "
                                           "border-radius: 4px; }"
                                           "QPushButton:hover { border: 2px solid #58a6ff; }")
                                           .arg(annotationLayer->currentColor().name()));
    colorPickerBtn->setToolTip("Change annotation color");
    connect(colorPickerBtn, &QPushButton::clicked, this, &PreviewOverlay::onColorPicker);
    toolbarLayout->addWidget(colorPickerBtn);
    
    // Pen width slider
    QLabel* widthLabel = new QLabel("Width:", annotationToolbar);
    widthLabel->setStyleSheet("QLabel { color: white; }");
    toolbarLayout->addWidget(widthLabel);
    
    penWidthSlider = new QSlider(Qt::Horizontal, annotationToolbar);
    penWidthSlider->setRange(1, 20);  // Doubled max width from 10 to 20
    penWidthSlider->setValue(3);
    penWidthSlider->setFixedWidth(100);
    penWidthSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #555; height: 4px; }"
        "QSlider::handle:horizontal { background: #58a6ff; width: 12px; margin: -4px 0; border-radius: 6px; }"
    );
    connect(penWidthSlider, &QSlider::valueChanged, this, &PreviewOverlay::onPenWidthChanged);
    toolbarLayout->addWidget(penWidthSlider);
    
    toolbarLayout->addStretch();
    
    // Undo/Redo buttons with icons
    undoBtn = new QPushButton(annotationToolbar);
    undoBtn->setIcon(QIcon(iconPath + "undo.png"));
    undoBtn->setIconSize(QSize(24, 24));
    undoBtn->setStyleSheet(buttonStyle);
    undoBtn->setToolTip("Undo (Ctrl+Z)");
    connect(undoBtn, &QPushButton::clicked, annotationLayer->undoStack(), &QUndoStack::undo);
    toolbarLayout->addWidget(undoBtn);
    
    redoBtn = new QPushButton(annotationToolbar);
    redoBtn->setIcon(QIcon(iconPath + "redo.png"));
    redoBtn->setIconSize(QSize(24, 24));
    redoBtn->setStyleSheet(buttonStyle);
    redoBtn->setToolTip("Redo (Ctrl+Y)");
    connect(redoBtn, &QPushButton::clicked, annotationLayer->undoStack(), &QUndoStack::redo);
    toolbarLayout->addWidget(redoBtn);
    
    // Clear and save buttons with icons
    clearAnnotationsBtn = new QPushButton(annotationToolbar);
    clearAnnotationsBtn->setIcon(QIcon(iconPath + "clear.png"));
    clearAnnotationsBtn->setIconSize(QSize(24, 24));
    clearAnnotationsBtn->setStyleSheet(buttonStyle);
    clearAnnotationsBtn->setToolTip("Clear all annotations on current frame");
    connect(clearAnnotationsBtn, &QPushButton::clicked, this, &PreviewOverlay::onClearAnnotations);
    toolbarLayout->addWidget(clearAnnotationsBtn);
    
    saveFrameBtn = new QPushButton(annotationToolbar);
    saveFrameBtn->setIcon(QIcon(iconPath + "save.png"));
    saveFrameBtn->setIconSize(QSize(24, 24));
    saveFrameBtn->setStyleSheet(buttonStyle);
    saveFrameBtn->setToolTip("Save annotated frame (Ctrl+S)");
    connect(saveFrameBtn, &QPushButton::clicked, this, &PreviewOverlay::onSaveAnnotatedFrame);
    toolbarLayout->addWidget(saveFrameBtn);
    
    saveAllFramesBtn = new QPushButton(annotationToolbar);
    saveAllFramesBtn->setIcon(QIcon(iconPath + "save all.png"));
    saveAllFramesBtn->setIconSize(QSize(24, 24));
    saveAllFramesBtn->setStyleSheet(buttonStyle);
    saveAllFramesBtn->setToolTip("Save all annotated frames");
    saveAllFramesBtn->hide(); // Show only when multiple frames are annotated
    connect(saveAllFramesBtn, &QPushButton::clicked, this, &PreviewOverlay::onSaveAllAnnotatedFrames);
    toolbarLayout->addWidget(saveAllFramesBtn);
    
    // Insert toolbar below top bar
    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (mainLayout) {
        mainLayout->insertWidget(1, annotationToolbar);
    }
}

void PreviewOverlay::enableAnnotationMode(bool enable)
{
    annotationModeEnabled = enable;
    
    if (enable) {
        // Show annotation toolbar
        if (annotationToolbar) {
            annotationToolbar->show();
        }
        
        // Pause playback and capture position for timecode display
        if (isVideo || isSequence) {
            if (isVideo) {
                // Capture current position before pausing
                lastKnownPosition = PLAYER_POSITION();
                qDebug() << "[PreviewOverlay] Captured position before annotation mode:" << lastKnownPosition;
                
                if (PLAYER_STATE() == PLAYER_STATE_PLAYING) {
                    PLAYER_PAUSE();
                }
                
                // Immediately update timecode display with captured position
                qint64 durationMs = PLAYER_DURATION();
                updateVideoTimeDisplays(lastKnownPosition, durationMs);
            }
            if (sequencePlaying) {
                pauseSequence();
            }
        }
        
        // For videos, capture current frame and switch to imageView for annotation
        // This works around Qt transparency issues with native video windows
        if (isVideo) {
            // Initialize explicit frame tracking from current position
            double fps = detectedFps > 0.0 ? detectedFps : 24.0;
            lastKnownVideoFrame = static_cast<int>(qRound((lastKnownPosition / 1000.0) * fps));
            qDebug() << "[PreviewOverlay] Entering annotation mode - initialized frame to:" << lastKnownVideoFrame;
            
#ifdef HAVE_TLRENDER
            QImage videoFrame = m_player->getCurrentFrame();
#else
            QImage videoFrame = m_gstreamerPlayer->getCurrentFrame();
#endif
            if (!videoFrame.isNull()) {
                originalPixmap = QPixmap::fromImage(videoFrame);
                
                // Switch from video widget to image view
                videoWidget->hide();
        if (stillImageView) stillImageView->hide();
        imageView->show();
                
                // Display captured frame in imageView
                imageScene->clear();
                imageItem = imageScene->addPixmap(originalPixmap);
                imageScene->setSceneRect(originalPixmap.rect());
                fitImageToView();
                
                qDebug() << "[PreviewOverlay] Video frame captured for annotation";
            } else {
                qWarning() << "[PreviewOverlay] Failed to capture video frame - annotation disabled";
                toggleAnnotationBtn->setChecked(false);
                annotationModeEnabled = false;
                if (annotationToolbar) {
                    annotationToolbar->hide();
                }
                return;
            }
            
            // Use regular imageScene for video annotations
            annotationLayer->setScene(imageScene);
        } else {
            // For images/sequences, use regular imageScene
            annotationLayer->setScene(imageScene);
        }
        
        // Enable scene interaction for annotations
        if (imageView && imageView->isVisible()) {
            imageView->setDragMode(QGraphicsView::NoDrag);
            imageView->setMouseTracking(true);
            imageView->viewport()->setMouseTracking(true);
        }
        
        // Load annotations for current frame when entering annotation mode
        int frameIndex = isSequence ? currentSequenceFrame : (isVideo ? getVideoFrameNumber() : 0);
        currentAnnotatedFrame = frameIndex;
        loadFrameAnnotations(frameIndex);
        
        // Set default to select mode (for moving/editing)
        annotationLayer->setDrawMode(AnnotationLayer::Select);
    } else {
        // Save annotations before closing editor
        saveCurrentFrameAnnotations();
        
        // Hide annotation toolbar
        if (annotationToolbar) {
            annotationToolbar->hide();
        }
        
        // For videos, restore video playback
        if (isVideo) {
            // Clear annotations from scene (they're saved in frameAnnotations)
            annotationLayer->clearAnnotations();
            
            // Switch back to video widget
            imageView->hide();
            videoWidget->show();
            
            // Clear imageScene
            imageScene->clear();
            imageItem = nullptr;
            originalPixmap = QPixmap();
            
            qDebug() << "[PreviewOverlay] Restored video playback from annotation mode";
        }
        
        // Restore normal interaction
        if (imageView) {
            imageView->setDragMode(QGraphicsView::ScrollHandDrag);
            imageView->setMouseTracking(false);
            imageView->viewport()->setMouseTracking(false);
        }
        
        // Set to None mode but DON'T clear annotations - just disable editing
        annotationLayer->setDrawMode(AnnotationLayer::None);
    }
}

void PreviewOverlay::onToggleAnnotation()
{
    enableAnnotationMode(toggleAnnotationBtn->isChecked());
}

void PreviewOverlay::onAnnotationToolSelected()
{
    // Uncheck all tool buttons
    penToolBtn->setChecked(false);
    textToolBtn->setChecked(false);
    rectangleToolBtn->setChecked(false);
    ellipseToolBtn->setChecked(false);
    arrowToolBtn->setChecked(false);
    
    // Check the clicked button and set mode
    QPushButton* clicked = qobject_cast<QPushButton*>(sender());
    if (clicked) {
        clicked->setChecked(true);
        
        if (clicked == penToolBtn) {
            annotationLayer->setDrawMode(AnnotationLayer::Pen);
        } else if (clicked == textToolBtn) {
            annotationLayer->setDrawMode(AnnotationLayer::Text);
        } else if (clicked == rectangleToolBtn) {
            annotationLayer->setDrawMode(AnnotationLayer::Rectangle);
        } else if (clicked == ellipseToolBtn) {
            annotationLayer->setDrawMode(AnnotationLayer::Ellipse);
        } else if (clicked == arrowToolBtn) {
            annotationLayer->setDrawMode(AnnotationLayer::Arrow);
        }
    }
}

void PreviewOverlay::onColorPicker()
{
    QColor color = QColorDialog::getColor(annotationLayer->currentColor(), this, "Select Annotation Color");
    if (color.isValid()) {
        annotationLayer->setCurrentColor(color);
        // Update color picker button background
        if (colorPickerBtn) {
            colorPickerBtn->setStyleSheet(QString("QPushButton { background-color: %1; border: 2px solid #fff; "
                                                   "border-radius: 4px; }"
                                                   "QPushButton:hover { border: 2px solid #58a6ff; }")
                                                   .arg(color.name()));
        }
    }
}

void PreviewOverlay::onPenWidthChanged(int width)
{
    annotationLayer->setCurrentPenWidth(width);
}

void PreviewOverlay::onClearAnnotations()
{
    annotationLayer->clearAnnotations();
    
    // Clear annotation marker for current frame
    int frameIndex = isSequence ? currentAnnotatedFrame : 0;
    if (frameIndex >= 0) {
        frameAnnotations.remove(frameIndex);
        annotatedFrameIndices.remove(frameIndex);
        if (positionSlider) {
            positionSlider->unmarkFrameAnnotated(frameIndex);
        }
    }
}

void PreviewOverlay::onSaveAnnotatedFrame()
{
    // Generate default name with pattern: {filename}_annotation_{frame}.png
    QString baseName = QFileInfo(currentFilePath).completeBaseName();
    int frameIndex = isSequence ? currentSequenceFrame : (isVideo ? getVideoFrameNumber() : 0);
    QString defaultName;
    
    if (isSequence || isVideo) {
        // For sequences and videos, include frame/time number
        defaultName = QString("%1_annotation_%2.png").arg(baseName).arg(frameIndex, 4, 10, QChar('0'));
    } else {
        // For single images, no frame number needed
        defaultName = baseName + "_annotation.png";
    }
    
    QString filePath = QFileDialog::getSaveFileName(this, "Save Annotated Frame", defaultName,
                                                      "PNG Image (*.png);;JPEG Image (*.jpg *.jpeg)");
    if (!filePath.isEmpty()) {
        QFileInfo fi(filePath);
        QString format = fi.suffix().toLower();
        if (format != "png" && format != "jpg" && format != "jpeg") {
            format = "png"; // Default to PNG
        }
        exportAnnotatedFrame(filePath, format);
    }
}

void PreviewOverlay::onSaveAllAnnotatedFrames()
{
    // Save current frame annotations before exporting all
    if (annotationModeEnabled && currentAnnotatedFrame >= 0) {
        qDebug() << "[PreviewOverlay] Saving current frame annotations before export all";
        saveCurrentFrameAnnotations();
    }

    // Use only frames that actually have stored annotations (skip unannotated current frame)
    QList<int> framesToExport = frameAnnotations.keys();
    std::sort(framesToExport.begin(), framesToExport.end());
    if (framesToExport.isEmpty()) {
        QMessageBox::information(this, "Export Annotated Frames", "No annotated frames to export.");
        return;
    }
    
    QString directory = QFileDialog::getExistingDirectory(this, "Select Output Directory");
    if (directory.isEmpty()) {
        return;
    }
    
    QString baseName = QFileInfo(currentFilePath).completeBaseName();
    
    // Save current frame/time state to restore later
    int originalFrame = isSequence ? currentSequenceFrame : (isVideo ? getVideoFrameNumber() : 0);
    bool wasPlaying = (PLAYER_STATE() == PLAYER_STATE_PLAYING);
    if (isVideo && wasPlaying) {
        PLAYER_PAUSE();
    }
    
    for (int frameIndex : framesToExport) {
        // For sequences, load the actual frame image first
        if (isSequence && frameIndex < sequenceFramePaths.size()) {
            loadSequenceFrame(frameIndex);
        } else if (isVideo) {
            // For videos, convert frame number back to time using FPS
            double fps = detectedFps > 0.0 ? detectedFps : 24.0;
            qint64 timeMs = static_cast<qint64>((frameIndex / fps) * 1000.0);
            qDebug() << "[PreviewOverlay] Exporting frame" << frameIndex << "at time" << timeMs << "ms (fps:" << fps << ")";
            PLAYER_SEEK(timeMs);
            // Give player time to seek
            QThread::msleep(100);
            // Capture the frame
#ifdef HAVE_TLRENDER
            QImage videoFrame = m_player->getCurrentFrame();
#else
            QImage videoFrame = m_gstreamerPlayer->getCurrentFrame();
#endif
            if (!videoFrame.isNull()) {
                originalPixmap = QPixmap::fromImage(videoFrame);
            } else {
                qWarning() << "[PreviewOverlay] Failed to capture video frame at time" << frameIndex;
                continue;
            }
        }
        
        // Load annotations for this frame
        currentAnnotatedFrame = frameIndex;
        loadFrameAnnotations(frameIndex);
        
        // Generate filename with pattern: {filename}_annotation_{frame}.png
        QString fileName = QString("%1_annotation_%2.png").arg(baseName).arg(frameIndex, 4, 10, QChar('0'));
        QString filePath = QDir(directory).filePath(fileName);
        
        // Export frame
        exportAnnotatedFrame(filePath, "png");
    }
    
    // Restore original frame/time
    if (isSequence) {
        loadSequenceFrame(originalFrame);
        loadFrameAnnotations(originalFrame);
        currentAnnotatedFrame = originalFrame;
    } else if (isVideo) {
        // Convert frame number back to time
        double fps = detectedFps > 0.0 ? detectedFps : 24.0;
        qint64 timeMs = static_cast<qint64>((originalFrame / fps) * 1000.0);
        PLAYER_SEEK(timeMs);
        QThread::msleep(100);
#ifdef HAVE_TLRENDER
        QImage videoFrame = m_player->getCurrentFrame();
#else
        QImage videoFrame = m_gstreamerPlayer->getCurrentFrame();
#endif
        if (!videoFrame.isNull()) {
            originalPixmap = QPixmap::fromImage(videoFrame);
            loadFrameAnnotations(originalFrame);
            currentAnnotatedFrame = originalFrame;
        }
        if (wasPlaying) {
            PLAYER_PLAY();
        }
    }
    
    QMessageBox::information(this, "Export Complete", 
                            QString("Exported %1 annotated frames to %2")
                                .arg(framesToExport.size())
                                .arg(directory));
}

void PreviewOverlay::saveCurrentFrameAnnotations()
{
    qDebug() << "[PreviewOverlay] saveCurrentFrameAnnotations() called";
    qDebug() << "  - annotationModeEnabled:" << annotationModeEnabled;
    qDebug() << "  - currentAnnotatedFrame:" << currentAnnotatedFrame;
    qDebug() << "  - isVideo:" << isVideo << "isSequence:" << isSequence;
    
    // Don't save if we never entered annotation mode on this frame
    if (currentAnnotatedFrame < 0) {
        qDebug() << "  - Skipping save: currentAnnotatedFrame < 0";
        return;
    }
    
    int frameIndex = isSequence ? currentAnnotatedFrame : (isVideo ? currentAnnotatedFrame : 0);
    qDebug() << "  - Determined frameIndex:" << frameIndex;
    
    // Always serialize and save, even if empty (to clear previous annotations)
    QJsonArray serialized = annotationLayer->serializeAnnotations();
    qDebug() << "  - Serialized annotations count:" << serialized.size();
    qDebug() << "  - Serialized JSON:" << QJsonDocument(serialized).toJson(QJsonDocument::Compact);
    
    if (serialized.isEmpty()) {
        // If no annotations, remove from storage
        qDebug() << "  - Removing frame from annotations (empty)";
        frameAnnotations.remove(frameIndex);
        annotatedFrameIndices.remove(frameIndex);
        // Update timeline marker
        if (positionSlider) {
            // For videos, convert frame number to milliseconds for timeline marking
            int markerPosition = frameIndex;
            if (isVideo) {
                double fps = detectedFps > 0.0 ? detectedFps : 24.0;
                markerPosition = static_cast<int>((frameIndex / fps) * 1000.0);
            }
            positionSlider->unmarkFrameAnnotated(markerPosition);
            qDebug() << "  - Cleared timeline marker for frame" << frameIndex;
        }
    } else {
        // Save annotations
        qDebug() << "  - Saving annotations for frame" << frameIndex;
        frameAnnotations[frameIndex] = serialized;
        bool wasNew = !annotatedFrameIndices.contains(frameIndex);
        annotatedFrameIndices.insert(frameIndex);
        qDebug() << "  - Frame was new:" << wasNew;
        qDebug() << "  - Total annotated frames now:" << annotatedFrameIndices.size();
        
        // Update timeline marker
        if (positionSlider) {
            // For videos, convert frame number to milliseconds for timeline marking
            int markerPosition = frameIndex;
            if (isVideo) {
                double fps = detectedFps > 0.0 ? detectedFps : 24.0;
                markerPosition = static_cast<int>((frameIndex / fps) * 1000.0);
                qDebug() << "  - Converting frame" << frameIndex << "to" << markerPosition << "ms for timeline (fps:" << fps << ")";
            }
            positionSlider->markFrameAnnotated(markerPosition);
        } else {
            qDebug() << "  - WARNING: positionSlider is null";
        }
    }
    
    // Show "Save All" button if multiple frames are annotated
    if (saveAllFramesBtn) {
        bool shouldShow = annotatedFrameIndices.size() > 1;
        qDebug() << "  - Save All button should be visible:" << shouldShow << "(count:" << annotatedFrameIndices.size() << ")";
        if (shouldShow) {
            saveAllFramesBtn->show();
        } else {
            saveAllFramesBtn->hide();
        }
    } else {
        qDebug() << "  - WARNING: saveAllFramesBtn is null";
    }
    
    qDebug() << "[PreviewOverlay] saveCurrentFrameAnnotations() completed";
}

void PreviewOverlay::loadFrameAnnotations(int frameIndex)
{
    qDebug() << "[PreviewOverlay] Loading annotations for frame" << frameIndex;
    
    // Clear current annotations
    annotationLayer->clearAnnotations();
    
    // Load annotations for this frame if they exist
    if (frameAnnotations.contains(frameIndex)) {
        QJsonArray data = frameAnnotations[frameIndex];
        qDebug() << "  - Found" << data.size() << "annotations to load";
        qDebug() << "  - JSON:" << QJsonDocument(data).toJson(QJsonDocument::Compact);
        annotationLayer->deserializeAnnotations(data);
        qDebug() << "  - After deserialization, annotation count:" << annotationLayer->annotationCount();
    } else {
        qDebug() << "  - No annotations found for this frame";
    }
}

QImage PreviewOverlay::captureCurrentFrame()
{
    if (isVideo) {
        // For video, capture current frame from player
#ifdef HAVE_TLRENDER
        QImage frame = m_player->getCurrentFrame();
#else
        QImage frame = m_gstreamerPlayer->getCurrentFrame();
#endif
        if (frame.isNull()) {
            qWarning() << "[PreviewOverlay] Failed to capture video frame for annotation";
        }
        return frame;
    } else if (imageItem && !originalPixmap.isNull()) {
        // For images/sequences, capture from the pixmap
        return originalPixmap.toImage();
    }
    
    return QImage();
}

void PreviewOverlay::exportAnnotatedFrame(const QString& filePath, const QString& format)
{
    // Capture base frame
    QImage baseFrame = captureCurrentFrame();
    if (baseFrame.isNull()) {
        QMessageBox::warning(this, "Export Failed", "Could not capture current frame.");
        return;
    }
    
    // Create a painter to render annotations on top
    QPainter painter(&baseFrame);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Render only the annotation items (not the entire scene)
    for (AnnotationItem* item : annotationLayer->annotations()) {
        // Save painter state
        painter.save();
        
        // Translate to item position
        painter.translate(item->pos());
        
        // Render the item
        item->paint(&painter, nullptr, nullptr);
        
        // Restore painter state
        painter.restore();
    }
    
    painter.end();
    
    // Save the composited image
    if (baseFrame.save(filePath, format.toUpper().toUtf8().constData())) {
        qDebug() << "[PreviewOverlay] Saved annotated frame to" << filePath;
    } else {
        QMessageBox::warning(this, "Export Failed", "Could not save file to " + filePath);
    }
}

int PreviewOverlay::getVideoFrameNumber() const
{
    if (!isVideo) return 0;
    
    // If we have an explicitly tracked frame number from seek/step operations,
    // use it directly for frame-perfect annotation positioning
    if (lastKnownVideoFrame >= 0) {
        qDebug() << "[PreviewOverlay] Using explicitly tracked frame:" << lastKnownVideoFrame;
        return lastKnownVideoFrame;
    }
    
    // Otherwise calculate from position with rounding to nearest frame
    qint64 currentTimeMs = PLAYER_POSITION();
    double fps = detectedFps > 0.0 ? detectedFps : 24.0; // Default to 24fps if not detected
    
    // Calculate frame number with proper rounding to snap to frame boundaries
    // This prevents ±1 frame drift from GStreamer position reporting
    int frameNumber = static_cast<int>(qRound((currentTimeMs / 1000.0) * fps));
    
    qDebug() << "[PreviewOverlay] Frame calculation: position=" << currentTimeMs 
             << "ms, fps=" << fps << "-> frame" << frameNumber;
    
    return frameNumber;
}

void PreviewOverlay::updateVideoAnnotationFrame()
{
    if (!isVideo || !annotationModeEnabled) {
        return;
    }
    
    // Get current frame number using FPS
    int newFrameIndex = getVideoFrameNumber();
    
    // Save annotations for previous frame if it changed
    if (currentAnnotatedFrame >= 0 && currentAnnotatedFrame != newFrameIndex) {
        qDebug() << "[PreviewOverlay] Saving annotations for frame" << currentAnnotatedFrame << "before switching to" << newFrameIndex;
        saveCurrentFrameAnnotations();
    }
    
    // Update frame index BEFORE loading
    int previousFrame = currentAnnotatedFrame;
    currentAnnotatedFrame = newFrameIndex;
    
    // Capture new frame (this is slow but necessary for accuracy)
    // Use a single-shot timer to avoid blocking
    QTimer::singleShot(0, this, [this, newFrameIndex, previousFrame]() {
#ifdef HAVE_TLRENDER
        QImage videoFrame = m_player->getCurrentFrame();
#else
        QImage videoFrame = m_gstreamerPlayer->getCurrentFrame();
#endif
        if (!videoFrame.isNull() && imageItem) {
            originalPixmap = QPixmap::fromImage(videoFrame);
            imageItem->setPixmap(originalPixmap);
            
            // Load annotations for this frame
            loadFrameAnnotations(newFrameIndex);
            
            qDebug() << "[PreviewOverlay] Updated video annotation frame at time" << newFrameIndex 
                     << "- annotated frames:" << annotatedFrameIndices.size();
        }
    });
}

