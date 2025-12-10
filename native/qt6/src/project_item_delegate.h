#ifndef PROJECT_ITEM_DELEGATE_H
#define PROJECT_ITEM_DELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QModelIndex>
#include <QStyleOptionViewItem>

class ProjectItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ProjectItemDelegate(QObject *parent = nullptr);

    void setThumbnailSize(int size);
    int thumbnailSize() const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    // Override to create combo box editor for version selection
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Check if click is on version dropdown area
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;

signals:
    // Emitted when user selects a different version from dropdown
    void versionSelected(qint64 assetId, const QString& versionPath);

private:
    QRect getVersionDropdownRect(const QRect& itemRect) const;
    void drawVersionBadge(QPainter* painter, const QRect& rect, const QString& version, bool hasMultiple, bool hovered) const;
    void drawProjectTypeBadge(QPainter* painter, const QRect& thumbRect, const QString& filePath) const;

    int m_thumbnailSize;
    mutable QModelIndex m_hoveredIndex;
    mutable QPoint m_lastMousePos;
};

#endif // PROJECT_ITEM_DELEGATE_H
