#include "database_worker.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QDebug>
#include <QMetaObject>
#include <QThread>

class DatabaseWorkerPrivate : public QObject {
    Q_OBJECT
public:
    explicit DatabaseWorkerPrivate(QObject* parent = nullptr)
        : QObject(parent)
    {
        m_connectionName = QStringLiteral("db_worker_") +
                           QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    ~DatabaseWorkerPrivate() {
        close();
    }

    bool open(const QString& dbFilePath) {
        if (m_db.isValid() && m_db.isOpen()) {
            m_open = true;
            emit openFinished(true, QString());
            return true;
        }

        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        m_db.setDatabaseName(dbFilePath);

        if (!m_db.open()) {
            const QString err = m_db.lastError().text();
            qWarning() << "[DatabaseWorker] Failed to open database:" << err;
            const QString connName = m_db.connectionName();
            m_db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connName);
            emit openFinished(false, err);
            return false;
        }

        QSqlQuery q(m_db);
        q.exec(QStringLiteral("PRAGMA foreign_keys=ON;"));

        m_open = true;
        emit openFinished(true, QString());
        return true;
    }

    void close() {
        if (m_db.isValid()) {
            const QString connName = m_db.connectionName();
            m_db.close();
            m_db = QSqlDatabase();
            QSqlDatabase::removeDatabase(connName);
        }
        m_open = false;
    }

    bool isOpen() const { return m_open; }

    void submit(DatabaseWorker::Job job, DatabaseWorker::ResultCallback callback, QObject* callbackContext) {
        DatabaseWorkerResult result;
        if (m_shuttingDown || !m_open) {
            result.success = false;
            result.error = m_shuttingDown
                               ? QStringLiteral("Worker is shutting down")
                               : QStringLiteral("Database not open");
            deliverResult(result, callback, callbackContext);
            return;
        }

        try {
            result = job(m_db);
        } catch (const std::exception& e) {
            result.success = false;
            result.error = QString::fromUtf8(e.what());
        } catch (...) {
            result.success = false;
            result.error = QStringLiteral("Unknown exception");
        }
        deliverResult(result, callback, callbackContext);
    }

    void shutdown() {
        m_shuttingDown = true;
        close();
        emit finished();
    }

signals:
    void openFinished(bool success, const QString& error);
    void finished();

private:
    static void deliverResult(const DatabaseWorkerResult& result,
                              DatabaseWorker::ResultCallback callback,
                              QObject* callbackContext) {
        if (!callback)
            return;

        QObject* target = callbackContext ? callbackContext : QThread::currentThread();
        if (!target)
            return;

        QMetaObject::invokeMethod(target, [callback, result]() {
            callback(result);
        }, Qt::QueuedConnection);
    }

    QSqlDatabase m_db;
    QString m_connectionName;
    bool m_open = false;
    bool m_shuttingDown = false;
};

DatabaseWorker::DatabaseWorker(QObject* parent)
    : QObject(parent)
{
}

DatabaseWorker::~DatabaseWorker() {
    shutdown();
}

bool DatabaseWorker::start(const QString& dbFilePath) {
    if (m_thread) {
        qWarning() << "[DatabaseWorker] Already started";
        return false;
    }

    m_thread = new QThread(this);
    m_worker = new DatabaseWorkerPrivate();
    m_worker->moveToThread(m_thread);

    connect(m_worker, &DatabaseWorkerPrivate::openFinished,
            this, &DatabaseWorker::databaseOpened, Qt::QueuedConnection);

    connect(m_worker, &DatabaseWorkerPrivate::finished, this, [this]() {
        if (m_thread)
            m_thread->quit();
    }, Qt::QueuedConnection);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, this, [this]() {
        if (m_thread) {
            m_thread->deleteLater();
            m_thread = nullptr;
            m_worker = nullptr;
        }
        emit stopped();
    }, Qt::QueuedConnection);

    m_thread->start();

    QMetaObject::invokeMethod(m_worker, [this, dbFilePath]() {
        m_worker->open(dbFilePath);
    }, Qt::QueuedConnection);

    return true;
}

void DatabaseWorker::shutdown() {
    if (!m_thread)
        return;

    if (QThread::currentThread() == m_thread) {
        m_worker->shutdown();
        return;
    }

    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, &DatabaseWorkerPrivate::shutdown, Qt::QueuedConnection);
    }

    if (m_thread->isRunning()) {
        m_thread->quit();
        if (!m_thread->wait(5000)) {
            qWarning() << "[DatabaseWorker] Thread did not stop gracefully";
        }
    }
}

bool DatabaseWorker::isRunning() const {
    return m_thread && m_thread->isRunning();
}

bool DatabaseWorker::isOpen() const {
    return m_worker && m_worker->isOpen();
}

void DatabaseWorker::submit(const Job& job, const ResultCallback& callback, QObject* callbackContext) {
    if (!m_worker) {
        if (callback) {
            DatabaseWorkerResult result;
            result.success = false;
            result.error = QStringLiteral("Worker not started");
            callback(result);
        }
        return;
    }

    QMetaObject::invokeMethod(m_worker, [this, job, callback, callbackContext]() {
        m_worker->submit(job, callback, callbackContext);
    }, Qt::QueuedConnection);
}

#include "database_worker.moc"
