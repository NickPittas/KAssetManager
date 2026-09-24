#include <QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "project_folder_watcher.h"
#include "project_manager_watcher.h"

class TestProjectWatchers : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
    }

    void projectFolderWatcherRejectsMissingRoot() {
        ProjectFolderWatcher watcher;
        QSignalSpy changedSpy(&watcher, &ProjectFolderWatcher::projectFolderChanged);

        watcher.addProjectFolder(7, m_dir.path() + "/missing-root");
        watcher.refreshProjectFolder(7);

        QCOMPARE(changedSpy.count(), 0);
    }

    void projectManagerWatcherRejectsMissingRoot() {
        ProjectManagerWatcher watcher;
        QSignalSpy addedSpy(&watcher, &ProjectManagerWatcher::newFilesDetected);
        QSignalSpy removedSpy(&watcher, &ProjectManagerWatcher::filesRemoved);

        watcher.watchProject(11, m_dir.path() + "/missing-project");

        QCOMPARE(watcher.knownFiles(11).size(), 0);
        QCOMPARE(addedSpy.count(), 0);
        QCOMPARE(removedSpy.count(), 0);
    }

    void projectManagerWatcherInitialScanRunsAsyncAndCachesFiles() {
        const QString root = m_dir.path() + "/project-root";
        QVERIFY(QDir().mkpath(root + "/plates"));
        const QString rootFile = root + "/hero.mov";
        const QString childFile = root + "/plates/plate.png";
        QVERIFY(writeFile(rootFile));
        QVERIFY(writeFile(childFile));

        ProjectManagerWatcher watcher;
        watcher.watchProject(12, root);

        QTRY_VERIFY_WITH_TIMEOUT(watcher.knownFiles(12).contains(rootFile), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(watcher.knownFiles(12).contains(childFile), 3000);
    }

private:
    static bool writeFile(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        return file.write("x", 1) == 1;
    }

    QTemporaryDir m_dir;
};

#include "test_project_watchers.moc"
QTEST_MAIN(TestProjectWatchers)
