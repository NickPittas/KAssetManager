#pragma once

#include "player_types.h"

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>

#include <QElapsedTimer>
 #include <QObject>
#include <chrono>
#include <memory>

class QOpenGLShaderProgram;
class QOpenGLTexture;
class QOpenGLBuffer;
class QOpenGLVertexArrayObject;
class PlayerLabPlayer;

namespace player_lab {

class FFmpegGpuDecoder;

// QOpenGLWidget-based viewport.
//
// GL ownership boundary: this widget (created and used on the UI/GL thread)
// owns every GL object — texture, VAO/VBO, shader. The decode thread never
// touches GL; it hands off CPU-visible FramePacket objects.
//
// Presentation pacing: frame advancement is gated by media PTS against a
// wall-clock playback clock, NOT by the repaint cadence. The widget may
// repaint at 60 Hz, but it only pops + uploads the next decoded frame when
// that frame's PTS is due. This keeps playback at real source fps.
class GpuPlayerWidget :
    public QOpenGLWidget,
    protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit GpuPlayerWidget(QWidget *parent = nullptr);
    ~GpuPlayerWidget() override;

    // Attach a decoder. The widget does not own the decoder.
    void setDecoder(FFmpegGpuDecoder *decoder);

    // Overlay text shown in the top-left corner.
    void setOverlayText(const QString &text);

    // Transport control. play() arms the media clock so the next paintGL
    // advances frames at source fps; pause() freezes on the current frame.
    // Both are cheap and safe to call repeatedly.
    void play();
    void pause();

    // Slice 2: called after a seek or frame-step. Drops stale frames (whose
    // seek generation doesn't match the live one), pops the first fresh frame
    // available, uploads it, and re-arms the media clock from its PTS. Safe
    // to call repeatedly; repaints the widget.
    void showOneFreshFrame();

    // PTS (seconds) of the frame currently on screen, or -1 if none yet.
    double currentPts() const { return m_currentPts; }
    // --- PreviewOverlay display contract (mirrors PlayerLabViewport) ---
    // Attach a PlayerLabPlayer backend: wires the decoder, repaint clock, and
    // transport state into this widget so it can present via GL. The widget
    // does not own the player.
    void setPlayer(PlayerLabPlayer *player);

    void setFrameView(bool enabled);
    bool frameViewEnabled() const;
    void zoomRelative(double factor);
    // Aspect-fit rect of the displayed frame within this widget, based on the
    // current frame dimensions and contentsRect(). Falls back to contentsRect()
    // when no frame has been presented yet.
    QRect displayedContentRect() const;

    // Cancel any in-flight still-frame request retries (e.g. those scheduled
    // by showOneFreshFrameForRequest via QTimer::singleShot) and reset the
    // presentation/clock state so the widget is ready for a fresh media file.
    // GL objects are left intact (they are only touched under the GL context).
    // Call this when the decoder or player changes.
    void cancelPendingFrameRequests();

    // Currently attached PlayerLabPlayer (or null). Used by hosts to avoid
    // re-attaching the same player and stacking signal connections.
    PlayerLabPlayer *player() const { return m_player; }

    // Reset the measured-paint-fps counters. Call on scrub start or file
    // change so paused still-frame presentations do not contaminate the fps
    // metric from the previous clip/scrub run.
    void resetMeasuredFps();

signals:
    // Measured paint fps, emitted about every 500ms.
    void fpsChanged(double fps);
    // Emitted at the end of each paintGL after the frame has been drawn.
    void frameRendered();


protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    // Pop + upload frames whose PTS is due according to the media clock.
    // Called on the GL thread from paintGL. Leaves the most recent due frame
    // in the texture; holds the texture when nothing is due yet.
    void pumpDueFrame();
    void showOneFreshFrameForRequest(quint64 requestSerial);
    void ensureTexture(int width, int height);
    // Update measured-paint-fps counters and emit fpsChanged/frameRendered.
    void noteFrameRendered();

    FFmpegGpuDecoder *m_decoder = nullptr;

    std::unique_ptr<QOpenGLShaderProgram> m_program;
    std::unique_ptr<QOpenGLVertexArrayObject> m_vao;
    std::unique_ptr<QOpenGLBuffer> m_vbo;
    std::unique_ptr<QOpenGLTexture> m_texture;

    int m_texWidth = 0;
    int m_texHeight = 0;
    bool m_glInitialized = false;

    // Most recent frame's dimensions for aspect-correct drawing.
    int m_frameWidth = 0;
    int m_frameHeight = 0;

    QString m_overlayText;

    // Frame counter for overlay.
    quint64 m_frameCount = 0;

    // --- Media-timed presentation clock ---
    bool m_playing = false;
    // Wall-clock instant playback started (or resumed) from.
    std::chrono::steady_clock::time_point m_wallBase;
    // Media PTS (seconds) corresponding to m_wallBase. Until the first frame
    // is presented this is invalid; it is seeded from that frame's PTS.
    double m_mediaPtsBase = 0.0;
    bool m_clockArmed = false;
    // PTS (seconds) of the frame currently held in the texture.
    double m_currentPts = -1.0;
    // Monotonic still-frame request id. Scrub and frame-step can issue many
    // seeks before decoding completes; delayed retries for older requests must
    // not consume or display frames for newer requests.
    quint64 m_stillFrameRequestSerial = 0;

    // --- PreviewOverlay display-contract state ---
    PlayerLabPlayer *m_player = nullptr;
    bool m_frameView = true;
    // Measured paint fps (mirrors PlayerLabViewport::noteFrameRendered).
    QElapsedTimer m_measuredFpsTimer;
    int m_measuredFpsFrames = 0;
    double m_measuredFps = 0.0;

    // --- Signal connection ownership ---
    // Every connection created in setPlayer() is tracked so the destructor
    // and a repeated setPlayer() can explicitly disconnect them. This avoids
    // leaving live lambdas that capture a destroyed `this` after the widget
    // is gone, and prevents stacked duplicate connections when setPlayer()
    // is called repeatedly with the same player.
    QMetaObject::Connection m_connRepaint;
    QMetaObject::Connection m_connPlayingChanged;
    QMetaObject::Connection m_connVideoFramesChanged;

    // Cap for still-frame retry rearming in
    // showOneFreshFrameForRequest(). Without this, EOF or a stuck seek
    // keeps scheduling QTimer::singleShot retries forever.
    static constexpr int kMaxStillFrameRetries = 200;
    int m_stillFrameRetryCount = 0;
 };

} // namespace player_lab
