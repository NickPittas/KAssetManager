#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QImage>
#include <QSignalSpy>
#include "../src/live_preview_manager.h"

class TestLivePreviewManager : public QObject {
    Q_OBJECT
private slots:
    void testRequestAndCacheStillPng();
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

QTEST_MAIN(TestLivePreviewManager)
#include "test_live_preview_manager.moc"

