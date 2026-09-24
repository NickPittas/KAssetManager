#ifndef PLAYER_LAB_VIEWPORT_H
#define PLAYER_LAB_VIEWPORT_H

#include <QWidget>
#include <QVBoxLayout>
#include <QPoint>
#include <QImage>
#include <QTimer>
#include <QElapsedTimer>

#include "player_lab_player.h"

/**
 * @brief Wayland raster video viewport backed by PlayerLabPlayer/FFmpegMovPlayer frames.
 *
 * The class name is retained for existing UI call sites. Frames are pulled from
 * PlayerLabPlayer::getCurrentFrame() and presented with QPainter on a QTimer-driven
 * raster path (no GL widget).
 */
class PlayerLabViewport : public QWidget
{
    Q_OBJECT

public:
    explicit PlayerLabViewport(QWidget* parent = nullptr);
    ~PlayerLabViewport() override;

    void setPlayer(PlayerLabPlayer* player);
    PlayerLabPlayer* player() const { return m_player; }

    void setFrameView(bool enabled);
    bool frameViewEnabled() const;
    void zoomRelative(double factor);
    QRect displayedContentRect() const;
    double fps() const;
    size_t droppedFrames() const;

    void cancelPendingFrameRequests();
    void resetMeasuredFps();
    QImage currentRasterFrameForTest();
    QImage currentPresentedFrameForTest();
    qint64 rasterPresentationRevisionForTest() const { return m_presentationRevision; }

signals:
    void fpsChanged(double fps);
    void frameRendered();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onMediaLoaded(const PlayerLabPlayer::MediaInfo& info);
    void updateRasterFrame();

private:
    void noteFrameRendered();
    void syncPresentationTimer();
    QRect rasterImageRect() const;

    QVBoxLayout* m_layout{nullptr};
    PlayerLabPlayer* m_player{nullptr};
    QTimer* m_presentationTimer{nullptr};
    QImage m_rasterFrame;
    bool m_isPanning{false};
    QPoint m_lastPanPos;
    double m_waylandZoom{1.0};
    qint64 m_presentationRevision{0};
    double m_mediaFps{0.0};
    QElapsedTimer m_measuredFpsTimer;
    int m_measuredFpsFrames{0};
    double m_measuredFps{0.0};
};

#endif // PLAYER_LAB_VIEWPORT_H
