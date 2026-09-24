#include <QApplication>
#include <QtTest/QtTest>

#include "icon_utils.h"

class TestIconUtils : public QObject {
    Q_OBJECT

private slots:
    void findsRepoIconsFromBuildTree();
    void findsAnnotationIconsFromBuildTree();
};

void TestIconUtils::findsRepoIconsFromBuildTree()
{
    const QIcon back = loadPngIcon(QStringLiteral("Back.png"));
    QVERIFY(!back.isNull());

    const QIcon play = loadPngIcon(QStringLiteral("media/Play.png"));
    QVERIFY(!play.isNull());
}

void TestIconUtils::findsAnnotationIconsFromBuildTree()
{
    QVERIFY(!findIconPath(QStringLiteral("Annotation/Annotate.png")).isEmpty());

    const QIcon annotate = loadRawPngIcon(QStringLiteral("Annotation/Annotate.png"));
    QVERIFY(!annotate.isNull());

    const QIcon saveAll = loadRawPngIcon(QStringLiteral("Annotation/save all.png"));
    QVERIFY(!saveAll.isNull());
}

QTEST_MAIN(TestIconUtils)
#include "test_icon_utils.moc"
