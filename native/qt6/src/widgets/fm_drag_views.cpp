#include "widgets/fm_drag_views.h"

#include "file_utils.h"
#include "virtual_drag.h"
#include "widgets/sequence_grouping_proxy_model.h"

#include <QDataStream>
#include <QDir>
#include <QDrag>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QListView>
#include <QMimeData>
#include <QPainter>
#include <QSet>
#include <QStringList>
#include <QTableView>
#include <QUrl>
#include <QVector>

namespace {

QStringList buildSequenceFrameList(const QString& reprPath, int start, int end)
{
    QStringList out;
    if (reprPath.isEmpty() || start > end) return out;
    QFileInfo fi(reprPath);
    QString name = fi.fileName();
    int pos = -1;
    int pad = 0;
    for (int i = name.size() - 1; i >= 0; --i) {
        if (name[i].isDigit()) {
            int j = i;
            while (j >= 0 && name[j].isDigit()) --j;
            pos = j + 1;
            pad = (i - j);
            break;
        }
    }
    if (pos < 0 || pad <= 0) return out;
    QString base = name.left(pos);
    QString suf = name.mid(pos + pad);
    for (int f = start; f <= end; ++f) {
        QString num = QString("%1").arg(f, pad, 10, QLatin1Char('0'));
        QString p = QDir(fi.absolutePath()).filePath(base + num + suf);
        if (FileUtils::fileExists(p)) out << p;
    }
    return out;
}

void populateDragMimeData(const QStringList& fullPaths,
                          const QStringList& dccTextLines,
                          const QStringList& dccUriLines,
                          const QList<QUrl>& repUrls,
                          Qt::DropActions supported,
                          QWidget* dragParent)
{
    QMimeData* mime = new QMimeData();
    if (!dccTextLines.isEmpty()) {
        mime->setText(dccTextLines.join("\r\n"));
        QByteArray uriData = dccUriLines.join("\r\n").toUtf8();
        mime->setData("text/uri-list", uriData);
    }
    if (!repUrls.isEmpty()) mime->setUrls(repUrls);
    if (!fullPaths.isEmpty()) {
        QByteArray enc;
        QDataStream ds(&enc, QIODevice::WriteOnly);
        ds << fullPaths;
        mime->setData("application/x-kasset-sequence-urls", enc);
    }

    QDrag* drag = new QDrag(dragParent);
    drag->setMimeData(mime);
    const int itemCount = fullPaths.size();
    QPixmap pm(60, 60);
    pm.fill(Qt::transparent);
    QPainter pr(&pm);
    pr.setRenderHint(QPainter::Antialiasing);
    pr.setBrush(QColor(88, 166, 255, 200));
    pr.setPen(Qt::NoPen);
    pr.drawRoundedRect(0, 0, 60, 60, 8, 8);
    pr.setPen(Qt::white);
    QFont f = pr.font();
    f.setPixelSize(20);
    f.setBold(true);
    pr.setFont(f);
    pr.drawText(QRect(0, 0, 60, 60), Qt::AlignCenter, QString::number(itemCount));
    pr.end();
    drag->setPixmap(pm);
    drag->setHotSpot(QPoint(30, 30));
    drag->exec(supported, Qt::CopyAction);
}

template <typename View>
void handleDrag(View* view,
                SequenceGroupingProxyModel* proxy,
                QFileSystemModel* dirModel,
                const QModelIndexList& selected,
                Qt::DropActions supported)
{
    if (selected.isEmpty()) return;
    QStringList dccTextLines;
    QStringList dccUriLines;
    QList<QUrl> repUrls;
    QStringList fullPaths;

    auto appendRep = [&repUrls](const QString& p) {
        if (!p.isEmpty()) repUrls.append(QUrl::fromLocalFile(p));
    };

    for (const QModelIndex& idx : selected) {
        if (!idx.isValid()) continue;
        QModelIndex proxyIdx = idx;
        if (proxy && proxyIdx.model() == proxy && proxy->isRepresentativeProxyIndex(proxyIdx)) {
            auto inf = proxy->infoForProxyIndex(proxyIdx);
            const QStringList frames = buildSequenceFrameList(inf.reprPath, inf.start, inf.end);
            if (!frames.isEmpty()) {
                const QString dirPath = QFileInfo(frames.first()).absolutePath();
                appendRep(dirPath);
                dccTextLines << QDir::toNativeSeparators(dirPath);
                dccUriLines << QUrl::fromLocalFile(dirPath).toString(QUrl::FullyEncoded);
                fullPaths.append(frames);
            }
        } else {
            QModelIndex srcIdx = proxyIdx;
            if (proxy && proxyIdx.model() == proxy) srcIdx = proxy->mapToSource(proxyIdx);
            const QString p = dirModel ? dirModel->filePath(srcIdx) : QString();
            if (!p.isEmpty()) {
                appendRep(p);
                fullPaths.append(p);
            }
        }
    }

    QVector<QString> frameVec = QVector<QString>(fullPaths.cbegin(), fullPaths.cend());
    QSet<QString> folderSet;
    for (const QUrl& u : repUrls) {
        if (u.isLocalFile()) folderSet.insert(QFileInfo(u.toLocalFile()).absoluteFilePath());
    }
    QVector<QString> folderVec = QVector<QString>(folderSet.cbegin(), folderSet.cend());
    if (!frameVec.isEmpty() || !folderVec.isEmpty()) {
        VirtualDrag::startAdaptivePathsDrag(frameVec, folderVec);
        return;
    }

    populateDragMimeData(fullPaths, dccTextLines, dccUriLines, repUrls, supported, view);
}

} // namespace

FmGridViewEx::FmGridViewEx(SequenceGroupingProxyModel* proxy,
                           QFileSystemModel* dirModel,
                           QWidget* parent)
    : QListView(parent)
    , m_proxy(proxy)
    , m_dirModel(dirModel)
{
}

void FmGridViewEx::startDrag(Qt::DropActions supported)
{
    const QModelIndexList sel = selectionModel() ? selectionModel()->selectedIndexes() : QModelIndexList{};
    handleDrag(this, m_proxy, m_dirModel, sel, supported);
}

FmListViewEx::FmListViewEx(SequenceGroupingProxyModel* proxy,
                           QFileSystemModel* dirModel,
                           QWidget* parent)
    : QTableView(parent)
    , m_proxy(proxy)
    , m_dirModel(dirModel)
{
}

void FmListViewEx::startDrag(Qt::DropActions supported)
{
    if (!selectionModel()) return;
    handleDrag(this, m_proxy, m_dirModel, selectionModel()->selectedIndexes(), supported);
}
