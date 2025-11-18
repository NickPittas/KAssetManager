#include "thumbnail_generator_worker.h"
#include "thumbnail_cache_manager.h"
#include "media/gstreamer_player.h"
#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QDebug>
#include <QDir>
#include <QtConcurrent>
#include <QMetaObject>

ThumbnailGeneratorWorker::ThumbnailGeneratorWorker(QObject* parent)
    : QObject(parent)
    , m_cancelling(0)
    , m_successCount(0)
    , m_failCount(0)
    , m_completedCount(0)
{
    // Create a thread pool with optimal thread count (CPU cores)
    m_threadPool = new QThreadPool(this);
    m_threadPool->setMaxThreadCount(QThread::idealThreadCount());
}

ThumbnailGeneratorWorker::~ThumbnailGeneratorWorker()
{
    if (m_threadPool) {
        m_threadPool->waitForDone();
    }
}

void ThumbnailGeneratorWorker::start(const QVector<Task>& tasks, const QSize& thumbnailSize)
{
    if (tasks.isEmpty()) {
        emit queueFinished(true);
        return;
    }

    m_tasks = tasks;
    m_thumbnailSize = thumbnailSize;
    m_cancelling.storeRelaxed(0);
    m_successCount.storeRelaxed(0);
    m_failCount.storeRelaxed(0);
    m_completedCount.storeRelaxed(0);

    emit queueStarted(m_tasks.size());
    emit logLine(QString("Starting thumbnail generation for %1 files using %2 threads...")
        .arg(m_tasks.size())
        .arg(m_threadPool->maxThreadCount()));
    emit logLine(QString("Thumbnail size: %1x%2").arg(thumbnailSize.width()).arg(thumbnailSize.height()));

    // Process all tasks in parallel using the thread pool
    for (int i = 0; i < m_tasks.size(); ++i) {
        const Task task = m_tasks[i]; // Copy task for thread safety
        m_threadPool->start([this, i, task]() {
            processFile(i, task);
        });
    }
}

void ThumbnailGeneratorWorker::cancelAll()
{
    m_cancelling.storeRelaxed(1);
    emit logLine("Cancelling thumbnail generation...");
}

void ThumbnailGeneratorWorker::processFile(int index, const Task& task)
{
    // Check if cancelled
    if (m_cancelling.loadRelaxed()) {
        m_completedCount.fetchAndAddOrdered(1);
        checkIfFinished();
        return;
    }

    // Emit signals using QMetaObject::invokeMethod for thread safety
    QMetaObject::invokeMethod(this, [this, index, task]() {
        emit fileStarted(index, task.filePath);
        emit logLine(QString("Processing: %1").arg(QFileInfo(task.filePath).fileName()));
        emit fileProgress(index, 0);
    }, Qt::QueuedConnection);

    QFileInfo info(task.filePath);
    if (!info.exists()) {
        QMetaObject::invokeMethod(this, [this, index, task]() {
            emit logLine(QString("ERROR: File not found: %1").arg(task.filePath));
            emit fileFinished(index, false, "File not found");
        }, Qt::QueuedConnection);
        m_failCount.fetchAndAddOrdered(1);
        m_completedCount.fetchAndAddOrdered(1);
        checkIfFinished();
        return;
    }

    bool success = false;

    if (task.isSequence) {
        success = generateSequenceThumbnails(index, task, m_thumbnailSize);
    } else if (task.isVideo) {
        success = generateVideoThumbnails(index, task.filePath, m_thumbnailSize);
    } else {
        success = generateImageThumbnail(index, task.filePath, m_thumbnailSize);
    }

    if (success) {
        m_successCount.fetchAndAddOrdered(1);
        QMetaObject::invokeMethod(this, [this, info]() {
            emit logLine(QString("SUCCESS: %1").arg(info.fileName()));
        }, Qt::QueuedConnection);
    } else {
        m_failCount.fetchAndAddOrdered(1);
        QMetaObject::invokeMethod(this, [this, info]() {
            emit logLine(QString("FAILED: %1").arg(info.fileName()));
        }, Qt::QueuedConnection);
    }

    QMetaObject::invokeMethod(this, [this, index, success]() {
        emit fileProgress(index, 100);
        emit fileFinished(index, success, success ? QString() : "Generation failed");
    }, Qt::QueuedConnection);

    m_completedCount.fetchAndAddOrdered(1);
    checkIfFinished();
}

void ThumbnailGeneratorWorker::checkIfFinished()
{
    int completed = m_completedCount.loadRelaxed();
    if (completed >= m_tasks.size()) {
        int success = m_successCount.loadRelaxed();
        int failed = m_failCount.loadRelaxed();
        bool cancelled = m_cancelling.loadRelaxed();

        QMetaObject::invokeMethod(this, [this, success, failed, cancelled]() {
            if (cancelled) {
                emit logLine(QString("Cancelled. Generated: %1, Failed: %2").arg(success).arg(failed));
                emit queueFinished(false);
            } else {
                emit logLine(QString("Completed. Generated: %1, Failed: %2").arg(success).arg(failed));
                emit queueFinished(failed == 0);
            }
        }, Qt::QueuedConnection);
    }
}

bool ThumbnailGeneratorWorker::generateImageThumbnail(int index, const QString& filePath, const QSize& size)
{
    // Load image and generate single thumbnail
    QFileInfo fileInfo(filePath);
    QImage image(filePath);
    if (image.isNull()) {
        emit logLine(QString("  ERROR: Failed to load image - format: %1, size: %2 bytes")
            .arg(fileInfo.suffix().toUpper())
            .arg(fileInfo.size()));
        emit logLine(QString("  Path: %1").arg(filePath));
        return false;
    }

    emit logLine(QString("  Loaded image: %1x%2, format: %3")
        .arg(image.width())
        .arg(image.height())
        .arg(fileInfo.suffix().toUpper()));

    // Scale to thumbnail size
    QImage scaled = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap pixmap = QPixmap::fromImage(scaled);

    // Store in cache
    ThumbnailCacheManager& cache = ThumbnailCacheManager::instance();
    if (!cache.storeThumbnail(filePath, size, 0.0, pixmap)) {
        emit logLine(QString("  ERROR: Failed to store thumbnail"));
        return false;
    }

    emit logLine(QString("  Generated 1 thumbnail"));
    return true;
}

bool ThumbnailGeneratorWorker::generateVideoThumbnails(int index, const QString& filePath, const QSize& size)
{
    // Generate thumbnails at multiple positions for scrubbing
    const QVector<qreal> positions = { 0.0, 0.25, 0.5, 0.75, 1.0 };

    QFileInfo fileInfo(filePath);
    emit logLine(QString("  Video format: %1, size: %2 MB")
        .arg(fileInfo.suffix().toUpper())
        .arg(fileInfo.size() / (1024.0 * 1024.0), 0, 'f', 2));

    ThumbnailCacheManager& cache = ThumbnailCacheManager::instance();
    int generated = 0;

    for (int i = 0; i < positions.size(); ++i) {
        qreal pos = positions[i];

        // Calculate position in milliseconds
        // We need to get video duration first
        qint64 durationMs = GStreamerPlayer::queryDuration(filePath);
        if (durationMs <= 0) {
            emit logLine(QString("  ERROR: Failed to get video duration - file may be corrupt or unsupported codec"));
            emit logLine(QString("  Path: %1").arg(filePath));
            return false;
        }

        if (i == 0) {
            emit logLine(QString("  Video duration: %1 seconds").arg(durationMs / 1000.0, 0, 'f', 2));
        }

        qint64 positionMs = static_cast<qint64>(pos * durationMs);

        // Extract thumbnail using GStreamer
        QImage thumbnail = GStreamerPlayer::extractThumbnail(filePath, size, positionMs);
        if (thumbnail.isNull()) {
            emit logLine(QString("  WARNING: Failed to extract thumbnail at position %1 (%2s)")
                .arg(pos)
                .arg(positionMs / 1000.0, 0, 'f', 2));
            continue;
        }

        QPixmap pixmap = QPixmap::fromImage(thumbnail);
        if (!cache.storeThumbnail(filePath, size, pos, pixmap)) {
            emit logLine(QString("  WARNING: Failed to store thumbnail at position %1").arg(pos));
            continue;
        }

        ++generated;
        QMetaObject::invokeMethod(this, [this, index, i, positions]() {
            emit fileProgress(index, (i + 1) * 100 / positions.size());
        }, Qt::QueuedConnection);
    }

    emit logLine(QString("  Generated %1/%2 thumbnails").arg(generated).arg(positions.size()));
    return generated > 0;
}

bool ThumbnailGeneratorWorker::generateSequenceThumbnails(int index, const Task& task, const QSize& size)
{
    if (task.sequenceFrames.isEmpty()) {
        emit logLine(QString("  ERROR: No frames in sequence"));
        return false;
    }

    // Generate thumbnails at multiple positions in the sequence
    const QVector<qreal> positions = { 0.0, 0.25, 0.5, 0.75, 1.0 };

    ThumbnailCacheManager& cache = ThumbnailCacheManager::instance();
    int generated = 0;
    int frameCount = task.sequenceFrames.size();

    for (int i = 0; i < positions.size(); ++i) {
        qreal pos = positions[i];

        // Calculate frame index
        int frameIndex = static_cast<int>(pos * (frameCount - 1));
        frameIndex = qBound(0, frameIndex, frameCount - 1);

        QString framePath = task.sequenceFrames[frameIndex];

        // Load frame
        QImage image(framePath);
        if (image.isNull()) {
            emit logLine(QString("  WARNING: Failed to load frame at position %1").arg(pos));
            continue;
        }

        // Scale to thumbnail size
        QImage scaled = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmap pixmap = QPixmap::fromImage(scaled);

        // Store in cache (use sequence representative path)
        if (!cache.storeThumbnail(task.filePath, size, pos, pixmap)) {
            emit logLine(QString("  WARNING: Failed to store thumbnail at position %1").arg(pos));
            continue;
        }

        ++generated;
        QMetaObject::invokeMethod(this, [this, index, i, positions]() {
            emit fileProgress(index, (i + 1) * 100 / positions.size());
        }, Qt::QueuedConnection);
    }

    emit logLine(QString("  Generated %1/%2 thumbnails from %3 frames").arg(generated).arg(positions.size()).arg(frameCount));
    return generated > 0;
}

