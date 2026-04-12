#include "image_preview_overlay.h"
#include "icon_utils.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCoreApplication>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QFileInfo>
#include <QPainter>
#include <QColorDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QGraphicsSceneMouseEvent>
#include <QCloseEvent>
#include <QDebug>

ImagePreviewOverlay::ImagePreviewOverlay(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    setFocusPolicy(Qt::StrongFocus);
}

ImagePreviewOverlay::~ImagePreviewOverlay()
{
    if (annotationLayer) {
        annotationLayer->clearAnnotations();
        delete annotationLayer;
        annotationLayer = nullptr;
    }
}

void ImagePreviewOverlay::setupUi()
{
    setStyleSheet("QWidget { background-color: #000000; }");
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    setWindowTitle(tr("Preview"));
    setAttribute(Qt::WA_DeleteOnClose, false);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QWidget *topBar = new QWidget(this);
    topBar->setStyleSheet("QWidget { background-color: rgba(0, 0, 0, 180); }");
    topBar->setFixedHeight(50);
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(10, 0, 10, 0);

    topLayout->addStretch();

    fileNameLabel = new QLabel(this);
    fileNameLabel->setStyleSheet("QLabel { color: white; font-size: 16px; padding: 10px; }");
    fileNameLabel->setAlignment(Qt::AlignCenter);
    fileNameLabel->setFocusPolicy(Qt::NoFocus);
    topLayout->addWidget(fileNameLabel);

    topLayout->addStretch();

    toggleAnnotationBtn = new QPushButton(this);
    toggleAnnotationBtn->setIcon(loadRawPngIcon(QStringLiteral("Annotation/Annotate.png")));
    toggleAnnotationBtn->setIconSize(QSize(24, 24));
    toggleAnnotationBtn->setText(" Annotate");
    toggleAnnotationBtn->setFocusPolicy(Qt::NoFocus);
    toggleAnnotationBtn->setCheckable(true);
    toggleAnnotationBtn->setStyleSheet(
        "QPushButton { background-color: #444; color: white; padding: 8px 15px; "
        "border: none; border-radius: 4px; }"
        "QPushButton:hover { background-color: #555; }"
        "QPushButton:checked { background-color: #58a6ff; }"
    );
    toggleAnnotationBtn->setToolTip("Toggle annotation mode (A)");
    connect(toggleAnnotationBtn, &QPushButton::clicked, this, &ImagePreviewOverlay::onToggleAnnotation);
    topLayout->addWidget(toggleAnnotationBtn);

    mainLayout->addWidget(topBar);

    imageView = new QGraphicsView(this);
    imageScene = new QGraphicsScene(this);
    imageView->setScene(imageScene);
    imageView->setStyleSheet("QGraphicsView { background-color: #000000; border: none; }");
    imageView->setRenderHints(QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
    imageView->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    imageView->setOptimizationFlags(QGraphicsView::DontSavePainterState);
    imageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    imageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    imageView->setDragMode(QGraphicsView::ScrollHandDrag);
    imageView->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    imageView->setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    imageView->setFocusPolicy(Qt::NoFocus);
    imageView->installEventFilter(this);
    imageView->viewport()->installEventFilter(this);
    mainLayout->addWidget(imageView, 1);

    annotationLayer = new AnnotationLayer(imageScene, this);
    annotationLayer->setParentWidget(this);

    setupAnnotationToolbar();
}

void ImagePreviewOverlay::showImage(const QString &filePath, const QString &fileName, const QString &fileType)
{
    Q_UNUSED(fileType);
    currentFilePath = filePath;
    if (fileNameLabel) {
        fileNameLabel->setText(fileName);
    }
    setWindowTitle(tr("Preview - %1").arg(fileName));

    annotationModeEnabled = false;
    if (toggleAnnotationBtn) {
        toggleAnnotationBtn->blockSignals(true);
        toggleAnnotationBtn->setChecked(false);
        toggleAnnotationBtn->blockSignals(false);
    }
    if (annotationToolbar) {
        annotationToolbar->hide();
    }
    if (annotationLayer) {
        annotationLayer->clearAnnotations();
        annotationLayer->setDrawMode(AnnotationLayer::None);
    }
    if (imageView) {
        imageView->setDragMode(QGraphicsView::ScrollHandDrag);
    }

    QImage image;
    QPixmap newPixmap;
    if (OIIOImageLoader::isOIIOSupported(filePath)) {
        image = OIIOImageLoader::loadImage(filePath, 0, 0, currentColorSpace);
        if (!image.isNull()) {
            newPixmap = QPixmap::fromImage(image);
        }
    }

    if (newPixmap.isNull()) {
        newPixmap = QPixmap(filePath);
    }

    if (newPixmap.isNull()) {
        qWarning() << "[ImagePreviewOverlay] Failed to load image:" << filePath;
        return;
    }

    if (imageItem) {
        imageItem->setPixmap(newPixmap);
    } else {
        imageScene->clear();
        imageItem = imageScene->addPixmap(newPixmap);
    }

    originalPixmap = newPixmap;
    imageScene->setSceneRect(newPixmap.rect());

    fitMode = true;
    currentZoom = 1.0;
    if (imageView) {
        imageView->resetTransform();
    }

    show();
    raise();
    setFocus();
    QTimer::singleShot(0, this, [this]() {
        if (fitMode) {
            fitImageToView();
        }
    });
}

void ImagePreviewOverlay::fitImageToView()
{
    if (!imageView || !imageItem) return;

    QRectF sceneRect = imageItem->boundingRect();
    if (sceneRect.isEmpty()) return;

    QRectF viewRect = imageView->viewport()->rect();
    if (viewRect.isEmpty()) return;

    double xRatio = viewRect.width() / sceneRect.width();
    double yRatio = viewRect.height() / sceneRect.height();
    const bool widthIsLargest = sceneRect.width() >= sceneRect.height();
    double majorRatio = widthIsLargest ? xRatio : yRatio;
    double minorRatio = widthIsLargest ? yRatio : xRatio;
    currentZoom = qMin(majorRatio, minorRatio);

    imageView->resetTransform();
    imageView->scale(currentZoom, currentZoom);
    imageView->centerOn(imageItem);
    imageView->viewport()->update();
}

void ImagePreviewOverlay::resetZoom()
{
    if (!imageView || !imageItem) return;
    currentZoom = 1.0;
    imageView->resetTransform();
    imageView->centerOn(imageItem);
    imageView->viewport()->update();
}

void ImagePreviewOverlay::zoomImage(double factor)
{
    if (!imageView || !imageItem) return;
    fitMode = false;
    currentZoom *= factor;
    imageView->resetTransform();
    imageView->scale(currentZoom, currentZoom);
    imageView->centerOn(imageItem);
    imageView->viewport()->update();
}

void ImagePreviewOverlay::toggleFitMode()
{
    if (!imageView || !imageItem) return;
    if (fitMode) {
        resetZoom();
        fitMode = false;
    } else {
        fitMode = true;
        fitImageToView();
    }
}

void ImagePreviewOverlay::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
        case Qt::Key_Escape:
            emit closed();
            break;
        case Qt::Key_Left:
            emit navigateRequested(-1);
            break;
        case Qt::Key_Right:
            emit navigateRequested(1);
            break;
        case Qt::Key_Space:
            emit closed();
            break;
        default:
            QWidget::keyPressEvent(event);
    }
}

void ImagePreviewOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (fitMode) {
        fitImageToView();
    }
}

void ImagePreviewOverlay::wheelEvent(QWheelEvent *event)
{
    if (!originalPixmap.isNull()) {
        double factor = event->angleDelta().y() > 0 ? 1.15 : 0.85;
        zoomImage(factor);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void ImagePreviewOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        resetZoom();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ImagePreviewOverlay::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        toggleFitMode();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ImagePreviewOverlay::closeEvent(QCloseEvent *event)
{
    emit closed();
    event->accept();
}

bool ImagePreviewOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (annotationModeEnabled && (watched == imageView || (imageView && watched == imageView->viewport()))) {
        if (annotationLayer->currentMode() == AnnotationLayer::Select) {
            return false;
        }

        if (event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseMove ||
            event->type() == QEvent::MouseButtonRelease) {

            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if (event->type() == QEvent::MouseButtonPress && mouseEvent->button() != Qt::LeftButton) {
                return false;
            }
            if (event->type() == QEvent::MouseMove && !(mouseEvent->buttons() & Qt::LeftButton)) {
                return false;
            }

            QPointF scenePos = imageView->mapToScene(mouseEvent->pos());

            QGraphicsSceneMouseEvent sceneMouseEvent(
                event->type() == QEvent::MouseButtonPress ? QEvent::GraphicsSceneMousePress :
                event->type() == QEvent::MouseMove ? QEvent::GraphicsSceneMouseMove :
                QEvent::GraphicsSceneMouseRelease
            );
            sceneMouseEvent.setScenePos(scenePos);
            sceneMouseEvent.setButton(mouseEvent->button());
            sceneMouseEvent.setButtons(mouseEvent->buttons());
            sceneMouseEvent.setModifiers(mouseEvent->modifiers());

            if (event->type() == QEvent::MouseButtonPress) {
                annotationLayer->handleMousePress(&sceneMouseEvent);
            } else if (event->type() == QEvent::MouseMove) {
                annotationLayer->handleMouseMove(&sceneMouseEvent);
            } else if (event->type() == QEvent::MouseButtonRelease) {
                annotationLayer->handleMouseRelease(&sceneMouseEvent);
            }
            return true;
        }
    }

    if ((watched == imageView || (imageView && watched == imageView->viewport())) && event->type() == QEvent::KeyPress) {
        keyPressEvent(static_cast<QKeyEvent*>(event));
        return true;
    }

    if ((watched == imageView || (imageView && watched == imageView->viewport())) && event->type() == QEvent::Wheel) {
        if (!originalPixmap.isNull()) {
            QWheelEvent *wheel = static_cast<QWheelEvent*>(event);
            double factor = wheel->angleDelta().y() > 0 ? 1.15 : 0.85;
            zoomImage(factor);
            wheel->accept();
            return true;
        }
    }

    if ((watched == imageView || (imageView && watched == imageView->viewport())) &&
        event->type() == QEvent::MouseButtonDblClick) {
        toggleFitMode();
        return true;
    }

    return QWidget::eventFilter(watched, event);
}

void ImagePreviewOverlay::setupAnnotationToolbar()
{
    annotationToolbar = new QWidget(this);
    annotationToolbar->setStyleSheet("QWidget { background-color: rgba(0, 0, 0, 180); }");
    annotationToolbar->setFixedHeight(60);
    annotationToolbar->hide();

    QHBoxLayout *toolbarLayout = new QHBoxLayout(annotationToolbar);
    toolbarLayout->setContentsMargins(10, 5, 10, 5);
    toolbarLayout->setSpacing(10);

    QString buttonStyle = "QPushButton { background-color: #444; border: none; border-radius: 4px; "
                         "padding: 6px; min-width: 36px; min-height: 36px; }"
                         "QPushButton:hover { background-color: #555; }"
                         "QPushButton:checked { background-color: #58a6ff; }";

    QPushButton *selectToolBtn = new QPushButton(annotationToolbar);
    selectToolBtn->setIcon(loadRawPngIcon(QStringLiteral("Annotation/cursor.png")));
    selectToolBtn->setIconSize(QSize(24, 24));
    selectToolBtn->setCheckable(true);
    selectToolBtn->setFocusPolicy(Qt::NoFocus);
    selectToolBtn->setChecked(true);
    selectToolBtn->setStyleSheet(buttonStyle);
    selectToolBtn->setToolTip("Select and move annotations");
    connect(selectToolBtn, &QPushButton::clicked, this, [this, selectToolBtn]() {
        if (penToolBtn) penToolBtn->setChecked(false);
        if (textToolBtn) textToolBtn->setChecked(false);
        if (rectangleToolBtn) rectangleToolBtn->setChecked(false);
        if (ellipseToolBtn) ellipseToolBtn->setChecked(false);
        if (arrowToolBtn) arrowToolBtn->setChecked(false);
        selectToolBtn->setChecked(true);
        annotationLayer->setDrawMode(AnnotationLayer::Select);
    });
    toolbarLayout->addWidget(selectToolBtn);

    penToolBtn = new QPushButton(annotationToolbar);
    penToolBtn->setIcon(loadRawPngIcon(QStringLiteral("Annotation/paint.png")));
    penToolBtn->setIconSize(QSize(24, 24));
    penToolBtn->setCheckable(true);
    penToolBtn->setFocusPolicy(Qt::NoFocus);
    penToolBtn->setStyleSheet(buttonStyle);
    penToolBtn->setToolTip("Freehand drawing");
    connect(penToolBtn, &QPushButton::clicked, this, &ImagePreviewOverlay::onAnnotationToolSelected);
    toolbarLayout->addWidget(penToolBtn);

    textToolBtn = new QPushButton(annotationToolbar);
    textToolBtn->setIcon(loadRawPngIcon(QStringLiteral("Annotation/text.png")));
    textToolBtn->setIconSize(QSize(24, 24));
    textToolBtn->setCheckable(true);
    textToolBtn->setFocusPolicy(Qt::NoFocus);
    textToolBtn->setStyleSheet(buttonStyle);
    textToolBtn->setToolTip("Add text annotation");
    connect(textToolBtn, &QPushButton::clicked, this, &ImagePreviewOverlay::onAnnotationToolSelected);
    toolbarLayout->addWidget(textToolBtn);

    rectangleToolBtn = new QPushButton(annotationToolbar);
    rectangleToolBtn->setIcon(loadRawPngIcon(QStringLiteral("Annotation/Rectangle.png")));
    rectangleToolBtn->setIconSize(QSize(24, 24));
    rectangleToolBtn->setCheckable(true);
    rectangleToolBtn->setFocusPolicy(Qt::NoFocus);
    rectangleToolBtn->setStyleSheet(buttonStyle);
    rectangleToolBtn->setToolTip("Draw rectangle");
    connect(rectangleToolBtn, &QPushButton::clicked, this, &ImagePreviewOverlay::onAnnotationToolSelected);
    toolbarLayout->addWidget(rectangleToolBtn);

    ellipseToolBtn = new QPushButton(annotationToolbar);
    ellipseToolBtn->setIcon(loadRawPngIcon(QStringLiteral("Annotation/circle.png")));
    ellipseToolBtn->setIconSize(QSize(24, 24));
    ellipseToolBtn->setCheckable(true);
    ellipseToolBtn->setFocusPolicy(Qt::NoFocus);
    ellipseToolBtn->setStyleSheet(buttonStyle);
    ellipseToolBtn->setToolTip("Draw circle/ellipse");
    connect(ellipseToolBtn, &QPushButton::clicked, this, &ImagePreviewOverlay::onAnnotationToolSelected);
    toolbarLayout->addWidget(ellipseToolBtn);

    arrowToolBtn = new QPushButton(annotationToolbar);
    arrowToolBtn->setIcon(loadRawPngIcon(QStringLiteral("Annotation/arrow.png")));
    arrowToolBtn->setIconSize(QSize(24, 24));
    arrowToolBtn->setCheckable(true);
    arrowToolBtn->setFocusPolicy(Qt::NoFocus);
    arrowToolBtn->setStyleSheet(buttonStyle);
    arrowToolBtn->setToolTip("Draw arrow");
    connect(arrowToolBtn, &QPushButton::clicked, this, &ImagePreviewOverlay::onAnnotationToolSelected);
    toolbarLayout->addWidget(arrowToolBtn);

    toolbarLayout->addSpacing(20);

    colorPickerBtn = new QPushButton(annotationToolbar);
    colorPickerBtn->setFixedSize(36, 36);
    colorPickerBtn->setFocusPolicy(Qt::NoFocus);
    colorPickerBtn->setStyleSheet(QString("QPushButton { background-color: %1; border: 2px solid #fff; "
                                           "border-radius: 4px; }"
                                           "QPushButton:hover { border: 2px solid #58a6ff; }")
                                           .arg(annotationLayer->currentColor().name()));
    colorPickerBtn->setToolTip("Change annotation color");
    connect(colorPickerBtn, &QPushButton::clicked, this, &ImagePreviewOverlay::onColorPicker);
    toolbarLayout->addWidget(colorPickerBtn);

    QLabel *widthLabel = new QLabel("Width:", annotationToolbar);
    widthLabel->setStyleSheet("QLabel { color: white; }");
    toolbarLayout->addWidget(widthLabel);

    penWidthSlider = new QSlider(Qt::Horizontal, annotationToolbar);
    penWidthSlider->setFocusPolicy(Qt::NoFocus);
    penWidthSlider->setRange(1, 20);
    penWidthSlider->setValue(3);
    penWidthSlider->setFixedWidth(100);
    penWidthSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #555; height: 4px; }"
        "QSlider::handle:horizontal { background: #58a6ff; width: 12px; margin: -4px 0; border-radius: 6px; }"
    );
    connect(penWidthSlider, &QSlider::valueChanged, this, &ImagePreviewOverlay::onPenWidthChanged);
    toolbarLayout->addWidget(penWidthSlider);

    toolbarLayout->addStretch();

    undoBtn = new QPushButton(annotationToolbar);
    undoBtn->setIcon(loadRawPngIcon(QStringLiteral("Annotation/undo.png")));
    undoBtn->setIconSize(QSize(24, 24));
    undoBtn->setStyleSheet(buttonStyle);
    undoBtn->setFocusPolicy(Qt::NoFocus);
    undoBtn->setToolTip("Undo (Ctrl+Z)");
    connect(undoBtn, &QPushButton::clicked, annotationLayer->undoStack(), &QUndoStack::undo);
    toolbarLayout->addWidget(undoBtn);

    redoBtn = new QPushButton(annotationToolbar);
    redoBtn->setIcon(loadRawPngIcon(QStringLiteral("Annotation/redo.png")));
    redoBtn->setIconSize(QSize(24, 24));
    redoBtn->setStyleSheet(buttonStyle);
    redoBtn->setFocusPolicy(Qt::NoFocus);
    redoBtn->setToolTip("Redo (Ctrl+Y)");
    connect(redoBtn, &QPushButton::clicked, annotationLayer->undoStack(), &QUndoStack::redo);
    toolbarLayout->addWidget(redoBtn);

    clearAnnotationsBtn = new QPushButton(annotationToolbar);
    clearAnnotationsBtn->setIcon(loadRawPngIcon(QStringLiteral("Annotation/clear.png")));
    clearAnnotationsBtn->setIconSize(QSize(24, 24));
    clearAnnotationsBtn->setStyleSheet(buttonStyle);
    clearAnnotationsBtn->setFocusPolicy(Qt::NoFocus);
    clearAnnotationsBtn->setToolTip("Clear annotations");
    connect(clearAnnotationsBtn, &QPushButton::clicked, this, &ImagePreviewOverlay::onClearAnnotations);
    toolbarLayout->addWidget(clearAnnotationsBtn);

    saveFrameBtn = new QPushButton(annotationToolbar);
    saveFrameBtn->setIcon(loadRawPngIcon(QStringLiteral("Annotation/save.png")));
    saveFrameBtn->setIconSize(QSize(24, 24));
    saveFrameBtn->setStyleSheet(buttonStyle);
    saveFrameBtn->setFocusPolicy(Qt::NoFocus);
    saveFrameBtn->setToolTip("Save annotated image (Ctrl+S)");
    connect(saveFrameBtn, &QPushButton::clicked, this, &ImagePreviewOverlay::onSaveAnnotatedFrame);
    toolbarLayout->addWidget(saveFrameBtn);

    QVBoxLayout *mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (mainLayout) {
        mainLayout->insertWidget(1, annotationToolbar);
    }
}

void ImagePreviewOverlay::enableAnnotationMode(bool enable)
{
    annotationModeEnabled = enable;

    if (annotationToolbar) {
        annotationToolbar->setVisible(enable);
    }

    if (annotationLayer) {
        annotationLayer->setDrawMode(enable ? AnnotationLayer::Select : AnnotationLayer::None);
    }

    if (imageView) {
        imageView->setDragMode(enable ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
        imageView->setMouseTracking(enable);
        imageView->viewport()->setMouseTracking(enable);
    }
}

void ImagePreviewOverlay::onToggleAnnotation()
{
    enableAnnotationMode(toggleAnnotationBtn->isChecked());
}

void ImagePreviewOverlay::onAnnotationToolSelected()
{
    if (penToolBtn) penToolBtn->setChecked(false);
    if (textToolBtn) textToolBtn->setChecked(false);
    if (rectangleToolBtn) rectangleToolBtn->setChecked(false);
    if (ellipseToolBtn) ellipseToolBtn->setChecked(false);
    if (arrowToolBtn) arrowToolBtn->setChecked(false);

    QPushButton *clicked = qobject_cast<QPushButton*>(sender());
    if (clicked) {
        clicked->setChecked(true);
        if (clicked == penToolBtn) {
            annotationLayer->setDrawMode(AnnotationLayer::Pen);
        } else if (clicked == textToolBtn) {
            annotationLayer->setDrawMode(AnnotationLayer::Text);
        } else if (clicked == rectangleToolBtn) {
            annotationLayer->setDrawMode(AnnotationLayer::Rectangle);
        } else if (clicked == ellipseToolBtn) {
            annotationLayer->setDrawMode(AnnotationLayer::Ellipse);
        } else if (clicked == arrowToolBtn) {
            annotationLayer->setDrawMode(AnnotationLayer::Arrow);
        }
    }
}

void ImagePreviewOverlay::onColorPicker()
{
    QColor color = QColorDialog::getColor(annotationLayer->currentColor(), this, "Select Annotation Color");
    if (color.isValid()) {
        annotationLayer->setCurrentColor(color);
        if (colorPickerBtn) {
            colorPickerBtn->setStyleSheet(QString("QPushButton { background-color: %1; border: 2px solid #fff; "
                                                   "border-radius: 4px; }"
                                                   "QPushButton:hover { border: 2px solid #58a6ff; }")
                                                   .arg(color.name()));
        }
    }
}

void ImagePreviewOverlay::onPenWidthChanged(int width)
{
    annotationLayer->setCurrentPenWidth(width);
}

void ImagePreviewOverlay::onClearAnnotations()
{
    annotationLayer->clearAnnotations();
}

void ImagePreviewOverlay::onSaveAnnotatedFrame()
{
    if (originalPixmap.isNull()) {
        QMessageBox::warning(this, "Export Failed", "No image loaded.");
        return;
    }

    QString baseName = QFileInfo(currentFilePath).completeBaseName();
    QString defaultName = baseName + "_annotation.png";

    QString filePath = QFileDialog::getSaveFileName(this, "Save Annotated Image", defaultName,
                                                    "PNG Image (*.png);;JPEG Image (*.jpg *.jpeg)");
    if (filePath.isEmpty()) return;

    QFileInfo fi(filePath);
    QString format = fi.suffix().toLower();
    if (format != "png" && format != "jpg" && format != "jpeg") {
        format = "png";
    }

    exportAnnotatedFrame(filePath, format);
}

QImage ImagePreviewOverlay::captureCurrentFrame() const
{
    return originalPixmap.toImage();
}

void ImagePreviewOverlay::exportAnnotatedFrame(const QString &filePath, const QString &format)
{
    QImage baseFrame = captureCurrentFrame();
    if (baseFrame.isNull()) {
        QMessageBox::warning(this, "Export Failed", "Could not capture current image.");
        return;
    }

    QPainter painter(&baseFrame);
    painter.setRenderHint(QPainter::Antialiasing);

    for (AnnotationItem *item : annotationLayer->annotations()) {
        painter.save();
        painter.translate(item->pos());
        item->paint(&painter, nullptr, nullptr);
        painter.restore();
    }

    painter.end();

    if (!baseFrame.save(filePath, format.toUpper().toUtf8().constData())) {
        QMessageBox::warning(this, "Export Failed", "Could not save file to " + filePath);
    }
}
