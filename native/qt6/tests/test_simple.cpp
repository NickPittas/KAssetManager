#include <QtTest>

class TestSimple : public QObject {
    Q_OBJECT

private slots:
    void testArithmetic() {
        QCOMPARE(2 + 2, 4);
    }

    void testString() {
        QString str = "Hello";
        QCOMPARE(str.length(), 5);
    }

    void testBool() {
        QVERIFY(true);
        QVERIFY(!false);
    }
};

#include "test_simple.moc"
QTEST_APPLESS_MAIN(TestSimple)

