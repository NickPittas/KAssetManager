#include "gpu_player_widget.h"
#include "ffmpeg_gpu_decoder.h"
#include "gpu_player.h"
#include "media/player_lab_player.h"
#include <QOpenGLBuffer>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QSurfaceFormat>
#include <QTimer>

#include <cmath>

namespace player_lab {

static const char *kVertSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

static const char *kFragSrc = R"(
#version 330 core
in vec2 v_uv;
out vec4 frag;
uniform sampler2D u_tex;
void main() {
    frag = texture(u_tex, v_uv);
}
)";

GpuPlayerWidget::GpuPlayerWidget(QWidget *parent)
    : QOpenGLWidget(parent) {
    // Request a 3.3 core context.
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    setFormat(fmt);
    m_measuredFpsTimer.start();
}

GpuPlayerWidget::~GpuPlayerWidget() {
    // Detach from the player BEFORE GL teardown: disconnects all stored
    // signal connections (lambdas capturing `this`), clears the external
    // presenter provider, and cancels pending frame requests. Without this,
    // the player's lambdas would capture a destroyed `this` and crash on
    // the next signal emission.
    setPlayer(nullptr);

    // Tear down GL objects while the context is still current.
    if (m_glInitialized) {
        makeCurrent();
        m_texture.reset();
        m_vbo.reset();
        m_vao.reset();
        m_program.reset();
        doneCurrent();
    }
}

void GpuPlayerWidget::setDecoder(FFmpegGpuDecoder *decoder) {
    if (m_decoder != decoder) {
        // Changing media: any in-flight still-frame retry from the previous
        // file must not consume or display frames for the new one.
        cancelPendingFrameRequests();
    }
    m_decoder = decoder;
}

void GpuPlayerWidget::setOverlayText(const QString &text) {
    m_overlayText = text;
    update();
}

void GpuPlayerWidget::play() {
    // Arm the media clock: the next paintGL seeds the media base from the
    // front frame's PTS, then advances at source fps from there.
    m_clockArmed = false;
    m_playing = true;
    // Kick an immediate repaint so the clock seeds without waiting for the
    // next ~16 ms timer tick.
    update();
}

void GpuPlayerWidget::pause() {
    m_playing = false;
}

void GpuPlayerWidget::setPlayer(PlayerLabPlayer *player) {
    // Idempotent: no work if we're already attached to the requested player.
    // This prevents stacked duplicate connections when setPlayer() is called
    // repeatedly with the same player pointer.
    if (m_player == player) {
        return;
    }

    // Detach the previous player: disconnect every stored connection (so no
    // lambda capturing `this` stays live after this widget is destroyed),
    // clear the external-presenter hook so the player's raster path resumes
    // owning the decoder queue, and cancel any in-flight still-frame retries
    // from the previous media.
    if (m_player) {
        if (m_connRepaint) {
            disconnect(m_connRepaint);
            m_connRepaint = {};
        }
        if (m_connPlayingChanged) {
            disconnect(m_connPlayingChanged);
            m_connPlayingChanged = {};
        }
        if (m_connVideoFramesChanged) {
            disconnect(m_connVideoFramesChanged);
            m_connVideoFramesChanged = {};
        }
        m_player->setExternalVideoPresenterPtsProvider({});
        cancelPendingFrameRequests();
    }

    m_player = player;

    if (m_player) {
        if (auto *gp = m_player->gpuPlayer()) {
            if (auto *dec = gp->decoder()) {
                setDecoder(dec);
            }
            m_connRepaint = connect(gp, &player_lab::GpuPlayer::repaintRequested, this,
                    [this]() { update(); });
            m_connPlayingChanged = connect(gp, &player_lab::GpuPlayer::playingChanged, this,
                    [this](bool playing) {
                        if (playing) {
                            play();
                        } else {
                            pause();
                        }
                    });
        }
        // This widget now owns the decoder's video frame queue. Give the
        // player an on-screen PTS provider so its position/facade stays
        // correct without touching the queue itself.
        m_player->setExternalVideoPresenterPtsProvider([this]() { return currentPts(); });
        m_connVideoFramesChanged = connect(m_player, &PlayerLabPlayer::videoFramesChanged, this, [this]() {
            if (m_player &&
                m_player->playbackState() != PlayerLabPlayer::PlaybackState::Playing) {
                showOneFreshFrame();
            }
        });
    }

    update();
}

void GpuPlayerWidget::setFrameView(bool enabled) {
    m_frameView = enabled;
}

bool GpuPlayerWidget::frameViewEnabled() const {
    return m_frameView;
}

void GpuPlayerWidget::zoomRelative(double factor) {
    Q_UNUSED(factor);
}

QRect GpuPlayerWidget::displayedContentRect() const {
    if (m_frameWidth <= 0 || m_frameHeight <= 0) {
        return contentsRect();
    }
    const QSize frameSize(m_frameWidth, m_frameHeight);
    const QSize target = frameSize.scaled(contentsRect().size(),
                                          Qt::KeepAspectRatio);
    if (target.isEmpty()) {
        return contentsRect();
    }
    const QRect cr = contentsRect();
    const int x = cr.x() + (cr.width() - target.width()) / 2;
    const int y = cr.y() + (cr.height() - target.height()) / 2;
    return QRect(x, y, target.width(), target.height());
}

void GpuPlayerWidget::noteFrameRendered() {
    ++m_measuredFpsFrames;
    const qint64 elapsedMs = m_measuredFpsTimer.elapsed();
    if (elapsedMs > 500 && m_measuredFpsFrames > 1) {
        const double elapsedSeconds = static_cast<double>(elapsedMs) / 1000.0;
        m_measuredFps = static_cast<double>(m_measuredFpsFrames) / elapsedSeconds;
        m_measuredFpsTimer.start();
        m_measuredFpsFrames = 0;
        emit fpsChanged(m_measuredFps);
    }
    emit frameRendered();
}

void GpuPlayerWidget::cancelPendingFrameRequests() {
    // Bump the serial so any in-flight QTimer::singleShot retry lambda from
    // showOneFreshFrameForRequest() sees a stale requestSerial and returns
    // immediately, instead of consuming/previewing frames for new media.
    ++m_stillFrameRequestSerial;
    // Reset the still-frame retry counter so the next scrub/frame-step gets
    // a fresh retry budget.
    m_stillFrameRetryCount = 0;
    // Reset playback clock + presentation state so a new file starts clean.
    // GL objects (texture/VAO/VBO/shader) are only touched under the GL
    // context, so they are left in place; dimension/PTS tracking is reset
    // so displayedContentRect() and the aspect-fit quad reflect the new file
    // only once its first frame is actually presented.
    m_playing = false;
    m_clockArmed = false;
    m_mediaPtsBase = 0.0;
    m_currentPts = -1.0;
    m_frameWidth = 0;
    m_frameHeight = 0;
}

void GpuPlayerWidget::resetMeasuredFps() {
    // Reset the measured-paint-fps window so a new clip or scrub run does
    // not inherit stale frame counts/timers from the previous one. Only
    // normal playback (pumpDueFrame uploads) contributes to this metric,
    // but resetting on boundaries keeps the first reported fps accurate.
    m_measuredFpsFrames = 0;
    m_measuredFps = 0.0;
    m_measuredFpsTimer.start();
}

void GpuPlayerWidget::initializeGL() {
    initializeOpenGLFunctions();

    glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_program = std::make_unique<QOpenGLShaderProgram>();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertSrc);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc);
    m_program->link();
    m_program->bind();

    // Full-screen triangle pair (two triangles covering clip space), with UVs
    // matching. FFmpeg produces top-down RGBA (row 0 = top of the image), and
    // GL samples a texture so that t=0 is the first uploaded row. To render
    // the image right-side up we therefore map the bottom of the quad (y=-1)
    // to v=1 and the top (y=+1) to v=0.
    // positions (xy), uvs (xy). Covers [-1,1] x [-1,1].
    const float verts[] = {
        // x      y     u     v
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
    };

    m_vao = std::make_unique<QOpenGLVertexArrayObject>();
    m_vao->create();
    m_vao->bind();

    m_vbo = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    m_vbo->create();
    m_vbo->bind();
    m_vbo->allocate(verts, sizeof(verts));

    // location 0 = position (vec2), location 1 = uv (vec2)
    int posLoc = 0;
    int uvLoc = 1;
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void *>(0));
    glEnableVertexAttribArray(uvLoc);
    glVertexAttribPointer(uvLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void *>(2 * sizeof(float)));

    m_vao->release();
    m_vbo->release();
    m_program->release();

    m_glInitialized = true;
}
void GpuPlayerWidget::resizeGL(int w, int h) {
    std::fprintf(stderr, "[player_lab] resizeGL %dx%d\n", w, h);
    glViewport(0, 0, w, h);
}

void GpuPlayerWidget::ensureTexture(int width, int height) {
    if (m_texture && m_texWidth == width && m_texHeight == height) {
        return;
    }
    m_texture = std::make_unique<QOpenGLTexture>(
        QOpenGLTexture::Target2D);
    m_texture->setSize(width, height);
    m_texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
    m_texture->setMinificationFilter(QOpenGLTexture::Linear);
    m_texture->setMagnificationFilter(QOpenGLTexture::Linear);
    m_texture->setWrapMode(QOpenGLTexture::ClampToEdge);
    m_texture->allocateStorage();
    m_texWidth = width;
    m_texHeight = height;
}

void GpuPlayerWidget::pumpDueFrame() {
    if (!m_decoder || !m_playing) {
        return;
    }

    // Seed the media clock from the front frame the first time we run after
    // play(). This makes the base PTS equal to whatever PTS the stream is at
    // (handles non-zero start PTS cleanly).
    if (!m_clockArmed) {
        if (!m_decoder->frontPtsValid()) {
            // Nothing decoded yet; hold the current texture and try again.
            return;
        }
        m_mediaPtsBase = m_decoder->frontPts();
        m_wallBase = std::chrono::steady_clock::now();
        m_clockArmed = true;
    }

    // Expected media time: how far into the stream wall-clock says we are.
    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double>(now - m_wallBase).count();
    const double expectedPts = m_mediaPtsBase + elapsed;

    // Advance while the front frame is due. Drains stale frames so playback
    // self-corrects after a stall. Caps the drain to avoid spinning if the
    // clock ran far ahead (e.g. after a debugger pause).
    int drained = 0;
    const int kMaxDrainPerFrame = 64;
    while (drained < kMaxDrainPerFrame) {
        if (!m_decoder->frontPtsValid()) {
            break;
        }
        if (m_decoder->frontPts() > expectedPts) {
            break; // front frame not due yet; hold current texture
        }

        FramePacketPtr frame = m_decoder->tryPop();
        if (!frame) {
            break;
        }
        // Slice 2: drop stale frames whose generation predates a seek.
        // These slipped into the queue before serviceSeek() flushed; they
        // must never be displayed.
        if (frame->seekGeneration !=
            m_decoder->seekGeneration()) {
            ++drained;
            continue; // discard stale frame, keep draining
        }
        ensureTexture(frame->width, frame->height);
        m_texture->setData(
            QOpenGLTexture::RGBA,
            QOpenGLTexture::UInt8,
            frame->pixels.data());
        const GLenum uploadError = glGetError();
        Q_UNUSED(uploadError);
        m_frameWidth = frame->width;
        m_frameHeight = frame->height;
        m_currentPts = frame->ptsSeconds;
        ++m_frameCount;
        // Count only actual frame uploads, not every paint.
        noteFrameRendered();
        ++drained;
    }
}

void GpuPlayerWidget::showOneFreshFrame() {
    ++m_stillFrameRequestSerial;
    // Fresh request: give it a full retry budget.
    m_stillFrameRetryCount = 0;
    showOneFreshFrameForRequest(m_stillFrameRequestSerial);
}

void GpuPlayerWidget::showOneFreshFrameForRequest(quint64 requestSerial) {
    // Slice 2: called after a seek or frame-step. Pops and uploads exactly
    // one frame with the current seek generation, dropping stale frames.
    // Pauses media-clock advancement (we're showing a single still frame).
    if (requestSerial != m_stillFrameRequestSerial) {
        return;
    }
    m_playing = false;
    if (!m_decoder) {
        return;
    }
    // Drain stale frames and grab the first fresh one. If none is available
    // yet (seek still in flight), schedule a retry for this same request.
    const uint64_t gen = m_decoder->seekGeneration();
    bool got = false;
    int drained = 0;
    const int kMaxDrain = 64;
    while (drained < kMaxDrain) {
        FramePacketPtr frame = m_decoder->tryPop();
        if (!frame) {
            break;
        }
        if (frame->seekGeneration != gen) {
            ++drained;
            continue;
        }
        ensureTexture(frame->width, frame->height);
        m_texture->setData(
            QOpenGLTexture::RGBA,
            QOpenGLTexture::UInt8,
            frame->pixels.data());
        m_frameWidth = frame->width;
        m_frameHeight = frame->height;
        m_currentPts = frame->ptsSeconds;
        m_mediaPtsBase = frame->ptsSeconds;
        m_wallBase = std::chrono::steady_clock::now();
        m_clockArmed = true;
        ++m_frameCount;
        // This is a paused scrub/frame-step still frame, NOT normal
        // playback — do NOT count it toward the presented-fps metric.
        got = true;
        break;
    }
    update();
    // If no fresh frame arrived yet, the seek is still being serviced. The
    // captured requestSerial cancels stale retries after a newer scrub/step.
    // Cap retries so EOF or a stuck seek cannot rearm indefinitely. The cap
    // is generous (≈6 s at 30 ms intervals); latest-drag-wins is preserved
    // because each new showOneFreshFrame() bumps the serial and resets count.
    if (!got) {
        if (++m_stillFrameRetryCount >= kMaxStillFrameRetries) {
            return; // stop rearming; no frame will arrive (EOF/stuck seek)
        }
        QTimer::singleShot(30, this, [this, requestSerial] {
            showOneFreshFrameForRequest(requestSerial);
        });
    }
}

void GpuPlayerWidget::paintGL() {
    QPainter painter(this);

    painter.beginNativePainting();
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Advance to the next due frame (media-timed), then draw whatever is in
    // the texture. When paused or nothing is due, the previous frame stays.
    pumpDueFrame();

    if (m_program && m_vao && m_texture && m_vbo) {
        // Aspect-fit: map displayedContentRect() (the aspect-fit sub-rect of
        // the widget for the current frame) into NDC and rebuild the quad
        // geometry so the texture is drawn letterboxed rather than stretched
        // to the full widget. UVs are unchanged and preserve FFmpeg top-down
        // orientation (bottom of quad -> v=1, top -> v=0).
        const QRect r = displayedContentRect();
        const float fw = static_cast<float>(width());
        const float fh = static_cast<float>(height());
        const float x0 = (fw > 0.0f) ? (2.0f * static_cast<float>(r.left()) / fw - 1.0f) : -1.0f;
        const float x1 = (fw > 0.0f) ? (2.0f * static_cast<float>(r.right() + 1) / fw - 1.0f) : 1.0f;
        // Widget y is top-down; GL clip space y is up.
        const float y0 = (fh > 0.0f) ? (1.0f - 2.0f * static_cast<float>(r.bottom() + 1) / fh) : -1.0f;
        const float y1 = (fh > 0.0f) ? (1.0f - 2.0f * static_cast<float>(r.top()) / fh) : 1.0f;
        const float verts[] = {
            // x      y     u     v   (two triangles: CCW)
            x0, y0, 0.0f, 1.0f,
            x1, y0, 1.0f, 1.0f,
            x0, y1, 0.0f, 0.0f,
            x0, y1, 0.0f, 0.0f,
            x1, y0, 1.0f, 1.0f,
            x1, y1, 1.0f, 0.0f,
        };
        m_vbo->bind();
        m_vbo->write(0, verts, sizeof(verts));
        m_vbo->release();

        m_program->bind();
        m_vao->bind();
        m_texture->bind(0);
        m_program->setUniformValue("u_tex", 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        m_vao->release();
        m_program->release();
    }
    painter.endNativePainting();

    // --- Overlay via QPainter over the GL widget (same surface) ---
    if (!m_overlayText.isEmpty()) {
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        QFont font(QStringLiteral("Monospace"), 10);
        painter.setFont(font);

        QRectF box(10, 10, width() - 20, 60);
        painter.fillRect(box, QColor(0, 0, 0, 140));
        painter.setPen(QColor(255, 255, 255));
        painter.drawText(box, Qt::AlignLeft | Qt::AlignTop, m_overlayText);
    }

    // noteFrameRendered() is invoked only from pumpDueFrame() (normal
    // playback uploads), so fps reflects actual presented frames and
    // ignores paused scrub/frame-step stills.
}

} // namespace player_lab
