#ifndef PREVIEW_OVERLAY_H
#define PREVIEW_OVERLAY_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QAudioOutput>
#include <QKeyEvent>
#include <QTimer>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>

class PreviewOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewOverlay(QWidget *parent = nullptr);
    ~PreviewOverlay();

    void showAsset(const QString &filePath, const QString &fileName, const QString &fileType);
    void navigateNext();
    void navigatePrevious();

signals:
    void closed();
    void navigateRequested(int delta);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private slots:
    void onPlayPauseClicked();
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onSliderMoved(int position);
    void onVolumeChanged(int value);
    void hideControls();

private:
    void setupUi();
    void showImage(const QString &filePath);
    void showVideo(const QString &filePath);
    void updatePlayPauseButton();
    QString formatTime(qint64 milliseconds);
    void zoomImage(double factor);
    void fitImageToView();
    void resetImageZoom();

    // UI Components
    QGraphicsView *imageView;
    QGraphicsScene *imageScene;
    QGraphicsPixmapItem *imageItem;
    QVideoWidget *videoWidget;
    QWidget *controlsWidget;
    QPushButton *playPauseBtn;
    QSlider *positionSlider;
    QLabel *timeLabel;
    QSlider *volumeSlider;
    QPushButton *closeBtn;
    QLabel *fileNameLabel;

    // Media player
    QMediaPlayer *mediaPlayer;
    QAudioOutput *audioOutput;

    // State
    QString currentFilePath;
    QString currentFileType;
    bool isVideo;
    QTimer *controlsTimer;

    // Image zoom/pan state
    double currentZoom;
    QPixmap originalPixmap;
    QPoint lastPanPoint;
    bool isPanning;
};

#endif // PREVIEW_OVERLAY_H

