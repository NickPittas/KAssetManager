#include <QtTest/QtTest>

#include "everything_search.h"

class TestEverythingSearch : public QObject {
    Q_OBJECT

private slots:
    void initializeUnavailableOnNonWindows();
};

void TestEverythingSearch::initializeUnavailableOnNonWindows()
{
#ifndef _WIN32
    EverythingSearch &search = EverythingSearch::instance();
    QVERIFY(!search.initialize());
    QVERIFY(!search.isAvailable());
    QVERIFY(search.search(QStringLiteral("plate"), 10).isEmpty());
#endif
}

QTEST_APPLESS_MAIN(TestEverythingSearch)
#include "test_everything_search.moc"
