/**
 * TLRenderViewport - Native tlRender viewport wrapper implementation
 */

#include "tlrender_viewport.h"
#include "tlrender_player.h"
#include <QDebug>
#include <QResizeEvent>

#ifdef HAVE_TLRENDER
#include <tlRender/Timeline/Player.h>
#include <tlRender/Timeline/ColorOptions.h>
#include <tlRender/Timeline/BackgroundOptions.h>
#endif

TLRenderViewport::TLRenderViewport(QWidget* parent)
    : QWidget(parent)
{
    // Create layout to hold the viewport
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

#ifdef HAVE_TLRENDER
    // Get shared context from TLRenderPlayer
    if (!TLRenderPlayer::isInitialized()) {
        TLRenderPlayer::initialize();
    }
    m_context = TLRenderPlayer::sharedContext();
    
    if (m_context) {
        // Create style for the viewport
        m_style = ftk::Style::create(m_context);
        
        // Create native tlRender viewport
        m_viewport = new tl::qtwidget::Viewport(m_context, m_style, this);
        m_layout->addWidget(m_viewport);
        
        // Enable frame view by default (fit video to window)
        m_viewport->setFrameView(true);
        
        // Set dark background
        tl::BackgroundOptions bgOptions;
        bgOptions.type = tl::Background::Solid;
        bgOptions.solidColor = ftk::Color4F(0.1f, 0.1f, 0.1f, 1.0f);
        m_viewport->setBackgroundOptions(bgOptions);
        
        qDebug() << "[TLRenderViewport] Native tlRender viewport created";
    } else {
        qWarning() << "[TLRenderViewport] No tlRender context available";
    }
#else
    qWarning() << "[TLRenderViewport] tlRender not available";
#endif
}

TLRenderViewport::~TLRenderViewport()
{
    // No cleanup needed - viewport and player objects are owned elsewhere
}

void TLRenderViewport::setPlayer(TLRenderPlayer* player)
{
    // Disconnect from previous player
    if (m_player) {
        disconnect(m_player, nullptr, this, nullptr);
    }
    
    m_player = player;

#ifdef HAVE_TLRENDER
    if (!m_viewport) {
        qWarning() << "[TLRenderViewport] No viewport to set player on";
        return;
    }

    if (player) {
        // Connect to media loaded signal to setup viewport when player has content
        connect(player, &TLRenderPlayer::mediaInfoReady, this, &TLRenderViewport::onMediaLoaded);
        
        // Connect to OCIO changes
        connect(player, &TLRenderPlayer::ocioOptionsChanged, this, [this]() {
            if (m_viewport && m_player) {
                m_viewport->setOCIOOptions(m_player->currentOCIOOptions());
            }
        });
        
        // If player already has media loaded, setup now
        if (player->playerObject()) {
            setupViewportPlayer();
        } else {
            // Clear viewport until media is loaded
            m_viewport->setPlayer(QSharedPointer<tl::qt::PlayerObject>());
            qDebug() << "[TLRenderViewport] Player set, waiting for media load";
        }
    } else {
        m_viewport->setPlayer(QSharedPointer<tl::qt::PlayerObject>());
        qDebug() << "[TLRenderViewport] Player cleared from viewport";
    }
#else
    Q_UNUSED(player)
#endif
}

void TLRenderViewport::onMediaLoaded(const TLRenderPlayer::MediaInfo& info)
{
    Q_UNUSED(info)
#ifdef HAVE_TLRENDER
    qDebug() << "[TLRenderViewport] onMediaLoaded slot called, setting up viewport player";
    setupViewportPlayer();
#endif
}

void TLRenderViewport::setupViewportPlayer()
{
#ifdef HAVE_TLRENDER
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
#ifdef HAVE_TLRENDER
    if (m_viewport) {
        m_viewport->setFrameView(enabled);
    }
#else
    Q_UNUSED(enabled)
#endif
}

double TLRenderViewport::fps() const
{
#ifdef HAVE_TLRENDER
    if (m_viewport) {
        return m_viewport->getFPS();
    }
#endif
    return 0.0;
}

size_t TLRenderViewport::droppedFrames() const
{
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
    // The native viewport handles resize internally
}
