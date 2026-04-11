#include <QtTest/QtTest>

#include "platform_session.h"

class TestPlatformSession : public QObject {
    Q_OBJECT

private slots:
    void detectsWaylandFromPlatformName();
    void detectsWaylandFromSessionType();
    void ignoresNonWaylandSessions();
    void doesNotForceRasterWidgetsOnWayland();
    void usesRasterPreviewFallbackOnWayland();
};

void TestPlatformSession::detectsWaylandFromPlatformName()
{
    QVERIFY(PlatformSession::isWayland(QStringLiteral("wayland"), QString()));
    QVERIFY(PlatformSession::isWayland(QStringLiteral("wayland-egl"), QString()));
}

void TestPlatformSession::detectsWaylandFromSessionType()
{
    QVERIFY(PlatformSession::isWayland(QString(), QStringLiteral("wayland")));
}

void TestPlatformSession::ignoresNonWaylandSessions()
{
    QVERIFY(!PlatformSession::isWayland(QStringLiteral("xcb"), QStringLiteral("x11")));
    QVERIFY(!PlatformSession::isWayland(QString(), QString()));
}

void TestPlatformSession::doesNotForceRasterWidgetsOnWayland()
{
    QVERIFY(!PlatformSession::shouldForceRasterWidgetsOnWayland());
}

void TestPlatformSession::usesRasterPreviewFallbackOnWayland()
{
    QVERIFY(PlatformSession::shouldUseRasterPreviewFallbackOnWayland());
}

QTEST_APPLESS_MAIN(TestPlatformSession)
#include "test_platform_session.moc"
