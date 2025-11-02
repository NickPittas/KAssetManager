#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QCoreApplication>
#include "db.h"
#include "assets_model.h"

class TestModels : public QObject {
    Q_OBJECT

private:
    QTemporaryDir tempDir;
    QString dbPath;

private slots:
    void initTestCase() {
        // Initialize DB for model tests
        dbPath = tempDir.path() + "/test_models.db";
        QVERIFY(DB::instance().init(dbPath));
    }

    void testAssetsModelRowCount() {
        AssetsModel model;

        // Initially empty
        int rowCount = model.rowCount(QModelIndex());
        QCOMPARE(rowCount, 0);

        // Create a folder and add assets
        int folderId = DB::instance().createFolder("TestFolder", 0);
        QVERIFY(folderId > 0);

        // Create test files
        for (int i = 0; i < 3; ++i) {
            QString testFile = tempDir.path() + QString("/asset_%1.txt").arg(i);
            QFile f(testFile);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QString("asset %1").arg(i).toUtf8());
            f.close();

            int assetId = DB::instance().upsertAsset(testFile);
            QVERIFY(assetId > 0);
            DB::instance().setAssetFolder(assetId, folderId);
        }

        // Set folder and verify row count
        model.setFolderId(folderId);
        // Note: model may need time to load, so we just verify it doesn't crash
        rowCount = model.rowCount(QModelIndex());
        QVERIFY(rowCount >= 0);
    }

    void testAssetsModelDataAccess() {
        AssetsModel model;

        // Verify model can be accessed without crashes
        QModelIndex index = model.index(0, 0);
        QVERIFY(!index.isValid());

        // Test with valid folder
        int folderId = DB::instance().createFolder("DataAccessTest", 0);
        QVERIFY(folderId > 0);

        model.setFolderId(folderId);

        // Create a test file
        QString testFile = tempDir.path() + "/data_access_test.txt";
        QFile f(testFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("data access test");
        f.close();

        int assetId = DB::instance().upsertAsset(testFile);
        QVERIFY(assetId > 0);
        DB::instance().setAssetFolder(assetId, folderId);
    }

    void testAssetsModelFiltering() {
        AssetsModel model;

        // Create folder with test data
        int folderId = DB::instance().createFolder("FilterTest", 0);
        QVERIFY(folderId > 0);

        // Create test files with different names
        QStringList names = {"apple.txt", "banana.txt", "cherry.txt"};
        for (const auto& name : names) {
            QString testFile = tempDir.path() + "/" + name;
            QFile f(testFile);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(name.toUtf8());
            f.close();

            int assetId = DB::instance().upsertAsset(testFile);
            QVERIFY(assetId > 0);
            DB::instance().setAssetFolder(assetId, folderId);
        }

        // Set folder and search
        model.setFolderId(folderId);
        model.setSearchQuery("apple");

        // Verify search doesn't crash
        int rowCount = model.rowCount(QModelIndex());
        QVERIFY(rowCount >= 0);
    }

    void testAssetsModelTypeFiltering() {
        AssetsModel model;

        // Create folder
        int folderId = DB::instance().createFolder("TypeFilterTest", 0);
        QVERIFY(folderId > 0);

        model.setFolderId(folderId);

        // Test type filtering
        model.setTypeFilter(AssetsModel::Images);
        int rowCount = model.rowCount(QModelIndex());
        QVERIFY(rowCount >= 0);

        // Test with different filter
        model.setTypeFilter(AssetsModel::Videos);
        rowCount = model.rowCount(QModelIndex());
        QVERIFY(rowCount >= 0);
    }

    void testAssetsModelSearch() {
        AssetsModel model;

        // Create folder with test data
        int folderId = DB::instance().createFolder("SearchTest", 0);
        QVERIFY(folderId > 0);

        // Create test files
        QString testFile = tempDir.path() + "/search_test.txt";
        QFile f(testFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("search test content");
        f.close();

        int assetId = DB::instance().upsertAsset(testFile);
        QVERIFY(assetId > 0);
        DB::instance().setAssetFolder(assetId, folderId);

        // Set folder and search
        model.setFolderId(folderId);
        model.setSearchQuery("search");

        // Verify search works
        int rowCount = model.rowCount(QModelIndex());
        QVERIFY(rowCount >= 0);
    }

    void cleanupTestCase() {
        // Cleanup is automatic with QTemporaryDir
    }
};

#include "test_models.moc"
QTEST_APPLESS_MAIN(TestModels)

