#include "preview_overlay.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPixmap>
#include <QFileInfo>
#include <QScrollArea>
#include <QStyle>
#include <QApplication>
#include <QWheelEvent>
#include <QMouseEvent>

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
{
    setupUi();
    setFocusPolicy(Qt::StrongFocus);

    // Auto-hide controls timer
    controlsTimer = new QTimer(this);
    controlsTimer->setSingleShot(true);
    controlsTimer->setInterval(3000);
    connect(controlsTimer, &QTimer::timeout, this, &PreviewOverlay::hideControls);
}

PreviewOverlay::~PreviewOverlay()
{
    if (mediaPlayer) {
        mediaPlayer->stop();
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
    videoWidget->hide();
    controlsWidget->hide();
    imageView->show();

    if (mediaPlayer->playbackState() != QMediaPlayer::StoppedState) {
        mediaPlayer->stop();
    }

    originalPixmap = QPixmap(filePath);
    if (!originalPixmap.isNull()) {
        // Clear scene and add new image
        imageScene->clear();
        imageItem = imageScene->addPixmap(originalPixmap);
        imageScene->setSceneRect(originalPixmap.rect());

        // Fit to view
        fitImageToView();
    }
}

void PreviewOverlay::showVideo(const QString &filePath)
{
    imageView->hide();
    videoWidget->show();
    controlsWidget->show();

    mediaPlayer->setSource(QUrl::fromLocalFile(filePath));

    // Set video to fill the widget while maintaining aspect ratio
    videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);

    mediaPlayer->play();

    controlsTimer->start();
}

void PreviewOverlay::onPlayPauseClicked()
{
    if (mediaPlayer->playbackState() == QMediaPlayer::PlayingState) {
        mediaPlayer->pause();
    } else {
        mediaPlayer->play();
    }
    updatePlayPauseButton();
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
    mediaPlayer->setPosition(position);
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

