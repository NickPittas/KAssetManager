#ifndef IMAGE_PREVIEW_OVERLAY_H
#define IMAGE_PREVIEW_OVERLAY_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QSlider>
#include <QKeyEvent>
#include <QTimer>
#include <QPixmap>

#include "annotation_layer.h"
#include "oiio_image_loader.h"

class ImagePreviewOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit ImagePreviewOverlay(QWidget *parent = nullptr);
    ~ImagePreviewOverlay();

    void showImage(const QString &filePath, const QString &fileName, const QString &fileType);
    void stopPlayback() {}
    QString currentPath() const { return currentFilePath; }

signals:
    void closed();
    void navigateRequested(int delta);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onToggleAnnotation();
    void onAnnotationToolSelected();
    void onColorPicker();
    void onPenWidthChanged(int width);
    void onClearAnnotations();
    void onSaveAnnotatedFrame();

private:
    void setupUi();
    void fitImageToView();
    void resetZoom();
    void zoomImage(double factor);
    void toggleFitMode();

    void setupAnnotationToolbar();
    void enableAnnotationMode(bool enable);
    QImage captureCurrentFrame() const;
    void exportAnnotatedFrame(const QString &filePath, const QString &format);

    QLabel *fileNameLabel = nullptr;
    QPushButton *toggleAnnotationBtn = nullptr;
    QGraphicsView *imageView = nullptr;
    QGraphicsScene *imageScene = nullptr;
    QGraphicsPixmapItem *imageItem = nullptr;

    QWidget *annotationToolbar = nullptr;
    QPushButton *penToolBtn = nullptr;
    QPushButton *textToolBtn = nullptr;
    QPushButton *rectangleToolBtn = nullptr;
    QPushButton *ellipseToolBtn = nullptr;
    QPushButton *arrowToolBtn = nullptr;
    QPushButton *colorPickerBtn = nullptr;
    QSlider *penWidthSlider = nullptr;
    QPushButton *clearAnnotationsBtn = nullptr;
    QPushButton *saveFrameBtn = nullptr;
    QPushButton *undoBtn = nullptr;
    QPushButton *redoBtn = nullptr;

    AnnotationLayer *annotationLayer = nullptr;
    bool annotationModeEnabled = false;

    QString currentFilePath;
    QPixmap originalPixmap;
    double currentZoom = 1.0;
    bool fitMode = true;

};

#endif // IMAGE_PREVIEW_OVERLAY_H
