#include "asset_grid_view.h"
#include "assets_model.h"
#include "virtual_drag.h"
#include "file_utils.h"
#include <QPainter>
#include <QFileInfo>
#include <QDir>

AssetGridView::AssetGridView(QWidget *parent) : QListView(parent) {}

void AssetGridView::startDrag(Qt::DropActions supportedActions)
{
    QModelIndexList indexes = selectionModel()->selectedIndexes();
    if (indexes.isEmpty()) {
        return;
    }

    // Build adaptive payloads from selected assets using model roles
    QVector<QString> frameVec; // expanded frames + single files
    QSet<QString> folderSet;   // sequence folders + single files (for DCCs)
    auto buildSeq = [](const QString& reprPath, int start, int end){
        QStringList out; if (reprPath.isEmpty() || start > end) return out;
        QFileInfo fi(reprPath); QString name = fi.fileName();
        int pos=-1, pad=0; for(int i=name.size()-1;i>=0;--i){ if (name[i].isDigit()){ int j=i; while(j>=0 && name[j].isDigit()) --j; pos=j+1; pad=(i-j); break; } }
        if (pos<0||pad<=0) return out; QString base=name.left(pos), suf=name.mid(pos+pad);
        for (int f=start; f<=end; ++f){ QString num=QString("%1").arg(f, pad, 10, QLatin1Char('0')); QString p=QDir(fi.absolutePath()).filePath(base+num+suf); if (FileUtils::fileExists(p)) out<<p; }
        return out;
    };
    for (const QModelIndex &idx : indexes) {
        const bool isSeq = idx.data(AssetsModel::IsSequenceRole).toBool();
        const QString firstPath = idx.data(AssetsModel::FilePathRole).toString();
        if (firstPath.isEmpty()) continue;
        if (isSeq) {
            const int start = idx.data(AssetsModel::SequenceStartFrameRole).toInt();
            const int end   = idx.data(AssetsModel::SequenceEndFrameRole).toInt();
            const QStringList frames = buildSeq(firstPath, start, end);
            for (const QString &p : frames) frameVec.push_back(p);
            if (!frames.isEmpty()) folderSet.insert(QFileInfo(frames.first()).absolutePath());
        } else {
            frameVec.push_back(firstPath);
            folderSet.insert(firstPath); // direct file for DCCs
        }
    }

    // Create a compact drag pixmap showing count
    int count = indexes.size();
    QPixmap pixmap(80, 80);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(QColor(88, 166, 255, 200));
    painter.setPen(QPen(QColor(255, 255, 255), 2));
    painter.drawRoundedRect(5, 5, 70, 70, 8, 8);
    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPixelSize(32);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(5, 5, 70, 70), Qt::AlignCenter, QString::number(count));
    painter.end();

    // Begin native drag (pixmap purely cosmetic)
    QVector<QString> folderVec = QVector<QString>(folderSet.cbegin(), folderSet.cend());
    if (!frameVec.isEmpty() || !folderVec.isEmpty()) {
        VirtualDrag::startAdaptivePathsDrag(frameVec, folderVec);
    }
    return;
}
