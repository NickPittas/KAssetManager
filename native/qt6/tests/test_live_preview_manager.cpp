#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QProcess>
#include "../src/live_preview_manager.h"

class TestLivePreviewManager : public QObject {
    Q_OBJECT
private slots:
    void testRequestAndCacheStillPng();
    void testRequestAndCacheVideoFrame();
    void testRapidVideoScrubKeepsLatestRequest();
};

void TestLivePreviewManager::testRequestAndCacheStillPng()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString imgPath = tmp.path() + "/color.png";

    // Create a simple 64x64 red image
    QImage img(64, 64, QImage::Format_ARGB32);
    img.fill(QColor(200, 10, 10, 255));
    QVERIFY(img.save(imgPath));

    auto &mgr = LivePreviewManager::instance();

    // Ensure clean state
    mgr.invalidate(imgPath);

    // Initially not cached
    auto handle0 = mgr.cachedFrame(imgPath, QSize(32,32), 0.0);
    QVERIFY(!handle0.isValid());

    // Request asynchronously and wait for signal
    QSignalSpy spyReady(&mgr, &LivePreviewManager::frameReady);
    mgr.requestFrame(imgPath, QSize(32,32), 0.0);

    QVERIFY2(spyReady.wait(2000), "frameReady not emitted in time");

    // Now it should be cached
    auto handle1 = mgr.cachedFrame(imgPath, QSize(32,32), 0.0);
    QVERIFY(handle1.isValid());
    QCOMPARE(handle1.size, QSize(32,32));

    // Second request should hit cache and emit immediately
    int before = spyReady.count();
    mgr.requestFrame(imgPath, QSize(32,32), 0.0);
    QTest::qWait(10); // allow queued emission
    QVERIFY(spyReady.count() >= before + 1);
}

void TestLivePreviewManager::testRequestAndCacheVideoFrame()
{
#if !defined(HAVE_TLRENDER) || !HAVE_TLRENDER
    QSKIP("video thumbnail decode is disabled in this unit target");
#endif
    if (!QFileInfo::exists(QStringLiteral("/usr/bin/ffmpeg"))) {
        QSKIP("/usr/bin/ffmpeg is not available");
    }

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString videoPath = tmp.path() + "/smoke.mp4";
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("/usr/bin/ffmpeg"), {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("testsrc2=size=64x36:rate=24:duration=1"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        videoPath,
        QStringLiteral("-y")
    });
    QVERIFY2(ffmpeg.waitForFinished(5000), "ffmpeg fixture generation timed out");
    QCOMPARE(ffmpeg.exitStatus(), QProcess::NormalExit);
    QCOMPARE(ffmpeg.exitCode(), 0);

    auto& mgr = LivePreviewManager::instance();
    mgr.invalidate(videoPath);

    QSignalSpy spyReady(&mgr, &LivePreviewManager::frameReady);
    QSignalSpy spyFailed(&mgr, &LivePreviewManager::frameFailed);
    mgr.requestFrame(videoPath, QSize(48, 48), 0.25);

    QVERIFY2(spyReady.wait(5000), "video frameReady not emitted in time");
    QCOMPARE(spyFailed.count(), 0);

    auto handle = mgr.cachedFrame(videoPath, QSize(48, 48), 0.25);
    QVERIFY(handle.isValid());
    QCOMPARE(handle.size, QSize(48, 48));
}


void TestLivePreviewManager::testRapidVideoScrubKeepsLatestRequest()
{
#if !defined(HAVE_TLRENDER) || !HAVE_TLRENDER
    QSKIP("video thumbnail decode is disabled in this unit target");
#endif
    if (!QFileInfo::exists(QStringLiteral("/usr/bin/ffmpeg"))) {
        QSKIP("/usr/bin/ffmpeg is not available");
    }

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString videoPath = tmp.path() + "/scrub.mkv";
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("/usr/bin/ffmpeg"), {
        QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("lavfi"),
        QStringLiteral("-i"), QStringLiteral("testsrc2=size=320x180:rate=24:duration=4"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        videoPath,
        QStringLiteral("-y")
    });
    QVERIFY2(ffmpeg.waitForFinished(10000), "ffmpeg MKV fixture generation timed out");
    QCOMPARE(ffmpeg.exitStatus(), QProcess::NormalExit);
    QCOMPARE(ffmpeg.exitCode(), 0);

    auto& mgr = LivePreviewManager::instance();
    mgr.invalidate(videoPath);

    QSignalSpy spyReady(&mgr, &LivePreviewManager::frameReady);
    QSignalSpy spyFailed(&mgr, &LivePreviewManager::frameFailed);
    const QSize targetSize(64, 64);
    const qreal finalPosition = 0.85;
    const QList<qreal> positions{0.05, 0.20, 0.35, 0.50, 0.65, finalPosition};
    for (qreal position : positions) {
        mgr.cancelPending();
        mgr.requestFrame(videoPath, targetSize, position);
    }

    QTRY_VERIFY_WITH_TIMEOUT(mgr.cachedFrame(videoPath, targetSize, finalPosition).isValid(), 10000);
    QCOMPARE(spyFailed.count(), 0);

    bool sawFinalReady = false;
    for (const auto& args : spyReady) {
        if (args.size() >= 2 && qFuzzyCompare(args.at(1).toReal(), finalPosition)) {
            sawFinalReady = true;
            break;
        }
    }
    QVERIFY2(sawFinalReady, "rapid scrub did not deliver the newest requested MKV frame");
}

QTEST_MAIN(TestLivePreviewManager)
#include "test_live_preview_manager.moc"

