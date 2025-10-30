#include "preview_overlay.h"
#include "oiio_image_loader.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QFileInfo>
#include <QScrollArea>
#include <QStyle>
#include <QApplication>
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

#include <QVector>

#include "office_preview.h"

#include <QGraphicsSvgItem>
#if defined(Q_OS_WIN) && defined(HAVE_QT_AX)
#include <QAxObject>
#endif

#include <atomic>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <QImage>
#include <QRegularExpression>

// Worker that reads PNG-coded frames from a MOV and emits QImage frames
class PreviewOverlay::FallbackPngMovReader : public QObject {
    Q_OBJECT
public:
    explicit FallbackPngMovReader(const QString& path)
        : m_path(path) {}

public slots:
    void start() {
        if (!open()) { emit finished(); return; }
        const double intervalMs = m_fps > 0.0 ? (1000.0 / m_fps) : (1000.0 / 24.0);
        AVPacket pkt; av_init_packet(&pkt);
        const uint8_t sig[8] = {0x89, 'P','N','G', 0x0D, 0x0A, 0x1A, 0x0A};
        qint64 lastPtsMs = 0;
        while (!m_stop) {
            if (m_paused) { QThread::msleep(10); continue; }
            if (av_read_frame(m_fmt, &pkt) < 0) {
                break; // EOF
            }
            if (pkt.stream_index == m_vIdx && pkt.size >= 8 && pkt.data) {
                int limit = pkt.size - 8;
                int pos = -1;
                for (int i = 0; i <= limit; ++i) {
                    if (pkt.data[i] == sig[0]) {
                        bool match = true;
                        for (int j = 1; j < 8; ++j) {
                            if (pkt.data[i + j] != sig[j]) { match = false; break; }
                        }
                        if (match) { pos = i; break; }
                    }
                }
                if (pos >= 0) {
                    QByteArray bytes(reinterpret_cast<const char*>(pkt.data + pos), pkt.size - pos);
                    QImage img = QImage::fromData(bytes, "PNG");
                    if (!img.isNull()) {
                        qint64 ptsMs = 0;
                        if (pkt.pts != AV_NOPTS_VALUE) {
                            AVRational ms = {1, 1000};
                            ptsMs = av_rescale_q(pkt.pts, m_stream->time_base, ms);
                        } else {
                            ptsMs = lastPtsMs + static_cast<qint64>(intervalMs);
                        }
                        lastPtsMs = ptsMs;
                        emit frameReady(img, ptsMs);
                        // Pace roughly to FPS; keep it simple
                        QThread::msleep(static_cast<unsigned long>(intervalMs));
                    }
                }
            }
            av_packet_unref(&pkt);
        }
        avformat_close_input(&m_fmt);
        emit finished();
    }
    void stop() { m_stop = true; }
    void setPaused(bool p) { m_paused = p; }

signals:
    void frameReady(const QImage& image, qint64 ptsMs);
    void finished();

public:
    double fps() const { return m_fps; }
    qint64 durationMs() const { return m_durationMs; }

private:
    bool open() {
        if (avformat_open_input(&m_fmt, m_path.toUtf8().constData(), nullptr, nullptr) < 0) {
            return false;
        }
        if (avformat_find_stream_info(m_fmt, nullptr) < 0) {
            avformat_close_input(&m_fmt);
            return false;
        }
        int vIdx = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (vIdx < 0) {
            avformat_close_input(&m_fmt);
            return false;
        }
        m_vIdx = vIdx;
        m_stream = m_fmt->streams[m_vIdx];
        AVRational r = m_stream->avg_frame_rate.num > 0 ? m_stream->avg_frame_rate : m_stream->r_frame_rate;
        m_fps = (r.num > 0 && r.den > 0) ? (static_cast<double>(r.num) / r.den) : 24.0;
        if (m_fmt->duration > 0) {
            m_durationMs = static_cast<qint64>( (m_fmt->duration * 1000) / AV_TIME_BASE );
        } else {
            m_durationMs = 0;
        }
        // Rewind to start for clean playback
        av_seek_frame(m_fmt, m_vIdx, 0, AVSEEK_FLAG_BACKWARD);
        return true;
    }

    QString m_path;
    AVFormatContext* m_fmt = nullptr;
    AVStream* m_stream = nullptr;
    int m_vIdx = -1;
    double m_fps = 24.0;
    qint64 m_durationMs = 0;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_paused{false};
};
#endif // HAVE_FFMPEG

PreviewOverlay::PreviewOverlay(QWidget *parent)
    : QWidget(parent)
    , imageView(nullptr)
    , imageScene(nullptr)
    , imageItem(nullptr)
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
{
    setupUi();
    setFocusPolicy(Qt::StrongFocus);

    // Auto-hide controls timer
    controlsTimer = new QTimer(this);
    controlsTimer->setSingleShot(true);
    controlsTimer->setInterval(3000);
    connect(controlsTimer, &QTimer::timeout, this, &PreviewOverlay::hideControls);

    // Sequence playback timer (24 fps default)
    sequenceTimer = new QTimer(this);
    sequenceTimer->setInterval(1000 / 24); // 24 fps
    connect(sequenceTimer, &QTimer::timeout, this, &PreviewOverlay::onSequenceTimerTick);
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
}

void PreviewOverlay::setupUi()
{
    // Full-screen black background
    setStyleSheet("QWidget { background-color: #000000; }");
    setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
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

    // Video widget (for videos)
    videoWidget = new QVideoWidget(this);
    videoWidget->hide();
    contentLayout->addWidget(videoWidget);

    mainLayout->addWidget(contentWidget, 1);

    // Bottom controls (for video)
    controlsWidget = new QWidget(this);
    controlsWidget->setStyleSheet("QWidget { background-color: rgba(0, 0, 0, 180); }");
    controlsWidget->setFixedHeight(80);
    controlsWidget->hide();

    QVBoxLayout *controlsLayout = new QVBoxLayout(controlsWidget);
    controlsLayout->setContentsMargins(20, 10, 20, 10);

    // Position slider
    positionSlider = new QSlider(Qt::Horizontal, this);
    positionSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #555; height: 4px; }"
        "QSlider::handle:horizontal { background: #58a6ff; width: 12px; margin: -4px 0; border-radius: 6px; }"
    );
    connect(positionSlider, &QSlider::sliderMoved, this, &PreviewOverlay::onSliderMoved);
    controlsLayout->addWidget(positionSlider);

    // Control buttons row
    QHBoxLayout *buttonsLayout = new QHBoxLayout();

    playPauseBtn = new QPushButton("▶", this);
    playPauseBtn->setFixedSize(40, 40);
    playPauseBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: white; font-size: 18px; "
        "border-radius: 20px; border: none; }"
        "QPushButton:hover { background-color: #4a90e2; }"
    );
    connect(playPauseBtn, &QPushButton::clicked, this, &PreviewOverlay::onPlayPauseClicked);
    buttonsLayout->addWidget(playPauseBtn);

    timeLabel = new QLabel("0:00 / 0:00", this);
    timeLabel->setStyleSheet("QLabel { color: white; font-size: 14px; padding: 0 10px; }");
    buttonsLayout->addWidget(timeLabel);

    buttonsLayout->addStretch();

    // Color space selector (for HDR/EXR images)
    colorSpaceLabel = new QLabel("Color Space:", this);
    colorSpaceLabel->setStyleSheet("QLabel { color: white; font-size: 14px; padding: 0 5px; }");
    colorSpaceLabel->hide();
    buttonsLayout->addWidget(colorSpaceLabel);

    colorSpaceCombo = new QComboBox(this);
    colorSpaceCombo->addItem("Linear");
    colorSpaceCombo->addItem("sRGB");
    colorSpaceCombo->addItem("Rec.709");
    colorSpaceCombo->setCurrentIndex(1); // Default to sRGB
    colorSpaceCombo->setStyleSheet(
        "QComboBox { background-color: #333; color: white; border: 1px solid #555; "
        "padding: 5px; border-radius: 3px; min-width: 100px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: none; border: none; }"
        "QComboBox QAbstractItemView { background-color: #333; color: white; "
        "selection-background-color: #58a6ff; }"
    );
    colorSpaceCombo->hide();
    connect(colorSpaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreviewOverlay::onColorSpaceChanged);
    buttonsLayout->addWidget(colorSpaceCombo);

    buttonsLayout->addSpacing(20);

    // Volume control
    QLabel *volumeIcon = new QLabel("🔊", this);
    volumeIcon->setStyleSheet("QLabel { color: white; font-size: 16px; }");
    buttonsLayout->addWidget(volumeIcon);

    volumeSlider = new QSlider(Qt::Horizontal, this);
    volumeSlider->setFixedWidth(100);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(50);
    volumeSlider->setStyleSheet(positionSlider->styleSheet());
    connect(volumeSlider, &QSlider::valueChanged, this, &PreviewOverlay::onVolumeChanged);
    buttonsLayout->addWidget(volumeSlider);

    controlsLayout->addLayout(buttonsLayout);

    mainLayout->addWidget(controlsWidget);

    // Initialize media player
    mediaPlayer = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    mediaPlayer->setAudioOutput(audioOutput);
    mediaPlayer->setVideoOutput(videoWidget);

    connect(mediaPlayer, &QMediaPlayer::positionChanged, this, &PreviewOverlay::onPositionChanged);
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
    QStringList videoFormats = {"mp4", "avi", "mov", "mkv", "webm", "flv", "wmv", "m4v"};
    isVideo = videoFormats.contains(currentFileType);

    // Make sure widget is shown and sized before loading content
    show();
    raise();
    setFocus();

    // Process events to ensure window is properly sized
    QApplication::processEvents();

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
    qDebug() << "[PreviewOverlay::showImage] Loading image:" << filePath;

    // CRITICAL: Hide other widgets and show image view
    if (videoWidget) videoWidget->hide();
    if (textView) textView->hide();
    if (tableView) tableView->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    imageView->setBackgroundBrush(QColor("#0a0a0a"));
    imageView->show();

    // CRITICAL: Stop any video playback
    if (mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        qDebug() << "[PreviewOverlay::showImage] Stopping video playback";
        mediaPlayer->stop();
    }
#ifdef HAVE_FFMPEG
    if (usingFallbackVideo) {
        qDebug() << "[PreviewOverlay::showImage] Stopping fallback playback";
        stopFallbackVideo();
    }
#endif

    // Check if this is an HDR/EXR image
    QFileInfo fileInfo(filePath);
    QString ext = fileInfo.suffix().toLower();
    isHDRImage = (ext == "exr" || ext == "hdr" || ext == "pic");

    // Try loading with OpenImageIO first for advanced formats
    QImage image;
    QPixmap newPixmap;
    if (OIIOImageLoader::isOIIOSupported(filePath)) {
        qDebug() << "[PreviewOverlay::showImage] Loading with OpenImageIO:" << filePath;
        // Load at full resolution for preview (no size limit) with current color space
        image = OIIOImageLoader::loadImage(filePath, 0, 0, currentColorSpace);
        if (!image.isNull()) {
            newPixmap = QPixmap::fromImage(image);
            qDebug() << "[PreviewOverlay::showImage] OIIO loaded successfully, size:" << newPixmap.size();
        } else {
            qWarning() << "[PreviewOverlay::showImage] OIIO failed to load:" << filePath;
        }
    }

    // Fall back to Qt's native loader if OIIO didn't work or isn't supported
    if (newPixmap.isNull()) {
        qDebug() << "[PreviewOverlay::showImage] Loading with Qt native loader:" << filePath;
        newPixmap = QPixmap(filePath);
        isHDRImage = false; // Qt loader doesn't support HDR
    }


    if (!newPixmap.isNull()) {
        qDebug() << "[PreviewOverlay::showImage] Loaded new pixmap, size:" << newPixmap.size();

        // CRITICAL: Update the pixmap on existing item or create new one
        if (imageItem) {
            qDebug() << "[PreviewOverlay::showImage] Updating existing item with setPixmap()";
            imageItem->setPixmap(newPixmap);
        } else {
            qDebug() << "[PreviewOverlay::showImage] Creating new graphics item";
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

        // CRITICAL: Force complete view refresh
        imageView->viewport()->update();
        imageView->update();
        imageScene->update();
        QApplication::processEvents(); // Force immediate processing
        qDebug() << "[PreviewOverlay::showImage] Image displayed successfully";

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
}

void PreviewOverlay::showVideo(const QString &filePath)
{
    qDebug() << "[PreviewOverlay::showVideo] Loading video:" << filePath;

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

    // CRITICAL: Hide other content and show video widget
    if (imageView) imageView->hide();
    if (textView) textView->hide();
    if (tableView) tableView->hide();
#ifdef HAVE_QT_PDF
    if (pdfView) pdfView->hide();
#endif
    videoWidget->show();
    controlsWidget->show();

    // Hide alpha toggle for videos
    if (alphaCheck) alphaCheck->hide();

    // CRITICAL: Clear the scene to free memory
    imageScene->clear();
    imageItem = nullptr;
    originalPixmap = QPixmap(); // Clear the pixmap

    qDebug() << "[PreviewOverlay::showVideo] Setting video source";
    mediaPlayer->setSource(QUrl::fromLocalFile(filePath));

    // Set video to fill the widget while maintaining aspect ratio
    videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);

    qDebug() << "[PreviewOverlay::showVideo] Starting playback";
    mediaPlayer->play();

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
                QMetaObject::invokeMethod(fallbackReader, "setPaused", Qt::QueuedConnection, Q_ARG(bool, fallbackPaused));
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
    timeLabel->setText(QString("%1 / %2").arg(formatTime(position)).arg(formatTime(duration)));
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
    } else {
#ifdef HAVE_FFMPEG
        if (usingFallbackVideo) {
            // Seeking not supported in fallback mode yet
            return;
        }
#endif
        // Seek in video
        mediaPlayer->setPosition(position);
    }
    controlsTimer->start();
}

void PreviewOverlay::onVolumeChanged(int value)
{
    audioOutput->setVolume(value / 100.0);
    controlsTimer->start();
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
        playPauseBtn->setText(fallbackPaused ? "▶" : "⏸");
        return;
    }
#endif
    if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
        playPauseBtn->setText("⏸");
    } else {
        playPauseBtn->setText("▶");
    }
}

QString PreviewOverlay::formatTime(qint64 milliseconds)
{
    int seconds = milliseconds / 1000;
    int minutes = seconds / 60;
    seconds = seconds % 60;
    return QString("%1:%2").arg(minutes).arg(seconds, 2, 10, QChar('0'));
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
            // Always stop any playback before navigating to avoid mixing
            stopPlayback();
            navigatePrevious();
            break;
        case Qt::Key_Right:
            // Always stop any playback before navigating to avoid mixing
            stopPlayback();
            navigateNext();
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
}

void PreviewOverlay::mousePressEvent(QMouseEvent *event)
{
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
    if (!isVideo && !originalPixmap.isNull()) {
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
    if ((watched == imageView || (imageView && watched == imageView->viewport())) && event->type() == QEvent::Wheel) {
        if (!isVideo && !originalPixmap.isNull()) {
            QWheelEvent* wheel = static_cast<QWheelEvent*>(event);
            double factor = wheel->angleDelta().y() > 0 ? 1.15 : 0.85;
            zoomImage(factor);
            wheel->accept();
            return true; // consume to prevent any scrolling
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
    currentZoom = 1.0;
    imageView->resetTransform();
    imageView->centerOn(imageItem);
}

void PreviewOverlay::showSequence(const QStringList &framePaths, const QString &sequenceName, int startFrame, int endFrame)
{
    qDebug() << "[PreviewOverlay::showSequence] Showing sequence:" << sequenceName << "frames:" << startFrame << "-" << endFrame;

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

    // Process events to ensure window is properly sized
    QApplication::processEvents();

    // CRITICAL: Clear scene before loading sequence
    qDebug() << "[PreviewOverlay::showSequence] Clearing scene";
    imageScene->clear();
    imageItem = nullptr;

    // Show image view and controls
    videoWidget->hide();
    imageView->show();
    controlsWidget->show();

    // Stop video player if running
    if (mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        qDebug() << "[PreviewOverlay::showSequence] Stopping video playback";
        mediaPlayer->stop();
    }

    // Update file name label
    fileNameLabel->setText(sequenceName);

    // Show/hide color space selector based on whether this is HDR
    if (isHDRImage) {
        colorSpaceLabel->show();
        colorSpaceCombo->show();
    } else {
        colorSpaceLabel->hide();
        colorSpaceCombo->hide();
    }

    // Load first frame
    if (!sequenceFramePaths.isEmpty()) {
        loadSequenceFrame(0);
    }

    // Update slider for sequence
    positionSlider->setRange(0, sequenceFramePaths.size() - 1);
    positionSlider->setValue(0);

    // Update time label
    timeLabel->setText(QString("Frame %1 / %2").arg(startFrame).arg(endFrame));

    // Update play/pause button
    updatePlayPauseButton();

    // Show controls
    controlsWidget->show();
    controlsTimer->start();
}

void PreviewOverlay::loadSequenceFrame(int frameIndex)
{
    qDebug() << "[PreviewOverlay::loadSequenceFrame] Called with frameIndex:" << frameIndex << "total frames:" << sequenceFramePaths.size();

    if (frameIndex < 0 || frameIndex >= sequenceFramePaths.size()) {
        qWarning() << "[PreviewOverlay::loadSequenceFrame] Invalid frame index:" << frameIndex;
        return;
    }

    currentSequenceFrame = frameIndex;
    QString framePath = sequenceFramePaths[frameIndex];

    qDebug() << "[PreviewOverlay::loadSequenceFrame] Loading frame:" << framePath;

    // Load frame with OpenImageIO if supported
    QImage image;
    if (OIIOImageLoader::isOIIOSupported(framePath)) {
        qDebug() << "[PreviewOverlay::loadSequenceFrame] Using OIIO to load frame with color space";
        image = OIIOImageLoader::loadImage(framePath, 0, 0, currentColorSpace);
        if (!image.isNull()) {
            originalPixmap = QPixmap::fromImage(image);
            qDebug() << "[PreviewOverlay::loadSequenceFrame] OIIO loaded successfully, size:" << originalPixmap.size();
        } else {
            qWarning() << "[PreviewOverlay::loadSequenceFrame] OIIO failed to load frame";
        }
    }

    // Fall back to Qt loader
    if (originalPixmap.isNull()) {
        qDebug() << "[PreviewOverlay::loadSequenceFrame] Using Qt to load frame";
        originalPixmap = QPixmap(framePath);
        if (!originalPixmap.isNull()) {
            qDebug() << "[PreviewOverlay::loadSequenceFrame] Qt loaded successfully, size:" << originalPixmap.size();
        } else {
            qWarning() << "[PreviewOverlay::loadSequenceFrame] Qt failed to load frame";
        }
    }

    if (!originalPixmap.isNull()) {
        qDebug() << "[PreviewOverlay::loadSequenceFrame] Adding pixmap to scene";
        imageScene->clear();
        imageItem = imageScene->addPixmap(originalPixmap);
        imageScene->setSceneRect(originalPixmap.rect());

        // Determine alpha availability and reset toggle for new frame/sequence
        previewHasAlpha = originalPixmap.hasAlphaChannel();
        if (alphaCheck) { alphaCheck->setVisible(previewHasAlpha); alphaCheck->blockSignals(true); alphaCheck->setChecked(false); alphaOnlyMode = false; alphaCheck->blockSignals(false); }

        fitImageToView();
        qDebug() << "[PreviewOverlay::loadSequenceFrame] Frame loaded and displayed";
    } else {
        qWarning() << "[PreviewOverlay::loadSequenceFrame] Failed to load frame - pixmap is null!";


    }

    // Update slider and time label
    positionSlider->blockSignals(true);
    positionSlider->setValue(frameIndex);
    positionSlider->blockSignals(false);

    int actualFrame = sequenceStartFrame + frameIndex;
    timeLabel->setText(QString("Frame %1 / %2").arg(actualFrame).arg(sequenceEndFrame));

    qDebug() << "[PreviewOverlay::loadSequenceFrame] Frame loading complete";
}

void PreviewOverlay::playSequence()
{
    if (!isSequence || sequenceFramePaths.isEmpty()) {
        return;
    }

    sequencePlaying = true;
    sequenceTimer->start();
    updatePlayPauseButton();
    qDebug() << "[PreviewOverlay] Playing sequence at 24 fps";
}

void PreviewOverlay::pauseSequence()
{
    sequencePlaying = false;
    sequenceTimer->stop();
    updatePlayPauseButton();
    qDebug() << "[PreviewOverlay] Paused sequence";
}

void PreviewOverlay::stopSequence()
{
    sequencePlaying = false;
    sequenceTimer->stop();
    currentSequenceFrame = 0;
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
    }

    loadSequenceFrame(currentSequenceFrame);
}

void PreviewOverlay::onColorSpaceChanged(int index)
{
    qDebug() << "[PreviewOverlay] Color space changed to index:" << index;

    // Update current color space
    switch (index) {
        case 0:
            currentColorSpace = OIIOImageLoader::ColorSpace::Linear;
            qDebug() << "[PreviewOverlay] Switched to Linear color space";
            break;
        case 1:
            currentColorSpace = OIIOImageLoader::ColorSpace::sRGB;
            qDebug() << "[PreviewOverlay] Switched to sRGB color space";
            break;
        case 2:
            currentColorSpace = OIIOImageLoader::ColorSpace::Rec709;
            qDebug() << "[PreviewOverlay] Switched to Rec.709 color space";
            break;
        default:
            currentColorSpace = OIIOImageLoader::ColorSpace::sRGB;
            break;
    }

    // Reload current frame/image with new color space
    if (isSequence) {
        qDebug() << "[PreviewOverlay] Reloading sequence frame with new color space";
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

    // Clear the media source to release the file
    mediaPlayer->setSource(QUrl());
}



#ifdef HAVE_FFMPEG
void PreviewOverlay::startFallbackVideo(const QString &filePath)
{
    if (usingFallbackVideo) {
        stopFallbackVideo();
    }

    qDebug() << "[PreviewOverlay] Starting fallback PNG-in-MOV playback for" << filePath;

    // Stop and hide the video widget path
    mediaPlayer->stop();
    videoWidget->hide();
    imageView->show();
    controlsWidget->show();

    // Clear scene and reset pixmap
    imageScene->clear();
    imageItem = nullptr;
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
        timeLabel->setText(QString("%1 / %2").arg(formatTime(0)).arg(formatTime(fallbackDurationMs)));
    } else {
        // Unknown duration
        positionSlider->setRange(0, 0);
        timeLabel->setText(QString("%1 / --:--").arg(formatTime(0)));
    }

    // Spin up worker thread
    fallbackThread = new QThread();
    fallbackReader = new FallbackPngMovReader(filePath);
    fallbackReader->moveToThread(fallbackThread);

    connect(fallbackThread, &QThread::started, fallbackReader, &FallbackPngMovReader::start);
    connect(fallbackReader, &FallbackPngMovReader::frameReady, this, &PreviewOverlay::onFallbackFrameReady, Qt::QueuedConnection);
    connect(fallbackReader, &FallbackPngMovReader::finished, this, &PreviewOverlay::onFallbackFinished, Qt::QueuedConnection);
    connect(fallbackReader, &FallbackPngMovReader::finished, fallbackThread, &QThread::quit);
    connect(fallbackThread, &QThread::finished, fallbackReader, &QObject::deleteLater);
    connect(fallbackThread, &QThread::finished, fallbackThread, &QObject::deleteLater);

    usingFallbackVideo = true;
    // Ensure reader and thread will stop on overlay destruction as a last resort
    connect(this, &QObject::destroyed, fallbackReader, &FallbackPngMovReader::stop, Qt::DirectConnection);
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
#ifdef HAVE_FFMPEG
    // Only trigger fallback if the error belongs to the currently requested source
    if (!isVideo) return;
    const QUrl src = mediaPlayer->source();
    if (src.isLocalFile()) {
        const QString srcPath = QDir::fromNativeSeparators(src.toLocalFile());
        const QString curPath = QDir::fromNativeSeparators(currentFilePath);
        if (srcPath != curPath) {
            qDebug() << "[PreviewOverlay] Ignoring error for stale source" << srcPath;
            return;
        }
    }
    // Fallback if codec is unsupported (e.g., PNG in MOV)
    if (!usingFallbackVideo) {
        startFallbackVideo(currentFilePath);
    }
#endif
}

void PreviewOverlay::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    // If media loads but never produces frames (black screen), we could add a timeout-based fallback here.
    Q_UNUSED(status);
}

void PreviewOverlay::onFallbackFrameReady(const QImage &image, qint64 ptsMs)
{
#ifdef HAVE_FFMPEG
    // Ignore frames from stale fallback readers
    if (!usingFallbackVideo || sender() != fallbackReader) {
        return;
    }
#endif
    // Update pixmap in the image scene
    originalPixmap = QPixmap::fromImage(image);

    // Determine alpha for fallback video frame display is irrelevant; hide toggle for videos
    if (alphaCheck) alphaCheck->hide();

    if (!imageItem) {
        imageItem = imageScene->addPixmap(originalPixmap);
    } else {
        imageItem->setPixmap(originalPixmap);
    }
    fitImageToView();

    // Update UI time/slider
    if (fallbackDurationMs > 0) {
        positionSlider->setValue(static_cast<int>(ptsMs));
        timeLabel->setText(QString("%1 / %2").arg(formatTime(ptsMs)).arg(formatTime(fallbackDurationMs)));
    } else {
        timeLabel->setText(QString("%1 / --:--").arg(formatTime(ptsMs)));
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
    QApplication::processEvents();

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
    QApplication::processEvents();

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
    QApplication::processEvents();

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


// Required because this .cpp defines a QObject with Q_OBJECT (FallbackPngMovReader)
#include "preview_overlay.moc"
