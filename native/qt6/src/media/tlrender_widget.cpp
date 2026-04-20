/**
 * TLRenderWidget - OpenGL widget for tlRender video rendering
 *
 * Provides hardware-accelerated video display with full OCIO color management.
 */

#include "tlrender_widget.h"
#include "tlrender_player.h"

#include <QDebug>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QOpenGLContext>

#ifdef HAVE_TLRENDER
#include <tlRender/GL/Render.h>
#include <tlRender/Timeline/Player.h>
#include <tlRender/Timeline/Video.h>
#include <tlRender/Timeline/DisplayOptions.h>
#include <tlRender/Timeline/ColorOptions.h>
#include <tlRender/Timeline/BackgroundOptions.h>

#include <ftk/Core/Context.h>
#include <ftk/Core/Box.h>
#include <ftk/Core/Size.h>
#include <ftk/Core/Color.h>
#include <ftk/Core/LogSystem.h>
#include <ftk/Core/FontSystem.h>
#include <ftk/Core/LRUCache.h>
#include <ftk/GL/Render.h>
#include <ftk/GL/Init.h>
#endif

// ============================================================================
// Construction / Destruction
// ============================================================================

TLRenderWidget::TLRenderWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    // Request OpenGL 4.1 Core Profile
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    setFormat(format);

    // Enable mouse tracking for hover events
    setMouseTracking(true);

    // Create render timer
    m_renderTimer = new QTimer(this);
    m_renderTimer->setInterval(16); // ~60fps
    connect(m_renderTimer, &QTimer::timeout, this, &TLRenderWidget::onRenderTimer);

    m_frameTimer.start();
}

TLRenderWidget::~TLRenderWidget()
{
    makeCurrent();
    cleanupRenderer();
    doneCurrent();
}

// ============================================================================
// Player Management
// ============================================================================

void TLRenderWidget::setPlayer(TLRenderPlayer* player)
{
    if (m_player) {
        disconnect(m_player, nullptr, this, nullptr);
    }

    m_player = player;

    if (m_player) {
        connect(m_player, &TLRenderPlayer::positionChanged,
                this, &TLRenderWidget::onPlayerPositionChanged);
        connect(m_player, &TLRenderPlayer::videoFramesChanged,
            this, [this]() {
                qDebug() << "[TLRenderWidget] videoFramesChanged received - requesting render";
                requestRender();
            });
        connect(m_player, &TLRenderPlayer::mediaInfoReady,
                this, [this](const TLRenderPlayer::MediaInfo& info) {
                    qDebug() << "[TLRenderWidget] mediaInfoReady:" << info.width << "x" << info.height;
                    m_videoSize = QSize(info.width, info.height);
                    calculateVideoRect();
                    emit videoSizeChanged(m_videoSize);
                    requestRender();
                });
    }

    requestRender();
}

// ============================================================================
// Display Settings
// ============================================================================

void TLRenderWidget::setFitMode(FitMode mode)
{
    if (m_fitMode != mode) {
        m_fitMode = mode;
        calculateVideoRect();
        requestRender();
    }
}

void TLRenderWidget::setBackgroundColor(const QColor& color)
{
    if (m_backgroundColor != color) {
        m_backgroundColor = color;
        requestRender();
    }
}

QRectF TLRenderWidget::videoRect() const
{
    return m_videoRect;
}

QPointF TLRenderWidget::widgetToVideo(const QPointF& widgetPos) const
{
    if (m_videoRect.isEmpty() || m_videoSize.isEmpty()) {
        return widgetPos;
    }

    // Convert widget position to video position
    double scaleX = m_videoSize.width() / m_videoRect.width();
    double scaleY = m_videoSize.height() / m_videoRect.height();

    return QPointF(
        (widgetPos.x() - m_videoRect.x()) * scaleX,
        (widgetPos.y() - m_videoRect.y()) * scaleY
    );
}

QPointF TLRenderWidget::videoToWidget(const QPointF& videoPos) const
{
    if (m_videoRect.isEmpty() || m_videoSize.isEmpty()) {
        return videoPos;
    }

    // Convert video position to widget position
    double scaleX = m_videoRect.width() / m_videoSize.width();
    double scaleY = m_videoRect.height() / m_videoSize.height();

    return QPointF(
        videoPos.x() * scaleX + m_videoRect.x(),
        videoPos.y() * scaleY + m_videoRect.y()
    );
}

void TLRenderWidget::requestRender()
{
    m_needsRender = true;
    update();
}

// ============================================================================
// OpenGL Implementation
// ============================================================================

void TLRenderWidget::initializeGL()
{
    if (!initializeOpenGLFunctions()) {
        qCritical() << "TLRenderWidget: Failed to initialize OpenGL 4.1 functions";
        emit renderError(tr("Failed to initialize OpenGL 4.1"));
        return;
    }

    // Log OpenGL info
    qDebug() << "TLRenderWidget: OpenGL initialized";
    qDebug() << "  Vendor:" << reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    qDebug() << "  Renderer:" << reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    qDebug() << "  Version:" << reinterpret_cast<const char*>(glGetString(GL_VERSION));

    // Setup tlRender renderer
    setupRenderer();

    // Start render timer
    m_renderTimer->start();

    m_initialized = true;
}

void TLRenderWidget::resizeGL(int w, int h)
{
    Q_UNUSED(w)
    Q_UNUSED(h)

    calculateVideoRect();
}

void TLRenderWidget::paintGL()
{
    if (!m_initialized) {
        return;
    }

    // Clear background
    glClearColor(
        m_backgroundColor.redF(),
        m_backgroundColor.greenF(),
        m_backgroundColor.blueF(),
        1.0f
    );
    glClear(GL_COLOR_BUFFER_BIT);

#ifdef HAVE_TLRENDER
    if (!m_player || !m_player->hasMedia() || !m_render) {
        return;
    }

    try {
        // Get current video frames
        auto videoFrames = m_player->currentVideoFrames();
        if (videoFrames.empty()) {
            static int emptyFrameLogCount = 0;
            if (emptyFrameLogCount++ % 100 == 0) {
                qDebug() << "[TLRenderWidget::paintGL] No frames available (every 100th log)";
            }
            return;
        }

        // Log frame info periodically during playback
        static int frameLogCount = 0;
        if (frameLogCount++ % 60 == 0) {
            qDebug() << "[TLRenderWidget::paintGL] Rendering frame, videoFrames.size():" << videoFrames.size();
        }

        // Get widget size
        ftk::Size2I renderSize(width() * devicePixelRatio(), height() * devicePixelRatio());

        // Begin render
        m_render->begin(renderSize);
        
        // Calculate video box
        ftk::Box2I videoBox(
            static_cast<int>(m_videoRect.x() * devicePixelRatio()),
            static_cast<int>(m_videoRect.y() * devicePixelRatio()),
            static_cast<int>(m_videoRect.width() * devicePixelRatio()),
            static_cast<int>(m_videoRect.height() * devicePixelRatio())
        );

        // Setup display options (OCIO is set on renderer, not in DisplayOptions)
        tl::DisplayOptions displayOptions;

        // Draw video
        std::vector<ftk::Box2I> boxes = {videoBox};
        std::vector<tl::DisplayOptions> displayOptionsList = {displayOptions};

        m_render->drawVideo(videoFrames, boxes, {}, displayOptionsList);

        // End render
        m_render->end();

        m_needsRender = false;
        emit frameRendered();

    } catch (const std::exception& e) {
        qWarning() << "TLRenderWidget: Render error:" << e.what();
        emit renderError(QString::fromStdString(e.what()));
    }
#endif

    checkGLError("paintGL");
}

void TLRenderWidget::setupRenderer()
{
#ifdef HAVE_TLRENDER
    if (!m_player || !m_player->context()) {
        qWarning() << "TLRenderWidget: Cannot setup renderer - no player context";
        return;
    }

    try {
        auto context = m_player->context();

        // Get required systems
        auto logSystem = context->getLogSystem();
        auto fontSystem = context->getSystem<ftk::FontSystem>();
        
        // Initialize GLAD for OpenGL function loading
        ftk::gl::initGLAD();

        // Create texture cache (TextureCache is a typedef for LRUCache)
        auto textureCache = std::make_shared<ftk::gl::TextureCache>();
        m_textureCache = textureCache; // Store as void* for header compatibility

        // Create renderer
        m_render = tl::gl::Render::create(logSystem, fontSystem, textureCache);

        qDebug() << "TLRenderWidget: Renderer created successfully";

    } catch (const std::exception& e) {
        qWarning() << "TLRenderWidget: Failed to create renderer:" << e.what();
        emit renderError(QString::fromStdString(e.what()));
    }
#endif
}

void TLRenderWidget::cleanupRenderer()
{
#ifdef HAVE_TLRENDER
    m_render.reset();
    m_textureCache.reset();
#endif
}

void TLRenderWidget::calculateVideoRect()
{
    if (m_videoSize.isEmpty()) {
        m_videoRect = QRectF(0, 0, width(), height());
        return;
    }

    double widgetAspect = static_cast<double>(width()) / height();
    double videoAspect = static_cast<double>(m_videoSize.width()) / m_videoSize.height();

    double displayWidth, displayHeight;
    double offsetX = 0, offsetY = 0;

    switch (m_fitMode) {
        case FitMode::Fit:
            if (videoAspect > widgetAspect) {
                // Video is wider - fit to width
                displayWidth = width();
                displayHeight = width() / videoAspect;
                offsetY = (height() - displayHeight) / 2.0;
            } else {
                // Video is taller - fit to height
                displayHeight = height();
                displayWidth = height() * videoAspect;
                offsetX = (width() - displayWidth) / 2.0;
            }
            break;

        case FitMode::Fill:
            if (videoAspect > widgetAspect) {
                // Video is wider - fit to height, crop width
                displayHeight = height();
                displayWidth = height() * videoAspect;
                offsetX = (width() - displayWidth) / 2.0;
            } else {
                // Video is taller - fit to width, crop height
                displayWidth = width();
                displayHeight = width() / videoAspect;
                offsetY = (height() - displayHeight) / 2.0;
            }
            break;

        case FitMode::OneToOne:
            displayWidth = m_videoSize.width();
            displayHeight = m_videoSize.height();
            offsetX = (width() - displayWidth) / 2.0;
            offsetY = (height() - displayHeight) / 2.0;
            break;
    }

    m_videoRect = QRectF(offsetX, offsetY, displayWidth, displayHeight);
}

void TLRenderWidget::checkGLError(const char* operation)
{
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        qWarning() << "TLRenderWidget: OpenGL error in" << operation << ":" << error;
    }
}

// ============================================================================
// Slots
// ============================================================================

void TLRenderWidget::onRenderTimer()
{
    if (m_player && m_player->playbackState() == TLRenderPlayer::PlaybackState::Playing) {
        // Frames and timing are advanced by tlRender's Qt ContextObject.
        // Keep repainting while playing.
        requestRender();
        return;
    }

    if (m_needsRender) {
        update();
    }
}

void TLRenderWidget::onPlayerPositionChanged(qint64 position)
{
    Q_UNUSED(position)
    requestRender();
}

// ============================================================================
// Event Handlers
// ============================================================================

void TLRenderWidget::mousePressEvent(QMouseEvent* event)
{
    // Forward to parent or handle click-to-play
    QOpenGLWidget::mousePressEvent(event);
}

void TLRenderWidget::mouseReleaseEvent(QMouseEvent* event)
{
    QOpenGLWidget::mouseReleaseEvent(event);
}

void TLRenderWidget::mouseMoveEvent(QMouseEvent* event)
{
    QOpenGLWidget::mouseMoveEvent(event);
}

void TLRenderWidget::wheelEvent(QWheelEvent* event)
{
    QOpenGLWidget::wheelEvent(event);
}
