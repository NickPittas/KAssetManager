#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QApplication>
#include "../src/db.h"
#include "../src/importer.h"

class TestImporter : public QObject {
    Q_OBJECT
private slots:
    void testImportFolder_basic();
};

static void touch(const QString& path) { QFile f(path); f.open(QIODevice::WriteOnly); }

void TestImporter::testImportFolder_basic()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // Fresh DB in this temp directory
    QString dbPath = QDir(tmp.path()).filePath("kasset_autotest.sqlite");
    QVERIFY(DB::instance().init(dbPath));

    QDir base(tmp.path());
    base.mkpath("shots/A");
    base.mkpath("shots/B");

    // Sequence in A: 0001, 0002, 0004
    touch(base.filePath("shots/A/shotA.0001.exr"));
    touch(base.filePath("shots/A/shotA.0002.exr"));
    touch(base.filePath("shots/A/shotA.0004.exr"));

    // Single image in A
    touch(base.filePath("shots/A/plate.png"));

    // Non-media in B (should be ignored)
    touch(base.filePath("shots/B/readme.txt"));

    Importer imp;
    QVERIFY(imp.importFolder(base.filePath("shots")));

    // Find the newly created top-level virtual folder
    int root = DB::instance().ensureRootFolder();

    // We expect: 1 sequence asset (shotA) + 1 single image = 2 assets
    QList<int> allAssets = DB::instance().getAssetIdsInFolder(root);
    QCOMPARE(allAssets.size(), 2);
}

QTEST_MAIN(TestImporter)
#include "test_importer.moc"

