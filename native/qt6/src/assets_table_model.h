#pragma once
#include <QAbstractTableModel>
#include <QDateTime>
#include "assets_model.h"
#include <QMimeData>
#include <QSet>

class AssetsTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        NameColumn = 0,
        ExtensionColumn,
        SizeColumn,
        DateColumn,
        RatingColumn,
        ColumnCount
    };

    explicit AssetsTableModel(AssetsModel* sourceModel, QObject* parent = nullptr)
        : QAbstractTableModel(parent), m_sourceModel(sourceModel)
    {
        connect(m_sourceModel, &QAbstractItemModel::modelReset, this, [this]() {
            beginResetModel();
            endResetModel();
        });
        connect(m_sourceModel, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex& topLeft, const QModelIndex& bottomRight) {
            emit dataChanged(index(topLeft.row(), 0), index(bottomRight.row(), ColumnCount - 1));
        });
        connect(m_sourceModel, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex&, int first, int last) {
            beginInsertRows(QModelIndex(), first, last);
            endInsertRows();
        });
        connect(m_sourceModel, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex&, int first, int last) {
            beginRemoveRows(QModelIndex(), first, last);
            endRemoveRows();
        });
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return m_sourceModel->rowCount(QModelIndex());
    }

    int columnCount(const QModelIndex& parent = QModelIndex()) const override
    {
        if (parent.isValid())
            return 0;
        return ColumnCount;
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() >= rowCount() || index.column() >= ColumnCount)
            return QVariant();

        QModelIndex sourceIndex = m_sourceModel->index(index.row(), 0);

        if (role == Qt::DisplayRole) {
            switch (index.column()) {
                case NameColumn: {
                    QString fileName = m_sourceModel->data(sourceIndex, AssetsModel::FileNameRole).toString();
                    bool isSequence = m_sourceModel->data(sourceIndex, AssetsModel::IsSequenceRole).toBool();
                    if (isSequence) {
                        QString pattern = m_sourceModel->data(sourceIndex, AssetsModel::SequencePatternRole).toString();
                        return pattern.isEmpty() ? fileName : pattern;
                    }
                    return fileName;
                }
                case ExtensionColumn: {
                    QString fileType = m_sourceModel->data(sourceIndex, AssetsModel::FileTypeRole).toString();
                    return fileType.toUpper();
                }
                case SizeColumn: {
                    qint64 size = m_sourceModel->data(sourceIndex, AssetsModel::FileSizeRole).toLongLong();
                    return formatFileSize(size);
                }
                case DateColumn: {
                    QDateTime dt = m_sourceModel->data(sourceIndex, AssetsModel::LastModifiedRole).toDateTime();
                    return dt.isValid() ? dt.toString("yyyy-MM-dd hh:mm") : QString();
                }
                case RatingColumn: {
                    int rating = m_sourceModel->data(sourceIndex, AssetsModel::RatingRole).toInt();
                    if (rating > 0) {
                        return QString(rating, QChar(0x2605)); // ★
                    }
                    return QString();
                }
            }
        } else if (role == Qt::TextAlignmentRole) {
            if (index.column() == SizeColumn || index.column() == RatingColumn) {
                return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
            }
            return QVariant::fromValue(Qt::AlignLeft | Qt::AlignVCenter);
        } else if (role == Qt::UserRole) {
            // Return asset ID for context menus, etc.
            return m_sourceModel->data(sourceIndex, AssetsModel::IdRole);
        } else if (role == Qt::UserRole + 1) {
            // Return file path for drag-and-drop, preview, etc.
            return m_sourceModel->data(sourceIndex, AssetsModel::FilePathRole);
        } else if (role >= AssetsModel::IdRole && role <= AssetsModel::PreviewStateRole) {
            // Forward all AssetsModel custom roles to the source model
            return m_sourceModel->data(sourceIndex, role);
        }

        return QVariant();
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override
    {
        if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
            switch (section) {
                case NameColumn: return "Name";
                case ExtensionColumn: return "Type";
                case SizeColumn: return "Size";
                case DateColumn: return "Date Modified";
                case RatingColumn: return "Rating";
            }
        }
        return QVariant();
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        if (!index.isValid())
            return Qt::NoItemFlags;
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled;
    }

    QStringList mimeTypes() const override {
        return QStringList() << "application/x-kasset-asset-ids" << "text/uri-list";
    }

    QMimeData *mimeData(const QModelIndexList &indexes) const override {
        // Forward drag data generation to the underlying AssetsModel, but ensure unique rows
        QSet<int> rows;
        for (const QModelIndex &idx : indexes) {
            if (idx.isValid()) rows.insert(idx.row());
        }
        QModelIndexList src;
        src.reserve(rows.size());
        for (int r : rows) src << m_sourceModel->index(r, 0);
        return m_sourceModel->mimeData(src);
    }

    Qt::DropActions supportedDragActions() const override {
        return m_sourceModel->supportedDragActions();
    }

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override
    {
        // Store sort settings for future use
        m_sortColumn = column;
        m_sortOrder = order;

        // Trigger re-sort by emitting layoutAboutToBeChanged/layoutChanged
        emit layoutAboutToBeChanged();
        // Note: Actual sorting would require access to the underlying data in AssetsModel
        // For now, we'll just emit the signal to indicate sorting is requested
        emit layoutChanged();
    }

    AssetsModel* sourceModel() const { return m_sourceModel; }

private:
    QString formatFileSize(qint64 bytes) const
    {
        if (bytes < 1024)
            return QString("%1 B").arg(bytes);
        else if (bytes < 1024 * 1024)
            return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
        else if (bytes < 1024 * 1024 * 1024)
            return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
        else
            return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
    }

    AssetsModel* m_sourceModel;
    int m_sortColumn = 0;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};

