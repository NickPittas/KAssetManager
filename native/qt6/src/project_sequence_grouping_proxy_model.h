#ifndef PROJECT_SEQUENCE_GROUPING_PROXY_MODEL_H
#define PROJECT_SEQUENCE_GROUPING_PROXY_MODEL_H

#include <QAbstractProxyModel>
#include <QVector>
#include <QString>

/**
 * @brief Proxy model for Project Manager that handles sequence grouping/ungrouping.
 * 
 * When grouping is enabled (default), sequences are shown as single rows.
 * When grouping is disabled, sequences are expanded to show individual frames.
 */
class ProjectSequenceGroupingProxyModel : public QAbstractProxyModel {
    Q_OBJECT
public:
    explicit ProjectSequenceGroupingProxyModel(QObject* parent = nullptr);

    void setGroupingEnabled(bool enabled);
    bool groupingEnabled() const;

    void setSourceModel(QAbstractItemModel *sourceModel) override;

    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &) const override;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

    QVariant data(const QModelIndex &proxyIndex, int role) const override;
    
    // Pass through drag/drop support
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QMimeData* mimeData(const QModelIndexList &indexes) const override;
    Qt::DropActions supportedDragActions() const override;

private:
    struct SourceMapping {
        int sourceRow = -1;
        int frameIndex = -1; // -1 for non-sequence or grouped sequence, >= 0 for expanded frame
        QString frameName;
        QString framePath;
    };

    void rebuildMapping();

    bool m_groupingEnabled = true;
    QVector<SourceMapping> m_proxyToSource;
};

#endif // PROJECT_SEQUENCE_GROUPING_PROXY_MODEL_H
