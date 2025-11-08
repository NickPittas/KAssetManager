#pragma once

#include <QListView>

class AssetGridView : public QListView
{
    Q_OBJECT

public:
    explicit AssetGridView(QWidget* parent = nullptr);

protected:
    void startDrag(Qt::DropActions supportedActions) override;
};
