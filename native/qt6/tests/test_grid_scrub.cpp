#include <QtTest/QtTest>

#include "grid_scrub.h"

class TestGridScrub : public QObject {
    Q_OBJECT

private slots:
    void disablesMouseGrabOnWayland();
    void keepsMouseGrabOnOtherSessions();
};

void TestGridScrub::disablesMouseGrabOnWayland()
{
    QVERIFY(!GridScrubController::shouldGrabMouseForSessionType(QStringLiteral("wayland")));
}

void TestGridScrub::keepsMouseGrabOnOtherSessions()
{
    QVERIFY(GridScrubController::shouldGrabMouseForSessionType(QStringLiteral("x11")));
    QVERIFY(GridScrubController::shouldGrabMouseForSessionType(QString()));
}

QTEST_APPLESS_MAIN(TestGridScrub)
#include "test_grid_scrub.moc"
