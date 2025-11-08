#pragma once

#include <QStyledItemDelegate>

class FmItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FmItemDelegate(QObject* parent = nullptr);

    void setThumbnailSize(int size);
    int thumbnailSize() const;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    int m_thumbnailSize;
};
