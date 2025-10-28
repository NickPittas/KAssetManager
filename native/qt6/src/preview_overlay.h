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
#include <QThread>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QComboBox>
#include "oiio_image_loader.h"

class PreviewOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewOverlay(QWidget *parent = nullptr);
    ~PreviewOverlay();

    void showAsset(const QString &filePath, const QString &fileName, const QString &fileType);
    void showSequence(const QStringList &framePaths, const QString &sequenceName, int startFrame, int endFrame);
    void navigateNext();
    void navigatePrevious();
    void stopPlayback();

signals:
    void closed();
    void navigateRequested(int delta);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onPlayPauseClicked();
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onSliderMoved(int position);
    void onVolumeChanged(int value);
    void hideControls();
    void onSequenceTimerTick();
    void onColorSpaceChanged(int index);
    void onPlayerError(QMediaPlayer::Error error, const QString &errorString);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onFallbackFrameReady(const QImage &image, qint64 ptsMs);
    void onFallbackFinished();

private:
    void setupUi();
    void showImage(const QString &filePath);
    void showVideo(const QString &filePath);
    void updatePlayPauseButton();
    QString formatTime(qint64 milliseconds);
    void zoomImage(double factor);
    void fitImageToView();
    void resetImageZoom();
    void loadSequenceFrame(int frameIndex);
    void playSequence();
    void pauseSequence();
    void stopSequence();
    void startFallbackVideo(const QString &filePath);
    void stopFallbackVideo();

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
    QComboBox *colorSpaceCombo;
    QLabel *colorSpaceLabel;

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

    // Image sequence playback state
    bool isSequence;
    QStringList sequenceFramePaths;
    int currentSequenceFrame;
    int sequenceStartFrame;
    int sequenceEndFrame;
    QTimer *sequenceTimer;
    bool sequencePlaying;

    // Fallback (software) video playback state for unsupported codecs (e.g., PNG-in-MOV)
    bool usingFallbackVideo = false;
    class FallbackPngMovReader; // defined in .cpp
    FallbackPngMovReader* fallbackReader = nullptr;
    QThread* fallbackThread = nullptr;
    qint64 fallbackDurationMs = 0;
    double fallbackFps = 0.0;
    bool fallbackPaused = false;

    // Color space for HDR/EXR images
    OIIOImageLoader::ColorSpace currentColorSpace;
    bool isHDRImage;
};

#endif // PREVIEW_OVERLAY_H

