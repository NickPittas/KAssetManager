#pragma once
#include <QObject>
#include <QStringList>
#include <QList>
#include <QFutureWatcher>
#include <QMutex>
#include <atomic>

class FileOpsQueue : public QObject {
    Q_OBJECT
public:
    enum class Type { Copy, Move, Delete };
    struct Item {
        int id = 0;
        Type type = Type::Copy;
        QStringList sources;
        QString destination; // For Copy/Move
        QString status; // Queued, In Progress, Completed, Cancelled, Failed
        int completedFiles = 0;
        int totalFiles = 0;
        QString currentFile;
        QString error;
        bool permanentDelete = false; // For Delete operations: true = permanent, false = Recycle Bin
    };

    static FileOpsQueue& instance();

    // Enqueue operations (thread-safe)
    enum class ConflictAction { Rename, Overwrite, Skip };

    void enqueueCopy(const QStringList& sources, const QString& destination);
    void enqueueMove(const QStringList& sources, const QString& destination);
    void enqueueDeletePermanent(const QStringList& sources);

    void enqueueDelete(const QStringList& sources);

    QList<Item> items() const; // snapshot copy
    bool isBusy() const;

signals:
    void queueChanged();
    void progressChanged(int current, int total, const QString& currentFile);
    void currentItemChanged(const FileOpsQueue::Item& item);
    void itemFinished(int id, bool success, const QString& error);

public slots:
    void cancelCurrent();
    void cancelAll();

private:
    explicit FileOpsQueue(QObject* parent = nullptr);
    Q_DISABLE_COPY(FileOpsQueue)

    void startNext();

    // Worker helpers (run in background thread)
    static bool copyFileWithProgress(const QString& src, const QString& dst, std::atomic_bool& cancel,
                                     std::function<void(qint64,qint64)> onProgress, QString* errorOut);
    static bool copyRecursively(const QString& src, const QString& dstDir, std::atomic_bool& cancel,
                                std::function<void(const QString&, int, int)> onFile, QString* errorOut);
    static bool removeRecursively(const QString& path, std::atomic_bool& cancel);
    static QString uniqueNameInDir(const QString& dir, const QString& baseName);

    mutable QMutex m_mutex;
    QList<Item> m_queue;
    int m_nextId = 1;
    bool m_running = false;
    std::atomic_bool m_cancel{false};
    QFuture<void> m_future;
    QFutureWatcher<void> m_watcher;
};

