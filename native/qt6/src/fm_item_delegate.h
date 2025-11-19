#ifndef FM_ITEM_DELEGATE_H
#define FM_ITEM_DELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QModelIndex>
#include <QStyleOptionViewItem>

class FmItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit FmItemDelegate(QObject *parent = nullptr);

    void setThumbnailSize(int size);
    int thumbnailSize() const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    int m_thumbnailSize;
};

#endif // FM_ITEM_DELEGATE_H
