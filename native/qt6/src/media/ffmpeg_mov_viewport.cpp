#include "ffmpeg_mov_viewport.h"

#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>

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
            invalidateDisplayCache();
            update();
            emit frameRendered();
        });
        connect(m_player, &FFmpegMovPlayer::mediaInfoReady, this, [this](const media_player::MediaInfo&) {
            invalidateDisplayCache();
            update();
        });
    }
    invalidateDisplayCache();
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

void FFmpegMovViewport::invalidateDisplayCache()
{
    m_displayPixmap = QPixmap();
    m_sourceFrameSize = QSize();
}

void FFmpegMovViewport::updateDisplayCache()
{
    if (!m_player) {
        invalidateDisplayCache();
        return;
    }

    const QImage frame = m_player->currentFrameImage();
    if (frame.isNull()) {
        invalidateDisplayCache();
        return;
    }

    const QRect targetRect = imageRect();
    if (!targetRect.isValid() || targetRect.isEmpty()) {
        invalidateDisplayCache();
        return;
    }

    if (!m_displayPixmap.isNull() && m_sourceFrameSize == frame.size() && m_displayPixmap.size() == targetRect.size()) {
        return;
    }

    const QImage scaled = frame.scaled(targetRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_displayPixmap = QPixmap::fromImage(scaled);
    m_sourceFrameSize = frame.size();
}

void FFmpegMovViewport::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 17));

    if (!m_player) {
        return;
    }

    updateDisplayCache();
    if (m_displayPixmap.isNull()) {
        return;
    }

    const QRect targetRect = imageRect();
    if (!targetRect.isValid() || targetRect.isEmpty()) {
        return;
    }

    painter.drawPixmap(targetRect, m_displayPixmap);
}

void FFmpegMovViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    invalidateDisplayCache();
}

QRect FFmpegMovViewport::imageRect() const
{
    const QRect contents = contentsRect();
    const QSize sourceSize = sourceImageSize();
    if (!sourceSize.isValid()) {
        return contents;
    }

    QSize drawSize = sourceSize.scaled(contents.size(), Qt::KeepAspectRatio);
    if (!qFuzzyCompare(m_zoom, 1.0)) {
        drawSize = (drawSize * m_zoom).boundedTo(contents.size() * 10);
    }

    const QPoint topLeft(
        contents.x() + (contents.width() - drawSize.width()) / 2,
        contents.y() + (contents.height() - drawSize.height()) / 2);
    return QRect(topLeft, drawSize);
}

QSize FFmpegMovViewport::sourceImageSize() const
{
    return m_player ? m_player->currentFrameSize() : QSize();
}
