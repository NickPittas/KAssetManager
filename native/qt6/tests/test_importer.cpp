#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QApplication>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include "../src/db.h"
#include "../src/import_controller.h"
#include "../src/importer.h"

class TestImporter : public QObject {
    Q_OBJECT
private slots:
    void testImportFolder_basic();
    void testImportController_assetLibraryAsync();
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


void TestImporter::testImportController_assetLibraryAsync()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString dbPath = QDir(tmp.path()).filePath("kasset_async_import.sqlite");
    int rootId = 0;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "async_import_test");
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("PRAGMA foreign_keys=ON"));
        QVERIFY(q.exec("CREATE TABLE virtual_folders (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, parent_id INTEGER NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP)"));
        QVERIFY(q.exec("CREATE TABLE assets (id INTEGER PRIMARY KEY AUTOINCREMENT, file_path TEXT NOT NULL UNIQUE, file_name TEXT NOT NULL, virtual_folder_id INTEGER NOT NULL, file_size INTEGER NULL, file_type TEXT NULL, last_modified TEXT NULL, checksum TEXT NULL, is_sequence INTEGER DEFAULT 0, sequence_pattern TEXT NULL, sequence_start_frame INTEGER NULL, sequence_end_frame INTEGER NULL, sequence_frame_count INTEGER NULL, sequence_has_gaps INTEGER DEFAULT 0, sequence_gap_count INTEGER DEFAULT 0, sequence_version TEXT NULL, created_at TEXT DEFAULT CURRENT_TIMESTAMP, updated_at TEXT DEFAULT CURRENT_TIMESTAMP)"));
        QVERIFY(q.exec("INSERT INTO virtual_folders(name,parent_id) VALUES('Root',NULL)"));
        rootId = q.lastInsertId().toInt();
        QVERIFY(rootId > 0);
    }
    QSqlDatabase::removeDatabase("async_import_test");

    QDir base(tmp.path());
    base.mkpath("drop/sub");
    touch(base.filePath("drop/sub/plate.png"));
    touch(base.filePath("drop/sub/readme.txt"));

    ImportController controller;
    QVERIFY(controller.start(ImportController::DatabaseKind::AssetLibrary, dbPath));
    QSignalSpy finished(&controller, &ImportController::importFinished);
    QSignalSpy progress(&controller, &ImportController::progressChanged);
    QVERIFY(controller.importToAssetLibrary(QStringList(), QStringList{base.filePath("drop")}, rootId, true));
    QVERIFY(finished.wait(5000));
    QVERIFY(!progress.isEmpty());

    const QList<QVariant> args = finished.takeFirst();
    QCOMPARE(args.at(0).toInt(), 1);
    QVERIFY(!args.at(1).value<QList<int>>().isEmpty());

    {
        QSqlDatabase verifyDb = QSqlDatabase::addDatabase("QSQLITE", "async_import_verify");
        verifyDb.setDatabaseName(dbPath);
        QVERIFY(verifyDb.open());
        QSqlQuery count(verifyDb);
        QVERIFY(count.exec("SELECT COUNT(*) FROM assets WHERE file_name='plate.png'"));
        QVERIFY(count.next());
        QCOMPARE(count.value(0).toInt(), 1);
    }
    QSqlDatabase::removeDatabase("async_import_verify");
}

QTEST_MAIN(TestImporter)
#include "test_importer.moc"

