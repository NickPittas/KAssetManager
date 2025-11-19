#ifndef ASSET_GRID_VIEW_H
#define ASSET_GRID_VIEW_H

#include <QListView>

class AssetGridView : public QListView
{
    Q_OBJECT
public:
    explicit AssetGridView(QWidget *parent = nullptr);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
};

#endif // ASSET_GRID_VIEW_H
