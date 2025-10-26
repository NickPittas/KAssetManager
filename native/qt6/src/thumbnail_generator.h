#pragma once
#include <QObject>
#include <QString>
#include <QImage>
#include <QDir>
#include <QThreadPool>
#include <QRunnable>
#include <QMutex>
#include <QSet>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QTimer>

// Forward declaration
class ThumbnailGenerator;

// Runnable task for generating IMAGE thumbnails in background (images only, not videos)
class ThumbnailTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    ThumbnailTask(const QString& filePath, ThumbnailGenerator* generator);
    void run() override;

signals:
    void finished(const QString& filePath, const QString& thumbnailPath, bool success);

private:
    QString m_filePath;
    ThumbnailGenerator* m_generator;
};

// Async video thumbnail generator (runs on main thread without blocking)
class VideoThumbnailGenerator : public QObject {
    Q_OBJECT
public:
    VideoThumbnailGenerator(const QString& filePath, const QString& cachePath, ThumbnailGenerator* generator);
    ~VideoThumbnailGenerator();
    void start();

private slots:
    void onMediaStatusChanged();
    void onVideoFrameChanged();
    void onTimeout();
    void onError(QMediaPlayer::Error error, const QString &errorString);

private:
    QString m_filePath;
    QString m_cachePath;
    ThumbnailGenerator* m_generator;
    QMediaPlayer* m_player;
    QVideoSink* m_videoSink;
    QTimer* m_timeout;
    bool m_frameReceived;
    QImage m_capturedFrame;
};

class ThumbnailGenerator : public QObject {
    Q_OBJECT
    friend class ThumbnailTask;
    friend class VideoThumbnailGenerator;

public:
    static ThumbnailGenerator& instance();

    Q_INVOKABLE QString getThumbnailPath(const QString& filePath);
    Q_INVOKABLE void requestThumbnail(const QString& filePath);
    Q_INVOKABLE bool isImageFile(const QString& filePath);
    Q_INVOKABLE bool isVideoFile(const QString& filePath);
    Q_INVOKABLE bool isQtSupportedFormat(const QString& filePath);
    Q_INVOKABLE void clearCache();
    Q_INVOKABLE QString createSampleImage(const QString& directory = QString());

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
    void startVideoThumbnailGeneration(const QString& filePath);
    QString getThumbnailCachePath(const QString& filePath);
    QString getFileHash(const QString& filePath);
    void ensureThumbnailDir();
    bool isThumbnailCached(const QString& filePath);
    QString createUnsupportedThumbnail(const QString& filePath);

    QDir m_thumbnailDir;
    QThreadPool* m_threadPool;
    QMutex m_mutex;
    QSet<QString> m_pendingThumbnails;

    int m_totalThumbnails;
    int m_completedThumbnails;

    static constexpr int THUMBNAIL_WIDTH = 256;
    static constexpr int THUMBNAIL_HEIGHT = 256;
};
