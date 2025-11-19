#include "asset_sequence_grouping_proxy_model.h"
#include "assets_model.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>

AssetSequenceGroupingProxyModel::AssetSequenceGroupingProxyModel(QObject* parent)
    : QAbstractProxyModel(parent), m_groupingEnabled(true) {}

void AssetSequenceGroupingProxyModel::setGroupingEnabled(bool enabled) {
    if (m_groupingEnabled == enabled) return;
    qDebug() << "[AssetSequenceGroupingProxyModel] setGroupingEnabled:" << enabled;
    beginResetModel();
    m_groupingEnabled = enabled;
    rebuildMapping();
    qDebug() << "[AssetSequenceGroupingProxyModel] After rebuild, proxy row count:" << m_proxyToSource.size();
    endResetModel();
}

bool AssetSequenceGroupingProxyModel::groupingEnabled() const {
    return m_groupingEnabled;
}

void AssetSequenceGroupingProxyModel::setSourceModel(QAbstractItemModel *sourceModel) {
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
        connect(sourceModel, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
            // When source data changes, we need to rebuild and emit dataChanged for affected proxy rows
            rebuildMapping();
            // Emit dataChanged for all proxy rows (simplified approach)
            if (rowCount() > 0) {
                emit dataChanged(index(0, 0), index(rowCount() - 1, 0));
            }
        });
    }
    rebuildMapping();
}

QModelIndex AssetSequenceGroupingProxyModel::mapToSource(const QModelIndex &proxyIndex) const {
    if (!proxyIndex.isValid() || !sourceModel()) return QModelIndex();
    int proxyRow = proxyIndex.row();
    if (proxyRow < 0 || proxyRow >= m_proxyToSource.size()) return QModelIndex();
    const SourceMapping &mapping = m_proxyToSource[proxyRow];
    return sourceModel()->index(mapping.sourceRow, 0);
}

QModelIndex AssetSequenceGroupingProxyModel::mapFromSource(const QModelIndex &sourceIndex) const {
    if (!sourceIndex.isValid() || !sourceModel()) return QModelIndex();
    int sourceRow = sourceIndex.row();
    // Find first proxy row that maps to this source row
    for (int i = 0; i < m_proxyToSource.size(); ++i) {
        if (m_proxyToSource[i].sourceRow == sourceRow) {
            // Return the first proxy row for this source row
            // When grouped: frameIndex is -1 (single row)
            // When ungrouped: frameIndex is 0 for first frame, 1 for second, etc.
            // We want the first one in either case
            if (m_groupingEnabled || m_proxyToSource[i].frameIndex == 0) {
                return index(i, 0);
            }
        }
    }
    return QModelIndex();
}

QModelIndex AssetSequenceGroupingProxyModel::index(int row, int column, const QModelIndex &parent) const {
    if (parent.isValid() || column != 0 || row < 0 || row >= m_proxyToSource.size()) {
        return QModelIndex();
    }
    return createIndex(row, column);
}

QModelIndex AssetSequenceGroupingProxyModel::parent(const QModelIndex &) const {
    return QModelIndex(); // Flat list
}

int AssetSequenceGroupingProxyModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid() || !sourceModel()) return 0;
    return m_proxyToSource.size();
}

int AssetSequenceGroupingProxyModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid() || !sourceModel()) return 0;
    return 1;
}

QVariant AssetSequenceGroupingProxyModel::data(const QModelIndex &proxyIndex, int role) const {
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
    if (role == Qt::DisplayRole || role == AssetsModel::FileNameRole) {
        // Show individual frame filename
        return mapping.frameName;
    } else if (role == AssetsModel::FilePathRole) {
        // Show individual frame path
        return mapping.framePath;
    } else if (role == AssetsModel::IsSequenceRole) {
        // Individual frames are not sequences
        return false;
    } else {
        // Pass through other roles from source (ID, rating, tags, etc.)
        return sourceModel()->data(sourceIdx, role);
    }
}

void AssetSequenceGroupingProxyModel::rebuildMapping() {
    m_proxyToSource.clear();
    if (!sourceModel()) return;

    int sourceRows = sourceModel()->rowCount();
    qDebug() << "[AssetSequenceGroupingProxyModel] rebuildMapping: sourceRows=" << sourceRows << "groupingEnabled=" << m_groupingEnabled;
    for (int srcRow = 0; srcRow < sourceRows; ++srcRow) {
        QModelIndex srcIdx = sourceModel()->index(srcRow, 0);
        bool isSeq = srcIdx.data(AssetsModel::IsSequenceRole).toBool();

        if (!isSeq || m_groupingEnabled) {
            // Not a sequence, or grouping is enabled: show as single row
            SourceMapping mapping;
            mapping.sourceRow = srcRow;
            mapping.frameIndex = -1;
            m_proxyToSource.append(mapping);
        } else {
            qDebug() << "[AssetSequenceGroupingProxyModel] Expanding sequence at srcRow" << srcRow;
            // Sequence with grouping disabled: expand into individual frames
            QString pattern = srcIdx.data(AssetsModel::SequencePatternRole).toString();
            int startFrame = srcIdx.data(AssetsModel::SequenceStartFrameRole).toInt();
            int endFrame = srcIdx.data(AssetsModel::SequenceEndFrameRole).toInt();
            QString firstPath = srcIdx.data(AssetsModel::FilePathRole).toString();

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
            qDebug() << "[AssetSequenceGroupingProxyModel] Creating" << (endFrame - startFrame + 1) << "proxy rows for frames" << startFrame << "to" << endFrame;
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
