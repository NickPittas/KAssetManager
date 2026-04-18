#include "ffmpeg_mov_viewport.h"

#include <QPainter>

#include "ffmpeg_mov_player.h"

FFmpegMovViewport::FFmpegMovViewport(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);
}

void FFmpegMovViewport::setPlayer(FFmpegMovPlayer* player)
{
    if (m_player) {
        disconnect(m_player, nullptr, this, nullptr);
    }

    m_player = player;
    if (m_player) {
        connect(m_player, &FFmpegMovPlayer::frameUpdated, this, [this]() {
            update();
            emit frameRendered();
        });
        connect(m_player, &FFmpegMovPlayer::mediaInfoReady, this, [this](const media_player::MediaInfo&) {
            update();
        });
    }
    update();
}

void FFmpegMovViewport::setFrameView(bool enabled)
{
    if (enabled) {
        m_zoom = 1.0;
        update();
    }
}

bool FFmpegMovViewport::frameViewEnabled() const
{
    return qFuzzyCompare(m_zoom, 1.0);
}

void FFmpegMovViewport::zoomRelative(double factor)
{
    m_zoom = qBound(0.1, m_zoom * factor, 10.0);
    update();
}

QRect FFmpegMovViewport::displayedContentRect() const
{
    return imageRect();
}

QImage FFmpegMovViewport::currentFrameForTest() const
{
    return m_player ? m_player->currentFrameImage() : QImage();
}

void FFmpegMovViewport::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 17));

    if (!m_player) {
        return;
    }

    const QImage frame = m_player->currentFrameImage();
    if (frame.isNull()) {
        return;
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(imageRect(), frame);
}

QRect FFmpegMovViewport::imageRect() const
{
    const QRect contents = contentsRect();
    if (!m_player) {
        return contents;
    }

    const QImage frame = m_player->currentFrameImage();
    if (frame.isNull()) {
        return contents;
    }

    QSize drawSize = frame.size().scaled(contents.size(), Qt::KeepAspectRatio);
    if (!qFuzzyCompare(m_zoom, 1.0)) {
        drawSize = (drawSize * m_zoom).boundedTo(contents.size() * 10);
    }

    const QPoint topLeft(
        contents.x() + (contents.width() - drawSize.width()) / 2,
        contents.y() + (contents.height() - drawSize.height()) / 2);
    return QRect(topLeft, drawSize);
}
