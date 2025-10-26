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

PreviewOverlay::PreviewOverlay(QWidget *parent)
    : QWidget(parent)
    , imageView(nullptr)
    , imageScene(nullptr)
    , imageItem(nullptr)
    , videoWidget(nullptr)
    , mediaPlayer(nullptr)
    , audioOutput(nullptr)
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
    if (mediaPlayer) {
        mediaPlayer->stop();
    }
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
    
    topLayout->addStretch();
    
    closeBtn = new QPushButton("✕", this);
    closeBtn->setStyleSheet(
        "QPushButton { background-color: transparent; color: white; font-size: 24px; "
        "border: none; padding: 10px 20px; }"
        "QPushButton:hover { background-color: rgba(255, 255, 255, 30); }"
    );
    connect(closeBtn, &QPushButton::clicked, this, &PreviewOverlay::closed);
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
    imageView->hide();
    contentLayout->addWidget(imageView);

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
    
    audioOutput->setVolume(0.5);
}

void PreviewOverlay::showAsset(const QString &filePath, const QString &fileName, const QString &fileType)
{
    // Reset sequence state
    isSequence = false;
    sequencePlaying = false;
    if (sequenceTimer->isActive()) {
        sequenceTimer->stop();
    }

    currentFilePath = filePath;
    currentFileType = fileType.toLower();
    fileNameLabel->setText(fileName);

    // Determine if it's a video or image
    QStringList videoFormats = {"mp4", "avi", "mov", "mkv", "webm", "flv", "wmv", "m4v"};
    isVideo = videoFormats.contains(currentFileType);

    // Make sure widget is shown and sized before loading content
    show();
    raise();
    setFocus();

    // Process events to ensure window is properly sized
    QApplication::processEvents();

    if (isVideo) {
        showVideo(filePath);
    } else {
        showImage(filePath);
    }
}

void PreviewOverlay::showImage(const QString &filePath)
{
    qDebug() << "[PreviewOverlay::showImage] Loading image:" << filePath;

    // CRITICAL: Hide video widget and show image view
    videoWidget->hide();
    imageView->show();

    // CRITICAL: Stop any video playback
    if (mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        qDebug() << "[PreviewOverlay::showImage] Stopping video playback";
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

    // CRITICAL: Hide image view and show video widget
    imageView->hide();
    videoWidget->show();
    controlsWidget->show();

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
    if (isVideo && mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
        controlsWidget->hide();
    }
}

void PreviewOverlay::updatePlayPauseButton()
{
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
            emit closed();
            break;
        case Qt::Key_Left:
            if (isVideo && mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
                mediaPlayer->stop();
            }
            navigatePrevious();
            break;
        case Qt::Key_Right:
            if (isVideo && mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
                mediaPlayer->stop();
            }
            navigateNext();
            break;
        case Qt::Key_Space:
            if (isVideo) {
                onPlayPauseClicked();
            }
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
        // Zoom with mouse wheel
        double factor = event->angleDelta().y() > 0 ? 1.15 : 0.85;
        zoomImage(factor);
        event->accept();
    } else {
        QWidget::wheelEvent(event);
    }
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

    // Stop sequence playback
    if (sequencePlaying) {
        pauseSequence();
    }

    // Clear the media source to release the file
    mediaPlayer->setSource(QUrl());
}

