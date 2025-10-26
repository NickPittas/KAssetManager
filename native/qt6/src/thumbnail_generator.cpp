#include "thumbnail_generator.h"
#include "progress_manager.h"
#include "log_manager.h"
#include "oiio_image_loader.h"
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
#include <QRegularExpression>

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

    // CRITICAL: Don't capture 'this' because ThumbnailTask will be deleted after run() completes
    // Capture only the values we need by value
    QString filePath = m_filePath;
    ThumbnailGenerator* generator = m_generator;

    QMetaObject::invokeMethod(generator, [generator, filePath, thumbnailPath, success]() {
        QMutexLocker locker(&generator->m_mutex);
        generator->m_pendingThumbnails.remove(filePath);

        // Update progress
        generator->updateProgress();

        if (success) {
            emit generator->thumbnailGenerated(filePath, thumbnailPath);
        } else {
            emit generator->thumbnailFailed(filePath);
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
    connect(m_player, &QMediaPlayer::errorOccurred, this, &VideoThumbnailGenerator::onError);
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

            qDebug() << "[VideoThumbnailGenerator] Captured video frame, size:" << m_capturedFrame.size()
                     << "format:" << m_capturedFrame.format();

            // Handle alpha channel premultiplication for videos with transparency
            // QVideoFrame often provides unpremultiplied alpha, but Qt expects premultiplied for proper display
            if (m_capturedFrame.hasAlphaChannel()) {
                QImage::Format format = m_capturedFrame.format();

                // Convert to premultiplied alpha if needed
                if (format == QImage::Format_ARGB32 || format == QImage::Format_RGBA8888) {
                    qDebug() << "[VideoThumbnailGenerator] Converting to premultiplied alpha";
                    m_capturedFrame = m_capturedFrame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                } else if (format != QImage::Format_ARGB32_Premultiplied &&
                           format != QImage::Format_RGBA8888_Premultiplied) {
                    // For other formats with alpha, convert to premultiplied
                    m_capturedFrame = m_capturedFrame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                }
            }

            QImage thumbnail = m_capturedFrame.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);

            // For images with alpha, save as PNG to preserve transparency
            // Otherwise use JPEG for smaller file size
            QString cachePath = m_cachePath;
            if (thumbnail.hasAlphaChannel()) {
                // Replace .jpg extension with .png for alpha videos
                cachePath.replace(QRegularExpression("\\.jpg$", QRegularExpression::CaseInsensitiveOption), ".png");
            }

            QString format = thumbnail.hasAlphaChannel() ? "PNG" : "JPEG";
            int quality = thumbnail.hasAlphaChannel() ? 100 : 85;

            if (thumbnail.save(cachePath, format.toUtf8().constData(), quality)) {
                qDebug() << "[VideoThumbnailGenerator] Saved video thumbnail:" << cachePath;

                QMutexLocker locker(&m_generator->m_mutex);
                m_generator->m_pendingThumbnails.remove(m_filePath);

                // Update progress
                m_generator->updateProgress();

                emit m_generator->thumbnailGenerated(m_filePath, cachePath);
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
    qDebug() << "[VideoThumbnailGenerator] Timeout waiting for video frame (video may be corrupted or unsupported):" << m_filePath;
    m_player->stop();

    QMutexLocker locker(&m_generator->m_mutex);
    m_generator->m_pendingThumbnails.remove(m_filePath);

    // Update progress
    m_generator->updateProgress();

    emit m_generator->thumbnailFailed(m_filePath);

    deleteLater();
}

void VideoThumbnailGenerator::onError(QMediaPlayer::Error error, const QString &errorString) {
    qDebug() << "[VideoThumbnailGenerator] Media player error for" << m_filePath << "- Error:" << error << errorString;

    m_timeout->stop();
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

    // PERFORMANCE: Use optimal thread count for thumbnail generation
    // Use half of available CPU cores to avoid overwhelming the system
    // Minimum 2 threads, maximum 8 threads for best balance
    int idealThreads = QThread::idealThreadCount();
    int optimalThreads = qBound(2, idealThreads / 2, 8);
    m_threadPool->setMaxThreadCount(optimalThreads);

    qDebug() << "[ThumbnailGenerator] Initialized with" << m_threadPool->maxThreadCount()
             << "threads (ideal:" << idealThreads << ")";
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

    // Check if PNG version exists (for videos with alpha)
    QString pngPath = m_thumbnailDir.absoluteFilePath(hash + ".png");
    if (QFileInfo::exists(pngPath)) {
        return pngPath;
    }

    // Default to JPG
    return m_thumbnailDir.absoluteFilePath(hash + ".jpg");
}

bool ThumbnailGenerator::isImageFile(const QString& filePath) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    // Qt natively supported formats (via QImageReader)
    QStringList qtSupportedExts = {
        "jpg", "jpeg", "png", "gif", "bmp", "webp",
        "tiff", "tif", "ico", "pbm", "pgm", "ppm", "pnm",
        "svg", "svgz"
    };

    // Formats that need special handling (not natively supported by Qt)
    QStringList specialFormats = {
        // RAW formats
        "raw", "cr2", "cr3", "nef", "arw", "dng", "orf", "rw2", "pef", "srw", "raf",
        // HDR/EXR formats
        "exr", "hdr", "pic",
        // Adobe formats
        "psd", "psb",
        // Other formats
        "heic", "heif", "avif", "jxl", "tga", "pcx"
    };

    return qtSupportedExts.contains(ext) || specialFormats.contains(ext);
}

bool ThumbnailGenerator::isVideoFile(const QString& filePath) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    // Comprehensive list of video formats
    QStringList videoExts = {
        // Common formats
        "mp4", "mov", "avi", "mkv", "webm", "flv", "wmv", "m4v",
        // MPEG variants
        "mpg", "mpeg", "m2v", "m4p", "m2ts", "mts", "ts",
        // Other formats
        "3gp", "3g2", "ogv", "ogg", "vob", "divx", "xvid",
        "asf", "rm", "rmvb", "f4v", "swf", "mxf", "roq", "nsv"
    };
    return videoExts.contains(ext);
}

bool ThumbnailGenerator::isQtSupportedFormat(const QString& filePath) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    // Qt natively supported image formats
    QStringList qtSupported = {
        "jpg", "jpeg", "png", "gif", "bmp", "webp",
        "tiff", "tif", "ico", "pbm", "pgm", "ppm", "pnm",
        "svg", "svgz"
    };

    return qtSupported.contains(ext);
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
        qWarning() << "[ThumbnailGenerator] Unsupported file type, creating placeholder:" << filePath;
        QString unsupportedThumb = createUnsupportedThumbnail(filePath);
        QMutexLocker locker(&m_mutex);
        m_pendingThumbnails.remove(filePath);
        if (!unsupportedThumb.isEmpty()) {
            emit thumbnailGenerated(filePath, unsupportedThumb);
        } else {
            emit thumbnailFailed(filePath);
        }
        updateProgress();
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
    qDebug() << "[ThumbnailGenerator] ===== START Generating image thumbnail for:" << filePath;

    try {
        // Validate file exists and is readable
        QFileInfo fileInfo(filePath);
        qDebug() << "[ThumbnailGenerator] File size:" << fileInfo.size() << "bytes";

        if (!fileInfo.exists() || !fileInfo.isReadable()) {
            qWarning() << "[ThumbnailGenerator] File not accessible:" << filePath;
            return QString();
        }

        // Check if this format should be handled by OpenImageIO
        if (OIIOImageLoader::isOIIOSupported(filePath)) {
            qDebug() << "[ThumbnailGenerator] Using OpenImageIO for:" << filePath;

            QImage image = OIIOImageLoader::loadImage(filePath, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
            if (!image.isNull()) {
                QString cachePath = getThumbnailCachePath(filePath);
                if (image.save(cachePath, "JPEG", 85)) {
                    qDebug() << "[ThumbnailGenerator] OIIO thumbnail saved:" << cachePath;
                    return cachePath;
                } else {
                    qWarning() << "[ThumbnailGenerator] Failed to save OIIO thumbnail";
                    return QString();
                }
            } else {
                qWarning() << "[ThumbnailGenerator] OIIO failed to load image, falling back to placeholder";
                // Fall through to placeholder generation
            }
        }

        // Check if this is a Qt-supported format
        if (!isQtSupportedFormat(filePath)) {
            qWarning() << "[ThumbnailGenerator] Format not supported by Qt or OIIO:" << filePath;
            qWarning() << "[ThumbnailGenerator] Creating placeholder thumbnail for unsupported format";

            // Create a placeholder thumbnail with format info
            QImage placeholder(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, QImage::Format_RGB32);
            placeholder.fill(QColor(50, 50, 50));

            QPainter painter(&placeholder);
            painter.setRenderHint(QPainter::Antialiasing);

            // Draw file icon
            painter.setPen(QPen(QColor(150, 150, 150), 2));
            painter.setBrush(Qt::NoBrush);
            QRect iconRect(THUMBNAIL_WIDTH/2 - 50, 30, 100, 100);
            painter.drawRoundedRect(iconRect, 8, 8);

            // Draw file extension
            QFileInfo fi(filePath);
            QString ext = fi.suffix().toUpper();
            painter.setFont(QFont("Segoe UI", 24, QFont::Bold));
            painter.setPen(QColor(200, 200, 200));
            painter.drawText(iconRect, Qt::AlignCenter, ext);

            // Draw "Preview Not Available" text
            painter.setFont(QFont("Segoe UI", 10));
            painter.setPen(QColor(180, 180, 180));
            QRect textRect(20, 150, THUMBNAIL_WIDTH - 40, 60);
            painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, "Preview Not Available\n(Format not supported)");

            QString cachePath = getThumbnailCachePath(filePath);
            if (placeholder.save(cachePath, "JPEG", 85)) {
                qDebug() << "[ThumbnailGenerator] Created placeholder thumbnail:" << cachePath;
                return cachePath;
            } else {
                qWarning() << "[ThumbnailGenerator] Failed to save placeholder thumbnail";
                return QString();
            }
        }

        qDebug() << "[ThumbnailGenerator] Creating QImageReader...";
        QImageReader reader(filePath);
        reader.setAutoTransform(true);

        // Set a decision handler to avoid crashes on corrupted images
        reader.setDecideFormatFromContent(true);

        // CRITICAL: Set quality to speed for large images
        reader.setQuality(50);

        qDebug() << "[ThumbnailGenerator] Reading image size...";
        QSize originalSize = reader.size();
        if (!originalSize.isValid()) {
            qWarning() << "[ThumbnailGenerator] Failed to read image size:" << filePath << reader.errorString();
            if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
                LogManager::instance().addLog(QString("Thumbnail read failure: %1").arg(QFileInfo(filePath).fileName()), "WARN");
            }
            return QString();
        }

        qDebug() << "[ThumbnailGenerator] Original image size:" << originalSize.width() << "x" << originalSize.height();

        // Validate size is reasonable
        if (originalSize.width() <= 0 || originalSize.height() <= 0 ||
            originalSize.width() > 50000 || originalSize.height() > 50000) {
            qWarning() << "[ThumbnailGenerator] Invalid image dimensions:" << originalSize << "for" << filePath;
            return QString();
        }

        // CRITICAL: For very large images (4K+), use scaled reading to avoid memory issues
        QSize scaledSize = originalSize.scaled(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, Qt::KeepAspectRatio);
        qDebug() << "[ThumbnailGenerator] Scaled size will be:" << scaledSize.width() << "x" << scaledSize.height();

        // Always set scaled size BEFORE reading to avoid loading full resolution
        reader.setScaledSize(scaledSize);

        // For very large images, also set a clip rect to limit memory usage
        if (originalSize.width() > 4000 || originalSize.height() > 4000) {
            qDebug() << "[ThumbnailGenerator] Large image detected, using optimized loading";
            reader.setScaledClipRect(QRect(0, 0, scaledSize.width(), scaledSize.height()));
        }

        qDebug() << "[ThumbnailGenerator] Reading image data...";
        QImage image = reader.read();
        if (image.isNull()) {
            qWarning() << "[ThumbnailGenerator] Failed to read image:" << filePath << reader.errorString();
            if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
                LogManager::instance().addLog(QString("Thumbnail decode failure: %1").arg(QFileInfo(filePath).fileName()), "WARN");
            }
            return QString();
        }

        qDebug() << "[ThumbnailGenerator] Image read successfully, actual size:" << image.size();

        QString cachePath = getThumbnailCachePath(filePath);
        qDebug() << "[ThumbnailGenerator] Saving thumbnail to:" << cachePath;

        if (!image.save(cachePath, "JPEG", 85)) {
            qWarning() << "[ThumbnailGenerator] Failed to save thumbnail:" << cachePath;
            if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
                LogManager::instance().addLog(QString("Thumbnail save failure: %1").arg(QFileInfo(cachePath).fileName()), "WARN");
            }
            return QString();
        }

        qDebug() << "[ThumbnailGenerator] ===== SUCCESS Generated image thumbnail:" << cachePath;
        if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
            LogManager::instance().addLog(QString("Thumbnail generated: %1").arg(QFileInfo(cachePath).fileName()), "DEBUG");
        }
        return cachePath;
    } catch (const std::exception& e) {
        qCritical() << "[ThumbnailGenerator] ===== EXCEPTION generating thumbnail for" << filePath << ":" << e.what();
        return QString();
    } catch (...) {
        qCritical() << "[ThumbnailGenerator] ===== UNKNOWN EXCEPTION generating thumbnail for" << filePath;
        return QString();
    }
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
        emit progressChanged(m_completedThumbnails, m_totalThumbnails);
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

QString ThumbnailGenerator::createUnsupportedThumbnail(const QString& filePath) {
    qDebug() << "[ThumbnailGenerator] Creating unsupported format thumbnail for:" << filePath;

    try {
        // Create a simple image with "Format Not Supported" text
        QImage image(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, QImage::Format_RGB32);
        image.fill(QColor(40, 40, 40));

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw icon/symbol
        painter.setPen(QPen(QColor(120, 120, 120), 3));
        painter.setBrush(Qt::NoBrush);
        QRect iconRect(THUMBNAIL_WIDTH/2 - 40, 40, 80, 80);
        painter.drawRect(iconRect);
        painter.drawLine(iconRect.topLeft(), iconRect.bottomRight());
        painter.drawLine(iconRect.topRight(), iconRect.bottomLeft());

        // Draw text
        painter.setPen(QColor(180, 180, 180));
        painter.setFont(QFont("Segoe UI", 12, QFont::Bold));
        QRect textRect(20, 140, THUMBNAIL_WIDTH - 40, 60);
        painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, "Format Not\nSupported");

        // Draw file extension
        QFileInfo fi(filePath);
        QString ext = fi.suffix().toUpper();
        if (!ext.isEmpty()) {
            painter.setFont(QFont("Segoe UI", 10));
            painter.setPen(QColor(140, 140, 140));
            QRect extRect(20, 200, THUMBNAIL_WIDTH - 40, 30);
            painter.drawText(extRect, Qt::AlignCenter, QString(".%1").arg(ext));
        }

        painter.end();

        // Save to cache
        QString cachePath = getThumbnailCachePath(filePath);
        if (image.save(cachePath, "JPEG", 85)) {
            qDebug() << "[ThumbnailGenerator] Created unsupported thumbnail:" << cachePath;
            return cachePath;
        } else {
            qWarning() << "[ThumbnailGenerator] Failed to save unsupported thumbnail:" << cachePath;
            return QString();
        }
    } catch (const std::exception& e) {
        qCritical() << "[ThumbnailGenerator] Exception creating unsupported thumbnail:" << e.what();
        return QString();
    } catch (...) {
        qCritical() << "[ThumbnailGenerator] Unknown exception creating unsupported thumbnail";
        return QString();
    }
}


