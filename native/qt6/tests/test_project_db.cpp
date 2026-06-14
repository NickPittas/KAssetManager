#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTextStream>

#include "project_db.h"

class TestProjectDB : public QObject {
    Q_OBJECT

    QTemporaryDir m_dbDir;

private slots:
    void initTestCase();
    void testEnsureFolderForNestedPath();
    void testEnsureFolderForCaseDistinctPathOnLinux();
    void testResyncAssetFoldersImportsRegularFiles();
};

void TestProjectDB::initTestCase()
{
    QVERIFY(m_dbDir.isValid());

    const QString dbPath = m_dbDir.path() + "/projects.db";
    QVERIFY(ProjectDB::instance().init(dbPath));
}

void TestProjectDB::testEnsureFolderForNestedPath()
{
    QTemporaryDir projectDir;
    QVERIFY(projectDir.isValid());

    const QString watchPath = QDir::cleanPath(projectDir.path());
    const int projectId = ProjectDB::instance().createProject("Test Project", watchPath);
    QVERIFY(projectId > 0);

    QDir rootDir(watchPath);
    QVERIFY(rootDir.mkpath("plates/day01"));

    const QString nestedPath = QDir(watchPath).filePath("plates/day01");
    const int nestedFolderId = ProjectDB::instance().ensureFolderForPath(projectId, nestedPath);
    QVERIFY(nestedFolderId > 0);

    const int repeatedFolderId = ProjectDB::instance().ensureFolderForPath(projectId, nestedPath);
    QCOMPARE(repeatedFolderId, nestedFolderId);

    const int rootFolderId = ProjectDB::instance().getProjectRootFolderId(projectId);
    QVERIFY(rootFolderId > 0);
    QVERIFY(nestedFolderId != rootFolderId);

    const int outsideFolderId = ProjectDB::instance().ensureFolderForPath(projectId, watchPath + "/../outside");
    QCOMPARE(outsideFolderId, rootFolderId);
}

void TestProjectDB::testEnsureFolderForCaseDistinctPathOnLinux()
{
#ifdef Q_OS_WIN
    QSKIP("Windows Project Manager matching remains case-insensitive");
#endif

    QTemporaryDir projectDir;
    QVERIFY(projectDir.isValid());

    const QString watchPath = QDir::cleanPath(projectDir.path());
    const int projectId = ProjectDB::instance().createProject("Case Sensitive Project", watchPath);
    QVERIFY(projectId > 0);

    QDir rootDir(watchPath);
    QVERIFY(rootDir.mkpath("Shots/PlateA"));
    QVERIFY(rootDir.mkpath("shots/plateb"));

    const int upperFolderId = ProjectDB::instance().ensureFolderForPath(projectId, QDir(watchPath).filePath("Shots/PlateA"));
    const int lowerFolderId = ProjectDB::instance().ensureFolderForPath(projectId, QDir(watchPath).filePath("shots/plateb"));

    QVERIFY(upperFolderId > 0);
    QVERIFY(lowerFolderId > 0);
    QVERIFY(upperFolderId != lowerFolderId);
}

void TestProjectDB::testResyncAssetFoldersImportsRegularFiles()
{
    QTemporaryDir projectDir;
    QVERIFY(projectDir.isValid());

    const QString watchPath = QDir::cleanPath(projectDir.path());
    const int projectId = ProjectDB::instance().createProject("Resync Project", watchPath);
    QVERIFY(projectId > 0);

    QDir rootDir(watchPath);
    QVERIFY(rootDir.mkpath("docs"));

    QFile notesFile(QDir(watchPath).filePath("docs/notes.txt"));
    QVERIFY(notesFile.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream(&notesFile) << "hello\n";
    notesFile.close();

    const int changedCount = ProjectDB::instance().resyncAssetFolders(projectId);
    QVERIFY(changedCount > 0);

    QSqlQuery q(ProjectDB::instance().database());
    q.prepare(
        "SELECT a.file_path, f.name "
        "FROM assets a "
        "JOIN virtual_folders f ON a.virtual_folder_id = f.id "
        "WHERE a.file_path = ?"
    );
    q.addBindValue(QDir::cleanPath(notesFile.fileName()));
    QVERIFY(q.exec());
    QVERIFY(q.next());
    QCOMPARE(QDir::cleanPath(q.value(0).toString()), QDir::cleanPath(notesFile.fileName()));
    QCOMPARE(q.value(1).toString(), QString("docs"));
}

#include "test_project_db.moc"
QTEST_MAIN(TestProjectDB)
