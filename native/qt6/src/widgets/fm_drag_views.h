#pragma once

#include <QListView>
#include <QTableView>

class SequenceGroupingProxyModel;
class QFileSystemModel;

class FmGridViewEx : public QListView
{
public:
    explicit FmGridViewEx(SequenceGroupingProxyModel* proxy,
                          QFileSystemModel* dirModel,
                          QWidget* parent = nullptr);

protected:
    void startDrag(Qt::DropActions supported) override;

private:
    SequenceGroupingProxyModel* m_proxy = nullptr;
    QFileSystemModel* m_dirModel = nullptr;
};

class FmListViewEx : public QTableView
{
public:
    explicit FmListViewEx(SequenceGroupingProxyModel* proxy,
                          QFileSystemModel* dirModel,
                          QWidget* parent = nullptr);

protected:
    void startDrag(Qt::DropActions supported) override;

private:
    SequenceGroupingProxyModel* m_proxy = nullptr;
    QFileSystemModel* m_dirModel = nullptr;
};
