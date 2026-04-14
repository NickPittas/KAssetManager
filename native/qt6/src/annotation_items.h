#ifndef ANNOTATION_ITEMS_H
#define ANNOTATION_ITEMS_H

#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QColor>
#include <QString>
#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QLineF>
#include <QPainterPath>
#include <QJsonObject>
#include <QPainter>

/**
 * @brief Base class for all annotation items
 * 
 * Provides common interface for drawing, selection, serialization,
 * and property management for all annotation types.
 */
class AnnotationItem : public QGraphicsItem
{
public:
    enum Type {
        Text,
        Freehand,
        Rectangle,
        Ellipse,
        Arrow
    };

    explicit AnnotationItem(QGraphicsItem* parent = nullptr);
    virtual ~AnnotationItem() = default;

    // Type identification
    virtual Type annotationType() const = 0;
    
    // QGraphicsItem interface
    virtual void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override = 0;
    virtual QRectF boundingRect() const override = 0;
    int type() const override { return UserType + annotationType(); }

    // Serialization
    virtual QJsonObject toJson() const = 0;
    static AnnotationItem* fromJson(const QJsonObject& json);

    // Properties
    QColor color() const { return m_color; }
    void setColor(const QColor& color) { m_color = color; update(); }
    
    int penWidth() const { return m_penWidth; }
    void setPenWidth(int width) { m_penWidth = width; update(); }
    
    bool isItemSelected() const { return m_isSelected; }
    void setItemSelected(bool selected) { m_isSelected = selected; update(); }

protected:
    // Override to sync Qt's selection with our flag
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    
    QColor m_color;
    int m_penWidth;
    bool m_isSelected;
};

/**
 * @brief Text annotation with customizable font and color
 */
class TextAnnotation : public AnnotationItem
{
public:
    explicit TextAnnotation(QGraphicsItem* parent = nullptr);
    TextAnnotation(const QString& text, const QPointF& position, QGraphicsItem* parent = nullptr);

    Type annotationType() const override { return Text; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;

    // Text properties
    QString text() const { return m_text; }
    void setText(const QString& text) { m_text = text; update(); }
    
    QFont font() const { return m_font; }
    void setFont(const QFont& font) { m_font = font; update(); }
    
    QPointF position() const { return m_position; }
    void setPosition(const QPointF& pos) { m_position = pos; setPos(pos); update(); }
    
    double scale() const { return m_scale; }
    void setScale(double scale) { m_scale = scale; QGraphicsItem::setScale(scale); update(); }
    
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    QString m_text;
    QFont m_font;
    QPointF m_position;
    double m_scale;
    bool m_isResizing;
    QPointF m_resizeStartPos;
    double m_resizeStartScale;
};

/**
 * @brief Freehand drawing annotation with stroke path
 */
class FreehandAnnotation : public AnnotationItem
{
public:
    explicit FreehandAnnotation(QGraphicsItem* parent = nullptr);
    FreehandAnnotation(const QPainterPath& path, QGraphicsItem* parent = nullptr);

    Type annotationType() const override { return Freehand; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;

    // Path operations
    QPainterPath path() const { return m_path; }
    void setPath(const QPainterPath& path) { prepareGeometryChange(); m_path = path; update(); }
    void addPoint(const QPointF& point);
    void finishPath();

private:
    QPainterPath m_path;
};

/**
 * @brief Rectangle/square annotation
 */
class RectangleAnnotation : public AnnotationItem
{
public:
    explicit RectangleAnnotation(QGraphicsItem* parent = nullptr);
    RectangleAnnotation(const QRectF& rect, QGraphicsItem* parent = nullptr);

    Type annotationType() const override { return Rectangle; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;

    // Rectangle properties
    QRectF rect() const { return m_rect; }
    void setRect(const QRectF& rect) { prepareGeometryChange(); m_rect = rect; update(); }

private:
    QRectF m_rect;
};

/**
 * @brief Ellipse/circle annotation
 */
class EllipseAnnotation : public AnnotationItem
{
public:
    explicit EllipseAnnotation(QGraphicsItem* parent = nullptr);
    EllipseAnnotation(const QRectF& rect, QGraphicsItem* parent = nullptr);

    Type annotationType() const override { return Ellipse; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;

    // Ellipse properties (bounding rect)
    QRectF rect() const { return m_rect; }
    void setRect(const QRectF& rect) { prepareGeometryChange(); m_rect = rect; update(); }

private:
    QRectF m_rect;
};

/**
 * @brief Arrow annotation with directional head
 */
class ArrowAnnotation : public AnnotationItem
{
public:
    explicit ArrowAnnotation(QGraphicsItem* parent = nullptr);
    ArrowAnnotation(const QLineF& line, QGraphicsItem* parent = nullptr);

    Type annotationType() const override { return Arrow; }
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;
    QRectF boundingRect() const override;
    QJsonObject toJson() const override;

    // Arrow properties
    QLineF line() const { return m_line; }
    void setLine(const QLineF& line) { prepareGeometryChange(); m_line = line; update(); }
    
    double arrowHeadSize() const { return m_arrowHeadSize; }
    void setArrowHeadSize(double size) { prepareGeometryChange(); m_arrowHeadSize = size; update(); }

private:
    QLineF m_line;
    double m_arrowHeadSize;
    
    // Helper to calculate arrow head polygon
    QPolygonF calculateArrowHead() const;
};

#endif // ANNOTATION_ITEMS_H
