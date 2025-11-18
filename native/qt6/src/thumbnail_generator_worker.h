#pragma once

#include <QObject>
#include <QStringList>
#include <QSize>
#include <QVector>
#include <QMutex>
#include <QAtomicInt>
#include <QThreadPool>

/**
 * @brief Worker thread for generating persistent thumbnails
 *
 * Processes a list of files and generates thumbnails for each:
 * - Images: Single thumbnail at position 0.0
 * - Videos: Multiple thumbnails at positions 0.0, 0.25, 0.5, 0.75, 1.0 for scrubbing
 * - Image sequences: Multiple thumbnails at various positions
 *
 * Uses QThreadPool for parallel processing of multiple files
 */
class ThumbnailGeneratorWorker : public QObject
{
    Q_OBJECT

public:
    struct Task {
        QString filePath;
        bool isVideo = false;
        bool isSequence = false;
        QStringList sequenceFrames; // For sequences
    };

    explicit ThumbnailGeneratorWorker(QObject* parent = nullptr);
    ~ThumbnailGeneratorWorker();

signals:
    void queueStarted(int totalFiles);
    void fileStarted(int index, const QString& filePath);
    void fileProgress(int index, int percent);
    void fileFinished(int index, bool success, const QString& errorMsg);
    void queueFinished(bool allSuccess);
    void logLine(const QString& line);

public slots:
    void start(const QVector<Task>& tasks, const QSize& thumbnailSize);
    void cancelAll();

private:
    void processFile(int index, const Task& task);
    void checkIfFinished();

    // Generate thumbnails for different file types
    bool generateImageThumbnail(int index, const QString& filePath, const QSize& size);
    bool generateVideoThumbnails(int index, const QString& filePath, const QSize& size);
    bool generateSequenceThumbnails(int index, const Task& task, const QSize& size);

    QVector<Task> m_tasks;
    QSize m_thumbnailSize;
    QAtomicInt m_cancelling;
    QAtomicInt m_successCount;
    QAtomicInt m_failCount;
    QAtomicInt m_completedCount;
    QThreadPool* m_threadPool;
    QMutex m_mutex;
};

