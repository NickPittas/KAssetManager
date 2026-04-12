#include <QtTest/QtTest>

#include "virtual_drag.h"

class TestVirtualDrag : public QObject {
    Q_OBJECT

private slots:
    void nonWindowsStubsReturnFalse();
};

void TestVirtualDrag::nonWindowsStubsReturnFalse()
{
#ifdef Q_OS_WIN
    QSKIP("Linux/non-Windows stub behavior test");
#else
    const QVector<VirtualDrag::VirtualFile> files{
        VirtualDrag::VirtualFile{QStringLiteral("note.txt"), QByteArray("hello")}
    };

    QVERIFY(!VirtualDrag::startVirtualDrag(files));
    QVERIFY(!VirtualDrag::startRealPathsDrag({QStringLiteral("/tmp/file.txt")}));
    QVERIFY(!VirtualDrag::startAdaptivePathsDrag(
        {QStringLiteral("/tmp/frames/shot.1001.exr")},
        {QStringLiteral("/tmp/frames")}));
#endif
}

QTEST_MAIN(TestVirtualDrag)
#include "test_virtual_drag.moc"
