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
#include <QPoint>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QPointer>
#include <algorithm>

#include "ffmpeg_mov_viewport.h"
#include "tlrender_player.h"
#include "mpv_viewport.h"


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

    void prepareForMpvPlayback();

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
    bool frameViewEnabled() const;
    void zoomRelative(double factor);
    QRect displayedContentRect() const;

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

    /**
     * @brief Test-only access to the last raster-presented frame.
     */
    QImage currentRasterFrameForTest();
    qint64 rasterPresentationRevisionForTest() const { return m_presentationRevision; }

signals:
    void fpsChanged(double fps);
    void frameRendered();

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onMediaLoaded(const TLRenderPlayer::MediaInfo& info);
    void updateRasterFrame();

private:
    void ensureMpvViewport();
    void ensureFfmpegMovViewport();
    void ensureViewport();
    void setupViewportPlayer();
    void syncPresentationTimer();
    void syncBackendWidgetVisibility();
    bool useFfmpegMovViewport() const;
    bool useMpvViewport() const;

    QVBoxLayout* m_layout{nullptr};
    TLRenderPlayer* m_player{nullptr};
    MpvViewport* m_mpvViewport{nullptr};
    FFmpegMovViewport* m_ffmpegMovViewport{nullptr};

#ifdef HAVE_TLRENDER
    std::shared_ptr<ftk::Context> m_context;
    std::shared_ptr<ftk::Style> m_style;
    tl::qtwidget::Viewport* m_viewport{nullptr};
#endif
    QLabel* m_rasterLabel{nullptr};
    QTimer* m_presentationTimer = nullptr;


    bool m_isPanning{false};
    QPoint m_lastPanPos;
    double m_waylandZoom{1.0};
    qint64 m_presentationRevision{0};
    double m_mediaFps{24.0};
};

#endif // TLRENDER_VIEWPORT_H
