#include "annotation_items.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QJsonArray>
#include <QFontMetrics>
#include <QtMath>

// ============================================================================
// AnnotationItem (Base Class)
// ============================================================================

AnnotationItem::AnnotationItem(QGraphicsItem* parent)
    : QGraphicsItem(parent)
    , m_color(Qt::red)
    , m_penWidth(3)
    , m_isSelected(false)
{
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsMovable);
}

QVariant AnnotationItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemSelectedChange) {
        m_isSelected = value.toBool();
        update(); // Trigger repaint to show/hide selection handles
    }
    return QGraphicsItem::itemChange(change, value);
}

AnnotationItem* AnnotationItem::fromJson(const QJsonObject& json)
{
    QString typeStr = json["type"].toString();
    AnnotationItem* item = nullptr;

    if (typeStr == "text") {
        QString text = json["text"].toString();
        QPointF pos(json["x"].toDouble(), json["y"].toDouble());
        item = new TextAnnotation(text, pos);
        
        // Font properties
        if (json.contains("font")) {
            QJsonObject fontObj = json["font"].toObject();
            QFont font;
            font.setFamily(fontObj["family"].toString("Arial"));
            font.setPointSize(fontObj["size"].toInt(12));
            font.setBold(fontObj["bold"].toBool(false));
            font.setItalic(fontObj["italic"].toBool(false));
            static_cast<TextAnnotation*>(item)->setFont(font);
        }
        
        // Scale
        if (json.contains("scale")) {
            static_cast<TextAnnotation*>(item)->setScale(json["scale"].toDouble(1.0));
        }
    }
    else if (typeStr == "freehand") {
        QPainterPath path;
        QJsonArray points = json["points"].toArray();
        if (!points.isEmpty()) {
            QJsonObject firstPt = points[0].toObject();
            path.moveTo(firstPt["x"].toDouble(), firstPt["y"].toDouble());
            for (int i = 1; i < points.size(); ++i) {
                QJsonObject pt = points[i].toObject();
                path.lineTo(pt["x"].toDouble(), pt["y"].toDouble());
            }
        }
        item = new FreehandAnnotation(path);
    }
    else if (typeStr == "rectangle") {
        QRectF rect(
            json["x"].toDouble(),
            json["y"].toDouble(),
            json["width"].toDouble(),
            json["height"].toDouble()
        );
        item = new RectangleAnnotation(rect);
    }
    else if (typeStr == "ellipse") {
        QRectF rect(
            json["x"].toDouble(),
            json["y"].toDouble(),
            json["width"].toDouble(),
            json["height"].toDouble()
        );
        item = new EllipseAnnotation(rect);
    }
    else if (typeStr == "arrow") {
        QLineF line(
            json["x1"].toDouble(),
            json["y1"].toDouble(),
            json["x2"].toDouble(),
            json["y2"].toDouble()
        );
        item = new ArrowAnnotation(line);
        if (json.contains("arrowHeadSize")) {
            static_cast<ArrowAnnotation*>(item)->setArrowHeadSize(json["arrowHeadSize"].toDouble());
        }
    }

    if (item) {
        // Common properties
        item->setColor(QColor(json["color"].toString("#FF0000")));
        item->setPenWidth(json["penWidth"].toInt(3));
    }

    return item;
}

// ============================================================================
// TextAnnotation
// ============================================================================

TextAnnotation::TextAnnotation(QGraphicsItem* parent)
    : AnnotationItem(parent)
    , m_text("Text")
    , m_font("Arial", 12)
    , m_position(0, 0)
    , m_scale(1.0)
    , m_isResizing(false)
    , m_resizeStartScale(1.0)
{
    setAcceptHoverEvents(true);
}

TextAnnotation::TextAnnotation(const QString& text, const QPointF& position, QGraphicsItem* parent)
    : AnnotationItem(parent)
    , m_text(text)
    , m_font("Arial", 12)
    , m_position(position)
    , m_scale(1.0)
    , m_isResizing(false)
    , m_resizeStartScale(1.0)
{
    setPos(position);
    setAcceptHoverEvents(true);
}

void TextAnnotation::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setFont(m_font);
    painter->setPen(QPen(m_color, m_penWidth));
    
    // Draw text with background for readability
    QFontMetrics fm(m_font);
    QRectF textRect = fm.boundingRect(m_text);
    textRect.adjust(-2, -2, 2, 2);
    
    // Semi-transparent background
    painter->fillRect(textRect, QColor(0, 0, 0, 128));
    
    // Draw text
    painter->drawText(textRect, Qt::AlignCenter, m_text);
    
    // Selection indicator with resize handles
    if (m_isSelected) {
        painter->setPen(QPen(Qt::yellow, 1, Qt::DashLine));
        painter->drawRect(textRect);
        
        // Draw resize handle in bottom-right corner
        QRectF handleRect(textRect.right() - 8, textRect.bottom() - 8, 8, 8);
        painter->setBrush(Qt::yellow);
        painter->setPen(Qt::NoPen);
        painter->drawRect(handleRect);
    }
}

QRectF TextAnnotation::boundingRect() const
{
    QFontMetrics fm(m_font);
    QRectF rect = fm.boundingRect(m_text);
    return rect.adjusted(-5, -5, 5, 5);
}

QJsonObject TextAnnotation::toJson() const
{
    QJsonObject json;
    const QPointF scenePosition = pos();
    json["type"] = "text";
    json["text"] = m_text;
    json["x"] = scenePosition.x();
    json["y"] = scenePosition.y();
    json["color"] = m_color.name();
    json["penWidth"] = m_penWidth;
    json["scale"] = m_scale;
    
    QJsonObject fontObj;
    fontObj["family"] = m_font.family();
    fontObj["size"] = m_font.pointSize();
    fontObj["bold"] = m_font.bold();
    fontObj["italic"] = m_font.italic();
    json["font"] = fontObj;
    
    return json;
}

void TextAnnotation::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isSelected) {
        // Check if clicking on resize handle
        QFontMetrics fm(m_font);
        QRectF textRect = fm.boundingRect(m_text);
        textRect.adjust(-2, -2, 2, 2);
        QRectF handleRect(textRect.right() - 8, textRect.bottom() - 8, 8, 8);
        
        if (handleRect.contains(event->pos())) {
            m_isResizing = true;
            m_resizeStartPos = event->pos();
            m_resizeStartScale = m_scale;
            event->accept();
            return;
        }
    }
    
    // Let base class handle movement
    QGraphicsItem::mousePressEvent(event);
}

void TextAnnotation::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_isResizing) {
        // Calculate scale based on drag distance
        QPointF delta = event->pos() - m_resizeStartPos;
        double distance = std::sqrt(delta.x() * delta.x() + delta.y() * delta.y());
        double scaleFactor = 1.0 + (distance / 50.0); // Adjust sensitivity
        
        // Apply scale relative to start - NO LIMIT on scale range
        double newScale = m_resizeStartScale * scaleFactor;
        newScale = qMax(0.1, newScale); // Only prevent scale from going too small
        
        setScale(newScale);
        event->accept();
        return;
    }
    
    // Let base class handle movement
    QGraphicsItem::mouseMoveEvent(event);
}

void TextAnnotation::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (m_isResizing) {
        m_isResizing = false;
        event->accept();
        return;
    }
    
    // Let base class handle
    QGraphicsItem::mouseReleaseEvent(event);
}

// ============================================================================
// FreehandAnnotation
// ============================================================================

FreehandAnnotation::FreehandAnnotation(QGraphicsItem* parent)
    : AnnotationItem(parent)
{
}

FreehandAnnotation::FreehandAnnotation(const QPainterPath& path, QGraphicsItem* parent)
    : AnnotationItem(parent)
    , m_path(path)
{
}

void FreehandAnnotation::addPoint(const QPointF& point)
{
    prepareGeometryChange();
    if (m_path.elementCount() == 0) {
        m_path.moveTo(point);
    } else {
        m_path.lineTo(point);
    }
    update();
}

void FreehandAnnotation::finishPath()
{
    // Called when drawing is complete
    prepareGeometryChange();
}

void FreehandAnnotation::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(m_color, m_penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter->setPen(pen);
    painter->drawPath(m_path);
    
    // Selection indicator
    if (m_isSelected) {
        painter->setPen(QPen(Qt::yellow, 1, Qt::DashLine));
        painter->drawRect(boundingRect());
    }
}

QRectF FreehandAnnotation::boundingRect() const
{
    return m_path.boundingRect().adjusted(-m_penWidth, -m_penWidth, m_penWidth, m_penWidth);
}

QJsonObject FreehandAnnotation::toJson() const
{
    QJsonObject json;
    const QPointF offset = pos();
    json["type"] = "freehand";
    json["color"] = m_color.name();
    json["penWidth"] = m_penWidth;
    
    QJsonArray points;
    for (int i = 0; i < m_path.elementCount(); ++i) {
        QPainterPath::Element elem = m_path.elementAt(i);
        QJsonObject pt;
        pt["x"] = elem.x + offset.x();
        pt["y"] = elem.y + offset.y();
        points.append(pt);
    }
    json["points"] = points;
    
    return json;
}

// ============================================================================
// RectangleAnnotation
// ============================================================================

RectangleAnnotation::RectangleAnnotation(QGraphicsItem* parent)
    : AnnotationItem(parent)
    , m_rect(0, 0, 100, 100)
{
}

RectangleAnnotation::RectangleAnnotation(const QRectF& rect, QGraphicsItem* parent)
    : AnnotationItem(parent)
    , m_rect(rect)
{
}

void RectangleAnnotation::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(m_color, m_penWidth);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(m_rect);
    
    // Selection indicator
    if (m_isSelected) {
        painter->setPen(QPen(Qt::yellow, 1, Qt::DashLine));
        painter->drawRect(m_rect.adjusted(-2, -2, 2, 2));
    }
}

QRectF RectangleAnnotation::boundingRect() const
{
    return m_rect.adjusted(-m_penWidth, -m_penWidth, m_penWidth, m_penWidth);
}

QJsonObject RectangleAnnotation::toJson() const
{
    QJsonObject json;
    const QRectF rect = m_rect.translated(pos());
    json["type"] = "rectangle";
    json["x"] = rect.x();
    json["y"] = rect.y();
    json["width"] = rect.width();
    json["height"] = rect.height();
    json["color"] = m_color.name();
    json["penWidth"] = m_penWidth;
    return json;
}

// ============================================================================
// EllipseAnnotation
// ============================================================================

EllipseAnnotation::EllipseAnnotation(QGraphicsItem* parent)
    : AnnotationItem(parent)
    , m_rect(0, 0, 100, 100)
{
}

EllipseAnnotation::EllipseAnnotation(const QRectF& rect, QGraphicsItem* parent)
    : AnnotationItem(parent)
    , m_rect(rect)
{
}

void EllipseAnnotation::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(m_color, m_penWidth);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(m_rect);
    
    // Selection indicator
    if (m_isSelected) {
        painter->setPen(QPen(Qt::yellow, 1, Qt::DashLine));
        painter->drawRect(m_rect.adjusted(-2, -2, 2, 2));
    }
}

QRectF EllipseAnnotation::boundingRect() const
{
    return m_rect.adjusted(-m_penWidth, -m_penWidth, m_penWidth, m_penWidth);
}

QJsonObject EllipseAnnotation::toJson() const
{
    QJsonObject json;
    const QRectF rect = m_rect.translated(pos());
    json["type"] = "ellipse";
    json["x"] = rect.x();
    json["y"] = rect.y();
    json["width"] = rect.width();
    json["height"] = rect.height();
    json["color"] = m_color.name();
    json["penWidth"] = m_penWidth;
    return json;
}

// ============================================================================
// ArrowAnnotation
// ============================================================================

ArrowAnnotation::ArrowAnnotation(QGraphicsItem* parent)
    : AnnotationItem(parent)
    , m_line(0, 0, 100, 100)
    , m_arrowHeadSize(15.0)
{
}

ArrowAnnotation::ArrowAnnotation(const QLineF& line, QGraphicsItem* parent)
    : AnnotationItem(parent)
    , m_line(line)
    , m_arrowHeadSize(15.0)
{
}

QPolygonF ArrowAnnotation::calculateArrowHead() const
{
    QPolygonF arrowHead;
    
    // Calculate arrow head angle
    double angle = std::atan2(-m_line.dy(), m_line.dx());
    
    QPointF p1 = m_line.p2() - QPointF(
        std::sin(angle + M_PI / 3) * m_arrowHeadSize,
        std::cos(angle + M_PI / 3) * m_arrowHeadSize
    );
    
    QPointF p2 = m_line.p2() - QPointF(
        std::sin(angle + M_PI - M_PI / 3) * m_arrowHeadSize,
        std::cos(angle + M_PI - M_PI / 3) * m_arrowHeadSize
    );
    
    arrowHead << m_line.p2() << p1 << p2;
    return arrowHead;
}

void ArrowAnnotation::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(m_color, m_penWidth);
    painter->setPen(pen);
    painter->setBrush(m_color);
    
    // Draw line
    painter->drawLine(m_line);
    
    // Draw arrow head
    painter->drawPolygon(calculateArrowHead());
    
    // Selection indicator
    if (m_isSelected) {
        painter->setPen(QPen(Qt::yellow, 1, Qt::DashLine));
        painter->drawRect(boundingRect());
    }
}

QRectF ArrowAnnotation::boundingRect() const
{
    QRectF rect;
    rect.setCoords(
        qMin(m_line.x1(), m_line.x2()),
        qMin(m_line.y1(), m_line.y2()),
        qMax(m_line.x1(), m_line.x2()),
        qMax(m_line.y1(), m_line.y2())
    );
    return rect.adjusted(-m_arrowHeadSize - m_penWidth, 
                         -m_arrowHeadSize - m_penWidth,
                         m_arrowHeadSize + m_penWidth, 
                         m_arrowHeadSize + m_penWidth);
}

QJsonObject ArrowAnnotation::toJson() const
{
    QJsonObject json;
    const QPointF offset = pos();
    json["type"] = "arrow";
    json["x1"] = m_line.x1() + offset.x();
    json["y1"] = m_line.y1() + offset.y();
    json["x2"] = m_line.x2() + offset.x();
    json["y2"] = m_line.y2() + offset.y();
    json["color"] = m_color.name();
    json["penWidth"] = m_penWidth;
    json["arrowHeadSize"] = m_arrowHeadSize;
    return json;
}
