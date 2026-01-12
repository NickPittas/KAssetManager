/**
 * TLRenderViewport - Native tlRender viewport wrapper for Qt
 *
 * This uses tlRender's built-in qtwidget::Viewport which provides:
 * - Proper 5ms precision timer-driven rendering
 * - Internal player observation (no manual frame fetching)
 * - Built-in OCIO/LUT color pipeline
 * - Efficient dirty-flag based redraws
 */

#ifndef TLRENDER_VIEWPORT_H
#define TLRENDER_VIEWPORT_H

#include <QWidget>
#include <QVBoxLayout>
#include <memory>

#include "tlrender_player.h"

#ifdef HAVE_TLRENDER
#include <tlRender/QtWidget/Viewport.h>
#include <tlRender/Qt/PlayerObject.h>
#include <ftk/Core/Context.h>
#include <ftk/UI/Style.h>
#endif

/**
 * @brief Qt wrapper around tlRender's native viewport
 *
 * This thin wrapper embeds tlRender's qtwidget::Viewport which handles
 * all rendering internally with proper timing and OCIO support.
 */
class TLRenderViewport : public QWidget
{
    Q_OBJECT

public:
    explicit TLRenderViewport(QWidget* parent = nullptr);
    ~TLRenderViewport();

    /**
     * @brief Set the player to render
     * Creates a PlayerObject wrapper for the native viewport
     */
    void setPlayer(TLRenderPlayer* player);
    TLRenderPlayer* player() const { return m_player; }

    /**
     * @brief Set OCIO options on the viewport
     */
    void setOCIOOptions(const QString& configPath,
                        const QString& inputColorSpace,
                        const QString& display,
                        const QString& view);

    /**
     * @brief Enable/disable frame view (fit to window)
     */
    void setFrameView(bool enabled);

    /**
     * @brief Get the native viewport for advanced access
     */
#ifdef HAVE_TLRENDER
    tl::qtwidget::Viewport* nativeViewport() const { return m_viewport; }
#endif

    /**
     * @brief Get current FPS from the viewport
     */
    double fps() const;

    /**
     * @brief Get dropped frame count
     */
    size_t droppedFrames() const;

signals:
    void fpsChanged(double fps);
    void frameRendered();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onMediaLoaded(const TLRenderPlayer::MediaInfo& info);

private:
    void setupViewportPlayer();
    
    TLRenderPlayer* m_player{nullptr};

#ifdef HAVE_TLRENDER
    std::shared_ptr<ftk::Context> m_context;
    std::shared_ptr<ftk::Style> m_style;
    tl::qtwidget::Viewport* m_viewport{nullptr};
#endif

    QVBoxLayout* m_layout{nullptr};
};

#endif // TLRENDER_VIEWPORT_H
