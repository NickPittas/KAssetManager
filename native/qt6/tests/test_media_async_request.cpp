#include <QtTest>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>

#include <stdexcept>

#include "media_async_request.h"

class TestMediaAsyncRequest : public QObject {
    Q_OBJECT

private slots:
    void latestOnlyDropsStaleResult() {
        MediaAsyncRequest requests;
        QSignalSpy finishedSpy(&requests, &MediaAsyncRequest::requestFinished);

        QSemaphore releaseFirst;
        const auto firstId = requests.submitLatest(QStringLiteral("preview"), [&releaseFirst] {
            releaseFirst.acquire();
            return QVariant(QStringLiteral("first"));
        });
        QVERIFY(firstId > 0);

        const auto secondId = requests.submitLatest(QStringLiteral("preview"), [] {
            return QVariant(QStringLiteral("second"));
        });
        QVERIFY(secondId > firstId);
        releaseFirst.release();

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 3000);
        const QList<QVariant> result = finishedSpy.takeFirst();
        QCOMPARE(result.at(0).toULongLong(), secondId);
        QCOMPARE(result.at(1).toString(), QStringLiteral("preview"));
        QCOMPARE(result.at(2).toString(), QStringLiteral("second"));
        QCOMPARE(requests.activeRequestCount(), 0);
    }

    void latestOnlyIsScopedPerChannel() {
        MediaAsyncRequest requests;
        QSignalSpy finishedSpy(&requests, &MediaAsyncRequest::requestFinished);

        QSemaphore releaseAsset;
        const auto staleAssetId = requests.submitLatest(QStringLiteral("asset-info-video"), [&releaseAsset] {
            releaseAsset.acquire();
            return QVariant(QStringLiteral("stale-asset"));
        });
        QVERIFY(staleAssetId > 0);

        const auto fmId = requests.submitLatest(QStringLiteral("fm-info-video"), [] {
            return QVariant(QStringLiteral("fm-current"));
        });
        QVERIFY(fmId > staleAssetId);

        const auto currentAssetId = requests.submitLatest(QStringLiteral("asset-info-video"), [] {
            return QVariant(QStringLiteral("asset-current"));
        });
        QVERIFY(currentAssetId > fmId);
        releaseAsset.release();

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 3000);
        QSet<quint64> deliveredIds;
        QSet<QString> deliveredValues;
        while (!finishedSpy.isEmpty()) {
            const QList<QVariant> result = finishedSpy.takeFirst();
            deliveredIds.insert(result.at(0).toULongLong());
            deliveredValues.insert(result.at(2).toString());
        }

        QVERIFY(!deliveredIds.contains(staleAssetId));
        QVERIFY(deliveredIds.contains(fmId));
        QVERIFY(deliveredIds.contains(currentAssetId));
        QVERIFY(deliveredValues.contains(QStringLiteral("fm-current")));
        QVERIFY(deliveredValues.contains(QStringLiteral("asset-current")));
        QCOMPARE(requests.activeRequestCount(), 0);
    }

    void explicitCancelDropsResult() {
        MediaAsyncRequest requests;
        QSignalSpy finishedSpy(&requests, &MediaAsyncRequest::requestFinished);

        QSemaphore release;
        const auto requestId = requests.submit(QStringLiteral("metadata"), [&release] {
            release.acquire();
            return QVariant(42);
        });
        QVERIFY(requestId > 0);
        QVERIFY(requests.isCurrent(requestId));

        requests.cancel(requestId);
        QVERIFY(!requests.isCurrent(requestId));
        release.release();

        QTest::qWait(100);
        QCOMPARE(finishedSpy.count(), 0);
        QCOMPARE(requests.activeRequestCount(), 0);
    }

    void callbackRunsOnRequesterThread() {
        MediaAsyncRequest requests;
        QSignalSpy finishedSpy(&requests, &MediaAsyncRequest::requestFinished);
        QThread* callbackThread = nullptr;
        connect(&requests, &MediaAsyncRequest::requestFinished, this, [&callbackThread] {
            callbackThread = QThread::currentThread();
        });

        const auto requestId = requests.submit(QStringLiteral("thread"), [] {
            return QVariant(QThread::currentThread() != qApp->thread());
        });
        QVERIFY(requestId > 0);

        QVERIFY(finishedSpy.wait(3000));
        QCOMPARE(callbackThread, QThread::currentThread());
        QCOMPARE(finishedSpy.takeFirst().at(2).toBool(), true);
    }

    void failuresAreDelivered() {
        MediaAsyncRequest requests;
        QSignalSpy failedSpy(&requests, &MediaAsyncRequest::requestFailed);

        const auto requestId = requests.submit(QStringLiteral("metadata"), []() -> QVariant {
            throw std::runtime_error("probe failed");
        });
        QVERIFY(requestId > 0);

        QVERIFY(failedSpy.wait(3000));
        const QList<QVariant> result = failedSpy.takeFirst();
        QCOMPARE(result.at(0).toULongLong(), requestId);
        QCOMPARE(result.at(1).toString(), QStringLiteral("metadata"));
        QVERIFY(result.at(2).toString().contains(QStringLiteral("probe failed")));
        QCOMPARE(requests.activeRequestCount(), 0);
    }
};

#include "test_media_async_request.moc"
QTEST_MAIN(TestMediaAsyncRequest)
