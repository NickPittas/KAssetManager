#include "ui/icon_helpers.h"

#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRectF>
#include <QDebug>
#include <QColor>
#include <QStringList>

#include <functional>

namespace {

QIcon mkIcon(const std::function<void(QPainter&, const QRectF&)>& draw)
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r(4, 4, 24, 24);
    QPen pen(QColor(235, 235, 235));
    pen.setWidthF(2.0);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    draw(p, r);
    p.end();
    return QIcon(pm);
}

QIcon loadPngIcon(const QString& filename, bool recolorToWhite = true)
{
    QStringList searchPaths = {
        QCoreApplication::applicationDirPath() + "/icons/" + filename,
        QCoreApplication::applicationDirPath() + "/../icons/" + filename,
        QCoreApplication::applicationDirPath() + "/../../icons/" + filename
    };

    QString foundPath;
    for (const QString& path : searchPaths) {
        if (QFile::exists(path)) {
            foundPath = path;
            break;
        }
    }

    if (foundPath.isEmpty()) {
        qWarning() << "Failed to find icon:" << filename << "- searched paths:" << searchPaths;
        return QIcon();
    }

    QPixmap pixmap(foundPath);
    if (pixmap.isNull()) {
        qWarning() << "Failed to load icon pixmap:" << foundPath;
        return QIcon();
    }

    if (pixmap.width() != 32 || pixmap.height() != 32) {
        pixmap = pixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (recolorToWhite) {
        QImage img = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < img.height(); ++y) {
            for (int x = 0; x < img.width(); ++x) {
                QColor pixel = img.pixelColor(x, y);
                if (pixel.alpha() > 0) {
                    int brightness = (pixel.red() + pixel.green() + pixel.blue()) / 3;
                    if (brightness < 128) {
                        img.setPixelColor(x, y, QColor(255, 255, 255, pixel.alpha()));
                    }
                }
            }
        }
        pixmap = QPixmap::fromImage(img);
    }

    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Active, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Active, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Disabled, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Disabled, QIcon::On);
    return icon;
}

} // namespace

QIcon icoFolderNew() { return loadPngIcon("Add to library2.png"); }
QIcon icoCopy() { return loadPngIcon("Copy.png"); }
QIcon icoCut() { return loadPngIcon("Cut.png"); }
QIcon icoPaste() { return loadPngIcon("Paste.png"); }
QIcon icoDelete() { return loadPngIcon("Delete.png"); }
QIcon icoRename() { return loadPngIcon("Rename.png"); }
QIcon icoAdd() { return loadPngIcon("Add to Library1.png"); }
QIcon icoGrid() { return loadPngIcon("Grid View.png"); }
QIcon icoList() { return loadPngIcon("List View.png"); }
QIcon icoGroup() { return loadPngIcon("Group Sequences.png"); }

QIcon icoEye()
{
    return mkIcon([](QPainter& p, const QRectF& r) {
        QPainterPath eye;
        QPointF left(r.left(), r.center().y());
        QPointF top(r.center().x(), r.top());
        QPointF right(r.right(), r.center().y());
        QPointF bottom(r.center().x(), r.bottom());
        eye.moveTo(top);
        eye.quadTo(right, bottom);
        eye.quadTo(left, top);
        p.drawPath(eye);
        p.setBrush(QColor(235, 235, 235, 80));
        p.drawEllipse(r.center(), r.width() / 6.0, r.height() / 6.0);
    });
}

QIcon icoBack() { return loadPngIcon("Back.png"); }
QIcon icoUp() { return loadPngIcon("Up.png"); }
QIcon icoRefresh() { return loadPngIcon("Refresh.png"); }
QIcon icoHide() { return loadPngIcon("Hide.png"); }
QIcon icoSearch() { return loadPngIcon("Search.png"); }

QIcon icoMediaPlay() { return loadPngIcon("media/Play.png"); }
QIcon icoMediaPause() { return loadPngIcon("media/Pause.png"); }
QIcon icoMediaStop() { return loadPngIcon("media/Stop.png"); }
QIcon icoMediaNextFrame() { return loadPngIcon("media/Next Frame.png"); }
QIcon icoMediaPrevFrame() { return loadPngIcon("media/Previous Frame.png"); }
QIcon icoMediaAudio() { return loadPngIcon("media/Audio.png"); }
QIcon icoMediaNoAudio() { return loadPngIcon("media/No Audio.png"); }
QIcon icoMediaMute() { return loadPngIcon("media/Mute.png"); }

QIcon icoFilePdf()
{
    return mkIcon([](QPainter& p, const QRectF& r) {
        QPainterPath page;
        page.moveTo(r.x()+4, r.y()+2);
        page.lineTo(r.right()-6, r.y()+2);
        page.lineTo(r.right()-2, r.y()+6);
        page.lineTo(r.right()-2, r.bottom()-2);
        page.lineTo(r.x()+4, r.bottom()-2);
        page.closeSubpath();
        p.drawPath(page);
        p.drawLine(QPointF(r.right()-6, r.y()+2), QPointF(r.right()-6, r.y()+6));
        p.drawLine(QPointF(r.right()-6, r.y()+6), QPointF(r.right()-2, r.y()+6));
        p.setBrush(QColor("#ff3d00"));
        p.drawRect(QRectF(r.x()+6, r.center().y()-4, r.width()-12, 8));
    });
}

QIcon icoFileCsv()
{
    return mkIcon([](QPainter& p, const QRectF& r) {
        QPainterPath grid;
        grid.addRoundedRect(r, 3, 3);
        p.drawPath(grid);
        for (int i = 1; i < 4; ++i) {
            qreal x = r.x() + i * (r.width()/4);
            p.drawLine(QPointF(x, r.y()+2), QPointF(x, r.bottom()-2));
        }
        for (int j = 1; j < 4; ++j) {
            qreal y = r.y() + j * (r.height()/4);
            p.drawLine(QPointF(r.x()+2, y), QPointF(r.right()-2, y));
        }
    });
}

QIcon icoFileDoc()
{
    return mkIcon([](QPainter& p, const QRectF& r) {
        QPainterPath page;
        page.addRoundedRect(r, 3, 3);
        p.drawPath(page);
        for (int i = 0; i < 4; ++i) {
            qreal y = r.y() + 6 + i * 4;
            p.drawLine(QPointF(r.x()+4, y), QPointF(r.right()-4, y));
        }
    });
}

QIcon icoFileXls()
{
    return mkIcon([](QPainter& p, const QRectF& r) {
        QPainterPath page;
        page.addRoundedRect(r, 3, 3);
        p.drawPath(page);
        QPainterPath x;
        x.moveTo(r.x()+6, r.y()+6);
        x.lineTo(r.right()-6, r.bottom()-6);
        x.moveTo(r.x()+6, r.bottom()-6);
        x.lineTo(r.right()-6, r.y()+6);
        p.drawPath(x);
    });
}

QIcon icoFileTxt()
{
    return mkIcon([](QPainter& p, const QRectF& r) {
        QPainterPath page;
        page.addRoundedRect(r, 3, 3);
        p.drawPath(page);
        for (int i = 0; i < 5; ++i) {
            qreal y = r.y() + 4 + i * 4;
            p.drawLine(QPointF(r.x()+4, y), QPointF(r.right()-4, y));
        }
    });
}

QIcon icoFileAi()
{
    return mkIcon([](QPainter& p, const QRectF& r) {
        QPainterPath page;
        page.moveTo(r.x()+4, r.y()+2);
        page.lineTo(r.right()-6, r.y()+2);
        page.lineTo(r.right()-2, r.y()+6);
        page.lineTo(r.right()-2, r.bottom()-2);
        page.lineTo(r.x()+4, r.bottom()-2);
        page.closeSubpath();
        p.drawPath(page);
        p.drawLine(QPointF(r.right()-6, r.y()+2), QPointF(r.right()-6, r.y()+6));
        p.drawLine(QPointF(r.right()-6, r.y()+6), QPointF(r.right()-2, r.y()+6));
        QPainterPath curve;
        curve.moveTo(r.x()+6, r.center().y()+2);
        curve.cubicTo(r.x()+8, r.y()+10, r.right()-8, r.y()+10, r.right()-6, r.center().y()+2);
        p.drawPath(curve);
    });
}

QIcon icoFileGeneric()
{
    return mkIcon([](QPainter& p, const QRectF& r) {
        QPainterPath page;
        page.moveTo(r.x()+5, r.y()+3);
        page.lineTo(r.right()-7, r.y()+3);
        page.lineTo(r.right()-3, r.y()+7);
        page.lineTo(r.right()-3, r.bottom()-3);
        page.lineTo(r.x()+5, r.bottom()-3);
        page.closeSubpath();
        p.drawPath(page);
        p.drawLine(QPointF(r.right()-7, r.y()+3), QPointF(r.right()-7, r.y()+7));
        p.drawLine(QPointF(r.right()-7, r.y()+7), QPointF(r.right()-3, r.y()+7));
    });
}

QIcon getFileTypeIcon(const QString& ext)
{
    const QString lower = ext.toLower();
    if (lower == "pdf") return icoFilePdf();
    if (lower == "csv") return icoFileCsv();
    if (lower == "doc" || lower == "docx") return icoFileDoc();
    if (lower == "xls" || lower == "xlsx") return icoFileXls();
    if (lower == "txt" || lower == "log" || lower == "md") return icoFileTxt();
    if (lower == "ai" || lower == "eps") return icoFileAi();
    return icoFileGeneric();
}
