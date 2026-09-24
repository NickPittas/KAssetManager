#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QVariant>
#include <QString>
#include <functional>

/**
 * @brief Result delivered to a DatabaseWorker completion callback.
 */
struct DatabaseWorkerResult {
    bool success = false;
    QVariant value;
    QString error;
};

class DatabaseWorkerPrivate;

/**
 * @brief QObject-owned worker thread that owns a named SQLite connection.
 *
 * The connection is opened on the worker thread and must only be used on that
 * thread. Jobs are submitted as callables receiving QSqlDatabase& and are
 * executed serially via the worker thread's event loop. Completion callbacks
 * can be delivered back to any QObject's thread via queued invocation.
 */
class DatabaseWorker : public QObject {
    Q_OBJECT
public:
    using Job = std::function<DatabaseWorkerResult(QSqlDatabase&)>;
    using ResultCallback = std::function<void(const DatabaseWorkerResult&)>;

    explicit DatabaseWorker(QObject* parent = nullptr);
    ~DatabaseWorker();

    /**
     * @brief Start the worker thread and open the SQLite database on it.
     * @return true if the thread was started (database open result is async)
     */
    bool start(const QString& dbFilePath);

    /**
     * @brief Gracefully shut down the worker, close the DB connection, and stop the thread.
     */
    void shutdown();

    bool isRunning() const;
    bool isOpen() const;

    /**
     * @brief Submit a job to run serially on the worker thread.
     *
     * If @p callbackContext is provided, @p callback is invoked on the thread
     * that owns @p callbackContext; otherwise it runs on the worker thread.
     */
    void submit(const Job& job, const ResultCallback& callback = nullptr, QObject* callbackContext = nullptr);

signals:
    void databaseOpened(bool success, const QString& error);
    void stopped();

private:
    QThread* m_thread = nullptr;
    DatabaseWorkerPrivate* m_worker = nullptr;
};
