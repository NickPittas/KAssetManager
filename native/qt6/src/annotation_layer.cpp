#include "annotation_layer.h"
#include <QGraphicsSceneMouseEvent>
#include <QInputDialog>
#include <QDebug>
#include <QJsonArray>

// ============================================================================
// AnnotationLayer
// ============================================================================

AnnotationLayer::AnnotationLayer(QGraphicsScene* scene, QObject* parent)
    : QObject(parent)
    , m_scene(scene)
    , m_parentWidget(nullptr)
    , m_currentMode(None)
    , m_currentColor(Qt::red)
    , m_currentPenWidth(3)
    , m_selectedAnnotation(nullptr)
    , m_isDrawing(false)
    , m_currentDrawingItem(nullptr)
{
    m_undoStack = new QUndoStack(this);
}

AnnotationLayer::~AnnotationLayer()
{
    clearAnnotations();
}

void AnnotationLayer::setDrawMode(DrawMode mode)
{
    if (m_isDrawing) {
        cancelDrawing();
    }
    m_currentMode = mode;
    clearSelection();
}

void AnnotationLayer::setCurrentColor(const QColor& color)
{
    m_currentColor = color;
}

void AnnotationLayer::setCurrentPenWidth(int width)
{
    m_currentPenWidth = qBound(1, width, 20);
}

void AnnotationLayer::addAnnotation(AnnotationItem* item)
{
    if (!item || m_annotations.contains(item)) {
        return;
    }
    
    // Only set color/width if item doesn't have them (when loading from JSON, they're already set)
    // When creating new items in handleMousePress, they already have color/width set
    // So we should NOT override here
    
    m_annotations.append(item);
    
    // Only add to scene if not already in it
    if (!item->scene()) {
        m_scene->addItem(item);
    }
    
    item->setVisible(true);
    item->setZValue(1000); // Ensure annotations are on top
    m_scene->update(); // Force scene update
    
    emit annotationAdded(item);
}

void AnnotationLayer::removeAnnotation(AnnotationItem* item)
{
    if (!item || !m_annotations.contains(item)) {
        return;
    }
    
    if (m_selectedAnnotation == item) {
        m_selectedAnnotation = nullptr;
    }
    
    m_annotations.removeOne(item);
    m_scene->removeItem(item);
    
    emit annotationRemoved(item);
}

void AnnotationLayer::clearAnnotations()
{
    m_selectedAnnotation = nullptr;
    
    for (AnnotationItem* item : m_annotations) {
        m_scene->removeItem(item);
        delete item;
    }
    m_annotations.clear();
    
    emit annotationsCleared();
}

void AnnotationLayer::clearSelection()
{
    if (m_selectedAnnotation) {
        m_selectedAnnotation->setItemSelected(false);
        m_selectedAnnotation = nullptr;
        emit selectionChanged(nullptr);
    }
}

AnnotationItem* AnnotationLayer::selectedAnnotation() const
{
    return m_selectedAnnotation;
}

void AnnotationLayer::selectAnnotation(AnnotationItem* item)
{
    if (m_selectedAnnotation) {
        m_selectedAnnotation->setItemSelected(false);
    }
    
    m_selectedAnnotation = item;
    
    if (m_selectedAnnotation) {
        m_selectedAnnotation->setItemSelected(true);
    }
    
    emit selectionChanged(item);
}

void AnnotationLayer::handleMousePress(QGraphicsSceneMouseEvent* event)
{
    if (m_currentMode == None) {
        return;
    }
    
    m_isDrawing = true;
    m_drawStartPoint = event->scenePos();
    
    switch (m_currentMode) {
        case Pen:
            m_currentDrawingItem = new FreehandAnnotation();
            m_currentDrawingItem->setColor(m_currentColor);
            m_currentDrawingItem->setPenWidth(m_currentPenWidth);
            static_cast<FreehandAnnotation*>(m_currentDrawingItem)->addPoint(m_drawStartPoint);
            m_scene->addItem(m_currentDrawingItem);
            break;
            
        case Text: {
            // Show input dialog for text with proper parent
            bool ok;
            QString text = QInputDialog::getText(m_parentWidget, tr("Text Annotation"), 
                                                tr("Enter text:"), QLineEdit::Normal,
                                                "Text", &ok);
            if (ok && !text.isEmpty()) {
                m_currentDrawingItem = new TextAnnotation(text, m_drawStartPoint);
                m_currentDrawingItem->setColor(m_currentColor);
                m_currentDrawingItem->setPenWidth(m_currentPenWidth);
                m_scene->addItem(m_currentDrawingItem);
                finishDrawing();
            } else {
                m_isDrawing = false;
            }
            break;
        }
            
        case Rectangle:
            m_currentDrawingItem = new RectangleAnnotation(QRectF(m_drawStartPoint, m_drawStartPoint));
            m_currentDrawingItem->setColor(m_currentColor);
            m_currentDrawingItem->setPenWidth(m_currentPenWidth);
            m_scene->addItem(m_currentDrawingItem);
            break;
            
        case Ellipse:
            m_currentDrawingItem = new EllipseAnnotation(QRectF(m_drawStartPoint, m_drawStartPoint));
            m_currentDrawingItem->setColor(m_currentColor);
            m_currentDrawingItem->setPenWidth(m_currentPenWidth);
            m_scene->addItem(m_currentDrawingItem);
            break;
            
        case Arrow:
            m_currentDrawingItem = new ArrowAnnotation(QLineF(m_drawStartPoint, m_drawStartPoint));
            m_currentDrawingItem->setColor(m_currentColor);
            m_currentDrawingItem->setPenWidth(m_currentPenWidth);
            m_scene->addItem(m_currentDrawingItem);
            break;
            
        default:
            break;
    }
}

void AnnotationLayer::handleMouseMove(QGraphicsSceneMouseEvent* event)
{
    if (!m_isDrawing || !m_currentDrawingItem) {
        return;
    }
    
    updateDrawingItem(event->scenePos());
}

void AnnotationLayer::handleMouseRelease(QGraphicsSceneMouseEvent* event)
{
    Q_UNUSED(event);
    
    if (!m_isDrawing) {
        return;
    }
    
    if (m_currentMode != Text) {
        finishDrawing();
    }
}

void AnnotationLayer::updateDrawingItem(const QPointF& currentPoint)
{
    if (!m_currentDrawingItem) {
        return;
    }
    
    switch (m_currentMode) {
        case Pen:
            static_cast<FreehandAnnotation*>(m_currentDrawingItem)->addPoint(currentPoint);
            break;
            
        case Rectangle: {
            QRectF rect = QRectF(m_drawStartPoint, currentPoint).normalized();
            static_cast<RectangleAnnotation*>(m_currentDrawingItem)->setRect(rect);
            break;
        }
            
        case Ellipse: {
            QRectF rect = QRectF(m_drawStartPoint, currentPoint).normalized();
            static_cast<EllipseAnnotation*>(m_currentDrawingItem)->setRect(rect);
            break;
        }
            
        case Arrow: {
            QLineF line(m_drawStartPoint, currentPoint);
            static_cast<ArrowAnnotation*>(m_currentDrawingItem)->setLine(line);
            break;
        }
            
        default:
            break;
    }
}

void AnnotationLayer::finishDrawing()
{
    if (!m_currentDrawingItem) {
        m_isDrawing = false;
        return;
    }
    
    // Finalize the drawing item
    if (m_currentMode == Pen) {
        static_cast<FreehandAnnotation*>(m_currentDrawingItem)->finishPath();
    }
    
    // Add to annotations list via undo command
    m_undoStack->push(new AddAnnotationCommand(this, m_currentDrawingItem));
    
    m_currentDrawingItem = nullptr;
    m_isDrawing = false;
}

void AnnotationLayer::cancelDrawing()
{
    if (m_currentDrawingItem) {
        m_scene->removeItem(m_currentDrawingItem);
        delete m_currentDrawingItem;
        m_currentDrawingItem = nullptr;
    }
    m_isDrawing = false;
}

QJsonArray AnnotationLayer::serializeAnnotations() const
{
    QJsonArray array;
    for (AnnotationItem* item : m_annotations) {
        array.append(item->toJson());
    }
    return array;
}

void AnnotationLayer::deserializeAnnotations(const QJsonArray& data)
{
    clearAnnotations();
    
    for (const QJsonValue& value : data) {
        QJsonObject obj = value.toObject();
        AnnotationItem* item = AnnotationItem::fromJson(obj);
        if (item) {
            addAnnotation(item);
        }
    }
}

// ============================================================================
// AddAnnotationCommand
// ============================================================================

AddAnnotationCommand::AddAnnotationCommand(AnnotationLayer* layer, AnnotationItem* item, QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_layer(layer)
    , m_item(item)
    , m_ownsItem(false)
{
    setText(QObject::tr("Add Annotation"));
}

void AddAnnotationCommand::undo()
{
    m_layer->removeAnnotation(m_item);
    m_ownsItem = true;
}

void AddAnnotationCommand::redo()
{
    m_layer->addAnnotation(m_item);
    m_ownsItem = false;
}

// ============================================================================
// RemoveAnnotationCommand
// ============================================================================

RemoveAnnotationCommand::RemoveAnnotationCommand(AnnotationLayer* layer, AnnotationItem* item, QUndoCommand* parent)
    : QUndoCommand(parent)
    , m_layer(layer)
    , m_item(item)
    , m_ownsItem(false)
{
    setText(QObject::tr("Remove Annotation"));
}

RemoveAnnotationCommand::~RemoveAnnotationCommand()
{
    if (m_ownsItem) {
        delete m_item;
    }
}

void RemoveAnnotationCommand::undo()
{
    m_layer->addAnnotation(m_item);
    m_ownsItem = false;
}

void RemoveAnnotationCommand::redo()
{
    m_layer->removeAnnotation(m_item);
    m_ownsItem = true;
}
