#ifndef TLRENDER_WIDGET_H
#define TLRENDER_WIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions_4_1_Core>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>

// Forward declarations
class TLRenderPlayer;

namespace ftk {
    class Context;
    class FontSystem;
    class LogSystem;
}

namespace tl {
    namespace gl {
        class Render;
    }
}

// Forward declare texture cache type - the actual type is a typedef
// Use void* for the member and cast in implementation
namespace ftk {
    namespace gl {
        class Texture;
    }
}

/**
 * @brief OpenGL widget for rendering tlRender video frames
 *
 * This widget provides hardware-accelerated rendering of video frames
 * from TLRenderPlayer using OpenGL 4.1 with full OCIO color management.
 *
 * Features:
 * - OpenGL 4.1 Core Profile rendering
 * - OCIO color pipeline integration
 * - Aspect ratio preservation
 * - Multiple fit modes (Fit, Fill, 1:1)
 * - Background color customization
 * - Overlay support for annotations
 */
class TLRenderWidget : public QOpenGLWidget, protected QOpenGLFunctions_4_1_Core
{
    Q_OBJECT

public:
    enum class FitMode {
        Fit,    // Fit within widget, preserve aspect ratio
        Fill,   // Fill widget, may crop
        OneToOne // 1:1 pixel mapping, may require scrolling
    };
    Q_ENUM(FitMode)

    explicit TLRenderWidget(QWidget* parent = nullptr);
    ~TLRenderWidget();

    /**
     * @brief Set the player to render frames from
     */
    void setPlayer(TLRenderPlayer* player);
    TLRenderPlayer* player() const { return m_player; }

    /**
     * @brief Set the fit mode for video display
     */
    void setFitMode(FitMode mode);
    FitMode fitMode() const { return m_fitMode; }

    /**
     * @brief Set the background color
     */
    void setBackgroundColor(const QColor& color);
    QColor backgroundColor() const { return m_backgroundColor; }

    /**
     * @brief Get the video rectangle in widget coordinates
     * Useful for overlay positioning
     */
    QRectF videoRect() const;

    /**
     * @brief Get the current video size
     */
    QSize videoSize() const { return m_videoSize; }

    /**
     * @brief Convert widget coordinates to video coordinates
     */
    QPointF widgetToVideo(const QPointF& widgetPos) const;

    /**
     * @brief Convert video coordinates to widget coordinates
     */
    QPointF videoToWidget(const QPointF& videoPos) const;

    /**
     * @brief Request a render update
     */
    void requestRender();

signals:
    /**
     * @brief Emitted when the video size changes
     */
    void videoSizeChanged(const QSize& size);

    /**
     * @brief Emitted when a frame is rendered
     */
    void frameRendered();

    /**
     * @brief Emitted on OpenGL errors
     */
    void renderError(const QString& error);

protected:
    // QOpenGLWidget interface
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    // Event handlers
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void onRenderTimer();
    void onPlayerPositionChanged(qint64 position);

private:
    void setupRenderer();
    void cleanupRenderer();
    void calculateVideoRect();
    void checkGLError(const char* operation);

    // Player
    TLRenderPlayer* m_player{nullptr};

    // tlRender renderer
    std::shared_ptr<tl::gl::Render> m_render;
    std::shared_ptr<void> m_textureCache; // Actually ftk::gl::TextureCache, cast in .cpp

    // Display settings
    FitMode m_fitMode{FitMode::Fit};
    QColor m_backgroundColor{Qt::black};
    QSize m_videoSize;
    QRectF m_videoRect;

    // Render timer for continuous playback
    QTimer* m_renderTimer{nullptr};
    QElapsedTimer m_frameTimer;
    bool m_needsRender{true};

    // OpenGL state
    bool m_initialized{false};
};

#endif // TLRENDER_WIDGET_H
