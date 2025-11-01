#pragma once

#include <QObject>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QElapsedTimer>
#include <QList>
#include <QStringList>

/**
 * LivePreviewManager streams preview frames for stills, video clips, and image sequences
 * without persisting thumbnails to disk. It exposes a lightweight request API that returns
 * cached pixmaps synchronously when available and emits signals when asynchronous decoding
 * completes.
 *
 * The manager is intentionally agnostic of any particular view; callers provide the
 * requested normalized position (0-1) and target size. Internally the manager performs
 * smart caching and throttles expensive requests so scrubbing stays responsive.
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
    void requestFrame(const QString& filePath, const QSize& targetSize, qreal position = 0.0);

    // Remove cached entries for a specific asset (all sizes/positions).
    void invalidate(const QString& filePath);
    void clear();
    int cacheEntryCount() const;

signals:
    void frameReady(const QString& filePath, qreal position, QSize targetSize, const QPixmap& pixmap);
    void frameFailed(const QString& filePath, QString errorString);

private:
    explicit LivePreviewManager(QObject* parent = nullptr);

    QString makeCacheKey(const QString& filePath, const QSize& targetSize, qreal position) const;
    void enqueueDecode(const Request& request, const QString& cacheKey);
    void enqueueSequenceDecode(const Request& request, const QString& cacheKey);
    void startDecodeTask(const Request& request, const QString& cacheKey, bool fromSequenceQueue);
    static QImage loadImageFrame(const Request& request, QString& error);
    static QImage loadVideoFrame(const Request& request, QString& error);
    static QImage loadSequenceFrame(const Request& request, QString& error);
    static bool isImageSequence(const QString& filePath);
    static QString sequenceHead(const QString& filePath);
    struct SequenceMeta;
    SequenceMeta sequenceMetaFor(const QString& filePath, QString& error);
    void pruneSequenceMetaCache();

    void storeFrame(const QString& key, const QPixmap& pixmap, qreal position, const QSize& size);

    struct CachedEntry {
        QPixmap pixmap;
        qreal position = 0.0;
        QSize size;
        QElapsedTimer lastAccess;
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
        QStringList frames;
        QElapsedTimer lastScan;
        bool isValid() const { return !frames.isEmpty(); }
    };

    mutable QMutex m_mutex;
    QHash<QString, CachedEntry> m_cache;
    QSet<QString> m_inFlight;
    QList<SequenceTask> m_sequenceQueue;
    QHash<QString, SequenceMeta> m_sequenceMetaCache;
    int m_maxCacheEntries = 256;
    int m_maxSequenceLoads = 1;
    int m_activeSequenceLoads = 0;
    int m_sequenceMetaLimit = 64;
    int m_sequenceQueueLimit = 24;
};
