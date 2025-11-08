#pragma once

#include <QHash>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>

class SequenceGroupingProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    struct Info {
        QString dir;
        QString base;
        QString ext;
        int start = -1;
        int end = -1;
        int count = 0;
        QString reprPath;
    };

    explicit SequenceGroupingProxyModel(QObject* parent = nullptr);

    void setGroupingEnabled(bool on);
    bool groupingEnabled() const;

    void setHideFolders(bool hide);
    bool hideFolders() const;

    void rebuildForRoot(const QString& dirPath);

    bool isRepresentativeProxyIndex(const QModelIndex& proxyIdx) const;
    Info infoForProxyIndex(const QModelIndex& proxyIdx) const;

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    QVariant data(const QModelIndex& proxyIndex, int role) const override;
    bool lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const override;

private:
    bool m_enabled = true;
    bool m_hideFolders = false;
    QSet<QString> m_hidden;
    QHash<QString, Info> m_infoByRepr;
    QHash<QString, QString> m_keyByRepr;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};
