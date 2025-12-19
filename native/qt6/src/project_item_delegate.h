#ifndef PROJECT_ITEM_DELEGATE_H
#define PROJECT_ITEM_DELEGATE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QModelIndex>
#include <QStyleOptionViewItem>

class QAbstractItemView;

class ProjectItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ProjectItemDelegate(QObject *parent = nullptr);

    void setThumbnailSize(int size);
    int thumbnailSize() const;
    
    // Set the map of selected versions (assetId -> versionString) for display
    void setSelectedVersions(const QHash<qint64, QString> *versions);
    
    /**
     * @brief Set the view this delegate is associated with.
     * 
     * This is needed for scrub frame lookup in the registry.
     */
    void setView(QAbstractItemView *view);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    // Override to create combo box editor for version selection
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Check if click is on version dropdown area
    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;
    
    // Check if a point (in item rect coordinates) is on the version dropdown
    bool isPointOnVersionBadge(const QRect& itemRect, const QPoint& point, const QModelIndex& index) const;
    
    // Get the version dropdown rect for an item
    QRect versionDropdownRect(const QRect& itemRect) const { return getVersionDropdownRect(itemRect); }

signals:
    // Emitted when user selects a different version from dropdown
    void versionSelected(qint64 assetId, const QString& versionPath);
    // Emitted when user clicks on the version dropdown area - view should show popup
    void versionDropdownRequested(const QModelIndex& index, const QPoint& globalPos);

private:
    QRect getVersionDropdownRect(const QRect& itemRect) const;
    void drawVersionBadge(QPainter* painter, const QRect& rect, const QString& version, bool hasMultiple, bool hovered) const;
    void drawProjectTypeBadge(QPainter* painter, const QRect& thumbRect, const QString& filePath) const;

    int m_thumbnailSize;
    mutable QModelIndex m_hoveredIndex;
    mutable QPoint m_lastMousePos;
    const QHash<qint64, QString> *m_selectedVersions = nullptr;
    QAbstractItemView *m_view = nullptr;
};

#endif // PROJECT_ITEM_DELEGATE_H
