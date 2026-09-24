#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QThread>
#include <QVariant>

#include <memory>

class IDbJob {
public:
    virtual ~IDbJob() = default;
    virtual QVariant run(QSqlDatabase& db) = 0;
};

class DbWorker : public QObject {
    Q_OBJECT
public:
    explicit DbWorker(QObject* parent = nullptr);
    ~DbWorker() override;

    bool start(const QString& connectionName, const QString& dbFilePath);
    void stop();
    bool isRunning() const { return m_running; }

    int submit(std::shared_ptr<IDbJob> job);

signals:
    void jobFinished(int jobId, const QVariant& result);
    void jobFailed(int jobId, const QString& error);

private:
    Q_DISABLE_COPY_MOVE(DbWorker)

    QThread m_thread;
    QObject* m_executor = nullptr;
    QString m_connectionName;
    bool m_running = false;
    int m_nextJobId = 1;
};
