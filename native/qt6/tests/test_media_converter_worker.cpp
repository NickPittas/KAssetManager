#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QColor>
#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QVector>

#include "../src/converter_tool_resolver.h"
#include "../src/media_converter_worker.h"

namespace {

class ScopedEnvVar {
public:
    explicit ScopedEnvVar(const QByteArray& name)
        : m_name(name), m_hadValue(qEnvironmentVariableIsSet(name.constData())), m_oldValue(qgetenv(name.constData())) {}

    ~ScopedEnvVar()
    {
        if (m_hadValue) {
            qputenv(m_name.constData(), m_oldValue);
        } else {
            qunsetenv(m_name.constData());
        }
    }

private:
    QByteArray m_name;
    bool m_hadValue = false;
    QByteArray m_oldValue;
};

bool createFile(const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write("stub");
    file.close();
    return true;
}

QString appToolPath(const QString& toolName)
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(toolName);
}

bool hasBundledTool(const QString& toolName)
{
    const QFileInfo info(appToolPath(toolName));
    return info.exists() && info.isFile() && info.isExecutable();
}

QString firstFileMatching(const QString& dirPath, const QStringList& filters)
{
    const QFileInfoList matches = QDir(dirPath).entryInfoList(filters, QDir::Files, QDir::Name);
    if (matches.isEmpty()) {
        return QString();
    }
    return matches.first().absoluteFilePath();
}

}

class TestMediaConverterWorker : public QObject {
    Q_OBJECT
private slots:
    void testEmptyQueueFinishesImmediately();
    void testResolveFfmpegPrefersBundledRuntimeOnLinux();
    void testResolveMagickSupportsLinuxBundledDevAndEnvCandidates();
    void testResolveSiblingFfprobePrefersResolvedLinuxSibling();
    void testFallbackStateDistinguishesStrictVerificationInputs();
    void testBundledMagickConvertsPngToJpg();
    void testBundledFfmpegConvertsMp4ToJpgSequence();
};

void TestMediaConverterWorker::testEmptyQueueFinishesImmediately()
{
    MediaConverterWorker worker;
    QSignalSpy spyQueueFinished(&worker, &MediaConverterWorker::queueFinished);
    QVector<MediaConverterWorker::Task> tasks; // empty
    worker.start(tasks);
    // Should emit queueFinished(true) synchronously
    if (spyQueueFinished.count() == 0) {
        spyQueueFinished.wait(200);
    }
    QVERIFY(spyQueueFinished.count() > 0);
    const auto args = spyQueueFinished.takeFirst();
    QVERIFY(args.size() >= 1);
    QVERIFY(args.at(0).toBool());
}

void TestMediaConverterWorker::testResolveFfmpegPrefersBundledRuntimeOnLinux()
{
#ifdef Q_OS_WIN
    QSKIP("Linux-first resolver behavior is covered on non-Windows platforms.");
#endif

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString appDir = QDir(tempDir.path()).filePath("app");
    QVERIFY(QDir().mkpath(appDir));
    const QString bundledFfmpeg = QDir(appDir).filePath("ffmpeg");
    QVERIFY(createFile(bundledFfmpeg));

    const ConverterToolResolution resolution = resolveFfmpegTool(appDir);
    QCOMPARE(resolution.toolName, QStringLiteral("ffmpeg"));
    QCOMPARE(resolution.source, ConverterToolSource::BundledRuntime);
    QVERIFY(resolution.isAbsolute());
    QVERIFY(!resolution.isFallback());
    QCOMPARE(resolution.resolvedPath, QFileInfo(bundledFfmpeg).absoluteFilePath());
}

void TestMediaConverterWorker::testResolveMagickSupportsLinuxBundledDevAndEnvCandidates()
{
#ifdef Q_OS_WIN
    QSKIP("Linux-first resolver behavior is covered on non-Windows platforms.");
#endif

    ScopedEnvVar magickRootGuard("MAGICK_ROOT");
    ScopedEnvVar imageMagickRootGuard("IMAGEMAGICK_ROOT");
    qunsetenv("MAGICK_ROOT");
    qunsetenv("IMAGEMAGICK_ROOT");

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString appDir = QDir(tempDir.path()).filePath("build/bin/app");
    QVERIFY(QDir().mkpath(appDir));

    const QString devMagick = QDir(tempDir.path()).filePath("build/third_party/ImageMagick-test/bin/magick");
    QVERIFY(createFile(devMagick));

    ConverterToolResolution resolution = resolveMagickTool(appDir);
    QCOMPARE(resolution.source, ConverterToolSource::BundledDevCheckout);
    QCOMPARE(resolution.resolvedPath, QFileInfo(devMagick).absoluteFilePath());
    QVERIFY(resolution.isAbsolute());
    QVERIFY(!resolution.isFallback());

    QFile::remove(devMagick);
    const QString envRoot = QDir(tempDir.path()).filePath("env/imagemagick");
    const QString envMagick = QDir(envRoot).filePath("bin/magick");
    QVERIFY(createFile(envMagick));
    qputenv("MAGICK_ROOT", QFileInfo(envRoot).absoluteFilePath().toUtf8());

    resolution = resolveMagickTool(appDir);
    QCOMPARE(resolution.source, ConverterToolSource::EnvOverride);
    QCOMPARE(resolution.resolvedPath, QFileInfo(envMagick).absoluteFilePath());
    QVERIFY(resolution.isAbsolute());
    QVERIFY(!resolution.isFallback());
}

void TestMediaConverterWorker::testResolveSiblingFfprobePrefersResolvedLinuxSibling()
{
#ifdef Q_OS_WIN
    QSKIP("Linux-first resolver behavior is covered on non-Windows platforms.");
#endif

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString ffmpegPath = QDir(tempDir.path()).filePath("bundle/ffmpeg");
    const QString ffprobePath = QDir(tempDir.path()).filePath("bundle/ffprobe");
    QVERIFY(createFile(ffmpegPath));
    QVERIFY(createFile(ffprobePath));

    QCOMPARE(resolveSiblingFfprobePath(ffmpegPath), QFileInfo(ffprobePath).absoluteFilePath());

    QFile::remove(ffprobePath);
    QCOMPARE(resolveSiblingFfprobePath(ffmpegPath), QStringLiteral("ffprobe"));
}

void TestMediaConverterWorker::testFallbackStateDistinguishesStrictVerificationInputs()
{
    const ConverterToolResolution fallback {
        QStringLiteral("ffmpeg"),
        QStringLiteral("ffmpeg"),
        ConverterToolSource::PathFallback
    };
    const ConverterToolResolution bundled {
        QStringLiteral("ffmpeg"),
        QStringLiteral("/tmp/app/ffmpeg"),
        ConverterToolSource::BundledRuntime
    };
    const ConverterToolResolution env {
        QStringLiteral("magick"),
        QStringLiteral("/tmp/tools/bin/magick"),
        ConverterToolSource::EnvOverride
    };

    QVERIFY(fallback.isFallback());
    QVERIFY(!fallback.isAbsolute());
    QVERIFY(!bundled.isFallback());
    QVERIFY(bundled.isAbsolute());
    QVERIFY(!env.isFallback());
    QVERIFY(env.isAbsolute());
}

void TestMediaConverterWorker::testBundledMagickConvertsPngToJpg()
{
#ifdef Q_OS_WIN
    QSKIP("Linux bundled converter smoke test is only relevant on non-Windows platforms.");
#endif

    if (!hasBundledTool(QStringLiteral("magick"))) {
        QSKIP("Bundled magick not present next to the test binary.");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString inputPath = QDir(tempDir.path()).filePath(QStringLiteral("input.png"));
    const QString outputDir = QDir(tempDir.path()).filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(outputDir));

    QImage image(32, 32, QImage::Format_RGBA8888);
    image.fill(QColor(10, 120, 200, 255));
    QVERIFY(image.save(inputPath));

    MediaConverterWorker worker;
    worker.setMagickPath(appToolPath(QStringLiteral("magick")));

    MediaConverterWorker::Task task;
    task.sourcePath = inputPath;
    task.outputDir = outputDir;
    task.target = MediaConverterWorker::TargetKind::ImageJpg;
    task.jpg.quality = 85;

    QSignalSpy spyFinished(&worker, &MediaConverterWorker::queueFinished);
    worker.start({task});
    QVERIFY2(spyFinished.wait(10000) || spyFinished.count() > 0, "Timed out waiting for ImageMagick conversion");
    QVERIFY(spyFinished.takeFirst().at(0).toBool());

    const QString outputPath = QDir(outputDir).filePath(QStringLiteral("input.jpg"));
    QVERIFY2(QFileInfo::exists(outputPath), qPrintable(outputPath));
    QImage converted(outputPath);
    QVERIFY(!converted.isNull());
}

void TestMediaConverterWorker::testBundledFfmpegConvertsMp4ToJpgSequence()
{
#ifdef Q_OS_WIN
    QSKIP("Linux bundled converter smoke test is only relevant on non-Windows platforms.");
#endif

    if (!hasBundledTool(QStringLiteral("ffmpeg")) || !hasBundledTool(QStringLiteral("ffprobe"))) {
        QSKIP("Bundled ffmpeg/ffprobe not present next to the test binary.");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString inputPath = QDir(tempDir.path()).filePath(QStringLiteral("sample.mp4"));
    const QString outputDir = QDir(tempDir.path()).filePath(QStringLiteral("out"));
    QVERIFY(QDir().mkpath(outputDir));

    QProcess generator;
    generator.start(appToolPath(QStringLiteral("ffmpeg")), {
        QStringLiteral("-y"),
        QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("testsrc=size=96x54:rate=12"),
        QStringLiteral("-t"), QStringLiteral("1"),
        inputPath,
    });
    QVERIFY2(generator.waitForFinished(10000), "Timed out generating MP4 test input");
    QCOMPARE(generator.exitStatus(), QProcess::NormalExit);
    QCOMPARE(generator.exitCode(), 0);
    QVERIFY(QFileInfo::exists(inputPath));

    MediaConverterWorker worker;
    worker.setFfmpegPath(appToolPath(QStringLiteral("ffmpeg")));

    MediaConverterWorker::Task task;
    task.sourcePath = inputPath;
    task.outputDir = outputDir;
    task.target = MediaConverterWorker::TargetKind::JpgSequence;
    task.jpgSeq.qscale = 4;
    task.jpgSeq.padDigits = 4;
    task.jpgSeq.startNumber = 1;

    QSignalSpy spyFinished(&worker, &MediaConverterWorker::queueFinished);
    worker.start({task});
    QVERIFY2(spyFinished.wait(15000) || spyFinished.count() > 0, "Timed out waiting for FFmpeg conversion");
    QVERIFY(spyFinished.takeFirst().at(0).toBool());

    const QString sequenceDir = QDir(outputDir).filePath(QStringLiteral("sample_jpg_seq"));
    QVERIFY(QDir(sequenceDir).exists());
    const QString firstFrame = firstFileMatching(sequenceDir, {QStringLiteral("*.jpg")});
    QVERIFY2(!firstFrame.isEmpty(), "No JPG frames were produced");
    QImage converted(firstFrame);
    QVERIFY(!converted.isNull());
}

QTEST_MAIN(TestMediaConverterWorker)
#include "test_media_converter_worker.moc"
