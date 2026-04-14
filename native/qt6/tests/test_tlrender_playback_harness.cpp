#include <QtTest/QtTest>
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <clocale>

#include "media/tlrender_player.h"
#include "media/tlrender_viewport.h"
#include "platform_session.h"

namespace {

constexpr const char* kFixtureRoot = "tests/fixtures/videos";
constexpr int kWarmupTimeoutMs = 30000;
constexpr int kPlaybackProbeMs = 2000;
constexpr int kMinimumDistinctFps = 24;

QString fixturePath(const QString& fileName)
{
    const QString root = QCoreApplication::applicationDirPath() + QStringLiteral("/") + QString::fromUtf8(kFixtureRoot);
    return root + QStringLiteral("/") + fileName;
}

QByteArray fingerprintImage(const QImage& image)
{
    if (image.isNull()) {
        return {};
    }

    QImage normalized = image.convertToFormat(QImage::Format_RGBA8888);
    return QCryptographicHash::hash(
        QByteArray(reinterpret_cast<const char*>(normalized.constBits()), static_cast<int>(normalized.sizeInBytes())),
        QCryptographicHash::Sha256);
}

struct PlaybackObservation {
    int renderedSignals = 0;
    int distinctFrames = 0;
    int playerFrameSignals = 0;
    int distinctPlayerFrames = 0;
    qint64 firstRevision = 0;
    qint64 lastRevision = 0;
    qint64 lastPlayerFrameNumber = -1;
    QByteArray lastFingerprint;
    double measuredDistinctFps = 0.0;
    double measuredRenderedSignalFps = 0.0;
    double measuredPlayerFrameFps = 0.0;
};

bool waitForMediaReadyOrFail(QSignalSpy& mediaSpy, QSignalSpy& errorSpy, int timeoutMs, QString* errorOut)
{
    if (mediaSpy.count() > 0) {
        return true;
    }
    if (errorSpy.count() > 0) {
        if (errorOut && !errorSpy.isEmpty() && !errorSpy.first().isEmpty()) {
            *errorOut = errorSpy.first().first().toString();
        }
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (mediaSpy.count() > 0) {
            return true;
        }
        if (errorSpy.count() > 0) {
            if (errorOut && !errorSpy.isEmpty() && !errorSpy.first().isEmpty()) {
                *errorOut = errorSpy.first().first().toString();
            }
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }

    if (errorOut) {
        *errorOut = QStringLiteral("Timed out waiting for mediaInfoReady");
    }
    return false;
}

PlaybackObservation observePlayback(TLRenderPlayer& player, TLRenderViewport& viewport, int durationMs)
{
    PlaybackObservation result;

    QObject::connect(&viewport, &TLRenderViewport::frameRendered, &viewport, [&]() {
        ++result.renderedSignals;
        result.lastRevision = viewport.rasterPresentationRevisionForTest();
        if (result.firstRevision == 0) {
            result.firstRevision = result.lastRevision;
        }

        const QByteArray currentFingerprint = fingerprintImage(viewport.currentRasterFrameForTest());
        if (!currentFingerprint.isEmpty() && currentFingerprint != result.lastFingerprint) {
            ++result.distinctFrames;
            result.lastFingerprint = currentFingerprint;
        }
    }, Qt::DirectConnection);
    QObject::connect(&player, &TLRenderPlayer::currentFrameChanged, &viewport, [&](qint64 frameNumber) {
        ++result.playerFrameSignals;
        if (frameNumber != result.lastPlayerFrameNumber) {
            ++result.distinctPlayerFrames;
            result.lastPlayerFrameNumber = frameNumber;
        }
    }, Qt::DirectConnection);

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < durationMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }

    player.pause();

    const double seconds = durationMs / 1000.0;
    result.measuredDistinctFps = (seconds > 0.0) ? (result.distinctFrames / seconds) : 0.0;
    result.measuredRenderedSignalFps = (seconds > 0.0) ? (result.renderedSignals / seconds) : 0.0;
    result.measuredPlayerFrameFps = (seconds > 0.0) ? (result.distinctPlayerFrames / seconds) : 0.0;
    return result;
}

} // namespace

class TestTLRenderPlaybackHarness : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sunshine25fpsViewerCadence();
    void atmosphereViewerCadence();
    void megyViewerCadence();
    void bloodHitAlphaViewerProgresses();
};

void TestTLRenderPlaybackHarness::initTestCase()
{
    setlocale(LC_NUMERIC, "C");
    const QString fixtureRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/") + QString::fromUtf8(kFixtureRoot);
    QVERIFY2(QFileInfo::exists(fixtureRoot), qPrintable(QStringLiteral("Fixture root does not exist: %1").arg(fixtureRoot)));
    QVERIFY2(PlatformSession::shouldUseRasterPreviewFallbackOnWayland(),
             "Playback harness must exercise the real Wayland raster preview path");
}

void TestTLRenderPlaybackHarness::sunshine25fpsViewerCadence()
{
    const QString filePath = fixturePath(QStringLiteral("Sunshine 10sec Full Comp v3.mov"));
    QVERIFY2(QFileInfo::exists(filePath), qPrintable(QStringLiteral("Missing fixture: %1").arg(filePath)));

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);
    viewport.prepareForMpvPlayback();

    QSignalSpy mediaSpy(&player, &TLRenderPlayer::mediaInfoReady);
    QSignalSpy errorSpy(&player, &TLRenderPlayer::error);
    QVERIFY(mediaSpy.isValid());
    QVERIFY(errorSpy.isValid());

    player.loadMedia(filePath);

    QString loadError;
    QVERIFY2(waitForMediaReadyOrFail(mediaSpy, errorSpy, kWarmupTimeoutMs, &loadError),
             qPrintable(QStringLiteral("25fps MOV fixture failed to become ready: %1 (%2)").arg(filePath, loadError)));

    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > 0, kWarmupTimeoutMs);
    QVERIFY2(!viewport.currentRasterFrameForTest().isNull(), "Viewport never produced an initial raster frame");

    player.resetFrameAcquisitionStatsForTest();
    player.play();
    const PlaybackObservation observation = observePlayback(player, viewport, kPlaybackProbeMs);
    const TLRenderPlayer::FrameAcquisitionStats frameStats = player.frameAcquisitionStatsForTest();

    QVERIFY2(observation.renderedSignals > 0,
             qPrintable(QStringLiteral("No viewer-side frameRendered signals observed for %1").arg(filePath)));
    QVERIFY2(observation.distinctFrames > 0,
             qPrintable(QStringLiteral("Viewer never presented a distinct raster frame for %1").arg(filePath)));
    QVERIFY2(observation.measuredDistinctFps >= kMinimumDistinctFps,
             qPrintable(QStringLiteral(
                            "Expected >= %1 distinct viewer fps for %2, observed %3 fps (%4 distinct frames, %5 render signals, %6 distinct player frames, %7 fallback extractions, last image type %8, last cached conversion %9 ms, last fallback %10 ms)")
                            .arg(kMinimumDistinctFps)
                            .arg(filePath)
                            .arg(observation.measuredDistinctFps, 0, 'f', 2)
                            .arg(observation.distinctFrames)
                            .arg(observation.renderedSignals)
                            .arg(observation.distinctPlayerFrames)
                            .arg(frameStats.fallbackExtractions)
                            .arg(frameStats.lastCachedImageType)
                           .arg(frameStats.lastCachedConversionNs / 1000000.0, 0, 'f', 2)
                           .arg(frameStats.lastFallbackExtractionNs / 1000000.0, 0, 'f', 2)));
}

void TestTLRenderPlaybackHarness::atmosphereViewerCadence()
{
    const QString filePath = fixturePath(QStringLiteral("Atmosphere-019.mov"));
    QVERIFY2(QFileInfo::exists(filePath), qPrintable(QStringLiteral("Missing fixture: %1").arg(filePath)));

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);
    viewport.prepareForMpvPlayback();

    QSignalSpy mediaSpy(&player, &TLRenderPlayer::mediaInfoReady);
    QSignalSpy errorSpy(&player, &TLRenderPlayer::error);
    QVERIFY(mediaSpy.isValid());
    QVERIFY(errorSpy.isValid());

    player.loadMedia(filePath);

    QString loadError;
    QVERIFY2(waitForMediaReadyOrFail(mediaSpy, errorSpy, kWarmupTimeoutMs, &loadError),
             qPrintable(QStringLiteral("Atmosphere MOV fixture failed to become ready: %1 (%2)").arg(filePath, loadError)));

    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > 0, kWarmupTimeoutMs);
    QVERIFY2(!viewport.currentRasterFrameForTest().isNull(), "Atmosphere MOV never produced an initial raster frame");

    player.resetFrameAcquisitionStatsForTest();
    player.play();
    const PlaybackObservation observation = observePlayback(player, viewport, kPlaybackProbeMs);
    const TLRenderPlayer::FrameAcquisitionStats frameStats = player.frameAcquisitionStatsForTest();

    QVERIFY2(observation.renderedSignals > 0,
             qPrintable(QStringLiteral("No viewer-side frameRendered signals observed for %1").arg(filePath)));
    QVERIFY2(observation.distinctFrames > 0,
             qPrintable(QStringLiteral("Viewer never presented a distinct raster frame for %1").arg(filePath)));
    QVERIFY2(observation.measuredDistinctFps >= 22,
             qPrintable(QStringLiteral(
                            "Expected >= %1 distinct viewer fps for %2, observed %3 fps (%4 distinct frames, %5 render signals, %6 distinct player frames, %7 fallback extractions, last image type %8, last cached conversion %9 ms, last fallback %10 ms)")
                            .arg(22)
                            .arg(filePath)
                            .arg(observation.measuredDistinctFps, 0, 'f', 2)
                            .arg(observation.distinctFrames)
                            .arg(observation.renderedSignals)
                            .arg(observation.distinctPlayerFrames)
                            .arg(frameStats.fallbackExtractions)
                            .arg(frameStats.lastCachedImageType)
                            .arg(frameStats.lastCachedConversionNs / 1000000.0, 0, 'f', 2)
                            .arg(frameStats.lastFallbackExtractionNs / 1000000.0, 0, 'f', 2)));
}

void TestTLRenderPlaybackHarness::megyViewerCadence()
{
    const QString filePath = fixturePath(QStringLiteral("MEGY_comp_4K_LL180_ap0_r709g24_v015.mov"));
    QVERIFY2(QFileInfo::exists(filePath), qPrintable(QStringLiteral("Missing fixture: %1").arg(filePath)));

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);
    viewport.prepareForMpvPlayback();

    QSignalSpy mediaSpy(&player, &TLRenderPlayer::mediaInfoReady);
    QSignalSpy errorSpy(&player, &TLRenderPlayer::error);
    QVERIFY(mediaSpy.isValid());
    QVERIFY(errorSpy.isValid());

    player.loadMedia(filePath);

    QString loadError;
    QVERIFY2(waitForMediaReadyOrFail(mediaSpy, errorSpy, kWarmupTimeoutMs, &loadError),
             qPrintable(QStringLiteral("MEGY MOV fixture failed to become ready: %1 (%2)").arg(filePath, loadError)));

    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > 0, kWarmupTimeoutMs);
    QVERIFY2(!viewport.currentRasterFrameForTest().isNull(), "MEGY MOV never produced an initial raster frame");

    player.resetFrameAcquisitionStatsForTest();
    player.play();
    const PlaybackObservation observation = observePlayback(player, viewport, kPlaybackProbeMs);
    const TLRenderPlayer::FrameAcquisitionStats frameStats = player.frameAcquisitionStatsForTest();

    QVERIFY2(observation.renderedSignals > 0,
             qPrintable(QStringLiteral("No viewer-side frameRendered signals observed for %1").arg(filePath)));
    QVERIFY2(observation.distinctFrames > 0,
             qPrintable(QStringLiteral("Viewer never presented a distinct raster frame for %1").arg(filePath)));
    QVERIFY2(observation.measuredDistinctFps >= 27,
             qPrintable(QStringLiteral(
                            "Expected >= %1 distinct viewer fps for %2, observed %3 fps (%4 distinct frames, %5 render signals, %6 distinct player frames, %7 fallback extractions, last image type %8, last cached conversion %9 ms, last fallback %10 ms)")
                            .arg(27)
                            .arg(filePath)
                            .arg(observation.measuredDistinctFps, 0, 'f', 2)
                            .arg(observation.distinctFrames)
                            .arg(observation.renderedSignals)
                            .arg(observation.distinctPlayerFrames)
                            .arg(frameStats.fallbackExtractions)
                            .arg(frameStats.lastCachedImageType)
                            .arg(frameStats.lastCachedConversionNs / 1000000.0, 0, 'f', 2)
                            .arg(frameStats.lastFallbackExtractionNs / 1000000.0, 0, 'f', 2)));
}

void TestTLRenderPlaybackHarness::bloodHitAlphaViewerProgresses()
{
    const QString filePath = fixturePath(QStringLiteral("Blood_Hit_03.mov"));
    QVERIFY2(QFileInfo::exists(filePath), qPrintable(QStringLiteral("Missing fixture: %1").arg(filePath)));

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);
    viewport.prepareForMpvPlayback();

    QSignalSpy mediaSpy(&player, &TLRenderPlayer::mediaInfoReady);
    QSignalSpy errorSpy(&player, &TLRenderPlayer::error);
    QVERIFY(mediaSpy.isValid());
    QVERIFY(errorSpy.isValid());

    player.loadMedia(filePath);

    QString loadError;
    QVERIFY2(waitForMediaReadyOrFail(mediaSpy, errorSpy, kWarmupTimeoutMs, &loadError),
             qPrintable(QStringLiteral("Alpha MOV fixture failed to become ready: %1 (%2)").arg(filePath, loadError)));

    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > 0, kWarmupTimeoutMs);
    QVERIFY2(!viewport.currentRasterFrameForTest().isNull(), "Alpha MOV never produced an initial raster frame");

    player.resetFrameAcquisitionStatsForTest();
    player.play();
    const PlaybackObservation observation = observePlayback(player, viewport, kPlaybackProbeMs);
    const TLRenderPlayer::FrameAcquisitionStats frameStats = player.frameAcquisitionStatsForTest();

    QVERIFY2(observation.renderedSignals > 1,
             qPrintable(QStringLiteral("Expected repeated viewer renders for alpha MOV %1, observed %2 signals")
                            .arg(filePath)
                            .arg(observation.renderedSignals)));
    QVERIFY2(observation.distinctFrames > 1,
             qPrintable(QStringLiteral("Expected multiple distinct viewer frames for alpha MOV %1, observed %2 distinct frames (distinct player frames %3, fallback extractions %4, last image type %5)")
                            .arg(filePath)
                            .arg(observation.distinctFrames)
                            .arg(observation.distinctPlayerFrames)
                            .arg(frameStats.fallbackExtractions)
                            .arg(frameStats.lastCachedImageType)));
}

QTEST_MAIN(TestTLRenderPlaybackHarness)
#include "test_tlrender_playback_harness.moc"
