#include <QtTest/QtTest>
#include <QApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <limits>
#include <clocale>

#include "media/tlrender_player.h"
#include "media/tlrender_viewport.h"
#include "platform_session.h"
#include "real_media_test_helper.h"

namespace {

constexpr const char* kBenchmarkFileEnvVar = "KASSETMANAGER_TLRENDER_BENCHMARK_FILE";
constexpr int kWarmupTimeoutMs = 30000;
constexpr int kPlaybackProbeMs = 2000;
constexpr int kMinimumDistinctFps = 24;

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
    int distinctPresentedFrames = 0;
    int playerFrameSignals = 0;
    int distinctPlayerFrames = 0;
    qint64 firstRevision = 0;
    qint64 lastRevision = 0;
    qint64 lastPlayerFrameNumber = -1;
    QByteArray lastFingerprint;
    QByteArray lastPresentedFingerprint;
    double measuredDistinctFps = 0.0;
    double measuredPresentedDistinctFps = 0.0;
    double measuredRenderedSignalFps = 0.0;
    double measuredPlayerFrameFps = 0.0;
    double elapsedSeconds = 0.0;
    int renderedIntervals = 0;
    double averageRenderedIntervalMs = 0.0;
    double minRenderedIntervalMs = 0.0;
    double maxRenderedIntervalMs = 0.0;
};

QString playbackMetricsSummary(const QString& filePath, const PlaybackObservation& observation)
{
    return QStringLiteral(
               "Playback metrics for %1: elapsed=%2 s, renderedSignals=%3 (%4 fps), distinctRasterFrames=%5 (%6 fps), distinctPresentedFrames=%7 (%8 fps), distinctPlayerFrames=%9 (%10 fps), playerFrameSignals=%11, frameRenderedIntervalMs(avg/min/max)=%12/%13/%14")
        .arg(QFileInfo(filePath).absoluteFilePath())
        .arg(observation.elapsedSeconds, 0, 'f', 2)
        .arg(observation.renderedSignals)
        .arg(observation.measuredRenderedSignalFps, 0, 'f', 2)
        .arg(observation.distinctFrames)
        .arg(observation.measuredDistinctFps, 0, 'f', 2)
        .arg(observation.distinctPresentedFrames)
        .arg(observation.measuredPresentedDistinctFps, 0, 'f', 2)
        .arg(observation.distinctPlayerFrames)
        .arg(observation.measuredPlayerFrameFps, 0, 'f', 2)
        .arg(observation.playerFrameSignals)
        .arg(observation.averageRenderedIntervalMs, 0, 'f', 2)
        .arg(observation.minRenderedIntervalMs, 0, 'f', 2)
        .arg(observation.maxRenderedIntervalMs, 0, 'f', 2);
}

bool waitForMediaReadyOrFail(QSignalSpy& mediaSpy, QSignalSpy& errorSpy, int timeoutMs, QString* errorOut);

bool loadPlayerAndViewport(const QString& filePath,
                          TLRenderPlayer* player,
                          TLRenderViewport* viewport,
                          QSignalSpy* mediaSpy,
                          QSignalSpy* errorSpy,
                          QSignalSpy* frameSpy,
                          QString* loadError)
{
    if (!player || !viewport || !mediaSpy || !errorSpy || !frameSpy) {
        return false;
    }

    viewport->resize(1280, 720);
    viewport->show();
    viewport->setPlayer(player);

    if (!mediaSpy->isValid() || !errorSpy->isValid() || !frameSpy->isValid()) {
        return false;
    }

    player->loadMedia(filePath);
    if (!waitForMediaReadyOrFail(*mediaSpy, *errorSpy, kWarmupTimeoutMs, loadError)) {
        return false;
    }

    QTest::qWait(20);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return QTest::qWaitFor([viewport]() {
        return viewport->rasterPresentationRevisionForTest() > 0
            && !viewport->currentRasterFrameForTest().isNull();
    }, kWarmupTimeoutMs);
}

void advanceForBackwardTests(TLRenderPlayer* player, QSignalSpy* frameSpy)
{
    player->play();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy->count() > 10, kWarmupTimeoutMs);
    player->pause();
    
    // Wait for raster to stabilize after pause
    QTest::qWait(200);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
}

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
    qint64 lastRenderedSignalElapsedMs = -1;
    qint64 renderedIntervalTotalMs = 0;
    qint64 minRenderedIntervalMs = std::numeric_limits<qint64>::max();
    qint64 maxRenderedIntervalMs = 0;
    QElapsedTimer timer;
    timer.start();

    QObject::connect(&viewport, &TLRenderViewport::frameRendered, &viewport, [&]() {
        ++result.renderedSignals;
        const qint64 nowMs = timer.elapsed();
        if (lastRenderedSignalElapsedMs >= 0) {
            const qint64 intervalMs = nowMs - lastRenderedSignalElapsedMs;
            ++result.renderedIntervals;
            renderedIntervalTotalMs += intervalMs;
            minRenderedIntervalMs = qMin(minRenderedIntervalMs, intervalMs);
            maxRenderedIntervalMs = qMax(maxRenderedIntervalMs, intervalMs);
        }
        lastRenderedSignalElapsedMs = nowMs;
        result.lastRevision = viewport.rasterPresentationRevisionForTest();
        if (result.firstRevision == 0) {
            result.firstRevision = result.lastRevision;
        }

        const QByteArray currentFingerprint = fingerprintImage(viewport.currentRasterFrameForTest());
        if (!currentFingerprint.isEmpty() && currentFingerprint != result.lastFingerprint) {
            ++result.distinctFrames;
            result.lastFingerprint = currentFingerprint;
        }

        const QByteArray presentedFingerprint = fingerprintImage(viewport.currentPresentedFrameForTest());
        if (!presentedFingerprint.isEmpty() && presentedFingerprint != result.lastPresentedFingerprint) {
            ++result.distinctPresentedFrames;
            result.lastPresentedFingerprint = presentedFingerprint;
        }
    }, Qt::DirectConnection);
    QObject::connect(&player, &TLRenderPlayer::currentFrameChanged, &viewport, [&](qint64 frameNumber) {
        ++result.playerFrameSignals;
        if (frameNumber != result.lastPlayerFrameNumber) {
            ++result.distinctPlayerFrames;
            result.lastPlayerFrameNumber = frameNumber;
        }
    }, Qt::DirectConnection);

    while (timer.elapsed() < durationMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(10);
    }

    player.pause();

    const double seconds = timer.elapsed() / 1000.0;
    result.elapsedSeconds = seconds;
    result.measuredDistinctFps = (seconds > 0.0) ? (result.distinctFrames / seconds) : 0.0;
    result.measuredPresentedDistinctFps = (seconds > 0.0) ? (result.distinctPresentedFrames / seconds) : 0.0;
    result.measuredRenderedSignalFps = (seconds > 0.0) ? (result.renderedSignals / seconds) : 0.0;
    result.measuredPlayerFrameFps = (seconds > 0.0) ? (result.distinctPlayerFrames / seconds) : 0.0;
    if (result.renderedIntervals > 0) {
        result.averageRenderedIntervalMs = renderedIntervalTotalMs / static_cast<double>(result.renderedIntervals);
        result.minRenderedIntervalMs = minRenderedIntervalMs;
        result.maxRenderedIntervalMs = maxRenderedIntervalMs;
    }
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
    void benchmarkEnvFile();
    void mp4StepBackwardUpdatesRasterFrame();
    void mp4SeekToPreviousFrameUpdatesRasterFrame();
    void mp4ReversePlaybackUpdatesRasterFrame();
    void movStepBackwardUpdatesRasterFrame();
    void movReversePlaybackUpdatesRasterFrame();
    void allMovFilesStepBackwardAndReverse();
};

void TestTLRenderPlaybackHarness::initTestCase()
{
    setlocale(LC_NUMERIC, "C");
    QVERIFY2(PlatformSession::shouldUseRasterPreviewFallbackOnWayland(),
             "Playback harness must exercise the real Wayland raster preview path");
}

void TestTLRenderPlaybackHarness::sunshine25fpsViewerCadence()
{
    const auto media = RealMediaTestHelper::resolve(
        QStringLiteral("ELLIOT_HD_ST_EN_24FPS_20260408.mov"),
        "Required real MOV cadence file missing");
    if (!media.isAvailable()) {
        QSKIP(qPrintable(media.skipReason));
    }
    const QString filePath = media.filePath;

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);

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
    const auto media = RealMediaTestHelper::resolve(
        QStringLiteral("Atmosphere-019.mov"),
        "Required real MOV cadence file missing");
    if (!media.isAvailable()) {
        QSKIP(qPrintable(media.skipReason));
    }
    const QString filePath = media.filePath;

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);

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
    const auto media = RealMediaTestHelper::resolve(
        QStringLiteral("ELLIOT_HD_ST_EN_24FPS_20260408.mov"),
        "Fallback real cadence file missing for megyViewerCadence");
    if (!media.isAvailable()) {
        QSKIP(qPrintable(media.skipReason));
    }
    const QString filePath = media.filePath;

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);

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
    const auto media = RealMediaTestHelper::resolve(
        QStringLiteral("Atmosphere-019.mov"),
        "Fallback real media file missing for bloodHitAlphaViewerProgresses");
    if (!media.isAvailable()) {
        QSKIP(qPrintable(media.skipReason));
    }
    const QString filePath = media.filePath;

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);

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

void TestTLRenderPlaybackHarness::benchmarkEnvFile()
{
    const QString configuredPath = qEnvironmentVariable(kBenchmarkFileEnvVar).trimmed();
    if (configuredPath.isEmpty()) {
        QSKIP("Set KASSETMANAGER_TLRENDER_BENCHMARK_FILE to benchmark an arbitrary media file");
    }

    const QString filePath = QFileInfo(configuredPath).absoluteFilePath();
    QVERIFY2(QFileInfo::exists(filePath), qPrintable(QStringLiteral("Benchmark file does not exist: %1").arg(filePath)));

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);

    QSignalSpy mediaSpy(&player, &TLRenderPlayer::mediaInfoReady);
    QSignalSpy errorSpy(&player, &TLRenderPlayer::error);
    QVERIFY(mediaSpy.isValid());
    QVERIFY(errorSpy.isValid());

    player.loadMedia(filePath);

    QString loadError;
    QVERIFY2(waitForMediaReadyOrFail(mediaSpy, errorSpy, kWarmupTimeoutMs, &loadError),
             qPrintable(QStringLiteral("Benchmark file failed to become ready: %1 (%2)").arg(filePath, loadError)));

    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > 0, kWarmupTimeoutMs);
    QVERIFY2(!viewport.currentRasterFrameForTest().isNull(), "Benchmark file never produced an initial raster frame");

    player.resetFrameAcquisitionStatsForTest();
    player.play();
    const PlaybackObservation observation = observePlayback(player, viewport, kPlaybackProbeMs);
    qInfo().noquote() << playbackMetricsSummary(filePath, observation);

    QVERIFY2(observation.renderedSignals > 0,
             qPrintable(QStringLiteral("No viewer-side frameRendered signals observed for %1").arg(filePath)));
    QVERIFY2(observation.distinctFrames > 0,
             qPrintable(QStringLiteral("Viewer never presented a distinct raster frame for %1").arg(filePath)));
    QVERIFY2(observation.distinctPlayerFrames > 0,
             qPrintable(QStringLiteral("Player never reported a distinct frame for %1").arg(filePath)));
}

void TestTLRenderPlaybackHarness::mp4StepBackwardUpdatesRasterFrame()
{
    const QString filePath = QStringLiteral("/mnt/ssd2/Tests/Videos/Ns Ethereal 1.mp4");
    QVERIFY2(QFileInfo::exists(filePath), qPrintable(QStringLiteral("Required MP4 test file missing: %1").arg(filePath)));

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);

    QSignalSpy mediaSpy(&player, &TLRenderPlayer::mediaInfoReady);
    QSignalSpy errorSpy(&player, &TLRenderPlayer::error);
    QSignalSpy frameSpy(&player, &TLRenderPlayer::currentFrameChanged);
    QVERIFY(mediaSpy.isValid());
    QVERIFY(errorSpy.isValid());
    QVERIFY(frameSpy.isValid());

    player.loadMedia(filePath);

    QString loadError;
    QVERIFY2(waitForMediaReadyOrFail(mediaSpy, errorSpy, kWarmupTimeoutMs, &loadError),
             qPrintable(QStringLiteral("MP4 file failed to become ready: %1 (%2)").arg(filePath, loadError)));

    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > 0, kWarmupTimeoutMs);
    QVERIFY2(!viewport.currentRasterFrameForTest().isNull(), "MP4 never produced an initial raster frame");

    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 10, kWarmupTimeoutMs);
    player.pause();

    // Wait for raster to stabilize after pause
    QTest::qWait(200);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    const qint64 revisionBeforeStep = viewport.rasterPresentationRevisionForTest();
    const QByteArray fingerprintBeforeStep = fingerprintImage(viewport.currentRasterFrameForTest());
    const qint64 frameBeforeStep = player.currentFrame();

    player.stepBackward();

    QTRY_VERIFY_WITH_TIMEOUT(player.currentFrame() < frameBeforeStep, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > revisionBeforeStep, 5000);

    const QByteArray fingerprintAfterStep = fingerprintImage(viewport.currentRasterFrameForTest());
    QVERIFY2(!fingerprintAfterStep.isEmpty(), "Step backward produced an empty raster frame");
    QVERIFY2(player.currentFrame() < frameBeforeStep,
             qPrintable(QStringLiteral("Step backward did not move to a previous frame (before=%1 after=%2)")
                            .arg(frameBeforeStep).arg(player.currentFrame())));
}

void TestTLRenderPlaybackHarness::mp4ReversePlaybackUpdatesRasterFrame()
{
    const QString filePath = QStringLiteral("/mnt/ssd2/Tests/Videos/Ns Ethereal 1.mp4");
    QVERIFY2(QFileInfo::exists(filePath), qPrintable(QStringLiteral("Required MP4 test file missing: %1").arg(filePath)));

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);

    QSignalSpy mediaSpy(&player, &TLRenderPlayer::mediaInfoReady);
    QSignalSpy errorSpy(&player, &TLRenderPlayer::error);
    QSignalSpy frameSpy(&player, &TLRenderPlayer::currentFrameChanged);
    QVERIFY(mediaSpy.isValid());
    QVERIFY(errorSpy.isValid());
    QVERIFY(frameSpy.isValid());

    player.loadMedia(filePath);

    QString loadError;
    QVERIFY2(waitForMediaReadyOrFail(mediaSpy, errorSpy, kWarmupTimeoutMs, &loadError),
             qPrintable(QStringLiteral("MP4 file failed to become ready: %1 (%2)").arg(filePath, loadError)));

    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > 0, kWarmupTimeoutMs);
    QVERIFY2(!viewport.currentRasterFrameForTest().isNull(), "MP4 never produced an initial raster frame");

    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 10, kWarmupTimeoutMs);
    player.pause();

    // Move away from frame zero so reverse playback has room to move.
    player.seekToFrame(qMax<qint64>(5, player.currentFrame()));
    player.refreshCurrentFrame();
    const qint64 frameBeforeReverse = player.currentFrame();
    const qint64 revisionBeforeReverse = viewport.rasterPresentationRevisionForTest();
    const QByteArray fingerprintBeforeReverse = fingerprintImage(viewport.currentRasterFrameForTest());

    player.setPlaybackRate(-1.0);
    player.play();

    // Poll for reverse playback progress instead of QTRY_VERIFY_WITH_TIMEOUT
    // to avoid flaky timing with tlRender's asynchronous frame delivery.
    bool reversed = false;
    for (int i = 0; i < 50; ++i) {
        QTest::qWait(100);
        if (player.currentFrame() < frameBeforeReverse &&
            viewport.rasterPresentationRevisionForTest() > revisionBeforeReverse) {
            reversed = true;
            break;
        }
    }
    QVERIFY2(reversed,
             qPrintable(QStringLiteral("Reverse playback did not progress within 5s (before=%1 after=%2 revisionBefore=%3 revisionAfter=%4)")
                            .arg(frameBeforeReverse)
                            .arg(player.currentFrame())
                            .arg(revisionBeforeReverse)
                            .arg(viewport.rasterPresentationRevisionForTest())));
    player.pause();

    const QByteArray fingerprintAfterReverse = fingerprintImage(viewport.currentRasterFrameForTest());
    QVERIFY2(!fingerprintAfterReverse.isEmpty(), "Reverse playback produced an empty raster frame");
    QVERIFY2(player.currentFrame() < frameBeforeReverse,
             qPrintable(QStringLiteral("Reverse playback did not move to a previous frame (before=%1 after=%2)")
                            .arg(frameBeforeReverse).arg(player.currentFrame())));
}

void TestTLRenderPlaybackHarness::mp4SeekToPreviousFrameUpdatesRasterFrame()
{
    const QString filePath = QStringLiteral("/mnt/ssd2/Tests/Videos/Ns Ethereal 1.mp4");
    QVERIFY2(QFileInfo::exists(filePath), qPrintable(QStringLiteral("Required MP4 test file missing: %1").arg(filePath)));

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);

    QSignalSpy mediaSpy(&player, &TLRenderPlayer::mediaInfoReady);
    QSignalSpy errorSpy(&player, &TLRenderPlayer::error);
    QSignalSpy frameSpy(&player, &TLRenderPlayer::currentFrameChanged);
    QVERIFY(mediaSpy.isValid());
    QVERIFY(errorSpy.isValid());
    QVERIFY(frameSpy.isValid());

    player.loadMedia(filePath);

    QString loadError;
    QVERIFY2(waitForMediaReadyOrFail(mediaSpy, errorSpy, kWarmupTimeoutMs, &loadError),
             qPrintable(QStringLiteral("MP4 file failed to become ready: %1 (%2)").arg(filePath, loadError)));

    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > 0, kWarmupTimeoutMs);
    QVERIFY2(!viewport.currentRasterFrameForTest().isNull(), "MP4 never produced an initial raster frame");

    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 10, kWarmupTimeoutMs);
    player.pause();

    // Wait for raster to stabilize after pause
    QTest::qWait(200);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    const qint64 revisionBeforeSeek = viewport.rasterPresentationRevisionForTest();
    const QByteArray fingerprintBeforeSeek = fingerprintImage(viewport.currentRasterFrameForTest());
    const qint64 frameBeforeSeek = player.currentFrame();

    player.seekToFrame(qMax<qint64>(0, frameBeforeSeek - 1));
    player.refreshCurrentFrame();

    QTRY_VERIFY_WITH_TIMEOUT(player.currentFrame() < frameBeforeSeek, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > revisionBeforeSeek, 5000);

    const QByteArray fingerprintAfterSeek = fingerprintImage(viewport.currentRasterFrameForTest());
    QVERIFY2(!fingerprintAfterSeek.isEmpty(), "Seek to previous frame produced an empty raster frame");
    QVERIFY2(player.currentFrame() < frameBeforeSeek,
             qPrintable(QStringLiteral("Seek to previous frame did not move to a previous frame (before=%1 after=%2)")
                            .arg(frameBeforeSeek).arg(player.currentFrame())));
}

void TestTLRenderPlaybackHarness::movStepBackwardUpdatesRasterFrame()
{
    const QString filePath = QStringLiteral("/mnt/ssd2/Tests/Videos/Cut1GRAPHICS.mov");
    QVERIFY2(QFileInfo::exists(filePath), qPrintable(QStringLiteral("Required MOV test file missing: %1").arg(filePath)));

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);

    QSignalSpy mediaSpy(&player, &TLRenderPlayer::mediaInfoReady);
    QSignalSpy errorSpy(&player, &TLRenderPlayer::error);
    QSignalSpy frameSpy(&player, &TLRenderPlayer::currentFrameChanged);
    QVERIFY(mediaSpy.isValid());
    QVERIFY(errorSpy.isValid());
    QVERIFY(frameSpy.isValid());

    player.loadMedia(filePath);

    QString loadError;
    QVERIFY2(waitForMediaReadyOrFail(mediaSpy, errorSpy, kWarmupTimeoutMs, &loadError),
             qPrintable(QStringLiteral("MOV file failed to become ready: %1 (%2)").arg(filePath, loadError)));

    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > 0, kWarmupTimeoutMs);
    QVERIFY2(!viewport.currentRasterFrameForTest().isNull(), "MOV never produced an initial raster frame");

    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 10, kWarmupTimeoutMs);
    player.pause();

    // Wait for raster to stabilize after pause
    QTest::qWait(200);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    const qint64 revisionBeforeStep = viewport.rasterPresentationRevisionForTest();
    const QByteArray fingerprintBeforeStep = fingerprintImage(viewport.currentRasterFrameForTest());
    const qint64 frameBeforeStep = player.currentFrame();

    player.stepBackward();

    QTRY_VERIFY_WITH_TIMEOUT(player.currentFrame() < frameBeforeStep, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > revisionBeforeStep, 5000);

    const QByteArray fingerprintAfterStep = fingerprintImage(viewport.currentRasterFrameForTest());
    QVERIFY2(!fingerprintAfterStep.isEmpty(), "MOV step backward produced an empty raster frame");
    QVERIFY2(player.currentFrame() < frameBeforeStep,
             qPrintable(QStringLiteral("MOV step backward did not move to a previous frame (before=%1 after=%2)")
                            .arg(frameBeforeStep).arg(player.currentFrame())));
}

void TestTLRenderPlaybackHarness::movReversePlaybackUpdatesRasterFrame()
{
    const QString filePath = QStringLiteral("/mnt/ssd2/Tests/Videos/Cut1GRAPHICS.mov");
    QVERIFY2(QFileInfo::exists(filePath), qPrintable(QStringLiteral("Required MOV test file missing: %1").arg(filePath)));

    TLRenderPlayer player;
    TLRenderViewport viewport;
    viewport.resize(1280, 720);
    viewport.show();
    viewport.setPlayer(&player);

    QSignalSpy mediaSpy(&player, &TLRenderPlayer::mediaInfoReady);
    QSignalSpy errorSpy(&player, &TLRenderPlayer::error);
    QSignalSpy frameSpy(&player, &TLRenderPlayer::currentFrameChanged);
    QVERIFY(mediaSpy.isValid());
    QVERIFY(errorSpy.isValid());
    QVERIFY(frameSpy.isValid());

    player.loadMedia(filePath);

    QString loadError;
    QVERIFY2(waitForMediaReadyOrFail(mediaSpy, errorSpy, kWarmupTimeoutMs, &loadError),
             qPrintable(QStringLiteral("MOV file failed to become ready: %1 (%2)").arg(filePath, loadError)));

    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > 0, kWarmupTimeoutMs);
    QVERIFY2(!viewport.currentRasterFrameForTest().isNull(), "MOV never produced an initial raster frame");

    player.play();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 10, kWarmupTimeoutMs);
    player.pause();

    // Wait for raster to stabilize after pause
    QTest::qWait(200);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    player.seekToFrame(qMax<qint64>(5, player.currentFrame()));
    player.refreshCurrentFrame();
    const qint64 frameBeforeReverse = player.currentFrame();
    const qint64 revisionBeforeReverse = viewport.rasterPresentationRevisionForTest();
    const QByteArray fingerprintBeforeReverse = fingerprintImage(viewport.currentRasterFrameForTest());

    player.setPlaybackRate(-1.0);
    player.play();

    QTRY_VERIFY_WITH_TIMEOUT(player.currentFrame() < frameBeforeReverse, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > revisionBeforeReverse, 5000);
    player.pause();

    const QByteArray fingerprintAfterReverse = fingerprintImage(viewport.currentRasterFrameForTest());
    QVERIFY2(!fingerprintAfterReverse.isEmpty(), "MOV reverse playback produced an empty raster frame");
    QVERIFY2(player.currentFrame() < frameBeforeReverse,
             qPrintable(QStringLiteral("MOV reverse playback did not move to a previous frame (before=%1 after=%2)")
                            .arg(frameBeforeReverse).arg(player.currentFrame())));
}

void TestTLRenderPlaybackHarness::allMovFilesStepBackwardAndReverse()
{
    const QStringList files = {
        QStringLiteral("/mnt/ssd2/Tests/Videos/Atmosphere-019.mov"),
        QStringLiteral("/mnt/ssd2/Tests/Videos/Cut1GRAPHICS.mov"),
        QStringLiteral("/mnt/ssd2/Tests/Videos/ELLIOT_HD_ST_EN_24FPS_20260408.mov"),
        QStringLiteral("/mnt/ssd2/Tests/Videos/Ns Ethereal 1.mov"),
        QStringLiteral("/mnt/ssd2/Tests/Videos/Shot_0140_v005.mov")
    };

    for (const QString& filePath : files) {
        QVERIFY2(QFileInfo::exists(filePath), qPrintable(QStringLiteral("Required MOV test file missing: %1").arg(filePath)));

        TLRenderPlayer player;
        TLRenderViewport viewport;
        QSignalSpy mediaSpy(&player, &TLRenderPlayer::mediaInfoReady);
        QSignalSpy errorSpy(&player, &TLRenderPlayer::error);
        QSignalSpy frameSpy(&player, &TLRenderPlayer::currentFrameChanged);
        QString loadError;

        QVERIFY2(loadPlayerAndViewport(filePath, &player, &viewport, &mediaSpy, &errorSpy, &frameSpy, &loadError),
                 qPrintable(QStringLiteral("MOV file failed to become ready: %1 (%2)").arg(filePath, loadError)));

        advanceForBackwardTests(&player, &frameSpy);

        const qint64 revisionBeforeStep = viewport.rasterPresentationRevisionForTest();
        const QByteArray fingerprintBeforeStep = fingerprintImage(viewport.currentRasterFrameForTest());
        const qint64 frameBeforeStep = player.currentFrame();

        player.stepBackward();

        QTRY_VERIFY_WITH_TIMEOUT(player.currentFrame() < frameBeforeStep, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > revisionBeforeStep, 5000);

        const QByteArray fingerprintAfterStep = fingerprintImage(viewport.currentRasterFrameForTest());
        QVERIFY2(!fingerprintAfterStep.isEmpty(), qPrintable(QStringLiteral("MOV step backward produced an empty raster frame for %1").arg(filePath)));
        QVERIFY2(player.currentFrame() < frameBeforeStep,
                 qPrintable(QStringLiteral("MOV step backward did not move to a previous frame for %1 (before=%2 after=%3)").arg(filePath).arg(frameBeforeStep).arg(player.currentFrame())));

    player.seekToFrame(qMax<qint64>(5, player.currentFrame()));
    player.refreshCurrentFrame();
    // Allow tlRender to deliver a frame at the new seek position before starting reverse.
    QTest::qWait(500);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 500);
    const qint64 frameBeforeReverse = player.currentFrame();
        const qint64 revisionBeforeReverse = viewport.rasterPresentationRevisionForTest();
        const QByteArray fingerprintBeforeReverse = fingerprintImage(viewport.currentRasterFrameForTest());

    player.setPlaybackRate(-1.0);
    player.play();

    QTRY_VERIFY_WITH_TIMEOUT(player.currentFrame() < frameBeforeReverse, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(viewport.rasterPresentationRevisionForTest() > revisionBeforeReverse, 5000);
        player.pause();

        const QByteArray fingerprintAfterReverse = fingerprintImage(viewport.currentRasterFrameForTest());
        QVERIFY2(!fingerprintAfterReverse.isEmpty(), qPrintable(QStringLiteral("MOV reverse playback produced an empty raster frame for %1").arg(filePath)));
        QVERIFY2(player.currentFrame() < frameBeforeReverse,
                 qPrintable(QStringLiteral("MOV reverse playback did not move to a previous frame for %1 (before=%2 after=%3)").arg(filePath).arg(frameBeforeReverse).arg(player.currentFrame())));
    }
}

QTEST_MAIN(TestTLRenderPlaybackHarness)
#include "test_tlrender_playback_harness.moc"
