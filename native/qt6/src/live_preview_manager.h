#pragma once

#include <QObject>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QReadWriteLock>
#include <QElapsedTimer>
#include <QList>
#include <QStringList>
#include <QThreadPool>
#include <QThread>

#include <QCache>


/**
 * LivePreviewManager streams preview frames for stills, video clips, and image sequences
 * without persisting thumbnails to disk. It exposes a lightweight request API that returns
 * cached pixmaps synchronously when available and emits signals when asynchronous decoding
 * completes.
 *
 * The manager is intentionally agnostic of any particular view; callers provide the
 * requested normalized position (0-1) and target size. Internally the manager performs
 * smart caching and throttles expensive requests so scrubbing stays responsive.
 *
 * **Thread Safety:**
 * - All public methods are thread-safe via internal QMutex (m_mutex)
 * - cachedFrame() and requestFrame() can be called from any thread
 * - Signals (frameReady, frameFailed) are emitted from worker threads; connect with Qt::QueuedConnection
 * - The cache is protected by m_mutex; concurrent access is serialized
 * - Decode operations run on QThreadPool; no blocking on GUI thread
 *
 * **Memory Management:**
 * - Cache uses LRU eviction when m_maxCacheEntries is exceeded
 * - Sequence metadata cache (m_sequenceMetaCache) is pruned periodically
 * - All QPixmap objects are stored in m_cache and managed by the manager
 * - Callers should not retain references to returned pixmaps beyond the current scope
 */
class LivePreviewManager : public QObject {
    Q_OBJECT

public:
    struct Request {
        QString filePath;
        QSize targetSize;
        qreal position = 0.0; // Normalized [0,1], 0 for poster frame
    };

    struct FrameHandle {
        QPixmap pixmap;
        qreal position = 0.0;
        QSize size;
        bool isValid() const { return !pixmap.isNull(); }
    };

    static LivePreviewManager& instance();

    // Returns cached pixmap if available; otherwise empty.
    FrameHandle cachedFrame(const QString& filePath, const QSize& targetSize, qreal position = 0.0);

    // Queue asynchronous decode for the requested asset/frame.
    void requestFrame(const QString& filePath, const QSize& targetSize, qreal position = 0.0,
                      bool bypassSuspension = false);

    // Remove cached entries for a specific asset (all sizes/positions).
    void invalidate(const QString& filePath);
    void clear();
    
    // Cancel all pending decode requests (for responsive navigation)
    // This allows user clicks to take priority over background thumbnail generation
    void cancelPending();
    
    int cacheEntryCount() const;

    // Configure cache size (bounds: 64-2048 entries)
    void setMaxCacheEntries(int maxEntries);
    int maxCacheEntries() const;

    // Enable/disable automatic sequence detection for File Manager
    void setSequenceDetectionEnabled(bool enabled);
    bool sequenceDetectionEnabled() const;

    // Suspend/resume thumbnail requests during resize operations
    // When suspended, requestFrame() calls are ignored (cached frames still returned)
    // Call resumeRequests() on mouse-up to trigger viewport refresh
    void suspendRequests();
    void resumeRequests();
    bool isRequestsSuspended() const;

    // Cache metrics
    quint64 cacheHits() const;
    quint64 cacheMisses() const;
    double cacheHitRate() const; // [0,1]

signals:
    void frameReady(const QString& filePath, qreal position, QSize targetSize, const QPixmap& pixmap);
    void frameFailed(const QString& filePath, QString errorString);
    void cacheStatus(const QString& status);

private:
    explicit LivePreviewManager(QObject* parent = nullptr);

    QString makeCacheKey(const QString& filePath, const QSize& targetSize, qreal position) const;
    void enqueueDecode(const Request& request, const QString& cacheKey);
    void enqueueSequenceDecode(const Request& request, const QString& cacheKey);
    void startDecodeTask(const Request& request, const QString& cacheKey, bool fromSequenceQueue);
    static QImage loadImageFrame(const Request& request, QString& error);
    QImage loadVideoFrame(const Request& request, QString& error, uint64_t cancellationToken);
    static QImage loadSequenceFrame(const Request& request, QString& error);
    bool isImageSequence(const QString& filePath) const;
    static QString sequenceHead(const QString& filePath);
    struct SequenceMeta;
    SequenceMeta sequenceMetaFor(const QString& filePath, QString& error);
    void pruneSequenceMetaCache();
    bool shouldSuppressRecentFailure(const QString& filePath, const QString& cacheKey) const;
    void rememberDecodeFailure(const QString& filePath, const QString& cacheKey, const QString& error);
    void clearDecodeFailure(const QString& filePath, const QString& cacheKey);

    void storeFrame(const QString& key, const QPixmap& pixmap, qreal position, const QSize& size);

    struct CachedEntry {
        QPixmap pixmap;
        qreal position = 0.0;
        QSize size;
    };

    struct SequenceTask {
        Request request;
        QString cacheKey;
        QString head;
    };

    struct SequenceMeta {
        QString head;
        QString directory;
        QString prefix;
        QString suffix;
        int padding = 0;
        QStringList frames; // Optional: may be empty when using fast detection
        qint64 firstFrame = -1;
        qint64 lastFrame = -1;
        QElapsedTimer lastScan;
        bool isValid() const {
            return !frames.isEmpty() || (padding > 0 && firstFrame >= 0 && lastFrame >= firstFrame);
        }
    };

    mutable QMutex m_mutex;  // For inFlight, sequenceQueue, sequenceMeta
    mutable QReadWriteLock m_cacheLock;  // Reader-writer lock for cache (faster reads during paint)
    QCache<QString, CachedEntry> m_cache;
    QSet<QString> m_inFlight;
    QHash<QString, qint64> m_recentFailures;
    QList<SequenceTask> m_sequenceQueue;
    QCache<QString, SequenceMeta> m_sequenceMetaCache;
    int m_maxCacheEntries = 256;
    int m_maxSequenceLoads = 1;
    int m_activeSequenceLoads = 0;
    int m_sequenceMetaLimit = 64;
    int m_sequenceQueueLimit = 24;
    bool m_sequenceDetectionEnabled = true; // Default to enabled for backward compatibility
    
    // Cancellation token for responsive navigation - incremented on cancelPending()
    // Decode tasks check this before/during expensive operations
    std::atomic<uint64_t> m_cancellationToken{0};
    
    // Dedicated thread pool for decode tasks - avoids contention with Qt's global pool
    // and ensures we spread work across all available CPU cores
    QThreadPool* m_decodePool = nullptr;
    
    // Throttle requests during rapid events (resize/scroll)
    QElapsedTimer m_lastRequestTime;
    std::atomic<int> m_pendingDecodes{0};
    static constexpr int kMaxConcurrentDecodes = 8; // Limit concurrent decodes
    
    // Suspend requests during resize operations (window resize, splitter drag)
    std::atomic<bool> m_requestsSuspended{false};

    // Metrics (protected by m_mutex)
    quint64 m_cacheHits = 0;
    quint64 m_cacheMisses = 0;

};
