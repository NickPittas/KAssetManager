#include "live_preview_manager.h"

#include "oiio_image_loader.h"
#include "utils.h"
#include "media/gstreamer_player.h"
#include "thumbnail_cache_manager.h"


#include <QtConcurrent/QtConcurrentRun>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMutexLocker>
#include <QDir>
#include <algorithm>
#include <cmath>

#include <memory>

#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}
#endif

#if defined(HAVE_GSTREAMER) && HAVE_GSTREAMER
#include <gst/gst.h>
#endif

namespace {

constexpr int kMinCacheEntries = 64;
constexpr int kMaxCacheEntries = 2048;
constexpr qint64 kSeqUpperSearchStart = 10000000; // 10M
constexpr int kSeqUpperSearchMaxDoublings = 32;
constexpr qint64 kSeqUpperSearchHardCap = 100000000; // 100M
constexpr int kDecodeSafetyIterMax = 256;

constexpr qreal kDefaultPosterPosition = 0.05; // pick early frame for motion clips
constexpr int kSequenceMetaTtlMs = 30000;

// Cache for video durations to avoid repeated GStreamer queries during scrubbing
static QHash<QString, qint64> s_durationCache;
static QMutex s_durationCacheMutex;

#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
QString ffmpegErrorString(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}
#else
static QString ffmpegErrorString(int err)
{
    return QString::number(err);
}
#endif

bool isImageExtension(const QString& suffix)
{
    static const QSet<QString> kImageExt = {
        "png", "jpg", "jpeg", "bmp", "tga", "tiff", "tif",
        "gif", "webp", "ico", "heic", "heif", "avif", "psd"
    };
    return kImageExt.contains(suffix.toLower());
}

bool isHdrExtension(const QString& suffix)
{
    static const QSet<QString> kHdrExt = { "exr", "hdr", "pfm", "dpx" };
    return kHdrExt.contains(suffix.toLower());
}

bool isSequenceFriendlyExtension(const QString& suffix)
{
    static const QSet<QString> kSequenceExt = {
        "exr", "dpx", "png", "jpg", "jpeg", "tga", "tiff", "tif", "bmp"
    };
    return kSequenceExt.contains(suffix.toLower());
}

bool isVideoExtension(const QString& suffix)
{
    static const QSet<QString> kVideoExt = {
        "mov", "qt", "mp4", "m4v", "mxf", "avi", "mkv", "webm",
        "mpg", "mpeg", "m2v", "m2ts", "mts", "wmv", "asf", "flv",
        "f4v", "ts", "ogv", "y4m", "3gp", "3g2"
    };
    return kVideoExt.contains(suffix.toLower());
}

}

LivePreviewManager& LivePreviewManager::instance()
{
    static LivePreviewManager s_instance;
    return s_instance;
}

LivePreviewManager::LivePreviewManager(QObject* parent)
    : QObject(parent)
{
#if 1
    // Explicitly log which renderer/backend is active for verification in app.log
    // tlRender is not integrated; we use OIIO for images + Qt for presentation
    qInfo() << "[LivePreview] Renderer backend:" << "OIIO+Qt (no tlRender)";
#endif
    // Initialize QCache capacity based on default setting
    // Cost is in KB units; assume ~256KB per thumbnail (256x256x4 bytes)
    // So maxCost = entries * 256 KB
    m_cache.setMaxCost(m_maxCacheEntries * 256);
    m_sequenceMetaCache.setMaxCost(m_sequenceMetaLimit);
    
    // Create dedicated thread pool for decode operations
    // This ensures thumbnail decoding spreads across all CPU cores
    // and doesn't compete with Qt's global thread pool for other work
    m_decodePool = new QThreadPool(this);
    int idealThreads = QThread::idealThreadCount();
    // Use all available cores but cap at a reasonable maximum
    int poolSize = qBound(2, idealThreads, 16);
    m_decodePool->setMaxThreadCount(poolSize);
    m_decodePool->setExpiryTimeout(30000); // 30 sec thread expiry for efficiency
    
    m_lastRequestTime.start();

    qInfo() << "[LivePreview] LivePreviewManager initialized with GStreamer backend,"
            << poolSize << "decode threads";
}

LivePreviewManager::FrameHandle LivePreviewManager::cachedFrame(const QString& filePath, const QSize& targetSize, qreal position)
{
    // Check memory cache FIRST (faster than disk)
    // Use read lock for fast concurrent cache reads during paint()
    const QString key = makeCacheKey(filePath, targetSize, position);
    {
        QReadLocker locker(&m_cacheLock);
        if (auto* entry = m_cache.object(key)) {
            QMutexLocker statsLocker(&m_mutex);
            ++m_cacheHits;
            return { entry->pixmap, entry->position, entry->size };
        }
    }

    // Then check persistent disk cache
    ThumbnailCacheManager& persistentCache = ThumbnailCacheManager::instance();
    QPixmap persistentPixmap = persistentCache.getCachedThumbnail(filePath, targetSize, position);
    if (!persistentPixmap.isNull()) {
        // Store in memory cache for faster subsequent access
        storeFrame(key, persistentPixmap, position, targetSize);
        return { persistentPixmap, position, targetSize };
    }

    return {};
}

void LivePreviewManager::requestFrame(const QString& filePath, const QSize& targetSize, qreal position)
{
    // Skip new decode requests entirely when suspended (during resize/splitter drag)
    // Cached frames are still returned by cachedFrame(), but no new work is queued
    if (m_requestsSuspended.load()) {
        return;
    }
    
    // Rapid-event throttling: if requests are coming too fast (e.g., during resize/scroll),
    // AND mouse button is pressed (user is actively dragging), skip entirely.
    // This prevents thumbnail decode work during window resize and splitter drags.
    qint64 elapsed = m_lastRequestTime.elapsed();
    if (elapsed < 16) { // Less than ~1 frame at 60fps
        // If mouse is down (resize/splitter drag in progress), skip all new requests
        if (QGuiApplication::mouseButtons() != Qt::NoButton) {
            return; // Will be re-requested when user releases mouse
        }
        // During rapid events without mouse drag, limit concurrent work
        if (m_pendingDecodes.load() >= kMaxConcurrentDecodes / 2) {
            return; // Skip this request - will be re-requested on next paint
        }
    }
    m_lastRequestTime.restart();
    
    // Extract suffix from path directly (avoid QFileInfo for performance)
    QString suffix;
    int dotPos = filePath.lastIndexOf(QLatin1Char('.'));
    if (dotPos >= 0 && dotPos < filePath.length() - 1) {
        suffix = filePath.mid(dotPos + 1).toLower();
    }
    
    if (!isImageExtension(suffix) && !isHdrExtension(suffix) &&
        !isSequenceFriendlyExtension(suffix) && !isVideoExtension(suffix)) {
        return;
    }

    const QString key = makeCacheKey(filePath, targetSize, position);
    
    // Check memory cache FIRST (fastest)
    // Check memory cache FIRST (fastest) - use read lock for concurrent reads
    {
        QReadLocker cacheLocker(&m_cacheLock);
        if (auto* cached = m_cache.object(key)) {
            QMutexLocker statsLocker(&m_mutex);
            ++m_cacheHits;
            QPixmap pixmap = cached->pixmap;
            QSize cachedSize = cached->size;
            qreal cachedPos = cached->position;
            statsLocker.unlock();
            cacheLocker.unlock();
            emit frameReady(filePath, cachedPos, cachedSize, pixmap);
            return;
        }
    }
    
    // Check if already in flight (separate from cache)
    {
        QMutexLocker locker(&m_mutex);
        if (m_inFlight.contains(key)) {
            return;
        }
    }

    // Then check persistent disk cache
    ThumbnailCacheManager& persistentCache = ThumbnailCacheManager::instance();
    QPixmap persistentPixmap = persistentCache.getCachedThumbnail(filePath, targetSize, position);
    if (!persistentPixmap.isNull()) {
        // Store in memory cache and emit immediately
        storeFrame(key, persistentPixmap, position, targetSize);
        emit frameReady(filePath, position, targetSize, persistentPixmap);
        return;
    }

    // Check file exists before queueing expensive decode (use QFileInfo only if needed)
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return;
    }

    {
        QMutexLocker locker(&m_mutex);
        m_inFlight.insert(key);
        ++m_cacheMisses;
    }

    Request request { filePath, targetSize, position };
    enqueueDecode(request, key);
}

void LivePreviewManager::invalidate(const QString& filePath)
{
    // Remove cached entries for this file path by scanning keys
    {
        QWriteLocker cacheLocker(&m_cacheLock);
        const auto keys = m_cache.keys();
        for (const QString& k : keys) {
            if (k.startsWith(filePath + "|")) {
                m_cache.remove(k);
            }
        }
    }
    // Clear any in-flight requests for this file path
    {
        QMutexLocker locker(&m_mutex);
        for (auto it = m_inFlight.begin(); it != m_inFlight.end(); ) {
            if (it->startsWith(filePath + "|")) {
                it = m_inFlight.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void LivePreviewManager::clear()
{
    {
        QWriteLocker cacheLocker(&m_cacheLock);
        m_cache.clear();
    }
    {
        QMutexLocker locker(&m_mutex);
        m_inFlight.clear();
        m_sequenceQueue.clear();
    }
}

void LivePreviewManager::cancelPending()
{
    // Increment cancellation token - all in-flight decode tasks will see this
    // and abort early, making UI clicks immediately responsive
    ++m_cancellationToken;
    
    QMutexLocker locker(&m_mutex);
    // Clear pending queues
    m_inFlight.clear();
    m_sequenceQueue.clear();
    m_activeSequenceLoads = 0;
    
    qDebug() << "[LivePreview] Cancelled all pending requests, token:" << m_cancellationToken.load();
}

int LivePreviewManager::cacheEntryCount() const
{
    QReadLocker locker(&m_cacheLock);
    return m_cache.size();
}

void LivePreviewManager::setMaxCacheEntries(int maxEntries)
{
    // Bounds: kMinCacheEntries-kMaxCacheEntries
    const int bounded = qBound(kMinCacheEntries, maxEntries, kMaxCacheEntries);
    {
        QMutexLocker locker(&m_mutex);
        m_maxCacheEntries = bounded;
    }
    {
        QWriteLocker cacheLocker(&m_cacheLock);
        // Cost is in KB units; assume ~256KB per thumbnail (256x256x4 bytes)
        // So maxCost = entries * 256 KB
        m_cache.setMaxCost(bounded * 256);
    }
    qInfo() << "[LivePreview] Cache size set to" << bounded << "entries (~" << (bounded * 256 / 1024) << "MB)";
}

int LivePreviewManager::maxCacheEntries() const
{
    QMutexLocker locker(&m_mutex);
    return m_maxCacheEntries;
}


quint64 LivePreviewManager::cacheHits() const { QMutexLocker locker(&m_mutex); return m_cacheHits; }
quint64 LivePreviewManager::cacheMisses() const { QMutexLocker locker(&m_mutex); return m_cacheMisses; }
double LivePreviewManager::cacheHitRate() const { QMutexLocker locker(&m_mutex); const quint64 total = m_cacheHits + m_cacheMisses; return total ? double(m_cacheHits) / double(total) : 0.0; }

void LivePreviewManager::suspendRequests()
{
    m_requestsSuspended.store(true);
}

void LivePreviewManager::resumeRequests()
{
    m_requestsSuspended.store(false);
}

bool LivePreviewManager::isRequestsSuspended() const
{
    return m_requestsSuspended.load();
}

void LivePreviewManager::setSequenceDetectionEnabled(bool enabled)
{
    QMutexLocker locker(&m_mutex);
    m_sequenceDetectionEnabled = enabled;
    qInfo() << "[LivePreview] Sequence detection" << (enabled ? "ENABLED" : "DISABLED");
}

bool LivePreviewManager::sequenceDetectionEnabled() const
{
    QMutexLocker locker(&m_mutex);
    return m_sequenceDetectionEnabled;
}

QString LivePreviewManager::makeCacheKey(const QString& filePath, const QSize& targetSize, qreal position) const
{
    // Faster than chained .arg(); avoids temporary allocations
    QString key;
    key.reserve(filePath.size() + 32);
    key += filePath;
    key += '|';
    key += QString::number(targetSize.width());
    key += 'x';
    key += QString::number(targetSize.height());
    key += '|';
    key += QString::number(position, 'f', 3);
    return key;
}

void LivePreviewManager::enqueueDecode(const Request& request, const QString& cacheKey)
{
    bool seqDetectionEnabled = false;
    {
        QMutexLocker locker(&m_mutex);
        seqDetectionEnabled = m_sequenceDetectionEnabled;
    }

    if (seqDetectionEnabled && isImageSequence(request.filePath)) {
        enqueueSequenceDecode(request, cacheKey);
        return;
    }
    startDecodeTask(request, cacheKey, false);
}

void LivePreviewManager::enqueueSequenceDecode(const Request& request, const QString& cacheKey)
{
    SequenceTask task{request, cacheKey, sequenceHead(request.filePath)};

    QMutexLocker locker(&m_mutex);

    if (m_activeSequenceLoads < m_maxSequenceLoads) {
        ++m_activeSequenceLoads;
        locker.unlock();
        startDecodeTask(task.request, task.cacheKey, true);
        return;
    }

    // Replace any queued request for the same sequence head with the most recent.
    for (auto it = m_sequenceQueue.begin(); it != m_sequenceQueue.end();) {
        if (it->head == task.head) {
            m_inFlight.remove(it->cacheKey);
            it = m_sequenceQueue.erase(it);
        } else {
            ++it;
        }
    }

    if (m_sequenceQueue.size() >= m_sequenceQueueLimit) {
        SequenceTask dropped = m_sequenceQueue.takeFirst();
        m_inFlight.remove(dropped.cacheKey);
    }

    m_sequenceQueue.append(task);
}

void LivePreviewManager::startDecodeTask(const Request& request, const QString& cacheKey, bool fromSequenceQueue)
{
    // Throttle: limit concurrent decodes to prevent overloading during rapid resize/scroll
    int pending = m_pendingDecodes.load();
    if (pending >= kMaxConcurrentDecodes) {
        // Too many pending - skip this request, it will be re-requested on next paint
        QMutexLocker locker(&m_mutex);
        m_inFlight.remove(cacheKey);
        return;
    }
    ++m_pendingDecodes;
    
    // Capture sequence detection state and cancellation token at the time of task creation
    bool seqDetectionEnabled = false;
    uint64_t startToken = m_cancellationToken.load();
    {
        QMutexLocker locker(&m_mutex);
        seqDetectionEnabled = m_sequenceDetectionEnabled;
    }

    // Use dedicated thread pool for decode tasks - ensures proper parallelism
    // across all CPU cores without competing with Qt's global thread pool
    auto task = [this, request, cacheKey, fromSequenceQueue, seqDetectionEnabled, startToken]() {
        // Decrement pending count when done (even if cancelled)
        struct PendingGuard {
            std::atomic<int>& counter;
            ~PendingGuard() { --counter; }
        } guard{m_pendingDecodes};
        
        // Early abort if cancelled before we even start
        if (m_cancellationToken.load() != startToken) {
            return;
        }
        
        QString error;
        QImage image;

        const bool treatAsSequence = fromSequenceQueue || (seqDetectionEnabled && isImageSequence(request.filePath));
        if (treatAsSequence) {
            image = loadSequenceFrame(request, error);
        } else {
            QFileInfo info(request.filePath);
            const QString suffix = info.suffix().toLower();
            if (isImageExtension(suffix) || isHdrExtension(suffix)) {
                image = loadImageFrame(request, error);
            } else {
                image = loadVideoFrame(request, error);
            }
        }
        
        // Check again after decode - if cancelled, don't bother posting result
        if (m_cancellationToken.load() != startToken) {
            return;
        }

        QMetaObject::invokeMethod(this, [this, request, cacheKey, image, error, fromSequenceQueue, startToken]() {
            // Final check on UI thread - if cancelled, discard result
            if (m_cancellationToken.load() != startToken) {
                return;
            }
            
            SequenceTask nextTask;
            bool launchNext = false;

            {
                QMutexLocker locker(&m_mutex);
                m_inFlight.remove(cacheKey);
                if (fromSequenceQueue) {
                    if (m_activeSequenceLoads > 0) {
                        --m_activeSequenceLoads;
                    }
                    if (!m_sequenceQueue.isEmpty()) {
                        nextTask = m_sequenceQueue.takeLast();
                        ++m_activeSequenceLoads;
                        launchNext = true;
                    }
                }
            }

            if (image.isNull()) {
                emit frameFailed(request.filePath, error.isEmpty() ? QStringLiteral("Unable to decode frame") : error);
            } else {
                QPixmap pixmap = QPixmap::fromImage(image);
                if (pixmap.isNull()) {
                    emit frameFailed(request.filePath, QStringLiteral("Failed to convert image to pixmap"));
                } else {
                    storeFrame(cacheKey, pixmap, request.position, request.targetSize);
                    emit frameReady(request.filePath, request.position, request.targetSize, pixmap);
                }
            }

            if (launchNext) {
                startDecodeTask(nextTask.request, nextTask.cacheKey, true);
            }
        }, Qt::QueuedConnection);
    };
    
    // Submit to dedicated decode pool for proper multi-core utilization
    m_decodePool->start(task);
}

void LivePreviewManager::storeFrame(const QString& key, const QPixmap& pixmap, qreal position, const QSize& size)
{
    // Calculate actual memory cost for better cache eviction
    // Approximate bytes: width * height * depth / 8 (depth is typically 32 bits)
    int cost = (pixmap.width() * pixmap.height() * pixmap.depth()) / 8;
    if (cost < 1) cost = 1; // Minimum cost of 1
    // Normalize cost to reasonable units (1KB = 1 cost unit)
    cost = qMax(1, cost / 1024);
    
    auto* entry = new CachedEntry();
    entry->pixmap = pixmap;
    entry->position = position;
    entry->size = size;
    
    // Use write lock for cache insertion
    QWriteLocker locker(&m_cacheLock);
    m_cache.insert(key, entry, cost);
}

bool LivePreviewManager::isImageSequence(const QString& filePath) const
{
    QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    if (!isSequenceFriendlyExtension(suffix)) {
        return false;
    }
    QString base = info.completeBaseName();
    static QRegularExpression seqPattern(R"(.*(?:\d{2,}|%0\d+d|\#\#\#).*)");
    return seqPattern.match(base).hasMatch();
}

QString LivePreviewManager::sequenceHead(const QString& filePath)
{
    QFileInfo info(filePath);
    QString dir = info.absolutePath();
    QString base = info.completeBaseName();
    QRegularExpression digits(R"((\d+)(?!.*\d))");
    auto match = digits.match(base);
    if (!match.hasMatch()) {
        return info.absoluteFilePath();
    }
    QString head = base.left(match.capturedStart(1));
    return dir + "/" + head;
}

QImage LivePreviewManager::loadImageFrame(const Request& request, QString& error)
{
    QFileInfo info(request.filePath);
    if (!info.exists()) {
        error = QStringLiteral("File does not exist");
        return {};
    }

    const QString suffix = info.suffix().toLower();
    QImage image;

    // Try OpenImageIO first for formats it supports (PSD, TIFF, EXR, HDR, etc.)
    if (OIIOImageLoader::isOIIOSupported(request.filePath)) {
#if defined(HAVE_OPENIMAGEIO) && HAVE_OPENIMAGEIO
        image = OIIOImageLoader::loadImage(request.filePath, request.targetSize.width(), request.targetSize.height());
        if (image.isNull()) {
            qDebug() << "[LivePreview] OIIO failed to load, falling back to Qt:" << request.filePath;
        }
#endif
    }

    // Fall back to Qt's image reader if OIIO didn't work or isn't available
    if (image.isNull()) {
        QImageReader reader(request.filePath);
        reader.setAutoTransform(true);
        // DO NOT use setScaledSize() here - it forces exact dimensions and ignores aspect ratio
        // Instead, load the full image and let the scaling below handle aspect ratio preservation
        image = reader.read();
        if (image.isNull()) {
            error = reader.errorString();
        }
    }

    if (!image.isNull() && request.targetSize.isValid()) {
        image = image.scaled(request.targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

void LivePreviewManager::pruneSequenceMetaCache()
{
    // TTL-based purge; size is enforced by QCache maxCost
    const QList<QString> keys = m_sequenceMetaCache.keys();
    for (const QString& key : keys) {
        if (SequenceMeta* meta = m_sequenceMetaCache.object(key)) {
            if (meta->lastScan.isValid() && meta->lastScan.elapsed() > kSequenceMetaTtlMs) {
                m_sequenceMetaCache.remove(key);
            }
        }
    }
}

LivePreviewManager::SequenceMeta LivePreviewManager::sequenceMetaFor(const QString& filePath, QString& error)
{
    const QString head = sequenceHead(filePath);
    {
        QMutexLocker locker(&m_mutex);
        if (SequenceMeta* cached = m_sequenceMetaCache.object(head)) {
            if (!cached->lastScan.isValid() || cached->lastScan.elapsed() < kSequenceMetaTtlMs) {
                if (!cached->lastScan.isValid()) {
                    cached->lastScan.start();
                } else {
                    cached->lastScan.restart();
                }
                return *cached; // return a copy
            }
        }
    }

    SequenceMeta meta;
    meta.head = head;

    QFileInfo info(filePath);
    if (!info.exists()) {
        error = QStringLiteral("Sequence member missing");
        return meta;
    }

    const QString fileName = info.fileName();
    QRegularExpression digits(R"((\d+)(?!.*\d))");
    QRegularExpressionMatch match = digits.match(fileName);
    if (!match.hasMatch()) {
        error = QStringLiteral("Sequence pattern not found");
        return meta;
    }

    meta.directory = info.absolutePath();
    meta.prefix = fileName.left(match.capturedStart(1));
    meta.suffix = fileName.mid(match.capturedEnd(1));
    meta.padding = match.capturedLength();

    QDir dir(meta.directory);

    auto existsFrame = [&](qint64 n) -> bool {
        if (n < 0) return false;
        const QString digits = QString::number(n).rightJustified(meta.padding, QLatin1Char('0'));
        const QString fileName = meta.prefix + digits + meta.suffix;
        return QFileInfo(dir.filePath(fileName)).exists();
    };

    // Use the current file's number as an anchor
    const qint64 curN = match.captured(1).toLongLong();

    // 1) Find first frame via binary search in [0, curN]
    meta.firstFrame = Utils::binarySearchFirstTrue(-1, curN, existsFrame);

    // 2) Find last frame using halving from a large bound then binary search
    const qint64 START_HUGE = kSeqUpperSearchStart;
    qint64 lastKnownExist = curN;
    qint64 lastKnownNonExist = -1;

    qint64 probe = START_HUGE;
    while (probe > lastKnownExist) {
        if (existsFrame(probe)) { lastKnownExist = probe; break; }
        lastKnownNonExist = probe;
        probe /= 2;
    }
    if (lastKnownExist == curN) {
        qint64 up = std::max<qint64>(curN + 1, 2 * curN);
        for (int i = 0; i < kSeqUpperSearchMaxDoublings; ++i) {
            if (!existsFrame(up)) { lastKnownNonExist = up; break; }
            if (up > kSeqUpperSearchHardCap) { lastKnownNonExist = up + 1; break; }
            up *= 2;
        }
        if (lastKnownNonExist < 0) lastKnownNonExist = curN + 1;
    } else {
        if (lastKnownNonExist < 0) lastKnownNonExist = lastKnownExist + 1;
    }

    if (lastKnownNonExist <= lastKnownExist) lastKnownNonExist = lastKnownExist + 1;
    meta.lastFrame = Utils::binarySearchLastTrue(lastKnownExist, lastKnownNonExist, existsFrame);

    if (meta.frames.isEmpty()) {
        if (!(meta.padding > 0 && meta.firstFrame >= 0 && meta.lastFrame >= meta.firstFrame)) {
            error = QStringLiteral("No sequence frames detected");
            return meta;
        }
    }

    meta.lastScan.start();
    {
        QMutexLocker locker(&m_mutex);
        pruneSequenceMetaCache();
        m_sequenceMetaCache.insert(head, new SequenceMeta(meta), 1);
    }

    return meta;
}

QImage LivePreviewManager::loadSequenceFrame(const Request& request, QString& error)
{
    LivePreviewManager& mgr = LivePreviewManager::instance();
    SequenceMeta meta = mgr.sequenceMetaFor(request.filePath, error);
    if (!meta.isValid()) {
        if (error.isEmpty()) {
            return loadImageFrame(request, error);
        }
        return {};
    }

    int frameCount = meta.frames.size();
    if (frameCount <= 0) {
        if (meta.firstFrame >= 0 && meta.lastFrame >= meta.firstFrame) {
            frameCount = int(meta.lastFrame - meta.firstFrame + 1);
        }
    }
    if (frameCount <= 0) {
        error = QStringLiteral("Sequence has no frames");
        return {};
    }

    qreal normalized = request.position;
    normalized = std::clamp(normalized, 0.0, 1.0);

    int frameIndex = 0;
    if (frameCount > 1) {
        frameIndex = static_cast<int>(std::round(normalized * (frameCount - 1)));
        frameIndex = std::clamp(frameIndex, 0, frameCount - 1);
    }

    Request frameRequest = request;
    if (!meta.frames.isEmpty()) {
        frameRequest.filePath = meta.frames.at(frameIndex);
    } else {
        const qint64 frameNumber = meta.firstFrame + frameIndex;
        const QString digits = QString::number(frameNumber).rightJustified(meta.padding, QLatin1Char('0'));
        frameRequest.filePath = QDir(meta.directory).filePath(meta.prefix + digits + meta.suffix);
    }

    qDebug() << "[LivePreview] Sequence load: requested=" << request.filePath
             << "position=" << request.position
             << "frameIndex=" << frameIndex
             << "actualFile=" << frameRequest.filePath;

    QString frameError;
    QImage image = loadImageFrame(frameRequest, frameError);
    if (image.isNull()) {
        error = frameError;
    }
    return image;
}

QImage LivePreviewManager::loadVideoFrame(const Request& request, QString& error)
{
#if defined(HAVE_GSTREAMER) && HAVE_GSTREAMER
    // Treat thumbnails EXACTLY like the preview pane:
    // - Use a PERSISTENT headless GStreamer pipeline with appsink (no video windows!)
    // - Load the video once and keep it in PAUSED state
    // - For each scrub position, just SEEK (like preview pane timeline scrubbing)
    // - Pull the frame from appsink
    // This is EXACTLY how preview pane works, just with appsink instead of video widget

    QFileInfo info(request.filePath);
    if (!info.exists()) {
        error = QStringLiteral("File does not exist");
        return {};
    }

    // Use a static cache to avoid repeated duration queries
    static QHash<QString, qint64> s_durationCache;
    static QMutex s_durationMutex;

    qint64 durationMs = 0;

    // Check cache first
    {
        QMutexLocker locker(&s_durationMutex);
        if (s_durationCache.contains(request.filePath)) {
            durationMs = s_durationCache[request.filePath];
        }
    }

    // If not cached, query duration using the new lightweight function
    if (durationMs == 0) {
        durationMs = GStreamerPlayer::queryDuration(request.filePath);

        if (durationMs <= 0) {
            error = QStringLiteral("Failed to get video duration");
            return {};
        }

        // Cache the duration
        QMutexLocker locker(&s_durationMutex);
        s_durationCache[request.filePath] = durationMs;
        qDebug() << "[LivePreview] Cached duration for" << request.filePath << ":" << durationMs << "ms";
    }

    // Calculate absolute position from normalized position (0.0 to 1.0)
    // Left edge of thumbnail = 0.0 (first frame), Right edge = 1.0 (last frame)
    qint64 positionMs = static_cast<qint64>(request.position * durationMs);
    positionMs = std::clamp(positionMs, 0LL, durationMs);

    qDebug() << "[LivePreview] Scrubbing to position:" << request.position << "-> " << positionMs << "ms (duration:" << durationMs << "ms)";

    // Use extractThumbnail - it uses the SAME seeking mechanism as the media player
    QImage thumbnail = GStreamerPlayer::extractThumbnail(request.filePath, request.targetSize, positionMs);

    if (thumbnail.isNull()) {
        error = QStringLiteral("Failed to decode video frame with GStreamer");
        return {};
    }

    return thumbnail;

#else
    Q_UNUSED(request);
    Q_UNUSED(error);
    // GStreamer not available - return empty image
    return QImage();
#endif
}
