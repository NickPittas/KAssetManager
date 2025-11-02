#include "mainwindow.h"
#include "virtual_folders.h"
#include "assets_model.h"
#include "assets_table_model.h"
#include "tags_model.h"
#include "importer.h"
#include "db.h"
#include "preview_overlay.h"
#include "oiio_image_loader.h"
#include "live_preview_manager.h"
#include "import_progress_dialog.h"
#include "settings_dialog.h"
#include "star_rating_widget.h"
#include "project_folder_watcher.h"
#include "log_viewer_widget.h"
#include "progress_manager.h"
#include "file_ops.h"
#include "file_ops_dialog.h"
#include "log_manager.h"
#include "sequence_detector.h"
#include "context_preserver.h"
#include "database_health_agent.h"
#include "database_health_dialog.h"

#include "office_preview.h"


#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QApplication>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenuBar>
#include <QSlider>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QItemSelectionModel>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <cmath>
#include <limits>
#include <QProgressDialog>

#include "file_utils.h"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
static size_t currentWorkingSetMB() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
        return static_cast<size_t>(pmc.WorkingSetSize / (1024ULL * 1024ULL));
    return 0;
}
#endif

#include <QProgressBar>
#include <QDirIterator>
#include <QStatusBar>
#include <QDrag>
#include <QMouseEvent>
#include <QFuture>
#include <functional>
#include <algorithm>
#include <QMenu>
#include <QAction>

#include <QFutureWatcher>
#include <QTimer>
#include <QScrollBar>
#include <QTableView>
#include <QStackedWidget>
#include <QCheckBox>
#include <QFileDialog>
#include <QSortFilterProxyModel>
#include <QRegularExpression>
#include <QToolButton>

#include <QStandardPaths>
#include <QTextOption>

#include <QVector>


#include <QPlainTextEdit>
#include <algorithm>
#include <QStandardItemModel>
#include <QDebug>
#ifdef HAVE_QT_PDF
#include <QPdfDocument>
#endif
#ifdef HAVE_QT_PDF_WIDGETS
#include <QPdfView>
#endif
#include <QSvgRenderer>
#include <QGraphicsSvgItem>

#include <QImageReader>
#include <QThreadPool>
#include <QRunnable>

#include <QPointer>
#include <QHash>
#include <QSet>
#include <QCursor>

namespace {

QHash<QString, QString> g_lastPreviewError;

constexpr qreal kScrubDefaultPosition = 0.0;
constexpr int kPreviewInset = 8;

QRect insetPreviewRect(const QRect &source)
{
    QRect result = source.adjusted(kPreviewInset, kPreviewInset, -kPreviewInset, -kPreviewInset);
    if (result.width() <= 0 || result.height() <= 0) {
        return source;
    }
    return result;
}


bool isPreviewableSuffix(const QString& suffix)
{
    if (suffix.isEmpty()) {
        return false;
    }
    static const QSet<QString> kImageSuffixes = {
        "png", "jpg", "jpeg", "bmp", "tif", "tiff", "tga", "gif",
        "webp", "heic", "heif", "avif", "psd", "exr", "dpx"
    };
    static const QSet<QString> kVideoSuffixes = {
        "mov", "qt", "mp4", "m4v", "mxf", "mkv", "avi", "asf",
        "wmv", "webm", "mpg", "mpeg", "m2v", "m2ts", "mts",
        "ogv", "flv", "f4v", "3gp", "3g2", "y4m"
    };
    const QString lower = suffix.toLower();
    return kImageSuffixes.contains(lower) || kVideoSuffixes.contains(lower);
}

}


// Forward declare helper
// Lightweight icon painters (no external resources) for toolbar buttons
#include <functional>
static QIcon mkIcon(const std::function<void(QPainter&, const QRectF&)>& draw)
{
    QPixmap pm(24, 24); pm.fill(Qt::transparent);
    QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing);
    QRectF r(3, 3, 18, 18);
    QPen pen(QColor(235,235,235)); pen.setWidthF(1.6);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    draw(p, r);
    p.end();
    return QIcon(pm);
}

static QIcon icoFolderNew() { return mkIcon([](QPainter& p, const QRectF& r){
    QRectF body(r.x()+3, r.y()+6, r.width()-6, r.height()-9);
    p.drawRoundedRect(body, 2, 2);
    QRectF tab(r.x()+5, r.y()+3, r.width()*0.35, 6);
    p.drawRoundedRect(tab, 2, 2);
    QPointF c(r.right()-6, r.top()+8);
    p.drawLine(QPointF(c.x()-3, c.y()), QPointF(c.x()+3, c.y()));
    p.drawLine(QPointF(c.x(), c.y()-3), QPointF(c.x(), c.y()+3));
}); }

static QIcon icoCopy() { return mkIcon([](QPainter& p, const QRectF& r){
    QRectF a(r.x()+5, r.y()+6, r.width()-10, r.height()-10); p.drawRoundedRect(a,2,2);
    QRectF b = a.translated(-4,-4); p.drawRoundedRect(b,2,2);
}); }

static QIcon icoCut() { return mkIcon([](QPainter& p, const QRectF& r){
    p.drawLine(QPointF(r.left()+4, r.bottom()-6), QPointF(r.right()-4, r.top()+6));
    p.drawLine(QPointF(r.left()+4, r.top()+6), QPointF(r.right()-4, r.bottom()-6));
    QPointF c1(r.center().x()-3, r.center().y()-2), c2(r.center().x()+3, r.center().y()+2);
    p.drawEllipse(c1, 2.5, 2.5); p.drawEllipse(c2, 2.5, 2.5);
}); }

static QIcon icoPaste() { return mkIcon([](QPainter& p, const QRectF& r){
    QRectF clip(r.x()+5, r.y()+6, r.width()-10, r.height()-8); p.drawRoundedRect(clip,2,2);
    QRectF head(r.center().x()-6, r.y()+2, 12, 6); p.drawRoundedRect(head,2,2);
}); }

static QIcon icoDelete() { return mkIcon([](QPainter& p, const QRectF& r){
    QRectF bin(r.x()+6, r.y()+7, r.width()-12, r.height()-9); p.drawRoundedRect(bin,2,2);
    p.drawLine(QPointF(r.x()+4, r.y()+7), QPointF(r.right()-4, r.y()+7));
    QRectF lid(r.x()+8, r.y()+4, r.width()-16, 4); p.drawRoundedRect(lid,1,1);
}); }

static QIcon icoRename() { return mkIcon([](QPainter& p, const QRectF& r){
    QPainterPath pencil; QPointF a(r.x()+6, r.bottom()-7), b(r.right()-6, r.y()+7);
    QPointF c(b.x()-3, b.y()-3), d(a.x()+3, a.y()+3);
    pencil.moveTo(a); pencil.lineTo(c); pencil.lineTo(b); pencil.lineTo(d); pencil.closeSubpath();
    p.drawPath(pencil);
}); }

static QIcon icoAdd() { return mkIcon([](QPainter& p, const QRectF& r){
    QPointF c = r.center(); p.drawEllipse(QRectF(c.x()-7,c.y()-7,14,14));
    p.drawLine(QPointF(c.x()-4,c.y()), QPointF(c.x()+4,c.y()));
    p.drawLine(QPointF(c.x(), c.y()-4), QPointF(c.x(), c.y()+4));
}); }

static QIcon icoGrid() { return mkIcon([](QPainter& p, const QRectF& r){
    const qreal s = (r.width()-6)/3.0;
    for (int i=0;i<3;i++) for (int j=0;j<3;j++) {
        QRectF cell(r.x()+3+i*s, r.y()+3+j*s, s-2, s-2); p.drawRect(cell);
    }
}); }

static QIcon icoList() { return mkIcon([](QPainter& p, const QRectF& r){
    for (int i=0;i<3;i++) {
        qreal y = r.y()+4+i*6; p.drawRect(QRectF(r.x()+3, y, 3, 3));
        p.drawLine(QPointF(r.x()+10, y+1.5), QPointF(r.right()-3, y+1.5));
    }
}); }

static QIcon icoGroup() { return mkIcon([](QPainter& p, const QRectF& r){
    QRectF a(r.x()+4, r.center().y()-5, 10, 10);
    QRectF b(r.center().x()-1, r.center().y()-5, 10, 10);
    p.drawArc(a, 45*16, 270*16); p.drawArc(b, 225*16, 270*16);
}); }

static QIcon icoEye() { return mkIcon([](QPainter& p, const QRectF& r){
    QPainterPath path; QPointF c = r.center(); qreal rx = r.width()/2 - 2; qreal ry = r.height()/3;
    path.moveTo(r.x()+2, c.y());
    path.cubicTo(r.x()+rx/2, r.y()+2, r.right()-rx/2, r.y()+2, r.right()-2, c.y());
    path.cubicTo(r.right()-rx/2, r.bottom()-2, r.x()+rx/2, r.bottom()-2, r.x()+2, c.y());
    p.drawPath(path); p.drawEllipse(QRectF(c.x()-3, c.y()-3, 6, 6));
}); }


static QIcon icoRefresh() { return mkIcon([](QPainter& p, const QRectF& r){
    QPointF c = r.center(); qreal rad = r.width()/2 - 4;
    QPainterPath path; path.moveTo(c.x()+rad, c.y());
    path.arcTo(QRectF(c.x()-rad, c.y()-rad, 2*rad, 2*rad), 0, 270);
    p.drawPath(path);
    QPointF a(c.x()-rad, c.y()-rad+2);
    p.drawLine(QPointF(a.x(), a.y()), QPointF(a.x()-4, a.y()+2));
    p.drawLine(QPointF(a.x(), a.y()), QPointF(a.x()+2, a.y()+4));
}); }

static bool isImageFile(const QString &ext);

// Lightweight proxy that groups numbered image sequences into a single representative row
class SequenceGroupingProxyModel : public QSortFilterProxyModel {
public:
    struct Info {
        QString dir;
        QString base;
        QString ext;
        int start = -1;
        int end = -1;
        int count = 0;
        QString reprPath; // first frame path
    };

    explicit SequenceGroupingProxyModel(QObject* parent=nullptr)
        : QSortFilterProxyModel(parent) {}

    void setGroupingEnabled(bool on) { if (m_enabled == on) return; m_enabled = on; invalidateFilter(); }
    bool groupingEnabled() const { return m_enabled; }

    void rebuildForRoot(const QString& dirPath) {
        m_hidden.clear(); m_infoByRepr.clear(); m_keyByRepr.clear();
        if (!m_enabled || dirPath.isEmpty()) { invalidateFilter(); return; }
        QDir d(dirPath);
        // Only files are considered for sequences
        const auto files = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        QHash<QString, QList<QFileInfo>> buckets;
        for (const QFileInfo &fi : files) {
            const QString name = fi.fileName();
            auto m = SequenceDetector::mainPattern().match(name);
            if (!m.hasMatch()) continue;
            const QString base = m.captured(1);
            const QString digits = m.captured(3);
            const QString ext = m.captured(4).toLower();
            // images only
            if (!isImageFile(ext)) continue;
            const QString key = fi.absolutePath() + "|" + base + "|" + ext;
            buckets[key].append(fi);
        }
        for (auto it = buckets.begin(); it != buckets.end(); ++it) {
            if (it->size() <= 1) continue; // not a sequence
            // compute range and representative
            int start = std::numeric_limits<int>::max();
            int end = std::numeric_limits<int>::min();
            QFileInfo repr;
            for (const QFileInfo &fi : it.value()) {
                auto m = SequenceDetector::mainPattern().match(fi.fileName());
                int f = m.captured(3).toInt();
                if (f < start) { start = f; repr = fi; }
                if (f > end) end = f;
            }
            Info info;
            info.dir = repr.absolutePath();
            auto reprMatch = SequenceDetector::mainPattern().match(repr.fileName());
            info.base = reprMatch.captured(1);
            info.ext = reprMatch.captured(4).toLower();
            info.start = start; info.end = end; info.count = it->size();
            info.reprPath = repr.absoluteFilePath();
            const QString key = it.key();
            m_infoByRepr.insert(info.reprPath, info);
            m_keyByRepr.insert(info.reprPath, key);
            // hide all non-representatives
            for (const QFileInfo &fi : it.value()) {
                if (fi.absoluteFilePath() == info.reprPath) continue;
                m_hidden.insert(fi.absoluteFilePath());
            }
        }
        invalidateFilter();
    }

    bool isRepresentativeProxyIndex(const QModelIndex& proxyIdx) const {
        if (!proxyIdx.isValid()) return false;
        QModelIndex src = mapToSource(proxyIdx);
        auto *fs = qobject_cast<QFileSystemModel*>(sourceModel());
        if (!fs) return false;
        const QString path = fs->filePath(src);
        return m_infoByRepr.contains(path);
    }

    Info infoForProxyIndex(const QModelIndex& proxyIdx) const {
        Info i; if (!proxyIdx.isValid()) return i;
        QModelIndex src = mapToSource(proxyIdx);
        auto *fs = qobject_cast<QFileSystemModel*>(sourceModel());
        if (!fs) return i;
        const QString path = fs->filePath(src);
        return m_infoByRepr.value(path);
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex &source_parent) const override {
        if (!m_enabled) return QSortFilterProxyModel::filterAcceptsRow(source_row, source_parent);
        auto *fs = qobject_cast<QFileSystemModel*>(sourceModel());
        if (!fs) return true;
        QModelIndex idx = fs->index(source_row, 0, source_parent);
        if (!idx.isValid()) return true;
        QString path = fs->filePath(idx);
        // Never hide directories
        if (fs->isDir(idx)) return true;
        return !m_hidden.contains(path);
    }

    QVariant data(const QModelIndex &proxyIndex, int role) const override {
        if (!m_enabled || role != Qt::DisplayRole) return QSortFilterProxyModel::data(proxyIndex, role);
        if (!isRepresentativeProxyIndex(proxyIndex)) return QSortFilterProxyModel::data(proxyIndex, role);
        Info info = infoForProxyIndex(proxyIndex);
        // Compute padded range string (keep width of start frame digits)
        int pad = QString::number(info.start).size();
        QString startStr = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
        QString endStr = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
        return QString("%1.[%2-%3].%4").arg(info.base, startStr, endStr, info.ext);
    }

    // Always place folders before files regardless of sort order
    bool lessThan(const QModelIndex &source_left, const QModelIndex &source_right) const override {
        auto *fs = qobject_cast<QFileSystemModel*>(sourceModel());
        if (!fs) return QSortFilterProxyModel::lessThan(source_left, source_right);
        const bool leftIsDir = fs->isDir(source_left);
        const bool rightIsDir = fs->isDir(source_right);
        if (leftIsDir != rightIsDir) {
            // Folders-first invariant
            return leftIsDir; // true when left is a dir
        }
        // Same type: defer to default comparison (respects column and sort role)
        return QSortFilterProxyModel::lessThan(source_left, source_right);
    }

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override {
        m_sortOrder = order;
        QSortFilterProxyModel::sort(column, order);
    }

private:
    bool m_enabled = true;
    QSet<QString> m_hidden; // absolute file paths to hide
    QHash<QString, Info> m_infoByRepr; // reprPath -> info
    QHash<QString, QString> m_keyByRepr; // reprPath -> grouping key
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};

#include <QMediaPlayer>
#include <QMediaMetaData>
#include <QVideoWidget>
#include <QScrollArea>
#include <QFrame>
#include <QDockWidget>
#include <QEventLoop>
#include <QAudioOutput>

#include <QDesktopServices>
#include <QSize>
#include <QShortcut>
#include <QKeySequence>
#include <QSettings>

#include <QFileIconProvider>



#include "video_metadata.h"

// Custom QListView with compact drag pixmap
class AssetGridView : public QListView
{
public:
    explicit AssetGridView(QWidget *parent = nullptr) : QListView(parent) {}

protected:
    void startDrag(Qt::DropActions supportedActions) override
    {
        QModelIndexList indexes = selectionModel()->selectedIndexes();
        if (indexes.isEmpty()) {
            return;
        }

        // Create mime data
        QMimeData *mimeData = model()->mimeData(indexes);
        if (!mimeData) {
            return;
        }

        // Create a compact drag pixmap showing count
        int count = indexes.size();
        QPixmap pixmap(80, 80);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw a rounded rectangle background
        painter.setBrush(QColor(88, 166, 255, 200));

        painter.setPen(QPen(QColor(255, 255, 255), 2));
        painter.drawRoundedRect(5, 5, 70, 70, 8, 8);

        // Draw count text
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPixelSize(32);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(QRect(5, 5, 70, 70), Qt::AlignCenter, QString::number(count));

        painter.end();

        // Start the drag with custom pixmap
        QDrag *drag = new QDrag(this);
        drag->setMimeData(mimeData);
        drag->setPixmap(pixmap);
        drag->setHotSpot(QPoint(40, 40)); // Center of the pixmap

        Qt::DropAction defaultAction = Qt::MoveAction;
        if (supportedActions & Qt::MoveAction) {
            defaultAction = Qt::MoveAction;
        }

        drag->exec(supportedActions, defaultAction);
    }

};


// Icon provider for File Manager using live previews
class FmIconProvider : public QFileIconProvider {
public:
    FmIconProvider() : QFileIconProvider() {}
    QIcon icon(const QFileInfo &info) const override {
        if (info.isDir()) {
            return QFileIconProvider::icon(info);
        }
        const QString path = info.absoluteFilePath();
        const QString suffix = info.suffix().toLower();
        if (!isPreviewableSuffix(suffix)) {
            return QFileIconProvider::icon(info);
        }
        const QSize targetSize(64, 64);
        auto handle = LivePreviewManager::instance().cachedFrame(path, targetSize);
        if (handle.isValid()) {
            return QIcon(handle.pixmap);
        }
        LivePreviewManager::instance().requestFrame(path, targetSize);
        return QFileIconProvider::icon(info);
    }
};

// Custom delegate for asset grid view with live previews
class AssetItemDelegate : public QStyledItemDelegate
{
public:
    explicit AssetItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent), m_thumbnailSize(180) {}

    void setThumbnailSize(int size) { m_thumbnailSize = size; }
    int thumbnailSize() const { return m_thumbnailSize; }

private:
    int m_thumbnailSize;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        try {
            painter->save();

            const bool isSelected = option.state & QStyle::State_Selected;
            const bool isHovered = option.state & QStyle::State_MouseOver;

            const QRect cardRect = option.rect.adjusted(2, 2, -2, -2);
            QColor baseColor(26, 26, 26);   // slightly lighter than #0a0a0a background
            QColor hoverColor(38, 38, 38);
            QColor selectedColor(62, 90, 140);
            QColor cardColor = baseColor;
            if (isSelected) {
                cardColor = selectedColor;
            } else if (isHovered) {
                cardColor = hoverColor;
            }
            painter->setPen(Qt::NoPen);
            painter->setBrush(cardColor);
            painter->drawRoundedRect(cardRect, 6, 6);

            const QString filePath = index.data(AssetsModel::FilePathRole).toString();
            const QString fileType = index.data(AssetsModel::FileTypeRole).toString();

            if (isSelected || isHovered) {
                QColor c = (option.state & QStyle::State_Selected) ? QColor(88,166,255) : QColor(80,80,80);
                painter->setPen(QPen(c, 1.5));
                painter->setBrush(Qt::NoBrush);
                painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
            }

            const int margin = 6;
            const int thumbSide = m_thumbnailSize;
            QRect thumbRect(option.rect.x() + (option.rect.width()-thumbSide)/2, option.rect.y() + margin, thumbSide, thumbSide);

            const QString suffix = QFileInfo(filePath).suffix().toLower();
            LivePreviewManager &previewMgr = LivePreviewManager::instance();
            const QSize targetSize(thumbSide, thumbSide);
            const bool previewable = isPreviewableSuffix(suffix);
            bool drewPreview = false;
            if (previewable) {
                auto handle = previewMgr.cachedFrame(filePath, targetSize);
                if (handle.isValid()) {
                    painter->save();
                    QRect previewRect = insetPreviewRect(thumbRect);
                    painter->setClipRect(previewRect);
                    QPixmap scaled = handle.pixmap.scaled(previewRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    int x = previewRect.x() + (previewRect.width() - scaled.width()) / 2;
                    int y = previewRect.y() + (previewRect.height() - scaled.height()) / 2;
                    painter->drawPixmap(x, y, scaled);
                    painter->restore();
                    drewPreview = true;
                } else {
                    previewMgr.requestFrame(filePath, targetSize);
                }
            }

            if (!drewPreview) {
                painter->setPen(QPen(QColor(120,120,120), 1)); painter->setBrush(Qt::NoBrush);
                QRect placeholderRect = insetPreviewRect(thumbRect);
                painter->drawRoundedRect(placeholderRect, 6, 6);
                QString label = fileType.toUpper();
                if (label.isEmpty()) label = suffix.toUpper();
                if (label.isEmpty()) label = "FILE";
                QFont placeholder("Segoe UI", 9, QFont::Medium);
                painter->setFont(placeholder);
                painter->setPen(QColor(180,180,180));
                painter->drawText(thumbRect.adjusted(10,10,-10,-10), Qt::AlignCenter | Qt::TextWordWrap, label.left(6));
            }

            // Draw warning badge for sequences with gaps
            bool isSequence = index.data(AssetsModel::IsSequenceRole).toBool();
            bool hasGaps = index.data(AssetsModel::SequenceHasGapsRole).toBool();
            if (isSequence && hasGaps) {
                int gapCount = index.data(AssetsModel::SequenceGapCountRole).toInt();

                // Draw warning triangle badge in top-right corner
                int badgeSize = 24;
                QRect badgeRect(thumbRect.right() - badgeSize - 4, thumbRect.top() + 4, badgeSize, badgeSize);

                // Draw semi-transparent background
                painter->setBrush(QColor(255, 140, 0, 200)); // Orange
                painter->setPen(Qt::NoPen);
                painter->drawEllipse(badgeRect);

                // Draw warning icon (exclamation mark)
                painter->setPen(QColor(255, 255, 255));
                QFont badgeFont("Segoe UI", 14, QFont::Bold);
                painter->setFont(badgeFont);
                painter->drawText(badgeRect, Qt::AlignCenter, "!");

                // Tooltip would show: "Sequence has X gap(s)"
            }

            QString fileName = index.data(AssetsModel::FileNameRole).toString();
            QFont nameFont("Segoe UI", 9);
            painter->setFont(nameFont);
            painter->setPen(QColor(230,230,230));
            QRect nameRect(option.rect.x()+4, thumbRect.bottom()+4, option.rect.width()-8, option.rect.bottom()-thumbRect.bottom()-6);
            QString elided = QFontMetrics(nameFont).elidedText(fileName, Qt::ElideRight, nameRect.width());
            painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop, elided);
        } catch (const std::exception& e) {
            qCritical() << "[AssetItemDelegate] Exception in paint():" << e.what();
        } catch (...) {
            qCritical() << "[AssetItemDelegate] Unknown exception in paint()";
        }
        painter->restore();
    }

public:
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        int height = m_thumbnailSize + 60; // Add space for text overlay
        return QSize(m_thumbnailSize, height);
    }
};

// Minimalist delegate for File Manager grid: live preview + filename
class FmItemDelegate : public QStyledItemDelegate
{
public:
    explicit FmItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent), m_thumbnailSize(120) {}
    void setThumbnailSize(int s) { m_thumbnailSize = s; }
    int thumbnailSize() const { return m_thumbnailSize; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();

        // Outline on hover/selection only
        const bool isSelected = option.state & QStyle::State_Selected;
        const bool isHovered = option.state & QStyle::State_MouseOver;
        const QRect cardRect = option.rect.adjusted(2, 2, -2, -2);
        QColor baseColor(26, 26, 26);
        QColor hoverColor(38, 38, 38);
        QColor selectedColor(62, 90, 140);
        QColor cardColor = baseColor;
        if (isSelected) {
            cardColor = selectedColor;
        } else if (isHovered) {
            cardColor = hoverColor;
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(cardColor);
        painter->drawRoundedRect(cardRect, 6, 6);

        if (isSelected || isHovered) {
            QColor c = isSelected ? QColor(88,166,255) : QColor(80,80,80);
            painter->setPen(QPen(c, 1.5));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
        }

        const int margin = 6;
        const int thumbSide = m_thumbnailSize;
        QRect thumbRect(option.rect.x() + (option.rect.width()-thumbSide)/2, option.rect.y() + margin, thumbSide, thumbSide);
        const QString filePath = index.data(QFileSystemModel::FilePathRole).toString();

        // Check if this is a folder
        QFileInfo fileInfo(filePath);
        const bool isFolder = fileInfo.isDir();

        bool drewPreview = false;

        if (isFolder) {
            // Draw folder icon using Qt's standard folder icon
            QIcon folderIcon = option.widget->style()->standardIcon(QStyle::SP_DirIcon);
            QRect iconRect = insetPreviewRect(thumbRect);
            // Scale icon to fit nicely in the preview area (80% of available space)
            int iconSize = qMin(iconRect.width(), iconRect.height()) * 0.8;
            QRect centeredIconRect(
                iconRect.x() + (iconRect.width() - iconSize) / 2,
                iconRect.y() + (iconRect.height() - iconSize) / 2,
                iconSize,
                iconSize
            );
            folderIcon.paint(painter, centeredIconRect, Qt::AlignCenter);
            drewPreview = true;
        } else {
            // Handle file preview
            LivePreviewManager &previewMgr = LivePreviewManager::instance();
            const QSize targetSize(thumbSide, thumbSide);
            const QString suffix = fileInfo.suffix().toLower();
            const bool previewable = isPreviewableSuffix(suffix);

            if (previewable) {
                auto handle = previewMgr.cachedFrame(filePath, targetSize);
                if (handle.isValid()) {
                    painter->save();
                    QRect previewRect = insetPreviewRect(thumbRect);
                    painter->setClipRect(previewRect);
                    QPixmap scaled = handle.pixmap.scaled(previewRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    int x = previewRect.x() + (previewRect.width() - scaled.width()) / 2;
                    int y = previewRect.y() + (previewRect.height() - scaled.height()) / 2;
                    painter->drawPixmap(x, y, scaled);
                    painter->restore();
                    drewPreview = true;
                } else {
                    previewMgr.requestFrame(filePath, targetSize);
                }
            }
        }

        if (!drewPreview) {
            painter->setPen(QPen(QColor(120,120,120), 1));
            painter->setBrush(Qt::NoBrush);
            QRect placeholderRect = insetPreviewRect(thumbRect);
            painter->drawRoundedRect(placeholderRect, 6, 6);
            QString label = fileInfo.suffix().toUpper();
            if (label.isEmpty()) label = index.data(Qt::DisplayRole).toString().left(4).toUpper();
            if (label.isEmpty()) label = "FILE";
            QFont placeholder("Segoe UI", 9, QFont::Medium);
            painter->setFont(placeholder);
            painter->setPen(QColor(180,180,180));
            painter->drawText(thumbRect.adjusted(10,10,-10,-10), Qt::AlignCenter | Qt::TextWordWrap, label.left(6));
        }

        QString name = index.data(Qt::DisplayRole).toString();
        QFont f("Segoe UI", 9);
        painter->setFont(f);
        painter->setPen(QColor(230,230,230));
        const int textTop = thumbRect.bottom() + 6;
        int textHeight = option.rect.bottom() - textTop - margin;
        if (textHeight < 20) textHeight = 20;
        QRect nameRect(option.rect.x() + 4, textTop, option.rect.width() - 8, textHeight);
        QString el = QFontMetrics(f).elidedText(name, Qt::ElideRight, nameRect.width());
        painter->drawText(nameRect, Qt::AlignHCenter | Qt::AlignTop, el);

        painter->restore();
    }

private:
    int m_thumbnailSize;
};

class GridScrubOverlay : public QWidget
{
public:
    explicit GridScrubOverlay(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        hide();
    }

    void setProgress(qreal value)
    {
        m_progress = std::clamp(value, 0.0, 1.0);
        if (!m_hasCustomHint) {
            m_statusText = QStringLiteral("%1%").arg(qRound(m_progress * 100.0));
        }
        update();
    }

    void setHintText(const QString &text)
    {
        m_statusText = text;
        m_hasCustomHint = true;
        update();
    }

    void clearHintText()
    {
        m_hasCustomHint = false;
        m_statusText = m_defaultHint;
        update();
    }

    void setFrame(const QPixmap &pixmap)
    {
        m_frame = pixmap;
        update();
    }

    void clearFrame()
    {
        if (!m_frame.isNull()) {
            m_frame = QPixmap();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF bounds = rect();
        if (!bounds.isValid()) {
            return;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::NoBrush);
        painter.setClipRect(bounds.adjusted(0, 0, -0.5, -0.5));

        painter.fillRect(bounds, QColor(0, 0, 0, 220));

        if (!m_frame.isNull()) {
            QSize targetSize = bounds.size().toSize();
            if (!targetSize.isEmpty()) {
                QPixmap scaled = m_frame.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                const qreal x = bounds.left() + (bounds.width() - scaled.width()) / 2.0;
                const qreal y = bounds.top() + (bounds.height() - scaled.height()) / 2.0;
                painter.drawPixmap(QPointF(x, y), scaled);
            }
        } else {
            painter.setPen(QPen(QColor(80, 80, 80, 160), 1.0));
            painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 6, 6);
            painter.setPen(Qt::NoPen);
        }

        const qreal hudHeight = 26.0;
        QRectF hudRect = bounds.adjusted(8, bounds.height() - hudHeight - 10, -8, -6);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 170));
        painter.drawRoundedRect(hudRect, 6, 6);

        const qreal barHeight = 4.0;
        QRectF barRect(hudRect.left() + 10, hudRect.bottom() - barHeight - 6, hudRect.width() - 20, barHeight);
        painter.setBrush(QColor(60, 60, 60, 220));
        painter.drawRoundedRect(barRect, 2, 2);

        QRectF fillRect = barRect;
        fillRect.setWidth(barRect.width() * m_progress);
        if (fillRect.width() > 0) {
            painter.setBrush(QColor(88, 166, 255, 230));
            painter.drawRoundedRect(fillRect, 3, 3);
        }

        painter.setPen(Qt::white);
        painter.setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::DemiBold));
        QRectF textRect(hudRect.left() + 10, hudRect.top() + 6, hudRect.width() - 20, hudRect.height() - barHeight - 14);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_statusText);
    }

private:
    qreal m_progress = 0.0;
    QString m_statusText = QStringLiteral("Ctrl + Move/Wheel to scrub");
    const QString m_defaultHint = QStringLiteral("Ctrl + Move/Wheel to scrub");
    bool m_hasCustomHint = false;
    QPixmap m_frame;
};

class GridScrubController : public QObject
{
public:
    GridScrubController(QAbstractItemView *view,
                        std::function<QString(const QModelIndex&)> resolver,
                        QObject *parent = nullptr)
        : QObject(parent),
          m_view(view),
          m_pathResolver(std::move(resolver)),
          m_overlay(new GridScrubOverlay(view->viewport()))
    {
        if (!m_view) return;
        m_view->setMouseTracking(true);
        if (m_view->viewport()) {
            m_view->viewport()->setMouseTracking(true);
            m_view->viewport()->installEventFilter(this);
        }
        m_view->installEventFilter(this);
        if (m_view->verticalScrollBar()) {
            connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](){ updateOverlayGeometry(); });
        }
        if (m_view->horizontalScrollBar()) {
            connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](){ updateOverlayGeometry(); });
        }
        if (m_view->model()) {
            connect(m_view->model(), &QAbstractItemModel::modelReset, this, [this]() {
                m_positions.clear();
                hideOverlay();
            });
        }

        LivePreviewManager &previewMgr = LivePreviewManager::instance();
        connect(&previewMgr, &LivePreviewManager::frameReady, this,
                [this](const QString &path, qreal position, QSize, const QPixmap &pixmap) {
            if (path != m_currentPath || !m_overlay) {
                return;
            }
            m_loadingFrame = false;
            m_position = position;
            m_positions[m_currentPath] = position;
            m_overlay->setProgress(m_position);
            m_overlay->setFrame(pixmap);
            if (QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) || !qFuzzyIsNull(m_position)) {
                m_overlay->setHintText(QStringLiteral("%1%").arg(qRound(m_position * 100.0)));
            } else {
                m_overlay->clearHintText();
            }
        });
        connect(&previewMgr, &LivePreviewManager::frameFailed, this,
                [this](const QString &path, const QString &error) {
            if (path != m_currentPath || !m_overlay) {
                return;
            }
            m_loadingFrame = false;
            m_overlay->clearFrame();
            m_overlay->setHintText(error);
        });
    }

    ~GridScrubController() override
    {
        endScrub();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (!m_view) return QObject::eventFilter(watched, event);

        if (watched == m_view->viewport()) {
            switch (event->type()) {
            case QEvent::MouseMove:
            {
                auto moveEvent = static_cast<QMouseEvent*>(event);
                const QPoint pos = moveEvent->position().toPoint();
                handleHoverMove(pos);
                if (QApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
                    if (m_currentIndex.isValid()) {
                        handleCtrlScrub(pos);
                        showOverlay();
                    }
                    event->accept();
                    return true;
                } else {
                    endScrub();
                    resetCtrlTracking();
                }
                break;
            }
            case QEvent::Leave:
                if (m_scrubActive && m_currentIndex.isValid()) {
                    updateOverlayGeometry();
                } else {
                    hideOverlay();
                    m_currentPath.clear();
                    m_currentIndex = QModelIndex();
                }
                break;
            case QEvent::Wheel:
            {
                auto wheel = static_cast<QWheelEvent*>(event);
                if (!wheel->modifiers().testFlag(Qt::ControlModifier)) {
                    endScrub();
                    hideOverlay();
                    resetCtrlTracking();
                    break;
                }
                QModelIndex idx = m_view->indexAt(wheel->position().toPoint());
                if (!idx.isValid()) {
                    wheel->accept();
                    return true;
                }
                setCurrentIndex(idx);
                beginScrub();
                int delta = wheel->angleDelta().x();
                if (delta == 0) {
                    delta = wheel->angleDelta().y();
                }
                if (delta != 0 && !m_currentPath.isEmpty()) {
                    const qreal step = static_cast<qreal>(delta) / 3600.0;
                    setPosition(std::clamp(m_position + step, 0.0, 1.0));
                    requestPreview();
                }
                showOverlay();
                resetCtrlTracking();
                wheel->accept();
                return true;
            }
            case QEvent::Resize:
                updateOverlayGeometry();
                break;
            default:
                break;
            }
        } else if (watched == m_view) {
            if (event->type() == QEvent::KeyRelease) {
                auto keyEvent = static_cast<QKeyEvent*>(event);
                if (keyEvent->key() == Qt::Key_Control) {
                    if (qFuzzyIsNull(m_position)) {
                        hideOverlay();
                    }
                    endScrub();
                    resetCtrlTracking();
                }
            }
        }

        return QObject::eventFilter(watched, event);
    }

private:
    void handleHoverMove(const QPoint &pos)
    {
        if (m_scrubActive && m_currentIndex.isValid()) {
            handleCtrlScrub(pos);
            updateOverlayGeometry();
            return;
        }
        QModelIndex idx = m_view->indexAt(pos);
        if (!idx.isValid()) {
            if (!m_scrubActive) {
                hideOverlay();
                m_currentIndex = QModelIndex();
                m_currentPath.clear();
            }
            return;
        }
        if (idx == m_currentIndex) {
            return;
        }
        setCurrentIndex(idx);
    }

    void setCurrentIndex(const QModelIndex &idx)
    {
        if (!idx.isValid()) {
            m_currentIndex = QModelIndex();
            m_currentPath.clear();
            hideOverlay();
            resetCtrlTracking();
            return;
        }
        QString resolved = m_pathResolver ? m_pathResolver(idx) : QString();
        if (resolved.isEmpty()) {
            m_currentIndex = QModelIndex();
            m_currentPath.clear();
            hideOverlay();
            resetCtrlTracking();
            return;
        }

        QFileInfo resolvedInfo(resolved);
        if (!resolvedInfo.exists() || !resolvedInfo.isFile()) {
            m_currentIndex = QModelIndex();
            m_currentPath.clear();
            hideOverlay();
            resetCtrlTracking();
            return;
        }

        m_currentIndex = idx;
        m_currentPath = resolved;
        m_position = m_positions.value(m_currentPath, kScrubDefaultPosition);
        m_loadingFrame = false;
        endScrub();
        if (m_overlay) {
            m_overlay->setProgress(m_position);
            m_overlay->clearHintText();
            m_overlay->clearFrame();
        }
        if (QApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
            m_lastMouseX = m_view->viewport()->mapFromGlobal(QCursor::pos()).x();
            showOverlay();
            requestPreview();
        } else {
            resetCtrlTracking();
        }
    }

    void setPosition(qreal value)
    {
        m_position = std::clamp(value, 0.0, 1.0);
        if (!m_currentPath.isEmpty()) {
            m_positions[m_currentPath] = m_position;
        }
        m_overlay->setProgress(m_position);
    }

    void requestPreview()
    {
        QFileInfo info(m_currentPath);
        if (!info.exists() || !info.isFile()) {
            return;
        }
        QSize targetSize = currentTargetSize();
        if (m_overlay) {
            m_overlay->setProgress(m_position);
            m_overlay->setHintText(QStringLiteral("Decoding..."));
        }
        beginScrub();
        m_loadingFrame = true;
        LivePreviewManager::instance().requestFrame(m_currentPath, targetSize, m_position);
    }

    void showOverlay()
    {
        if (!m_overlay || !m_currentIndex.isValid()) return;
        if (m_loadingFrame) {
            m_overlay->setHintText(QStringLiteral("Decoding..."));
        } else if (qFuzzyCompare(m_position, kScrubDefaultPosition) && !QApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
            m_overlay->clearHintText();
        } else {
            m_overlay->setHintText(QStringLiteral("%1%").arg(qRound(m_position * 100.0)));
        }
        updateOverlayGeometry();
        m_overlay->setProgress(m_position);
        m_overlay->show();
        m_overlay->raise();
    }

    void hideOverlay()
    {
        if (m_overlay) {
            m_overlay->hide();
            m_overlay->clearHintText();
            m_overlay->clearFrame();
        }
        m_loadingFrame = false;
        endScrub();
        resetCtrlTracking();
    }

        void updateOverlayGeometry()
    {
        if (!m_overlay || !m_currentIndex.isValid()) return;
        QRect thumbRect = currentThumbRect();
        if (!thumbRect.isValid()) {
            hideOverlay();
            return;
        }
        m_overlay->setGeometry(thumbRect.adjusted(1, 1, -1, -1));
    }

    bool handleCtrlScrub(const QPoint &pos)
    {
        if (m_warpingCursor) {
            m_warpingCursor = false;
        }
        if (m_currentPath.isEmpty() || !m_currentIndex.isValid()) {
            return false;
        }
        QRect thumbRect = currentThumbRect();
        if (!thumbRect.isValid() || thumbRect.width() <= 0) {
            return false;
        }
        const int clampedX = std::clamp(pos.x(), thumbRect.left(), thumbRect.right());
        const int clampedY = std::clamp(pos.y(), thumbRect.top(), thumbRect.bottom());
        if (m_view && m_view->viewport() && (clampedX != pos.x() || clampedY != pos.y())) {
            m_warpingCursor = true;
            const QPoint clampedPoint(clampedX, clampedY);
            QCursor::setPos(m_view->viewport()->mapToGlobal(clampedPoint));
        }
        beginScrub();
        const qreal fraction = thumbRect.width() > 0
            ? static_cast<qreal>(clampedX - thumbRect.left()) / static_cast<qreal>(thumbRect.width())
            : 0.0;
        m_lastMouseX = clampedX;
        setPosition(fraction);
        requestPreview();
        return true;
    }

    QSize currentTargetSize() const
    {
        QSize targetSize = m_view ? m_view->iconSize() : QSize();
        if (!targetSize.isValid() || targetSize.isEmpty()) {
            targetSize = QSize(180, 180);
        }
        return targetSize;
    }

    QRect thumbRectFor(const QRect &itemRect) const
    {
        if (!itemRect.isValid()) {
            return QRect();
        }
        const int margin = 6;
        QSize icon = currentTargetSize();
        int side = std::max(0, std::min(icon.width(), icon.height()));
        side = std::min(side, itemRect.width() - margin * 2);
        side = std::min(side, itemRect.height() - margin * 2);
        if (side <= 0) {
            return QRect();
        }
        int x = itemRect.x() + (itemRect.width() - side) / 2;
        int y = itemRect.y() + margin;
        if (y + side > itemRect.bottom() - margin) {
            y = itemRect.bottom() - margin - side;
        }
        return QRect(x, y, side, side);
    }

    QRect currentThumbRect() const
    {
        if (!m_currentIndex.isValid() || !m_view) {
            return QRect();
        }
        QRect itemRect = m_view->visualRect(m_currentIndex);
        return thumbRectFor(itemRect);
    }

    void resetCtrlTracking()
    {
        m_lastMouseX = std::numeric_limits<qreal>::quiet_NaN();
    }

    void beginScrub()
    {
        if (m_scrubActive) {
            return;
        }
        m_scrubActive = true;
        if (m_view && m_view->viewport() && !m_mouseGrabbed) {
            m_view->viewport()->grabMouse();
            m_mouseGrabbed = true;
        }
    }

    void endScrub()
    {
        if (!m_scrubActive) {
            return;
        }
        m_scrubActive = false;
        if (m_view && m_view->viewport() && m_mouseGrabbed) {
            m_view->viewport()->releaseMouse();
            m_mouseGrabbed = false;
        }
    }

    QAbstractItemView *m_view = nullptr;
    std::function<QString(const QModelIndex&)> m_pathResolver;
    GridScrubOverlay *m_overlay = nullptr;
    QModelIndex m_currentIndex;
    QString m_currentPath;
    qreal m_position = kScrubDefaultPosition;
    QHash<QString, qreal> m_positions;
    qreal m_lastMouseX = std::numeric_limits<qreal>::quiet_NaN();
    bool m_loadingFrame = false;
    bool m_scrubActive = false;
    bool m_mouseGrabbed = false;
    bool m_warpingCursor = false;
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , mainSplitter(nullptr)
    , rightSplitter(nullptr)
    , anchorIndex(-1)
    , currentAssetId(-1)
    , previewIndex(-1)
    , previewOverlay(nullptr)
    , importer(nullptr)
    , projectFolderWatcher(nullptr)
    , assetsLocked(true) // Locked by default
{
    fileOpsDialog = nullptr;
    LogManager::instance().addLog("[MAINWINDOW] ctor begin");

    // Load LivePreview cache size setting
    {
        QSettings s("AugmentCode", "KAssetManager");
        int cacheSize = s.value("LivePreview/MaxCacheEntries", 256).toInt();
        LivePreviewManager::instance().setMaxCacheEntries(cacheSize);
    }

    m_initializing = true;
    setupUi();
    setupConnections();
    m_initializing = false;
#ifdef QT_DEBUG
    qDebug() << "[INIT] [PREVIEW_CAPS] QtPdf="
#ifdef HAVE_QT_PDF
             << "ON";
#else
             << "OFF";
#endif
    qDebug() << "[INIT] [PREVIEW_CAPS] ActiveQt="
#ifdef HAVE_QT_AX
             << "ON";
#else
             << "OFF";
#endif
#endif

    setWindowTitle("KAsset Manager");
    resize(1400, 900);

    // Enable drag and drop
    setAcceptDrops(true);

    // Create importer
    importer = new Importer(this);
    connect(importer, &Importer::progressChanged, this, &MainWindow::onImportProgress);
    connect(importer, &Importer::currentFileChanged, this, &MainWindow::onImportFileChanged);
    connect(importer, &Importer::currentFolderChanged, this, &MainWindow::onImportFolderChanged);
    connect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);

    // Create project folder watcher
    projectFolderWatcher = new ProjectFolderWatcher(this);
    connect(projectFolderWatcher, &ProjectFolderWatcher::projectFolderChanged,
            this, &MainWindow::onProjectFolderChanged);

    // Load existing project folders into watcher
    auto projectFolders = DB::instance().listProjectFolders();
    for (const auto& pf : projectFolders) {
        int projectFolderId = pf.first;
        QString path = pf.second.second;
        projectFolderWatcher->addProjectFolder(projectFolderId, path);
    }

    // Create import progress dialog (will be shown when needed)
    importProgressDialog = nullptr;

    // Setup live preview progress bar in status bar
    thumbnailProgressLabel = new QLabel(this);
    thumbnailProgressLabel->setVisible(false);
    thumbnailProgressBar = new QProgressBar(this);
    thumbnailProgressBar->setVisible(false);
    thumbnailProgressBar->setMaximumWidth(200);
    thumbnailProgressBar->setTextVisible(true);
    statusBar()->addPermanentWidget(thumbnailProgressLabel);
    statusBar()->addPermanentWidget(thumbnailProgressBar);

    // Debounced timer for visible-only preview progress
    visibleThumbTimer.setSingleShot(true);
    connect(&visibleThumbTimer, &QTimer::timeout, this, &MainWindow::updateVisibleThumbProgress);

    // Update views when live preview frames arrive
    connect(&LivePreviewManager::instance(), &LivePreviewManager::frameReady,
            this, [this](const QString& filePath, qreal, QSize, const QPixmap& pixmap) {
        g_lastPreviewError.remove(filePath);
        if (assetGridView && assetGridView->viewport()) assetGridView->viewport()->update();
        if (fmGridView && fmGridView->viewport()) fmGridView->viewport()->update();
        versionPreviewCache[filePath] = pixmap;
        if (versionTable) {
            for (int row = 0; row < versionTable->rowCount(); ++row) {
                QTableWidgetItem *iconItem = versionTable->item(row, 0);
                if (iconItem && iconItem->data(Qt::UserRole).toString() == filePath) {
                    iconItem->setIcon(QIcon(pixmap));
                    iconItem->setText(QString());
                }
            }
        }
        visibleThumbTimer.start(50);
    });
    connect(&LivePreviewManager::instance(), &LivePreviewManager::frameFailed,
            this, [](const QString& path, const QString& error) {
        QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            return;
        }
        if (!isPreviewableSuffix(info.suffix())) {
            return;
        }
        const QString last = g_lastPreviewError.value(path);
        if (last == error) {
            return;
        }
        g_lastPreviewError.insert(path, error);
        qWarning() << "[LivePreview] failed for" << path << ':' << error;
    });

}

MainWindow::~MainWindow()
{
}

void MainWindow::setupUi()
{
    LogManager::instance().addLog("[TRACE] setupUi enter", "DEBUG");
    // Menu bar
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);

    QMenu* fileMenu = menuBar->addMenu("&File");
    fileMenu->setStyleSheet(
        "QMenu { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; }"
        "QMenu::item:selected { background-color: #2f3a4a; }"
    );

    QAction* addProjectFolderAction = fileMenu->addAction("Add &Project Folder...");
    addProjectFolderAction->setShortcut(QKeySequence("Ctrl+P"));
    connect(addProjectFolderAction, &QAction::triggered, this, &MainWindow::onAddProjectFolder);

    fileMenu->addSeparator();

    QAction* settingsAction = fileMenu->addAction("&Settings");
    settingsAction->setShortcut(QKeySequence("Ctrl+,"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);

    fileMenu->addSeparator();

    QAction* exitAction = fileMenu->addAction("E&xit");
    exitAction->setShortcut(QKeySequence("Ctrl+Q"));
    connect(exitAction, &QAction::triggered, this, &QMainWindow::close);

    // View menu
    QMenu* viewMenu = menuBar->addMenu("&View");
    viewMenu->setStyleSheet(
        "QMenu { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; }"
        "QMenu::item:selected { background-color: #2f3a4a; }"
    );

    toggleLogViewerAction = viewMenu->addAction("Show &Log Viewer");
    toggleLogViewerAction->setShortcut(QKeySequence("Ctrl+L"));
    toggleLogViewerAction->setCheckable(true);
    toggleLogViewerAction->setChecked(false);
    connect(toggleLogViewerAction, &QAction::triggered, this, &MainWindow::onToggleLogViewer);

    // Tools menu
    QMenu* toolsMenu = menuBar->addMenu("&Tools");
    toolsMenu->setStyleSheet(
        "QMenu { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; }"
        "QMenu::item:selected { background-color: #2f3a4a; }"
    );

    QAction* dbHealthAction = toolsMenu->addAction("Database &Health...");
    dbHealthAction->setShortcut(QKeySequence("Ctrl+H"));
    connect(dbHealthAction, &QAction::triggered, this, &MainWindow::showDatabaseHealthDialog);

    // Tabs: Asset Manager | File Manager
    mainTabs = new QTabWidget(this);
    mainTabs->setDocumentMode(true);
    mainTabs->setTabsClosable(false);
    setCentralWidget(mainTabs);

    // Asset Manager page
    assetManagerPage = new QWidget(this);
    QVBoxLayout* amLayout = new QVBoxLayout(assetManagerPage);
    amLayout->setContentsMargins(0, 0, 0, 0);

    // Main splitter: left (folders) | center (assets) | right (filters+info)
    mainSplitter = new QSplitter(Qt::Horizontal, assetManagerPage);
    amLayout->addWidget(mainSplitter);

    // Left panel: Folder tree with recursive checkbox
    QWidget* leftPanel = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    folderTreeView = new QTreeView(leftPanel);
    folderModel = new VirtualFolderTreeModel(leftPanel);
    LogManager::instance().addLog("[TRACE] folder model created", "DEBUG");
    folderTreeView->setModel(folderModel);
    LogManager::instance().addLog("[TRACE] folder model set on tree", "DEBUG");

    folderTreeView->setHeaderHidden(true);
    folderTreeView->setContextMenuPolicy(Qt::CustomContextMenu);

    // Enable multi-selection with Ctrl+Click and Shift+Click
    folderTreeView->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // Allow normal expand/collapse behavior like Windows Explorer
    folderTreeView->setExpandsOnDoubleClick(false);

    folderTreeView->setStyleSheet(
        "QTreeView { background-color: #121212; color: #ffffff; border: none; }"
        "QTreeView::item:selected { background-color: #2f3a4a; color: #ffffff; }"
        "QTreeView::item:hover { background-color: #202020; }"
    );

    // Expand root folder by default
    folderTreeView->expandToDepth(0);

    leftLayout->addWidget(folderTreeView);

    // Recursive checkbox at bottom of folder pane
    recursiveCheckBox = new QCheckBox("Include subfolder contents", leftPanel);
    recursiveCheckBox->setChecked(false);
    recursiveCheckBox->setStyleSheet(
        "QCheckBox { color: #ffffff; font-size: 11px; padding: 4px 8px; background-color: #1a1a1a; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
        "QCheckBox::indicator:checked { background-color: #58a6ff; border: 1px solid #58a6ff; }"
        "QCheckBox::indicator:unchecked { background-color: #2a2a2a; border: 1px solid #666; }"
    );
    recursiveCheckBox->setToolTip("When checked, shows assets from selected folder and all its subfolders");
    connect(recursiveCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        assetsModel->setRecursiveMode(checked);
    });

    leftLayout->addWidget(recursiveCheckBox);

    // Center panel: Asset grid with toolbar
    QWidget* centerPanel = new QWidget(this);
    QVBoxLayout* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    // Toolbar for view controls
    QWidget* toolbar = new QWidget(centerPanel);
    toolbar->setStyleSheet("QWidget { background-color: #1a1a1a; border-bottom: 1px solid #333; }");
    QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);
    toolbarLayout->setSpacing(6);

    // View mode toggle button
    isGridMode = true;
    viewModeButton = new QToolButton(toolbar);
    viewModeButton->setIcon(icoGrid());
    viewModeButton->setToolTip("Toggle Grid/List");
    viewModeButton->setAutoRaise(true);
    viewModeButton->setIconSize(QSize(20,20));
    connect(viewModeButton, &QToolButton::clicked, this, &MainWindow::onViewModeChanged);
    toolbarLayout->addWidget(viewModeButton);

    // Thumbnail size label
    QLabel* sizeLabel = new QLabel("Size:", toolbar);
    sizeLabel->setStyleSheet("color: #ffffff; font-size: 12px;");
    toolbarLayout->addWidget(sizeLabel);

    // Thumbnail size slider
    thumbnailSizeSlider = new QSlider(Qt::Horizontal, toolbar);
    thumbnailSizeSlider->setRange(100, 400);
    thumbnailSizeSlider->setValue(180);
    thumbnailSizeSlider->setFixedWidth(150);
    thumbnailSizeSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #333; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #58a6ff; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: #4a8fd9; }"
    );
    thumbnailSizeSlider->setToolTip("Adjust thumbnail size");
    connect(thumbnailSizeSlider, &QSlider::valueChanged, this, &MainWindow::onThumbnailSizeChanged);
    toolbarLayout->addWidget(thumbnailSizeSlider);

    // Size value label
    QLabel* sizeValueLabel = new QLabel("180px", toolbar);
    sizeValueLabel->setStyleSheet("color: #999; font-size: 11px; min-width: 45px;");
    connect(thumbnailSizeSlider, &QSlider::valueChanged, [sizeValueLabel](int value) {

        sizeValueLabel->setText(QString("%1px").arg(value));
    });
    toolbarLayout->addWidget(sizeValueLabel);

    toolbarLayout->addStretch();

    // Lock checkbox for project folders
    lockCheckBox = new QCheckBox("🔒 Lock Assets", toolbar);
    lockCheckBox->setChecked(true); // Locked by default
    lockCheckBox->setStyleSheet(
        "QCheckBox { color: #ff4444; font-size: 12px; font-weight: bold; }"
        "QCheckBox::indicator { width: 16px; height: 16px; }"
        "QCheckBox::indicator:checked { background-color: #ff4444; border: 1px solid #ff4444; }"
        "QCheckBox::indicator:unchecked { background-color: #2a2a2a; border: 1px solid #666; }"
    );
    lockCheckBox->setToolTip("When locked, assets can only be moved within their project folder");
    connect(lockCheckBox, &QCheckBox::toggled, this, &MainWindow::onLockToggled);
    toolbarLayout->addWidget(lockCheckBox);

    // Refresh button
    refreshButton = new QPushButton(toolbar);
    refreshButton->setIcon(icoRefresh());
    // Live preview prefetch button (with menu)
    thumbGenButton = new QToolButton(toolbar);
    thumbGenButton->setIcon(icoRefresh());
    thumbGenButton->setToolTip("Prefetch live previews");
    thumbGenButton->setAutoRaise(true);
    thumbGenButton->setIconSize(QSize(20,20));
    QMenu *genMenu = new QMenu(thumbGenButton);
    QAction *actGen = genMenu->addAction("Prefetch for this folder");
    QAction *actRegen = genMenu->addAction("Refresh for this folder");
    genMenu->addSeparator();
    QAction *actGenRec = genMenu->addAction("Prefetch recursive");
    QAction *actRegenRec = genMenu->addAction("Refresh recursive");
    connect(actGen, &QAction::triggered, this, &MainWindow::onPrefetchLivePreviewsForFolder);
    connect(actRegen, &QAction::triggered, this, &MainWindow::onRefreshLivePreviewsForFolder);
    connect(actGenRec, &QAction::triggered, this, &MainWindow::onPrefetchLivePreviewsRecursive);
    connect(actRegenRec, &QAction::triggered, this, &MainWindow::onRefreshLivePreviewsRecursive);
    thumbGenButton->setMenu(genMenu);
    thumbGenButton->setPopupMode(QToolButton::MenuButtonPopup);
    toolbarLayout->addWidget(thumbGenButton);

    refreshButton->setToolTip("Refresh assets from project folders");
    refreshButton->setFixedSize(28, 28);
    refreshButton->setFlat(true);
    refreshButton->setStyleSheet("QPushButton{background:transparent;border:none;}");
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::onRefreshAssets);
    toolbarLayout->addWidget(refreshButton);


    centerLayout->addWidget(toolbar);


    // Stacked widget to switch between grid and table views
    viewStack = new QStackedWidget(centerPanel);

    // Asset grid view (using custom AssetGridView with compact drag pixmap)
    assetGridView = new AssetGridView(viewStack);
    assetsModel = new AssetsModel(viewStack);

    assetGridView->setModel(assetsModel);
    LogManager::instance().addLog("[TRACE] assetGridView + model wired", "DEBUG");
    assetGridView->setViewMode(QListView::IconMode);
    assetGridView->setResizeMode(QListView::Adjust);
    assetGridView->setSpacing(4);
    assetGridView->setUniformItemSizes(true);
    assetGridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    assetGridView->setContextMenuPolicy(Qt::CustomContextMenu);
    assetGridView->setItemDelegate(new AssetItemDelegate(viewStack));

    assetGridView->setIconSize(QSize(180, 180));
    assetGridView->setStyleSheet(
        "QListView { background-color: #0a0a0a; border: none; }"
    );
    viewStack->addWidget(assetGridView); // Index 0

    // Asset table view for list mode
    assetTableView = new QTableView(viewStack);
    AssetsTableModel* tableModel = new AssetsTableModel(assetsModel, viewStack);
    assetTableView->setModel(tableModel);
    assetTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    assetTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    assetTableView->setContextMenuPolicy(Qt::CustomContextMenu);
    assetTableView->setSortingEnabled(true);
    assetTableView->setAlternatingRowColors(false);
    assetTableView->setShowGrid(false);
    assetTableView->verticalHeader()->setVisible(false);
    assetTableView->verticalHeader()->setDefaultSectionSize(22);
    assetTableView->verticalHeader()->setMinimumSectionSize(18);
    assetTableView->horizontalHeader()->setStretchLastSection(true);
    // Persist assetTableView column widths immediately when resized
    connect(assetTableView->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logical, int /*oldSize*/, int newSize){
        QSettings s("AugmentCode", "KAssetManager");
        s.setValue(QString("AssetManager/AssetTable/Col%1").arg(logical), newSize);
    });

    assetTableView->setStyleSheet(
        "QTableView { background-color: #0a0a0a; color: #ffffff; border: none; }"
        "QTableView::item { padding: 2px 6px; }"
        "QTableView::item:selected { background-color: #2f3a4a; }"
        "QHeaderView::section { background-color: #1a1a1a; color: #ffffff; border: none; padding: 4px; }"
    );
    // Set column widths
    assetTableView->setColumnWidth(AssetsTableModel::NameColumn, 300);
    assetTableView->setColumnWidth(AssetsTableModel::ExtensionColumn, 80);
    assetTableView->setColumnWidth(AssetsTableModel::SizeColumn, 100);

    assetTableView->setColumnWidth(AssetsTableModel::DateColumn, 150);

    assetTableView->setColumnWidth(AssetsTableModel::RatingColumn, 100);
    viewStack->addWidget(assetTableView); // Index 1


    // Set grid view as default
    viewStack->setCurrentIndex(0);

    centerLayout->addWidget(viewStack);

    // Enable drag-and-drop
    assetGridView->setDragEnabled(true);
    assetGridView->setAcceptDrops(false);
    assetGridView->setDragDropMode(QAbstractItemView::DragOnly);
    assetGridView->setDefaultDropAction(Qt::MoveAction);
    assetGridView->setSelectionRectVisible(false);

    // Enable drag-and-drop on folder tree for moving assets to folders AND reorganizing folders
    folderTreeView->setDragEnabled(true);
    folderTreeView->setAcceptDrops(true);
    folderTreeView->setDropIndicatorShown(true);
    folderTreeView->setDragDropMode(QAbstractItemView::DragDrop);
    folderTreeView->setDefaultDropAction(Qt::MoveAction);

    folderTreeView->viewport()->installEventFilter(this);

    // Install event filter on asset views to handle Space key for preview
    assetGridView->installEventFilter(this);
    assetTableView->installEventFilter(this);
    // Also monitor viewport resize to update visible-only progress
    assetGridView->viewport()->installEventFilter(this);
    assetTableView->viewport()->installEventFilter(this);

    assetScrubController = new GridScrubController(
        assetGridView,
        [this](const QModelIndex& idx) -> QString {
            if (!assetsModel) {
                return QString();
            }
            return assetsModel->data(idx, AssetsModel::FilePathRole).toString();
        },
        this);
    LogManager::instance().addLog("[TRACE] assetScrubController ready", "DEBUG");

    // Right panel: Filters + Info
    rightPanel = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    // Filters panel
    filtersPanel = new QWidget(this);
    QVBoxLayout *filtersLayout = new QVBoxLayout(filtersPanel);
    filtersLayout->setContentsMargins(8, 8, 8, 8);

    QLabel *filtersTitle = new QLabel("Filters", this);
    filtersTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #ffffff;");
    filtersLayout->addWidget(filtersTitle);

    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText("Search...");
    searchBox->setStyleSheet(
        "QLineEdit { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; padding: 6px; border-radius: 4px; }"
    );
    filtersLayout->addWidget(searchBox);

    QLabel *ratingLabel = new QLabel("Rating:", this);
    ratingLabel->setStyleSheet("color: #ffffff; margin-top: 8px;");
    filtersLayout->addWidget(ratingLabel);

    ratingFilter = new QComboBox(this);
    ratingFilter->addItems({"All", "5 Stars", "4+ Stars", "3+ Stars", "Unrated"});
    ratingFilter->setStyleSheet(
        "QComboBox { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; padding: 6px; border-radius: 4px; }"
    );
    connect(ratingFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        assetsModel->setRatingFilter(index);
    });
    filtersLayout->addWidget(ratingFilter);

    // Tags section with + button
    QHBoxLayout *tagsHeaderLayout = new QHBoxLayout();
    QLabel *tagsLabel = new QLabel("Tags:", this);
    tagsLabel->setStyleSheet("color: #ffffff; margin-top: 8px;");
    tagsHeaderLayout->addWidget(tagsLabel);
    tagsHeaderLayout->addStretch();

    QPushButton *addTagBtn = new QPushButton("+", this);
    addTagBtn->setFixedSize(24, 24);
    addTagBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: #ffffff; border: none; border-radius: 12px; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background-color: #4a8fd9; }"
    );
    addTagBtn->setToolTip("Create new tag");
    connect(addTagBtn, &QPushButton::clicked, this, &MainWindow::onCreateTag);
    tagsHeaderLayout->addWidget(addTagBtn);

    filtersLayout->addLayout(tagsHeaderLayout);

    tagsListView = new QListView(filtersPanel);
    tagsModel = new TagsModel(this);
    tagsListView->setModel(tagsModel);
    tagsListView->setSelectionMode(QAbstractItemView::MultiSelection);
    tagsListView->setContextMenuPolicy(Qt::CustomContextMenu);
    tagsListView->setStyleSheet("");
    tagsListView->setMaximumHeight(150);

    // Enable drops on tags list for assigning tags to assets
    tagsListView->setAcceptDrops(true);
    tagsListView->setDropIndicatorShown(true);
    tagsListView->setDragDropMode(QAbstractItemView::DropOnly);
    // Tag action buttons
    QHBoxLayout *tagButtonsLayout = new QHBoxLayout();

    applyTagsBtn = new QPushButton("Apply", this);
    applyTagsBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: #ffffff; border: none; padding: 6px 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8fd9; }"
        "QPushButton:disabled { background-color: #333; color: #666; }"
    );
    applyTagsBtn->setToolTip("Apply selected tags to selected assets");
    applyTagsBtn->setEnabled(false);
    connect(applyTagsBtn, &QPushButton::clicked, this, &MainWindow::onApplyTags);
    tagButtonsLayout->addWidget(applyTagsBtn);

    filterByTagsBtn = new QPushButton("Filter", this);
    filterByTagsBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: #ffffff; border: none; padding: 6px 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8fd9; }"
        "QPushButton:disabled { background-color: #333; color: #666; }"
    );
    filterByTagsBtn->setToolTip("Filter assets by selected tags");
    filterByTagsBtn->setEnabled(false);
    connect(filterByTagsBtn, &QPushButton::clicked, this, &MainWindow::onFilterByTags);
    tagButtonsLayout->addWidget(filterByTagsBtn);

    // AND/OR mode selector
    tagFilterModeCombo = new QComboBox(this);
    tagFilterModeCombo->addItems({"AND", "OR"});
    tagFilterModeCombo->setCurrentIndex(0); // Default to AND
    tagFilterModeCombo->setStyleSheet(
        "QComboBox { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; padding: 4px 8px; border-radius: 4px; }"
    );
    qDebug() << "[INIT] Tag buttons and mode added";
    tagFilterModeCombo->setToolTip("AND: Assets must have ALL selected tags\nOR: Assets must have ANY selected tag");
    tagButtonsLayout->addWidget(tagFilterModeCombo);

    filtersLayout->addLayout(tagButtonsLayout);

    QPushButton *applyFiltersBtn = new QPushButton("Apply Filters", this);
    applyFiltersBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: #ffffff; border: none; padding: 8px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8fd9; }"
    );
    connect(applyFiltersBtn, &QPushButton::clicked, this, &MainWindow::applyFilters);
    filtersLayout->addWidget(applyFiltersBtn);

    QPushButton *clearFiltersBtn = new QPushButton("Clear Filters", this);
    clearFiltersBtn->setStyleSheet(
        "QPushButton { background-color: #333; color: #ffffff; border: none; padding: 8px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #444; }"
    );
    connect(clearFiltersBtn, &QPushButton::clicked, this, &MainWindow::clearFilters);
    filtersLayout->addWidget(clearFiltersBtn);

    filtersLayout->addStretch();
    filtersPanel->setStyleSheet("background-color: #121212;");

    // Info panel with scrollable area for all metadata
    infoPanel = new QWidget(this);
    QVBoxLayout *infoPanelLayout = new QVBoxLayout(infoPanel);
    infoPanelLayout->setContentsMargins(0, 0, 0, 0);
    infoPanelLayout->setSpacing(0);

    QLabel *infoTitle = new QLabel("Asset Info", this);
    infoTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #ffffff; padding: 8px; background-color: #1a1a1a;");
    infoPanelLayout->addWidget(infoTitle);

    // Scrollable area for metadata
    QScrollArea *infoScrollArea = new QScrollArea(this);
    infoScrollArea->setWidgetResizable(true);
    infoScrollArea->setFrameShape(QFrame::NoFrame);
    infoScrollArea->setStyleSheet("QScrollArea { background-color: #121212; border: none; }");

    QWidget *infoScrollWidget = new QWidget();
    QVBoxLayout *infoLayout = new QVBoxLayout(infoScrollWidget);
    infoLayout->setContentsMargins(8, 8, 8, 8);
    infoLayout->setSpacing(4);

    infoFileName = new QLabel("No selection", this);
    infoFileName->setStyleSheet("color: #ffffff; margin-top: 4px; font-weight: bold;");
    infoFileName->setWordWrap(true);
    infoLayout->addWidget(infoFileName);

    infoFilePath = new QLabel("", this);
    infoFilePath->setStyleSheet("color: #999; font-size: 10px;");
    infoFilePath->setWordWrap(true);
    infoLayout->addWidget(infoFilePath);

    // Add separator
    QFrame *separator1 = new QFrame(this);
    separator1->setFrameShape(QFrame::HLine);
    separator1->setStyleSheet("background-color: #333;");
    separator1->setFixedHeight(1);
    infoLayout->addWidget(separator1);

    infoFileSize = new QLabel("", this);
    infoFileSize->setStyleSheet("color: #ccc; font-size: 11px;");
    infoFileSize->setWordWrap(true);
    infoLayout->addWidget(infoFileSize);

    infoFileType = new QLabel("", this);
    infoFileType->setStyleSheet("color: #ccc; font-size: 11px;");
    infoFileType->setWordWrap(true);
    infoLayout->addWidget(infoFileType);

    infoDimensions = new QLabel("", this);
    infoDimensions->setStyleSheet("color: #ccc; font-size: 11px;");
    infoDimensions->setWordWrap(true);
    infoLayout->addWidget(infoDimensions);

    infoCreated = new QLabel("", this);
    infoCreated->setStyleSheet("color: #ccc; font-size: 11px;");
    infoCreated->setWordWrap(true);
    infoLayout->addWidget(infoCreated);

    infoModified = new QLabel("", this);
    infoModified->setStyleSheet("color: #ccc; font-size: 11px;");
    infoModified->setWordWrap(true);
    infoLayout->addWidget(infoModified);

    infoPermissions = new QLabel("", this);
    infoPermissions->setStyleSheet("color: #ccc; font-size: 11px;");
    infoPermissions->setWordWrap(true);
    infoLayout->addWidget(infoPermissions);

    // Rating widget
    QFrame *separator2 = new QFrame(this);
    separator2->setFrameShape(QFrame::HLine);
    separator2->setStyleSheet("background-color: #333;");
    separator2->setFixedHeight(1);
    infoLayout->addWidget(separator2);

    infoRatingLabel = new QLabel("Rating:", this);
    infoRatingLabel->setStyleSheet("color: #ccc; margin-top: 4px; font-size: 11px;");
    infoLayout->addWidget(infoRatingLabel);

    infoRatingWidget = new StarRatingWidget(this);
    infoLayout->addWidget(infoRatingWidget);
    connect(infoRatingWidget, &StarRatingWidget::ratingChanged, this, &MainWindow::onRatingChanged);

    infoTags = new QLabel("", this);
    infoTags->setStyleSheet("color: #ccc; margin-top: 4px; font-size: 11px;");
    infoTags->setWordWrap(true);
    infoLayout->addWidget(infoTags);

    // Separator before versions
    QFrame *separator3 = new QFrame(this);
    separator3->setFrameShape(QFrame::HLine);
    separator3->setStyleSheet("background-color: #333;");
    separator3->setFixedHeight(1);
    infoLayout->addWidget(separator3);

    // Version history section
    versionsTitleLabel = new QLabel("Version History", this);
    versionsTitleLabel->setStyleSheet("font-size: 13px; font-weight: bold; color: #ffffff; margin-top: 6px;");
    infoLayout->addWidget(versionsTitleLabel);

    versionTable = new QTableWidget(this);
    versionTable->setColumnCount(5);
    QStringList headers; headers << "" << "Version" << "Date" << "Size" << "Notes";
    versionTable->setHorizontalHeaderLabels(headers);
    versionTable->verticalHeader()->setVisible(false);
    versionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    versionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    versionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    versionTable->setShowGrid(false);
    versionTable->setStyleSheet(
        "QTableWidget { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; }"
        "QHeaderView::section { background-color: #1a1a1a; color: #ffffff; border: none; padding: 4px; }"
    );
    versionTable->setIconSize(QSize(48, 48));
    // Persist versionTable column widths immediately when resized
    connect(versionTable->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logical, int /*oldSize*/, int newSize){
        QSettings s("AugmentCode", "KAssetManager");
        s.setValue(QString("AssetManager/VersionTable/Col%1").arg(logical), newSize);
    });

    versionTable->setMaximumHeight(220);
    versionTable->setColumnWidth(0, 56);
    versionTable->setColumnWidth(1, 70);
    versionTable->setColumnWidth(2, 150);
    versionTable->setColumnWidth(3, 90);
    versionTable->horizontalHeader()->setStretchLastSection(true);
    infoLayout->addWidget(versionTable);

    QHBoxLayout *versionButtonsLayout = new QHBoxLayout();
    backupVersionCheck = new QCheckBox("Backup current version", this);
    backupVersionCheck->setChecked(true);
    backupVersionCheck->setStyleSheet("color: #ccc;");
    revertVersionButton = new QPushButton("Revert to Selected", this);
    revertVersionButton->setStyleSheet(
        "QPushButton { background-color: #d9534f; color: #ffffff; border: none; padding: 6px 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #c9302c; }"
        "QPushButton:disabled { background-color: #333; color: #666; }"
    );
    revertVersionButton->setEnabled(false);
    connect(revertVersionButton, &QPushButton::clicked, this, &MainWindow::onRevertSelectedVersion);
    connect(versionTable->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](const QItemSelection&, const QItemSelection&){
        revertVersionButton->setEnabled(versionTable->currentRow() >= 0);
    });
    versionButtonsLayout->addWidget(backupVersionCheck);
    versionButtonsLayout->addStretch();
    versionButtonsLayout->addWidget(revertVersionButton);
    infoLayout->addLayout(versionButtonsLayout);

    infoLayout->addStretch();
    infoScrollWidget->setLayout(infoLayout);
    infoScrollArea->setWidget(infoScrollWidget);
    infoPanelLayout->addWidget(infoScrollArea);
    infoPanel->setStyleSheet("background-color: #121212;");

    rightLayout->addWidget(filtersPanel, 1);
    rightLayout->addWidget(infoPanel, 1);

    // Add panels to main splitter
    mainSplitter->addWidget(leftPanel);
    mainSplitter->addWidget(centerPanel);
    mainSplitter->addWidget(rightPanel);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 3);
    mainSplitter->setStretchFactor(2, 1);

    // Add Asset Manager page to tabs
    // File Manager page
    fileManagerPage = new QWidget(this);
    setupFileManagerUi();
    mainTabs->addTab(fileManagerPage, "File Manager");

    // Add Asset Manager page to tabs
    mainTabs->addTab(assetManagerPage, "Asset Manager");

    // Log viewer as dock widget at bottom (hidden by default)
    QDockWidget* logDock = new QDockWidget("Application Log", this);
    LogManager::instance().addLog("[TRACE] logDock created", "DEBUG");
    logDock->setAllowedAreas(Qt::BottomDockWidgetArea);
    logDock->setFeatures(QDockWidget::DockWidgetClosable);
    logViewerWidget = new LogViewerWidget(logDock);
    logDock->setWidget(logViewerWidget);
    logDock->setStyleSheet(
        "QDockWidget { background-color: #121212; color: #ffffff; }"
        "QDockWidget::title { background-color: #1a1a1a; padding: 4px; }"
    );
    addDockWidget(Qt::BottomDockWidgetArea, logDock);
    logDock->hide(); // Hidden by default
    LogManager::instance().addLog("[TRACE] logDock initialised", "DEBUG");

    // Connect dock visibility to menu action
    connect(logDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        toggleLogViewerAction->setChecked(visible);
        if (visible) {
            toggleLogViewerAction->setText("Hide &Log Viewer");
        } else {
            toggleLogViewerAction->setText("Show &Log Viewer");
        }
    });
    LogManager::instance().addLog("[TRACE] logDock visibility hook set", "DEBUG");


    // Restore window and workspace state
    {
        QSettings s("AugmentCode", "KAssetManager");
        LogManager::instance().addLog("[TRACE] restore settings begin", "DEBUG");
        if (s.contains("Window/Geometry")) restoreGeometry(s.value("Window/Geometry").toByteArray());
        if (s.contains("Window/State")) restoreState(s.value("Window/State").toByteArray());
        LogManager::instance().addLog("[TRACE] restore window geometry/state done", "DEBUG");
        if (mainSplitter && s.contains("AssetManager/MainSplitter")) mainSplitter->restoreState(s.value("AssetManager/MainSplitter").toByteArray());
        LogManager::instance().addLog("[TRACE] restore mainSplitter state done", "DEBUG");
        if (rightSplitter && s.contains("AssetManager/RightSplitter")) rightSplitter->restoreState(s.value("AssetManager/RightSplitter").toByteArray());
        LogManager::instance().addLog("[TRACE] restore rightSplitter state done", "DEBUG");
        if (s.contains("AssetManager/ViewMode")) {
            bool grid = s.value("AssetManager/ViewMode").toBool();
            LogManager::instance().addLog(QString("[TRACE] restore view mode flag: %1").arg(grid), "DEBUG");
        if (versionTable) {
            auto hh = versionTable->horizontalHeader();
            for (int c = 0; c < versionTable->columnCount(); ++c) {
                QVariant v = s.value(QString("AssetManager/VersionTable/Col%1").arg(c));
                if (v.isValid()) hh->resizeSection(c, v.toInt());
            }
        }
            LogManager::instance().addLog("[TRACE] restored version table columns", "DEBUG");

            isGridMode = grid;
            viewStack->setCurrentIndex(grid ? 0 : 1);
            viewModeButton->setIcon(grid ? icoGrid() : icoList());
            thumbnailSizeSlider->setEnabled(grid);
            LogManager::instance().addLog("[TRACE] applied view mode toggle", "DEBUG");
        }
        LogManager::instance().addLog("[TRACE] restore asset manager view", "DEBUG");
        if (assetTableView && assetTableView->model()) {
            auto hh = assetTableView->horizontalHeader();
            for (int c = 0; c < assetTableView->model()->columnCount(); ++c) {
                QVariant v = s.value(QString("AssetManager/AssetTable/Col%1").arg(c));
                if (v.isValid()) hh->resizeSection(c, v.toInt());
            }
        }
        LogManager::instance().addLog("[TRACE] restore asset table columns", "DEBUG");
    }
    LogManager::instance().addLog("[TRACE] window state restored", "DEBUG");

    // Load initial data
    folderModel->reload();
    tagsModel->reload();

    // Restore last active tab
    int lastTab = ContextPreserver::instance().loadLastActiveTab();
    if (mainTabs && lastTab >= 0 && lastTab < mainTabs->count()) {
        mainTabs->setCurrentIndex(lastTab);
    }

    // Restore last active folder or select first folder
    int lastFolderId = ContextPreserver::instance().loadLastActiveFolder();
    bool folderRestored = false;

    if (lastFolderId > 0) {
        // Try to find and select the last active folder
        QModelIndex lastFolderIndex = folderModel->findIndexById(lastFolderId);
        if (lastFolderIndex.isValid()) {
            folderTreeView->setCurrentIndex(lastFolderIndex);
            onFolderSelected(lastFolderIndex);
            folderRestored = true;
            qDebug() << "[ContextPreserver] Restored last active folder:" << lastFolderId;
        }
    }

    // Fallback to first folder if restoration failed
    if (!folderRestored && folderModel->rowCount(QModelIndex()) > 0) {
        QModelIndex firstFolder = folderModel->index(0, 0, QModelIndex());
        folderTreeView->setCurrentIndex(firstFolder);
        onFolderSelected(firstFolder);
    }

    LogManager::instance().addLog("[TRACE] mainwindow ctor finished", "DEBUG");

    // Schedule database health check on startup (delayed to avoid blocking UI)
    QTimer::singleShot(2000, this, &MainWindow::performStartupHealthCheck);
}

void MainWindow::performStartupHealthCheck()
{
    DatabaseHealthAgent& agent = DatabaseHealthAgent::instance();
    DatabaseStats stats = agent.getDatabaseStats();

    // Check if VACUUM is recommended
    if (agent.shouldVacuum()) {
        QString recommendation = agent.getVacuumRecommendation();

        // Show notification in status bar
        statusBar()->showMessage(QString("Database maintenance recommended: %1").arg(recommendation), 10000);

        // Log the recommendation
        qInfo() << "[DatabaseHealth] Startup check:" << recommendation;
    }

    // Check for critical issues (orphaned records, missing files)
    if (stats.orphanedAssets > 0 || stats.missingFiles > 10) {
        QString message = QString("Database health issues detected: ");
        if (stats.orphanedAssets > 0) {
            message += QString("%1 orphaned asset(s) ").arg(stats.orphanedAssets);
        }
        if (stats.missingFiles > 10) {
            message += QString("%1 missing file(s) ").arg(stats.missingFiles);
        }
        message += "- Open Tools > Database Health to review.";

        statusBar()->showMessage(message, 15000);
        qWarning() << "[DatabaseHealth]" << message;
    }
}

void MainWindow::setupFileManagerUi()
{
    // Splitter: left (tree) | right (view)
    fmSplitter = new QSplitter(Qt::Horizontal, fileManagerPage);

    // Left: Favorites (top) | Folder tree (bottom) in a vertical splitter
    QWidget *left = new QWidget(fmSplitter);
    QVBoxLayout *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0,0,0,0);
    leftLayout->setSpacing(0);

    fmTreeModel = new QFileSystemModel(left);
    fmTreeModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Drives);

    fmLeftSplitter = new QSplitter(Qt::Vertical, left);

    // Favorites container
    QWidget *favContainer = new QWidget(fmLeftSplitter);
    QVBoxLayout *favLayout = new QVBoxLayout(favContainer);
    favLayout->setContentsMargins(0,0,0,0);
    favLayout->setSpacing(0);
    QLabel *favHeader = new QLabel("★ Favorites", favContainer);
    favHeader->setStyleSheet("color:#9aa0a6; font-weight:bold; padding:6px 4px;");
    favLayout->addWidget(favHeader);

    fmFavoritesList = new QListWidget(favContainer);
    fmFavoritesList->setStyleSheet("QListWidget{background:#0a0a0a; border:none; color:#fff;} QListWidget::item:selected{background:#2f3a4a;}");
    fmFavoritesList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fmFavoritesList, &QListWidget::itemDoubleClicked, this, &MainWindow::onFmFavoriteActivated);
    connect(fmFavoritesList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos){
        if (!fmFavoritesList) return;
        QPoint gp = fmFavoritesList->viewport()->mapToGlobal(pos);
        QMenu m; QAction *rem = m.addAction("Remove Favorite", this, &MainWindow::onFmRemoveFavorite);
        rem->setEnabled(fmFavoritesList->currentItem()!=nullptr);
        m.exec(gp);
    });
    favLayout->addWidget(fmFavoritesList);
    loadFmFavorites();

    // Folder tree
    fmTreeModel->setRootPath(""); // show drives at root
    fmTree = new QTreeView(fmLeftSplitter);

    fmTree->setModel(fmTreeModel);
    fmTree->setHeaderHidden(false);
    fmTree->header()->setStretchLastSection(true);
    fmTree->header()->setSectionResizeMode(QHeaderView::Interactive);
    // Persist fmTree column widths immediately when resized
    connect(fmTree->header(), &QHeaderView::sectionResized, this, [this](int logical, int /*oldSize*/, int newSize){
        QSettings s("AugmentCode", "KAssetManager");
        s.setValue(QString("FileManager/Tree/Col%1").arg(logical), newSize);
    });

    fmTree->setContextMenuPolicy(Qt::CustomContextMenu);
    fmTree->setExpandsOnDoubleClick(true);
    fmTree->setSelectionMode(QAbstractItemView::SingleSelection);
    fmTree->setStyleSheet(
        "QTreeView { background-color: #121212; color: #ffffff; border: none; }"
        "QTreeView::item:selected { background-color: #2f3a4a; color: #ffffff; }"
        "QHeaderView::section { background-color: #1a1a1a; color: #ffffff; border: none; padding: 4px; }"
    );
    // set root to the "Computer" level
    // Navigate on single click; keep double-click for expand/collapse
    connect(fmTree, &QTreeView::clicked, this, &MainWindow::onFmTreeActivated);
    connect(fmTree, &QTreeView::customContextMenuRequested, this, &MainWindow::onFmTreeContextMenu);
    // Enable drag and drop on folder tree
    fmTree->setDragEnabled(true);
    fmTree->setAcceptDrops(true);
    fmTree->setDropIndicatorShown(true);
    fmTree->setDragDropMode(QAbstractItemView::DragDrop);
    fmTree->viewport()->installEventFilter(this);

    fmTree->setRootIndex(fmTreeModel->index(fmTreeModel->rootPath()));

    // Add to left layout
    leftLayout->addWidget(fmLeftSplitter);

    // Right: toolbar + stacked views (grid/list)
    QWidget *right = new QWidget(fmSplitter);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0,0,0,0);
    rightLayout->setSpacing(0);

    // Toolbar
    fmToolbar = new QWidget(right);
    fmToolbar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fmToolbar->setFixedHeight(40);

    QHBoxLayout *tb = new QHBoxLayout(fmToolbar);
    tb->setContentsMargins(8,4,8,4);
    tb->setSpacing(6);

    fmIsGridMode = true;

    auto mkTb = [&](const QIcon &ic, const QString &tip) {
        QToolButton *b = new QToolButton(fmToolbar);
        b->setIcon(ic); b->setToolTip(tip); b->setAutoRaise(true); b->setIconSize(QSize(20,20));
        return b;
    };

    // Left-aligned: New Folder, Copy, Cut, Paste, Delete, Rename, Add to Library, List/Grid Toggle, Grid Size bar, Group Sequences
    QToolButton *newFolderBtn = mkTb(icoFolderNew(), "New Folder");
    connect(newFolderBtn, &QToolButton::clicked, this, &MainWindow::onFmNewFolder);
    tb->addWidget(newFolderBtn);

    QToolButton *copyBtn = mkTb(icoCopy(), "Copy");       connect(copyBtn, &QToolButton::clicked, this, &MainWindow::onFmCopy); tb->addWidget(copyBtn);
    QToolButton *cutBtn = mkTb(icoCut(), "Cut");          connect(cutBtn, &QToolButton::clicked, this, &MainWindow::onFmCut); tb->addWidget(cutBtn);
    QToolButton *pasteBtn = mkTb(icoPaste(), "Paste");    connect(pasteBtn, &QToolButton::clicked, this, &MainWindow::onFmPaste); tb->addWidget(pasteBtn);
    QToolButton *deleteBtn = mkTb(icoDelete(), "Delete"); connect(deleteBtn, &QToolButton::clicked, this, &MainWindow::onFmDelete); tb->addWidget(deleteBtn);
    QToolButton *renameBtn = mkTb(icoRename(), "Rename"); connect(renameBtn, &QToolButton::clicked, this, &MainWindow::onFmRename); tb->addWidget(renameBtn);

    QToolButton *addToLibraryBtn = mkTb(icoAdd(), "Add to Library");
    connect(addToLibraryBtn, &QToolButton::clicked, this, &MainWindow::onAddSelectionToAssetLibrary);
    tb->addWidget(addToLibraryBtn);

    fmViewModeButton = new QToolButton(fmToolbar);
    fmViewModeButton->setIcon(icoGrid());
    fmViewModeButton->setToolTip("Toggle Grid/List");
    fmViewModeButton->setAutoRaise(true);
    fmViewModeButton->setIconSize(QSize(20,20));
    connect(fmViewModeButton, &QToolButton::clicked, this, &MainWindow::onFmViewModeToggled);
    tb->addWidget(fmViewModeButton);

    // Thumbnail size slider (File Manager)
    QLabel *fmSizeLbl = new QLabel("Size:", fmToolbar); fmSizeLbl->setStyleSheet("color:#9aa0a6;"); tb->addWidget(fmSizeLbl);
    fmThumbnailSizeSlider = new QSlider(Qt::Horizontal, fmToolbar);
    fmThumbnailSizeSlider->setRange(64, 320);
    fmThumbnailSizeSlider->setFixedWidth(140);
    fmThumbnailSizeSlider->setToolTip("Adjust thumbnail size");
    tb->addWidget(fmThumbnailSizeSlider);
    connect(fmThumbnailSizeSlider, &QSlider::valueChanged, this, &MainWindow::onFmThumbnailSizeChanged);

    // Right-aligned controls
    tb->addStretch();

    fmGroupSequencesCheckBox = new QCheckBox("Group sequences", fmToolbar);
    fmGroupSequencesCheckBox->setToolTip("Group image sequences into single entries");
    fmGroupSequencesCheckBox->setStyleSheet("QCheckBox { color:#9aa0a6; } QCheckBox::indicator { width: 16px; height: 16px; }");
    connect(fmGroupSequencesCheckBox, &QCheckBox::toggled, this, &MainWindow::onFmGroupSequencesToggled);
    tb->addWidget(fmGroupSequencesCheckBox);

    fmPreviewToggleButton = new QToolButton(fmToolbar);
    fmPreviewToggleButton->setIcon(icoEye());
    fmPreviewToggleButton->setToolTip("Show/Hide preview panel");
    fmPreviewToggleButton->setCheckable(true);
    fmPreviewToggleButton->setChecked(true);
    fmPreviewToggleButton->setAutoRaise(true);
    fmPreviewToggleButton->setIconSize(QSize(20,20));
    connect(fmPreviewToggleButton, &QToolButton::toggled, this, &MainWindow::onFmTogglePreview);
    tb->addWidget(fmPreviewToggleButton);
    rightLayout->addWidget(fmToolbar);

    // Models/views
    fmViewStack = new QStackedWidget(right);

    fmDirModel = new QFileSystemModel(fmViewStack);
    fmDirModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
    fmDirModel->setRootPath("");
    fmDirModel->setIconProvider(new FmIconProvider());

    // Grid view
    fmGridView = new QListView(fmViewStack);
    // Sequence grouping proxy
    fmProxyModel = new SequenceGroupingProxyModel(fmViewStack);
    fmProxyModel->setSourceModel(fmDirModel);

    fmGridView->setModel(fmProxyModel);
    fmGridView->setViewMode(QListView::IconMode);
    fmGridView->setResizeMode(QListView::Adjust);
    fmGridView->setSpacing(4);
    fmGridView->setUniformItemSizes(true);
    fmGridView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fmGridView->setContextMenuPolicy(Qt::CustomContextMenu);

    // Minimalist delegate to remove cell color separation
    {
        auto *d = new FmItemDelegate(fmGridView);
        fmGridView->setItemDelegate(d);
        // Restore thumbnail size from settings (default 120)
        QSettings s("AugmentCode", "KAssetManager");
        int fmThumb = s.value("FileManager/GridThumbSize", 120).toInt();
        d->setThumbnailSize(fmThumb);
        fmGridView->setIconSize(QSize(fmThumb, fmThumb));
        fmGridView->setGridSize(QSize(fmThumb + 24, fmThumb + 40));
        if (fmThumbnailSizeSlider) fmThumbnailSizeSlider->setValue(fmThumb);
    }
    fmGridView->setStyleSheet("QListView { background-color: #0a0a0a; border: none; }");
    fmGridView->setDragEnabled(true);
    fmGridView->setAcceptDrops(true);
    fmGridView->setDropIndicatorShown(true);
    fmGridView->setDragDropMode(QAbstractItemView::DragDrop);
    fmGridView->setDefaultDropAction(Qt::CopyAction);
    if (fmGridView->viewport()) fmGridView->viewport()->installEventFilter(this);
    connect(fmGridView, &QListView::doubleClicked, this, &MainWindow::onFmItemDoubleClicked);
    fmViewStack->addWidget(fmGridView); // 0

    fmScrubController = new GridScrubController(
        fmGridView,
        [this](const QModelIndex& idx) -> QString {
            if (!fmDirModel) {
                return QString();
            }
            QModelIndex srcIdx = idx;
            if (fmProxyModel && idx.model() == fmProxyModel) {
                srcIdx = fmProxyModel->mapToSource(idx);
            }
            if (!srcIdx.isValid()) {
                return QString();
            }
            if (fmDirModel->isDir(srcIdx)) {
                return QString();
            }
            return fmDirModel->filePath(srcIdx);
        },
        this);
    LogManager::instance().addLog("[TRACE] fmScrubController ready", "DEBUG");

    // List view
    fmListView = new QTableView(fmViewStack);
    fmListView->setModel(fmProxyModel);
    LogManager::instance().addLog("[TRACE] fmListView created", "DEBUG");
    // Persist fmListView column widths immediately when resized
    connect(fmListView->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int logical, int /*oldSize*/, int newSize){
        QSettings s("AugmentCode", "KAssetManager");
        s.setValue(QString("FileManager/ListView/Col%1").arg(logical), newSize);
    });

    fmListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    fmListView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fmListView->setSortingEnabled(true);
    fmListView->setAlternatingRowColors(false);
    fmListView->setShowGrid(false);
    fmListView->verticalHeader()->setVisible(false);
    fmListView->verticalHeader()->setDefaultSectionSize(22);
    fmListView->verticalHeader()->setMinimumSectionSize(18);
    fmListView->setIconSize(QSize(18,18));
    fmListView->horizontalHeader()->setStretchLastSection(true);
    fmListView->setStyleSheet(
        "QTableView { background-color: #0a0a0a; color: #ffffff; border: none; }"
        "QTableView::item { padding: 2px 6px; }"
        "QTableView::item:selected { background-color: #2f3a4a; }"
        "QHeaderView::section { background-color: #1a1a1a; color: #ffffff; border: none; padding: 4px; }"
    );
    fmListView->setDragEnabled(true);
    fmListView->setAcceptDrops(true);
    fmListView->setDropIndicatorShown(true);
    fmListView->setDragDropMode(QAbstractItemView::DragDrop);
    fmListView->setDefaultDropAction(Qt::CopyAction);
    if (fmListView->viewport()) fmListView->viewport()->installEventFilter(this);
    connect(fmListView, &QTableView::doubleClicked, this, &MainWindow::onFmItemDoubleClicked);
    fmViewStack->addWidget(fmListView); // 1
    LogManager::instance().addLog("[TRACE] fmListView wired", "DEBUG");

    fmViewStack->setCurrentIndex(0);
    LogManager::instance().addLog("[TRACE] fmViewStack initialised", "DEBUG");

    // Right-side splitter: views | preview panel
    fmRightSplitter = new QSplitter(Qt::Horizontal, right);
    LogManager::instance().addLog("[TRACE] fmRightSplitter created", "DEBUG");
    QWidget *viewContainer = new QWidget(fmRightSplitter);
    QVBoxLayout *viewContainerLayout = new QVBoxLayout(viewContainer);
    viewContainerLayout->setContentsMargins(0,0,0,0);
    viewContainerLayout->setSpacing(0);
    viewContainerLayout->addWidget(fmViewStack);
    LogManager::instance().addLog("[TRACE] fm view container ready", "DEBUG");

    // Preview panel (embedded)
    fmPreviewPanel = new QWidget(fmRightSplitter);
    fmPreviewPanel->setMinimumWidth(260);
    fmPreviewPanel->setStyleSheet("background-color:#0e0e0e; border-left:1px solid #222;");
    QVBoxLayout *pv = new QVBoxLayout(fmPreviewPanel);
    pv->setContentsMargins(8,8,8,8);
    pv->setSpacing(6);
    QLabel *pvTitle = new QLabel("Preview", fmPreviewPanel);
    pvTitle->setStyleSheet("color:#9aa0a6; font-weight:bold;");
    pv->addWidget(pvTitle);
    LogManager::instance().addLog("[TRACE] fm preview header ready", "DEBUG");

    // Image view with zoom/pan
    fmImageScene = new QGraphicsScene(fmPreviewPanel);
    fmImageItem = new QGraphicsPixmapItem();
    fmImageScene->addItem(fmImageItem);
    fmImageView = new QGraphicsView(fmImageScene, fmPreviewPanel);
    fmImageView->setDragMode(QGraphicsView::ScrollHandDrag);
    fmImageView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    fmImageView->setMinimumHeight(160);
    fmImageView->setStyleSheet("background:#090909; border:1px solid #222;");
    fmImageView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    fmImageView->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    fmImageView->setAlignment(Qt::AlignCenter);
    fmImageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // Additional preview widgets (hidden by default)
    fmTextView = new QPlainTextEdit(fmPreviewPanel);
    fmTextView->setReadOnly(true);
    fmTextView->setWordWrapMode(QTextOption::NoWrap);
    fmTextView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    // Ensure white background and black text for text/DOCX previews
    fmTextView->setStyleSheet("QPlainTextEdit { background-color: #ffffff; color: #000000; border: none; }");
    fmTextView->hide();

    fmCsvModel = new QStandardItemModel(fmPreviewPanel);
    fmCsvView = new QTableView(fmPreviewPanel);
    fmCsvView->setModel(fmCsvModel);
    fmCsvView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fmCsvView->setSelectionMode(QAbstractItemView::NoSelection);
    fmCsvView->setAlternatingRowColors(true);
    // Ensure white background and black text for CSV/XLSX previews
    fmCsvView->setStyleSheet(
        "QTableView { background-color: #ffffff; color: #000000; gridline-color: #cccccc; border: none; }"
        "QHeaderView::section { background-color: #f0f0f0; color: #000000; border: none; padding: 4px; }"
    );
    fmCsvView->hide();

#ifdef HAVE_QT_PDF
    fmPdfDoc = new QPdfDocument(fmPreviewPanel);
#endif
#ifdef HAVE_QT_PDF_WIDGETS
    fmPdfView = new QPdfView(fmPreviewPanel);
    fmPdfView->setPageMode(QPdfView::PageMode::SinglePage);
    fmPdfView->setDocument(fmPdfDoc);
    fmPdfView->hide();
#endif

    fmSvgScene = new QGraphicsScene(fmPreviewPanel);
    fmSvgItem = nullptr;
    fmSvgView = new QGraphicsView(fmSvgScene, fmPreviewPanel);
    fmSvgView->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    fmSvgView->setAlignment(Qt::AlignCenter);
    fmSvgView->hide();

    fmImageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    fmImageView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    fmImageView->viewport()->installEventFilter(this);
    fmImageView->installEventFilter(this);



    // Alpha toggle row (for images with alpha)
    QHBoxLayout* alphaRow = new QHBoxLayout();
    fmAlphaCheck = new QCheckBox("Alpha", fmPreviewPanel);
    fmAlphaCheck->setToolTip("Show alpha channel (grayscale)");
    fmAlphaCheck->hide();
    connect(fmAlphaCheck, &QCheckBox::toggled, this, [this](bool on){
        fmAlphaOnlyMode = on;
        if (!fmOriginalImage.isNull() && fmImageItem) {
            QImage disp = fmOriginalImage;
            if (fmAlphaOnlyMode && disp.hasAlphaChannel()) {
                QImage a(disp.size(), QImage::Format_Grayscale8);
                for (int y=0;y<disp.height();++y){
                    const uchar* al = disp.constScanLine(y);
                    // convert alpha channel quickly by reading from pixel's alpha
                    for (int x=0;x<disp.width();++x){
                        uchar alpha = qAlpha(reinterpret_cast<const QRgb*>(disp.constScanLine(y))[x]);
                        a.scanLine(y)[x] = alpha;
                    }
                }
                disp = a.convertToFormat(QImage::Format_Grayscale8);
            }
            fmImageItem->setPixmap(QPixmap::fromImage(disp));
            if (fmImageFitToView) fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
        }
    });
    alphaRow->addWidget(fmAlphaCheck);
    alphaRow->addStretch();

    // PDF page controls (hidden by default)
    QHBoxLayout* docc = new QHBoxLayout();
    fmPdfPrevBtn = new QToolButton(fmPreviewPanel);
    fmPdfPrevBtn->setText("◀");
    fmPdfNextBtn = new QToolButton(fmPreviewPanel);
    fmPdfNextBtn->setText("▶");
    fmPdfPageLabel = new QLabel("--/--", fmPreviewPanel);
    docc->addWidget(fmPdfPrevBtn);
    docc->addWidget(fmPdfPageLabel);
    docc->addWidget(fmPdfNextBtn);
    docc->addStretch();
    fmPdfPrevBtn->hide(); fmPdfNextBtn->hide(); fmPdfPageLabel->hide();
#if defined(HAVE_QT_PDF)
    connect(fmPdfPrevBtn, &QToolButton::clicked, this, [this]{
        bool handled = false;
        #ifdef HAVE_QT_PDF
        if (fmPdfDoc && fmPdfDoc->pageCount() > 0) {
            handled = true;
            if (fmPdfCurrentPage > 0) fmPdfCurrentPage--;
            const QSizeF pts = fmPdfDoc->pagePointSize(fmPdfCurrentPage);
            int vw = fmImageView ? fmImageView->viewport()->width() : 800;
            if (vw < 1) vw = 800;
            int w = vw;
            int h = pts.width() > 0 ? int(pts.height() * (w / pts.width())) : w;
            QImage img = fmPdfDoc->render(fmPdfCurrentPage, QSize(w, h));
            if (!img.isNull() && fmImageItem) {
                // Composite onto white to avoid dark theme bleeding through
                if (img.hasAlphaChannel()) {
                    QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied);
                    bg.fill(Qt::white);
                    QPainter p(&bg);
                    p.drawImage(0, 0, img);
                    p.end();
                    img = bg;
                }
                fmImageItem->setPixmap(QPixmap::fromImage(img));
                if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                if (fmImageView) {
                    fmImageView->resetTransform();
                    fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                    fmImageFitToView = true;
                    fmImageView->setBackgroundBrush(Qt::white);
                    fmImageView->show();
                }
            }
            if (fmPdfPageLabel) fmPdfPageLabel->setText(QString("%1/%2").arg(fmPdfCurrentPage+1).arg(fmPdfDoc->pageCount()));
        }
        #endif
    });
    connect(fmPdfNextBtn, &QToolButton::clicked, this, [this]{
        bool handled = false;
        #ifdef HAVE_QT_PDF
        if (fmPdfDoc && fmPdfDoc->pageCount() > 0) {
            handled = true;
            if (fmPdfCurrentPage + 1 < fmPdfDoc->pageCount()) fmPdfCurrentPage++;
            const QSizeF pts = fmPdfDoc->pagePointSize(fmPdfCurrentPage);
            int vw = fmImageView ? fmImageView->viewport()->width() : 800;
            if (vw < 1) vw = 800;
            int w = vw;
            int h = pts.width() > 0 ? int(pts.height() * (w / pts.width())) : w;
            QImage img = fmPdfDoc->render(fmPdfCurrentPage, QSize(w, h));
            if (!img.isNull() && fmImageItem) {
                if (img.hasAlphaChannel()) {
                    QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied);
                    bg.fill(Qt::white);
                    QPainter p(&bg);
                    p.drawImage(0, 0, img);
                    p.end();
                    img = bg;
                }
                fmImageItem->setPixmap(QPixmap::fromImage(img));
                if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                if (fmImageView) {
                    fmImageView->resetTransform();
                    fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                    fmImageFitToView = true;
                    fmImageView->setBackgroundBrush(Qt::white);
                    fmImageView->show();
                }
            }
            if (fmPdfPageLabel) fmPdfPageLabel->setText(QString("%1/%2").arg(fmPdfCurrentPage+1).arg(fmPdfDoc->pageCount()));
        }
        #endif
    });
#endif

    pv->addLayout(alphaRow);

    fmVideoWidget = new QVideoWidget(fmPreviewPanel);
    fmVideoWidget->setMinimumHeight(160);
    fmVideoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    fmVideoWidget->hide();

    fmMediaPlayer = new QMediaPlayer(fmPreviewPanel);
    fmAudioOutput = new QAudioOutput(fmPreviewPanel);
    fmMediaPlayer->setAudioOutput(fmAudioOutput);
    fmMediaPlayer->setVideoOutput(fmVideoWidget);

    // Simple media controls
    QHBoxLayout *mc = new QHBoxLayout();
    fmPlayPauseBtn = new QPushButton("Play", fmPreviewPanel);
    fmPositionSlider = new QSlider(Qt::Horizontal, fmPreviewPanel);
    fmPositionSlider->setMinimum(0); fmPositionSlider->setMaximum(1000);
    fmTimeLabel = new QLabel("00:00 / 00:00", fmPreviewPanel);
    fmVolumeSlider = new QSlider(Qt::Horizontal, fmPreviewPanel);
    fmVolumeSlider->setRange(0, 100); fmVolumeSlider->setValue(50);
    mc->addWidget(fmPlayPauseBtn); mc->addWidget(fmPositionSlider); mc->addWidget(fmTimeLabel); mc->addWidget(fmVolumeSlider);

    connect(fmPlayPauseBtn, &QPushButton::clicked, this, [this]{
        if (!fmMediaPlayer) return; if (fmMediaPlayer->playbackState() == QMediaPlayer::PlayingState) { fmMediaPlayer->pause(); fmPlayPauseBtn->setText("Play"); } else { fmMediaPlayer->play(); fmPlayPauseBtn->setText("Pause"); }
    });
    connect(fmMediaPlayer, &QMediaPlayer::positionChanged, this, [this](qint64 pos){ if (fmMediaPlayer && fmMediaPlayer->duration()>0){ fmPositionSlider->blockSignals(true); fmPositionSlider->setValue(int(pos*1000/fmMediaPlayer->duration())); fmPositionSlider->blockSignals(false); fmTimeLabel->setText(QString("%1 / %2").arg(QTime::fromMSecsSinceStartOfDay(int(pos)).toString("mm:ss")).arg(QTime::fromMSecsSinceStartOfDay(int(fmMediaPlayer->duration())).toString("mm:ss"))); }});
    connect(fmPositionSlider, &QSlider::sliderMoved, this, [this](int v){ if (fmMediaPlayer && fmMediaPlayer->duration()>0) fmMediaPlayer->setPosition(qint64(v) * fmMediaPlayer->duration() / 1000); });
    connect(fmVolumeSlider, &QSlider::valueChanged, this, [this](int v){ if (fmAudioOutput) fmAudioOutput->setVolume(v/100.0); });

    // Center the preview content between title and controls
    QWidget *previewContent = new QWidget(fmPreviewPanel);
    QVBoxLayout *pc = new QVBoxLayout(previewContent);
    pc->setContentsMargins(0,0,0,0);
    pc->setSpacing(6);
    pc->addWidget(fmImageView, 1);
    pc->addWidget(fmVideoWidget, 1);
    pv->addWidget(previewContent);
    pv->addLayout(mc);
    // Hide media controls by default (only show for video/audio)
    fmPlayPauseBtn->hide();
    fmPositionSlider->hide();
    fmTimeLabel->hide();
    pc->addWidget(fmTextView, 1);
    pv->addLayout(docc);

    pc->addWidget(fmCsvView, 1);
#ifdef HAVE_QT_PDF_WIDGETS
    pc->addWidget(fmPdfView, 1);
#endif
    pc->addWidget(fmSvgView, 1);

    fmVolumeSlider->hide();


    // Assemble right side
    fmRightSplitter->addWidget(viewContainer);
    fmRightSplitter->addWidget(fmPreviewPanel);
    fmRightSplitter->setStretchFactor(0, 3);
    fmRightSplitter->setStretchFactor(1, 1);
    rightLayout->addWidget(fmRightSplitter);
    rightLayout->setStretch(0, 0); // toolbar
    rightLayout->setStretch(1, 1); // main content


    // Create File Manager shortcuts (default key sequences), store them, then apply custom mappings
    {
        auto sc = new QShortcut(QKeySequence(Qt::Key_Space), fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmOpenOverlay);
        fmShortcutObjs.insert("OpenOverlay", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence::Copy, fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmCopy);
        fmShortcutObjs.insert("Copy", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence::Cut, fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmCut);
        fmShortcutObjs.insert("Cut", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence::Paste, fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmPaste);
        fmShortcutObjs.insert("Paste", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence::Delete, fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmDelete);
        fmShortcutObjs.insert("Delete", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::Key_F2), fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmRename);
        fmShortcutObjs.insert("Rename", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Delete), fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmDeletePermanent);
        fmShortcutObjs.insert("DeletePermanent", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence::New, fileManagerPage); // Ctrl+N
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmNewFolder);
        fmShortcutObjs.insert("NewFolder", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmCreateFolderWithSelected);
        fmShortcutObjs.insert("CreateFolderWithSelected", sc);
    }
    {
        auto sc = new QShortcut(QKeySequence(Qt::Key_Backspace), fileManagerPage);
        connect(sc, &QShortcut::activated, this, &MainWindow::onFmBackToParent);
        fmShortcutObjs.insert("BackToParent", sc);
    }


    // Apply custom shortcuts from settings (overrides defaults)
    applyFmShortcuts();

    // Connect selection changes to preview
    connect(fmGridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onFmSelectionChanged);
    connect(fmListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onFmSelectionChanged);

    // Wire splitter widgets
    fmSplitter->addWidget(left);
    fmSplitter->addWidget(right);

    // Context menus
    fmGridView->setContextMenuPolicy(Qt::CustomContextMenu);
    fmListView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fmGridView, &QWidget::customContextMenuRequested, this, &MainWindow::onFmShowContextMenu);
    connect(fmListView, &QWidget::customContextMenuRequested, this, &MainWindow::onFmShowContextMenu);


    fmSplitter->setStretchFactor(0,1);
    fmSplitter->setStretchFactor(1,3);

    // Root: select first drive if exists
    QFileInfoList drives = QDir::drives();
    if (!drives.isEmpty()) {
        QString path = drives.first().absoluteFilePath();
        QModelIndex idx = fmTreeModel->index(path);
        if (idx.isValid()) {
            fmTree->setCurrentIndex(idx);
                    fmDirModel->setRootPath(path);
            QModelIndex srcRoot = fmDirModel->index(path);
            if (fmProxyModel) {
                fmProxyModel->rebuildForRoot(path);
                QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
                fmGridView->setRootIndex(proxyRoot);
                fmListView->setRootIndex(proxyRoot);
            } else {
                fmGridView->setRootIndex(srcRoot);
                fmListView->setRootIndex(srcRoot);
            }
        }
    }

    // React to tree single-click to change right view root
    // (activated via Enter/double-click remains for expansion)
    connect(fmTree, &QTreeView::clicked, this, &MainWindow::onFmTreeActivated);

    // Install page layout
    QVBoxLayout *pageLayout = new QVBoxLayout(fileManagerPage);
    pageLayout->setContentsMargins(0,0,0,0);
    pageLayout->addWidget(fmSplitter);

    // Persist splitter positions immediately when moved
    if (fmSplitter) {
        connect(fmSplitter, &QSplitter::splitterMoved, this, [this](int, int){
            QSettings s("AugmentCode", "KAssetManager");
            s.setValue("FileManager/MainSplitter", fmSplitter->saveState());
            QVariantList sizes; for (int v : fmSplitter->sizes()) sizes << v;
            s.setValue("FileManager/MainSplitterSizes", sizes);
            s.sync();
        });
    }
    if (fmLeftSplitter) {
        connect(fmLeftSplitter, &QSplitter::splitterMoved, this, [this](int, int){
            QSettings s("AugmentCode", "KAssetManager");
            s.setValue("FileManager/LeftSplitter", fmLeftSplitter->saveState());
            QVariantList sizes; for (int v : fmLeftSplitter->sizes()) sizes << v;
            s.setValue("FileManager/LeftSplitterSizes", sizes);
            s.sync();
        });
    }
    if (fmRightSplitter) {
        connect(fmRightSplitter, &QSplitter::splitterMoved, this, [this](int, int){
            QSettings s("AugmentCode", "KAssetManager");
            s.setValue("FileManager/RightSplitter", fmRightSplitter->saveState());
            QVariantList sizes; for (int v : fmRightSplitter->sizes()) sizes << v;
            s.setValue("FileManager/RightSplitterSizes", sizes);
            s.sync();
        });
    }

    // Restore persisted workspace for File Manager (after widgets are shown)
    QTimer::singleShot(0, this, [this]{
        QSettings s("AugmentCode", "KAssetManager");
        // View mode and preview visibility first
        if (s.contains("FileManager/ViewMode")) {
            bool grid = s.value("FileManager/ViewMode").toBool();
            fmIsGridMode = grid;
            fmViewStack->setCurrentIndex(grid ? 0 : 1);
            if (fmViewModeButton) fmViewModeButton->setIcon(grid ? icoGrid() : icoList());
        }
        if (s.contains("FileManager/PreviewVisible")) {
            bool vis = s.value("FileManager/PreviewVisible").toBool();
            if (fmPreviewToggleButton) fmPreviewToggleButton->setChecked(vis);
            if (fmPreviewPanel) fmPreviewPanel->setVisible(vis);
        }
        // Group sequences toggle
        fmGroupSequences = s.value("FileManager/GroupSequences", true).toBool();
        if (fmGroupSequencesCheckBox) fmGroupSequencesCheckBox->setChecked(fmGroupSequences);
        if (fmProxyModel) fmProxyModel->setGroupingEnabled(fmGroupSequences);

        // Splitters
        if (fmSplitter && s.contains("FileManager/MainSplitter")) fmSplitter->restoreState(s.value("FileManager/MainSplitter").toByteArray());
        if (fmLeftSplitter && s.contains("FileManager/LeftSplitter")) fmLeftSplitter->restoreState(s.value("FileManager/LeftSplitter").toByteArray());
        if (fmRightSplitter && s.contains("FileManager/RightSplitter")) fmRightSplitter->restoreState(s.value("FileManager/RightSplitter").toByteArray());
        // Fallback: explicit sizes if present
        auto applySizes = [](QSplitter* sp, const QVariant& v) {
            if (!sp) return;
            if (!v.isValid()) return;
            QList<int> sizes;
            const auto list = v.toList();
            for (const QVariant &x : list) sizes << x.toInt();
            if (!sizes.isEmpty()) sp->setSizes(sizes);
        };
        applySizes(fmSplitter, s.value("FileManager/MainSplitterSizes"));
        applySizes(fmLeftSplitter, s.value("FileManager/LeftSplitterSizes"));
        applySizes(fmRightSplitter, s.value("FileManager/RightSplitterSizes"));

        // Headers
        if (fmListView && fmListView->model()) {
            auto hh = fmListView->horizontalHeader();
            for (int c = 0; c < fmListView->model()->columnCount(); ++c) {
                QVariant v = s.value(QString("FileManager/ListView/Col%1").arg(c));
                if (v.isValid()) hh->resizeSection(c, v.toInt());
            }
        }
        if (fmTree && fmTree->model()) {
            auto th = fmTree->header();
            for (int c = 0; c < fmTree->model()->columnCount(); ++c) {
                QVariant v = s.value(QString("FileManager/Tree/Col%1").arg(c));
                if (v.isValid()) th->resizeSection(c, v.toInt());
            }
        }
        // Restore current navigation path
        if (s.contains("FileManager/CurrentPath")) {
            QString savedPath = s.value("FileManager/CurrentPath").toString();
            if (QFileInfo::exists(savedPath)) {
                if (fmTreeModel && fmTree) {
                    QModelIndex idx = fmTreeModel->index(savedPath);
                    if (idx.isValid()) fmTree->setCurrentIndex(idx);
                }
                if (fmDirModel) {
                    fmDirModel->setRootPath(savedPath);
                    QModelIndex srcRoot = fmDirModel->index(savedPath);
                    if (fmProxyModel) {
                        fmProxyModel->rebuildForRoot(savedPath);
                        QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
                        if (fmGridView) fmGridView->setRootIndex(proxyRoot);
                        if (fmListView) fmListView->setRootIndex(proxyRoot);
                    } else {
                        if (fmGridView) fmGridView->setRootIndex(srcRoot);
                        if (fmListView) fmListView->setRootIndex(srcRoot);
                    }
                }
            }
        }
    });

    LogManager::instance().addLog("[TRACE] setupFileManagerUi exit", "DEBUG");
}

void MainWindow::onFmTreeActivated(const QModelIndex &index)
{
    QString path = fmTreeModel->filePath(index);
    if (path.isEmpty()) return;

    fmDirModel->setRootPath(path);
    QModelIndex srcRoot = fmDirModel->index(path);
    if (fmProxyModel) {
        fmProxyModel->rebuildForRoot(path);
        QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
        fmGridView->setRootIndex(proxyRoot);
        fmListView->setRootIndex(proxyRoot);
    } else {
        fmGridView->setRootIndex(srcRoot);
        fmListView->setRootIndex(srcRoot);
    }

    // Sync folder tree selection/expansion
    if (fmTree && fmTreeModel) {
        QModelIndex treeIdx = fmTreeModel->index(path);
        if (treeIdx.isValid()) {
            QModelIndex p = treeIdx;
            while (p.isValid()) { fmTree->expand(p); p = p.parent(); }
            fmTree->setCurrentIndex(treeIdx);
            fmTree->scrollTo(treeIdx, QAbstractItemView::PositionAtCenter);
        }
    }

    // Persist current path
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/CurrentPath", path);
}

// Forward declarations for file-type helpers used by File Manager handlers
static bool isImageFile(const QString &ext);
static bool isVideoFile(const QString &ext);

void MainWindow::onFmItemDoubleClicked(const QModelIndex &index)
{
    QModelIndex idx = index.sibling(index.row(), 0);
    // If view uses proxy, map to source when needed
    QModelIndex srcIdx = idx;
    if (fmProxyModel && idx.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(idx);

    QString path = fmDirModel->filePath(srcIdx);
    if (path.isEmpty()) return;

    // If grouping is enabled and this is a representative, open sequence in overlay
    if (fmProxyModel && fmGroupSequences && idx.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(idx)) {
        auto info = fmProxyModel->infoForProxyIndex(idx);
        QStringList frames = reconstructSequenceFramePaths(info.reprPath, info.start, info.end);
        if (!frames.isEmpty()) {
            if (!previewOverlay) {
                previewOverlay = new PreviewOverlay(this);
                previewOverlay->setGeometry(rect());
                connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changePreview);
            } else {
                previewOverlay->stopPlayback();
            }
            // Remember source view/index for focus restoration on close
            QAbstractItemView* srcView = (fmGridView && fmGridView->isVisible() && fmGridView->hasFocus()) ? static_cast<QAbstractItemView*>(fmGridView) : static_cast<QAbstractItemView*>(fmListView);
            fmOverlayCurrentIndex = QPersistentModelIndex(idx);
            fmOverlaySourceView = srcView;
            // Build display name
            int pad = 0;
            auto m = SequenceDetector::mainPattern().match(QFileInfo(info.reprPath).fileName());
            if (m.hasMatch()) pad = m.captured(3).length(); else pad = QString::number(info.start).length();
            QString s0 = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
            QString s1 = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
            QString seqName = QString("%1.[%2-%3].%4").arg(info.base, s0, s1, info.ext);
            previewOverlay->showSequence(frames, seqName, info.start, info.end);
            return;
        }
    }

    QFileInfo fi(path);
    if (fi.isDir()) {
            fmDirModel->setRootPath(path);
        QModelIndex srcRoot = fmDirModel->index(path);
        if (fmProxyModel) {
            fmProxyModel->rebuildForRoot(path);
            QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
            fmGridView->setRootIndex(proxyRoot);
            fmListView->setRootIndex(proxyRoot);
        } else {
            fmGridView->setRootIndex(srcRoot);
            fmListView->setRootIndex(srcRoot);
        }
        // Sync folder tree selection/expansion
        if (fmTree && fmTreeModel) {
            QModelIndex treeIdx = fmTreeModel->index(path);
            if (treeIdx.isValid()) {
                QModelIndex p = treeIdx;
                while (p.isValid()) { fmTree->expand(p); p = p.parent(); }
                fmTree->setCurrentIndex(treeIdx);
                fmTree->scrollTo(treeIdx, QAbstractItemView::PositionAtCenter);
            }
        }
        QSettings s("AugmentCode", "KAssetManager");
        s.setValue("FileManager/CurrentPath", path);
        return;
    }

    const QString ext = fi.suffix();
    if (isImageFile(ext) || isVideoFile(ext)) {
        if (!previewOverlay) {
            previewOverlay = new PreviewOverlay(this);
            previewOverlay->setGeometry(rect());
            connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
            connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changePreview);
        } else {
            previewOverlay->stopPlayback();
        }
        // Remember source view/index for focus restoration on close
        QAbstractItemView* srcView = (fmGridView && fmGridView->isVisible() && fmGridView->hasFocus()) ? static_cast<QAbstractItemView*>(fmGridView) : static_cast<QAbstractItemView*>(fmListView);
        fmOverlayCurrentIndex = QPersistentModelIndex(idx);
        fmOverlaySourceView = srcView;
        previewOverlay->showAsset(path, fi.fileName(), fi.suffix());
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

static QString uniqueNameInDir(const QString &dirPath, const QString &baseName)
{
    QFileInfo fi(dirPath + QDir::separator() + baseName);
    if (!fi.exists()) return fi.absoluteFilePath();
    QString name = fi.completeBaseName();
    QString ext = fi.completeSuffix();
    int n = 2;
    while (true) {
        QString candidate = name + QString(" (%1)").arg(n);
        if (!ext.isEmpty()) candidate += "." + ext;
        QFileInfo fi2(dirPath + QDir::separator() + candidate);
        if (!fi2.exists()) return fi2.absoluteFilePath();
        ++n;
    }
}


QStringList getSelectedFileManagerPaths(QFileSystemModel *model, QListView *grid, QTableView *list, QStackedWidget *stack)
{
    QStringList out;
    auto mapToSource = [](const QModelIndex &viewIdx) -> QModelIndex {
        if (!viewIdx.isValid()) return viewIdx;
        auto proxy = qobject_cast<const QSortFilterProxyModel*>(viewIdx.model());
        if (proxy) return proxy->mapToSource(viewIdx);
        return viewIdx;
    };

    if (stack->currentIndex() == 0) {
        const auto idxs = grid->selectionModel()->selectedIndexes();
        for (const QModelIndex &idx : idxs) {
            if (idx.column() != 0) continue;
            QModelIndex src = mapToSource(idx);
            out << model->filePath(src);
        }
    } else {
        const auto rows = list->selectionModel()->selectedRows();
        for (const QModelIndex &idx : rows) {
            QModelIndex src = mapToSource(idx);
            out << model->filePath(src);
        }
    }
    out.removeDuplicates();
    return out;
}

void MainWindow::onFmCopy()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    fmClipboard = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    fmClipboardCutMode = false;
}

void MainWindow::onFmCut()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    fmClipboard = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    fmClipboardCutMode = true;
}

void MainWindow::onFmPaste()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    if (fmClipboard.isEmpty()) return;
    const QString destDir = fmDirModel->rootPath();

    // Ensure any preview locks are released before file ops
    if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
    // Release any locks held by previews
    releaseAnyPreviewLocksForPaths(fmClipboard);
    // Enqueue async operation
    auto &q = FileOpsQueue::instance();
    if (fmClipboardCutMode) q.enqueueMove(fmClipboard, destDir);
    else q.enqueueCopy(fmClipboard, destDir);

    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();

    fmClipboard.clear();
    fmClipboardCutMode = false;
}

void MainWindow::onFmDelete()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    QStringList paths = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    if (paths.isEmpty()) return;
    const auto ret = QMessageBox::question(this, "Move to Recycle Bin", QString("Delete %1 item(s)? They will be moved to Recycle Bin.").arg(paths.size()));
    if (ret != QMessageBox::Yes) return;

    // Ensure any preview locks are released before file ops
    releaseAnyPreviewLocksForPaths(paths);
    // Enqueue async delete
    auto &q = FileOpsQueue::instance();
    q.enqueueDelete(paths);
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
}


void MainWindow::onFmDeletePermanent()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    QStringList paths = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    if (paths.isEmpty()) return;
    const auto ret = QMessageBox::warning(this, "Permanent Delete",
        QString("PERMANENTLY delete %1 item(s)? This action cannot be undone!").arg(paths.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    // Ensure any preview locks are released before file ops
    releaseAnyPreviewLocksForPaths(paths);
    auto &q = FileOpsQueue::instance();
    q.enqueueDeletePermanent(paths);
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
}

void MainWindow::onFmBackToParent()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    if (!fmDirModel) return;
    QString currentPath = fmDirModel->rootPath();
    if (currentPath.isEmpty()) return;
    QDir dir(currentPath);
    if (dir.cdUp()) {
        const QString parentPath = dir.absolutePath();
            fmDirModel->setRootPath(parentPath);
        QModelIndex srcRoot = fmDirModel->index(parentPath);
        if (fmProxyModel) {
            fmProxyModel->rebuildForRoot(parentPath);
            QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
            if (fmGridView) fmGridView->setRootIndex(proxyRoot);
            if (fmListView) fmListView->setRootIndex(proxyRoot);
        } else {
            if (fmGridView) fmGridView->setRootIndex(srcRoot);
            if (fmListView) fmListView->setRootIndex(srcRoot);
        }
        // select in tree if exists
        if (fmTree && fmTreeModel) {
            QModelIndex idx = fmTreeModel->index(parentPath);
            if (idx.isValid()) fmTree->setCurrentIndex(idx);
        }
        QSettings s("AugmentCode", "KAssetManager");
        s.setValue("FileManager/CurrentPath", parentPath);
    }
}

void MainWindow::onFmRename()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    QStringList paths = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    if (paths.size() != 1) return;
    QString p = paths.first();
    releaseAnyPreviewLocksForPaths(QStringList{p});
    QFileInfo fi(p);
    bool ok = false;
    QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, fi.fileName(), &ok);
    if (!ok || newName.trimmed().isEmpty()) return;
    QString dest = fi.absolutePath() + QDir::separator() + newName.trimmed();
    if (fi.isDir()) {
        QDir parent(fi.absolutePath());
        parent.rename(fi.fileName(), newName.trimmed());
    } else {
        QFile::rename(p, dest);
    }
}

void MainWindow::onFmNewFolder()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    const QString destDir = fmDirModel->rootPath();
    QString path = uniqueNameInDir(destDir, "New Folder");
    QDir().mkpath(path);
}

void MainWindow::onFmAddToFavorites()
{
    QStringList sel = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    if (sel.isEmpty()) return;
    bool changed = false;
    for (const QString &p : sel) {
        if (!fmFavorites.contains(p)) {
            fmFavorites << p;
            changed = true;
        }
    }
    if (changed) {
        fmFavorites.removeDuplicates();
        saveFmFavorites();
        // refresh list
        if (fmFavoritesList) {
            fmFavoritesList->clear();
            for (const QString &p : fmFavorites) {
                QListWidgetItem *it = new QListWidgetItem(QIcon::fromTheme("star"), QFileInfo(p).fileName());
                it->setToolTip(p);
                it->setData(Qt::UserRole, p);
                fmFavoritesList->addItem(it);
            }
        }
    }
}

void MainWindow::onFmRemoveFavorite()
{
    if (!fmFavoritesList) return;
    QListWidgetItem *it = fmFavoritesList->currentItem();
    if (!it) return;
    QString path = it->data(Qt::UserRole).toString();
    fmFavorites.removeAll(path);
    delete it;
    saveFmFavorites();
}

void MainWindow::onFmFavoriteActivated(QListWidgetItem* item)
{
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;
    QModelIndex idx = fmTreeModel->index(path);
    if (idx.isValid()) fmTree->setCurrentIndex(idx);
    fmDirModel->setRootPath(path);
    QModelIndex srcRoot = fmDirModel->index(path);
    if (fmProxyModel) {
        fmProxyModel->rebuildForRoot(path);
        QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
        fmGridView->setRootIndex(proxyRoot);
        fmListView->setRootIndex(proxyRoot);
    } else {
        fmGridView->setRootIndex(srcRoot);
        fmListView->setRootIndex(srcRoot);
    }
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/CurrentPath", path);
}

void MainWindow::loadFmFavorites()
{
    fmFavorites.clear();
    QSettings s("AugmentCode", "KAssetManager");
    int size = s.beginReadArray("FileManager/Favorites");
    for (int i=0;i<size;++i) {
        s.setArrayIndex(i);
        QString p = s.value("path").toString();
        if (!p.isEmpty()) fmFavorites << p;
    }
    s.endArray();
    fmFavorites.removeDuplicates();
    if (fmFavoritesList) {
        fmFavoritesList->clear();
        for (const QString &p : fmFavorites) {
            QListWidgetItem *it = new QListWidgetItem(QIcon::fromTheme("star"), QFileInfo(p).fileName());
            it->setToolTip(p);
            it->setData(Qt::UserRole, p);
            fmFavoritesList->addItem(it);
        }
    }
}

void MainWindow::saveFmFavorites()
{
    QSettings s("AugmentCode", "KAssetManager");
    s.beginWriteArray("FileManager/Favorites");
    for (int i=0;i<fmFavorites.size();++i) {
        s.setArrayIndex(i);
        s.setValue("path", fmFavorites.at(i));
    }
    s.endArray();
}


void MainWindow::onFmShowContextMenu(const QPoint &pos)
{
    QWidget *senderW = qobject_cast<QWidget*>(sender());
    QPoint globalPos = senderW->mapToGlobal(pos);
    QMenu menu;
    QAction *copyA = menu.addAction("Copy", this, &MainWindow::onFmCopy, QKeySequence::Copy);
    QAction *cutA = menu.addAction("Cut", this, &MainWindow::onFmCut, QKeySequence::Cut);
    QAction *pasteA = menu.addAction("Paste", this, &MainWindow::onFmPaste, QKeySequence::Paste);
    menu.addSeparator();
    QAction *renameA = menu.addAction("Rename", this, &MainWindow::onFmRename, QKeySequence(Qt::Key_F2));
    QAction *delA = menu.addAction("Delete", this, &MainWindow::onFmDelete, QKeySequence::Delete);
    QAction *createFolderWithSel = menu.addAction("Create Folder with Selected Files", this, &MainWindow::onFmCreateFolderWithSelected);
    menu.addSeparator();
    QAction *addLibA = menu.addAction("Add to Asset Library", this, &MainWindow::onAddSelectionToAssetLibrary);

    QAction *favA = menu.addAction("Add to Favorites", this, &MainWindow::onFmAddToFavorites);

    // Enable/disable depending on selection
    bool hasSel = !getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack).isEmpty();
    copyA->setEnabled(hasSel);
    cutA->setEnabled(hasSel);
    renameA->setEnabled(hasSel && getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack).size()==1);
    delA->setEnabled(hasSel);
    pasteA->setEnabled(!fmClipboard.isEmpty());
    addLibA->setEnabled(hasSel);
    favA->setEnabled(hasSel);
    createFolderWithSel->setEnabled(hasSel);

    menu.exec(globalPos);

}

void MainWindow::onFmTreeContextMenu(const QPoint &pos)
{
    if (!fmTree || !fmTreeModel) return;
    QModelIndex idx = fmTree->indexAt(pos);
    if (!idx.isValid()) return;
    QString path = fmTreeModel->filePath(idx);
    if (path.isEmpty()) return;

    QMenu menu;
    QAction *copyA = menu.addAction("Copy");
    QAction *cutA = menu.addAction("Cut");
    QAction *pasteA = menu.addAction("Paste");
    menu.addSeparator();
    QAction *renameA = menu.addAction("Rename");
    QAction *delA = menu.addAction("Delete (Recycle Bin)");
    QAction *permDelA = menu.addAction("Permanent Delete (Shift+Delete)");
    QAction *newFolderA = menu.addAction("New Folder");
    QAction *createFolderWithSelA = menu.addAction("Create Folder with Selected Files");

    // Enable states
    const bool hasClipboard = !fmClipboard.isEmpty();
    pasteA->setEnabled(hasClipboard);

    QAction *chosen = menu.exec(fmTree->viewport()->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == copyA) {
        fmClipboard = getSelectedFmTreePaths();
        fmClipboardCutMode = false;
    } else if (chosen == cutA) {
        fmClipboard = getSelectedFmTreePaths();
        fmClipboardCutMode = true;
    } else if (chosen == pasteA) {
        onFmPasteInto(path);
    } else if (chosen == delA) {
        QStringList paths = getSelectedFmTreePaths();
        if (paths.isEmpty()) return;
        auto ret = QMessageBox::question(this, "Move to Recycle Bin",
            QString("Delete %1 item(s)? They will be moved to Recycle Bin.").arg(paths.size()));
        if (ret != QMessageBox::Yes) return;
        releaseAnyPreviewLocksForPaths(paths);
        FileOpsQueue::instance().enqueueDelete(paths);
        if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
        fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
    } else if (chosen == permDelA) {
        QStringList paths = getSelectedFmTreePaths();
        releaseAnyPreviewLocksForPaths(paths);
        doPermanentDelete(paths);
    } else if (chosen == renameA) {
        QStringList paths = getSelectedFmTreePaths();
        if (paths.size() != 1) return;
        QFileInfo fi(paths.first());
        bool ok=false;
        QString newName = QInputDialog::getText(this, "Rename", "New name:", QLineEdit::Normal, fi.fileName(), &ok);
        if (!ok || newName.trimmed().isEmpty()) return;
        QDir parent(fi.absolutePath());
        parent.rename(fi.fileName(), newName.trimmed());
    } else if (chosen == newFolderA) {
        QDir dir(path);
        QString newPath = uniqueNameInDir(path, "New Folder");
        dir.mkpath(newPath);
    } else if (chosen == createFolderWithSelA) {
        // Use selection from main view, create folder inside tree path
        QStringList files = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
        if (files.isEmpty()) return;
        bool ok=false;
        QString folderName = QInputDialog::getText(this, "Create Folder", "Enter folder name:", QLineEdit::Normal, "New Folder", &ok);
        if (!ok) return;
        folderName = folderName.trimmed();
        if (folderName.isEmpty()) return;
        QDir dd(path);
        QString folderPath = dd.filePath(folderName);
        if (QFileInfo::exists(folderPath)) {
            int i=2; QString base = folderName;
            while (QFileInfo::exists(folderPath)) { folderName = QString("%1 (%2)").arg(base).arg(i++); folderPath = dd.filePath(folderName); }
        }
        if (!dd.mkpath(folderPath)) {
            QMessageBox::warning(this, "Error", QString("Failed to create folder: %1").arg(folderPath));
            return;
        }
        releaseAnyPreviewLocksForPaths(files);
        FileOpsQueue::instance().enqueueMove(files, folderPath);
        if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
        fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
    }
}

QStringList MainWindow::getSelectedFmTreePaths() const
{
    QStringList out;
    if (!fmTree || !fmTreeModel) return out;
    auto sel = fmTree->selectionModel();
    if (!sel) return out;
    const auto rows = sel->selectedRows();
    for (const QModelIndex &idx : rows) {
        out << fmTreeModel->filePath(idx);
    }
    out.removeDuplicates();
    return out;
}

void MainWindow::onFmPasteInto(const QString& destDir)
{
    if (fmClipboard.isEmpty()) return;
    // Ensure any preview locks are released before file ops
    releaseAnyPreviewLocksForPaths(fmClipboard);
    auto &q = FileOpsQueue::instance();
    if (fmClipboardCutMode) q.enqueueMove(fmClipboard, destDir);
    else q.enqueueCopy(fmClipboard, destDir);
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
    fmClipboard.clear();
    fmClipboardCutMode = false;
}

void MainWindow::doPermanentDelete(const QStringList& paths)
{
    if (paths.isEmpty()) return;
    const auto ret = QMessageBox::warning(this, "Permanent Delete",
        QString("PERMANENTLY delete %1 item(s)? This action cannot be undone!").arg(paths.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    // Ensure any preview locks are released before file ops
    releaseAnyPreviewLocksForPaths(paths);
    FileOpsQueue::instance().enqueueDeletePermanent(paths);
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
}


void MainWindow::releaseAnyPreviewLocksForPaths(const QStringList& paths)
{
    QSet<QString> s; for (const QString &p : paths) s.insert(QFileInfo(p).absoluteFilePath());
    // Embedded FM preview: stop media and clear if current preview is among paths
    if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
    if (!fmCurrentPreviewPath.isEmpty()) {
        QString abs = QFileInfo(fmCurrentPreviewPath).absoluteFilePath();
        if (s.contains(abs)) {
            clearFmPreview();
        }
    }
    // Overlay: if showing one of these files, close it to fully release handles
    if (previewOverlay) {
        QString cur = previewOverlay->currentPath();
        if (s.contains(QFileInfo(cur).absoluteFilePath())) {
            closePreview();
        } else {
            // still release any handles
            previewOverlay->stopPlayback();
        }
    }
}


void MainWindow::onFmCreateFolderWithSelected()
{
    if (qobject_cast<QShortcut*>(sender())) {
        QWidget* fw = QApplication::focusWidget();
        if (fw && (qobject_cast<QLineEdit*>(fw) || fw->inherits("QTextEdit") || fw->inherits("QPlainTextEdit"))) return;
    }
    QStringList paths = getSelectedFileManagerPaths(fmDirModel, fmGridView, fmListView, fmViewStack);
    if (paths.isEmpty()) return;
    // Destination directory is current root of fmDirModel
    const QString destDir = fmDirModel->rootPath();
    bool ok=false;
    QString folderName = QInputDialog::getText(this, "Create Folder", "Enter folder name:", QLineEdit::Normal, "New Folder", &ok);
    if (!ok) return;
    folderName = folderName.trimmed();
    if (folderName.isEmpty()) return;
    QDir dd(destDir);
    QString folderPath = dd.filePath(folderName);
    if (FileUtils::pathExists(folderPath)) {
        // attempt unique suffix
        int i=2; QString base = folderName; while (FileUtils::pathExists(folderPath)) { folderName = QString("%1 (%2)").arg(base).arg(i++); folderPath = dd.filePath(folderName);}
    }
    if (!dd.mkpath(folderPath)) {
        QMessageBox::warning(this, "Error", QString("Failed to create folder: %1").arg(folderPath));
        return;
    }
    // Enqueue async move of selected into the new folder
    auto &q = FileOpsQueue::instance();
    q.enqueueMove(paths, folderPath);
    if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
    fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
}

void MainWindow::onFmViewModeToggled()
{
    fmIsGridMode = !fmIsGridMode;
    fmViewStack->setCurrentIndex(fmIsGridMode ? 0 : 1);
    fmViewModeButton->setIcon(fmIsGridMode ? icoGrid() : icoList());

    // Keep the current folder when switching views
    if (fmDirModel) {
        const QString path = fmDirModel->rootPath();
        if (!path.isEmpty()) {
            QModelIndex srcRoot = fmDirModel->index(path);
            if (fmProxyModel) {
                fmProxyModel->rebuildForRoot(path);
                QModelIndex proxyRoot = fmProxyModel->mapFromSource(srcRoot);
                if (fmGridView) fmGridView->setRootIndex(proxyRoot);
                if (fmListView) fmListView->setRootIndex(proxyRoot);
            } else {
                if (fmGridView) fmGridView->setRootIndex(srcRoot);
                if (fmListView) fmListView->setRootIndex(srcRoot);
            }
        }
    }

    // Persist immediately
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/ViewMode", fmIsGridMode);
    s.sync();
}

void MainWindow::onFmThumbnailSizeChanged(int size)
{
    if (fmGridView) {
        fmGridView->setIconSize(QSize(size, size));
        fmGridView->setGridSize(QSize(size + 24, size + 40));
        if (auto *d = dynamic_cast<FmItemDelegate*>(fmGridView->itemDelegate())) d->setThumbnailSize(size);
        fmGridView->reset();
    }
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/GridThumbSize", size);
}


void MainWindow::onAddSelectionToAssetLibrary()
{
    // Collect selected paths (files and folders) from the active File Manager view.
    // Map proxy indexes to source before using fmDirModel APIs.
    QStringList filePaths;
    QStringList folderPaths;

    const bool isGrid = (fmViewStack->currentIndex() == 0);
    if (isGrid) {
        if (!fmGridView || !fmGridView->selectionModel()) return;
        const auto indexes = fmGridView->selectionModel()->selectedIndexes();
        for (const QModelIndex &idx : indexes) {
            QModelIndex srcIdx = idx;
            if (fmProxyModel && idx.model() == fmProxyModel)
                srcIdx = fmProxyModel->mapToSource(idx);
            if (!srcIdx.isValid()) continue;
            const QString path = fmDirModel->filePath(srcIdx);
            if (path.isEmpty()) continue;
            if (fmDirModel->isDir(srcIdx)) folderPaths << path; else filePaths << path;
        }
    } else {
        if (!fmListView || !fmListView->selectionModel()) return;
        const auto rows = fmListView->selectionModel()->selectedRows();
        for (const QModelIndex &idx : rows) {
            QModelIndex srcIdx = idx;
            if (fmProxyModel && idx.model() == fmProxyModel)
                srcIdx = fmProxyModel->mapToSource(idx);
            if (!srcIdx.isValid()) continue;
            const QString path = fmDirModel->filePath(srcIdx);
            if (path.isEmpty()) continue;
            if (fmDirModel->isDir(srcIdx)) folderPaths << path; else filePaths << path;
        }
    }

    filePaths.removeDuplicates();
    folderPaths.removeDuplicates();

    if (filePaths.isEmpty() && folderPaths.isEmpty()) return;

    // Ensure a destination asset folder is selected
    if (!folderTreeView || !folderTreeView->currentIndex().isValid()) {
        QMessageBox::warning(this, "No Folder Selected", "Please select a folder in the Asset Library before importing.");
        return;
    }
    const int targetFolderId = folderTreeView->currentIndex().data(VirtualFolderTreeModel::IdRole).toInt();

    // Show progress dialog
    if (!importProgressDialog) importProgressDialog = new ImportProgressDialog(this);
    importProgressDialog->show();
    importProgressDialog->raise();
    importProgressDialog->activateWindow();

    // Prevent the dialog from closing between multiple import calls
    disconnect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);

    int totalImported = 0;

    // Import folders preserving subfolder structure
    for (const QString &dir : folderPaths) {
        if (importer->importFolder(dir, targetFolderId)) totalImported++;
    }

    // Import individual files
    if (!filePaths.isEmpty()) {
        importer->importFiles(filePaths, targetFolderId); // emits importFinished
        totalImported += filePaths.size();
    }

    // Reconnect and close dialog
    connect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);
    onImportComplete();

    if (totalImported > 0) {
        statusBar()->showMessage(QString("Imported %1 item(s)").arg(totalImported), 3000);
    }
}



void MainWindow::setupConnections()
{
    // Debounced folder selection for Asset Manager
    folderSelectTimer.setSingleShot(true);
    connect(&folderSelectTimer, &QTimer::timeout, this, [this]{
        const int fid = pendingFolderId;
        if (fid <= 0) return;

        // Save context for current folder before switching
        if (currentAssetId > 0 || !selectedAssetIds.isEmpty()) {
            int currentFolderId = assetsModel->folderId();
            if (currentFolderId > 0) {
                ContextPreserver::FolderContext ctx;
                // Save scroll position
                if (isGridMode && assetGridView) {
                    ctx.scrollPosition = assetGridView->verticalScrollBar()->value();
                } else if (!isGridMode && assetTableView) {
                    ctx.scrollPosition = assetTableView->verticalScrollBar()->value();
                }
                ctx.isGridMode = isGridMode;
                ctx.searchText = searchBox->text();
                ctx.ratingFilter = ratingFilter->currentIndex() - 1; // -1 for "All"
                ctx.selectedAssetIds = selectedAssetIds;
                ctx.recursiveMode = recursiveCheckBox->isChecked();

                // Save selected tags
                QModelIndexList tagSelection = tagsListView->selectionModel()->selectedIndexes();
                for (const QModelIndex& idx : tagSelection) {
                    int tagId = idx.data(TagsModel::IdRole).toInt();
                    if (tagId > 0) ctx.selectedTagIds.insert(tagId);
                }

                ContextPreserver::instance().saveFolderContext(currentFolderId, ctx);
            }
        }

        // Stop any preview playback but do NOT cancel thumbnail generation; allow it to continue in background
        if (previewOverlay) previewOverlay->stopPlayback();
        // Apply folder change
        assetsModel->setFolderId(fid);

        // Try to restore context for new folder
        if (ContextPreserver::instance().hasFolderContext(fid)) {
            QTimer::singleShot(50, this, [this, fid](){
                ContextPreserver::FolderContext ctx = ContextPreserver::instance().loadFolderContext(fid);

                // Restore view mode
                if (ctx.isGridMode != isGridMode) {
                    onViewModeChanged();
                }

                // Restore filters
                if (!ctx.searchText.isEmpty()) {
                    searchBox->setText(ctx.searchText);
                }
                if (ctx.ratingFilter >= -1) {
                    ratingFilter->setCurrentIndex(ctx.ratingFilter + 1);
                }
                recursiveCheckBox->setChecked(ctx.recursiveMode);

                // Restore scroll position
                if (ctx.scrollPosition > 0) {
                    if (isGridMode && assetGridView) {
                        assetGridView->verticalScrollBar()->setValue(ctx.scrollPosition);
                    } else if (!isGridMode && assetTableView) {
                        assetTableView->verticalScrollBar()->setValue(ctx.scrollPosition);
                    }
                }

                // Note: Asset selection restoration would need to wait for model to load
                // This is a future enhancement
            });
        } else {
            // No saved context - ensure the asset views start at the top for every new folder
            QTimer::singleShot(0, this, [this](){
                if (assetGridView) assetGridView->scrollToTop();
                if (assetTableView) assetTableView->scrollToTop();
            });
        }

        // Log memory usage before/after applying folder change
#ifdef Q_OS_WIN
        qDebug() << "[NAV] Folder change applied to id=" << fid << ", working set (MB)=" << (qulonglong)currentWorkingSetMB();
        QTimer::singleShot(1000, this, [](){ qDebug() << "[NAV] Post-change working set (MB)=" << (qulonglong)currentWorkingSetMB(); });
#endif

        clearSelection();
        updateInfoPanel();

        // Save as last active folder
        ContextPreserver::instance().saveLastActiveFolder(fid);
    });

    connect(folderTreeView, &QTreeView::clicked, this, &MainWindow::onFolderSelected);
    connect(folderTreeView, &QTreeView::customContextMenuRequested, this, &MainWindow::onFolderContextMenu);

    // Save/restore folder expansion state when model reloads
    connect(folderModel, &VirtualFolderTreeModel::modelAboutToBeReset, this, &MainWindow::saveFolderExpansionState);
    connect(folderModel, &VirtualFolderTreeModel::modelReset, this, &MainWindow::restoreFolderExpansionState);

    connect(assetGridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onAssetSelectionChanged);
    connect(assetGridView, &QListView::doubleClicked, this, &MainWindow::onAssetDoubleClicked);
    connect(assetGridView, &QListView::customContextMenuRequested, this, &MainWindow::onAssetContextMenu);

    // Connect table view signals
    connect(assetTableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::onAssetSelectionChanged);
    connect(assetTableView, &QTableView::doubleClicked, this, &MainWindow::onAssetDoubleClicked);
    connect(assetTableView, &QTableView::customContextMenuRequested, this, &MainWindow::onAssetContextMenu);

    // Update tag button states when selections change
    connect(tagsListView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateTagButtonStates);
    connect(assetGridView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateTagButtonStates);
    connect(assetTableView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::updateTagButtonStates);

    // Tag context menu
    connect(tagsListView, &QListView::customContextMenuRequested, this, &MainWindow::onTagContextMenu);

    // Install event filter on tags viewport after UI is fully built
    if (tagsListView && tagsListView->viewport()) {
        tagsListView->viewport()->installEventFilter(this);
        qDebug() << "[INIT] tagsListView viewport event filter installed (late)";
    }


    // Connect search box for real-time filtering
    connect(searchBox, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);

    // Visible-only live preview progress wiring
    connect(assetsModel, &QAbstractItemModel::modelReset, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(assetGridView->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(assetGridView->horizontalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(assetTableView->verticalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(assetTableView->horizontalScrollBar(), &QScrollBar::valueChanged, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(viewStack, &QStackedWidget::currentChanged, this, &MainWindow::scheduleVisibleThumbProgressUpdate);
    connect(&ProgressManager::instance(), &ProgressManager::isActiveChanged, this, [this]() {
        if (ProgressManager::instance().isActive()) {
            // Hide our visible-only progress while an import/global progress is active
            thumbnailProgressLabel->setVisible(false);
            thumbnailProgressBar->setVisible(false);
        } else {
            scheduleVisibleThumbProgressUpdate();
        }
    });
    // Update version table when versions change
    connect(&DB::instance(), &DB::assetVersionsChanged,
            this, &MainWindow::onAssetVersionsChanged);

}

void MainWindow::onFolderSelected(const QModelIndex &index)
{
    if (!index.isValid()) {
        qWarning() << "MainWindow::onFolderSelected - Invalid index";
        return;
    }

    int folderId = index.data(VirtualFolderTreeModel::IdRole).toInt();
    if (folderId <= 0) {
        qWarning() << "MainWindow::onFolderSelected - Invalid folder ID:" << folderId;
        return;
    }

    // Debounce rapid selections; actual load happens on timer to allow cleanup/cancel
    pendingFolderId = folderId;
    folderSelectTimer.start(150);
}

void MainWindow::onAssetSelectionChanged()
{
    updateSelectionInfo();
    updateInfoPanel();
}

void MainWindow::onAssetDoubleClicked(const QModelIndex &index)
{
    if (!index.isValid()) return;
    showPreview(index.row());
}

void MainWindow::onAssetContextMenu(const QPoint &pos)
{
    // Get index from the currently active view
    QModelIndex index;
    if (isGridMode) {
        index = assetGridView->indexAt(pos);
    } else {
        index = assetTableView->indexAt(pos);
    }

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; }"
        "QMenu::item:selected { background-color: #2f3a4a; }"
    );

    if (index.isValid()) {
        // Asset context menu
        QAction *openAction = menu.addAction("Open Preview");
        QAction *showInExplorerAction = menu.addAction("Show in Explorer");
        menu.addSeparator();


        // Assign Tag submenu
        QMenu *assignTagMenu = menu.addMenu("Assign Tag");
        assignTagMenu->setStyleSheet(menu.styleSheet());

        QVector<QPair<int, QString>> tags = DB::instance().listTags();
        for (const auto& tag : tags) {
            QAction *tagAction = assignTagMenu->addAction(tag.second);
            tagAction->setData(tag.first);

        }
        if (tags.isEmpty()) {
            QAction *noTagsAction = assignTagMenu->addAction("(No tags available)");
            noTagsAction->setEnabled(false);
        }

        // Set Rating submenu
        QMenu *setRatingMenu = menu.addMenu("Set Rating");
        setRatingMenu->setStyleSheet(menu.styleSheet());

        QAction *rating0 = setRatingMenu->addAction("☆☆☆☆☆ (Clear rating)");
        rating0->setData(-1);
        setRatingMenu->addSeparator();
        QAction *rating1 = setRatingMenu->addAction("★☆☆☆☆");
        rating1->setData(1);
        QAction *rating2 = setRatingMenu->addAction("★★☆☆☆");
        rating2->setData(2);
        QAction *rating3 = setRatingMenu->addAction("★★★☆☆");
        rating3->setData(3);
        QAction *rating4 = setRatingMenu->addAction("★★★★☆");
        rating4->setData(4);
        QAction *rating5 = setRatingMenu->addAction("★★★★★");
        rating5->setData(5);

        menu.addSeparator();
        QAction *removeAction = menu.addAction("Remove from App");

        QAction *selected = menu.exec(assetGridView->mapToGlobal(pos));

        if (selected == openAction) {
            showPreview(index.row());
        } else if (selected == showInExplorerAction) {
            QString filePath = index.data(AssetsModel::FilePathRole).toString();
            QFileInfo fileInfo(filePath);
            QStringList args;
            args << "/select," + QDir::toNativeSeparators(fileInfo.absoluteFilePath());
            QProcess::startDetached("explorer", args);
        } else if (selected && assignTagMenu->actions().contains(selected)) {
            // Assign tag action
            int tagId = selected->data().toInt();
            QSet<int> selectedIds = getSelectedAssetIds();
            QList<int> assetIdsList = selectedIds.values();
            QList<int> tagIds = {tagId};

            if (DB::instance().assignTagsToAssets(assetIdsList, tagIds)) {
                updateInfoPanel();
                statusBar()->showMessage(QString("Assigned tag to %1 asset(s)").arg(assetIdsList.size()), 3000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to assign tag");
            }
        } else if (selected && setRatingMenu->actions().contains(selected)) {
            // Set rating action
            int rating = selected->data().toInt();
            QSet<int> selectedIds = getSelectedAssetIds();
            QList<int> assetIdsList = selectedIds.values();

            if (DB::instance().setAssetsRating(assetIdsList, rating)) {
                assetsModel->reload();
                updateInfoPanel();
                QString ratingText = (rating < 0) ? "cleared" : QString::number(rating) + " star(s)";
                statusBar()->showMessage(QString("Set rating to %1 for %2 asset(s)").arg(ratingText).arg(assetIdsList.size()), 3000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to set rating");
            }
        } else if (selected == removeAction) {
            // Remove selected assets from database
            QSet<int> selectedIds = getSelectedAssetIds();
            QList<int> assetIdsList = selectedIds.values();

            QMessageBox::StandardButton reply = QMessageBox::question(
                this, "Remove Assets",
                QString("Are you sure you want to remove %1 asset(s) from the library?\n\nThis will not delete the actual files.").arg(assetIdsList.size()),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                if (DB::instance().removeAssets(assetIdsList)) {
                    assetsModel->reload();
                    clearSelection();
                    statusBar()->showMessage(QString("Removed %1 asset(s) from library").arg(assetIdsList.size()), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to remove assets");
                }
            }
        }
    } else {
        // Empty space context menu
        QAction *clearSelectionAction = menu.addAction("Clear Selection");

        QAction *selected = menu.exec(assetGridView->mapToGlobal(pos));

        if (selected == clearSelectionAction) {
            clearSelection();
        }
    }
}

void MainWindow::onFolderContextMenu(const QPoint &pos)
{
    QModelIndex index = folderTreeView->indexAt(pos);
    if (!index.isValid()) return;

    // Get all selected folders
    QModelIndexList selectedIndexes = folderTreeView->selectionModel()->selectedIndexes();
    if (selectedIndexes.isEmpty()) return;

    // Get info from the clicked folder
    int folderId = folderModel->data(index, VirtualFolderTreeModel::IdRole).toInt();
    QString folderName = folderModel->data(index, Qt::DisplayRole).toString();
    bool isProjectFolder = folderModel->data(index, VirtualFolderTreeModel::IsProjectFolderRole).toBool();
    int projectFolderId = folderModel->data(index, VirtualFolderTreeModel::ProjectFolderIdRole).toInt();

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; }"
        "QMenu::item:selected { background-color: #2f3a4a; }"
    );

    QAction *createAction = menu.addAction("Create Subfolder");
    QAction *renameAction = nullptr;
    QAction *deleteAction = nullptr;

    // Only show rename for single selection
    if (selectedIndexes.size() == 1) {
        renameAction = menu.addAction("Rename");
    }

    // Only allow deletion of non-project folders
    if (!isProjectFolder) {
        deleteAction = menu.addAction("Delete");
    } else {
        deleteAction = menu.addAction("Remove Project Folder");
    }

    QAction *selected = menu.exec(folderTreeView->mapToGlobal(pos));

    if (selected == createAction) {
        // Create subfolder
        bool ok;
        QString name = QInputDialog::getText(this, "Create Subfolder",
                                            "Enter subfolder name:",
                                            QLineEdit::Normal, "", &ok);
        if (ok && !name.isEmpty()) {
            int newId = DB::instance().createFolder(name, folderId);
            if (newId > 0) {
                folderModel->reload();
                statusBar()->showMessage(QString("Created subfolder '%1'").arg(name), 3000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to create subfolder");
            }
        }
    } else if (renameAction && selected == renameAction) {
        // Rename folder (only for single selection)
        bool ok;
        QString newName = QInputDialog::getText(this, "Rename Folder",
                                               "Enter new name:",
                                               QLineEdit::Normal, folderName, &ok);
        if (ok && !newName.isEmpty() && newName != folderName) {
            // If it's a project folder, use the project folder rename method
            if (isProjectFolder) {
                if (DB::instance().renameProjectFolder(projectFolderId, newName)) {
                    folderModel->reload();
                    statusBar()->showMessage(QString("Renamed project folder to '%1'").arg(newName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to rename project folder");
                }
            } else {
                if (DB::instance().renameFolder(folderId, newName)) {
                    folderModel->reload();
                    statusBar()->showMessage(QString("Renamed folder to '%1'").arg(newName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to rename folder");
                }
            }
        }
    } else if (selected == deleteAction) {
        // Collect all selected folders
        QList<int> folderIds;
        QList<int> projectFolderIds;
        QStringList folderNames;

        for (const QModelIndex &idx : selectedIndexes) {
            int id = folderModel->data(idx, VirtualFolderTreeModel::IdRole).toInt();
            QString name = folderModel->data(idx, Qt::DisplayRole).toString();
            bool isProjFolder = folderModel->data(idx, VirtualFolderTreeModel::IsProjectFolderRole).toBool();
            int projFolderId = folderModel->data(idx, VirtualFolderTreeModel::ProjectFolderIdRole).toInt();

            if (isProjFolder) {
                projectFolderIds.append(projFolderId);
            } else {
                folderIds.append(id);
            }
            folderNames.append(name);
        }

        // Show confirmation dialog
        QString message;
        if (selectedIndexes.size() == 1) {
            if (!projectFolderIds.isEmpty()) {
                message = QString("Are you sure you want to remove project folder '%1'?\n\nThis will remove the folder and all its assets from the library, but will not delete the actual files.").arg(folderNames.first());
            } else {
                message = QString("Are you sure you want to delete '%1' and all its contents?").arg(folderNames.first());
            }
        } else {
            message = QString("Are you sure you want to delete %1 folders and all their contents?\n\nFolders: %2")
                .arg(selectedIndexes.size())
                .arg(folderNames.join(", "));
        }

        QMessageBox::StandardButton reply = QMessageBox::question(
            this, selectedIndexes.size() == 1 ? "Delete Folder" : "Delete Folders",
            message,
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes) {
            int deletedCount = 0;

            // Delete project folders
            for (int projFolderId : projectFolderIds) {
                projectFolderWatcher->removeProjectFolder(projFolderId);
                if (DB::instance().deleteProjectFolder(projFolderId)) {
                    deletedCount++;
                }
            }

            // Delete regular folders
            for (int id : folderIds) {
                if (DB::instance().deleteFolder(id)) {
                    deletedCount++;
                }
            }

            folderModel->reload();
            assetsModel->reload();

            if (deletedCount == selectedIndexes.size()) {
                statusBar()->showMessage(QString("Deleted %1 folder(s)").arg(deletedCount), 3000);
            } else {
                QMessageBox::warning(this, "Error",
                    QString("Failed to delete some folders. Deleted %1 of %2").arg(deletedCount).arg(selectedIndexes.size()));
            }
        }
    }
}

void MainWindow::onEmptySpaceContextMenu(const QPoint &pos)
{
    Q_UNUSED(pos);
    clearSelection();
}

void MainWindow::showPreview(int index)
{
    qDebug() << "[MainWindow::showPreview] Called with index:" << index;

    if (index < 0 || index >= assetsModel->rowCount(QModelIndex())) {
        qWarning() << "[MainWindow::showPreview] Invalid index:" << index << "rowCount:" << assetsModel->rowCount(QModelIndex());
        return;
    }

    previewIndex = index;
    QModelIndex modelIndex = assetsModel->index(index, 0);

    QString filePath = modelIndex.data(AssetsModel::FilePathRole).toString();
    QString fileName = modelIndex.data(AssetsModel::FileNameRole).toString();
    QString fileType = modelIndex.data(AssetsModel::FileTypeRole).toString();
    bool isSequence = modelIndex.data(AssetsModel::IsSequenceRole).toBool();

    if (!previewOverlay) {
        previewOverlay = new PreviewOverlay(this);
        previewOverlay->setGeometry(rect());

        connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
        connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changePreview);
    } else {
        // CRITICAL FIX: Stop any playing media before loading new content
        previewOverlay->stopPlayback();
    }

    if (isSequence) {
        // Get sequence information
        QString sequencePattern = modelIndex.data(AssetsModel::SequencePatternRole).toString();
        int startFrame = modelIndex.data(AssetsModel::SequenceStartFrameRole).toInt();
        int endFrame = modelIndex.data(AssetsModel::SequenceEndFrameRole).toInt();
        int frameCount = modelIndex.data(AssetsModel::SequenceFrameCountRole).toInt();

        // Reconstruct frame paths from first frame path and pattern
        QStringList framePaths = reconstructSequenceFramePaths(filePath, startFrame, endFrame);

        if (framePaths.isEmpty()) {
            qWarning() << "[MainWindow::showPreview] No frame paths reconstructed! Cannot show sequence.";
            QMessageBox::warning(this, "Error", "Failed to reconstruct sequence frame paths.");
            return;
        }

        previewOverlay->showSequence(framePaths, sequencePattern, startFrame, endFrame);
    } else {
        previewOverlay->showAsset(filePath, fileName, fileType);
    }
}

void MainWindow::closePreview()
{
    // Preserve the last asset index for restoring Asset Manager focus
    const int lastAssetIndex = previewIndex;
    previewIndex = -1;

    if (previewOverlay) {
        // Stop any playback (video, fallback, sequence) before hiding/deleting
        previewOverlay->stopPlayback();
        previewOverlay->hide();
        previewOverlay->deleteLater();
        previewOverlay = nullptr;
    }

    // 1) If preview was opened from File Manager, restore focus/selection there
    if (fmOverlaySourceView && fmOverlayCurrentIndex.isValid()) {
        if (QItemSelectionModel *sel = fmOverlaySourceView->selectionModel()) {
            sel->setCurrentIndex(fmOverlayCurrentIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        } else {
            fmOverlaySourceView->setCurrentIndex(fmOverlayCurrentIndex);
        }
        fmOverlaySourceView->setFocus();
        return; // Done
    }

    // 2) Otherwise, restore focus/selection to Asset Manager
    if (lastAssetIndex >= 0) {
        if (isGridMode && assetGridView && assetsModel) {
            QModelIndex idx = assetsModel->index(lastAssetIndex, 0);
            if (idx.isValid()) {
                assetGridView->setCurrentIndex(idx);
                assetGridView->setFocus();
            }
        } else if (assetTableView && assetTableView->model()) {
            QModelIndex idx = assetTableView->model()->index(lastAssetIndex, 0);
            if (idx.isValid()) {
                assetTableView->setCurrentIndex(idx);
                assetTableView->setFocus();
            }
        }
    }
}

void MainWindow::changePreview(int delta)
{
    if (previewIndex < 0) return;

    int newIndex = previewIndex + delta;
    if (newIndex >= 0 && newIndex < assetsModel->rowCount(QModelIndex())) {
        showPreview(newIndex);
    }
}


void MainWindow::changeFmPreview(int delta)
{
    if (!previewOverlay) return;
    QModelIndex cur = fmOverlayCurrentIndex;
    if (!cur.isValid()) {
        // fallback: try current selection from focused view
        if (fmGridView && fmGridView->hasFocus()) cur = fmGridView->currentIndex();
        else if (fmListView && fmListView->hasFocus()) cur = fmListView->currentIndex();
        if (!cur.isValid()) return;
        cur = cur.sibling(cur.row(), 0);
        fmOverlayCurrentIndex = QPersistentModelIndex(cur);
        fmOverlaySourceView = (fmGridView && fmGridView->hasFocus()) ? static_cast<QAbstractItemView*>(fmGridView) : static_cast<QAbstractItemView*>(fmListView);
    }
    QAbstractItemModel* model = const_cast<QAbstractItemModel*>(cur.model());
    if (!model) return;
    int newRow = cur.row() + delta;
    if (newRow < 0) return;
    if (newRow >= model->rowCount(cur.parent())) return;
    QModelIndex next = model->index(newRow, 0, cur.parent());
    if (!next.isValid()) return;

    // Update context
    fmOverlayCurrentIndex = QPersistentModelIndex(next);
    if (fmOverlaySourceView) {
        fmOverlaySourceView->setCurrentIndex(next);
        fmOverlaySourceView->scrollTo(next, QAbstractItemView::PositionAtCenter);
    }

    // Handle grouping representative
    if (fmProxyModel && fmGroupSequences && next.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(next)) {
        auto info = fmProxyModel->infoForProxyIndex(next);
        QStringList frames = reconstructSequenceFramePaths(info.reprPath, info.start, info.end);
        if (!frames.isEmpty()) {
            previewOverlay->stopPlayback();
            int pad = 0;
            auto m = SequenceDetector::mainPattern().match(QFileInfo(info.reprPath).fileName());
            if (m.hasMatch()) pad = m.captured(3).length(); else pad = QString::number(info.start).length();
            QString s0 = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
            QString s1 = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
            QString seqName = QString("%1.[%2-%3].%4").arg(info.base, s0, s1, info.ext);
            previewOverlay->showSequence(frames, seqName, info.start, info.end);
            return;
        }
    }

    // Map to source if needed and show asset
    QModelIndex srcIdx = next;
    if (fmProxyModel && next.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(next);
    QString path = fmDirModel ? fmDirModel->filePath(srcIdx) : QString();
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (!fi.exists()) return;
    previewOverlay->stopPlayback();
    previewOverlay->showAsset(path, fi.fileName(), fi.suffix());
}


QItemSelectionModel* MainWindow::getCurrentSelectionModel()
{
    return isGridMode ? assetGridView->selectionModel() : assetTableView->selectionModel();
}

void MainWindow::updateInfoPanel()
{
    QModelIndexList selected = getCurrentSelectionModel()->selectedIndexes();

    if (selected.isEmpty()) {
        infoFileName->setText("No selection");
        infoFilePath->clear();
        infoFileSize->clear();
        infoFileType->clear();
        infoDimensions->clear();
        infoCreated->clear();
        infoModified->clear();
        infoPermissions->clear();
        infoRatingLabel->setVisible(false);
        infoRatingWidget->setVisible(false);
        infoTags->clear();
        if (versionTable) { versionTable->setRowCount(0); }
        if (versionsTitleLabel) { versionsTitleLabel->setText("Version History"); }
        if (revertVersionButton) { revertVersionButton->setEnabled(false); }

        return;
    }

    if (selected.size() == 1) {
        QModelIndex index = selected.first();
        QString fileName = index.data(AssetsModel::FileNameRole).toString();
        QString filePath = index.data(AssetsModel::FilePathRole).toString();
        qint64 fileSize = index.data(AssetsModel::FileSizeRole).toLongLong();
        QString fileType = index.data(AssetsModel::FileTypeRole).toString();
        QDateTime modified = index.data(AssetsModel::LastModifiedRole).toDateTime();
        int rating = index.data(AssetsModel::RatingRole).toInt();
        bool isSequence = index.data(AssetsModel::IsSequenceRole).toBool();

        infoFileName->setText(fileName);
        infoFilePath->setText(filePath);

        QFileInfo fileInfo(filePath);

        // Format file size
        QString sizeStr;
        if (fileSize < 1024) {
            sizeStr = QString::number(fileSize) + " B";
        } else if (fileSize < 1024 * 1024) {
            sizeStr = QString::number(fileSize / 1024.0, 'f', 1) + " KB";
        } else if (fileSize < 1024 * 1024 * 1024) {
            sizeStr = QString::number(fileSize / (1024.0 * 1024.0), 'f', 1) + " MB";
        } else {
            sizeStr = QString::number(fileSize / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
        }
        infoFileSize->setText("Size: " + sizeStr.toLower());

        infoFileType->setText("Type: " + fileType.toUpper());

        // Extract dimensions for images and videos
        QString dimensionsStr;
        if (isSequence) {
            int frameCount = index.data(AssetsModel::SequenceFrameCountRole).toInt();
            int startFrame = index.data(AssetsModel::SequenceStartFrameRole).toInt();
            int endFrame = index.data(AssetsModel::SequenceEndFrameRole).toInt();
            bool hasGaps = index.data(AssetsModel::SequenceHasGapsRole).toBool();
            int gapCount = index.data(AssetsModel::SequenceGapCountRole).toInt();
            QString version = index.data(AssetsModel::SequenceVersionRole).toString();

            // Try to get dimensions from first frame
            QImageReader reader(filePath);
            if (reader.canRead()) {
                QSize size = reader.size();
                dimensionsStr = QString("Dimensions: %1 x %2 (%3 frames: %4-%5)")
                    .arg(size.width()).arg(size.height())
                    .arg(frameCount).arg(startFrame).arg(endFrame);
            } else {
                dimensionsStr = QString("Sequence: %1 frames (%2-%3)")
                    .arg(frameCount).arg(startFrame).arg(endFrame);
            }

            // Add gap warning if present
            if (hasGaps) {
                int expectedFrames = endFrame - startFrame + 1;
                int missingFrames = expectedFrames - frameCount;
                dimensionsStr += QString("\n⚠ WARNING: %1 gap(s), %2 missing frame(s)")
                    .arg(gapCount).arg(missingFrames);
            }

            // Add version info if present
            if (!version.isEmpty()) {
                dimensionsStr += QString("\nVersion: %1").arg(version);
            }
        } else {
            // Check if it's an image
            QStringList imageExts = {"jpg", "jpeg", "png", "gif", "bmp", "tiff", "tif", "webp",
                                     "exr", "hdr", "psd", "psb", "tga", "dng", "cr2", "cr3",
                                     "nef", "arw", "orf", "rw2", "pef", "srw", "raf", "raw"};

            if (imageExts.contains(fileType.toLower())) {
                QImageReader reader(filePath);
                if (reader.canRead()) {
                    QSize size = reader.size();
                    QString format = reader.format();
                    dimensionsStr = QString("Dimensions: %1 x %2 (%3)")
                        .arg(size.width()).arg(size.height()).arg(QString(format).toUpper());
                } else {
                    dimensionsStr = "Dimensions: Unable to read";
                }
            }
            // Check if it's a video
            else {
                QStringList videoExts = {"mp4", "mov", "avi", "mkv", "wmv", "flv", "webm",
                                        "m4v", "mpg", "mpeg", "3gp", "mts", "m2ts"};
                if (videoExts.contains(fileType.toLower())) {
                    // Extract video metadata using QMediaPlayer
                    QMediaPlayer tempPlayer;
                    QAudioOutput tempAudio;
                    tempPlayer.setAudioOutput(&tempAudio);
                    tempPlayer.setSource(QUrl::fromLocalFile(filePath));

                    // Wait briefly for metadata to load
                    QEventLoop loop;
                    QTimer timeout;
                    timeout.setSingleShot(true);
                    timeout.setInterval(1000); // 1 second timeout

                    bool metadataLoaded = false;
                    connect(&tempPlayer, &QMediaPlayer::metaDataChanged, &loop, [&]() {
                        metadataLoaded = true;
                        loop.quit();
                    });
                    connect(&tempPlayer, &QMediaPlayer::mediaStatusChanged, &loop, [&](QMediaPlayer::MediaStatus status) {
                        if (status == QMediaPlayer::LoadedMedia) {
                            metadataLoaded = true;
                            loop.quit();
                        }
                    });
                    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

                    timeout.start();
                    loop.exec();

                    QStringList videoInfo;

                    // Try to get codec information from all available metadata
                    QMediaMetaData metadata = tempPlayer.metaData();

                    // Video codec (do not add to UI yet; we may replace with FFmpeg + profile)
                    QString videoCodec;
                    if (metadata.value(QMediaMetaData::VideoCodec).isValid()) {
                        videoCodec = metadata.value(QMediaMetaData::VideoCodec).toString();
                    }
                    if (videoCodec.isEmpty() && metadata.stringValue(QMediaMetaData::VideoCodec).length() > 0) {
                        videoCodec = metadata.stringValue(QMediaMetaData::VideoCodec);
                    }
                    // Treat "UNSPECIFIED" / "UNKNOWN" as missing


                    if (!videoCodec.isEmpty()) {
                        const QString vc = videoCodec.trimmed();
                        if (vc.compare("UNSPECIFIED", Qt::CaseInsensitive) == 0 ||
                            vc.compare("UNKNOWN", Qt::CaseInsensitive) == 0) {
                            videoCodec.clear();
                        }
                    }

                    // Audio codec
                    QString audioCodec;
                    if (metadata.value(QMediaMetaData::AudioCodec).isValid()) {
                        audioCodec = metadata.value(QMediaMetaData::AudioCodec).toString();
                    }
                    if (audioCodec.isEmpty() && metadata.stringValue(QMediaMetaData::AudioCodec).length() > 0) {
                        audioCodec = metadata.stringValue(QMediaMetaData::AudioCodec);
                    }
                    if (!audioCodec.isEmpty()) {
                        videoInfo << QString("Audio Codec: %1").arg(audioCodec.toUpper());
                    }

                    // Bitrate
                    bool hasBitrate = false;
                    if (metadata.value(QMediaMetaData::VideoBitRate).isValid()) {
                        int bitrate = metadata.value(QMediaMetaData::VideoBitRate).toInt();
                        if (bitrate > 0) {
                            hasBitrate = true;
                            double mbps = bitrate / 1000000.0;
                            videoInfo << QString("Bitrate: %1 Mbps").arg(mbps, 0, 'f', 2);
                        }
                    }

                    // Resolution
                    bool hasResolution = false;
                    QVariant resVar = metadata.value(QMediaMetaData::Resolution);
                    if (resVar.isValid() && resVar.canConvert<QSize>()) {
                        QSize resolution = resVar.toSize();
                        if (resolution.width() > 0 && resolution.height() > 0) {
                            hasResolution = true;
                            videoInfo << QString("Frame Size: %1x%2").arg(resolution.width()).arg(resolution.height());
                        }
                    }

                    // Framerate
                    bool hasFps = false;
                    if (metadata.value(QMediaMetaData::VideoFrameRate).isValid()) {

                        double fps = metadata.value(QMediaMetaData::VideoFrameRate).toDouble();
                        if (fps > 0) {
                            hasFps = true;
                            videoInfo << QString("FPS: %1").arg(fps, 0, 'f', 0);
                        }
                    }

                    // FFmpeg probing for reliable codecs, profiles, and details
#ifdef HAVE_FFMPEG
                    MediaInfo::VideoMetadata ff;
                    QString ffErr;
                    if (MediaInfo::probeVideoFile(filePath, ff, &ffErr)) {
                        // Fill missing audio/bitrate/resolution/fps
                        if (audioCodec.isEmpty() && !ff.audioCodec.isEmpty()) {
                            videoInfo << QString("Audio Codec: %1").arg(ff.audioCodec.toUpper());
                        }
                        if (!hasBitrate && ff.bitrate > 0) {
                            double mbps = ff.bitrate / 1000000.0;
                            videoInfo << QString("Bitrate: %1 Mbps").arg(mbps, 0, 'f', 2);
                        }
                        if (!hasResolution && ff.width > 0 && ff.height > 0) {
                            videoInfo << QString("Frame Size: %1x%2").arg(ff.width).arg(ff.height);
                        }
                        if (!hasFps && ff.fps > 0) {
                            videoInfo << QString("FPS: %1").arg(ff.fps, 0, 'f', 0);
                        }
                    }
#endif


                    // Compose final Video Codec line once (prefer Qt value unless empty/unspecified; append FFmpeg profile if available)
                    {
                        QString finalCodec = videoCodec;
                        QString finalProfile;
#ifdef HAVE_FFMPEG
                        if (finalCodec.isEmpty() && !ff.videoCodec.isEmpty()) {
                            finalCodec = ff.videoCodec;
                        }
                        if (!ff.videoProfile.isEmpty()) {
                            finalProfile = ff.videoProfile;
                        }
#endif
                        if (!finalCodec.isEmpty()) {
                            const QString line = finalProfile.isEmpty()
                                ? QString("Video Codec: %1").arg(finalCodec.toUpper())
                                : QString("Video Codec: %1 %2").arg(finalCodec.toUpper(), finalProfile.toUpper());
                            videoInfo << line;
                        }
                    }

                    if (!videoInfo.isEmpty()) {
                        dimensionsStr = videoInfo.join("\n");
                    } else {
                        dimensionsStr = "Video file";
                    }
                }
            }
        }

        if (!dimensionsStr.isEmpty()) {
            infoDimensions->setText(dimensionsStr);
            infoDimensions->setVisible(true);
        } else {
            infoDimensions->clear();
            infoDimensions->setVisible(false);
        }

        // Creation and modification dates
        if (fileInfo.exists()) {
            QDateTime created = fileInfo.birthTime();
            if (created.isValid()) {
                infoCreated->setText("Created: " + created.toString("dd-MM-yyyy"));
                infoCreated->setVisible(true);
            } else {
                infoCreated->clear();
                infoCreated->setVisible(false);
            }

            infoModified->setText("Modified: " + modified.toString("dd-MM-yyyy"));

            // File permissions
            QStringList perms;
            if (fileInfo.isReadable()) perms << "R";
            if (fileInfo.isWritable()) perms << "W";
            if (fileInfo.isExecutable()) perms << "X";
            if (fileInfo.isHidden()) perms << "Hidden";

            infoPermissions->setText("Permissions: " + perms.join(", "));
            infoPermissions->setVisible(true);
        } else {
            infoCreated->clear();
            infoCreated->setVisible(false);
            infoModified->setText("Modified: File not found");
            infoPermissions->clear();
            infoPermissions->setVisible(false);
        }

        // Show rating widget
        infoRatingLabel->setVisible(true);
        infoRatingWidget->setVisible(true);
        infoRatingWidget->setReadOnly(false);
        infoRatingWidget->setRating(rating);

        // Load tags for this asset
        int assetId = index.data(AssetsModel::IdRole).toInt();
        QStringList tags = DB::instance().tagsForAsset(assetId);
        if (tags.isEmpty()) {
            infoTags->setText("Tags: None");


        // Load version history for this asset
        reloadVersionHistory();


        } else {
            infoTags->setText("Tags: " + tags.join(", "));

        // Load version history for this asset
        reloadVersionHistory();


        }
    } else {
        if (versionTable) { versionTable->setRowCount(0); }
        if (versionsTitleLabel) { versionsTitleLabel->setText("Version History"); }
        if (revertVersionButton) { revertVersionButton->setEnabled(false); }

        infoFileName->setText(QString("%1 assets selected").arg(selected.size()));
        infoFilePath->clear();

        infoFileSize->clear();
        infoFileType->clear();
        infoDimensions->clear();
        infoCreated->clear();
        infoModified->clear();
        infoPermissions->clear();
        infoRatingLabel->setVisible(false);
        infoRatingWidget->setVisible(false);
        infoTags->clear();
    }
}

void MainWindow::onRatingChanged(int rating)
{
    // Get currently selected asset
    QModelIndexList selected = getCurrentSelectionModel()->selectedIndexes();
    if (selected.size() != 1) return;

    int assetId = selected.first().data(AssetsModel::IdRole).toInt();

    // Update rating in database
    if (DB::instance().setAssetsRating({assetId}, rating)) {
        assetsModel->reload();
        statusBar()->showMessage(QString("Rating set to %1 star%2").arg(rating).arg(rating == 1 ? "" : "s"), 2000);
    } else {
        QMessageBox::warning(this, "Error", "Failed to set rating");
    }
}

void MainWindow::updateSelectionInfo()
{
    // Update internal selection tracking
    selectedAssetIds.clear();
    QModelIndexList selected = getCurrentSelectionModel()->selectedIndexes();
    for (const QModelIndex &index : selected) {
        int assetId = index.data(AssetsModel::IdRole).toInt();
        selectedAssetIds.insert(assetId);
    }
}

QSet<int> MainWindow::getSelectedAssetIds() const
{
    return selectedAssetIds;
}

int MainWindow::getAnchorIndex() const
{
    return anchorIndex;
}

void MainWindow::selectAsset(int assetId, int index, Qt::KeyboardModifiers modifiers)
{
    Q_UNUSED(assetId);

    if (modifiers & Qt::ControlModifier) {
        // Toggle selection
        QModelIndex idx = assetsModel->index(index, 0);
        assetGridView->selectionModel()->select(idx, QItemSelectionModel::Toggle);
    } else if (modifiers & Qt::ShiftModifier) {
        // Range selection
        if (anchorIndex >= 0) {
            selectRange(anchorIndex, index);
        } else {
            selectSingle(assetId, index);
        }
    } else {
        selectSingle(assetId, index);
    }
}

void MainWindow::selectSingle(int assetId, int index)
{
    Q_UNUSED(assetId);
    assetGridView->selectionModel()->clearSelection();
    QModelIndex idx = assetsModel->index(index, 0);
    assetGridView->selectionModel()->select(idx, QItemSelectionModel::Select);
    anchorIndex = index;
}

void MainWindow::toggleSelection(int assetId, int index)
{
    Q_UNUSED(assetId);
    QModelIndex idx = assetsModel->index(index, 0);
    assetGridView->selectionModel()->select(idx, QItemSelectionModel::Toggle);
}

void MainWindow::selectRange(int fromIndex, int toIndex)
{
    assetGridView->selectionModel()->clearSelection();

    int start = qMin(fromIndex, toIndex);
    int end = qMax(fromIndex, toIndex);

    for (int i = start; i <= end; i++) {
        QModelIndex idx = assetsModel->index(i, 0);
        assetGridView->selectionModel()->select(idx, QItemSelectionModel::Select);
    }
}

void MainWindow::clearSelection()
{
    assetGridView->selectionModel()->clearSelection();
    selectedAssetIds.clear();
    anchorIndex = -1;
    currentAssetId = -1;
}

void MainWindow::applyFilters()
{
    // Filters are applied automatically via:
    // - Search box (onSearchTextChanged)
    // - Rating filter (connected to model)
    // - Tags (Filter by Tags button)
    // This button is kept for future batch filter application if needed
    statusBar()->showMessage("Filters are active", 2000);
}

void MainWindow::clearFilters()
{
    searchBox->clear();
    ratingFilter->setCurrentIndex(0);
    tagsListView->clearSelection();

    // Clear tag filter in model
    assetsModel->setSelectedTagNames(QStringList());

    statusBar()->showMessage("Filters cleared", 2000);
}

void MainWindow::onSearchTextChanged(const QString &text)
{
    assetsModel->setSearchQuery(text);

    if (text.isEmpty()) {
        statusBar()->showMessage("Search cleared", 1000);
    } else {
        statusBar()->showMessage(QString("Searching for: %1").arg(text), 2000);
    }
}

void MainWindow::onCreateTag()
{
    bool ok;
    QString tagName = QInputDialog::getText(this, "Create Tag", "Tag name:", QLineEdit::Normal, "", &ok);

    if (ok && !tagName.isEmpty()) {
        int tagId = tagsModel->createTag(tagName);
        if (tagId > 0) {
            statusBar()->showMessage(QString("Tag '%1' created").arg(tagName), 2000);
        } else {
            QMessageBox::warning(this, "Error", "Failed to create tag. Tag may already exist.");
        }
    }
}

void MainWindow::onApplyTags()
{
    // Get selected tags
    QModelIndexList selectedTagIndexes = tagsListView->selectionModel()->selectedIndexes();
    if (selectedTagIndexes.isEmpty()) {
        statusBar()->showMessage("No tags selected", 2000);
        return;
    }

    // Get selected assets
    QSet<int> assetIds = getSelectedAssetIds();
    if (assetIds.isEmpty()) {
        statusBar()->showMessage("No assets selected", 2000);
        return;
    }

    // Collect tag IDs
    QList<int> tagIds;
    for (const QModelIndex &index : selectedTagIndexes) {
        int tagId = index.data(TagsModel::IdRole).toInt();
        if (tagId > 0) {
            tagIds.append(tagId);
        }
    }

    if (tagIds.isEmpty()) {
        return;
    }

    // Apply tags to assets
    QList<int> assetIdList = assetIds.values();
    if (DB::instance().assignTagsToAssets(assetIdList, tagIds)) {
        statusBar()->showMessage(QString("Applied %1 tag(s) to %2 asset(s)").arg(tagIds.size()).arg(assetIds.size()), 3000);
        updateInfoPanel();
    } else {
        QMessageBox::warning(this, "Error", "Failed to apply tags");
    }
}

void MainWindow::onFilterByTags()
{
    // Get selected tags
    QModelIndexList selectedTagIndexes = tagsListView->selectionModel()->selectedIndexes();
    if (selectedTagIndexes.isEmpty()) {
        assetsModel->setSelectedTagNames(QStringList());
        statusBar()->showMessage("Tag filter cleared", 2000);
        return;
    }

    QStringList tagNames;
    for (const QModelIndex &index : selectedTagIndexes) {
        QString tagName = index.data(TagsModel::NameRole).toString();
        if (!tagName.isEmpty()) tagNames.append(tagName);
    }
    if (tagNames.isEmpty()) return;

    int mode = tagFilterModeCombo->currentIndex(); // 0 = AND, 1 = OR
    QString modeText = (mode == AssetsModel::And) ? "AND" : "OR";

    assetsModel->setSelectedTagNames(tagNames);
    assetsModel->setTagFilterMode(mode);

    QString message = (tagNames.size() == 1)
        ? QString("Filtering by tag: %1").arg(tagNames.first())
        : QString("Filtering by %1 tag(s) (%2 logic)").arg(tagNames.size()).arg(modeText);
    statusBar()->showMessage(message, 3000);
}

#if 0 // cleanup: removed misplaced version history block

            iconItem->setText(" 39d7");

            if (tagsModel->deleteTag(tagId)) {
                statusBar()->showMessage(QString("Tag '%1' deleted").arg(tagName), 2000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to delete tag.");
            }
        }
    } else if (selected == mergeAction) {
        // Merge tag into another
        QVector<QPair<int, QString>> allTags = DB::instance().listTags();
        QStringList tagNames;
        QList<int> tagIds;

        for (const auto& tag : allTags) {
            if (tag.first != tagId) { // Exclude current tag
                tagNames.append(tag.second);
                tagIds.append(tag.first);
            }
        }

        if (tagNames.isEmpty()) {
            QMessageBox::information(this, "Merge Tag", "No other tags available to merge into.");
            return;
        }

        bool ok;
        QString targetTagName = QInputDialog::getItem(this, "Merge Tag",
            QString("Merge tag '%1' into:").arg(tagName),
            tagNames, 0, false, &ok);

        if (ok && !targetTagName.isEmpty()) {
            int targetTagId = tagIds[tagNames.indexOf(targetTagName)];

            QMessageBox::StandardButton reply = QMessageBox::question(this, "Merge Tag",
                QString("Merge tag '%1' into '%2'?\n\nAll assets tagged with '%1' will be tagged with '%2' instead, and '%1' will be deleted.")
                    .arg(tagName).arg(targetTagName),
                QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                if (DB::instance().mergeTags(tagId, targetTagId)) {
                    tagsModel->reload();
                    assetsModel->reload();
                    statusBar()->showMessage(QString("Tag '%1' merged into '%2'").arg(tagName).arg(targetTagName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to merge tags.");
                }
            }
        }
#endif // cleanup

void MainWindow::onTagContextMenu(const QPoint &pos)
{
    QModelIndex index = tagsListView->indexAt(pos);
    if (!index.isValid()) return;

    int tagId = index.data(TagsModel::IdRole).toInt();
    QString tagName = index.data(TagsModel::NameRole).toString();

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background-color: #2a2a2a; color: #ffffff; border: 1px solid #444; }"
        "QMenu::item:selected { background-color: #3a3a3a; }"
    );

    QAction *renameAction = menu.addAction("Rename Tag");
    QAction *deleteAction = menu.addAction("Delete Tag");
    menu.addSeparator();
    QAction *mergeAction = menu.addAction("Merge Into...");

    QAction *selected = menu.exec(tagsListView->mapToGlobal(pos));

    if (selected == renameAction) {
        bool ok;
        QString newName = QInputDialog::getText(this, "Rename Tag",
            QString("Rename tag '%1' to:").arg(tagName),
            QLineEdit::Normal, tagName, &ok);
        if (ok && !newName.isEmpty() && newName != tagName) {
            if (tagsModel->renameTag(tagId, newName)) {
                statusBar()->showMessage(QString("Tag renamed to '%1'").arg(newName), 2000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to rename tag. Tag name may already exist.");
            }
        }
    } else if (selected == deleteAction) {
        QMessageBox::StandardButton reply = QMessageBox::question(this, "Delete Tag",
            QString("Are you sure you want to delete tag '%1'?\n\nThis will remove the tag from all assets.").arg(tagName),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            if (tagsModel->deleteTag(tagId)) {
                statusBar()->showMessage(QString("Tag '%1' deleted").arg(tagName), 2000);
            } else {
                QMessageBox::warning(this, "Error", "Failed to delete tag.");
            }
        }
    } else if (selected == mergeAction) {
        QVector<QPair<int, QString>> allTags = DB::instance().listTags();
        QStringList tagNamesList;
        QList<int> tagIds;
        for (const auto& tag : allTags) {
            if (tag.first != tagId) {
                tagNamesList.append(tag.second);
                tagIds.append(tag.first);
            }
        }
        if (tagNamesList.isEmpty()) {
            QMessageBox::information(this, "Merge Tag", "No other tags available to merge into.");
            return;
        }
        bool ok;
        QString targetTagName = QInputDialog::getItem(this, "Merge Tag",
            QString("Merge tag '%1' into:").arg(tagName),
            tagNamesList, 0, false, &ok);
        if (ok && !targetTagName.isEmpty()) {
            int targetTagId = tagIds[tagNamesList.indexOf(targetTagName)];
            QMessageBox::StandardButton reply = QMessageBox::question(this, "Merge Tag",
                QString("Merge tag '%1' into '%2'?\n\nAll assets tagged with '%1' will be tagged with '%2' instead, and '%1' will be deleted.")
                    .arg(tagName).arg(targetTagName),
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                if (DB::instance().mergeTags(tagId, targetTagId)) {
                    tagsModel->reload();
                    assetsModel->reload();
                    statusBar()->showMessage(QString("Tag '%1' merged into '%2'").arg(tagName).arg(targetTagName), 3000);
                } else {
                    QMessageBox::warning(this, "Error", "Failed to merge tags.");
                }
            }
        }
    }
}


void MainWindow::updateTagButtonStates()
{
    bool hasSelectedTags = !tagsListView->selectionModel()->selectedIndexes().isEmpty();
    bool hasSelectedAssets = !getSelectedAssetIds().isEmpty();

    // Apply button: enabled only when both tags AND assets are selected
    applyTagsBtn->setEnabled(hasSelectedTags && hasSelectedAssets);

    // Filter button: enabled when tags are selected
    filterByTagsBtn->setEnabled(hasSelectedTags);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        statusBar()->showMessage("Drop files here to import...");
    } else {
        event->ignore();
    }
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    Q_UNUSED(event);
    statusBar()->clearMessage();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    statusBar()->clearMessage();

    const QMimeData *mimeData = event->mimeData();

    if (mimeData->hasUrls()) {
        QStringList filePaths;
        QStringList folderPaths;
        QList<QUrl> urls = mimeData->urls();

        // Get currently selected folder ID
        QModelIndex currentFolderIndex = folderTreeView->currentIndex();
        int parentFolderId = 0;
        if (currentFolderIndex.isValid()) {
            parentFolderId = folderModel->data(currentFolderIndex, VirtualFolderTreeModel::IdRole).toInt();
        }
        if (parentFolderId <= 0) {
            parentFolderId = folderModel->rootId();
        }

        for (const QUrl &url : urls) {
            if (url.isLocalFile()) {
                QString path = url.toLocalFile();
                QFileInfo info(path);

                if (info.isFile()) {
                    filePaths.append(path);
                } else if (info.isDir()) {
                    // Keep directories separate to preserve structure
                    folderPaths.append(path);
                }
            }
        }

        int totalImported = 0;

        // Create and show import progress dialog
        if (!importProgressDialog) {
            importProgressDialog = new ImportProgressDialog(this);
        }
        importProgressDialog->show();
        importProgressDialog->raise();
        importProgressDialog->activateWindow();

        // Disconnect importFinished temporarily to prevent premature dialog closure
        disconnect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);

        // Import folders with structure preservation
        for (const QString &folderPath : folderPaths) {
            if (importer->importFolder(folderPath, parentFolderId)) {
                totalImported++;
            }
        }

        // Import individual files
        if (!filePaths.isEmpty()) {
            importFiles(filePaths);
            totalImported += filePaths.size();
        }

        // Reconnect importFinished signal
        connect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);

        // Manually trigger import complete
        onImportComplete();

        if (totalImported > 0) {
            statusBar()->showMessage(QString("Import complete: %1 item(s)").arg(totalImported), 3000);
        } else {
            statusBar()->showMessage("No valid files to import", 3000);
        }

        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void MainWindow::importFiles(const QStringList &filePaths)
{
    if (!folderTreeView->currentIndex().isValid()) {
        QMessageBox::warning(this, "No Folder Selected", "Please select a folder before importing files.");
        return;
    }

    int folderId = folderTreeView->currentIndex().data(VirtualFolderTreeModel::IdRole).toInt();

    // Create and show import progress dialog
    if (!importProgressDialog) {
        importProgressDialog = new ImportProgressDialog(this);
    }
    importProgressDialog->show();
    importProgressDialog->raise();
    importProgressDialog->activateWindow();

    // Start import
    importer->importFiles(filePaths, folderId);
}

void MainWindow::onImportProgress(int current, int total)
{
    // Update progress dialog
    if (importProgressDialog) {
        importProgressDialog->setProgress(current, total);
    }

    // Also update status bar
    statusBar()->showMessage(QString("Importing: %1 of %2 files...").arg(current).arg(total));
}

void MainWindow::onImportFileChanged(const QString& fileName)
{
    // Update progress dialog with current file
    if (importProgressDialog) {
        importProgressDialog->setCurrentFile(fileName);
    }
}

void MainWindow::onImportFolderChanged(const QString& folderName)
{
    // Update progress dialog with current folder
    if (importProgressDialog) {
        importProgressDialog->setCurrentFolder(folderName);
    }
}

void MainWindow::onImportComplete()
{
    // Close and delete the import progress dialog
    if (importProgressDialog) {
        importProgressDialog->accept();  // Close the dialog
        importProgressDialog->deleteLater();
        importProgressDialog = nullptr;
    }

    statusBar()->showMessage("Import complete", 3000);

    // Reload assets model to show new imports
    assetsModel->reload();

    // Warm live preview cache for all assets in current folder
    QList<int> assetIds;
    for (int row = 0; row < assetsModel->rowCount(QModelIndex()); ++row) {
        QModelIndex index = assetsModel->index(row, 0);
        int assetId = index.data(AssetsModel::IdRole).toInt();
        assetIds.append(assetId);
    }

    if (!assetIds.isEmpty()) {
        qDebug() << "[MainWindow] Prefetching live previews for" << assetIds.size() << "assets";

        QStringList filePaths;
        for (int assetId : assetIds) {
            QString filePath = DB::instance().getAssetFilePath(assetId);
            if (!filePath.isEmpty()) {
                filePaths.append(filePath);
            }
        }

        LivePreviewManager &previewMgr = LivePreviewManager::instance();
        QSize targetSize = assetGridView ? assetGridView->iconSize() : QSize(180, 180);
        if (!targetSize.isValid()) targetSize = QSize(180, 180);
        for (const QString &filePath : filePaths) {
            previewMgr.requestFrame(filePath, targetSize);
        }
        scheduleVisibleThumbProgressUpdate();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // During UI construction, ignore heavy logic in event filter
    if (m_initializing) {
        return false; // do not intercept; let normal processing continue
    }
    // Update visible-only progress when asset viewports resize
    if ((watched == assetGridView->viewport() || watched == assetTableView->viewport()) && event->type() == QEvent::Resize) {
        scheduleVisibleThumbProgressUpdate();
    }

    // Handle Space key on asset views to toggle preview (open/close)
    if ((watched == assetGridView || watched == assetTableView) && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Space && !keyEvent->isAutoRepeat()) {
            if (previewOverlay && previewOverlay->isVisible()) {
                closePreview();
                return true;
            }
            // Get the current selection
            QItemSelectionModel *selectionModel = isGridMode ? assetGridView->selectionModel() : assetTableView->selectionModel();
            QModelIndexList selected = selectionModel->selectedIndexes();
            if (!selected.isEmpty()) {
                // Open preview for the first selected item
                QModelIndex index = selected.first();
                showPreview(index.row());
                return true; // Event handled
            }
        }
    }

    // Mouse wheel zoom for File Manager image preview
    if ((watched == fmImageView || (fmImageView && watched == fmImageView->viewport())) && event->type() == QEvent::Wheel) {
        QWheelEvent *wheel = static_cast<QWheelEvent*>(event);
        const int delta = wheel->angleDelta().y();
        double factor = (delta > 0) ? 1.15 : 0.85;
        if (fmImageView) fmImageView->scale(factor, factor);
        // User performed manual zoom; stop auto-fit
        fmImageFitToView = false;
        return true;
    }
    // Keep image fitted on view resize if auto-fit is active
    if ((watched == fmImageView || (fmImageView && watched == fmImageView->viewport())) && event->type() == QEvent::Resize) {
        if (fmImageFitToView && fmImageView && fmImageItem && !fmImageItem->pixmap().isNull()) {
            fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
        }
    }

    // Handle drops on File Manager views (copy files/folders or assets into current directory)
    if ((fmGridView && watched == fmGridView->viewport()) || (fmListView && watched == fmListView->viewport())) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasUrls() || dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids")) {
                const bool shift = dragEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                dragEvent->setDropAction(shift ? Qt::MoveAction : Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::DragMove) {
            QDragMoveEvent *dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasUrls() || dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids")) {
                const bool shift = dragEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                dragEvent->setDropAction(shift ? Qt::MoveAction : Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData *mimeData = dropEvent->mimeData();
            const QString destDir = fmDirModel ? fmDirModel->rootPath() : QString();
            if (destDir.isEmpty()) return false;
            QStringList sources;
            if (mimeData->hasUrls()) {
                for (const QUrl &url : mimeData->urls()) {
                    if (url.isLocalFile()) sources << url.toLocalFile();
                }
            } else if (mimeData->hasFormat("application/x-kasset-asset-ids")) {
                QByteArray encodedData = mimeData->data("application/x-kasset-asset-ids");
                QDataStream stream(&encodedData, QIODevice::ReadOnly);
                QList<int> assetIds; stream >> assetIds;
                for (int id : assetIds) {
                    const QString src = DB::instance().getAssetFilePath(id);
                    if (!src.isEmpty()) sources << src;
                }
            }

            if (!sources.isEmpty()) {
                const bool shift = dropEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                // Ensure any preview locks are released before file ops
                if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
                if (shift) FileOpsQueue::instance().enqueueMove(sources, destDir);
                else FileOpsQueue::instance().enqueueCopy(sources, destDir);
                if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
                fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
                statusBar()->showMessage(QString("Queued %1 item(s) for %2").arg(sources.size()).arg(shift ? "move" : "copy"), 3000);
            }
            dropEvent->setDropAction(dropEvent->keyboardModifiers().testFlag(Qt::ShiftModifier) ? Qt::MoveAction : Qt::CopyAction);
            dropEvent->accept();
            return true;
        }
    }
    // Handle drops on File Manager folder tree (filesystem)
    if (fmTree && watched == fmTree->viewport()) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasUrls() || dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids")) {
                const bool shift = dragEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                dragEvent->setDropAction(shift ? Qt::MoveAction : Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::DragMove) {
            QDragMoveEvent *dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasUrls() || dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids")) {
                // Highlight folder under cursor
                QPoint pos = dragEvent->position().toPoint();
                QModelIndex idx = fmTree->indexAt(pos);
                if (idx.isValid()) {
                    fmTree->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect);
                }
                const bool shift = dragEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                dragEvent->setDropAction(shift ? Qt::MoveAction : Qt::CopyAction);
                dragEvent->accept();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData *mimeData = dropEvent->mimeData();
            QPoint pos = dropEvent->position().toPoint();
            QModelIndex idx = fmTree->indexAt(pos);
            if (!idx.isValid()) return false;
            const QString destDir = fmTreeModel ? fmTreeModel->filePath(idx) : QString();
            if (destDir.isEmpty()) return false;
            QStringList sources;
            if (mimeData->hasUrls()) {
                for (const QUrl &url : mimeData->urls()) {
                    if (url.isLocalFile()) sources << url.toLocalFile();
                }
            }
            if (mimeData->hasFormat("application/x-kasset-asset-ids")) {
                QByteArray encodedData = mimeData->data("application/x-kasset-asset-ids");
                QDataStream stream(&encodedData, QIODevice::ReadOnly);
                QList<int> assetIds; stream >> assetIds;
                for (int id : assetIds) {
                    const QString src = DB::instance().getAssetFilePath(id);
                    if (!src.isEmpty()) sources << src;
                }
            }
            if (!sources.isEmpty()) {
                const bool shift = dropEvent->keyboardModifiers().testFlag(Qt::ShiftModifier);
                // Ensure any preview locks are released before file ops
                if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
                if (shift) FileOpsQueue::instance().enqueueMove(sources, destDir);
                else       FileOpsQueue::instance().enqueueCopy(sources, destDir);
                if (!fileOpsDialog) fileOpsDialog = new FileOpsProgressDialog(this);
                fileOpsDialog->show(); fileOpsDialog->raise(); fileOpsDialog->activateWindow();
                statusBar()->showMessage(QString("Queued %1 item(s) for %2").arg(sources.size()).arg(shift ? "move" : "copy"), 3000);
            }
            dropEvent->setDropAction(dropEvent->keyboardModifiers().testFlag(Qt::ShiftModifier) ? Qt::MoveAction : Qt::CopyAction);
            dropEvent->accept();
            return true;
        }
    }


    // Handle drops on folder tree
    if (watched == folderTreeView->viewport()) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") ||
                dragEvent->mimeData()->hasFormat("application/x-kasset-folder-ids") ||
                dragEvent->mimeData()->hasUrls()) {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragMove) {
            QDragMoveEvent *dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") ||
                dragEvent->mimeData()->hasFormat("application/x-kasset-folder-ids") ||
                dragEvent->mimeData()->hasUrls()) {
                // Highlight the folder under cursor using selection
                QPoint pos = dragEvent->position().toPoint();
                QModelIndex index = folderTreeView->indexAt(pos);
                if (index.isValid()) {
                    folderTreeView->selectionModel()->select(index,
                        QItemSelectionModel::ClearAndSelect);
                }
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragLeave) {
            // Clear highlight when drag leaves
            folderTreeView->clearSelection();
            return false;
        }
        else if (event->type() == QEvent::Drop) {
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData *mimeData = dropEvent->mimeData();

            // Get the folder at drop position
            QPoint pos = dropEvent->position().toPoint();
            QModelIndex folderIndex = folderTreeView->indexAt(pos);

            if (folderIndex.isValid()) {
                int targetFolderId = folderModel->data(folderIndex, VirtualFolderTreeModel::IdRole).toInt();

                // Handle file URL drops (import into target folder)
                if (mimeData->hasUrls()) {
                    QStringList filePaths;
                    QStringList folderPaths;
                    for (const QUrl &url : mimeData->urls()) {
                        if (!url.isLocalFile()) continue;
                        const QString path = url.toLocalFile();
                        QFileInfo info(path);
                        if (info.isDir()) folderPaths << path; else if (info.isFile()) filePaths << path;
                    }
                    if (!filePaths.isEmpty() || !folderPaths.isEmpty()) {
                        if (!importProgressDialog) importProgressDialog = new ImportProgressDialog(this);
                        importProgressDialog->show();
                        importProgressDialog->raise();
                        importProgressDialog->activateWindow();

                        // Avoid premature dialog closure when importing folders then files
                        disconnect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);

                        for (const QString &dir : folderPaths) {
                            importer->importFolder(dir, targetFolderId);
                        }
                        if (!filePaths.isEmpty()) {
                            importer->importFiles(filePaths, targetFolderId);
                        }

                        // Reconnect and finalize
                        connect(importer, &Importer::importFinished, this, &MainWindow::onImportComplete);
                        onImportComplete();

                        dropEvent->acceptProposedAction();
                        return true;
                    }
                }
                // Handle asset drops
                else if (mimeData->hasFormat("application/x-kasset-asset-ids")) {
                    // Decode asset IDs
                    QByteArray encodedData = mimeData->data("application/x-kasset-asset-ids");
                    QDataStream stream(&encodedData, QIODevice::ReadOnly);
                    QList<int> assetIds;
                    stream >> assetIds;

                    // Check if locked and if move is allowed
                    if (assetsLocked) {
                        // Check if target is in a project folder
                        int targetProjectFolderId = -1;
                        QModelIndex current = folderIndex;
                        while (current.isValid()) {
                            if (folderModel->data(current, VirtualFolderTreeModel::IsProjectFolderRole).toBool()) {
                                targetProjectFolderId = folderModel->data(current, VirtualFolderTreeModel::ProjectFolderIdRole).toInt();
                                break;
                            }
                            current = folderModel->parent(current);
                        }

                        // Check if all assets are from the same project folder
                        bool canMove = true;
                        int sourceProjectFolderId = -1;

                        for (int assetId : assetIds) {
                            QSqlQuery q(DB::instance().database());
                            q.prepare("SELECT virtual_folder_id FROM assets WHERE id=?");
                            q.addBindValue(assetId);
                            if (q.exec() && q.next()) {
                                int assetFolderId = q.value(0).toInt();

                                // Find if asset is in a project folder
                                int assetProjectFolderId = -1;
                                std::function<void(const QModelIndex&)> findProjectFolder = [&](const QModelIndex& idx) {
                                    if (!idx.isValid()) return;
                                    if (folderModel->data(idx, VirtualFolderTreeModel::IdRole).toInt() == assetFolderId) {
                                        QModelIndex cur = idx;
                                        while (cur.isValid()) {
                                            if (folderModel->data(cur, VirtualFolderTreeModel::IsProjectFolderRole).toBool()) {
                                                assetProjectFolderId = folderModel->data(cur, VirtualFolderTreeModel::ProjectFolderIdRole).toInt();
                                                return;
                                            }
                                            cur = folderModel->parent(cur);
                                        }
                                        return;
                                    }
                                    for (int row = 0; row < folderModel->rowCount(idx); ++row) {
                                        findProjectFolder(folderModel->index(row, 0, idx));
                                        if (assetProjectFolderId != -1) return;
                                    }
                                };
                                findProjectFolder(QModelIndex());

                                if (sourceProjectFolderId == -1) {
                                    sourceProjectFolderId = assetProjectFolderId;
                                } else if (sourceProjectFolderId != assetProjectFolderId) {
                                    canMove = false;
                                    break;
                                }
                            }
                        }

                        if (!canMove || (sourceProjectFolderId != -1 && sourceProjectFolderId != targetProjectFolderId)) {
                            QMessageBox::warning(this, "Move Restricted",
                                "Assets are locked. You can only move assets within their project folder.\n"
                                "Uncheck the 'Lock Assets' checkbox to move assets freely.");
                            dropEvent->ignore();
                            return false;
                        }
                    }

                    // Move assets to folder (batch operation to avoid multiple reloads)
                    bool success = true;
                    for (int assetId : assetIds) {
                        if (!DB::instance().setAssetFolder(assetId, targetFolderId)) {
                            success = false;
                        }
                    }

                    // Reload once after all moves are complete
                    if (success) {
                        assetsModel->reload();
                        statusBar()->showMessage(QString("Moved %1 asset(s) to folder").arg(assetIds.size()), 3000);
                    } else {
                        assetsModel->reload();
                        statusBar()->showMessage("Failed to move some assets", 3000);
                    }

                    dropEvent->acceptProposedAction();
                    return true;
                }
                // Handle folder drops (reorganize hierarchy)
                else if (mimeData->hasFormat("application/x-kasset-folder-ids")) {
                    // Decode folder IDs
                    QByteArray encodedData = mimeData->data("application/x-kasset-folder-ids");
                    QDataStream stream(&encodedData, QIODevice::ReadOnly);
                    QList<int> folderIds;
                    stream >> folderIds;

                    // Move folders to new parent
                    bool success = true;
                    for (int folderId : folderIds) {
                        // Don't allow moving a folder into itself or its descendants
                        if (folderId == targetFolderId) {
                            QMessageBox::warning(this, "Error", "Cannot move a folder into itself");
                            success = false;
                            continue;
                        }

                        if (!folderModel->moveFolder(folderId, targetFolderId)) {
                            success = false;
                        }
                    }

                    if (success) {
                        folderModel->reload();
                        statusBar()->showMessage(QString("Moved %1 folder(s)").arg(folderIds.size()), 3000);
                    } else {
                        statusBar()->showMessage("Failed to move some folders", 3000);
                    }

                    dropEvent->acceptProposedAction();
                    return true;
                }
            }
        }
    }

    // Handle drops on tags list
    if (watched == tagsListView->viewport()) {
        if (event->type() == QEvent::DragEnter) {
            QDragEnterEvent *dragEvent = static_cast<QDragEnterEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") ||
                dragEvent->mimeData()->hasFormat("application/x-kasset-folder-ids")) {
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragMove) {
            QDragMoveEvent *dragEvent = static_cast<QDragMoveEvent*>(event);
            if (dragEvent->mimeData()->hasFormat("application/x-kasset-asset-ids") ||
                dragEvent->mimeData()->hasFormat("application/x-kasset-folder-ids")) {
                // Highlight the tag under cursor using selection
                QPoint pos = dragEvent->position().toPoint();
                QModelIndex index = tagsListView->indexAt(pos);
                if (index.isValid()) {
                    tagsListView->selectionModel()->select(index,
                        QItemSelectionModel::ClearAndSelect);
                }
                dragEvent->acceptProposedAction();
                return true;
            }
        }
        else if (event->type() == QEvent::DragLeave) {
            // Clear highlight when drag leaves
            tagsListView->clearSelection();
            return false;
        }
        else if (event->type() == QEvent::Drop) {
            QDropEvent *dropEvent = static_cast<QDropEvent*>(event);
            const QMimeData *mimeData = dropEvent->mimeData();

            // Get the tag at drop position
            QPoint pos = dropEvent->position().toPoint();
            QModelIndex tagIndex = tagsListView->indexAt(pos);

            if (tagIndex.isValid()) {
                int tagId = tagsModel->data(tagIndex, TagsModel::IdRole).toInt();
                QString tagName = tagsModel->data(tagIndex, TagsModel::NameRole).toString();

                // Handle asset drops
                if (mimeData->hasFormat("application/x-kasset-asset-ids")) {
                    // Decode asset IDs
                    QByteArray encodedData = mimeData->data("application/x-kasset-asset-ids");
                    QDataStream stream(&encodedData, QIODevice::ReadOnly);
                    QList<int> assetIds;
                    stream >> assetIds;

                    // Assign tag to assets
                    QList<int> tagIds;
                    tagIds.append(tagId);
                    if (DB::instance().assignTagsToAssets(assetIds, tagIds)) {
                        statusBar()->showMessage(QString("Assigned tag '%1' to %2 asset(s)").arg(tagName).arg(assetIds.size()), 3000);
                        updateInfoPanel();
                    } else {
                        statusBar()->showMessage("Failed to assign tag", 3000);
                    }

                    dropEvent->acceptProposedAction();
                    return true;
                }
                // Handle folder drops (assign tag to all assets in folder)
                else if (mimeData->hasFormat("application/x-kasset-folder-ids")) {
                    // Decode folder IDs
                    QByteArray encodedData = mimeData->data("application/x-kasset-folder-ids");
                    QDataStream stream(&encodedData, QIODevice::ReadOnly);
                    QList<int> folderIds;
                    stream >> folderIds;

                    // Get all assets in these folders (recursive)
                    QList<int> allAssetIds;
                    for (int folderId : folderIds) {
                        QList<int> assetIds = DB::instance().getAssetIdsInFolder(folderId, true);
                        allAssetIds.append(assetIds);
                    }

                    if (!allAssetIds.isEmpty()) {
                        // Assign tag to all assets
                        QList<int> tagIds;
                        tagIds.append(tagId);
                        if (DB::instance().assignTagsToAssets(allAssetIds, tagIds)) {
                            statusBar()->showMessage(QString("Assigned tag '%1' to %2 asset(s) in %3 folder(s)")
                                .arg(tagName).arg(allAssetIds.size()).arg(folderIds.size()), 3000);
                            updateInfoPanel();
                        } else {
                            statusBar()->showMessage("Failed to assign tag", 3000);
                        }
                    } else {
                        statusBar()->showMessage("No assets found in selected folder(s)", 3000);
                    }

                    dropEvent->acceptProposedAction();
                    return true;
                }
            }
        }

    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::saveFolderExpansionState()
{
    expandedFolderIds.clear();

    // Recursively save expanded state
    std::function<void(const QModelIndex&)> saveExpanded = [&](const QModelIndex& parent) {
        int rowCount = folderModel->rowCount(parent);
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex index = folderModel->index(i, 0, parent);
            if (index.isValid()) {
                if (folderTreeView->isExpanded(index)) {
                    int folderId = index.data(VirtualFolderTreeModel::IdRole).toInt();
                    expandedFolderIds.insert(folderId);
                }
                // Recurse into children
                saveExpanded(index);
            }
        }
    };

    saveExpanded(QModelIndex());
    qDebug() << "Saved expansion state for" << expandedFolderIds.size() << "folders";
}

void MainWindow::restoreFolderExpansionState()
{
    // Recursively restore expanded state
    std::function<void(const QModelIndex&)> restoreExpanded = [&](const QModelIndex& parent) {
        int rowCount = folderModel->rowCount(parent);
        for (int i = 0; i < rowCount; ++i) {
            QModelIndex index = folderModel->index(i, 0, parent);
            if (index.isValid()) {
                int folderId = index.data(VirtualFolderTreeModel::IdRole).toInt();
                if (expandedFolderIds.contains(folderId)) {
                    folderTreeView->setExpanded(index, true);
                }
                // Recurse into children
                restoreExpanded(index);
            }
        }
    };

    restoreExpanded(QModelIndex());
}

void MainWindow::onOpenSettings()
{
    SettingsDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        applyFmShortcuts();
    }
}

void MainWindow::onThumbnailSizeChanged(int size)
{
    // Update delegate thumbnail size
    AssetItemDelegate* delegate = static_cast<AssetItemDelegate*>(assetGridView->itemDelegate());
    if (delegate) {
        delegate->setThumbnailSize(size);
    }

    // Update icon size for the view
    assetGridView->setIconSize(QSize(size, size));

    // Force view to update by resetting the model
    assetGridView->reset();

    // Recompute visible-only progress since layout changed
    scheduleVisibleThumbProgressUpdate();
}

void MainWindow::onViewModeChanged()
{
    isGridMode = !isGridMode;

    if (isGridMode) {
        // Switch to grid mode
        viewModeButton->setIcon(icoGrid());
        viewStack->setCurrentIndex(0); // Show grid view
        thumbnailSizeSlider->setEnabled(true);
    } else {
        // Switch to list mode (table view)
        viewModeButton->setIcon(icoList());
        viewStack->setCurrentIndex(1); // Show table view
        thumbnailSizeSlider->setEnabled(false);
    }

    // Recompute visible-only progress for the new view
    scheduleVisibleThumbProgressUpdate();
}


void MainWindow::onPrefetchLivePreviewsForFolder()
{
    if (!assetsModel) return;
    LivePreviewManager &previewMgr = LivePreviewManager::instance();
    QSize targetSize = assetGridView ? assetGridView->iconSize() : QSize(180, 180);
    if (!targetSize.isValid()) targetSize = QSize(180, 180);

    int requested = 0;
    const int rows = assetsModel->rowCount(QModelIndex());
    for (int r = 0; r < rows; ++r) {
        QModelIndex idx = assetsModel->index(r, 0);
        const QString fp = assetsModel->data(idx, AssetsModel::FilePathRole).toString();
        if (fp.isEmpty()) continue;
        auto handle = previewMgr.cachedFrame(fp, targetSize);
        if (!handle.isValid()) {
            previewMgr.requestFrame(fp, targetSize);
            ++requested;
        }
    }
    if (requested > 0) {
        statusBar()->showMessage(QString("Prefetching %1 live previews...").arg(requested), 2000);
    }
}

void MainWindow::onRefreshLivePreviewsForFolder()
{
    if (!assetsModel) return;
    LivePreviewManager &previewMgr = LivePreviewManager::instance();
    QSize targetSize = assetGridView ? assetGridView->iconSize() : QSize(180, 180);
    if (!targetSize.isValid()) targetSize = QSize(180, 180);

    int requested = 0;
    const int rows = assetsModel->rowCount(QModelIndex());
    for (int r = 0; r < rows; ++r) {
        QModelIndex idx = assetsModel->index(r, 0);
        const QString fp = assetsModel->data(idx, AssetsModel::FilePathRole).toString();
        if (fp.isEmpty()) continue;
        previewMgr.invalidate(fp);
        previewMgr.requestFrame(fp, targetSize);
        ++requested;
    }
    if (requested > 0) {
        statusBar()->showMessage(QString("Refreshing %1 live previews...").arg(requested), 2000);
    }
}

void MainWindow::onPrefetchLivePreviewsRecursive()
{
    if (!assetsModel) return;
    int fid = assetsModel->folderId();
    if (fid <= 0) return;
    QList<int> ids = DB::instance().getAssetIdsInFolder(fid, /*recursive*/true);
    if (ids.isEmpty()) return;
    LivePreviewManager &previewMgr = LivePreviewManager::instance();
    QSize targetSize = assetGridView ? assetGridView->iconSize() : QSize(180, 180);
    if (!targetSize.isValid()) targetSize = QSize(180, 180);

    int requested = 0;
    for (int id : ids) {
        const QString fp = DB::instance().getAssetFilePath(id);
        if (fp.isEmpty()) continue;
        auto handle = previewMgr.cachedFrame(fp, targetSize);
        if (!handle.isValid()) {
            previewMgr.requestFrame(fp, targetSize);
            ++requested;
        }
    }
    if (requested > 0) {
        statusBar()->showMessage(QString("Prefetching %1 live previews (recursive)...").arg(requested), 2000);
    }
}

void MainWindow::onRefreshLivePreviewsRecursive()
{
    if (!assetsModel) return;
    int fid = assetsModel->folderId();
    if (fid <= 0) return;
    QList<int> ids = DB::instance().getAssetIdsInFolder(fid, /*recursive*/true);
    if (ids.isEmpty()) return;
    LivePreviewManager &previewMgr = LivePreviewManager::instance();
    QSize targetSize = assetGridView ? assetGridView->iconSize() : QSize(180, 180);
    if (!targetSize.isValid()) targetSize = QSize(180, 180);

    int requested = 0;
    for (int id : ids) {
        const QString fp = DB::instance().getAssetFilePath(id);
        if (fp.isEmpty()) continue;
        previewMgr.invalidate(fp);
        previewMgr.requestFrame(fp, targetSize);
        ++requested;
    }
    if (requested > 0) {
        statusBar()->showMessage(QString("Refreshing %1 live previews (recursive)...").arg(requested), 2000);
    }
}

void MainWindow::scheduleVisibleThumbProgressUpdate()
{
    if (m_initializing) return;
    // Do not show our visible-only progress while a global/import progress is active
    if (ProgressManager::instance().isActive()) {
        return;
    }
    // Debounce frequent scroll/resize updates
    visibleThumbTimer.start(100);
}

void MainWindow::updateVisibleThumbProgress()
{
    if (m_initializing) return;
    if (ProgressManager::instance().isActive()) {
        if (thumbnailProgressLabel) thumbnailProgressLabel->setVisible(false);
        if (thumbnailProgressBar) thumbnailProgressBar->setVisible(false);
        return;
    }

    int visibleTotal = 0;
    int readyCount = 0;
    bool anyViewConsidered = false;

    if (!thumbnailProgressLabel || !thumbnailProgressBar) {
        if (thumbnailProgressLabel) thumbnailProgressLabel->setVisible(false);
        if (thumbnailProgressBar) thumbnailProgressBar->setVisible(false);
        return;
    }

    auto accumulateFromAssets = [&](QAbstractItemView* view) {
        if (!assetsModel || !view || !view->isVisible() || !view->viewport()) {
            return;
        }
        const QRect viewportRect = view->viewport()->rect();
        const int totalRows = assetsModel->rowCount(QModelIndex());
        if (totalRows <= 0) return;
        anyViewConsidered = true;
        const int thumbSide = view->iconSize().isValid() ? view->iconSize().width() : 180;
        const QSize targetSize(thumbSide, thumbSide);
        LivePreviewManager &previewMgr = LivePreviewManager::instance();
        for (int row = 0; row < totalRows; ++row) {
            const QModelIndex idx = assetsModel->index(row, 0);
            const QRect itemRect = view->visualRect(idx);
            if (!itemRect.isValid() || !itemRect.intersects(viewportRect)) {
                continue;
            }
            ++visibleTotal;
            const QString filePath = assetsModel->data(idx, AssetsModel::FilePathRole).toString();
            auto handle = previewMgr.cachedFrame(filePath, targetSize);
            if (handle.isValid()) {
                ++readyCount;
            } else {
                previewMgr.requestFrame(filePath, targetSize);
            }
        }
    };

    auto accumulateFromFileManager = [&](QAbstractItemView* view) {
        if (!view || !view->isVisible() || !view->viewport() || !fmDirModel) {
            return;
        }
        QAbstractItemModel* model = view->model();
        if (!model) return;
        const QRect viewportRect = view->viewport()->rect();
        const int rows = model->rowCount();
        const int thumbSide = view->iconSize().isValid() ? view->iconSize().width() : 120;
        const QSize targetSize(thumbSide, thumbSide);
        LivePreviewManager &previewMgr = LivePreviewManager::instance();
        anyViewConsidered = true;
        for (int row = 0; row < rows; ++row) {
            const QModelIndex idx = model->index(row, 0);
            const QRect itemRect = view->visualRect(idx);
            if (!itemRect.isValid() || !itemRect.intersects(viewportRect)) {
                continue;
            }
            ++visibleTotal;
            QModelIndex srcIdx = idx;
            if (fmProxyModel && idx.model() == fmProxyModel) {
                srcIdx = fmProxyModel->mapToSource(idx);
            }
            const QString filePath = fmDirModel->filePath(srcIdx);
            if (filePath.isEmpty()) continue;
            auto handle = previewMgr.cachedFrame(filePath, targetSize);
            if (handle.isValid()) {
                ++readyCount;
            } else {
                previewMgr.requestFrame(filePath, targetSize);
            }
        }
    };

    if (isGridMode) {
        accumulateFromAssets(assetGridView);
    } else {
        accumulateFromAssets(assetTableView);
    }
    accumulateFromFileManager(fmGridView);

    if (!anyViewConsidered || visibleTotal == 0 || readyCount >= visibleTotal) {
        thumbnailProgressLabel->setVisible(false);
        thumbnailProgressBar->setVisible(false);
        return;
    }

    thumbnailProgressLabel->setText("Live previews (visible):");
    thumbnailProgressLabel->setVisible(true);
    thumbnailProgressBar->setMaximum(visibleTotal);
    thumbnailProgressBar->setValue(readyCount);
    thumbnailProgressBar->setFormat(QString("%1/%2 (%p%)").arg(readyCount).arg(visibleTotal));
    thumbnailProgressBar->setVisible(true);
}

void MainWindow::onToggleLogViewer()
{
    // Find the log dock widget
    QList<QDockWidget*> docks = findChildren<QDockWidget*>();
    for (QDockWidget* dock : docks) {
        if (dock->windowTitle() == "Application Log") {
            dock->setVisible(!dock->isVisible());
            break;
        }
    }
}

QStringList MainWindow::reconstructSequenceFramePaths(const QString& firstFramePath, int startFrame, int endFrame)
{
    QStringList framePaths;
    QFileInfo firstFrameInfo(firstFramePath);
    QString fileName = firstFrameInfo.fileName();
    QString dirPath = firstFrameInfo.absolutePath();
    QString extension = firstFrameInfo.suffix();

    // Find the LAST frame number pattern in the first frame filename
    // Use globalMatch to find all occurrences, then take the last one
    QRegularExpression re("(\\d{3,})");
    QRegularExpressionMatchIterator it = re.globalMatch(fileName);

    QRegularExpressionMatch lastMatch;
    bool hasMatch = false;

    while (it.hasNext()) {
        lastMatch = it.next();
        hasMatch = true;
    }

    if (!hasMatch) {
        qWarning() << "[MainWindow] Could not find frame number pattern in:" << fileName;
        return framePaths;
    }

    QString frameNumberStr = lastMatch.captured(1);
    int paddingLength = frameNumberStr.length();
    int matchPos = lastMatch.capturedStart(1);

    // Extract the base name (everything before the frame number)
    QString baseName = fileName.left(matchPos);

    // Extract the suffix (everything after the frame number, including extension)
    QString suffix = fileName.mid(matchPos + paddingLength);

    // Reconstruct all frame paths
    for (int frame = startFrame; frame <= endFrame; ++frame) {
        QString frameNum = QString("%1").arg(frame, paddingLength, 10, QChar('0'));
        QString framePath = QDir(dirPath).filePath(baseName + frameNum + suffix);

        // Only add if file exists
        if (FileUtils::fileExists(framePath)) {
            framePaths.append(framePath);
        }
    }

    return framePaths;
}

void MainWindow::onAddProjectFolder()
{
    // Ask user to select a folder
    QString folderPath = QFileDialog::getExistingDirectory(
        this,
        "Select Project Folder",
        QString(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );

    if (folderPath.isEmpty()) {
        return;
    }

    // Ask for a name for this project folder
    bool ok;
    QString folderName = QInputDialog::getText(
        this,
        "Project Folder Name",
        "Enter a name for this project folder:",
        QLineEdit::Normal,
        QFileInfo(folderPath).fileName(),
        &ok
    );

    if (!ok || folderName.isEmpty()) {
        return;
    }

    // Create the project folder in the database
    int projectFolderId = DB::instance().createProjectFolder(folderName, folderPath);
    if (projectFolderId <= 0) {
        QMessageBox::warning(this, "Error", "Failed to create project folder. The name or path may already exist.");
        return;
    }

    // Add to watcher
    projectFolderWatcher->addProjectFolder(projectFolderId, folderPath);

    // Reload folder tree
    folderModel->reload();

    // Import the folder contents
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Import Assets",
        "Do you want to import all assets from this folder now?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        // Get the virtual folder ID for this project
        auto projectFolders = DB::instance().listProjectFolders();
        for (const auto& pf : projectFolders) {
            if (pf.first == projectFolderId) {
                // Import the folder
                importFiles(QStringList() << folderPath);
                break;
            }
        }
    }

    statusBar()->showMessage(QString("Added project folder '%1'").arg(folderName), 3000);
}

void MainWindow::onRefreshAssets()
{
    qDebug() << "MainWindow::onRefreshAssets";

    // Get all project folders
    auto projectFolders = DB::instance().listProjectFolders();

    if (projectFolders.isEmpty()) {
        statusBar()->showMessage("No project folders to refresh", 3000);
        return;
    }

    // Manually trigger refresh for all project folders
    for (const auto& pf : projectFolders) {
        int projectFolderId = pf.first;
        projectFolderWatcher->refreshProjectFolder(projectFolderId);
    }

    statusBar()->showMessage("Refreshing all project folders...", 3000);
}

void MainWindow::onLockToggled(bool checked)
{
    assetsLocked = checked;

    if (checked) {
        statusBar()->showMessage("Assets locked - can only move within project folders", 3000);
    } else {
        statusBar()->showMessage("Assets unlocked - can move freely", 3000);
    }
}

void MainWindow::onProjectFolderChanged(int projectFolderId, const QString& path)
{
    // Re-import the folder to pick up new/changed files
    // This will update existing assets and add new ones
    statusBar()->showMessage(QString("Refreshing project folder: %1").arg(QFileInfo(path).fileName()), 2000);

    // Import the folder (this will upsert assets)
    importFiles(QStringList() << path);
}


// ===== Asset Versioning UI Handlers =====
void MainWindow::reloadVersionHistory()
{
    // Default state
    if (!versionTable) return;
    revertVersionButton->setEnabled(false);
    versionTable->setRowCount(0);

    // Determine current single-selected asset
    QModelIndexList selected = getCurrentSelectionModel()->selectedIndexes();
    if (selected.size() != 1) {
        if (versionsTitleLabel) versionsTitleLabel->setText("Version History");
        return;
    }

    QModelIndex idx = selected.first();
    currentAssetId = idx.data(AssetsModel::IdRole).toInt();
    if (currentAssetId <= 0) return;

    QVector<AssetVersionRow> versions = DB::instance().listAssetVersions(currentAssetId);
    versionTable->setRowCount(versions.size());

    // Fill rows
    int row = 0;
    for (const auto& v : versions) {
        // Icon column
        QTableWidgetItem *iconItem = new QTableWidgetItem();
        const QSize targetSize(96, 96);
        QPixmap cached = versionPreviewCache.value(v.filePath);
        if (!cached.isNull()) {
            iconItem->setIcon(QIcon(cached));
        } else {
            auto handle = LivePreviewManager::instance().cachedFrame(v.filePath, targetSize);
            if (handle.isValid()) {
                iconItem->setIcon(QIcon(handle.pixmap));
            } else {
                LivePreviewManager::instance().requestFrame(v.filePath, targetSize);
                iconItem->setText("...");
            }
        }
        iconItem->setData(Qt::UserRole, v.filePath);
        versionTable->setItem(row, 0, iconItem);

        // Version column (store id in UserRole)
        QTableWidgetItem *verItem = new QTableWidgetItem(v.versionName);
        verItem->setData(Qt::UserRole, v.id);
        versionTable->setItem(row, 1, verItem);

        // Date column
        QTableWidgetItem *dateItem = new QTableWidgetItem(v.createdAt);
        versionTable->setItem(row, 2, dateItem);

        // Size column
        QString sizeStr;
        if (v.fileSize < 1024) sizeStr = QString::number(v.fileSize) + " B";
        else if (v.fileSize < 1024 * 1024) sizeStr = QString::number(v.fileSize / 1024.0, 'f', 1) + " KB";
        else if (v.fileSize < 1024ll * 1024ll * 1024ll) sizeStr = QString::number(v.fileSize / (1024.0 * 1024.0), 'f', 1) + " MB";
        else sizeStr = QString::number(v.fileSize / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
        QTableWidgetItem *sizeItem = new QTableWidgetItem(sizeStr.toLower());
        versionTable->setItem(row, 3, sizeItem);

        // Notes column
        QTableWidgetItem *notesItem = new QTableWidgetItem(v.notes);
        versionTable->setItem(row, 4, notesItem);

        ++row;
    }

    if (!versions.isEmpty()) {
        versionTable->selectRow(versionTable->rowCount() - 1); // Select latest by default
        revertVersionButton->setEnabled(true);
        if (versionsTitleLabel) versionsTitleLabel->setText(QString("Version History (%1)").arg(versions.size()));
    } else {
        if (versionsTitleLabel) versionsTitleLabel->setText("Version History (0)");
    }
}

void MainWindow::onRevertSelectedVersion()
{
    if (!versionTable) return;
    int row = versionTable->currentRow();
    if (row < 0 || currentAssetId <= 0) return;

    int versionId = 0;
    if (QTableWidgetItem *item = versionTable->item(row, 1)) {
        versionId = item->data(Qt::UserRole).toInt();
    }
    if (versionId <= 0) return;

    const bool makeBackup = backupVersionCheck && backupVersionCheck->isChecked();
    const QString question = makeBackup
        ? "Revert this asset to the selected version? A backup of the current file will be saved as a new version."
        : "Revert this asset to the selected version? This will overwrite the current file.";

    if (QMessageBox::question(this, "Revert to Version", question, QMessageBox::Yes|QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    if (!DB::instance().revertAssetToVersion(currentAssetId, versionId, makeBackup)) {
        QMessageBox::warning(this, "Revert Failed", "Failed to revert to the selected version.");
        return;
    }

    // Refresh UI
    reloadVersionHistory();
    updateInfoPanel();

    // Prefetch live preview for the asset file
    const QString assetPath = DB::instance().getAssetFilePath(currentAssetId);
    if (!assetPath.isEmpty()) {
        LivePreviewManager &previewMgr = LivePreviewManager::instance();
        previewMgr.invalidate(assetPath);
        previewMgr.requestFrame(assetPath, QSize(180, 180));
    }

    QMessageBox::information(this, "Reverted", "Asset has been reverted to the selected version.");
}

void MainWindow::onAssetVersionsChanged(int assetId)
{
    if (assetId == currentAssetId) {
        reloadVersionHistory();
    }
}


// ===== File Manager Preview handlers =====
void MainWindow::clearFmPreview()
{
    if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
    if (fmVideoWidget) fmVideoWidget->hide();
    if (fmPlayPauseBtn) fmPlayPauseBtn->hide();
    if (fmPositionSlider) fmPositionSlider->hide();
    if (fmTimeLabel) fmTimeLabel->hide();
    if (fmVolumeSlider) fmVolumeSlider->hide();

    if (fmTextView) { fmTextView->clear(); fmTextView->hide(); }
    if (fmCsvView) fmCsvView->hide();
    if (fmCsvModel) fmCsvModel->clear();
#ifdef HAVE_QT_PDF_WIDGETS
    if (fmPdfView) fmPdfView->hide();
#endif
#ifdef HAVE_QT_PDF
    if (fmPdfDoc) fmPdfDoc->close();
#endif
    if (fmPdfPrevBtn) fmPdfPrevBtn->hide();
    if (fmPdfNextBtn) fmPdfNextBtn->hide();
    if (fmPdfPageLabel) fmPdfPageLabel->hide();
    if (fmSvgItem) { fmSvgScene->removeItem(fmSvgItem); delete fmSvgItem; fmSvgItem = nullptr; }
    if (fmSvgView) fmSvgView->hide();

    if (fmImageItem) {
        fmImageItem->setPixmap(QPixmap());
    }
    if (fmAlphaCheck) fmAlphaCheck->hide();
    if (fmImageView) fmImageView->show();
}

static inline bool isImageFile(const QString &ext)
{
    static const QSet<QString> exts = {"png","jpg","jpeg","bmp","gif","tif","tiff","webp","heic","heif","exr","psd"};
    return exts.contains(ext.toLower());
}
static inline bool isVideoFile(const QString &ext)
{
    static const QSet<QString> exts = {"mp4","mov","avi","mkv","wmv","m4v","mpg","mpeg"};
    return exts.contains(ext.toLower());
}
static inline bool isAudioFile(const QString &ext)
{
    static const QSet<QString> exts = {"mp3","wav","aac","flac","ogg","m4a"};
    return exts.contains(ext.toLower());
}
static inline bool isPdfFile(const QString &ext)
{
    return ext.compare("pdf", Qt::CaseInsensitive) == 0;
}
static inline bool isSvgFile(const QString &ext)
{
    static const QSet<QString> exts = {"svg","svgz"};
    return exts.contains(ext.toLower());
}
static inline bool isTextFile(const QString &ext)
{
    static const QSet<QString> exts = {"txt","log"};
    return exts.contains(ext.toLower());
}
static inline bool isCsvFile(const QString &ext)
{
    return ext.compare("csv", Qt::CaseInsensitive) == 0;
}
static inline bool isExcelFile(const QString &ext)
{
    static const QSet<QString> exts = {"xls","xlsx"};
    return exts.contains(ext.toLower());
}
static inline bool isDocxFile(const QString &ext)
{
    return ext.compare("docx", Qt::CaseInsensitive) == 0;
}
static inline bool isDocFile(const QString &ext)
{
    return ext.compare("doc", Qt::CaseInsensitive) == 0;
}
static inline bool isAiFile(const QString &ext)
{
    return ext.compare("ai", Qt::CaseInsensitive) == 0;
}
static inline bool isPptxFile(const QString &ext)
{
    // Treat both .pptx and legacy .ppt as PowerPoint documents for preview handling
    return ext.compare("pptx", Qt::CaseInsensitive) == 0 || ext.compare("ppt", Qt::CaseInsensitive) == 0;
}






void MainWindow::updateFmPreviewForIndex(const QModelIndex &idx)
{
    if (!fmPreviewPanel || !fmPreviewPanel->isVisible()) return;
    if (!idx.isValid()) { clearFmPreview(); return; }

    QModelIndex viewIdx = idx.sibling(idx.row(), 0);

    // If this is a representative sequence item, show first frame

    if (fmProxyModel && fmGroupSequences && viewIdx.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(viewIdx)) {
        auto info = fmProxyModel->infoForProxyIndex(viewIdx);
        QString path = info.reprPath;
        if (path.isEmpty()) { clearFmPreview(); return; }
        QFileInfo infoFi(path);
        if (!infoFi.exists()) { clearFmPreview(); return; }
        // Treat as image preview of first frame
        QPixmap px;
        if (OIIOImageLoader::isOIIOSupported(path)) {
            QImage img = OIIOImageLoader::loadImage(path, 0, 0, OIIOImageLoader::ColorSpace::sRGB);
            if (!img.isNull()) px = QPixmap::fromImage(img);
        }
        if (px.isNull()) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            QImage img = reader.read();
            if (!img.isNull()) px = QPixmap::fromImage(img);
        }
        if (px.isNull()) { clearFmPreview(); return; }
        if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
        if (fmVideoWidget) fmVideoWidget->hide();
        if (fmPlayPauseBtn) fmPlayPauseBtn->hide();
        if (fmPositionSlider) fmPositionSlider->hide();
        if (fmTimeLabel) fmTimeLabel->hide();
        if (fmVolumeSlider) fmVolumeSlider->hide();
        fmCurrentPreviewPath = path;
        fmImageItem->setPixmap(px);
        fmImageItem->setTransformationMode(Qt::SmoothTransformation);
        fmImageView->resetTransform();
        fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
        fmImageFitToView = true;
        fmImageView->setBackgroundBrush(QColor("#0a0a0a"));
        fmImageView->show();
        return;
    }

    QModelIndex srcIdx = viewIdx;
    if (fmProxyModel && viewIdx.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(viewIdx);
    const QString path = fmDirModel->filePath(srcIdx);
    if (path.isEmpty()) { clearFmPreview(); return; }
    QFileInfo info(path);
    if (!info.exists() || info.isDir()) { clearFmPreview(); return; }

    const QString ext = info.suffix();

    auto hideNonImageWidgets = [this]{
        if (fmTextView) fmTextView->hide();
        if (fmCsvView) fmCsvView->hide();
#ifdef HAVE_QT_PDF_WIDGETS
        if (fmPdfView) fmPdfView->hide();
#endif
        if (fmPdfPrevBtn) fmPdfPrevBtn->hide();
        if (fmPdfNextBtn) fmPdfNextBtn->hide();
        if (fmPdfPageLabel) fmPdfPageLabel->hide();
        if (fmSvgView) fmSvgView->hide();
        if (fmVideoWidget) fmVideoWidget->hide();
        if (fmPlayPauseBtn) fmPlayPauseBtn->hide();
        if (fmPositionSlider) fmPositionSlider->hide();
        if (fmTimeLabel) fmTimeLabel->hide();
        if (fmVolumeSlider) fmVolumeSlider->hide();
        if (fmImageView) fmImageView->hide();
        if (fmAlphaCheck) fmAlphaCheck->hide();
    };

    if (isImageFile(ext)) {
        // Stop any media playback and hide media-specific widgets/controls
        if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
        hideNonImageWidgets();

        // Try OpenImageIO first for advanced formats (PSD/EXR/TIFF/etc.)
        QImage img;
        if (OIIOImageLoader::isOIIOSupported(path)) {
            img = OIIOImageLoader::loadImage(path, 0, 0, OIIOImageLoader::ColorSpace::sRGB);
        }
        if (img.isNull()) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            img = reader.read();
        }
        if (img.isNull()) { clearFmPreview(); return; }

        fmCurrentPreviewPath = path;
        fmOriginalImage = img;
        fmPreviewHasAlpha = img.hasAlphaChannel();
        if (fmAlphaCheck) { fmAlphaCheck->setVisible(fmPreviewHasAlpha); fmAlphaCheck->setChecked(false); }
        QImage disp = fmOriginalImage;
        if (fmAlphaOnlyMode && disp.hasAlphaChannel()) {
            QImage a(disp.size(), QImage::Format_Grayscale8);
            for (int y=0;y<disp.height();++y){
                for (int x=0;x<disp.width();++x){
                    uchar alpha = qAlpha(reinterpret_cast<const QRgb*>(disp.constScanLine(y))[x]);
                    a.scanLine(y)[x] = alpha;
                }
            }
            disp = a;
        }
        fmImageItem->setPixmap(QPixmap::fromImage(disp));
        fmImageItem->setTransformationMode(Qt::SmoothTransformation);
        fmImageView->resetTransform();
        fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
        fmImageFitToView = true;
        fmImageView->setBackgroundBrush(QColor("#0a0a0a"));
        fmImageView->show();
        return;
    }

#ifdef HAVE_QT_PDF
    if (isPdfFile(ext)) {
        hideNonImageWidgets();
        if (fmPdfDoc) {
            fmCurrentPreviewPath = path;
            auto err = fmPdfDoc->load(path);
            if (err == QPdfDocument::Error::None && fmPdfDoc->pageCount() > 0) {
            // Always render PDF pages into the image view for consistent zoom/pan
                fmPdfCurrentPage = 0;
                const QSizeF pts = fmPdfDoc->pagePointSize(fmPdfCurrentPage);
                int vw = fmImageView ? fmImageView->viewport()->width() : 800;
                if (vw < 1) vw = 800;
                int w = vw;
                int h = pts.width() > 0 ? int(pts.height() * (w / pts.width())) : w;
                QImage img = fmPdfDoc->render(fmPdfCurrentPage, QSize(w, h));
                if (!img.isNull() && fmImageItem) {
                    if (img.hasAlphaChannel()) {
                        QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied);
                        bg.fill(Qt::white);
                        QPainter p(&bg);
                        p.drawImage(0, 0, img);
                        p.end();
                        img = bg;
                    }
                    fmImageItem->setPixmap(QPixmap::fromImage(img));
                    if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                    if (fmImageView) {
                        fmImageView->resetTransform();
                        fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                        fmImageFitToView = true;
                        fmImageView->setBackgroundBrush(Qt::white);
                        fmImageView->show();
                    }
                }
                if (fmPdfPrevBtn) fmPdfPrevBtn->show();
                if (fmPdfNextBtn) fmPdfNextBtn->show();
                if (fmPdfPageLabel) { fmPdfPageLabel->show(); fmPdfPageLabel->setText(QString("%1/%2").arg(1).arg(fmPdfDoc->pageCount())); }
                #ifdef HAVE_QT_PDF_WIDGETS
                if (fmPdfView) fmPdfView->hide();
                #endif
            } else {
                qWarning() << "[PREVIEW] PDF load failed" << int(err) << "pages=" << (fmPdfDoc ? fmPdfDoc->pageCount() : -1) << path;
                // Fallback: show not available message in text view
                if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); }
            }
        }
        return;
    }
#else
    if (isPdfFile(ext)) {
        hideNonImageWidgets();
        if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); }
        return;
    }
#endif

    if (isSvgFile(ext)) {
        hideNonImageWidgets();
        if (fmSvgScene && fmSvgView) {
            // Remove previous item
            if (fmSvgItem) { fmSvgScene->removeItem(fmSvgItem); delete fmSvgItem; fmSvgItem = nullptr; }
            fmCurrentPreviewPath = path;
            auto *item = new QGraphicsSvgItem(path);
            item->setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable);
            fmSvgItem = item;
            fmSvgScene->addItem(fmSvgItem);
            fmSvgView->fitInView(fmSvgItem, Qt::KeepAspectRatio);
            fmSvgView->show();
        }
        return;
    }

    if (isTextFile(ext)) {
        hideNonImageWidgets();
        if (fmTextView) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                fmCurrentPreviewPath = path;
                QByteArray data = f.read(2*1024*1024); // cap to 2MB

                auto decodeText = [](const QByteArray &data) -> QString {
                    if (data.isEmpty()) return QString();
                    const uchar *b = reinterpret_cast<const uchar*>(data.constData());
                    const int n = data.size();
                    // UTF-8 BOM
                    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) {
                        return QString::fromUtf8(reinterpret_cast<const char*>(b + 3), n - 3);
                    }
                    // UTF-16 LE BOM
                    if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
                        return QString::fromUtf16(reinterpret_cast<const ushort*>(b + 2), (n - 2) / 2);
                    }
                    // UTF-16 BE BOM
                    if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
                        const int ulen = (n - 2) / 2;
                        QVector<ushort> buf; buf.resize(ulen);
                        for (int i = 0; i < ulen; ++i) buf[i] = (ushort(b[2 + 2*i]) << 8) | ushort(b[2 + 2*i + 1]);
                        return QString::fromUtf16(buf.constData(), ulen);
                    }
                    // Heuristic: UTF-16 without BOM (look for lots of NULs at odd/even positions)
                    const int sample = qMin(n, 4096);
                    int zeroEven = 0, zeroOdd = 0;
                    for (int i = 0; i < sample; ++i) {
                        if (b[i] == 0) { if ((i & 1) == 0) ++zeroEven; else ++zeroOdd; }
                    }
                    if ((zeroOdd + zeroEven) > sample / 16) {
                        const bool le = (zeroOdd > zeroEven);
                        const int ulen = n / 2;
                        if (le) {
                            return QString::fromUtf16(reinterpret_cast<const ushort*>(b), ulen);
                        } else {
                            QVector<ushort> buf; buf.resize(ulen);
                            for (int i = 0; i < ulen; ++i) buf[i] = (ushort(b[2*i]) << 8) | ushort(b[2*i + 1]);
                            return QString::fromUtf16(buf.constData(), ulen);
                        }
                    }
                    // Default: UTF-8, fallback to local 8-bit if many replacement chars
                    QString s = QString::fromUtf8(reinterpret_cast<const char*>(b), n);
                    int bad = 0; const int check = qMin(s.size(), 4096);
                    for (int i = 0; i < check; ++i) if (s.at(i).unicode() == 0xFFFD) ++bad;
                    if (bad > check / 16) s = QString::fromLocal8Bit(reinterpret_cast<const char*>(b), n);
                    return s;
                };

                fmTextView->setPlainText(decodeText(data));
                fmTextView->show();
            } else {
                if (fmTextView) {
                    fmTextView->setPlainText("Preview not available");
                    fmTextView->show();
                }
            }
        }
        return;
    }

// Office formats (DOC/DOCX/XLSX): lightweight, parse-only previews (no WYSIWYG)
if (isDocxFile(ext)) {
    hideNonImageWidgets();
    fmCurrentPreviewPath = path;
    if (fmTextView) {
        const QString text = extractDocxText(path);
        if (!text.isEmpty()) {
            fmTextView->setFont(QFont("Segoe UI"));
            fmTextView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            fmTextView->setPlainText(text);
        } else {
            fmTextView->setFont(QFont("Segoe UI"));
            fmTextView->setPlainText("Preview not available");
        }
        fmTextView->show();
    }
    return;
}
if (isDocFile(ext)) {
    hideNonImageWidgets();
    fmCurrentPreviewPath = path;
    if (fmTextView) {
        const QString text = extractDocBinaryText(path, 2 * 1024 * 1024);
        if (!text.isEmpty()) {
            fmTextView->setFont(QFont("Segoe UI"));
            fmTextView->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            fmTextView->setPlainText(text);
        } else {
            fmTextView->setFont(QFont("Segoe UI"));
            fmTextView->setPlainText("Preview not available");
        }
        fmTextView->show();
    }
    return;
}

if (isExcelFile(ext)) {
    hideNonImageWidgets();
    fmCurrentPreviewPath = path;
    if (fmCsvModel && fmCsvView) {
        fmCsvModel->clear();
        if (loadXlsxSheet(path, fmCsvModel, 2000)) {
            fmCsvView->resizeColumnsToContents();
            fmCsvView->show();
        } else if (fmTextView) {
            fmTextView->setPlainText("Preview not available");
            fmTextView->show();
        }
    }
    return;
}

    if (isCsvFile(ext)) {
        hideNonImageWidgets();
        if (fmCsvModel && fmCsvView) {
            fmCsvModel->clear();
            fmCurrentPreviewPath = path;
            QFile f(path);
            if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream ts(&f);
                int row=0; QChar delim = ',';
                while (!ts.atEnd() && row<2000) {
                    const QString line = ts.readLine();
                    if (row == 0) {
                        // Auto-detect delimiter: ',', ';', or tab
                        int cComma = line.count(',');
                        int cSemi  = line.count(';');
                        int cTab   = line.count('\t');
                        if (cSemi > cComma && cSemi >= cTab) delim = ';';
                        else if (cTab > cComma && cTab >= cSemi) delim = '\t';
                    }
                    const QStringList cols = line.split(delim);
                    if (row==0) fmCsvModel->setColumnCount(cols.size());
                    QList<QStandardItem*> items; items.reserve(cols.size());
                    for (const QString &c : cols) items << new QStandardItem(c.trimmed());
                    fmCsvModel->appendRow(items);
                    ++row;
                }
                fmCsvView->resizeColumnsToContents();
                fmCsvView->show();
            } else {
                if (fmTextView) {
                    fmTextView->setPlainText("Preview not available");
                    fmTextView->show();
                }
            }
        }
        return;
    }

    if (isAudioFile(ext) || isVideoFile(ext)) {
        // Media branch: audio/video
        if (isVideoFile(ext)) {
            fmCurrentPreviewPath = path;
            if (fmVideoWidget) fmVideoWidget->show();
            if (fmImageView) fmImageView->hide();
        } else {
            fmCurrentPreviewPath = path;
            if (fmVideoWidget) fmVideoWidget->hide();
            if (fmImageView) fmImageView->hide();
        }
        if (fmPlayPauseBtn) fmPlayPauseBtn->show();
        if (fmPositionSlider) fmPositionSlider->show();
        if (fmTimeLabel) fmTimeLabel->show();
        if (fmVolumeSlider) fmVolumeSlider->show();
        if (fmMediaPlayer) {
            fmMediaPlayer->setSource(QUrl::fromLocalFile(path));
            fmMediaPlayer->pause();
            if (fmPlayPauseBtn) fmPlayPauseBtn->setText("Play");
        }
        return;
    }

    if (isExcelFile(ext) || isDocxFile(ext) || isDocFile(ext)) {
        hideNonImageWidgets();
        if (fmTextView) {
            fmTextView->setPlainText("Preview not available");
            fmTextView->show();
        }
        return;
    }

#ifdef HAVE_QT_PDF
    if (isAiFile(ext)) {
        // Many .ai files embed PDF — try to render with PDF engine
        auto err = fmPdfDoc ? fmPdfDoc->load(path) : QPdfDocument::Error::Unknown;
        if (fmPdfDoc && err == QPdfDocument::Error::None && fmPdfDoc->pageCount()>0) {
            hideNonImageWidgets();
        // Always render PDF pages into the image view for consistent zoom/pan
            fmPdfCurrentPage = 0;
            const QSizeF pts = fmPdfDoc->pagePointSize(fmPdfCurrentPage);
            int vw = fmImageView ? fmImageView->viewport()->width() : 800;
            if (vw < 1) vw = 800;
            int w = vw;
            int h = pts.width() > 0 ? int(pts.height() * (w / pts.width())) : w;
            QImage img = fmPdfDoc->render(fmPdfCurrentPage, QSize(w, h));
            if (!img.isNull() && fmImageItem) {
                // Composite onto white to avoid dark theme bleeding through
                if (img.hasAlphaChannel()) {
                    QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied);
                    bg.fill(Qt::white);
                    QPainter p(&bg);
                    p.drawImage(0, 0, img);
                    p.end();
                    img = bg;
                }
                fmImageItem->setPixmap(QPixmap::fromImage(img));
                if (fmImageScene) fmImageScene->setSceneRect(fmImageItem->boundingRect());
                if (fmImageView) {
                    fmImageView->resetTransform();
                    fmImageView->fitInView(fmImageItem, Qt::KeepAspectRatio);
                    fmImageFitToView = true;
                    fmImageView->setBackgroundBrush(Qt::white);
                    fmImageView->show();
                }
            }
            if (fmPdfPrevBtn) fmPdfPrevBtn->show(); if (fmPdfNextBtn) fmPdfNextBtn->show(); if (fmPdfPageLabel) { fmPdfPageLabel->show(); fmPdfPageLabel->setText(QString("%1/%2").arg(1).arg(fmPdfDoc->pageCount())); }
            #ifdef HAVE_QT_PDF_WIDGETS
            if (fmPdfView) fmPdfView->hide();
            #endif
            return;
        } else {
            qWarning() << "[PREVIEW] AI (PDF-embedded) load failed or no pages" << path;
        }
        hideNonImageWidgets();
        if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); }
        return;
    }
#else
    if (isAiFile(ext)) {
        hideNonImageWidgets();
        if (fmTextView) { fmTextView->setPlainText("Preview not available"); fmTextView->show(); }
        return;
    }
#endif

    clearFmPreview();
}

void MainWindow::onFmSelectionChanged()
{
    QModelIndex idx;
    if (fmGridView->hasFocus()) {
        idx = fmGridView->currentIndex();
    } else if (fmListView->hasFocus()) {
        idx = fmListView->currentIndex();
    }
    if (!idx.isValid()) {
        auto sel = fmGridView->selectionModel()->selectedIndexes();
        if (!sel.isEmpty()) idx = sel.first();
    }
    if (!idx.isValid()) {
        auto sel = fmListView->selectionModel()->selectedIndexes();
        if (!sel.isEmpty()) idx = sel.first();
    }
    updateFmPreviewForIndex(idx);
}

void MainWindow::onFmTogglePreview()
{
    if (!fmPreviewPanel) return;
    const bool show = fmPreviewToggleButton ? fmPreviewToggleButton->isChecked() : !fmPreviewPanel->isVisible();
    fmPreviewPanel->setVisible(show);
    if (!show) {
        if (fmMediaPlayer) { fmMediaPlayer->stop(); fmMediaPlayer->setSource(QUrl()); }
    } else {
        onFmSelectionChanged();
    }
    // Persist immediately
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/PreviewVisible", show);
}

void MainWindow::onFmOpenOverlay()
{
    // Toggle: if overlay is visible, close it
    if (previewOverlay && previewOverlay->isVisible()) { closePreview(); return; }

    // Determine current selection in FM and open full-screen overlay
    QModelIndex idx;
    if (fmGridView && fmGridView->hasFocus()) idx = fmGridView->currentIndex();
    else if (fmListView && fmListView->hasFocus()) idx = fmListView->currentIndex();
    if (!idx.isValid()) return;
    idx = idx.sibling(idx.row(), 0);

    // Record overlay navigation context
    fmOverlayCurrentIndex = QPersistentModelIndex(idx);
    fmOverlaySourceView = (fmGridView && fmGridView->hasFocus()) ? static_cast<QAbstractItemView*>(fmGridView) : static_cast<QAbstractItemView*>(fmListView);

    // If sequence grouping is enabled and the selection is a representative, open as sequence
    if (fmProxyModel && fmGroupSequences && idx.model() == fmProxyModel && fmProxyModel->isRepresentativeProxyIndex(idx)) {
        auto info = fmProxyModel->infoForProxyIndex(idx);
        QStringList frames = reconstructSequenceFramePaths(info.reprPath, info.start, info.end);
        if (!frames.isEmpty()) {
            if (!previewOverlay) {
                previewOverlay = new PreviewOverlay(this);
                previewOverlay->setGeometry(rect());
                connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
                connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
            } else {
                previewOverlay->stopPlayback();
            }
            int pad = 0;
            auto m = SequenceDetector::mainPattern().match(QFileInfo(info.reprPath).fileName());
            if (m.hasMatch()) pad = m.captured(3).length(); else pad = QString::number(info.start).length();
            QString s0 = QString("%1").arg(info.start, pad, 10, QLatin1Char('0'));
            QString s1 = QString("%1").arg(info.end, pad, 10, QLatin1Char('0'));
            QString seqName = QString("%1.[%2-%3].%4").arg(info.base, s0, s1, info.ext);
            previewOverlay->showSequence(frames, seqName, info.start, info.end);
            return;
        }
    }

    // Otherwise open single asset
    QModelIndex srcIdx = idx;
    if (fmProxyModel && idx.model() == fmProxyModel)
        srcIdx = fmProxyModel->mapToSource(idx);
    const QString path = fmDirModel ? fmDirModel->filePath(srcIdx) : QString();
    if (path.isEmpty()) return;
    QFileInfo info(path);
    if (!info.exists()) return;
    if (!previewOverlay) {
        previewOverlay = new PreviewOverlay(this);
        previewOverlay->setGeometry(rect());
        connect(previewOverlay, &PreviewOverlay::closed, this, &MainWindow::closePreview);
        connect(previewOverlay, &PreviewOverlay::navigateRequested, this, &MainWindow::changeFmPreview);
    } else {
        previewOverlay->stopPlayback();
    }
    previewOverlay->showAsset(path, info.fileName(), info.suffix());
}



void MainWindow::closeEvent(QCloseEvent* event)
{
    // Save current folder context before closing
    int currentFolderId = assetsModel->folderId();
    if (currentFolderId > 0) {
        ContextPreserver::FolderContext ctx;
        // Save scroll position
        if (isGridMode && assetGridView) {
            ctx.scrollPosition = assetGridView->verticalScrollBar()->value();
        } else if (!isGridMode && assetTableView) {
            ctx.scrollPosition = assetTableView->verticalScrollBar()->value();
        }
        ctx.isGridMode = isGridMode;
        ctx.searchText = searchBox->text();
        ctx.ratingFilter = ratingFilter->currentIndex() - 1;
        ctx.selectedAssetIds = selectedAssetIds;
        ctx.recursiveMode = recursiveCheckBox->isChecked();

        // Save selected tags
        QModelIndexList tagSelection = tagsListView->selectionModel()->selectedIndexes();
        for (const QModelIndex& idx : tagSelection) {
            int tagId = idx.data(TagsModel::IdRole).toInt();
            if (tagId > 0) ctx.selectedTagIds.insert(tagId);
        }

        ContextPreserver::instance().saveFolderContext(currentFolderId, ctx);
    }

    // Save current tab
    if (mainTabs) {
        ContextPreserver::instance().saveLastActiveTab(mainTabs->currentIndex());
    }

    QSettings s("AugmentCode", "KAssetManager");
    // Window
    s.setValue("Window/Geometry", saveGeometry());
    s.setValue("Window/State", saveState());

    // Asset Manager
    if (mainSplitter) s.setValue("AssetManager/MainSplitter", mainSplitter->saveState());
    if (rightSplitter) s.setValue("AssetManager/RightSplitter", rightSplitter->saveState());
    s.setValue("AssetManager/ViewMode", isGridMode);
    if (assetTableView && assetTableView->model()) {

        auto hh = assetTableView->horizontalHeader();
        for (int c = 0; c < assetTableView->model()->columnCount(); ++c) {
            s.setValue(QString("AssetManager/AssetTable/Col%1").arg(c), hh->sectionSize(c));
        }
    }
    // Persist current File Manager path
    if (fmDirModel) s.setValue("FileManager/CurrentPath", fmDirModel->rootPath());


    // File Manager

    if (versionTable) {
        auto hh = versionTable->horizontalHeader();
        for (int c = 0; c < versionTable->columnCount(); ++c) {
            s.setValue(QString("AssetManager/VersionTable/Col%1").arg(c), hh->sectionSize(c));
        }
    }

    if (fmSplitter) {
        s.setValue("FileManager/MainSplitter", fmSplitter->saveState());
        QVariantList sizes; for (int v : fmSplitter->sizes()) sizes << v; s.setValue("FileManager/MainSplitterSizes", sizes);
    }
    if (fmLeftSplitter) {
        s.setValue("FileManager/LeftSplitter", fmLeftSplitter->saveState());
        QVariantList sizes; for (int v : fmLeftSplitter->sizes()) sizes << v; s.setValue("FileManager/LeftSplitterSizes", sizes);
    }
    if (fmRightSplitter) {
        s.setValue("FileManager/RightSplitter", fmRightSplitter->saveState());
        QVariantList sizes; for (int v : fmRightSplitter->sizes()) sizes << v; s.setValue("FileManager/RightSplitterSizes", sizes);
    }
    s.setValue("FileManager/ViewMode", fmIsGridMode);
    if (fmPreviewPanel) s.setValue("FileManager/PreviewVisible", fmPreviewPanel->isVisible());
    s.setValue("FileManager/GroupSequences", fmGroupSequences);
    if (fmListView && fmListView->model()) {
        auto hh = fmListView->horizontalHeader();
        for (int c = 0; c < fmListView->model()->columnCount(); ++c) {
            s.setValue(QString("FileManager/ListView/Col%1").arg(c), hh->sectionSize(c));
        }
    }
    if (fmTree && fmTree->model()) {
        auto th = fmTree->header();
        for (int c = 0; c < fmTree->model()->columnCount(); ++c) {
            s.setValue(QString("FileManager/Tree/Col%1").arg(c), th->sectionSize(c));
        }
    }

    s.sync();
    QMainWindow::closeEvent(event);
}


void MainWindow::applyFmShortcuts()
{
    QSettings s("AugmentCode", "KAssetManager");
    s.beginGroup("FileManager/Shortcuts");
    for (auto it = fmShortcutObjs.begin(); it != fmShortcutObjs.end(); ++it) {
        const QString action = it.key();
        QShortcut* sc = it.value();
        if (!sc) continue;
        const QString stored = s.value(action).toString();
        if (!stored.isEmpty()) sc->setKey(QKeySequence(stored));
    }
    s.endGroup();
}

void MainWindow::onFmGroupSequencesToggled(bool checked)
{
    fmGroupSequences = checked;
    if (fmProxyModel) fmProxyModel->setGroupingEnabled(checked);
    // Rebuild for current root
    if (fmDirModel && fmProxyModel) {
        QString rootPath = fmDirModel->rootPath();
        if (!rootPath.isEmpty()) fmProxyModel->rebuildForRoot(rootPath);
    }
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("FileManager/GroupSequences", checked);
}

QKeySequence MainWindow::fmShortcutFor(const QString& actionName, const QKeySequence& def)
{
    QSettings s("AugmentCode", "KAssetManager");
    s.beginGroup("FileManager/Shortcuts");
    const QString stored = s.value(actionName).toString();
    s.endGroup();
    if (stored.isEmpty()) return def;
    return QKeySequence(stored);
}

void MainWindow::showDatabaseHealthDialog()
{
    DatabaseHealthDialog dialog(this);
    dialog.exec();
}
