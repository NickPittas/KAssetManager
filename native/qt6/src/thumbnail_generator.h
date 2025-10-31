#pragma once
#include <QObject>
#include <QString>
#include <QImage>
#include <QDir>
#include <QThreadPool>
#include <QRunnable>
#include <QMutex>
#include <QSet>
#include <QQueue>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QTimer>
#include <atomic>

// Forward declaration
class ThumbnailGenerator;

// Runnable task for generating IMAGE thumbnails in background (images only, not videos)
class ThumbnailTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    ThumbnailTask(const QString& filePath, ThumbnailGenerator* generator, int sessionId);
    void run() override;

signals:
    void finished(const QString& filePath, const QString& thumbnailPath, bool success);

private:
    QString m_filePath;
    ThumbnailGenerator* m_generator;
    int m_sessionId = 0;
};

// Fallback task: decode a still frame using FFmpeg in background
class VideoFFmpegTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    VideoFFmpegTask(const QString& filePath, const QString& cachePath, ThumbnailGenerator* generator);
    void run() override;
private:
    bool decodeAndSave();
    QString m_filePath;
    QString m_cachePath;
    ThumbnailGenerator* m_generator;
};

// Async video thumbnail generator (primary path via QMediaPlayer; falls back to FFmpeg task when needed)
class VideoThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    VideoThumbnailGenerator(const QString& filePath, const QString& cachePath, ThumbnailGenerator* generator, int sessionId);
    ~VideoThumbnailGenerator();
    void start();

private slots:
    void onMediaStatusChanged();
    void onVideoFrameChanged();
    void onTimeout();
    void onError(QMediaPlayer::Error error, const QString &errorString);

private:
    void startFfmpegFallback();
    QString m_filePath;
    QString m_cachePath;
    ThumbnailGenerator* m_generator;
    QMediaPlayer* m_player;
    QVideoSink* m_videoSink;
    QTimer* m_timeout;
    bool m_frameReceived;
    QImage m_capturedFrame;
    int m_sessionId = 0;
};

class ThumbnailGenerator : public QObject {
    Q_OBJECT
    friend class ThumbnailTask;
    friend class VideoThumbnailGenerator;
    friend class VideoFFmpegTask;

public:
    static ThumbnailGenerator& instance();

    Q_INVOKABLE QString getThumbnailPath(const QString& filePath);
    Q_INVOKABLE void requestThumbnail(const QString& filePath);
    Q_INVOKABLE bool isImageFile(const QString& filePath);
    Q_INVOKABLE bool isVideoFile(const QString& filePath);
    Q_INVOKABLE bool isQtSupportedFormat(const QString& filePath);
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE void requestThumbnailForce(const QString& filePath);

    Q_INVOKABLE QString createSampleImage(const QString& directory = QString());

    // Session control: cancel previous generation and only allow current-folder work
    void beginNewSession();
    int currentSessionId() const { return m_sessionId.load(); }

    void startProgress(int total);
    void updateProgress();
    void finishProgress();

signals:
    void thumbnailGenerated(const QString& filePath, const QString& thumbnailPath);
    void thumbnailFailed(const QString& filePath);
    void progressChanged(int current, int total);

private:
    explicit ThumbnailGenerator(QObject* parent = nullptr);

    QString generateImageThumbnail(const QString& filePath);
    QString getThumbnailCachePath(const QString& filePath);
    QString writeThumbnailImage(const QString& sourcePath, const QImage& image);
    QString getFileHash(const QString& filePath);
    void ensureThumbnailDir();
    bool isThumbnailCached(const QString& filePath);
    QString createUnsupportedThumbnail(const QString& filePath);
    void updateThreadPoolLimit();

    // Video concurrency control
    void startNextVideoIfPossible();

    QDir m_thumbnailDir;
    QThreadPool* m_threadPool;
    QMutex m_mutex;
    QSet<QString> m_pendingThumbnails;
    QSet<class VideoThumbnailGenerator*> m_activeVideoGenerators;
    QQueue<QPair<QString, QString>> m_videoQueue; // (filePath, cachePath)
    int m_maxActiveVideos = 2;

    int m_totalThumbnails;
    int m_completedThumbnails;
    int m_baseThreadCount = 2;
    int m_currentThreadLimit = 2;
    int m_pendingImageTasks = 0;
    int m_lastReportedProgress = 0;

    std::atomic<int> m_sessionId{0};

    static constexpr int THUMBNAIL_WIDTH = 256;
    static constexpr int THUMBNAIL_HEIGHT = 256;
};

