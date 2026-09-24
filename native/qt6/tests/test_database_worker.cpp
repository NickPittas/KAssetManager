#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <QSemaphore>
#include <QCoreApplication>
#include <atomic>

#include "../src/database_worker.h"

class TestDatabaseWorker : public QObject {
    Q_OBJECT

private slots:
    void testJobRunsOnWorkerWithOpenDb();
    void testTwoJobsSeriallySameConnection();
    void testCallbackReturnsOnCallerThread();
    void testShutdownRemovesConnectionCleanly();
};

void TestDatabaseWorker::testJobRunsOnWorkerWithOpenDb()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.path() + QStringLiteral("/test.db");

    DatabaseWorker worker;
    QSignalSpy spyOpen(&worker, &DatabaseWorker::databaseOpened);
    QVERIFY(worker.start(dbPath));
    QVERIFY(spyOpen.wait(5000));
    QCOMPARE(spyOpen.count(), 1);
    QVERIFY(spyOpen.first().first().toBool());
    QVERIFY(worker.isOpen());

    DatabaseWorkerResult result;
    QSemaphore sem;
    worker.submit([&](QSqlDatabase& db) -> DatabaseWorkerResult {
        DatabaseWorkerResult r;
        r.success = db.isOpen() && db.isValid();
        r.value = db.connectionName();
        QSqlQuery q(db);
        r.success = r.success && q.exec(QStringLiteral("CREATE TABLE foo (id INTEGER PRIMARY KEY)"));
        return r;
    }, [&](const DatabaseWorkerResult& r) {
        result = r;
        sem.release();
    }, this);

    QVERIFY(sem.tryAcquire(1, 5000));
    QVERIFY(result.success);
    QVERIFY(!result.value.toString().isEmpty());
    QVERIFY(result.value.toString().startsWith(QStringLiteral("db_worker_")));

    QSignalSpy spyStopped(&worker, &DatabaseWorker::stopped);
    worker.shutdown();
    QVERIFY(spyStopped.wait(5000));
}

void TestDatabaseWorker::testTwoJobsSeriallySameConnection()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.path() + QStringLiteral("/test.db");

    DatabaseWorker worker;
    QSignalSpy spyOpen(&worker, &DatabaseWorker::databaseOpened);
    QVERIFY(worker.start(dbPath));
    QVERIFY(spyOpen.wait(5000));
    QVERIFY(spyOpen.first().first().toBool());

    std::atomic<int> counter{0};
    DatabaseWorkerResult result1;
    DatabaseWorkerResult result2;
    QSemaphore sem;

    worker.submit([&](QSqlDatabase& db) -> DatabaseWorkerResult {
        const int v = counter.fetch_add(1);
        QThread::msleep(50);
        return {true, QVariant::fromValue(v), db.connectionName()};
    }, [&](const DatabaseWorkerResult& r) {
        result1 = r;
        sem.release();
    }, this);

    worker.submit([&](QSqlDatabase& db) -> DatabaseWorkerResult {
        const int v = counter.fetch_add(1);
        return {true, QVariant::fromValue(v), db.connectionName()};
    }, [&](const DatabaseWorkerResult& r) {
        result2 = r;
        sem.release();
    }, this);

    QVERIFY(sem.tryAcquire(2, 5000));
    QCOMPARE(result1.value.toInt(), 0);
    QCOMPARE(result2.value.toInt(), 1);
    QVERIFY(!result1.error.isEmpty());
    QCOMPARE(result1.error, result2.error);

    QSignalSpy spyStopped(&worker, &DatabaseWorker::stopped);
    worker.shutdown();
    QVERIFY(spyStopped.wait(5000));
}

void TestDatabaseWorker::testCallbackReturnsOnCallerThread()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.path() + QStringLiteral("/test.db");

    DatabaseWorker worker;
    QSignalSpy spyOpen(&worker, &DatabaseWorker::databaseOpened);
    QVERIFY(worker.start(dbPath));
    QVERIFY(spyOpen.wait(5000));
    QVERIFY(spyOpen.first().first().toBool());

    QThread* const callerThread = QThread::currentThread();
    QThread* callbackThread = nullptr;
    QSemaphore sem;

    worker.submit([](QSqlDatabase& db) -> DatabaseWorkerResult {
        Q_UNUSED(db)
        return {true, QVariant(), QString()};
    }, [&](const DatabaseWorkerResult& r) {
        Q_UNUSED(r)
        callbackThread = QThread::currentThread();
        sem.release();
    }, this);

    QVERIFY(sem.tryAcquire(1, 5000));
    QCOMPARE(callbackThread, callerThread);

    QSignalSpy spyStopped(&worker, &DatabaseWorker::stopped);
    worker.shutdown();
    QVERIFY(spyStopped.wait(5000));
}

void TestDatabaseWorker::testShutdownRemovesConnectionCleanly()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString dbPath = tempDir.path() + QStringLiteral("/test.db");

    DatabaseWorker worker;
    QSignalSpy spyOpen(&worker, &DatabaseWorker::databaseOpened);
    QVERIFY(worker.start(dbPath));
    QVERIFY(spyOpen.wait(5000));
    QVERIFY(spyOpen.first().first().toBool());

    QString connectionName;
    QSemaphore sem;
    worker.submit([&](QSqlDatabase& db) -> DatabaseWorkerResult {
        connectionName = db.connectionName();
        return {true, QVariant(), QString()};
    }, [&](const DatabaseWorkerResult& r) {
        Q_UNUSED(r)
        sem.release();
    }, this);

    QVERIFY(sem.tryAcquire(1, 5000));
    QVERIFY(QSqlDatabase::contains(connectionName));

    QSignalSpy spyStopped(&worker, &DatabaseWorker::stopped);
    worker.shutdown();
    QVERIFY(spyStopped.wait(5000));
    QVERIFY(!worker.isRunning());
    QVERIFY(!QSqlDatabase::contains(connectionName));
}

#include "test_database_worker.moc"
QTEST_MAIN(TestDatabaseWorker)
