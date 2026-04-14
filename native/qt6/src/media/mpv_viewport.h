#ifndef MPV_VIEWPORT_H
#define MPV_VIEWPORT_H

#include <QtCore/QRect>
#include <QtGui/QImage>
#include <QtQuick/QQuickFramebufferObject>
#include <QWidget>

#include "mpv_player.h"
#include "libmpv_headers.h"

class QQuickWidget;

// Forward-declare the renderer (defined in .cpp)
class MpvQuickRenderer;

/**
 * @brief QQuickFramebufferObject item that renders mpv frames via the render API.
 *
 * This is the Qt Quick equivalent of the old QOpenGLWindow-based MpvViewport.
 * It works on Wayland+NVIDIA because Qt Quick's scene graph manages GL contexts
 * correctly, unlike QOpenGLWidget/QOpenGLWindow.
 *
 * Pattern taken from KDE/mpvqt (MpvAbstractItem + MpvRenderer).
 */
class MpvQuickItem : public QQuickFramebufferObject
{
    Q_OBJECT

public:
    explicit MpvQuickItem(QQuickItem* parent = nullptr);
    ~MpvQuickItem() override;

    Renderer* createRenderer() const override;

    void setPlayer(MpvPlayer* player);
    MpvPlayer* player() const { return m_player; }

    mpv_handle* mpvHandle() const;

signals:
    void frameRendered();

private slots:
    void onWindowChanged(QQuickWindow* window);

private:
    friend class MpvQuickRenderer;
    static void onMpvRedraw(void* ctx);

    MpvPlayer* m_player{nullptr};
};

/**
 * @brief QWidget wrapper around a QQuickWidget hosting MpvQuickItem.
 *
 * This provides the same public interface as the old QOpenGLWindow-based
 * MpvViewport, so TLRenderViewport can use it as a drop-in replacement
 * without any QML files or QRC resources.
 */
class MpvViewport : public QWidget
{
    Q_OBJECT

public:
    explicit MpvViewport(QWidget* parent = nullptr);
    ~MpvViewport() override;

    void setPlayer(MpvPlayer* player);
    MpvPlayer* player() const;

    void setFrameView(bool enabled);
    bool frameViewEnabled() const { return m_frameViewEnabled; }
    void zoomRelative(double factor);
    QRect displayedContentRect() const;
    QImage currentFrameForTest();

signals:
    void frameRendered();

private:
    QQuickWidget* m_quickWidget{nullptr};
    MpvQuickItem* m_quickItem{nullptr};
    bool m_frameViewEnabled{true};
    double m_zoom{1.0};
};

#endif
