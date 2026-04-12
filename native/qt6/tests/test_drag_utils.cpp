#include <QtTest/QtTest>

#include "drag_utils.h"

class TestDragUtils : public QObject {
    Q_OBJECT

private slots:
    void emptyFileDragReturnsFalse();
};

void TestDragUtils::emptyFileDragReturnsFalse()
{
    DragUtils dragUtils;
    QVERIFY(!dragUtils.startFileDrag({}));
}

QTEST_MAIN(TestDragUtils)
#include "test_drag_utils.moc"
