#include "project_sequence_grouping_proxy_model.h"
#include "project_assets_model.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QMimeData>
#include <QUrl>

ProjectSequenceGroupingProxyModel::ProjectSequenceGroupingProxyModel(QObject* parent)
    : QAbstractProxyModel(parent), m_groupingEnabled(true) {}

void ProjectSequenceGroupingProxyModel::setGroupingEnabled(bool enabled) {
    if (m_groupingEnabled == enabled) return;
    qDebug() << "[ProjectSequenceGroupingProxyModel] setGroupingEnabled:" << enabled;
    beginResetModel();
    m_groupingEnabled = enabled;
    rebuildMapping();
    qDebug() << "[ProjectSequenceGroupingProxyModel] After rebuild, proxy row count:" << m_proxyToSource.size();
    endResetModel();
}

bool ProjectSequenceGroupingProxyModel::groupingEnabled() const {
    return m_groupingEnabled;
}

void ProjectSequenceGroupingProxyModel::setSourceModel(QAbstractItemModel *sourceModel) {
    if (this->sourceModel()) {
        disconnect(this->sourceModel(), nullptr, this, nullptr);
    }
    QAbstractProxyModel::setSourceModel(sourceModel);
    if (sourceModel) {
        connect(sourceModel, &QAbstractItemModel::modelAboutToBeReset, this, [this]() {
            beginResetModel();
        });
        connect(sourceModel, &QAbstractItemModel::modelReset, this, [this]() {
            rebuildMapping();
            endResetModel();
        });
        connect(sourceModel, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &, const QModelIndex &) {
            rebuildMapping();
            if (rowCount() > 0) {
                emit dataChanged(index(0, 0), index(rowCount() - 1, 0));
            }
        });
    }
    rebuildMapping();
}

QModelIndex ProjectSequenceGroupingProxyModel::mapToSource(const QModelIndex &proxyIndex) const {
    if (!proxyIndex.isValid() || !sourceModel()) return QModelIndex();
    int proxyRow = proxyIndex.row();
    if (proxyRow < 0 || proxyRow >= m_proxyToSource.size()) return QModelIndex();
    const SourceMapping &mapping = m_proxyToSource[proxyRow];
    return sourceModel()->index(mapping.sourceRow, 0);
}

QModelIndex ProjectSequenceGroupingProxyModel::mapFromSource(const QModelIndex &sourceIndex) const {
    if (!sourceIndex.isValid() || !sourceModel()) return QModelIndex();
    int sourceRow = sourceIndex.row();
    for (int i = 0; i < m_proxyToSource.size(); ++i) {
        if (m_proxyToSource[i].sourceRow == sourceRow) {
            if (m_groupingEnabled || m_proxyToSource[i].frameIndex == 0) {
                return index(i, 0);
            }
        }
    }
    return QModelIndex();
}

QModelIndex ProjectSequenceGroupingProxyModel::index(int row, int column, const QModelIndex &parent) const {
    if (parent.isValid() || column != 0 || row < 0 || row >= m_proxyToSource.size()) {
        return QModelIndex();
    }
    return createIndex(row, column);
}

QModelIndex ProjectSequenceGroupingProxyModel::parent(const QModelIndex &) const {
    return QModelIndex(); // Flat list
}

int ProjectSequenceGroupingProxyModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid() || !sourceModel()) return 0;
    return m_proxyToSource.size();
}

int ProjectSequenceGroupingProxyModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid() || !sourceModel()) return 0;
    return 1;
}

QVariant ProjectSequenceGroupingProxyModel::data(const QModelIndex &proxyIndex, int role) const {
    if (!proxyIndex.isValid() || !sourceModel()) return QVariant();

    int proxyRow = proxyIndex.row();
    if (proxyRow < 0 || proxyRow >= m_proxyToSource.size()) return QVariant();

    const SourceMapping &mapping = m_proxyToSource[proxyRow];
    QModelIndex sourceIdx = sourceModel()->index(mapping.sourceRow, 0);

    // If grouping is enabled or this is not a sequence, pass through source data
    if (m_groupingEnabled || mapping.frameIndex < 0) {
        return sourceModel()->data(sourceIdx, role);
    }

    // Grouping is disabled and this is an expanded frame
    // Override certain roles to show individual frame info
    if (role == Qt::DisplayRole || role == ProjectAssetsModel::FileNameRole) {
        return mapping.frameName;
    } else if (role == ProjectAssetsModel::FilePathRole) {
        return mapping.framePath;
    } else if (role == ProjectAssetsModel::IsSequenceRole) {
        // Individual frames are not sequences
        return false;
    } else {
        // Pass through other roles from source
        return sourceModel()->data(sourceIdx, role);
    }
}

Qt::ItemFlags ProjectSequenceGroupingProxyModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    return f;
}

QMimeData* ProjectSequenceGroupingProxyModel::mimeData(const QModelIndexList &indexes) const {
    QMimeData* mimeData = new QMimeData();
    QList<QUrl> urls;

    for (const QModelIndex& proxyIdx : indexes) {
        if (proxyIdx.isValid()) {
            QString path = data(proxyIdx, ProjectAssetsModel::FilePathRole).toString();
            if (!path.isEmpty()) {
                urls.append(QUrl::fromLocalFile(path));
            }
        }
    }

    mimeData->setUrls(urls);
    return mimeData;
}

Qt::DropActions ProjectSequenceGroupingProxyModel::supportedDragActions() const {
    return Qt::CopyAction;
}

void ProjectSequenceGroupingProxyModel::rebuildMapping() {
    m_proxyToSource.clear();
    if (!sourceModel()) return;

    int sourceRows = sourceModel()->rowCount();
    qDebug() << "[ProjectSequenceGroupingProxyModel] rebuildMapping: sourceRows=" << sourceRows << "groupingEnabled=" << m_groupingEnabled;
    
    for (int srcRow = 0; srcRow < sourceRows; ++srcRow) {
        QModelIndex srcIdx = sourceModel()->index(srcRow, 0);
        bool isSeq = srcIdx.data(ProjectAssetsModel::IsSequenceRole).toBool();

        if (!isSeq || m_groupingEnabled) {
            // Not a sequence, or grouping is enabled: show as single row
            SourceMapping mapping;
            mapping.sourceRow = srcRow;
            mapping.frameIndex = -1;
            m_proxyToSource.append(mapping);
        } else {
            qDebug() << "[ProjectSequenceGroupingProxyModel] Expanding sequence at srcRow" << srcRow;
            // Sequence with grouping disabled: expand into individual frames
            QString pattern = srcIdx.data(ProjectAssetsModel::SequencePatternRole).toString();
            int startFrame = srcIdx.data(ProjectAssetsModel::SequenceStartFrameRole).toInt();
            int endFrame = srcIdx.data(ProjectAssetsModel::SequenceEndFrameRole).toInt();
            QString firstPath = srcIdx.data(ProjectAssetsModel::FilePathRole).toString();

            if (firstPath.isEmpty() || startFrame > endFrame) {
                // Invalid sequence, show as single row
                SourceMapping mapping;
                mapping.sourceRow = srcRow;
                mapping.frameIndex = -1;
                m_proxyToSource.append(mapping);
                continue;
            }

            // Parse the first frame path to extract base and suffix
            QFileInfo fi(firstPath);
            QString name = fi.fileName();
            QString dir = fi.absolutePath();

            // Find digit position and padding
            int digitPos = -1;
            int digitPad = 0;
            for (int i = name.size() - 1; i >= 0; --i) {
                if (name[i].isDigit()) {
                    int j = i;
                    while (j >= 0 && name[j].isDigit()) --j;
                    digitPos = j + 1;
                    digitPad = i - j;
                    break;
                }
            }

            if (digitPos < 0 || digitPad <= 0) {
                // Can't parse, show as single row
                SourceMapping mapping;
                mapping.sourceRow = srcRow;
                mapping.frameIndex = -1;
                m_proxyToSource.append(mapping);
                continue;
            }

            QString base = name.left(digitPos);
            QString suffix = name.mid(digitPos + digitPad);

            // Create one proxy row per frame
            qDebug() << "[ProjectSequenceGroupingProxyModel] Creating" << (endFrame - startFrame + 1) << "proxy rows for frames" << startFrame << "to" << endFrame;
            for (int frame = startFrame; frame <= endFrame; ++frame) {
                QString frameNumStr = QString("%1").arg(frame, digitPad, 10, QLatin1Char('0'));
                QString frameName = base + frameNumStr + suffix;
                QString framePath = QDir(dir).filePath(frameName);

                SourceMapping mapping;
                mapping.sourceRow = srcRow;
                mapping.frameIndex = frame - startFrame;
                mapping.frameName = frameName;
                mapping.framePath = framePath;
                m_proxyToSource.append(mapping);
            }
        }
    }
}
