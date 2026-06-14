#include "player_lab_viewport.h"

#include "player_lab_player.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>


PlayerLabViewport::PlayerLabViewport(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(false);

    installEventFilter(this);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_presentationTimer = new QTimer(this);
    m_presentationTimer->setTimerType(Qt::PreciseTimer);
    connect(m_presentationTimer, &QTimer::timeout, this, &PlayerLabViewport::updateRasterFrame);
}

PlayerLabViewport::~PlayerLabViewport() = default;

void PlayerLabViewport::setPlayer(PlayerLabPlayer* player)
{
    if (m_player) {
        disconnect(m_player, nullptr, this, nullptr);
    }

    m_presentationTimer->stop();
    m_player = player;
    m_rasterFrame = QImage();

    if (m_player) {
        connect(m_player, &PlayerLabPlayer::mediaInfoReady, this, &PlayerLabViewport::onMediaLoaded);
        connect(m_player, &PlayerLabPlayer::playbackStateChanged, this, &PlayerLabViewport::syncPresentationTimer);
        connect(m_player, &PlayerLabPlayer::videoFramesChanged, this, [this]() {
            if (m_player && m_player->playbackState() != PlayerLabPlayer::PlaybackState::Playing) {
                updateRasterFrame();
            }
        });
    }

    updateRasterFrame();
    syncPresentationTimer();
    update();
}

void PlayerLabViewport::onMediaLoaded(const PlayerLabPlayer::MediaInfo& info)
{
    m_mediaFps = info.fps;
    m_measuredFpsTimer.start();
    m_measuredFpsFrames = 0;
    m_measuredFps = 0.0;
    syncPresentationTimer();
    updateRasterFrame();
    update();
}

void PlayerLabViewport::syncPresentationTimer()
{
    if (!m_player) {
        m_presentationTimer->stop();
        return;
    }

    if (m_player->playbackState() == PlayerLabPlayer::PlaybackState::Playing) {
        const double fps = m_mediaFps > 0.0 ? m_mediaFps : 24.0;
        const int intervalMs = std::max(1, static_cast<int>(1000.0 / fps));
        m_presentationTimer->start(intervalMs);
    } else {
        m_presentationTimer->stop();
    }
}

void PlayerLabViewport::updateRasterFrame()
{
    if (!m_player) {
        return;
    }

    const QImage frame = m_player->getCurrentFrame(size());
    if (frame.isNull()) {
        return;
    }

    m_rasterFrame = frame;
    ++m_presentationRevision;
    noteFrameRendered();
    if (m_player->playbackState() == PlayerLabPlayer::PlaybackState::Playing) {
        repaint();
    } else {
        update();
    }
}

void PlayerLabViewport::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    if (!m_rasterFrame.isNull()) {
        const QRect target = rasterImageRect();
        const bool playing = m_player &&
            m_player->playbackState() == PlayerLabPlayer::PlaybackState::Playing;
        painter.setRenderHint(QPainter::SmoothPixmapTransform, !playing);
        painter.drawImage(target, m_rasterFrame);
    }


}

QImage PlayerLabViewport::currentRasterFrameForTest()
{
    return m_rasterFrame;
}

QImage PlayerLabViewport::currentPresentedFrameForTest()
{
    return m_rasterFrame;
}

void PlayerLabViewport::setFrameView(bool enabled)
{
    Q_UNUSED(enabled);
}

bool PlayerLabViewport::frameViewEnabled() const
{
    return true;
}

void PlayerLabViewport::zoomRelative(double factor)
{
    Q_UNUSED(factor);
}

QRect PlayerLabViewport::displayedContentRect() const
{
    return contentsRect();
}

double PlayerLabViewport::fps() const
{
    return m_measuredFps;
}

size_t PlayerLabViewport::droppedFrames() const
{
    return 0;
}

void PlayerLabViewport::cancelPendingFrameRequests()
{
    // Raster presenter has no delayed GL still-frame retries; clear any
    // pending queued repaint by leaving current frame ownership intact.
}

void PlayerLabViewport::resetMeasuredFps()
{
    m_measuredFpsTimer.start();
    m_measuredFpsFrames = 0;
    m_measuredFps = 0.0;
}

void PlayerLabViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateRasterFrame();
}

QRect PlayerLabViewport::rasterImageRect() const
{
    if (m_rasterFrame.isNull()) {
        return rect();
    }

    const QSize frameSize = m_rasterFrame.size();
    if (frameSize.isEmpty()) {
        return rect();
    }

    const QSize target = frameSize.scaled(rect().size(), Qt::KeepAspectRatio);
    const int x = (width() - target.width()) / 2;
    const int y = (height() - target.height()) / 2;
    return QRect(x, y, target.width(), target.height());
}

void PlayerLabViewport::noteFrameRendered()
{
    ++m_measuredFpsFrames;
    const qint64 elapsedMs = m_measuredFpsTimer.elapsed();
    if (elapsedMs > 500 && m_measuredFpsFrames > 1) {
        const double elapsedSeconds = static_cast<double>(elapsedMs) / 1000.0;
        m_measuredFps = static_cast<double>(m_measuredFpsFrames) / elapsedSeconds;
        m_measuredFpsTimer.start();
        m_measuredFpsFrames = 0;
        emit fpsChanged(m_measuredFps);
    }
    emit frameRendered();
}

bool PlayerLabViewport::eventFilter(QObject* watched, QEvent* event)
{
    // Only treat events originating from this widget as owned.
    const bool ours = (watched == this);
    if (!ours) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        if (QWidget* overlay = parentWidget()) {
            QCoreApplication::sendEvent(overlay, event);
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            m_isPanning = false;
            unsetCursor();
            setFrameView(true);
            return true;
        }
        if (mouseEvent->button() == Qt::MiddleButton) {
            if (frameViewEnabled()) {
                setFrameView(false);
            }
            m_isPanning = true;
            m_lastPanPos = mouseEvent->pos();
            setCursor(Qt::ClosedHandCursor);
            return true;
        }
    } else if (event->type() == QEvent::Wheel) {
        auto* wheelEvent = static_cast<QWheelEvent*>(event);
        const double factor = wheelEvent->angleDelta().y() > 0 ? 1.15 : 0.85;
        zoomRelative(factor);
        return true;
    } else if (event->type() == QEvent::MouseMove) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (m_isPanning && (mouseEvent->buttons() & Qt::MiddleButton)) {
            m_lastPanPos = mouseEvent->pos();
            return true;
        }
    } else if (event->type() == QEvent::MouseButtonRelease) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (m_isPanning && mouseEvent->button() == Qt::MiddleButton) {
            m_isPanning = false;
            unsetCursor();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}
