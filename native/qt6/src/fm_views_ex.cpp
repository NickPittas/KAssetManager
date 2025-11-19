#include "fm_views_ex.h"
#include "sequence_grouping_proxy_model.h"
#include "file_utils.h"
#include "virtual_drag.h"
#include <QFileSystemModel>
#include <QMimeData>
#include <QDrag>
#include <QPainter>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QDataStream>

// FmGridViewEx implementation

FmGridViewEx::FmGridViewEx(SequenceGroupingProxyModel* proxy, QFileSystemModel* dirModel, QWidget* parent)
    : QListView(parent), m_proxy(proxy), m_dirModel(dirModel) 
{
}

void FmGridViewEx::startDrag(Qt::DropActions supported) 
{
    QModelIndexList sel = selectionModel() ? selectionModel()->selectedIndexes() : QModelIndexList{};
    if (sel.isEmpty()) return;
    // Build payloads
    QStringList dccTextLines; // sequence-notation Windows paths
    QStringList dccUriLines;  // sequence-notation file:/// URIs
    QList<QUrl> repUrls;      // for non-sequences
    QStringList fullPaths;    // expanded frames for internal ops

    auto appendRep = [&repUrls](const QString& p){ if(!p.isEmpty()) repUrls.append(QUrl::fromLocalFile(p)); };
    auto buildSeq = [](const QString& reprPath, int start, int end){
        QStringList out; if (reprPath.isEmpty() || start> end) return out;
        QFileInfo fi(reprPath); QString name = fi.fileName();
        int pos=-1, pad=0; for(int i=name.size()-1;i>=0;--i){ if (name[i].isDigit()){ int j=i; while(j>=0 && name[j].isDigit()) --j; pos=j+1; pad=(i-j); break; } }
        if (pos<0||pad<=0) return out; QString base=name.left(pos), suf=name.mid(pos+pad);
        for (int f=start; f<=end; ++f){ QString num=QString("%1").arg(f, pad, 10, QLatin1Char('0')); QString p=QDir(fi.absolutePath()).filePath(base+num+suf); if (FileUtils::fileExists(p)) out<<p; }
        return out;
    };
    for (const QModelIndex& idx : sel) {
        if (!idx.isValid()) continue;
        QModelIndex proxyIdx = idx; // QListView returns per-item index; single column
        if (m_proxy && proxyIdx.model()==m_proxy && m_proxy->isRepresentativeProxyIndex(proxyIdx)) {
            auto inf = m_proxy->infoForProxyIndex(proxyIdx);
            const QStringList frames = buildSeq(inf.reprPath, inf.start, inf.end);
            if (!frames.isEmpty()) {
                // External: provide only the folder path for sequences (mitigate Nuke)
                const QString dirPath = QFileInfo(frames.first()).absolutePath();
                appendRep(dirPath);
                // Also provide the folder path as text/URI for DCCs
                dccTextLines << QDir::toNativeSeparators(dirPath);
                dccUriLines  << QUrl::fromLocalFile(dirPath).toString(QUrl::FullyEncoded);
                // Internal: full frame list retained for our own ops
                fullPaths.append(frames);
            }
        } else {
            QModelIndex srcIdx = proxyIdx;
            if (m_proxy && proxyIdx.model()==m_proxy) srcIdx = m_proxy->mapToSource(proxyIdx);
            const QString p = m_dirModel ? m_dirModel->filePath(srcIdx) : QString();
            if (!p.isEmpty()) { appendRep(p); fullPaths.append(p); }
        }
    }

    // Use adaptive native drag for sequences/non-seqs combined: frames for Explorer/self, folders for DCCs
    QVector<QString> frameVec;
    frameVec.reserve(fullPaths.size());
    for (const QString &p : fullPaths) frameVec.push_back(p);
    QSet<QString> folderSet;
    for (const QUrl &u : repUrls) if (u.isLocalFile()) folderSet.insert(QFileInfo(u.toLocalFile()).absoluteFilePath());
    QVector<QString> folderVec = QVector<QString>(folderSet.cbegin(), folderSet.cend());
    if (!frameVec.isEmpty() || !folderVec.isEmpty()) {
        VirtualDrag::startAdaptivePathsDrag(frameVec, folderVec);
    }
    return;
    QMimeData* mime = new QMimeData();
    if (!dccTextLines.isEmpty()) {
        mime->setText(dccTextLines.join("\r\n"));
        QByteArray uriData = dccUriLines.join("\r\n").toUtf8();
        mime->setData("text/uri-list", uriData);
    }
    if (!repUrls.isEmpty()) mime->setUrls(repUrls);
    if (!fullPaths.isEmpty()) {
        QByteArray enc; QDataStream ds(&enc, QIODevice::WriteOnly); ds << fullPaths;
        mime->setData("application/x-kasset-sequence-urls", enc);
    }
    QDrag* drag = new QDrag(this);
    drag->setMimeData(mime);
    // Simple badge pixmap with item count (use total count we expose internally)
    const int itemCount = fullPaths.size();
    QPixmap pm(60, 60); pm.fill(Qt::transparent);
    QPainter pr(&pm); pr.setRenderHint(QPainter::Antialiasing);
    pr.setBrush(QColor(88,166,255,200)); pr.setPen(Qt::NoPen); pr.drawRoundedRect(0,0,60,60,8,8);
    pr.setPen(Qt::white); QFont f=pr.font(); f.setPixelSize(20); f.setBold(true); pr.setFont(f);
    pr.drawText(QRect(0,0,60,60), Qt::AlignCenter, QString::number(itemCount)); pr.end();
    drag->setPixmap(pm); drag->setHotSpot(QPoint(30,30));
    drag->exec(supported, Qt::CopyAction);
}

// FmListViewEx implementation

FmListViewEx::FmListViewEx(SequenceGroupingProxyModel* proxy, QFileSystemModel* dirModel, QWidget* parent)
    : QTableView(parent), m_proxy(proxy), m_dirModel(dirModel) 
{
}

void FmListViewEx::startDrag(Qt::DropActions supported) 
{
    if (!selectionModel()) return;
    QModelIndexList sel = selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return;
    // Build payloads
    QStringList dccTextLines; // sequence-notation Windows paths
    QStringList dccUriLines;  // sequence-notation file:/// URIs
    QList<QUrl> repUrls;      // for non-sequences
    QStringList fullPaths;    // expanded frames for internal ops

    auto appendRep = [&repUrls](const QString& p){ if(!p.isEmpty()) repUrls.append(QUrl::fromLocalFile(p)); };
    auto buildSeq = [](const QString& reprPath, int start, int end){
        QStringList out; if (reprPath.isEmpty() || start> end) return out;
        QFileInfo fi(reprPath); QString name = fi.fileName();
        int pos=-1, pad=0; for(int i=name.size()-1;i>=0;--i){ if (name[i].isDigit()){ int j=i; while(j>=0 && name[j].isDigit()) --j; pos=j+1; pad=(i-j); break; } }
        if (pos<0||pad<=0) return out; QString base=name.left(pos), suf=name.mid(pos+pad);
        for (int f=start; f<=end; ++f){ QString num=QString("%1").arg(f, pad, 10, QLatin1Char('0')); QString p=QDir(fi.absolutePath()).filePath(base+num+suf); if (FileUtils::fileExists(p)) out<<p; }
        return out;
    };
    QSet<QModelIndex> rows;
    for (const QModelIndex& idx : sel) { if (idx.isValid()) rows.insert(idx.sibling(idx.row(), 0)); }
    for (const QModelIndex& rowIdx : rows) {
        QModelIndex proxyIdx = rowIdx;
        if (m_proxy && proxyIdx.model()==m_proxy && m_proxy->isRepresentativeProxyIndex(proxyIdx)) {
            auto inf = m_proxy->infoForProxyIndex(proxyIdx);
            const QStringList frames = buildSeq(inf.reprPath, inf.start, inf.end);
            if (!frames.isEmpty()) {
                // External: provide only the folder path for sequences (mitigate Nuke)
                const QString dirPath = QFileInfo(frames.first()).absolutePath();
                appendRep(dirPath);
                // Also provide the folder path as text/URI for DCCs
                dccTextLines << QDir::toNativeSeparators(dirPath);
                dccUriLines  << QUrl::fromLocalFile(dirPath).toString(QUrl::FullyEncoded);
                // Internal: full frame list retained for our own ops
                fullPaths.append(frames);
            }
        } else {
            QModelIndex srcIdx = proxyIdx;
            if (m_proxy && proxyIdx.model()==m_proxy) srcIdx = m_proxy->mapToSource(proxyIdx);
            const QString p = m_dirModel ? m_dirModel->filePath(srcIdx) : QString();
            if (!p.isEmpty()) { appendRep(p); fullPaths.append(p); }
        }
    }
    // Use adaptive native drag for sequences/non-seqs combined: frames for Explorer/self, folders/files for DCCs
    QVector<QString> frameVec; frameVec.reserve(fullPaths.size());
    for (const QString &p : fullPaths) frameVec.push_back(p);
    QSet<QString> folderSet;
    for (const QUrl &u : repUrls) if (u.isLocalFile()) folderSet.insert(QFileInfo(u.toLocalFile()).absoluteFilePath());
    QVector<QString> folderVec = QVector<QString>(folderSet.cbegin(), folderSet.cend());
    if (!frameVec.isEmpty() || !folderVec.isEmpty()) {
        VirtualDrag::startAdaptivePathsDrag(frameVec, folderVec);
    }
    return;
}
