#include <QtTest>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>

#include <functional>
#include <stdexcept>

#include "db_worker.h"

class LambdaDbJob final : public IDbJob {
public:
    explicit LambdaDbJob(std::function<QVariant(QSqlDatabase&)> fn)
        : m_fn(std::move(fn)) {}

    QVariant run(QSqlDatabase& db) override { return m_fn(db); }

private:
    std::function<QVariant(QSqlDatabase&)> m_fn;
};

class TestDbWorker : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void simpleReadWriteJob() {
        const QString connectionName = QStringLiteral("test_db_worker_simple");
        DbWorker worker;
        QVERIFY(worker.start(connectionName, dbPath("simple.sqlite")));

        QSignalSpy finishedSpy(&worker, &DbWorker::jobFinished);
        const int jobId = worker.submit(std::make_shared<LambdaDbJob>([](QSqlDatabase& db) {
            QSqlQuery q(db);
            if (!q.exec("CREATE TABLE items(id INTEGER PRIMARY KEY, name TEXT)"))
                return QVariant(QStringLiteral("create failed: ") + q.lastError().text());
            if (!q.exec("INSERT INTO items(name) VALUES('one')"))
                return QVariant(QStringLiteral("insert failed: ") + q.lastError().text());
            if (!q.exec("SELECT COUNT(*) FROM items") || !q.next())
                return QVariant(QStringLiteral("count failed: ") + q.lastError().text());
            return QVariant(q.value(0).toInt());
        }));
        QVERIFY(jobId > 0);
        QVERIFY(finishedSpy.wait(1000));
        const QList<QVariant> result = finishedSpy.takeFirst();
        QCOMPARE(result.at(0).toInt(), jobId);
        QCOMPARE(result.at(1).toInt(), 1);

        worker.stop();
        QVERIFY(!QSqlDatabase::contains(connectionName));
    }

    void serialJobsShareConnection() {
        const QString connectionName = QStringLiteral("test_db_worker_serial");
        DbWorker worker;
        QVERIFY(worker.start(connectionName, dbPath("serial.sqlite")));

        QSignalSpy finishedSpy(&worker, &DbWorker::jobFinished);
        QVERIFY(worker.submit(std::make_shared<LambdaDbJob>([](QSqlDatabase& db) {
            QSqlQuery q(db);
            return q.exec("CREATE TABLE serial(value INTEGER)");
        })) > 0);
        QVERIFY(worker.submit(std::make_shared<LambdaDbJob>([](QSqlDatabase& db) {
            QSqlQuery q(db);
            return q.exec("INSERT INTO serial(value) VALUES(1)");
        })) > 0);
        QVERIFY(worker.submit(std::make_shared<LambdaDbJob>([](QSqlDatabase& db) {
            QSqlQuery q(db);
            return q.exec("INSERT INTO serial(value) VALUES(2)");
        })) > 0);
        const int finalJobId = worker.submit(std::make_shared<LambdaDbJob>([](QSqlDatabase& db) {
            QSqlQuery q(db);
            if (!q.exec("SELECT COUNT(*), MAX(value) FROM serial") || !q.next())
                return QVariantList{0, 0};
            return QVariantList{q.value(0).toInt(), q.value(1).toInt()};
        }));
        QVERIFY(finalJobId > 0);

        while (finishedSpy.count() < 4)
            QVERIFY(finishedSpy.wait(1000));
        const QList<QVariant> finalResult = finishedSpy.takeLast();
        QCOMPARE(finalResult.at(0).toInt(), finalJobId);
        const QVariantList values = finalResult.at(1).toList();
        QCOMPARE(values.at(0).toInt(), 2);
        QCOMPARE(values.at(1).toInt(), 2);

        worker.stop();
        QVERIFY(!QSqlDatabase::contains(connectionName));
    }

    void callbackRunsOnCallerThread() {
        const QString connectionName = QStringLiteral("test_db_worker_callback");
        DbWorker worker;
        QVERIFY(worker.start(connectionName, dbPath("callback.sqlite")));

        QThread* callbackThread = nullptr;
        connect(&worker, &DbWorker::jobFinished, this, [&callbackThread] {
            callbackThread = QThread::currentThread();
        });

        QSignalSpy finishedSpy(&worker, &DbWorker::jobFinished);
        QVERIFY(worker.submit(std::make_shared<LambdaDbJob>([](QSqlDatabase& db) {
            return db.isOpen();
        })) > 0);
        QVERIFY(finishedSpy.wait(1000));
        QCOMPARE(callbackThread, QThread::currentThread());

        worker.stop();
        QVERIFY(!QSqlDatabase::contains(connectionName));
    }

    void failedJobEmitsFailure() {
        const QString connectionName = QStringLiteral("test_db_worker_failure");
        DbWorker worker;
        QVERIFY(worker.start(connectionName, dbPath("failure.sqlite")));

        QSignalSpy failedSpy(&worker, &DbWorker::jobFailed);
        const int jobId = worker.submit(std::make_shared<LambdaDbJob>([](QSqlDatabase& db) -> QVariant {
            QSqlQuery q(db);
            if (!q.exec("SELECT * FROM missing_table"))
                throw std::runtime_error(q.lastError().text().toStdString());
            return true;
        }));
        QVERIFY(jobId > 0);
        QVERIFY(failedSpy.wait(1000));
        const QList<QVariant> result = failedSpy.takeFirst();
        QCOMPARE(result.at(0).toInt(), jobId);
        QVERIFY(!result.at(1).toString().isEmpty());

        worker.stop();
        QVERIFY(!QSqlDatabase::contains(connectionName));
    }

private:
    QString dbPath(const QString& name) const {
        return m_dir.path() + QLatin1Char('/') + name;
    }

    QTemporaryDir m_dir;
};

#include "test_db_worker.moc"
QTEST_MAIN(TestDbWorker)
