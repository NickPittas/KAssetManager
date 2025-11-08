#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include "../src/sequence_detector.h"

class TestSequenceDetector : public QObject {
    Q_OBJECT
private slots:
    void testGeneratePattern();
    void testExtractFrameNumber();
    void testIsSequenceFile();
    void testDetectSequences_basic();
};

void TestSequenceDetector::testGeneratePattern()
{
    QCOMPARE(SequenceDetector::generatePattern("render", 4, "exr"), QString("render.####.exr"));
    QCOMPARE(SequenceDetector::generatePattern("shotA_v01", 3, "png"), QString("shotA_v01.###.png"));
}

void TestSequenceDetector::testExtractFrameNumber()
{
    int pad = 0;
    QCOMPARE(SequenceDetector::extractFrameNumber("render.0042.exr", pad), 42);
    QCOMPARE(pad, 4);

    pad = 0;
    QCOMPARE(SequenceDetector::extractFrameNumber("C0642_comp_v01.1001.exr", pad), 1001);
    QCOMPARE(pad, 4);

    pad = 0;
    QCOMPARE(SequenceDetector::extractFrameNumber("no_digits_here.txt", pad), -1);
    QCOMPARE(pad, 0);
}

void TestSequenceDetector::testIsSequenceFile()
{
    QVERIFY(SequenceDetector::isSequenceFile("shot.0001.exr"));
    QVERIFY(SequenceDetector::isSequenceFile("shot_1001.png"));
    QVERIFY(SequenceDetector::isSequenceFile("shot1001.tif"));
    QVERIFY(!SequenceDetector::isSequenceFile("image.png"));
}

void TestSequenceDetector::testDetectSequences_basic()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString base = dir.path();
    // Create a small EXR sequence with a gap: 0001, 0002, 0004
    const QString a1 = base + "/shotA.0001.exr";
    const QString a2 = base + "/shotA.0002.exr";
    const QString a4 = base + "/shotA.0004.exr";

    QFile(a1).open(QIODevice::WriteOnly);
    QFile(a2).open(QIODevice::WriteOnly);
    QFile(a4).open(QIODevice::WriteOnly);

    // Create a single PNG (should not be considered a sequence with <2 frames)
    const QString b1 = base + "/shotB_001.png";
    QFile(b1).open(QIODevice::WriteOnly);

    QStringList files{a1, a2, a4, b1};

    QVector<ImageSequence> seqs = SequenceDetector::detectSequences(files);

    // We expect only one detected sequence (shotA)
    QCOMPARE(seqs.size(), 1);
    const ImageSequence &seq = seqs.first();
    QCOMPARE(seq.baseName, QString("shotA"));
    QCOMPARE(seq.extension, QString("exr"));
    QCOMPARE(seq.paddingLength, 4);
    QCOMPARE(seq.startFrame, 1);
    QCOMPARE(seq.endFrame, 4);
    QCOMPARE(seq.frameCount, 3);
    QVERIFY(seq.hasGaps);
    QVERIFY(seq.missingFrames.contains(3));
    QCOMPARE(seq.gapCount, 1);
    QCOMPARE(seq.pattern, QString("shotA.####.exr"));
}

QTEST_APPLESS_MAIN(TestSequenceDetector)
#include "test_sequence_detector.moc"

