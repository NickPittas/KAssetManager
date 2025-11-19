#ifndef FM_VIEWS_EX_H
#define FM_VIEWS_EX_H

#include <QListView>
#include <QTableView>

class SequenceGroupingProxyModel;
class QFileSystemModel;

// File Manager custom views to expand image sequences to full frame lists on drag
class FmGridViewEx : public QListView 
{
    Q_OBJECT
public:
    explicit FmGridViewEx(SequenceGroupingProxyModel* proxy, QFileSystemModel* dirModel, QWidget* parent = nullptr);

protected:
    void startDrag(Qt::DropActions supported) override;

private:
    SequenceGroupingProxyModel* m_proxy = nullptr;
    QFileSystemModel* m_dirModel = nullptr;
};

class FmListViewEx : public QTableView 
{
    Q_OBJECT
public:
    explicit FmListViewEx(SequenceGroupingProxyModel* proxy, QFileSystemModel* dirModel, QWidget* parent = nullptr);

protected:
    void startDrag(Qt::DropActions supported) override;

private:
    SequenceGroupingProxyModel* m_proxy = nullptr;
    QFileSystemModel* m_dirModel = nullptr;
};

#endif // FM_VIEWS_EX_H
