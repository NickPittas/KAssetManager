#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "project_assets_model.h"
#include "project_db.h"

class TestProjectAssetsModel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_dbDir.isValid());
        const QString dbPath = m_dbDir.path() + "/projects.db";
        QVERIFY(ProjectDB::instance().init(dbPath));
    }

    void testReloadWithNoProjectEmitsOneResetPair() {
        ProjectAssetsModel model;

        int aboutToResetCount = 0;
        int resetCount = 0;
        connect(&model, &QAbstractItemModel::modelAboutToBeReset,
                this, [&aboutToResetCount]() { ++aboutToResetCount; });
        connect(&model, &QAbstractItemModel::modelReset,
                this, [&resetCount]() { ++resetCount; });

        model.reload();

        QCOMPARE(aboutToResetCount, 1);
        QCOMPARE(resetCount, 1);
        QCOMPARE(model.rowCount({}), 0);
    }

    void testMultipleFilterSettersCoalesceToOneReset() {
        ProjectAssetsModel model;

        QSignalSpy aboutToResetSpy(&model, &QAbstractItemModel::modelAboutToBeReset);
        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

        // Multiple filter changes in the same event-loop turn must be coalesced
        // into a single queued reset.
        model.setSearchQuery("foo");
        model.setTypeFilter(ProjectAssetsModel::Images);
        model.setShowAllVersions(true);
        model.setFolderId(1);

        // No synchronous reset should have happened yet.
        QCOMPARE(aboutToResetSpy.count(), 0);
        QCOMPARE(resetSpy.count(), 0);

        // Wait for the queued coalesced reset.
        QVERIFY(resetSpy.wait(1000));
        QCOMPARE(aboutToResetSpy.count(), 1);
        QCOMPARE(resetSpy.count(), 1);
    }

    void testFilterResetPreservesLoadedRowFiltering() {
        const QString projectRoot = m_dbDir.path() + "/filter-project";
        QVERIFY(QDir().mkpath(projectRoot + "/plates"));
        QVERIFY(writeFile(projectRoot + "/hero.png"));
        QVERIFY(writeFile(projectRoot + "/hero.mov"));
        QVERIFY(writeFile(projectRoot + "/plates/plate.png"));

        ProjectDB& db = ProjectDB::instance();
        const int projectId = db.createProject("Filter Project", projectRoot);
        QVERIFY(projectId > 0);
        const int rootFolderId = db.getProjectRootFolderId(projectId);
        QVERIFY(rootFolderId > 0);
        const int platesFolderId = db.ensureFolderForPath(projectId, projectRoot + "/plates");
        QVERIFY(platesFolderId > 0);
        QVERIFY(db.insertAssetMetadataFast(projectRoot + "/hero.png", rootFolderId) > 0);
        QVERIFY(db.insertAssetMetadataFast(projectRoot + "/hero.mov", rootFolderId) > 0);
        QVERIFY(db.insertAssetMetadataFast(projectRoot + "/plates/plate.png", platesFolderId) > 0);

        ProjectAssetsModel model;
        model.setProjectId(projectId);
        QCOMPARE(model.rowCount({}), 3);

        QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
        model.setTypeFilter(ProjectAssetsModel::Images);
        QVERIFY(resetSpy.wait(1000));
        QCOMPARE(model.rowCount({}), 2);

        model.setSearchQuery("plate");
        QVERIFY(resetSpy.wait(1000));
        QCOMPARE(model.rowCount({}), 1);
        QCOMPARE(model.data(model.index(0, 0), ProjectAssetsModel::FileNameRole).toString(), QString("plate.png"));

        model.setFolderId(rootFolderId);
        QVERIFY(resetSpy.wait(1000));
        QCOMPARE(model.rowCount({}), 1);
        QCOMPARE(model.data(model.index(0, 0), ProjectAssetsModel::FileNameRole).toString(), QString("plates"));
        QCOMPARE(model.data(model.index(0, 0), ProjectAssetsModel::IsFolderRole).toBool(), true);
    }

private:
    static bool writeFile(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write("x", 1) == 1;
    }

    QTemporaryDir m_dbDir;
};

#include "test_project_assets_model.moc"
QTEST_MAIN(TestProjectAssetsModel)
