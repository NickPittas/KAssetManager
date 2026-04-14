/**
 * TLRenderViewport - Native tlRender viewport wrapper implementation
 */

#include "tlrender_viewport.h"
#include "tlrender_player.h"
#include "platform_session.h"
#include <QDebug>
#include <QPixmap>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QWindow>

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
        m_rasterLabel = new QLabel(this);
        m_rasterLabel->setAlignment(Qt::AlignCenter);
        m_rasterLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_rasterLabel->setMinimumSize(QSize(0, 0));
        m_rasterLabel->setFocusPolicy(Qt::StrongFocus);
        m_rasterLabel->installEventFilter(this);
        m_rasterLabel->setStyleSheet(QStringLiteral("QLabel { background: #111; color: #888; }") );
        m_layout->addWidget(m_rasterLabel);
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
    if (!m_player || !m_player->mpvPlayer() || !m_player->mpvPlayer()->isAvailable()) {
        return;
    }

    ensureMpvViewport();
    m_mpvViewport->setPlayer(m_player->mpvPlayer());
    m_mpvViewport->setVisible(true);
#ifdef HAVE_TLRENDER
    if (m_rasterLabel) {
        m_rasterLabel->setVisible(false);
    }
    if (m_viewport) {
        m_viewport->setVisible(false);
    }
#endif
    m_mpvViewport->update();
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
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
        ++m_presentationRevision;
        emit frameRendered();
    });
}

void TLRenderViewport::setPlayer(TLRenderPlayer* player)
{
    // Disconnect from previous player
    if (m_player) {
        disconnect(m_player, nullptr, this, nullptr);
    }

    if (m_presentationTimer) {
        m_presentationTimer->stop();
    }

    m_player = player;

    if (m_mpvViewport) {
        m_mpvViewport->setPlayer(player ? player->mpvPlayer() : nullptr);
    }

    syncBackendWidgetVisibility();

    if (useMpvViewport()) {
        return;
    }

#ifdef HAVE_TLRENDER
    if (player) {
        // Connect to media loaded signal to setup viewport when player has content
        connect(player, &TLRenderPlayer::mediaInfoReady, this, &TLRenderViewport::onMediaLoaded);
        connect(player, &TLRenderPlayer::playbackStateChanged, this, [this](TLRenderPlayer::PlaybackState) {
            syncPresentationTimer();
        });
        
        // Connect to OCIO changes
        connect(player, &TLRenderPlayer::ocioOptionsChanged, this, [this]() {
            if (m_viewport && m_player) {
                m_viewport->setOCIOOptions(m_player->currentOCIOOptions());
            }
        });
        
        // Connect to video frames changed (for exposure/gamma updates)
        connect(player, &TLRenderPlayer::videoFramesChanged, this, [this]() {
            if (m_rasterLabel && m_player && PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
                if (m_player->playbackState() != TLRenderPlayer::PlaybackState::Playing) {
                    updateRasterFrame();
                }
            } else if (m_viewport && m_player) {
                m_viewport->setDisplayOptions(std::vector<tl::DisplayOptions>{m_player->currentDisplayOptions()});
            }
        });
        
        // If player already has media loaded, setup now
        if (m_rasterLabel && PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
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
}

void TLRenderViewport::onMediaLoaded(const TLRenderPlayer::MediaInfo& info)
{
    m_mediaFps = info.fps > 0.0 ? info.fps : 24.0;
    syncBackendWidgetVisibility();
    if (useMpvViewport()) {
        if (m_mpvViewport) {
            m_mpvViewport->update();
        }
        return;
    }
#ifdef HAVE_TLRENDER
    if (m_rasterLabel && PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
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
    if (mpvActive) {
        ensureMpvViewport();
        m_mpvViewport->setPlayer(m_player ? m_player->mpvPlayer() : nullptr);
    }
    if (m_mpvViewport) {
        m_mpvViewport->setVisible(mpvActive);
    }
#ifdef HAVE_TLRENDER
    if (m_rasterLabel) {
        m_rasterLabel->setVisible(!mpvActive);
    }
    if (m_viewport) {
        m_viewport->setVisible(!mpvActive);
    }
#endif
}

void TLRenderViewport::syncPresentationTimer()
{
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
    if (!m_rasterLabel || !m_player || !PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
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
    if (m_rasterLabel && PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
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
    if (useMpvViewport()) {
        if (m_mpvViewport) {
            m_mpvViewport->update();
        }
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_rasterLabel || !m_player) {
        return;
    }

    const QSize targetSize = m_rasterLabel->size().isValid() ? m_rasterLabel->size() : size();
    const QImage frame = m_player->getCurrentFrame(targetSize);
    if (frame.isNull()) {
        return;
    }

    QPixmap pixmap = QPixmap::fromImage(frame);
    if (!targetSize.isEmpty()) {
        const QSize scaled = pixmap.size().scaled(targetSize * m_waylandZoom, Qt::KeepAspectRatio);
        pixmap = pixmap.scaled(scaled, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    m_rasterLabel->setPixmap(pixmap);
    ++m_presentationRevision;
    emit frameRendered();
#endif
}

QImage TLRenderViewport::currentRasterFrameForTest()
{
    if (useMpvViewport()) {
        return m_mpvViewport ? m_mpvViewport->currentFrameForTest() : QImage();
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (!m_rasterLabel || !m_rasterLabel->pixmap()) {
        return {};
    }
    return m_rasterLabel->pixmap().toImage();
#else
    if (!m_rasterLabel) {
        return {};
    }
    const QPixmap* pixmap = m_rasterLabel->pixmap();
    if (!pixmap) {
        return {};
    }
    return pixmap->toImage();
#endif
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
    
    // Apply current OCIO options from player
    m_viewport->setOCIOOptions(m_player->currentOCIOOptions());
    
    // Apply current display options (exposure/gamma) from player
    m_viewport->setDisplayOptions(std::vector<tl::DisplayOptions>{m_player->currentDisplayOptions()});
    
    qDebug() << "[TLRenderViewport] Viewport player setup complete";
#endif
}

void TLRenderViewport::setOCIOOptions(const QString& configPath,
                                       const QString& inputColorSpace,
                                       const QString& display,
                                       const QString& view)
{
#ifdef HAVE_TLRENDER
    if (!m_viewport) return;

    tl::OCIOOptions options;
    options.enabled = !configPath.isEmpty();
    options.fileName = configPath.toStdString();
    options.input = inputColorSpace.toStdString();
    options.display = display.toStdString();
    options.view = view.toStdString();
    
    m_viewport->setOCIOOptions(options);
    qDebug() << "[TLRenderViewport] OCIO options set:"
             << "config=" << configPath
             << "input=" << inputColorSpace
             << "display=" << display
             << "view=" << view;
#else
    Q_UNUSED(configPath)
    Q_UNUSED(inputColorSpace)
    Q_UNUSED(display)
    Q_UNUSED(view)
#endif
}

void TLRenderViewport::setFrameView(bool enabled)
{
    if (useMpvViewport()) {
        if (m_mpvViewport) {
            m_mpvViewport->setFrameView(enabled);
        }
        return;
    }
#ifdef HAVE_TLRENDER
    if (m_rasterLabel && PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
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
    if (useMpvViewport()) {
        return m_mpvViewport ? m_mpvViewport->frameViewEnabled() : true;
    }
#ifdef HAVE_TLRENDER
    if (m_rasterLabel && PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
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
    if (useMpvViewport()) {
        if (m_mpvViewport) {
            m_mpvViewport->zoomRelative(factor);
        }
        return;
    }
#ifdef HAVE_TLRENDER
    if (m_rasterLabel && PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        m_waylandZoom = qBound(0.1, m_waylandZoom * factor, 10.0);
        updateRasterFrame();
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
    if (useMpvViewport()) {
        return m_mpvViewport ? m_mpvViewport->displayedContentRect() : rect();
    }
#ifdef HAVE_TLRENDER
    if (m_rasterLabel) {
        const QRect contents = m_rasterLabel->contentsRect();
        const QPixmap pixmap = m_rasterLabel->pixmap();
        if (pixmap.isNull()) {
            return contents;
        }

        const QSize fitted = pixmap.size().scaled(contents.size(), Qt::KeepAspectRatio);
        const QPoint topLeft(
            contents.x() + (contents.width() - fitted.width()) / 2,
            contents.y() + (contents.height() - fitted.height()) / 2);
        return QRect(topLeft, fitted);
    }
    if (m_viewport) {
        return m_viewport->geometry();
    }
#endif
    return rect();
}

double TLRenderViewport::fps() const
{
    if (useMpvViewport()) {
        return 0.0;
    }
#ifdef HAVE_TLRENDER
    if (m_rasterLabel && PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        return 0.0;
    } else if (m_viewport) {
        return m_viewport->getFPS();
    }
#endif
    return 0.0;
}

size_t TLRenderViewport::droppedFrames() const
{
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
    if (useMpvViewport() && m_mpvViewport) {
        m_mpvViewport->update();
        return;
    }
    if (m_rasterLabel && PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
        updateRasterFrame();
    }
}

bool TLRenderViewport::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_mpvViewport) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
            if (QWidget* overlay = parentWidget()) {
                QCoreApplication::sendEvent(overlay, event);
                return true;
            }
        }
    }
#ifdef HAVE_TLRENDER
    if (watched == m_viewport || watched == m_rasterLabel) {
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
                if (m_rasterLabel && PlatformSession::shouldUseRasterPreviewFallbackOnWayland()) {
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

bool TLRenderViewport::useMpvViewport() const
{
    return m_player && m_player->mpvPlayer() && m_player->mpvPlayer()->hasMedia();
}
