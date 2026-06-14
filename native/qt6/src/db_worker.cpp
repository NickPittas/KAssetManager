#include "db_worker.h"

#include <exception>
#include <QMetaObject>
#include <QPointer>
#include <QSqlError>
#include <QSqlQuery>

class DbWorkerExecutor final : public QObject {
public:
    bool open(const QString& connectionName, const QString& dbFilePath) {
        if (m_db.isOpen())
            return false;

        m_connectionName = connectionName;
        m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
        m_db.setDatabaseName(dbFilePath);
        if (!m_db.open()) {
            m_lastError = m_db.lastError().text();
            m_db = QSqlDatabase();
            QSqlDatabase::removeDatabase(m_connectionName);
            m_connectionName.clear();
            return false;
        }

        QSqlQuery pragma(m_db);
        pragma.exec("PRAGMA foreign_keys = ON");
        return true;
    }

    QString lastError() const { return m_lastError; }

    void runJob(const QPointer<DbWorker>& owner, int jobId, std::shared_ptr<IDbJob> job) {
        if (!m_db.isOpen()) {
            finishFailed(owner, jobId, QStringLiteral("Database worker connection is not open"));
            return;
        }
        if (!job) {
            finishFailed(owner, jobId, QStringLiteral("Database worker received an empty job"));
            return;
        }

        try {
            const QVariant result = job->run(m_db);
            if (owner) {
                QMetaObject::invokeMethod(owner, [owner, jobId, result] {
                    if (owner)
                        emit owner->jobFinished(jobId, result);
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& e) {
            finishFailed(owner, jobId, QString::fromUtf8(e.what()));
        } catch (...) {
            finishFailed(owner, jobId, QStringLiteral("Database worker job failed with an unknown exception"));
        }
    }

    void close() {
        const QString name = m_connectionName;
        if (name.isEmpty())
            return;

        if (m_db.isOpen())
            m_db.close();
        m_db = QSqlDatabase();
        m_connectionName.clear();
        QSqlDatabase::removeDatabase(name);
    }

private:
    static void finishFailed(const QPointer<DbWorker>& owner, int jobId, const QString& error) {
        if (owner) {
            QMetaObject::invokeMethod(owner, [owner, jobId, error] {
                if (owner)
                    emit owner->jobFailed(jobId, error);
            }, Qt::QueuedConnection);
        }
    }

    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_lastError;
};

DbWorker::DbWorker(QObject* parent)
    : QObject(parent)
    , m_executor(new DbWorkerExecutor)
{
}

DbWorker::~DbWorker() {
    stop();
    delete m_executor;
    m_executor = nullptr;
}

bool DbWorker::start(const QString& connectionName, const QString& dbFilePath) {
    if (m_running)
        return false;
    if (connectionName.isEmpty() || dbFilePath.isEmpty())
        return false;

    m_connectionName = connectionName;
    m_executor->moveToThread(&m_thread);
    m_thread.setObjectName(connectionName + QStringLiteral("_thread"));
    m_thread.start();

    bool opened = false;
    QMetaObject::invokeMethod(m_executor, [&] {
        opened = static_cast<DbWorkerExecutor*>(m_executor)->open(connectionName, dbFilePath);
    }, Qt::BlockingQueuedConnection);

    if (!opened) {
        QMetaObject::invokeMethod(m_executor, [executor = m_executor, targetThread = thread()] {
            executor->moveToThread(targetThread);
        }, Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
        m_connectionName.clear();
        return false;
    }

    m_running = true;
    return true;
}

void DbWorker::stop() {
    if (!m_thread.isRunning()) {
        m_running = false;
        m_connectionName.clear();
        return;
    }

    QMetaObject::invokeMethod(m_executor, [executor = static_cast<DbWorkerExecutor*>(m_executor), targetThread = thread()] {
        executor->close();
        executor->moveToThread(targetThread);
    }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    m_running = false;
    m_connectionName.clear();
}

int DbWorker::submit(std::shared_ptr<IDbJob> job) {
    if (!m_running || !job)
        return 0;

    const int jobId = m_nextJobId++;
    const QPointer<DbWorker> owner(this);
    QMetaObject::invokeMethod(m_executor, [executor = static_cast<DbWorkerExecutor*>(m_executor), owner, jobId, job = std::move(job)] {
        executor->runJob(owner, jobId, job);
    }, Qt::QueuedConnection);
    return jobId;
}
