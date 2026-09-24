#include "gpu_player.h"
#include "ffmpeg_gpu_decoder.h"

#include <cmath>

namespace player_lab {

GpuPlayer::GpuPlayer(QObject *parent)
    : QObject(parent) {
    m_decoder = std::make_unique<FFmpegGpuDecoder>();

    // ~60 Hz presentation clock. Each tick triggers the widget's update(),
    // which pulls + uploads the next available frame on the GL thread.
    // Slice 2: also pumps audio on each tick.
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &GpuPlayer::onTick);
}

GpuPlayer::~GpuPlayer() {
    stop();
}

bool GpuPlayer::load(const QString &path) {
    stop();
    if (!m_decoder->open(path.toStdString())) {
        return false;
    }
    updateOverlay();
    m_requestedDisplayPts = -1.0;
    return true;
}

void GpuPlayer::setLooping(bool looping) {
    m_looping = looping;
}

void GpuPlayer::play() {
    if (!m_decoder->isOpen()) {
        return;
    }
    // play() drives its own EOF handling below; clear any loop-restart guard
    // so the next EOF boundary (re)arms cleanly rather than inheriting a
    // stale pending state from a prior onTick loop restart.
    m_loopRestartPending = false;
    if (m_decoder->atEnd()) {
        if (!m_looping) {
            return;
        }
        // Loop boundary restart driven by play() (e.g. the decoder already
        // reached atEnd before play() was called). Mirror the onTick()
        // guarded path: arm the restart guard so the boundary fires exactly
        // once, seek to head, and emit looped(). The guard was cleared on
        // entry, so this emits once even across repeated play() calls.
        if (!m_loopRestartPending) {
            m_loopRestartPending = true;
            emit looped();
        }
        m_decoder->seek(0.0);
    }
    m_playing = true;
    m_decoder->start();
    const double fpsVal = fps();
    const int intervalMs = (fpsVal > 0.0)
        ? std::max(1, static_cast<int>(std::round(1000.0 / fpsVal)))
        : 41;
    m_timer.setInterval(intervalMs);
    m_timer.start();
    emit playingChanged(true);
    m_requestedDisplayPts = -1.0;
}
void GpuPlayer::pause() {
    if (!m_playing) {
        return;
    }
    m_playing = false;
    m_timer.stop();
    emit playingChanged(false);
}

void GpuPlayer::stop() {
    m_playing = false;
    m_loopRestartPending = false;
    m_timer.stop();
    m_decoder->stop();
    emit playingChanged(false);
    m_requestedDisplayPts = -1.0;
}

double GpuPlayer::duration() const {
    return m_decoder->durationSeconds();
}

double GpuPlayer::currentPts() const {
    // Prefer the presented frame's PTS (read back from the viewport via the
    // provider the demo wired). Falls back to the decoder queue front when
    // no provider is set.
    if (m_ptsProvider) {
        return m_ptsProvider();
    }
    return m_decoder->frontPts();
}

int GpuPlayer::videoWidth() const {
    return m_decoder->videoWidth();
}

int GpuPlayer::videoHeight() const {
    return m_decoder->videoHeight();
}

double GpuPlayer::fps() const {
    return m_decoder->fps();
}

void GpuPlayer::onTick() {
    // Slice 2: pump audio to QAudioSink if the demo wired the hook.
    if (m_audioPump) {
        m_audioPump();
    }
    emit repaintRequested();
    updateOverlay();
    // Loop trigger. The canonical path is decoder EOF (atEnd()), but for
    // some containers (notably MP4/MOV) the decoder's atEnd() becomes true
    // late — only after trailing audio/metadata packets drain — so a loop
    // that waits for atEnd() stalls visibly at the final frame. Treat the
    // presented/current PTS reaching the end boundary as an additional
    // trigger: if looping is on, duration is known, and the current PTS is
    // within about one frame of duration, restart the loop now.
    //
    // m_loopRestartPending guards the *whole* restart sequence (both the
    // PTS-boundary and atEnd() triggers) so looped() + seek(0.0) fire
    // exactly once per boundary crossing. It clears once the decoder
    // resumes producing frames (the tail of this function).
    const double dur = duration();
    if (m_looping && dur > 0.0 && !m_loopRestartPending) {
        const double pts = currentPts();
        const double fpsVal = fps();
        const double frame = (fpsVal > 0.0) ? (1.0 / fpsVal) : (1.0 / 30.0);
        if (pts >= 0.0 && (dur - pts) <= 2.0 * frame + 1e-6) {
            // Same guarded restart path as EOF: seek head, keep playing,
            // emit looped() once, do NOT emit ended().
            m_loopRestartPending = true;
            m_playing = true;
            m_decoder->seek(0.0);
            m_decoder->start();
            emit looped();
            return;
        }
    }
    if (m_decoder->atEnd()) {
        if (m_looping) {
            // Loop restart on decoder EOF. Same guarded path as the
            // PTS-boundary trigger above; never emit
            // playingChanged(false)/ended() on the loop boundary.
            if (m_loopRestartPending) {
                return;
            }
            m_loopRestartPending = true;
            m_playing = true;
            m_decoder->seek(0.0);
            m_decoder->start();
            emit looped();
            return;
        }
        // Non-looping EOF: pause + ended, exactly once.
        m_loopRestartPending = false;
        pause();
        emit ended();
        return;
    }
    // Decoder is producing frames: a previously pending loop restart has
    // completed. Clear the guard so the next boundary can fire again.
    m_loopRestartPending = false;
    // Normal playback tick with a live frame: report the presented position so
    // the timeline/scrubber tracks the frame on screen. Skip when PTS is
    // unknown/negative (e.g. initial buffering).
    const double pts = currentPts();
    if (pts >= 0.0) {
        emit positionChanged(pts);
    }
}

void GpuPlayer::seek(double targetSeconds) {
    if (!m_decoder->isOpen()) {
        return;
    }
    // Pausing freezes the viewport clock; the decoder's seek() bumps the
    // generation counter and invalidates stale queued frames. If we were
    // playing, the decode loop is already running and will service the seek
    // on its next iteration; if paused, we start the decoder briefly so it
    // services the seek, then leave playback state to the caller.
    m_requestedDisplayPts = targetSeconds;
    m_decoder->seek(targetSeconds);
    m_decoder->start();
    // Paused scrubs (including frame-step) won't be repainted by the playback
    // timer, so route them through the step callback — which the demo wired to
    // GpuPlayerWidget::showOneFreshFrame() — to pull and display the freshly
    // seeked frame. While playing, the timer-driven repaint handles delivery.
    if (!m_playing && m_stepCallback) {
        m_stepCallback();
    }
}

void GpuPlayer::stepForward() {
    if (!m_decoder->isOpen()) {
        return;
    }
    // Frame-stepping implies paused presentation.
    if (m_playing) {
        pause();
    }
    // UI delivery is asynchronous. If the viewport has already displayed the
    // previous step request, continue from that displayed/requested PTS.
    // Otherwise use the frame currently reported by the viewport. The decoder
    // seek path already has preroll tolerance, so seek to the exact next frame
    // PTS; midpoint targets can be accepted as the current frame.
    const double fpsVal = fps();
    const double frame = (fpsVal > 0.0) ? (1.0 / fpsVal) : (1.0 / 30.0);
    const double visiblePts = currentPts();
    const bool requestedIsDisplayed =
        m_requestedDisplayPts >= 0.0 &&
        std::fabs(visiblePts - m_requestedDisplayPts) <= (frame * 0.55);
    const double basePts = requestedIsDisplayed ? m_requestedDisplayPts
                                                : visiblePts;
    double target = basePts + frame;
    const double dur = duration();
    if (dur > 0.0 && target > dur) {
        target = dur;
    }
    seek(target);
    // pause() above left m_playing false, so seek() routes this through the
    // step callback to display the freshly seeked frame (one callback per
    // action — no duplicate invocation here).
}


void GpuPlayer::stepBackward() {
    if (!m_decoder->isOpen()) {
        return;
    }
    if (m_playing) {
        pause();
    }
    // UI delivery is asynchronous. If another step/seek was requested before
    // the viewport consumed its fresh frame, step from that requested display
    // PTS instead of rereading the still-visible old frame. Once the viewport
    // has visibly advanced elsewhere, discard the old requested PTS.
    const double fpsVal = fps();
    const double frame = (fpsVal > 0.0) ? (1.0 / fpsVal) : (1.0 / 30.0);
    const double visiblePts = currentPts();
    const bool requestedStillCurrent =
        m_requestedDisplayPts >= 0.0 &&
        std::fabs(visiblePts - m_requestedDisplayPts) <= (frame * 0.55);
    const double basePts = requestedStillCurrent ? m_requestedDisplayPts
                                                 : visiblePts;
    double displayTarget = basePts - frame;
    if (displayTarget < 0.0) {
        displayTarget = 0.0;
    }
    m_requestedDisplayPts = displayTarget;
    const double target = displayTarget;
    seek(target);
    // pause() above left m_playing false, so seek() routes this through the
    // step callback to display the freshly seeked frame (one callback per
    // action — no duplicate invocation here).
}

void GpuPlayer::updateOverlay() {
    QString text;
    text = QStringLiteral("GPU Player Lab — Slice 2\n");
    text += QStringLiteral("%1x%2 @ %3 fps\n")
                .arg(videoWidth())
                .arg(videoHeight())
                .arg(fps(), 0, 'f', 2);
    text += QStringLiteral("Decode: %1\n")
                .arg(QString::fromUtf8(m_decoder->selectedHwDeviceName()));
    double pts = currentPts();
    if (pts >= 0.0) {
        text += QStringLiteral("PTS: %1s / %2s")
                    .arg(pts, 0, 'f', 3)
                    .arg(duration(), 0, 'f', 3);
    } else {
        text += QStringLiteral("(buffering...)");
    }
    text += m_playing ? QStringLiteral("  [PLAYING]") :
                        QStringLiteral("  [PAUSED]");
    emit overlayChanged(text);
}

} // namespace player_lab
