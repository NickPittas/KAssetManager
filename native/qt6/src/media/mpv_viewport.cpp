#include "mpv_viewport.h"

#include <QDebug>
#include <QGuiApplication>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QVBoxLayout>
#include <QtGui/qguiapplication_platform.h>
#include <cstdio>

#include "libmpv_runtime.h"
#include "platform_session.h"

#define MPV_LOG(fmt, ...) do { fprintf(stderr, "[MPV_DBG] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

// ============================================================================
// MpvQuickRenderer — QQuickFramebufferObject::Renderer that drives mpv
// ============================================================================

class MpvQuickRenderer : public QQuickFramebufferObject::Renderer
{
public:
    MpvQuickRenderer(MpvQuickItem* item)
        : m_item(item)
        , m_runtime(LibMpvRuntime::instance())
    {
        MPV_LOG("MpvQuickRenderer constructed, item=%p", item);
    }

    ~MpvQuickRenderer() override
    {
        MPV_LOG("MpvQuickRenderer destructor");
        if (m_renderContext) {
            m_runtime.render_context_set_update_callback(m_renderContext, nullptr, nullptr);
            m_runtime.render_context_free(m_renderContext);
            m_renderContext = nullptr;
        }
    }

    // Called on render thread with GUI thread blocked — safe to read item state
    void synchronize(QQuickFramebufferObject* fboItem) override
    {
        auto* item = static_cast<MpvQuickItem*>(fboItem);
        m_mpvHandle = item->mpvHandle();
        MPV_LOG("synchronize: mpvHandle=%p", m_mpvHandle);
    }

    // Called on render thread when FBO needs (re)creation — GL context is valid here
    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override
    {
        MPV_LOG("createFramebufferObject: size=%dx%d", size.width(), size.height());
        auto* ctx = QOpenGLContext::currentContext();
        MPV_LOG("  GL context=%p valid=%d", ctx, ctx ? ctx->isValid() : -1);
        if (!m_renderContext && m_mpvHandle) {
            createRenderContext();
        }
        auto* fbo = new QOpenGLFramebufferObject(size);
        MPV_LOG("  FBO created: valid=%d handle=%u", fbo->isValid(), fbo->handle());
        return fbo;
    }

    // Called on render thread to render a frame into the FBO
    void render() override
    {
        MPV_LOG("render: m_renderContext=%p", m_renderContext);
        if (!m_renderContext) {
            return;
        }

        const uint64_t updateFlags = m_runtime.render_context_update(m_renderContext);
        MPV_LOG("  render_context_update flags=%llu", static_cast<unsigned long long>(updateFlags));

        auto* fbo = framebufferObject();
        MPV_LOG("  FBO handle=%u size=%dx%d", fbo->handle(), fbo->width(), fbo->height());
        const auto dpr = m_item->window() ? m_item->window()->devicePixelRatio() : 1.0;
        const int w = static_cast<int>(fbo->width() * dpr);
        const int h = static_cast<int>(fbo->height() * dpr);

        mpv_opengl_fbo mpvFbo{
            static_cast<int>(fbo->handle()),
            w, h,
            0  // internal format (0 = default)
        };
        int flipY = 0;  // QQuickFBO renders offscreen; scene graph handles Y-flip
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };

        MPV_LOG("  calling render_context_render w=%d h=%d", w, h);
        m_runtime.render_context_render(m_renderContext, params);
        m_runtime.render_context_report_swap(m_renderContext);
        MPV_LOG("  render_context_render returned");

        // Signal the item that a frame was rendered
        if (m_item) {
            QMetaObject::invokeMethod(m_item, "frameRendered", Qt::QueuedConnection);
        }
    }

private:
    static void* getProcAddress(void* /*ctx*/, const char* name)
    {
        if (auto* gl = QOpenGLContext::currentContext()) {
            return reinterpret_cast<void*>(gl->getProcAddress(QByteArray(name)));
        }
        return nullptr;
    }

    void createRenderContext()
    {
        MPV_LOG("createRenderContext: mpvHandle=%p runtime.available=%d",
                m_mpvHandle, m_runtime.isAvailable());
        if (!m_mpvHandle || !m_runtime.isAvailable()) {
            return;
        }

        mpv_opengl_init_params glInit{&MpvQuickRenderer::getProcAddress, nullptr};

        void* wlDisplay = nullptr;
        if (PlatformSession::isWayland()) {
            if (auto* app = qGuiApp) {
                if (auto* waylandApp = app->nativeInterface<QNativeInterface::QWaylandApplication>()) {
                    wlDisplay = waylandApp->display();
                }
            }
        }
        MPV_LOG("  wlDisplay=%p", wlDisplay);

        int advanced = 0;
        mpv_render_param params[5] = {};
        int idx = 0;
        params[idx++] = {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)};
        params[idx++] = {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit};
        if (wlDisplay) {
            params[idx++] = {MPV_RENDER_PARAM_WL_DISPLAY, wlDisplay};
            MPV_LOG("  Passing wl_display to mpv render context");
        }
        params[idx++] = {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced};
        params[idx++] = {MPV_RENDER_PARAM_INVALID, nullptr};

        int rc = m_runtime.render_context_create(&m_renderContext, m_mpvHandle, params);
        if (rc < 0) {
            MPV_LOG("  FAILED to create render context, rc=%d", rc);
            m_renderContext = nullptr;
            return;
        }

        MPV_LOG("  Render context created successfully: %p", m_renderContext);

        // Set up the redraw callback — when mpv has a new frame, trigger update on the item
        m_runtime.render_context_set_update_callback(
            m_renderContext, &MpvQuickItem::onMpvRedraw, m_item);
    }

    MpvQuickItem* m_item{nullptr};
    const LibMpvRuntime& m_runtime;
    mpv_handle* m_mpvHandle{nullptr};
    mpv_render_context* m_renderContext{nullptr};
};


// ============================================================================
// MpvQuickItem — QQuickFramebufferObject subclass
// ============================================================================

MpvQuickItem::MpvQuickItem(QQuickItem* parent)
    : QQuickFramebufferObject(parent)
{
    MPV_LOG("MpvQuickItem constructed");
    // When this item is placed in a window, connect to frameSwapped
    connect(this, &QQuickItem::windowChanged, this, &MpvQuickItem::onWindowChanged);
}

MpvQuickItem::~MpvQuickItem() = default;

QQuickFramebufferObject::Renderer* MpvQuickItem::createRenderer() const
{
    MPV_LOG("MpvQuickItem::createRenderer called");
    return new MpvQuickRenderer(const_cast<MpvQuickItem*>(this));
}

void MpvQuickItem::setPlayer(MpvPlayer* player)
{
    MPV_LOG("MpvQuickItem::setPlayer player=%p", player);
    if (m_player) {
        disconnect(m_player, nullptr, this, nullptr);
    }
    m_player = player;
    if (m_player) {
        MPV_LOG("  mpv handle=%p", m_player->handle());
        // When mpv signals a new frame, trigger a Qt Quick update
        connect(m_player, &MpvPlayer::frameUpdated, this, [this]() {
            update();
        });
        connect(m_player, &MpvPlayer::mediaInfoReady, this, [this](const media_player::MediaInfo&) {
            update();
        });
    }
    update();
}

mpv_handle* MpvQuickItem::mpvHandle() const
{
    return m_player ? m_player->handle() : nullptr;
}

void MpvQuickItem::onWindowChanged(QQuickWindow* window)
{
    MPV_LOG("MpvQuickItem::onWindowChanged window=%p", window);
    if (window) {
        // Ensure scene graph rendering triggers our update
        connect(window, &QQuickWindow::frameSwapped, this, [this]() {
            // Already handled by render() emitting frameRendered
        }, Qt::DirectConnection);
    }
}

// Static callback from mpv render context — called from mpv's internal thread
void MpvQuickItem::onMpvRedraw(void* ctx)
{
    QMetaObject::invokeMethod(
        static_cast<MpvQuickItem*>(ctx),
        "update",
        Qt::QueuedConnection);
}


// ============================================================================
// MpvViewport — QWidget wrapper hosting QQuickWidget + MpvQuickItem
// ============================================================================

MpvViewport::MpvViewport(QWidget* parent)
    : QWidget(parent)
{
    MPV_LOG("MpvViewport::MpvViewport START");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    MPV_LOG("  Creating QQuickWidget...");
    m_quickWidget = new QQuickWidget(this);
    MPV_LOG("  QQuickWidget created: %p", m_quickWidget);
    m_quickWidget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_quickWidget->setClearColor(QColor(0x11, 0x11, 0x11));

    MPV_LOG("  Creating MpvQuickItem...");
    m_quickItem = new MpvQuickItem();

    // Set the MpvQuickItem as the root item — no QML file needed
    MPV_LOG("  Setting content on QQuickWidget...");
    m_quickWidget->setContent(QUrl(), nullptr, m_quickItem);
    MPV_LOG("  Content set, status=%d", static_cast<int>(m_quickWidget->status()));

    layout->addWidget(m_quickWidget);

    // Forward frameRendered from the quick item
    connect(m_quickItem, &MpvQuickItem::frameRendered, this, &MpvViewport::frameRendered);
    MPV_LOG("MpvViewport::MpvViewport DONE");
}

MpvViewport::~MpvViewport() = default;

void MpvViewport::setPlayer(MpvPlayer* player)
{
    MPV_LOG("MpvViewport::setPlayer player=%p", player);
    m_quickItem->setPlayer(player);
}

MpvPlayer* MpvViewport::player() const
{
    return m_quickItem->player();
}

void MpvViewport::setFrameView(bool enabled)
{
    m_frameViewEnabled = enabled;
    if (enabled) {
        m_zoom = 1.0;
    }
    // mpv handles its own aspect ratio via keepaspect=yes
    // Zoom/pan would need mpv's video-pan-x/y properties in future
}

void MpvViewport::zoomRelative(double factor)
{
    if (m_frameViewEnabled) {
        m_frameViewEnabled = false;
    }
    m_zoom = qBound(0.1, m_zoom * factor, 10.0);
    // Zoom/pan via mpv properties — future enhancement
}

QRect MpvViewport::displayedContentRect() const
{
    // With keepaspect=yes, mpv renders within its surface respecting aspect ratio.
    // Return the full widget rect as a reasonable approximation.
    return rect();
}

QImage MpvViewport::currentFrameForTest()
{
    if (m_quickWidget) {
        return m_quickWidget->grabFramebuffer();
    }
    return {};
}
