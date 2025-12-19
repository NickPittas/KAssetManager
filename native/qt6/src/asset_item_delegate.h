#ifndef ASSET_ITEM_DELEGATE_H
#define ASSET_ITEM_DELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QModelIndex>
#include <QStyleOptionViewItem>

class QAbstractItemView;

class AssetItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit AssetItemDelegate(QObject *parent = nullptr);

    void setThumbnailSize(int size);
    int thumbnailSize() const;

    /**
     * @brief Set the view this delegate is associated with.
     * 
     * This is needed for scrub frame lookup in the registry.
     */
    void setView(QAbstractItemView *view);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    int m_thumbnailSize;
    QAbstractItemView *m_view = nullptr;
};

#endif // ASSET_ITEM_DELEGATE_H
