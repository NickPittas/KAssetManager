#ifndef FM_ITEM_DELEGATE_H
#define FM_ITEM_DELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QModelIndex>
#include <QStyleOptionViewItem>

class QAbstractItemView;

class FmItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit FmItemDelegate(QObject *parent = nullptr);

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

#endif // FM_ITEM_DELEGATE_H
