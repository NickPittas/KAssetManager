#include "file_utils.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class TestFileUtils : public QObject {
    Q_OBJECT

private slots:
    void validTempDirectory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const FileUtils::PathAvailabilityResult result = FileUtils::checkPathAvailability(dir.path());

        QVERIFY(result.available);
        QCOMPARE(result.failure, FileUtils::PathAvailabilityFailure::None);
        QVERIFY(!result.normalizedPath.isEmpty());
        QCOMPARE(result.directoryPath, result.normalizedPath);
        QVERIFY(!result.storageFingerprint.isEmpty());
        QVERIFY(result.message.contains(QLatin1String("Available")));
    }

    void emptyPathFailure()
    {
        const FileUtils::PathAvailabilityResult result = FileUtils::checkPathAvailability(QString());

        QVERIFY(!result.available);
        QCOMPARE(result.failure, FileUtils::PathAvailabilityFailure::EmptyPath);
        QCOMPARE(result.normalizedPath, QString());
        QCOMPARE(result.directoryPath, QString());
    }

    void missingPathFailure()
    {
        const QString missingPath = QStringLiteral("/this/path/should/not/exist/kasset");

        const FileUtils::PathAvailabilityResult result = FileUtils::checkPathAvailability(missingPath);

        QVERIFY(!result.available);
        QCOMPARE(result.failure, FileUtils::PathAvailabilityFailure::Missing);
        QVERIFY(!result.normalizedPath.isEmpty());
    }

    void fileInputNormalizesToParent()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        QFile file(dir.filePath(QStringLiteral("dummy.txt")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        const FileUtils::PathAvailabilityResult result = FileUtils::checkPathAvailability(
            file.fileName(), FileUtils::PathAvailabilityMode::FileOrContainingDirectory);

        QVERIFY(result.available);
        QCOMPARE(result.failure, FileUtils::PathAvailabilityFailure::None);
        QCOMPARE(result.normalizedPath, QDir(dir.path()).canonicalPath());
        QCOMPARE(result.directoryPath, result.normalizedPath);
    }

    void fingerprintMismatchFails()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const FileUtils::PathAvailabilityResult baseline = FileUtils::checkPathAvailability(dir.path());
        QVERIFY(baseline.available);
        QVERIFY(!baseline.storageFingerprint.isEmpty());

        const FileUtils::PathAvailabilityResult result = FileUtils::checkPathAvailability(
            dir.path(),
            FileUtils::PathAvailabilityMode::DirectoryOnly,
            QStringLiteral("bad-fingerprint"));

        QVERIFY(!result.available);
        QCOMPARE(result.failure, FileUtils::PathAvailabilityFailure::StorageFingerprintMismatch);
        QCOMPARE(result.storageFingerprint, baseline.storageFingerprint);
    }

    void firstAvailableDirectoryReturnsAvailable()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString preferred = FileUtils::firstAvailableDirectory(dir.path());
        QVERIFY(!preferred.isEmpty());
        QCOMPARE(preferred, QDir(dir.path()).canonicalPath());

        const QString fallback = FileUtils::firstAvailableDirectory();
        QVERIFY(!fallback.isEmpty());
    }
};

#include "test_file_utils.moc"
QTEST_APPLESS_MAIN(TestFileUtils)
