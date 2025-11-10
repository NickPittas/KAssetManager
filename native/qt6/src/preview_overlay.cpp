#include "preview_overlay.h"
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
#include <QImageReader>
#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#include <QPdfView>
#endif
#include <QFontDatabase>
#include <QTextOption>

#include <QVector>
#include <QSettings>

#include <QVideoFrame>

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
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QScreen>
#include "star_rating_widget.h"

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
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}
#include <QImage>
#include <QRegularExpression>

#include "ffmpeg_video_reader.h"


#endif // HAVE_FFMPEG

PreviewOverlay::PreviewOverlay(QWidget *parent)
    : QWidget(parent)
    , imageView(nullptr)
    , imageScene(nullptr)
    , imageItem(nullptr)
    , videoItem(nullptr)
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
    , fitToView(true)
    , initialSized(false)
    , isSequence(false)
    , currentSequenceFrame(0)
    , sequenceStartFrame(0)
    , sequenceEndFrame(0)
    , sequencePlaying(false)
    , currentColorSpace(OIIOImageLoader::ColorSpace::Rec709)
    , isHDRImage(false)
    , useCacheForSequences(true) // ENABLED - using QRecursiveMutex to fix deadlock
{
    setupUi();
    setFocusPolicy(Qt::StrongFocus);

    // Show as a normal resizable window (not fullscreen/child of main window)
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setAttribute(Qt::WA_DeleteOnClose, false);

    // Remove any stray StarRatingWidget if accidentally added to the overlay
    if (auto star = findChild<StarRatingWidget*>(QString(), Qt::FindChildrenRecursively)) {
        star->deleteLater();
    }

    // Auto-hide controls timer
    controlsTimer = new QTimer(this);
    controlsTimer->setSingleShot(true);
    controlsTimer->setInterval(3000);
    connect(controlsTimer, &QTimer::timeout, this, &PreviewOverlay::hideControls);

    // Sequence playback timer (24 fps default)
    sequenceTimer = new QTimer(this);
    sequenceTimer->setInterval(1000 / 24); // 24 fps
    connect(sequenceTimer, &QTimer::timeout, this, &PreviewOverlay::onSequenceTimerTick);

    // Initialize frame cache for image sequence playback
    frameCache = new SequenceFrameCache(this);
    qDebug() << "[PreviewOverlay] Frame cache initialized with QRecursiveMutex (deadlock fix)";
}

PreviewOverlay::~PreviewOverlay()
{
    // Ensure all playback modes are fully stopped before destruction
    stopPlayback();
#ifdef HAVE_FFMPEG
    // As an extra safety net, wait a bit if the worker thread is still winding down
    if (fallbackThread) {
        fallbackThread->wait(3000);
    }
#endif
    if (sequenceTimer) {
        sequenceTimer->stop();
    }

    // Stop frame cache pre-fetching
    if (frameCache) {
        frameCache->stopPrefetch();
    }
}

void PreviewOverlay::setupUi()
{
    // Full-screen black background
    setStyleSheet("QWidget { background-color: #000000; }");
    setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    // Lift the entire controls area up from the bottom to avoid OS/taskbar overlap
    mainLayout->setContentsMargins(0, 0, 0, 80); // bottom offset ~80px
    mainLayout->setSpacing(0);

    // Top bar with filename and close button
    QWidget *topBar = new QWidget(this);
    topBar->setStyleSheet("QWidget { background-color: rgba(0, 0, 0, 180); }");
    topBar->setFixedHeight(50);
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);

    fileNameLabel = new QLabel(this);
    fileNameLabel->setStyleSheet("QLabel { color: white; font-size: 16px; padding: 10px; }");
    topLayout->addWidget(fileNameLabel);

    // Alpha toggle (for images with alpha)
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
    topLayout->addSpacing(12);
    topLayout->addWidget(alphaCheck);

    topLayout->addStretch();

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

    // Image view with zoom/pan support (for images)
    imageView = new QGraphicsView(this);
    imageScene = new QGraphicsScene(this);
    imageView->setScene(imageScene);
    imageView->setStyleSheet("QGraphicsView { background-color: #000000; border: none; }");
    imageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    imageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    imageView->setDragMode(QGraphicsView::ScrollHandDrag);
    imageView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    imageView->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    // Consume wheel on the view/viewport to avoid parent scrolling
    imageView->installEventFilter(this);
    imageView->viewport()->installEventFilter(this);
    // Disable DnD in overlay preview to avoid conflicts with pan gesture
    imageView->setAcceptDrops(false);
    if (imageView->viewport()) imageView->viewport()->setAcceptDrops(false);

    imageView->hide();
    contentLayout->addWidget(imageView);
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

    // Video item (for videos) inside the same GraphicsView to support zoom/pan
    videoItem = new QGraphicsVideoItem();
    videoItem->setVisible(false);
    imageScene->addItem(videoItem);

    // Fit video when native size becomes available
    connect(videoItem, &QGraphicsVideoItem::nativeSizeChanged, this, [this](const QSizeF& sz){
        if (imageView && videoItem && videoItem->isVisible()) {
            // Normalize item geometry and scene rect to the new native size
            videoItem->setPos(0, 0);
            if (sz.isValid()) {
                videoItem->setSize(sz);
                if (imageScene) imageScene->setSceneRect(QRectF(QPointF(0, 0), sz));
            }
            if (fitToView) {
                imageView->resetTransform();
                imageView->fitInView(videoItem, Qt::KeepAspectRatio);
            }
            if (!initialSized) {
                const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
                const int contentW = static_cast<int>(sz.width());
                const int contentH = static_cast<int>(sz.height());
                int w = qMin(contentW + 40, avail.width() - 80);
                int h = qMin(50 + contentH + 120 + 40, avail.height() - 80);
                resize(w, h);
                const QPoint center = avail.center();
                move(center.x() - width()/2, center.y() - height()/2);
                initialSized = true;
            }
        }
    });

    mainLayout->addWidget(contentWidget, 1);

    // Bottom controls (for video)
    controlsWidget = new QWidget(this);
    controlsWidget->setStyleSheet("QWidget { background-color: rgba(0, 0, 0, 180); }");
    controlsWidget->setFixedHeight(120);
    controlsWidget->installEventFilter(this);
    controlsWidget->hide();

    QVBoxLayout *controlsLayout = new QVBoxLayout(controlsWidget);
    controlsLayout->setContentsMargins(20, 10, 20, 10);

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

    // ========== ROW 2: Timeline (Current | Slider | Duration) ==========
    currentTimeLabel = new QLabel("00:00:00:00", this);
    currentTimeLabel->setStyleSheet("QLabel { color: white; font-size: 14px; padding: 0 8px; }");

    durationTimeLabel = new QLabel("00:00:00:00", this);
    durationTimeLabel->setStyleSheet("QLabel { color: white; font-size: 14px; padding: 0 8px; }");

    positionSlider->setFixedHeight(20);

    {
        QHBoxLayout *timelineRow = new QHBoxLayout();
        timelineRow->setContentsMargins(0, 4, 0, 4);
        timelineRow->setSpacing(8);
        timelineRow->addWidget(currentTimeLabel);
        timelineRow->addWidget(positionSlider, /*stretch*/ 1);
        timelineRow->addWidget(durationTimeLabel);
        controlsLayout->addLayout(timelineRow);
    }

    // Optional color space selector (kept hidden by default)
    colorSpaceLabel = new QLabel("Color Space:", this);
    colorSpaceLabel->setStyleSheet("QLabel { color: white; font-size: 14px; padding: 0 5px; }");
    colorSpaceLabel->hide();

    colorSpaceCombo = new QComboBox(this);
    colorSpaceCombo->addItem("Linear");
    colorSpaceCombo->addItem("sRGB");
    colorSpaceCombo->addItem("Rec.709");
    colorSpaceCombo->setCurrentIndex(2);
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
    csGroup->setVisible(true);
    bottomGrid->addWidget(csGroup, 0, 2, Qt::AlignVCenter);

    bottomGrid->addWidget(audioGroup, 0, 3, Qt::AlignRight | Qt::AlignVCenter);

    controlsLayout->addLayout(bottomGrid);

    // Overlay controls float over content; not added to mainLayout to maximize content area
    // Geometry is set in resizeEvent and when showing content

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


    // Initialize media player
    mediaPlayer = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    mediaPlayer->setAudioOutput(audioOutput);
    // Create a QVideoSink for optional software processing (inactive by default)
    videoSink = new QVideoSink(this);
    // Default to hardware video path for full-resolution playback
    mediaPlayer->setVideoOutput(videoItem);
    connect(videoSink, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &frame){
        if (!frame.isValid()) return;
        QImage img = frame.toImage();
        if (img.isNull()) return;
        lastVideoFrameRaw = img; // cache raw frame (unscaled)

        // Use full-resolution frame for display; the view will scale down as needed
        QImage out = lastVideoFrameRaw;

        auto toLinear709 = [](float v){ return (v < 0.081f) ? (v / 4.5f) : std::pow((v + 0.099f) / 1.099f, 1.0f/0.45f); };
        auto linearToSRGB = [](float v){ v = std::clamp(v, 0.0f, 1.0f); return (v <= 0.0031308f) ? 12.92f*v : 1.055f*std::pow(v, 1.0f/2.4f) - 0.055f; };

        if (currentColorSpace != OIIOImageLoader::ColorSpace::Rec709) {
            out = out.convertToFormat(QImage::Format_RGBA8888);
            const int w = out.width(), h = out.height();
            for (int y = 0; y < h; ++y) {
                uchar* p = out.scanLine(y);
                for (int x = 0; x < w; ++x) {
                    float r = p[4*x+0] / 255.0f;
                    float g = p[4*x+1] / 255.0f;
                    float b = p[4*x+2] / 255.0f;
                    r = toLinear709(r); g = toLinear709(g); b = toLinear709(b);
                    if (currentColorSpace == OIIOImageLoader::ColorSpace::sRGB) {
                        r = linearToSRGB(r); g = linearToSRGB(g); b = linearToSRGB(b);
                    } else { // Linear target
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

        // Show processed frame in the graphics view
        if (videoItem) videoItem->setVisible(false);
        if (imageScene) {
            if (!imageItem) {
                imageItem = new QGraphicsPixmapItem();
                imageScene->addItem(imageItem);
            }
            imageItem->setPixmap(QPixmap::fromImage(out));
            imageItem->setTransformationMode(Qt::SmoothTransformation);
            imageScene->setSceneRect(imageItem->boundingRect());
            if (imageView && fitToView) {
                const QSize sz = out.size();
                if (sz != lastVideoPixmapSize) {
                    lastVideoPixmapSize = sz;
                    imageView->resetTransform();
                    imageView->fitInView(imageItem, Qt::KeepAspectRatio);
                }
            }
        // Size window on first frame for QVideoSink path
        if (!initialSized) {
            const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
            const int contentW = out.width();
            const int contentH = out.height();
            int w = qMin(contentW + 40, avail.width() - 80);
            int h = qMin(50 + contentH + 120 + 40, avail.height() - 80);
            resize(w, h);
            const QPoint center = avail.center();
            move(center.x() - width()/2, center.y() - height()/2);
            initialSized = true;
        }

        }
    });

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
        if (videoItem) videoItem->setVisible(false);
        imageView->show();
        if (videoItem && videoItem->scene() == imageScene) imageScene->removeItem(videoItem);
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
        if (videoItem) videoItem->setVisible(false);
#ifdef HAVE_QT_PDF
        if (pdfView) pdfView->hide();
#endif
        imageView->show();
        if (videoItem && videoItem->scene() == imageScene) imageScene->removeItem(videoItem);
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
    if (videoItem) videoItem->setVisible(false);
    if (textView) textView->hide();
    if (tableView) tableView->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    // Reset sizing/fit for new content
    initialSized = false;
    fitToView = true;
    imageView->setBackgroundBrush(QColor("#0a0a0a"));
    imageView->show();

    // Anchor nav arrows to the image viewport when showing images
    positionNavButtons(imageView->viewport());

    // CRITICAL: Stop any video playback
    if (mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        mediaPlayer->stop();
    }
#ifdef HAVE_FFMPEG
    if (usingFallbackVideo) {
        stopFallbackVideo();
    }
#endif

    // Check if this is an HDR/EXR image
    QFileInfo fileInfo(filePath);
    QString ext = fileInfo.suffix().toLower();
    isHDRImage = (ext == "exr" || ext == "hdr" || ext == "tif" || ext == "tiff" || ext == "psd");

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
        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        QImage img = reader.read();
        if (!img.isNull()) newPixmap = QPixmap::fromImage(img);
        isHDRImage = false; // Qt loader doesn't support HDR
    }


    if (!newPixmap.isNull()) {
        // CRITICAL: Update the pixmap on existing item or create new one
        if (imageItem) {
            imageItem->setPixmap(newPixmap);
        } else {
            if (videoItem && videoItem->scene() == imageScene) imageScene->removeItem(videoItem);
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

        // Fit to view
        fitImageToView();
        // Initial window sizing to content
        if (!initialSized) {
            const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
            const int contentW = newPixmap.width();
            const int contentH = newPixmap.height();
            int w = qMin(contentW + 40, avail.width() - 80);
            int h = qMin(50 + contentH + 120 + 40, avail.height() - 80);
            resize(w, h);
            // Center relative to screen
            const QPoint center = avail.center();
            move(center.x() - width()/2, center.y() - height()/2);
            initialSized = true;
        }

        // CRITICAL: Force complete view refresh
        imageView->viewport()->update();
        imageView->update();
        imageScene->update();

        // Show/hide color space selector based on whether this is HDR
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
    // Reset sizing/fit for new content
    initialSized = false;
    fitToView = true;

    // Ensure any fallback is stopped before starting normal video
#ifdef HAVE_FFMPEG
    if (usingFallbackVideo) {
        stopFallbackVideo();
    }
#endif

    // Ensure media player is in a clean state
    if (mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        mediaPlayer->stop();
    }
    mediaPlayer->setSource(QUrl()); // clear previous source to avoid stray signals
    mediaPlayer->setPosition(0);

    // CRITICAL: Hide other content and show video inside graphics view
    if (textView) textView->hide();
    if (tableView) tableView->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    if (imageView) imageView->show();
    if (videoItem) videoItem->setVisible(true); // ensure hardware video item is visible
    // Anchor nav arrows to the image viewport (graphics view)
    positionNavButtons(imageView ? imageView->viewport() : this);
    controlsWidget->show();
    controlsWidget->setGeometry(0, height() - controlsWidget->height(), width(), controlsWidget->height());
    controlsWidget->raise();
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
    // Show color space controls for videos but disable transforms (Rec.709 hardware path)
    if (colorSpaceLabel) colorSpaceLabel->show();
    if (colorSpaceCombo) { colorSpaceCombo->show(); colorSpaceCombo->setEnabled(false); }

    // Prepare imageItem for video frames (delete stale item safely)
    if (imageItem) {
        if (imageItem->scene() == imageScene) imageScene->removeItem(imageItem);
        delete imageItem;
        imageItem = nullptr;
    }

    // Probe metadata for FPS/timecode via FFmpeg (if available) and choose pipeline deterministically
    {
        MediaInfo::VideoMetadata vm;
        QString err;
        bool probed = MediaInfo::probeVideoFile(filePath, vm, &err);
        if (probed) {
            if (vm.fps > 0.0) detectedFps = vm.fps;
            hasEmbeddedTimecode = vm.hasTimecode;
            embeddedStartTimecode = vm.timecodeStart;
        }
        bool useFFmpeg = false;
        if (probed) {
            const QString codec = vm.videoCodec.toLower();
            const QString container = QFileInfo(filePath).suffix().toLower();
            if (codec == "prores" || codec.startsWith("dnx") || codec == "qtrle" || codec == "png" || container == "mxf") {
                useFFmpeg = true;
            }
        }
#ifdef HAVE_FFMPEG
        if (useFFmpeg) {
            qDebug() << "[PreviewOverlay] Routing" << filePath << "to FFmpeg reader based on probe";
            startFallbackVideo(filePath);
            return;
        }
#else
        Q_UNUSED(useFFmpeg);
#endif
    }

    originalPixmap = QPixmap(); // Clear the pixmap

    // Use hardware path (QGraphicsVideoItem) for full-resolution playback; disable transforms for videos
    mediaPlayer->setVideoOutput(videoItem);
    if (videoItem) videoItem->setVisible(true);
    if (imageItem) imageItem->setVisible(false);
    if (colorSpaceLabel) colorSpaceLabel->show();
    if (colorSpaceCombo) { colorSpaceCombo->show(); colorSpaceCombo->setCurrentIndex(2); colorSpaceCombo->setEnabled(false); }
    if (alphaCheck) alphaCheck->hide();
    // Normalize geometry and ensure initial fit in case nativeSizeChanged is not emitted
    if (videoItem && imageView) {
        videoItem->setPos(0, 0);
        const QSizeF ns = videoItem->nativeSize();
        if (ns.isValid()) {
            videoItem->setSize(ns);
            if (imageScene) imageScene->setSceneRect(QRectF(QPointF(0, 0), ns));
            if (fitToView) {
                imageView->resetTransform();
                imageView->fitInView(videoItem, Qt::KeepAspectRatio);
            }
        }
    }

    mediaPlayer->setSource(QUrl::fromLocalFile(filePath));

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
#ifdef HAVE_FFMPEG
        if (usingFallbackVideo) {
            fallbackPaused = !fallbackPaused;
            if (fallbackReader) {
                fallbackReader->setPaused(fallbackPaused);
            }
            updatePlayPauseButton();
        } else
#endif
        {
            if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
                mediaPlayer->pause();
            } else {
                mediaPlayer->play();
            }
            updatePlayPauseButton();
        }
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
#ifdef HAVE_FFMPEG
    if (usingFallbackVideo) {
        // Update time label and seek continuously while dragging (live scrubbing)
        if (fallbackDurationMs > 0) {
            updateVideoTimeDisplays(position, fallbackDurationMs);
        } else {
            updateVideoTimeDisplays(position, -1);
        }
        if (fallbackReader) {
            fallbackReader->seekToMs(static_cast<qint64>(position));
            fallbackReader->stepOnce();
        }
        controlsTimer->start();
        return;
    }
#endif
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
#ifdef HAVE_FFMPEG
    if (usingFallbackVideo) {
        wasPlayingBeforeSeek = !fallbackPaused;
        fallbackPaused = true;
        if (fallbackReader) {
            // Ensure the decode thread is paused before we continue (prevents racing during scrubbing)
            fallbackReader->setPaused(true);
        }
        return;
    }
#endif
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
#ifdef HAVE_FFMPEG
    if (usingFallbackVideo) {
        if (fallbackReader) {
            // Apply seek synchronously, then single-step to render frame at target
            fallbackReader->seekToMs(static_cast<qint64>(pos));
            fallbackReader->stepOnce();
        }
        if (wasPlayingBeforeSeek) {
            fallbackPaused = false;
            if (fallbackReader) {
                fallbackReader->setPaused(false);
            }
        }
        userSeeking = false;
        updatePlayPauseButton();
        controlsTimer->start();
        return;
    }
#endif
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
#ifdef HAVE_FFMPEG
    if (usingFallbackVideo) {
        // Pause and single-step forward
        fallbackPaused = true;
        if (fallbackReader) {
            fallbackReader->setPaused(true);
            qint64 pos = positionSlider->value();
            qint64 dt = static_cast<qint64>(qRound64(frameDurationMs()));
            qint64 target = qMin(pos + dt, static_cast<qint64>(positionSlider->maximum()));
            fallbackReader->seekToMs(target);
            fallbackReader->stepOnce();
        }
        updatePlayPauseButton();
        return;
    }
#endif
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
#ifdef HAVE_FFMPEG
    if (usingFallbackVideo) {
        fallbackPaused = true;
        if (fallbackReader) {
            fallbackReader->setPaused(true);
            qint64 pos = positionSlider->value();
            qint64 dt = static_cast<qint64>(qRound64(frameDurationMs()));
            qint64 target = pos - dt; if (target < 0) target = 0;
            fallbackReader->seekToMs(target);
            fallbackReader->stepOnce();
        }
        updatePlayPauseButton();
        return;
    }
#endif
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
#ifdef HAVE_FFMPEG
    if (usingFallbackVideo && fallbackFps > 0.0) {
        return 1000.0 / fallbackFps;
    }
#endif
    double fps = detectedFps;
    if (fps <= 0.0) fps = 24.0;
    return 1000.0 / fps;
}

void PreviewOverlay::updateDetectedFps()
{
    detectedFps = 0.0;
    if (!isVideo) return;
#ifdef HAVE_FFMPEG
    if (usingFallbackVideo && fallbackFps > 0.0) { detectedFps = fallbackFps; return; }
#endif
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
#ifdef HAVE_FFMPEG
    if (usingFallbackVideo) {
        playPauseBtn->setIcon(fallbackPaused ? playIcon : pauseIcon);
        return;
    }
#endif
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

    const bool videoCase = false;

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
            // Navigate; showAsset will stop any prior playback safely
            navigatePrevious();
            break;
        case Qt::Key_Right:
            // CTRL+Right: Step forward one frame
            if (event->modifiers() & Qt::ControlModifier) {
                if (isVideo || isSequence) { onStepNextFrame(); return; }
                break;
            }
            // Navigate; showAsset will stop any prior playback safely
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

    // Refit content only when auto-fit is enabled
    if (fitToView) {
        if (!originalPixmap.isNull()) {
            // Images and fallback-video frames use the pixmap path
            fitImageToView();
        } else if (isVideo && videoItem && videoItem->isVisible()) {
            // Hardware video path
            imageView->fitInView(videoItem, Qt::KeepAspectRatio);
        }
    }

    // Float controls at the very bottom without reducing content area
    if (controlsWidget && controlsWidget->isVisible()) {
        controlsWidget->setGeometry(0, height() - controlsWidget->height(), width(), controlsWidget->height());
        controlsWidget->raise();
    }

    // Reposition nav arrows within their container on overlay resize
    if (navContainer) {
        positionNavButtons(navContainer);
    }
}

void PreviewOverlay::mousePressEvent(QMouseEvent *event)
{
    if (isVideo) {
        // Show controls on click
        controlsWidget->show();
        controlsWidget->setGeometry(0, height() - controlsWidget->height(), width(), controlsWidget->height());
        controlsWidget->raise();

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
    if ((isVideo && videoItem && videoItem->isVisible()) || (!isVideo && !originalPixmap.isNull())) {
        // Zoom with mouse wheel (fallback if event reached overlay)
        double factor = event->angleDelta().y() > 0 ? 1.15 : 0.85;
        zoomImage(factor);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

bool PreviewOverlay::eventFilter(QObject* watched, QEvent* event)
{
    // Handle wheel events for zoom (images and videos)
    if ((watched == imageView || (imageView && watched == imageView->viewport()))) {
        if (event->type() == QEvent::Wheel) {
            if ((isVideo && videoItem && videoItem->isVisible()) || (!isVideo && !originalPixmap.isNull())) {
                QWheelEvent* wheel = static_cast<QWheelEvent*>(event);
                double factor = wheel->angleDelta().y() > 0 ? 1.15 : 0.85;
                zoomImage(factor);
                wheel->accept();
                return true; // consume to prevent any scrolling
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            // Any user interaction starts manual zoom/pan mode
            fitToView = false;
        }
    }


    // Handle keyboard events from child widgets (imageView viewport, etc.)
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
    // User initiated zoom disables auto-fit
    fitToView = false;

    // Clamp factor so resulting zoom stays within bounds
    double newZoom = currentZoom * factor;
    if (newZoom < 0.1) {
        factor = 0.1 / qMax(0.0001, currentZoom);
        newZoom = 0.1;
    } else if (newZoom > 10.0) {
        factor = 10.0 / qMax(0.0001, currentZoom);
        newZoom = 10.0;
    }
    currentZoom = newZoom;

    if (!imageView) return;

    // Incremental scaling; with AnchorUnderMouse this zooms around the mouse position
    imageView->scale(factor, factor);
}

void PreviewOverlay::fitImageToView()
{
    if (!fitToView) return;
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
    currentZoom = 1.0;
    fitToView = true;
    fitImageToView();
}

void PreviewOverlay::showSequence(const QStringList &framePaths, const QString &sequenceName, int startFrame, int endFrame)
{
    isSequence = true;
    isVideo = false;
    // Reset sizing/fit for new content
    initialSized = false;
    fitToView = true;
    sequenceFramePaths = framePaths;
    sequenceStartFrame = startFrame;
    sequenceEndFrame = endFrame;
    currentSequenceFrame = 0;
    sequencePlaying = false;

    // Check if this is an HDR/EXR sequence
    if (!framePaths.isEmpty()) {
        QFileInfo fileInfo(framePaths.first());
        QString ext = fileInfo.suffix().toLower();
        isHDRImage = (ext == "exr" || ext == "hdr" || ext == "tif" || ext == "tiff" || ext == "psd");
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
    if (videoItem && videoItem->scene() == imageScene) imageScene->removeItem(videoItem);
    imageScene->clear();
    imageItem = nullptr;

    // Show image view and controls
    if (videoItem) videoItem->setVisible(false);
    imageView->show();
    controlsWidget->show();
    controlsWidget->setGeometry(0, height() - controlsWidget->height(), width(), controlsWidget->height());
    controlsWidget->raise();
    // Disable audio controls for image sequences (no audio)
    if (muteBtn) { muteBtn->setEnabled(false); muteBtn->setIcon(noAudioIcon); }
    if (volumeSlider) volumeSlider->setEnabled(false);


    // Stop video player if running
    if (mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        mediaPlayer->stop();
    }

    // Update file name label
    fileNameLabel->setText(sequenceName);

    // Show color space selector for sequences as requested (always visible)
    if (colorSpaceLabel) colorSpaceLabel->show();
    if (colorSpaceCombo) { colorSpaceCombo->show(); colorSpaceCombo->setEnabled(true); }

    // Clear cached frame visualization
    positionSlider->clearCachedFrames();

    // Initialize frame cache for this sequence (only if enabled)
    if (frameCache && useCacheForSequences) {
        frameCache->setSequence(framePaths, currentColorSpace);
        qDebug() << "[PreviewOverlay] Frame cache initialized for sequence with" << framePaths.size() << "frames";

        // Prepare to receive cache progress updates (disconnect to avoid duplicates)
        disconnect(frameCache, &SequenceFrameCache::frameCached, nullptr, nullptr);
        // We no longer paint cache on the slider; only the cache bar reflects caching

        // Update the separate cache bar as frames are cached
        connect(frameCache, &SequenceFrameCache::frameCached, this, [this](int frameIndex){
            if (cacheBar) {
                cacheBar->markFrameCached(frameIndex);
                cacheBar->show();
            }
        });
        // Start pre-fetching immediately (this will load frames in background)
        frameCache->startPrefetch(0);
        qDebug() << "[PreviewOverlay] Started pre-fetching frames from index 0";
    }

    // Initialize cache bar and kick off first frame load asynchronously
    if (cacheBar) {
        cacheBar->setTotalFrames(sequenceFramePaths.size());
        cacheBar->clearCachedFrames();
        cacheBar->show();
    }
    // Request first frame via cache/async path (non-blocking)
    loadSequenceFrame(0);

    // Update slider for sequence
    positionSlider->setRange(0, sequenceFramePaths.size() - 1);
    positionSlider->setValue(0);

    // Update time labels
    updateSequenceTimeDisplays(0);

    // Update play/pause button
    updatePlayPauseButton();

    // Show controls
    controlsWidget->show();
    controlsWidget->setGeometry(0, height() - controlsWidget->height(), width(), controlsWidget->height());
    controlsWidget->raise();
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
            // Pause playback (professional RAM player behavior)
            if (sequencePlaying) {
                sequenceTimer->stop();
            }

            // Keep showing previous frame
            // Update slider and time label anyway
            positionSlider->blockSignals(true);
            positionSlider->setValue(frameIndex);
            positionSlider->blockSignals(false);
            updateSequenceTimeDisplays(frameIndex, true);

            // Schedule a retry after a short delay to check if frame is ready
            QTimer::singleShot(50, this, [this, frameIndex]() {
                if (frameCache && frameCache->hasFrame(frameIndex)) {
                    // Resume playback
                    if (sequencePlaying) {
                        sequenceTimer->start();
                    }
                    // Load the frame now that it's ready
                    loadSequenceFrame(frameIndex);
                } else {
                    // Frame still not ready, schedule another retry
                    loadSequenceFrame(frameIndex);
                }
            });
            return;
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
        if (videoItem && videoItem->scene() == imageScene) imageScene->removeItem(videoItem);
        imageScene->clear();
        imageItem = imageScene->addPixmap(originalPixmap);
        imageScene->setSceneRect(originalPixmap.rect());

        // Determine alpha availability and reset toggle for new frame/sequence
        previewHasAlpha = originalPixmap.hasAlphaChannel();
        if (alphaCheck) { alphaCheck->setVisible(previewHasAlpha); alphaCheck->blockSignals(true); alphaCheck->setChecked(false); alphaOnlyMode = false; alphaCheck->blockSignals(false); }

        fitImageToView();
        // Initial window sizing to content (first frame)
        if (!initialSized) {
            const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
            const int contentW = originalPixmap.width();
            const int contentH = originalPixmap.height();
            int w = qMin(contentW + 40, avail.width() - 80);
            int h = qMin(50 + contentH + 120 + 40, avail.height() - 80);
            resize(w, h);
            // Center relative to screen
            const QPoint center = avail.center();
            move(center.x() - width()/2, center.y() - height()/2);
            initialSized = true;
        }
    } else {
        qWarning() << "[PreviewOverlay::loadSequenceFrame] Failed to load frame - pixmap is null!";
    }

    // Update slider and time label
    positionSlider->blockSignals(true);
    positionSlider->setValue(frameIndex);
    positionSlider->blockSignals(false);

    updateSequenceTimeDisplays(frameIndex);
}

void PreviewOverlay::playSequence()
{
    if (!isSequence || sequenceFramePaths.isEmpty()) {
        return;
    }

    sequencePlaying = true;
    sequenceTimer->start();
    updatePlayPauseButton();

    // Start pre-fetching frames ahead of current position (only if cache is enabled)
    if (frameCache && useCacheForSequences) {
        frameCache->startPrefetch(currentSequenceFrame);
        qDebug() << "[PreviewOverlay] Playing sequence at 24 fps with pre-fetching enabled";
    } else {
        qDebug() << "[PreviewOverlay] Playing sequence at 24 fps (cache disabled)";
    }
}

void PreviewOverlay::pauseSequence()
{
    sequencePlaying = false;
    sequenceTimer->stop();
    updatePlayPauseButton();

    // Keep pre-fetching running when paused so frames continue to load in background
    // This allows smooth scrubbing and instant resume

    qDebug() << "[PreviewOverlay] Paused sequence";
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

        // When looping, restart prefetch from frame 0 to ensure smooth playback
        if (frameCache && useCacheForSequences) {
            qDebug() << "[PreviewOverlay] Sequence looped to start, restarting prefetch";
            frameCache->startPrefetch(0);
        }
    }

    loadSequenceFrame(currentSequenceFrame);
}

void PreviewOverlay::onColorSpaceChanged(int index)
{
    qDebug() << "[PreviewOverlay] Color space changed to index:" << index;

    // Update current color space
    switch (index) {
        case 0: currentColorSpace = OIIOImageLoader::ColorSpace::Linear; break;
        case 1: currentColorSpace = OIIOImageLoader::ColorSpace::sRGB;  break;
        case 2: currentColorSpace = OIIOImageLoader::ColorSpace::Rec709; break;
        default: currentColorSpace = OIIOImageLoader::ColorSpace::sRGB; break;
    }

    // Reload current frame/image with new color space
    if (isSequence) {
        if (frameCache && useCacheForSequences) {
            frameCache->setSequence(sequenceFramePaths, currentColorSpace);
        }
        loadSequenceFrame(currentSequenceFrame);
    } else if (!currentFilePath.isEmpty() && isHDRImage) {
        showImage(currentFilePath);
    } else if (isVideo) {
        // Re-render last frame for current pipeline (QVideoSink or FFmpeg)
    #ifdef HAVE_FFMPEG
        if (usingFallbackVideo && !lastFallbackFrameRaw.isNull()) {
            // Reuse fallback rendering path for current frame
            onFallbackFrameReady(lastFallbackFrameRaw, positionSlider ? positionSlider->value() : 0);
            return;
        }
    #endif
        if (!lastVideoFrameRaw.isNull()) {
            // Apply current color transform to the last received hardware frame at full resolution
            QImage out = lastVideoFrameRaw;
            auto toLinear709 = [](float v){ return (v < 0.081f) ? (v / 4.5f) : std::pow((v + 0.099f) / 1.099f, 1.0f/0.45f); };
            auto linearToSRGB = [](float v){ v = std::clamp(v, 0.0f, 1.0f); return (v <= 0.0031308f) ? 12.92f*v : 1.055f*std::pow(v, 1.0f/2.4f) - 0.055f; };
            if (currentColorSpace != OIIOImageLoader::ColorSpace::Rec709) {
                out = out.convertToFormat(QImage::Format_RGBA8888);
                const int w = out.width(), h = out.height();
                for (int y = 0; y < h; ++y) {
                    uchar* p = out.scanLine(y);
                    for (int x = 0; x < w; ++x) {
                        float r = p[4*x+0] / 255.0f;
                        float g = p[4*x+1] / 255.0f;
                        float b = p[4*x+2] / 255.0f;
                        r = toLinear709(r); g = toLinear709(g); b = toLinear709(b);
                        if (currentColorSpace == OIIOImageLoader::ColorSpace::sRGB) {
                            r = linearToSRGB(r); g = linearToSRGB(g); b = linearToSRGB(b);
                        } else { // Linear target
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
            if (!imageItem) { imageItem = imageScene->addPixmap(QPixmap::fromImage(out)); }
            else { imageItem->setPixmap(QPixmap::fromImage(out)); }
            imageScene->setSceneRect(imageItem->boundingRect());
            if (fitToView && imageView) { imageView->resetTransform(); imageView->fitInView(imageItem, Qt::KeepAspectRatio); }
        }
        return;
    }
}




void PreviewOverlay::stopPlayback()
{
    qDebug() << "[PreviewOverlay] Stopping playback";

    // Stop video playback
    if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
        mediaPlayer->stop();
    }

#ifdef HAVE_FFMPEG
    // Stop fallback playback if active
    if (usingFallbackVideo) {
        stopFallbackVideo();
    }
#endif

    // Stop sequence playback
    if (sequencePlaying) {
        pauseSequence();
    }

    // Stop pre-fetching but keep cache intact (only if cache is enabled)
    if (frameCache && useCacheForSequences) {
        frameCache->stopPrefetch();
    }

    // Clear the media source to release the file
    mediaPlayer->setSource(QUrl());
}



#ifdef HAVE_FFMPEG
void PreviewOverlay::startFallbackVideo(const QString &filePath)
{
    if (usingFallbackVideo) {
        stopFallbackVideo();
    }

    qDebug() << "[PreviewOverlay] Starting FFmpeg software playback for" << filePath;

    // Stop and hide the video widget path
    mediaPlayer->stop();
    if (videoItem) videoItem->setVisible(false);
    imageView->show();
    controlsWidget->show();
    controlsWidget->setGeometry(0, height() - controlsWidget->height(), width(), controlsWidget->height());
    controlsWidget->raise();
    positionNavButtons(imageView->viewport());
    // Videos: keep color controls visible but disable transforms (force Rec.709)
    currentColorSpace = OIIOImageLoader::ColorSpace::Rec709;
    if (colorSpaceLabel) colorSpaceLabel->show();
    if (colorSpaceCombo) { colorSpaceCombo->show(); colorSpaceCombo->setCurrentIndex(2); colorSpaceCombo->setEnabled(true); }
    if (alphaCheck) alphaCheck->hide();

    // Reset zoom/pan for new clip
    lastVideoPixmapSize = QSize();
    if (imageView) imageView->resetTransform();


    // Prepare scene for software frames: keep videoItem in scene (hidden) to avoid black screen when switching back
    if (imageItem) {
        if (imageItem->scene() == imageScene) imageScene->removeItem(imageItem);
        delete imageItem;
        imageItem = nullptr;
    }
    originalPixmap = QPixmap();

    // Probe duration and fps for UI
    fallbackDurationMs = 0;
    fallbackFps = 24.0;
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, filePath.toUtf8().constData(), nullptr, nullptr) == 0) {
        if (avformat_find_stream_info(fmt, nullptr) == 0) {
            int vIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (vIdx >= 0) {
                AVStream* vs = fmt->streams[vIdx];
                AVRational r = vs->avg_frame_rate.num > 0 ? vs->avg_frame_rate : vs->r_frame_rate;
                if (r.num > 0 && r.den > 0) fallbackFps = static_cast<double>(r.num) / r.den;
            }
            if (fmt->duration > 0) {
                fallbackDurationMs = static_cast<qint64>((fmt->duration * 1000) / AV_TIME_BASE);
            }
        }
        avformat_close_input(&fmt);
    }

    if (fallbackDurationMs > 0) {
        positionSlider->setRange(0, fallbackDurationMs);
        updateVideoTimeDisplays(0, fallbackDurationMs);
    } else {
        // Unknown duration
        positionSlider->setRange(0, 0);
        updateVideoTimeDisplays(0, -1);
    }

    // Spin up worker thread
    fallbackThread = new QThread();
    {
        QSettings s("AugmentCode", "KAssetManager");
        const bool drop = s.value("Playback/DropLateFrames", true).toBool();
        fallbackReader = new FfmpegVideoReader(filePath, drop);
    }
    fallbackReader->moveToThread(fallbackThread);

    connect(fallbackThread, &QThread::started, fallbackReader, &FfmpegVideoReader::start);
    connect(fallbackReader, &FfmpegVideoReader::frameReady, this, &PreviewOverlay::onFallbackFrameReady, Qt::QueuedConnection);
    connect(fallbackReader, &FfmpegVideoReader::finished, this, &PreviewOverlay::onFallbackFinished, Qt::QueuedConnection);
    connect(fallbackReader, &FfmpegVideoReader::finished, fallbackThread, &QThread::quit);
    connect(fallbackThread, &QThread::finished, fallbackReader, &QObject::deleteLater);
    connect(fallbackThread, &QThread::finished, fallbackThread, &QObject::deleteLater);

    usingFallbackVideo = true;
    // Ensure reader and thread will stop on overlay destruction as a last resort
    connect(this, &QObject::destroyed, fallbackReader, &FfmpegVideoReader::stop, Qt::DirectConnection);
    connect(this, &QObject::destroyed, fallbackThread, &QThread::quit);
    fallbackThread->start();
}

void PreviewOverlay::stopFallbackVideo()
{
    if (!usingFallbackVideo) return;
    usingFallbackVideo = false;

    if (fallbackReader) {
        // Disconnect to avoid queued frames landing after teardown
        disconnect(fallbackReader, nullptr, this, nullptr);
        // Set stop flag immediately (atomic) without relying on the worker event loop
        fallbackReader->stop(); // thread-safe: sets std::atomic<bool>
    }
    if (fallbackThread) {
        fallbackThread->quit();
        // Give the worker time to exit its loop and close input
        fallbackThread->wait(3000);
    }
    fallbackReader = nullptr;
    fallbackThread = nullptr;
}
#endif // HAVE_FFMPEG

void PreviewOverlay::onPlayerError(QMediaPlayer::Error error, const QString &errorString)
{
    qWarning() << "[PreviewOverlay] Media player error:" << error << errorString;
    // Deterministic routing is done before playback; do not auto-fallback here to avoid double tries.
    Q_UNUSED(error);
    Q_UNUSED(errorString);
}

void PreviewOverlay::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    Q_UNUSED(status);
    // Update FPS when metadata becomes available
    updateDetectedFps();
}

void PreviewOverlay::onFallbackFrameReady(const QImage &image, qint64 ptsMs)
{
#ifdef HAVE_FFMPEG
    // Ignore frames from stale fallback readers
    if (!usingFallbackVideo || sender() != fallbackReader) {
        return;
    }
#endif
    // Store raw frame (assumed Rec.709 source)
    lastFallbackFrameRaw = image;

    // Apply current color transform to fallback video frames
    QImage out = image;
    auto toLinear709 = [](float v){ return (v < 0.081f) ? (v / 4.5f) : std::pow((v + 0.099f) / 1.099f, 1.0f/0.45f); };
    auto linearToSRGB = [](float v){ v = std::clamp(v, 0.0f, 1.0f); return (v <= 0.0031308f) ? 12.92f*v : 1.055f*std::pow(v, 1.0f/2.4f) - 0.055f; };
    if (currentColorSpace != OIIOImageLoader::ColorSpace::Rec709) {
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
                if (currentColorSpace == OIIOImageLoader::ColorSpace::sRGB) {
                    r = linearToSRGB(r); g = linearToSRGB(g); b = linearToSRGB(b);
                } else { // Linear target
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

    // Update pixmap in the image scene
    originalPixmap = QPixmap::fromImage(out);

    // Determine alpha for fallback video frame display is irrelevant; hide toggle for videos
    if (alphaCheck) alphaCheck->hide();

    if (!imageItem) {
        imageItem = imageScene->addPixmap(originalPixmap);
    } else {
        imageItem->setPixmap(originalPixmap);
    }
    // Ensure scene rect matches content so fit calculations are correct
    imageScene->setSceneRect(originalPixmap.rect());

    // On the first frame after switching content, always refit so the new clip isn't zoomed
    if (!initialSized) {
        imageView->resetTransform();
        if (fitToView) imageView->fitInView(imageItem, Qt::KeepAspectRatio);
    }

    // Fit only when size changes to maintain realtime playback
    if (imageView && fitToView) {
        const QSize sz = out.size();
        if (sz != lastVideoPixmapSize) {
            lastVideoPixmapSize = sz;
            imageView->resetTransform();
            imageView->fitInView(imageItem, Qt::KeepAspectRatio);
        }
    }

    // Size window on first frame
    if (!initialSized) {
        const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
        const int contentW = originalPixmap.width();
        const int contentH = originalPixmap.height();
        int w = qMin(contentW + 40, avail.width() - 80);
        int h = qMin(50 + contentH + 120 + 40, avail.height() - 80);
        resize(w, h);
        const QPoint center = avail.center();
        move(center.x() - width()/2, center.y() - height()/2);
        initialSized = true;
    }

    // Update UI: always refresh time labels; only set the slider thumb if the user isn't dragging
    const bool seekingNow = (positionSlider && (positionSlider->isSliderDown() || userSeeking));
    if (!seekingNow) {
        if (positionSlider) positionSlider->setValue(static_cast<int>(ptsMs));
    }
    if (fallbackDurationMs > 0) {
        updateVideoTimeDisplays(ptsMs, fallbackDurationMs);
    } else {
        updateVideoTimeDisplays(ptsMs, -1);
    }
}

void PreviewOverlay::onFallbackFinished()
{
#ifdef HAVE_FFMPEG
    // Ignore finish from stale readers
    if (sender() != fallbackReader) {
        return;
    }
#endif
    qDebug() << "[PreviewOverlay] Fallback playback finished";
    stopFallbackVideo();
}

void PreviewOverlay::showText(const QString &filePath)
{
    // Hide other content
    if (videoItem) videoItem->setVisible(false);
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
    if (videoItem) videoItem->setVisible(false);
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
    if (videoItem) videoItem->setVisible(false);
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
    if (videoItem) videoItem->setVisible(false);
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
    if (videoItem) videoItem->setVisible(false);
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
        if (videoItem && videoItem->scene() == imageScene) imageScene->removeItem(videoItem);
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
    // Do not block UI waiting for worker threads. Mark all in-flight tasks as cancelled
    // and let them wind down naturally.
    stopPrefetch();
    clearCache();
}

void SequenceFrameCache::setSequence(const QStringList &framePaths, OIIOImageLoader::ColorSpace colorSpace)
{
    QMutexLocker locker(&m_mutex);
    stopPrefetch();
    clearCache();
    m_framePaths = framePaths;
    m_colorSpace = colorSpace;
    m_currentFrame = 0;
    qDebug() << "[SequenceFrameCache] Set sequence with" << framePaths.size() << "frames";
}

void SequenceFrameCache::clearCache()
{
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_pendingFrames.clear();
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

    // Only clean up cache if it's getting close to the limit
    // This prevents aggressive cleanup during normal playback
    int currentCacheSize = m_cache.count();
    int cacheThreshold = static_cast<int>(m_maxCacheSize * 0.9); // 90% of max

    if (currentCacheSize >= cacheThreshold) {
        // Remove frames that are too far behind the current position (sliding window)
        // Scale the window based on max cache size
        // Keep 40% of cache behind, 60% ahead for smooth forward playback
        const int behindWindow = static_cast<int>(m_maxCacheSize * 0.4);
        const int aheadWindow = static_cast<int>(m_maxCacheSize * 0.6);

        // Remove frames that are outside the window
        QList<int> keysToRemove;
        for (int i = 0; i < m_framePaths.size(); ++i) {
            if (m_cache.contains(i)) {
                // Remove if too far behind or too far ahead
                if (i < frameIndex - behindWindow || i > frameIndex + aheadWindow) {
                    keysToRemove.append(i);
                }
            }
        }

        // Remove old frames
        for (int key : keysToRemove) {
            m_cache.remove(key);
        }

        // Frames removed silently to avoid log spam
        Q_UNUSED(keysToRemove);
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

    // Pre-fetch frames ahead of current position for smooth playback
    // Scale prefetch count based on max cache size (60% of cache for forward playback)
    const int prefetchCount = static_cast<int>(m_maxCacheSize * 0.6);

    for (int i = 0; i <= prefetchCount; ++i) {
        int frameIndex = startFrame + i;

        // Stop if we've reached the end
        if (frameIndex >= m_framePaths.size()) {
            break;
        }

        // Skip if already cached or being loaded
        if (m_cache.contains(frameIndex) || m_pendingFrames.contains(frameIndex)) {
            continue;
        }

        // Mark as pending
        m_pendingFrames.insert(frameIndex);

        // Create worker to load frame in background
        QString framePath = m_framePaths[frameIndex];
        FrameLoaderWorker *worker = new FrameLoaderWorker(this, frameIndex, framePath, m_colorSpace, epoch);

        // Use Qt::AutoConnection - Qt will automatically choose the right connection type
        // This ensures proper thread safety without blocking
        connect(worker, &FrameLoaderWorker::frameLoaded, this, [this](int idx, QPixmap pixmap) {
            QMutexLocker locker(&m_mutex);
            m_pendingFrames.remove(idx);

            if (!pixmap.isNull() && m_prefetchActive) {
                // Insert into cache
                int cost = pixmap.width() * pixmap.height() * 4 / 1024; // Cost in KB

                // Check if cache is full before inserting
                int currentCost = m_cache.totalCost();
                int maxCost = m_cache.maxCost();
                m_cache.insert(idx, new QPixmap(pixmap), cost);
                emit frameCached(idx);
            } else if (pixmap.isNull()) {
                qWarning() << "[SequenceFrameCache] Failed to load frame" << idx;
            }
        }, Qt::AutoConnection);

        m_threadPool->start(worker);
    }
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


