#include "thumbnail_generator.h"
#include "progress_manager.h"
#include "log_manager.h"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDateTime>
#include <QPainter>
#include <QColor>
#include <QBrush>
#include <QFont>
#include <QImageReader>
#include <QDebug>
#include <QMutexLocker>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QTimer>

// ThumbnailTask implementation (for images only)
ThumbnailTask::ThumbnailTask(const QString& filePath, ThumbnailGenerator* generator)
    : m_filePath(filePath), m_generator(generator) {
    setAutoDelete(true);
}

void ThumbnailTask::run() {
    qDebug() << "[ThumbnailTask] Generating image thumbnail for:" << m_filePath;

    QString thumbnailPath;
    bool success = false;

    try {
        thumbnailPath = m_generator->generateImageThumbnail(m_filePath);
        success = !thumbnailPath.isEmpty();

        if (success) {
            qDebug() << "[ThumbnailTask] Successfully generated image thumbnail:" << thumbnailPath;
        } else {
            qDebug() << "[ThumbnailTask] No image thumbnail generated for:" << m_filePath;
        }
    } catch (...) {
        qWarning() << "[ThumbnailTask] Exception during image thumbnail generation:" << m_filePath;
    }

    QMetaObject::invokeMethod(m_generator, [this, thumbnailPath, success]() {
        QMutexLocker locker(&m_generator->m_mutex);
        m_generator->m_pendingThumbnails.remove(m_filePath);

        // Update progress
        m_generator->updateProgress();

        if (success) {
            emit m_generator->thumbnailGenerated(m_filePath, thumbnailPath);
        } else {
            emit m_generator->thumbnailFailed(m_filePath);
        }
    }, Qt::QueuedConnection);
}

// VideoThumbnailGenerator implementation
VideoThumbnailGenerator::VideoThumbnailGenerator(const QString& filePath, const QString& cachePath, ThumbnailGenerator* generator)
    : QObject(nullptr), m_filePath(filePath), m_cachePath(cachePath), m_generator(generator),
      m_frameReceived(false) {

    m_player = new QMediaPlayer(this);
    m_videoSink = new QVideoSink(this);
    m_player->setVideoSink(m_videoSink);

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(3000);

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &VideoThumbnailGenerator::onMediaStatusChanged);
    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, &VideoThumbnailGenerator::onVideoFrameChanged);
    connect(m_timeout, &QTimer::timeout, this, &VideoThumbnailGenerator::onTimeout);
}

VideoThumbnailGenerator::~VideoThumbnailGenerator() {
    if (m_player) {
        m_player->stop();
    }
}

void VideoThumbnailGenerator::start() {
    qDebug() << "[VideoThumbnailGenerator] Starting async video thumbnail generation for:" << m_filePath;
    m_player->setSource(QUrl::fromLocalFile(m_filePath));
    m_timeout->start();
}

void VideoThumbnailGenerator::onMediaStatusChanged() {
    QMediaPlayer::MediaStatus status = m_player->mediaStatus();
    qDebug() << "[VideoThumbnailGenerator] Media status changed:" << status;

    if (status == QMediaPlayer::LoadedMedia) {
        qint64 duration = m_player->duration();
        qint64 seekPos = qMin(1000LL, duration / 10);
        qDebug() << "[VideoThumbnailGenerator] Video loaded, duration:" << duration << "ms, seeking to:" << seekPos << "ms";
        m_player->setPosition(seekPos);
        m_player->play();
    }
}

void VideoThumbnailGenerator::onVideoFrameChanged() {
    if (m_frameReceived) return;

    QVideoFrame frame = m_videoSink->videoFrame();
    if (!frame.isValid()) return;

    if (frame.map(QVideoFrame::ReadOnly)) {
        m_capturedFrame = frame.toImage();
        frame.unmap();

        if (!m_capturedFrame.isNull()) {
            m_frameReceived = true;
            m_timeout->stop();
            m_player->stop();

            qDebug() << "[VideoThumbnailGenerator] Captured video frame, size:" << m_capturedFrame.size();

            QImage thumbnail = m_capturedFrame.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);

            if (thumbnail.save(m_cachePath, "JPEG", 85)) {
                qDebug() << "[VideoThumbnailGenerator] Saved video thumbnail:" << m_cachePath;

                QMutexLocker locker(&m_generator->m_mutex);
                m_generator->m_pendingThumbnails.remove(m_filePath);

                // Update progress
                m_generator->updateProgress();

                emit m_generator->thumbnailGenerated(m_filePath, m_cachePath);
            } else {
                qWarning() << "[VideoThumbnailGenerator] Failed to save video thumbnail:" << m_cachePath;

                QMutexLocker locker(&m_generator->m_mutex);
                m_generator->m_pendingThumbnails.remove(m_filePath);

                // Update progress
                m_generator->updateProgress();

                emit m_generator->thumbnailFailed(m_filePath);
            }

            deleteLater();
        }
    }
}

void VideoThumbnailGenerator::onTimeout() {
    qWarning() << "[VideoThumbnailGenerator] Timeout waiting for video frame:" << m_filePath;
    m_player->stop();

    QMutexLocker locker(&m_generator->m_mutex);
    m_generator->m_pendingThumbnails.remove(m_filePath);

    // Update progress
    m_generator->updateProgress();

    emit m_generator->thumbnailFailed(m_filePath);

    deleteLater();
}

// ThumbnailGenerator implementation
ThumbnailGenerator& ThumbnailGenerator::instance() {
    static ThumbnailGenerator s;
    return s;
}

ThumbnailGenerator::ThumbnailGenerator(QObject* parent)
    : QObject(parent), m_totalThumbnails(0), m_completedThumbnails(0) {
    ensureThumbnailDir();

    // Create thread pool for thumbnail generation
    m_threadPool = new QThreadPool(this);
    m_threadPool->setMaxThreadCount(2); // Max 2 concurrent thumbnail generations (videos block main thread)

    qDebug() << "[ThumbnailGenerator] Initialized with" << m_threadPool->maxThreadCount() << "threads";
}

void ThumbnailGenerator::ensureThumbnailDir() {
    // Store thumbnails in {appDir}/data/thumbnails/
    QString appDir = QCoreApplication::applicationDirPath();
    QString dataDir = appDir + "/data";
    QString thumbDir = dataDir + "/thumbnails";
    
    QDir dir;
    if (!dir.exists(dataDir)) {
        dir.mkpath(dataDir);
    }
    if (!dir.exists(thumbDir)) {
        dir.mkpath(thumbDir);
    }
    
    m_thumbnailDir = QDir(thumbDir);
    qDebug() << "[ThumbnailGenerator] Cache directory:" << m_thumbnailDir.absolutePath();
}

QString ThumbnailGenerator::getFileHash(const QString& filePath) {
    // Use MD5 hash of absolute file path as cache key
    QFileInfo fi(filePath);
    QString absPath = fi.absoluteFilePath();
    QByteArray hash = QCryptographicHash::hash(absPath.toUtf8(), QCryptographicHash::Md5);
    return QString(hash.toHex());
}

QString ThumbnailGenerator::getThumbnailCachePath(const QString& filePath) {
    QString hash = getFileHash(filePath);
    return m_thumbnailDir.absoluteFilePath(hash + ".jpg");
}

bool ThumbnailGenerator::isImageFile(const QString& filePath) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();
    QStringList imageExts = {"jpg", "jpeg", "png", "gif", "bmp", "webp", "tiff", "tif", "ico", "svg"};
    return imageExts.contains(ext);
}

bool ThumbnailGenerator::isVideoFile(const QString& filePath) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();
    QStringList videoExts = {"mp4", "mov", "avi", "mkv", "webm", "flv", "wmv", "m4v", "mpg", "mpeg"};
    return videoExts.contains(ext);
}

bool ThumbnailGenerator::isThumbnailCached(const QString& filePath) {
    QString cachePath = getThumbnailCachePath(filePath);
    QFileInfo cacheInfo(cachePath);

    if (!cacheInfo.exists()) {
        return false;
    }

    // Check if cache is stale (source file is newer than thumbnail)
    QFileInfo sourceInfo(filePath);
    if (sourceInfo.lastModified() > cacheInfo.lastModified()) {
        return false;
    }

    return true;
}

QString ThumbnailGenerator::getThumbnailPath(const QString& filePath) {
    if (filePath.isEmpty()) {
        return QString();
    }

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        return QString();
    }

    // Check if thumbnail already exists in cache
    // Return cached path if it exists
    if (isThumbnailCached(filePath)) {
        return getThumbnailCachePath(filePath);
    }

    // No cached thumbnail - return empty string (caller should use requestThumbnail for async generation)
    return QString();
}

void ThumbnailGenerator::requestThumbnail(const QString& filePath) {
    if (filePath.isEmpty()) {
        return;
    }

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        return;
    }

    // Check if already cached
    if (isThumbnailCached(filePath)) {
        QString cachePath = getThumbnailCachePath(filePath);
        qDebug() << "[ThumbnailGenerator] Using cached thumbnail:" << cachePath;
        emit thumbnailGenerated(filePath, cachePath);
        return;
    }

    // Check if already being processed
    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingThumbnails.contains(filePath)) {
            qDebug() << "[ThumbnailGenerator] Thumbnail already being generated:" << filePath;
            return; // Already in progress
        }
        m_pendingThumbnails.insert(filePath);
    }

    bool isVideo = isVideoFile(filePath);
    bool isImage = isImageFile(filePath);

    if (!isVideo && !isImage) {
        qWarning() << "[ThumbnailGenerator] Unsupported file type:" << filePath;
        QMutexLocker locker(&m_mutex);
        m_pendingThumbnails.remove(filePath);
        emit thumbnailFailed(filePath);
        return;
    }

    if (isVideo) {
        QString cachePath = getThumbnailCachePath(filePath);
        VideoThumbnailGenerator* videoGen = new VideoThumbnailGenerator(filePath, cachePath, this);
        videoGen->start();
        qDebug() << "[ThumbnailGenerator] Started async video thumbnail generation for:" << filePath;
    } else {
        ThumbnailTask* task = new ThumbnailTask(filePath, this);
        m_threadPool->start(task);
        qDebug() << "[ThumbnailGenerator] Queued image thumbnail generation for:" << filePath
                 << "(active threads:" << m_threadPool->activeThreadCount() << ")";
    }
}

QString ThumbnailGenerator::generateImageThumbnail(const QString& filePath) {
    qDebug() << "[ThumbnailGenerator] Generating image thumbnail for:" << filePath;

    QImageReader reader(filePath);
    reader.setAutoTransform(true);

    QSize originalSize = reader.size();
    if (!originalSize.isValid()) {
        qWarning() << "[ThumbnailGenerator] Failed to read image size:" << filePath << reader.errorString();
        if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
            LogManager::instance().addLog(QString("Thumbnail read failure: %1").arg(QFileInfo(filePath).fileName()), "WARN");
        }
        return QString();
    }

    QSize scaledSize = originalSize.scaled(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, Qt::KeepAspectRatio);
    reader.setScaledSize(scaledSize);

    QImage image = reader.read();
    if (image.isNull()) {
        qWarning() << "[ThumbnailGenerator] Failed to read image:" << filePath << reader.errorString();
        if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
            LogManager::instance().addLog(QString("Thumbnail decode failure: %1").arg(QFileInfo(filePath).fileName()), "WARN");
        }
        return QString();
    }

    QString cachePath = getThumbnailCachePath(filePath);
    if (!image.save(cachePath, "JPEG", 85)) {
        qWarning() << "[ThumbnailGenerator] Failed to save thumbnail:" << cachePath;
        if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
            LogManager::instance().addLog(QString("Thumbnail save failure: %1").arg(QFileInfo(cachePath).fileName()), "WARN");
        }
        return QString();
    }

    qDebug() << "[ThumbnailGenerator] Generated image thumbnail:" << cachePath;
    if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
        LogManager::instance().addLog(QString("Thumbnail generated: %1").arg(QFileInfo(cachePath).fileName()), "DEBUG");
    }
    return cachePath;
}

QString ThumbnailGenerator::createSampleImage(const QString& directory) {
    QString baseDir = directory;
    if (baseDir.isEmpty()) {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        if (baseDir.isEmpty()) {
            baseDir = QDir::tempPath();
        }
        baseDir += "/kasset_autotest";
    }

    QDir dir(baseDir);
    if (!dir.exists()) {
        if (!QDir().mkpath(baseDir)) {
            qWarning() << "[ThumbnailGenerator] Failed to create sample image directory" << baseDir;
            LogManager::instance().addLog(QString("Failed to create sample image directory %1").arg(baseDir));
            return QString();
        }
    }

    QString fileName = QStringLiteral("autotest_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    QString filePath = dir.filePath(fileName);

    QImage img(256, 256, QImage::Format_ARGB32);
    img.fill(QColor("#1e1e1e"));

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QBrush(QColor("#4a90e2")));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(24, 24, img.width() - 48, img.height() - 48), 24, 24);

    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 28, QFont::Bold));
    painter.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("KAsset\nAutotest"));
    painter.end();

    if (img.save(filePath, "PNG", 95)) {
        qDebug() << "[ThumbnailGenerator] Created sample image at" << filePath;
        if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
            LogManager::instance().addLog(QString("Generated sample image %1").arg(fileName), "DEBUG");
        }
        return filePath;
    }

    qWarning() << "[ThumbnailGenerator] Failed to save sample image at" << filePath;
    if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
        LogManager::instance().addLog(QString("Failed to create sample image %1").arg(fileName), "WARN");
    }
    return QString();
}

void ThumbnailGenerator::clearCache() {
    qDebug() << "ThumbnailGenerator: clearing cache...";
    QStringList files = m_thumbnailDir.entryList(QDir::Files);
    int count = 0;
    for (const QString& file : files) {
        if (m_thumbnailDir.remove(file)) {
            count++;
        }
    }
    qDebug() << "ThumbnailGenerator: cleared" << count << "cached thumbnails";
}

void ThumbnailGenerator::startProgress(int total) {
    m_totalThumbnails = total;
    m_completedThumbnails = 0;

    if (total > 0) {
        ProgressManager::instance().start("Generating thumbnails...", total);
        qDebug() << "[ThumbnailGenerator] Started progress tracking for" << total << "thumbnails";
    }
}

void ThumbnailGenerator::updateProgress() {
    m_completedThumbnails++;

    if (m_totalThumbnails > 0) {
        ProgressManager::instance().update(m_completedThumbnails);
        if (!qEnvironmentVariableIsEmpty("KASSET_DIAGNOSTICS")) {
            qDebug() << "[ThumbnailGenerator] Progress:" << m_completedThumbnails << "/" << m_totalThumbnails;
        }
        if (m_completedThumbnails >= m_totalThumbnails) {
            finishProgress();
        }
    }
}

void ThumbnailGenerator::finishProgress() {
    ProgressManager::instance().finish();
    qDebug() << "[ThumbnailGenerator] Finished progress tracking";
    m_totalThumbnails = 0;
    m_completedThumbnails = 0;
}


