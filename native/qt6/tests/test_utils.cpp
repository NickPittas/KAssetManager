#include <QtTest>
#include "../src/utils.h"

class TestUtils : public QObject {
    Q_OBJECT
private slots:
    void testBinarySearchFirstTrue();
    void testBinarySearchLastTrue();
};

void TestUtils::testBinarySearchFirstTrue()
{
    // Find first x >= 10 in [0, 100]
    auto pred = [](qint64 x){ return x >= 10; };
    QCOMPARE(Utils::binarySearchFirstTrue(-1, 100, pred), 10);

    // Strictly above 0
    auto pred2 = [](qint64 x){ return x > 0; };
    QCOMPARE(Utils::binarySearchFirstTrue(0, 5, pred2), 1);
}

void TestUtils::testBinarySearchLastTrue()
{
    // Find last x <= 10 in [0, 100]
    auto pred = [](qint64 x){ return x <= 10; };
    QCOMPARE(Utils::binarySearchLastTrue(0, 100, pred), 10);

    // x < 5 in [0, 10)
    auto pred2 = [](qint64 x){ return x < 5; };
    QCOMPARE(Utils::binarySearchLastTrue(0, 10, pred2), 4);
}

QTEST_APPLESS_MAIN(TestUtils)
#include "test_utils.moc"

