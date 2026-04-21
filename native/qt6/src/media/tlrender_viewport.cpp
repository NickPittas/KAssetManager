/**
 * TLRenderViewport - Native tlRender viewport wrapper implementation
 */

#include "tlrender_viewport.h"
#include "ffmpeg_mov_player.h"
#include "tlrender_player.h"
#include "platform_session.h"
#include <QDebug>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QWindow>
#include <QTimer>

#ifdef HAVE_TLRENDER
#include <tlRender/Timeline/Player.h>
#include <tlRender/Timeline/ColorOptions.h>
#include <tlRender/Timeline/BackgroundOptions.h>
#endif

TLRenderViewport::TLRenderViewport(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);

    // Create layout to hold the viewport
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_presentationTimer = new QTimer(this);
    m_presentationTimer->setTimerType(Qt::PreciseTimer);
    connect(m_presentationTimer, &QTimer::timeout, this, &TLRenderViewport::updateRasterFrame);

#ifdef HAVE_TLRENDER
    if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        setAttribute(Qt::WA_OpaquePaintEvent, true);
        setAutoFillBackground(false);
        installEventFilter(this);
    }
#else
    qWarning() << "[TLRenderViewport] tlRender not available";
#endif
}

TLRenderViewport::~TLRenderViewport()
{
    // No cleanup needed - viewport and player objects are owned elsewhere
}

void TLRenderViewport::prepareForMpvPlayback()
{
    return;
}

void TLRenderViewport::ensureMpvViewport()
{
    if (m_mpvViewport) {
        return;
    }

    m_mpvViewport = new MpvViewport(this);
    m_mpvViewport->hide();
    m_mpvViewport->setFocusPolicy(Qt::StrongFocus);
    m_mpvViewport->installEventFilter(this);
    m_layout->insertWidget(0, m_mpvViewport);
    connect(m_mpvViewport, &MpvViewport::frameRendered, this, [this]() {
        noteFrameRendered();
    });
}

void TLRenderViewport::ensureFfmpegMovViewport()
{
    if (m_ffmpegMovViewport) {
        return;
    }

    m_ffmpegMovViewport = new FFmpegMovViewport(this);
    m_ffmpegMovViewport->hide();
    m_ffmpegMovViewport->setFocusPolicy(Qt::StrongFocus);
    m_ffmpegMovViewport->installEventFilter(this);
    m_layout->insertWidget(0, m_ffmpegMovViewport);
    connect(m_ffmpegMovViewport, &FFmpegMovViewport::frameRendered, this, [this]() {
        noteFrameRendered();
    });
}

void TLRenderViewport::noteFrameRendered()
{
    ++m_presentationRevision;
    ++m_measuredFpsFrames;
    if (!m_measuredFpsTimer.isValid()) {
        m_measuredFpsTimer.start();
    } else {
        const qint64 elapsed = m_measuredFpsTimer.elapsed();
        if (elapsed >= 500) {
            m_measuredFps = (m_measuredFpsFrames * 1000.0) / qMax<qint64>(1, elapsed);
            emit fpsChanged(m_measuredFps);
            m_measuredFpsFrames = 0;
            m_measuredFpsTimer.restart();
        }
    }
    emit frameRendered();
}

void TLRenderViewport::setPlayer(TLRenderPlayer* player)
{
    // Disconnect from previous player
    if (m_player) {
        disconnect(m_player, nullptr, this, nullptr);
        if (m_player->mpvPlayer()) {
            disconnect(m_player->mpvPlayer(), nullptr, this, nullptr);
        }
    }

    if (m_presentationTimer) {
        m_presentationTimer->stop();
    }

    m_player = player;

    if (m_mpvViewport) {
        m_mpvViewport->setPlayer(player ? player->mpvPlayer() : nullptr);
    }
    if (m_ffmpegMovViewport) {
        m_ffmpegMovViewport->setPlayer(player ? player->ffmpegMovPlayer() : nullptr);
    }

#ifdef HAVE_TLRENDER
    if (player) {
        if (auto* mpvPlayer = player->mpvPlayer()) {
            connect(mpvPlayer, &MpvPlayer::viewportReadyChanged, this, [this](bool ready) {
                syncBackendWidgetVisibility();
                syncPresentationTimer();
                if (ready && m_player && m_player->mpvPlayer() && m_player->mpvPlayer()->hasMedia()) {
                    prepareForMpvPlayback();
                }
            });
        }
        // Connect to media loaded signal to setup viewport when player has content
        connect(player, &TLRenderPlayer::mediaInfoReady, this, &TLRenderViewport::onMediaLoaded);
        connect(player, &TLRenderPlayer::playbackStateChanged, this, [this](TLRenderPlayer::PlaybackState) {
            syncPresentationTimer();
        });
        
        connect(player, &TLRenderPlayer::videoFramesChanged, this, [this]() {
            if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland() && m_player) {
                if (m_player->playbackState() != TLRenderPlayer::PlaybackState::Playing) {
                    updateRasterFrame();
                }
            }
        });
        
        // If player already has media loaded, setup now
        if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
            updateRasterFrame();
            syncPresentationTimer();
        } else if (player->playerObject()) {
            setupViewportPlayer();
        } else if (m_viewport) {
            // Clear viewport until media is loaded
            m_viewport->setPlayer(QSharedPointer<tl::qt::PlayerObject>());
            qDebug() << "[TLRenderViewport] Player set, waiting for media load";
        }
    } else if (m_viewport) {
        m_viewport->setPlayer(QSharedPointer<tl::qt::PlayerObject>());
        qDebug() << "[TLRenderViewport] Player cleared from viewport";
    }
#else
    Q_UNUSED(player)
#endif

    syncBackendWidgetVisibility();
    if (useFfmpegMovViewport()) {
        if (m_ffmpegMovViewport) {
            m_ffmpegMovViewport->update();
        }
        syncPresentationTimer();
        return;
    }
    if (useMpvViewport()) {
        syncPresentationTimer();
        return;
    }
}

void TLRenderViewport::onMediaLoaded(const TLRenderPlayer::MediaInfo& info)
{
    m_mediaFps = info.fps > 0.0 ? info.fps : 24.0;
    syncBackendWidgetVisibility();
    if (useFfmpegMovViewport()) {
        syncPresentationTimer();
        return;
    }
    if (useMpvViewport()) {
        prepareForMpvPlayback();
        syncPresentationTimer();
        return;
    }
#ifdef HAVE_TLRENDER
    if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        updateRasterFrame();
        syncPresentationTimer();
    } else {
        qDebug() << "[TLRenderViewport] onMediaLoaded slot called, setting up viewport player";
        setupViewportPlayer();
    }
#endif
}

void TLRenderViewport::syncBackendWidgetVisibility()
{
    const bool mpvActive = useMpvViewport();
    const bool ffmpegMovActive = useFfmpegMovViewport();
    if (mpvActive) {
        ensureMpvViewport();
        m_mpvViewport->setPlayer(m_player ? m_player->mpvPlayer() : nullptr);
    }
    if (ffmpegMovActive) {
        ensureFfmpegMovViewport();
        m_ffmpegMovViewport->setPlayer(m_player ? m_player->ffmpegMovPlayer() : nullptr);
    }
    if (m_mpvViewport) {
        m_mpvViewport->setVisible(mpvActive);
    }
    if (m_ffmpegMovViewport) {
        m_ffmpegMovViewport->setVisible(ffmpegMovActive);
    }
    if ((mpvActive || ffmpegMovActive) && m_presentationTimer) {
        m_presentationTimer->stop();
    }
#ifdef HAVE_TLRENDER
    if (m_viewport) {
        m_viewport->setVisible(!mpvActive && !ffmpegMovActive);
    }
#endif
}

void TLRenderViewport::syncPresentationTimer()
{
    if (useFfmpegMovViewport()) {
        if (m_presentationTimer) {
            m_presentationTimer->stop();
        }
        return;
    }
    if (useMpvViewport()) {
        if (m_presentationTimer) {
            m_presentationTimer->stop();
        }
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_presentationTimer) {
        return;
    }
    if (!m_player || !PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        m_presentationTimer->stop();
        return;
    }
    if (m_player->mpvPlayer() && m_player->mpvPlayer()->hasMedia()) {
        m_presentationTimer->stop();
        return;
    }
    if (m_player->playbackState() != TLRenderPlayer::PlaybackState::Playing) {
        m_presentationTimer->stop();
        return;
    }

    const double fps = m_mediaFps > 0.0 ? m_mediaFps : 24.0;
    const int intervalMs = std::max(1, qRound(1000.0 / fps));
    if (m_presentationTimer->interval() != intervalMs) {
        m_presentationTimer->setInterval(intervalMs);
    }
    if (!m_presentationTimer->isActive()) {
        m_presentationTimer->start();
    }
#endif
}

void TLRenderViewport::ensureViewport()
{
#ifdef HAVE_TLRENDER
    if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        return;
    }
    if (m_viewport) {
        return;
    }

    if (!TLRenderPlayer::isInitialized()) {
        TLRenderPlayer::initialize();
    }
    m_context = TLRenderPlayer::sharedContext();
    if (!m_context) {
        qWarning() << "[TLRenderViewport] No tlRender context available";
        return;
    }

    if (!m_style) {
        m_style = ftk::Style::create(m_context);
    }

    m_viewport = new tl::qtwidget::Viewport(m_context, m_style, this);
    m_layout->addWidget(m_viewport);
    m_viewport->installEventFilter(this);
    m_viewport->setMinimumSize(QSize(0, 0));
    m_viewport->setMouseTracking(true);
    m_viewport->setFrameView(true);

    tl::BackgroundOptions bgOptions;
    bgOptions.type = tl::Background::Solid;
    bgOptions.solidColor = ftk::Color4F(0.1f, 0.1f, 0.1f, 1.0f);
    m_viewport->setBackgroundOptions(bgOptions);

    qDebug() << "[TLRenderViewport] Native tlRender viewport created";
#endif
}

void TLRenderViewport::updateRasterFrame()
{
    if (useFfmpegMovViewport()) {
        if (m_ffmpegMovViewport) {
            m_ffmpegMovViewport->update();
        }
        return;
    }
    if (useMpvViewport()) {
        if (m_mpvViewport) {
            m_mpvViewport->update();
        }
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player || !PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        return;
    }
    if (m_player->mpvPlayer() && m_player->mpvPlayer()->hasMedia()) {
        return;
    }

    const QSize targetSize = size();
    QImage frame;
    if (m_player->ffmpegMovPlayer() && m_player->ffmpegMovPlayer()->hasMedia()) {
        frame = m_player->ffmpegMovPlayer()->currentFrameImage(targetSize);
    } else {
        frame = m_player->getCurrentFrame(targetSize);
    }
    if (frame.isNull()) {
        return;
    }

    m_rasterFrame = frame;
    ++m_presentationRevision;
    update();
    emit frameRendered();
#endif
}

void TLRenderViewport::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    if (!PlatformSession::shouldUseRasterPreviewFallbackOnWayland() || useFfmpegMovViewport() || useMpvViewport()) {
        return;
    }

    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 17));

    if (m_rasterFrame.isNull()) {
        return;
    }

    const QRect targetRect = rasterImageRect();
    if (!targetRect.isValid() || targetRect.isEmpty()) {
        return;
    }

    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(targetRect, m_rasterFrame);
}

QImage TLRenderViewport::currentRasterFrameForTest()
{
    if (useFfmpegMovViewport()) {
        return m_ffmpegMovViewport ? m_ffmpegMovViewport->currentFrameForTest() : QImage();
    }
    if (useMpvViewport()) {
        return m_mpvViewport ? m_mpvViewport->currentFrameForTest() : QImage();
    }
    return m_rasterFrame;
}

QImage TLRenderViewport::currentPresentedFrameForTest()
{
    if (useFfmpegMovViewport()) {
        return m_ffmpegMovViewport ? m_ffmpegMovViewport->currentFrameForTest() : QImage();
    }
    if (useMpvViewport()) {
        return m_mpvViewport ? m_mpvViewport->currentFrameForTest() : QImage();
    }
#ifdef HAVE_TLRENDER
    if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        return currentRasterFrameForTest();
    }
    if (m_viewport) {
        return m_viewport->grab().toImage();
    }
#endif
    return {};
}

void TLRenderViewport::setupViewportPlayer()
{
#ifdef HAVE_TLRENDER
    ensureViewport();
    qDebug() << "[TLRenderViewport] setupViewportPlayer called";
    qDebug() << "[TLRenderViewport] m_viewport=" << m_viewport << "m_player=" << m_player;
    
    if (!m_viewport) {
        qWarning() << "[TLRenderViewport] Cannot setup - no viewport";
        return;
    }
    if (!m_player) {
        qWarning() << "[TLRenderViewport] Cannot setup - no player";
        return;
    }
    
    // Get the shared PlayerObject from TLRenderPlayer (don't create our own)
    auto playerObj = m_player->playerObject();
    if (!playerObj) {
        qWarning() << "[TLRenderViewport] Cannot setup - player has no PlayerObject";
        return;
    }

    qDebug() << "[TLRenderViewport] Using shared PlayerObject from TLRenderPlayer";
    
    // Set the shared player on native viewport
    m_viewport->setPlayer(playerObj);
    
    qDebug() << "[TLRenderViewport] Viewport player setup complete";
#endif
}

void TLRenderViewport::setFrameView(bool enabled)
{
    if (useFfmpegMovViewport()) {
        if (m_ffmpegMovViewport) {
            m_ffmpegMovViewport->setFrameView(enabled);
        }
        return;
    }
    if (useMpvViewport()) {
        if (m_mpvViewport) {
            m_mpvViewport->setFrameView(enabled);
        }
        return;
    }
#ifdef HAVE_TLRENDER
    if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        if (enabled) {
            m_waylandZoom = 1.0;
            updateRasterFrame();
        }
        return;
    }
    if (m_viewport) {
        m_viewport->setFrameView(enabled);
    }
#else
    Q_UNUSED(enabled)
#endif
}

bool TLRenderViewport::frameViewEnabled() const
{
    if (useFfmpegMovViewport()) {
        return m_ffmpegMovViewport ? m_ffmpegMovViewport->frameViewEnabled() : true;
    }
    if (useMpvViewport()) {
        return m_mpvViewport ? m_mpvViewport->frameViewEnabled() : true;
    }
#ifdef HAVE_TLRENDER
    if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        return qFuzzyCompare(m_waylandZoom, 1.0);
    }
    if (m_viewport) {
        return m_viewport->hasFrameView();
    }
#endif
    return true;
}

void TLRenderViewport::zoomRelative(double factor)
{
    if (useFfmpegMovViewport()) {
        if (m_ffmpegMovViewport) {
            m_ffmpegMovViewport->zoomRelative(factor);
        }
        return;
    }
    if (useMpvViewport()) {
        if (m_mpvViewport) {
            m_mpvViewport->zoomRelative(factor);
        }
        return;
    }
#ifdef HAVE_TLRENDER
    if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        m_waylandZoom = qBound(0.1, m_waylandZoom * factor, 10.0);
        update();
        return;
    }
    if (m_viewport) {
        if (m_viewport->hasFrameView()) {
            m_viewport->setFrameView(false);
        }
        const double zoom = qBound(0.1, m_viewport->zoom() * factor, 10.0);
        m_viewport->setViewPosAndZoom(m_viewport->viewPos(), zoom);
    }
#else
    Q_UNUSED(factor)
#endif
}

QRect TLRenderViewport::displayedContentRect() const
{
    if (useFfmpegMovViewport()) {
        return m_ffmpegMovViewport ? m_ffmpegMovViewport->displayedContentRect() : rect();
    }
    if (useMpvViewport()) {
        return m_mpvViewport ? m_mpvViewport->displayedContentRect() : rect();
    }
#ifdef HAVE_TLRENDER
    if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        return rasterImageRect();
    }
    if (m_viewport) {
        return m_viewport->geometry();
    }
#endif
    return rect();
}

double TLRenderViewport::fps() const
{
    if (useFfmpegMovViewport()) {
        return m_measuredFps;
    }
    if (useMpvViewport()) {
        return m_measuredFps;
    }
#ifdef HAVE_TLRENDER
    if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        return m_measuredFps;
    } else if (m_viewport) {
        return m_viewport->getFPS();
    }
#endif
    return 0.0;
}

size_t TLRenderViewport::droppedFrames() const
{
    if (useFfmpegMovViewport()) {
        return 0;
    }
    if (useMpvViewport()) {
        return 0;
    }
#ifdef HAVE_TLRENDER
    if (m_viewport) {
        return m_viewport->getDroppedFrames();
    }
#endif
    return 0;
}

void TLRenderViewport::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (useFfmpegMovViewport() && m_ffmpegMovViewport) {
        m_ffmpegMovViewport->update();
        return;
    }
    if (useMpvViewport() && m_mpvViewport) {
        m_mpvViewport->update();
        return;
    }
    if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        updateRasterFrame();
    }
}

QRect TLRenderViewport::rasterImageRect() const
{
    const QRect contents = contentsRect();
    if (m_rasterFrame.isNull()) {
        return contents;
    }

    QSize drawSize = m_rasterFrame.size().scaled(contents.size(), Qt::KeepAspectRatio);
    if (!qFuzzyCompare(m_waylandZoom, 1.0)) {
        drawSize = (drawSize * m_waylandZoom).boundedTo(contents.size() * 10);
    }

    const QPoint topLeft(
        contents.x() + (contents.width() - drawSize.width()) / 2,
        contents.y() + (contents.height() - drawSize.height()) / 2);
    return QRect(topLeft, drawSize);
}

bool TLRenderViewport::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_ffmpegMovViewport || watched == m_mpvViewport) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            if (QWidget* overlay = parentWidget()) {
                QCoreApplication::sendEvent(overlay, event);
                return true;
            }
        }
    }
#ifdef HAVE_TLRENDER
    if (watched == m_viewport || watched == this) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            auto *keyEvent = static_cast<QKeyEvent*>(event);
            if (QWidget *overlay = parentWidget()) {
                QCoreApplication::sendEvent(overlay, keyEvent);
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
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
            QWheelEvent* wheelEvent = static_cast<QWheelEvent*>(event);
            const double factor = wheelEvent->angleDelta().y() > 0 ? 1.15 : 0.85;
            zoomRelative(factor);
            return true;
        } else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (m_isPanning && (mouseEvent->buttons() & Qt::MiddleButton)) {
                if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
                    return true;
                }
                const QPoint delta = mouseEvent->pos() - m_lastPanPos;
                m_lastPanPos = mouseEvent->pos();
                const double zoom = m_viewport->zoom();
                const double scale = zoom > 0.0 ? zoom : 1.0;
                const int dx = qRound(delta.x() / scale);
                const int dy = qRound(delta.y() / scale);
                const ftk::V2I& currentPos = m_viewport->viewPos();
                const ftk::V2I newPos(currentPos.x - dx, currentPos.y - dy);
                m_viewport->setViewPosAndZoom(newPos, zoom);
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (m_isPanning && mouseEvent->button() == Qt::MiddleButton) {
                m_isPanning = false;
                unsetCursor();
                return true;
            }
        }
    }
#endif
    return QWidget::eventFilter(watched, event);
}

bool TLRenderViewport::useFfmpegMovViewport() const
{
    if (PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        return false;
    }
    return m_player
        && m_player->ffmpegMovPlayer()
        && m_player->ffmpegMovPlayer()->hasMedia();
}

bool TLRenderViewport::useMpvViewport() const
{
    return false;
}
