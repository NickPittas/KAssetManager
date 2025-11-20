#ifndef ANNOTATION_LAYER_H
#define ANNOTATION_LAYER_H

#include <QObject>
#include <QWidget>
#include <QGraphicsScene>
#include <QList>
#include <QMap>
#include <QSet>
#include <QColor>
#include <QPointF>
#include <QUndoStack>
#include "annotation_items.h"

class QGraphicsSceneMouseEvent;

/**
 * @brief Manages annotation items and drawing interactions for PreviewOverlay
 * 
 * Handles creation, selection, and manipulation of annotation items.
 * Provides undo/redo support and per-frame annotation storage.
 */
class AnnotationLayer : public QObject
{
    Q_OBJECT

public:
    enum DrawMode {
        None,
        Select,     // Select and move existing annotations
        Pen,        // Freehand drawing
        Text,       // Text annotation
        Rectangle,  // Rectangle shape
        Ellipse,    // Ellipse/circle shape
        Arrow       // Arrow with head
    };

    explicit AnnotationLayer(QGraphicsScene* scene, QObject* parent = nullptr);
    ~AnnotationLayer();
    
    // Set parent widget for dialogs
    void setParentWidget(QWidget* widget) { m_parentWidget = widget; }
    
    // Change the scene where annotations are rendered
    void setScene(QGraphicsScene* scene) { m_scene = scene; }

    // Drawing mode
    DrawMode currentMode() const { return m_currentMode; }
    void setDrawMode(DrawMode mode);

    // Properties
    QColor currentColor() const { return m_currentColor; }
    void setCurrentColor(const QColor& color);
    
    int currentPenWidth() const { return m_currentPenWidth; }
    void setCurrentPenWidth(int width);

    // Annotation management
    void addAnnotation(AnnotationItem* item);
    void removeAnnotation(AnnotationItem* item);
    void clearAnnotations();
    QList<AnnotationItem*> annotations() const { return m_annotations; }
    int annotationCount() const { return m_annotations.size(); }

    // Selection
    void clearSelection();
    AnnotationItem* selectedAnnotation() const;
    void selectAnnotation(AnnotationItem* item);

    // Undo/Redo
    QUndoStack* undoStack() { return m_undoStack; }
    bool canUndo() const { return m_undoStack->canUndo(); }
    bool canRedo() const { return m_undoStack->canRedo(); }

    // Scene event handling
    void handleMousePress(QGraphicsSceneMouseEvent* event);
    void handleMouseMove(QGraphicsSceneMouseEvent* event);
    void handleMouseRelease(QGraphicsSceneMouseEvent* event);

    // Serialization
    QJsonArray serializeAnnotations() const;
    void deserializeAnnotations(const QJsonArray& data);

signals:
    void annotationAdded(AnnotationItem* item);
    void annotationRemoved(AnnotationItem* item);
    void annotationsCleared();
    void selectionChanged(AnnotationItem* item);

private:
    QGraphicsScene* m_scene;
    QWidget* m_parentWidget;
    DrawMode m_currentMode;
    QColor m_currentColor;
    int m_currentPenWidth;
    
    QList<AnnotationItem*> m_annotations;
    AnnotationItem* m_selectedAnnotation;
    
    // Drawing state for current item being created
    bool m_isDrawing;
    QPointF m_drawStartPoint;
    AnnotationItem* m_currentDrawingItem;
    
    // Undo/Redo
    QUndoStack* m_undoStack;
    
    // Helper methods
    void finishDrawing();
    void cancelDrawing();
    void updateDrawingItem(const QPointF& currentPoint);
};

/**
 * @brief Undo command for adding an annotation
 */
class AddAnnotationCommand : public QUndoCommand
{
public:
    AddAnnotationCommand(AnnotationLayer* layer, AnnotationItem* item, QUndoCommand* parent = nullptr);
    void undo() override;
    void redo() override;

private:
    AnnotationLayer* m_layer;
    AnnotationItem* m_item;
    bool m_ownsItem;
};

/**
 * @brief Undo command for removing an annotation
 */
class RemoveAnnotationCommand : public QUndoCommand
{
public:
    RemoveAnnotationCommand(AnnotationLayer* layer, AnnotationItem* item, QUndoCommand* parent = nullptr);
    ~RemoveAnnotationCommand();
    void undo() override;
    void redo() override;

private:
    AnnotationLayer* m_layer;
    AnnotationItem* m_item;
    bool m_ownsItem;
};

/**
 * @brief Per-frame annotation storage for sequences and videos
 */
struct FrameAnnotations
{
    int frameNumber;
    QString framePath;
    QList<AnnotationItem*> items;
    
    FrameAnnotations() : frameNumber(-1) {}
    explicit FrameAnnotations(int frame, const QString& path = QString())
        : frameNumber(frame), framePath(path) {}
    
    ~FrameAnnotations() {
        qDeleteAll(items);
    }
};

#endif // ANNOTATION_LAYER_H
