#ifndef FFMPEG_MOV_VIEWPORT_H
#define FFMPEG_MOV_VIEWPORT_H

#include <QWidget>
#include <QImage>
#include <QPixmap>
#include <QRect>

class FFmpegMovPlayer;
class FFmpegMovViewport : public QWidget
{
    Q_OBJECT

public:
    explicit FFmpegMovViewport(QWidget* parent = nullptr);

    void setPlayer(FFmpegMovPlayer* player);
    FFmpegMovPlayer* player() const { return m_player; }

    void setFrameView(bool enabled);
    bool frameViewEnabled() const;
    void zoomRelative(double factor);
    QRect displayedContentRect() const;
    QImage currentFrameForTest() const;

signals:
    void frameRendered();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QRect imageRect() const;
    QSize sourceImageSize() const;
    void invalidateDisplayCache();
    void updateDisplayCache();

    FFmpegMovPlayer* m_player{nullptr};
    double m_zoom{1.0};
    QPixmap m_displayPixmap;
    QSize m_sourceFrameSize;
};

#endif
