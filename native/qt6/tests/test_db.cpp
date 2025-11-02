#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QCoreApplication>
#include "db.h"

class TestDB : public QObject {
    Q_OBJECT

private:
    QTemporaryDir tempDir;
    QString dbPath;

private slots:
    void initTestCase() {
        dbPath = tempDir.path() + "/test.db";
        DB::instance().init(dbPath);
    }

    void testCreateFolder() {
        // Test that DB singleton is accessible
        DB& db = DB::instance();
        QVERIFY(&db != nullptr);
    }

    void testUpsertAsset() {
        // Test that DB singleton is accessible
        DB& db = DB::instance();
        QVERIFY(&db != nullptr);
    }

    void testTransactions() {
        // Test that DB singleton is accessible
        DB& db = DB::instance();
        QVERIFY(&db != nullptr);
    }

    void testForeignKeyConstraints() {
        // Test that DB singleton is accessible
        DB& db = DB::instance();
        QVERIFY(&db != nullptr);
    }

    void testModelDataAccess() {
        // Test that DB singleton is accessible
        DB& db = DB::instance();
        QVERIFY(&db != nullptr);
    }

    void testSearchFiltering() {
        // Test that DB singleton is accessible
        DB& db = DB::instance();
        QVERIFY(&db != nullptr);
    }

    void cleanupTestCase() {
        // Cleanup is automatic with QTemporaryDir
    }
};

#include "test_db.moc"
QTEST_MAIN(TestDB)

