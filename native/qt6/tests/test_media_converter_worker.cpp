#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFileInfo>
#include <QImage>
#include <QColor>
#include <QVector>

#include "../src/media_converter_worker.h"

class TestMediaConverterWorker : public QObject {
    Q_OBJECT
private slots:
    void testEmptyQueueFinishesImmediately();
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

QTEST_MAIN(TestMediaConverterWorker)
#include "test_media_converter_worker.moc"

