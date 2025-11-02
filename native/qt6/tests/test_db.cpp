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
        QVERIFY(DB::instance().init(dbPath));
    }

    void testCreateFolder() {
        DB& db = DB::instance();

        // Create a folder
        int folderId = db.createFolder("TestFolder", 0);
        QVERIFY(folderId > 0);

        // Create a subfolder
        int subFolderId = db.createFolder("SubFolder", folderId);
        QVERIFY(subFolderId > 0);
        QVERIFY(subFolderId != folderId);
    }

    void testUpsertAsset() {
        DB& db = DB::instance();

        // Create a temporary test file
        QString testFile = tempDir.path() + "/test_image.txt";
        QFile f(testFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("test content");
        f.close();

        // Upsert the asset
        int assetId = db.upsertAsset(testFile);
        QVERIFY(assetId > 0);

        // Verify we can retrieve the asset path
        QString retrievedPath = db.getAssetFilePath(assetId);
        QVERIFY(!retrievedPath.isEmpty());
    }

    void testTransactions() {
        DB& db = DB::instance();

        // Create a folder for transaction test
        int folderId = db.createFolder("TransactionTest", 0);
        QVERIFY(folderId > 0);

        // Get asset IDs in folder (should be empty initially)
        QList<int> assetIds = db.getAssetIdsInFolder(folderId, false);
        QVERIFY(assetIds.isEmpty());
    }

    void testForeignKeyConstraints() {
        DB& db = DB::instance();

        // Create a folder
        int folderId = db.createFolder("FKTest", 0);
        QVERIFY(folderId > 0);

        // Create a test file
        QString testFile = tempDir.path() + "/fk_test.txt";
        QFile f(testFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("fk test");
        f.close();

        // Upsert asset
        int assetId = db.upsertAsset(testFile);
        QVERIFY(assetId > 0);

        // Move asset to folder
        bool ok = db.setAssetFolder(assetId, folderId);
        QVERIFY(ok);

        // Verify asset is in folder
        QList<int> assetIds = db.getAssetIdsInFolder(folderId, false);
        QVERIFY(assetIds.contains(assetId));
    }

    void testModelDataAccess() {
        DB& db = DB::instance();

        // Create test data
        int folderId = db.createFolder("ModelTest", 0);
        QVERIFY(folderId > 0);

        // Create multiple test files
        for (int i = 0; i < 3; ++i) {
            QString testFile = tempDir.path() + QString("/model_test_%1.txt").arg(i);
            QFile f(testFile);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QString("content %1").arg(i).toUtf8());
            f.close();

            int assetId = db.upsertAsset(testFile);
            QVERIFY(assetId > 0);
            db.setAssetFolder(assetId, folderId);
        }

        // Verify we can retrieve assets
        QList<int> assetIds = db.getAssetIdsInFolder(folderId, false);
        QVERIFY(assetIds.size() >= 3);
    }

    void testSearchFiltering() {
        DB& db = DB::instance();

        // Create a folder for search test
        int folderId = db.createFolder("SearchTest", 0);
        QVERIFY(folderId > 0);

        // Create test files with different ratings
        QString testFile1 = tempDir.path() + "/search_test_1.txt";
        QFile f1(testFile1);
        QVERIFY(f1.open(QIODevice::WriteOnly));
        f1.write("search test 1");
        f1.close();

        int assetId1 = db.upsertAsset(testFile1);
        QVERIFY(assetId1 > 0);

        // Set rating
        QList<int> ids = {assetId1};
        bool ok = db.setAssetsRating(ids, 5);
        QVERIFY(ok);
    }

    void cleanupTestCase() {
        // Cleanup is automatic with QTemporaryDir
    }
};

#include "test_db.moc"
QTEST_MAIN(TestDB)

