#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDateTime>
#include "../src/db.h"
#include "../src/assets_model.h"

class TestModels : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY2(tmp.isValid(), "Temporary directory not created");
        const QString dbPath = tmp.path() + "/kam_test.db";
        QVERIFY(DB::instance().init(dbPath));
        rootId = DB::instance().ensureRootFolder();
        folderId = DB::instance().createFolder("Test", rootId);
        QVERIFY(folderId > 0);

        // Create two dummy files (image + video)
        imgPath = tmp.path() + "/img1.png";
        vidPath = tmp.path() + "/clip1.mp4";
        QVERIFY(writeDummy(imgPath));
        QVERIFY(writeDummy(vidPath));

        // Insert metadata fast into DB under our folder
        QVERIFY(DB::instance().insertAssetMetadataFast(imgPath, folderId) > 0);
        QVERIFY(DB::instance().insertAssetMetadataFast(vidPath, folderId) > 0);
    }

    void testAssetsModelRowCount() {
        AssetsModel model;
        model.setFolderId(folderId);
        model.reload();
        QCOMPARE(model.rowCount({}), 2);
    }

    void testAssetsModelDataAccess() {
        AssetsModel model;
        model.setFolderId(folderId);
        model.reload();
        QCOMPARE(model.rowCount({}), 2);
        auto idx0 = model.index(0, 0);
        QVERIFY(idx0.isValid());
        const QVariant fileType = model.data(idx0, AssetsModel::FileTypeRole);
        QVERIFY(fileType.isValid());
        const QVariantMap m = model.get(0);
        QVERIFY(m.contains("filePath"));
        QVERIFY(m.contains("previewState"));
        const QVariantMap preview = m.value("previewState").toMap();
        QVERIFY(preview.contains("fileType"));
    }

    void testAssetsModelTypeFiltering() {
        AssetsModel model;
        model.setFolderId(folderId);
        model.reload();

        model.setTypeFilter(AssetsModel::Images);
        model.reload();
        QCOMPARE(model.rowCount({}), 1);

        model.setTypeFilter(AssetsModel::Videos);
        model.reload();
        QCOMPARE(model.rowCount({}), 1);

        model.setTypeFilter(AssetsModel::All);
        model.reload();
        QCOMPARE(model.rowCount({}), 2);
    }

    void testAssetsModelSearch() {
        AssetsModel model;
        model.setFolderId(folderId);
        model.reload();

        model.setSearchEntireDatabase(false);
        model.setSearchQuery("img1");
        QCOMPARE(model.rowCount({}), 1);

        model.setSearchQuery("clip1");
        QCOMPARE(model.rowCount({}), 1);

        model.setSearchQuery("");
        QCOMPARE(model.rowCount({}), 2);
    }

private:
    static bool writeDummy(const QString& p) {
        QFile f(p);
        if (!f.open(QIODevice::WriteOnly)) return false;
        QByteArray bytes("dummy\n");
        // Touch mtime
        f.write(bytes);
        f.close();
        QFile::setPermissions(p, QFile::ReadOwner | QFile::WriteOwner);
        return true;
    }

    QTemporaryDir tmp;
    int rootId = 0;
    int folderId = 0;
    QString imgPath;
    QString vidPath;
};

#include "test_models.moc"
QTEST_MAIN(TestModels)
