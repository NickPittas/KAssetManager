#include <QtTest/QtTest>

#include "annotation_items.h"

class TestAnnotationItems : public QObject {
    Q_OBJECT

private slots:
    void serializesMovedTextAtScenePosition();
    void serializesMovedRectangleAtScenePosition();
    void serializesMovedArrowAtScenePosition();
    void roundTripsMovedFreehandPoints();
};

void TestAnnotationItems::serializesMovedTextAtScenePosition()
{
    TextAnnotation item(QStringLiteral("Test"), QPointF(10.0, 20.0));
    item.setPos(QPointF(35.0, 45.0));

    const QJsonObject json = item.toJson();
    QCOMPARE(json.value(QStringLiteral("x")).toDouble(), 35.0);
    QCOMPARE(json.value(QStringLiteral("y")).toDouble(), 45.0);
}

void TestAnnotationItems::serializesMovedRectangleAtScenePosition()
{
    RectangleAnnotation item(QRectF(5.0, 6.0, 20.0, 30.0));
    item.setPos(QPointF(40.0, 50.0));

    const QJsonObject json = item.toJson();
    QCOMPARE(json.value(QStringLiteral("x")).toDouble(), 45.0);
    QCOMPARE(json.value(QStringLiteral("y")).toDouble(), 56.0);
    QCOMPARE(json.value(QStringLiteral("width")).toDouble(), 20.0);
    QCOMPARE(json.value(QStringLiteral("height")).toDouble(), 30.0);
}

void TestAnnotationItems::serializesMovedArrowAtScenePosition()
{
    ArrowAnnotation item(QLineF(1.0, 2.0, 11.0, 12.0));
    item.setPos(QPointF(7.0, 9.0));

    const QJsonObject json = item.toJson();
    QCOMPARE(json.value(QStringLiteral("x1")).toDouble(), 8.0);
    QCOMPARE(json.value(QStringLiteral("y1")).toDouble(), 11.0);
    QCOMPARE(json.value(QStringLiteral("x2")).toDouble(), 18.0);
    QCOMPARE(json.value(QStringLiteral("y2")).toDouble(), 21.0);
}

void TestAnnotationItems::roundTripsMovedFreehandPoints()
{
    FreehandAnnotation item;
    item.addPoint(QPointF(1.0, 2.0));
    item.addPoint(QPointF(3.0, 4.0));
    item.setPos(QPointF(10.0, 20.0));

    const QJsonObject json = item.toJson();
    AnnotationItem* restored = AnnotationItem::fromJson(json);
    QVERIFY(restored != nullptr);

    auto* freehand = dynamic_cast<FreehandAnnotation*>(restored);
    QVERIFY(freehand != nullptr);

    const QPainterPath path = freehand->path();
    QCOMPARE(path.elementCount(), 2);
    QCOMPARE(path.elementAt(0).x, 11.0);
    QCOMPARE(path.elementAt(0).y, 22.0);
    QCOMPARE(path.elementAt(1).x, 13.0);
    QCOMPARE(path.elementAt(1).y, 24.0);

    delete restored;
}

QTEST_MAIN(TestAnnotationItems)
#include "test_annotation_items.moc"
