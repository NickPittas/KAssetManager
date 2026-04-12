#include "icon_utils.h"
#include <QApplication>
#include <QStyle>
#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include <QImage>
#include <QPixmap>
#include <QPainterPath>
#include <QScreen>
#include <QGuiApplication>
#include <QHash>
#include <QDir>
#include <QFileInfo>

// Icon generation helpers
// Creates DPI-aware icons that render crisply on High DPI displays
QIcon mkIcon(const std::function<void(QPainter&, const QRectF&)>& draw, const QColor& color)
{
    // Get device pixel ratio for High DPI support
    qreal dpr = 1.0;
    if (QGuiApplication::primaryScreen()) {
        dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
    }

    // Create pixmap at physical pixel size for crisp rendering
    int logicalSize = 32;
    int physicalSize = static_cast<int>(logicalSize * dpr);

    QPixmap pm(physicalSize, physicalSize);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    // Use logical coordinates - Qt will handle the scaling
    QRectF r(4, 4, 24, 24);
    QPen pen(color);
    pen.setWidthF(2.0);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    draw(p, r);
    p.end();

    return QIcon(pm);
}

QString findIconPath(const QString& filename)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString baseName = QFileInfo(filename).fileName();
    const QString normalized = filename.startsWith(QStringLiteral("media/"), Qt::CaseInsensitive)
        ? QStringLiteral("Media/") + filename.mid(6)
        : filename;
    const QString lowerNormalized = normalized.toLower();

    QStringList searchPaths = {
        appDir + "/Icons/" + normalized,
        appDir + "/../Icons/" + normalized,
        appDir + "/../../Icons/" + normalized,
        appDir + "/../../../Icons/" + normalized,
        appDir + "/icons/" + lowerNormalized,
        appDir + "/../icons/" + lowerNormalized,
        appDir + "/../../icons/" + lowerNormalized,
        appDir + "/../../../icons/" + lowerNormalized,
        appDir + "/Icons/Media/" + baseName,
        appDir + "/../Icons/Media/" + baseName,
        appDir + "/../../Icons/Media/" + baseName,
        appDir + "/../../../Icons/Media/" + baseName,
        appDir + "/Icons/Annotation/" + baseName,
        appDir + "/../Icons/Annotation/" + baseName,
        appDir + "/../../Icons/Annotation/" + baseName,
        appDir + "/../../../Icons/Annotation/" + baseName,
        appDir + "/../Resources/icons/" + normalized,
        appDir + "/../../Resources/icons/" + normalized,
        appDir + "/../../../Resources/icons/" + normalized,
        QDir::currentPath() + "/Icons/" + normalized,
        QDir::currentPath() + "/Icons/Media/" + baseName,
        QDir::currentPath() + "/Icons/Annotation/" + baseName,
        QDir::currentPath() + "/../Icons/" + normalized,
        QDir::currentPath() + "/../Icons/Media/" + baseName,
        QDir::currentPath() + "/../Icons/Annotation/" + baseName,
        QDir::currentPath() + "/../../Icons/" + normalized,
        QDir::currentPath() + "/../../Icons/Media/" + baseName,
        QDir::currentPath() + "/../../Icons/Annotation/" + baseName,
        QDir::currentPath() + "/../../../Icons/" + normalized,
        QDir::currentPath() + "/../../../Icons/Media/" + baseName,
        QDir::currentPath() + "/../../../Icons/Annotation/" + baseName
    };

    for (const QString& path : searchPaths) {
        if (QFile::exists(path)) {
            return path;
        }
    }

    return QString();
}

QIcon loadRawPngIcon(const QString& filename)
{
    const QString foundPath = findIconPath(filename);
    if (foundPath.isEmpty()) {
        qWarning() << "Failed to find raw icon:" << filename;
        return QIcon();
    }

    return QIcon(foundPath);
}

QIcon loadPngIcon(const QString& filename, const QColor& targetColor)
{
    const QString foundPath = findIconPath(filename);

    if (foundPath.isEmpty()) {
        qWarning() << "Failed to find icon:" << filename;
        return QIcon();
    }

    // Get device pixel ratio for High DPI support
    qreal dpr = 1.0;
    if (QGuiApplication::primaryScreen()) {
        dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
    }

    // Load the image at high resolution for crisp High DPI rendering
    QPixmap pixmap(foundPath);
    if (pixmap.isNull()) {
        qWarning() << "Failed to load icon pixmap:" << foundPath;
        return QIcon();
    }

    // Scale to 32x32 logical pixels (physical pixels = 32 * dpr) for toolbar icons
    // Use higher resolution for High DPI displays
    int logicalSize = 32;
    int physicalSize = static_cast<int>(logicalSize * dpr);

    if (pixmap.width() != physicalSize || pixmap.height() != physicalSize) {
        pixmap = pixmap.scaled(physicalSize, physicalSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    pixmap.setDevicePixelRatio(dpr);

    // Recolor dark pixels to target color
    QImage img = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            QColor pixel = img.pixelColor(x, y);
            // If pixel is dark (closer to black), convert to target color while preserving alpha
            if (pixel.alpha() > 0) {
                // Calculate brightness (0-255)
                int brightness = (pixel.red() + pixel.green() + pixel.blue()) / 3;
                // If it's dark (less than 128), recolor to target color
                if (brightness < 128) {
                    img.setPixelColor(x, y, QColor(targetColor.red(), targetColor.green(), targetColor.blue(), pixel.alpha()));
                }
            }
        }
    }
    img.setDevicePixelRatio(dpr);
    pixmap = QPixmap::fromImage(img);
    pixmap.setDevicePixelRatio(dpr);

    // Create QIcon and explicitly set pixmaps for all states to prevent Qt from auto-generating grey disabled versions
    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Normal, QIcon::On);
    icon.addPixmap(pixmap, QIcon::Active, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Active, QIcon::On);
    // For disabled state, use the same pixmap to prevent grey-out (user can see button is disabled by other means)
    icon.addPixmap(pixmap, QIcon::Disabled, QIcon::Off);
    icon.addPixmap(pixmap, QIcon::Disabled, QIcon::On);

    return icon;
}

// General icons
QIcon icoFolderNew(const QColor& color) { return loadPngIcon("Add to library2.png", color); }
QIcon icoCopy(const QColor& color) { return loadPngIcon("Copy.png", color); }
QIcon icoCut(const QColor& color) { return loadPngIcon("Cut.png", color); }
QIcon icoPaste(const QColor& color) { return loadPngIcon("Paste.png", color); }
QIcon icoDelete(const QColor& color) { return loadPngIcon("Delete.png", color); }
QIcon icoRename(const QColor& color) { return loadPngIcon("Rename.png", color); }
QIcon icoAdd(const QColor& color) { return loadPngIcon("Add to Library1.png", color); }
QIcon icoGrid(const QColor& color) { return loadPngIcon("Grid View.png", color); }
QIcon icoList(const QColor& color) { return loadPngIcon("List View.png", color); }
QIcon icoGroup(const QColor& color) { return loadPngIcon("Group Sequences.png", color); }
QIcon icoEye(const QColor& color) { return mkIcon([](QPainter& p, const QRectF& r){
    QPainterPath path; QPointF c = r.center(); qreal rx = r.width()/2 - 2; qreal ry = r.height()/3;
    path.moveTo(r.x()+2, c.y());
    path.cubicTo(r.x()+rx/2, r.y()+2, r.right()-rx/2, r.y()+2, r.right()-2, c.y());
    path.cubicTo(r.right()-rx/2, r.bottom()-2, r.x()+rx/2, r.bottom()-2, r.x()+2, c.y());
    p.drawPath(path); p.drawEllipse(QRectF(c.x()-3, c.y()-3, 6, 6));
}, color); }
QIcon icoBack(const QColor& color) { return loadPngIcon("Back.png", color); }
QIcon icoUp(const QColor& color) { return loadPngIcon("Up.png", color); }
QIcon icoRefresh(const QColor& color) { return loadPngIcon("Refresh.png", color); }
QIcon icoHide(const QColor& color) { return loadPngIcon("Hide.png", color); }
QIcon icoSearch(const QColor& color) { return loadPngIcon("Search.png", color); }
QIcon icoDualPane(const QColor& color) { return loadPngIcon("DualPane.png", color); }

// Media icons
QIcon icoMediaPlay(const QColor& color) { return loadPngIcon("media/Play.png", color); }
QIcon icoMediaPause(const QColor& color) { return loadPngIcon("media/Pause.png", color); }
QIcon icoMediaStop(const QColor& color) { return loadPngIcon("media/Stop.png", color); }
QIcon icoMediaNextFrame(const QColor& color) { return loadPngIcon("media/Next Frame.png", color); }
QIcon icoMediaPrevFrame(const QColor& color) { return loadPngIcon("media/Previous Frame.png", color); }
QIcon icoMediaAudio(const QColor& color) { return loadPngIcon("media/Audio.png", color); }
QIcon icoMediaNoAudio(const QColor& color) { return loadPngIcon("media/No Audio.png", color); }
QIcon icoMediaMute(const QColor& color) { return loadPngIcon("media/Mute.png", color); }

// File type icons
QIcon icoFilePdf(const QColor& color) { return mkIcon([](QPainter& p, const QRectF& r){
    // PDF document icon - page with folded corner
    QPainterPath page;
    page.moveTo(r.x()+4, r.y()+2);
    page.lineTo(r.right()-6, r.y()+2);
    page.lineTo(r.right()-2, r.y()+6);
    page.lineTo(r.right()-2, r.bottom()-2);
    page.lineTo(r.x()+4, r.bottom()-2);
    page.closeSubpath();
    p.drawPath(page);
    // Folded corner
    p.drawLine(QPointF(r.right()-6, r.y()+2), QPointF(r.right()-6, r.y()+6));
    p.drawLine(QPointF(r.right()-6, r.y()+6), QPointF(r.right()-2, r.y()+6));
    // PDF text
    QFont f = p.font(); f.setPixelSize(6); f.setBold(true); p.setFont(f);
    p.drawText(QRectF(r.x()+4, r.center().y()-2, r.width()-8, 8), Qt::AlignCenter, "PDF");
}, color); }

QIcon icoFileCsv(const QColor& color) { return mkIcon([](QPainter& p, const QRectF& r){
    // Spreadsheet grid icon
    QRectF sheet(r.x()+3, r.y()+4, r.width()-6, r.height()-8);
    p.drawRect(sheet);
    // Grid lines
    qreal cellH = sheet.height() / 3;
    qreal cellW = sheet.width() / 3;
    for (int i = 1; i < 3; i++) {
        p.drawLine(QPointF(sheet.x(), sheet.y() + i*cellH), QPointF(sheet.right(), sheet.y() + i*cellH));
        p.drawLine(QPointF(sheet.x() + i*cellW, sheet.y()), QPointF(sheet.x() + i*cellW, sheet.bottom()));
    }
}, color); }

QIcon icoFileDoc(const QColor& color) { return mkIcon([](QPainter& p, const QRectF& r){
    // Word document icon - page with lines
    QPainterPath page;
    page.moveTo(r.x()+4, r.y()+2);
    page.lineTo(r.right()-6, r.y()+2);
    page.lineTo(r.right()-2, r.y()+6);
    page.lineTo(r.right()-2, r.bottom()-2);
    page.lineTo(r.x()+4, r.bottom()-2);
    page.closeSubpath();
    p.drawPath(page);
    // Folded corner
    p.drawLine(QPointF(r.right()-6, r.y()+2), QPointF(r.right()-6, r.y()+6));
    p.drawLine(QPointF(r.right()-6, r.y()+6), QPointF(r.right()-2, r.y()+6));
    // Text lines
    for (int i = 0; i < 3; i++) {
        qreal y = r.y() + 10 + i*4;
        p.drawLine(QPointF(r.x()+6, y), QPointF(r.right()-4, y));
    }
}, color); }

QIcon icoFileXls(const QColor& color) { return mkIcon([](QPainter& p, const QRectF& r){
    // Excel spreadsheet icon - page with grid
    QPainterPath page;
    page.moveTo(r.x()+4, r.y()+2);
    page.lineTo(r.right()-6, r.y()+2);
    page.lineTo(r.right()-2, r.y()+6);
    page.lineTo(r.right()-2, r.bottom()-2);
    page.lineTo(r.x()+4, r.bottom()-2);
    page.closeSubpath();
    p.drawPath(page);
    // Folded corner
    p.drawLine(QPointF(r.right()-6, r.y()+2), QPointF(r.right()-6, r.y()+6));
    p.drawLine(QPointF(r.right()-6, r.y()+6), QPointF(r.right()-2, r.y()+6));
    // Small grid
    QRectF grid(r.x()+6, r.y()+10, r.width()-12, r.height()-16);
    p.drawRect(grid);
    p.drawLine(QPointF(grid.center().x(), grid.y()), QPointF(grid.center().x(), grid.bottom()));
    p.drawLine(QPointF(grid.x(), grid.center().y()), QPointF(grid.right(), grid.center().y()));
}, color); }

QIcon icoFileTxt(const QColor& color) { return mkIcon([](QPainter& p, const QRectF& r){
    // Text file icon - simple page
    QPainterPath page;
    page.moveTo(r.x()+5, r.y()+3);
    page.lineTo(r.right()-5, r.y()+3);
    page.lineTo(r.right()-5, r.bottom()-3);
    page.lineTo(r.x()+5, r.bottom()-3);
    page.closeSubpath();
    p.drawPath(page);
    // Text lines
    for (int i = 0; i < 4; i++) {
        qreal y = r.y() + 7 + i*4;
        p.drawLine(QPointF(r.x()+7, y), QPointF(r.right()-7, y));
    }
}, color); }

QIcon icoFileAi(const QColor& color) { return mkIcon([](QPainter& p, const QRectF& r){
    // Adobe Illustrator icon - page with vector path
    QPainterPath page;
    page.moveTo(r.x()+4, r.y()+2);
    page.lineTo(r.right()-6, r.y()+2);
    page.lineTo(r.right()-2, r.y()+6);
    page.lineTo(r.right()-2, r.bottom()-2);
    page.lineTo(r.x()+4, r.bottom()-2);
    page.closeSubpath();
    p.drawPath(page);
    // Folded corner
    p.drawLine(QPointF(r.right()-6, r.y()+2), QPointF(r.right()-6, r.y()+6));
    p.drawLine(QPointF(r.right()-6, r.y()+6), QPointF(r.right()-2, r.y()+6));
    // Bezier curve
    QPainterPath curve;
    curve.moveTo(r.x()+6, r.center().y()+2);
    curve.cubicTo(r.x()+8, r.y()+10, r.right()-8, r.y()+10, r.right()-6, r.center().y()+2);
    p.drawPath(curve);
}, color); }

QIcon icoFileGeneric(const QColor& color) { return mkIcon([](QPainter& p, const QRectF& r){
    // Generic file icon - simple page
    QPainterPath page;
    page.moveTo(r.x()+5, r.y()+3);
    page.lineTo(r.right()-7, r.y()+3);
    page.lineTo(r.right()-3, r.y()+7);
    page.lineTo(r.right()-3, r.bottom()-3);
    page.lineTo(r.x()+5, r.bottom()-3);
    page.closeSubpath();
    p.drawPath(page);
    // Folded corner
    p.drawLine(QPointF(r.right()-7, r.y()+3), QPointF(r.right()-7, r.y()+7));
    p.drawLine(QPointF(r.right()-7, r.y()+7), QPointF(r.right()-3, r.y()+7));
}, color); }

QIcon getFileTypeIcon(const QString &ext, const QColor& color) {
    // Cache icons by extension+color to avoid regenerating on every paint
    // This is critical for resize performance - icons were being regenerated thousands of times
    static QHash<QPair<QString, QRgb>, QIcon> iconCache;
    
    QString lower = ext.toLower();
    QRgb colorKey = color.rgba();
    auto cacheKey = qMakePair(lower, colorKey);
    
    auto it = iconCache.find(cacheKey);
    if (it != iconCache.end()) {
        return it.value();
    }
    
    QIcon icon;
    if (lower == "pdf") icon = icoFilePdf(color);
    else if (lower == "csv") icon = icoFileCsv(color);
    else if (lower == "doc" || lower == "docx") icon = icoFileDoc(color);
    else if (lower == "xls" || lower == "xlsx") icon = icoFileXls(color);
    else if (lower == "txt" || lower == "log" || lower == "md") icon = icoFileTxt(color);
    else if (lower == "ai" || lower == "eps") icon = icoFileAi(color);
    else icon = icoFileGeneric(color);
    
    iconCache.insert(cacheKey, icon);
    return icon;
}

// Icon providers

// Cached standard icons - standardIcon() is expensive (shell calls on Windows)
namespace {
    static QIcon& cachedDirIcon() {
        static QIcon icon;
        if (icon.isNull()) icon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
        return icon;
    }
    static QIcon& cachedDriveIcon() {
        static QIcon icon;
        if (icon.isNull()) icon = QApplication::style()->standardIcon(QStyle::SP_DriveHDIcon);
        return icon;
    }
    static QIcon& cachedComputerIcon() {
        static QIcon icon;
        if (icon.isNull()) icon = QApplication::style()->standardIcon(QStyle::SP_ComputerIcon);
        return icon;
    }
    static QIcon& cachedFileIcon() {
        static QIcon icon;
        if (icon.isNull()) icon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
        return icon;
    }
}

QIcon FmTreeIconProvider::icon(IconType type) const {
    switch (type) {
    case QFileIconProvider::Folder:
        return cachedDirIcon();
    case QFileIconProvider::Drive:
        return cachedDriveIcon();
    case QFileIconProvider::Computer:
        return cachedComputerIcon();
    default:
        return cachedFileIcon();
    }
}

QIcon FmTreeIconProvider::icon(const QFileInfo &info) const {
    if (info.isDir()) {
        if (info.isRoot()) {
            return cachedDriveIcon();
        }
        return cachedDirIcon();
    }
    return cachedFileIcon();
}

QIcon FmIconProvider::icon(const QFileInfo &info) const {
    // Always keep folders lightweight and immediate
    if (info.isDir()) {
        return cachedDirIcon();
    }

    const QString suffix = info.suffix().toLower();
    return getFileTypeIcon(suffix);
}
