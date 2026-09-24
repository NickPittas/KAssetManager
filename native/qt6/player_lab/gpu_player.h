#pragma once

#include "player_types.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <memory>

namespace player_lab {

class FFmpegGpuDecoder;

// GpuPlayer is the small public facade for the standalone demo.
//
// It owns the decoder and a presentation timer that drives the
// QOpenGLWidget's repaint (which in turn pulls + uploads frames on the
// GL/UI thread). No GL objects live here.
//
// Presentation pacing lives in the viewport: this facade emits
// playingChanged(bool) so the viewport can arm/freeze its media clock, and
// the demo wires setPtsProvider() so the overlay PTS reflects the frame
// actually on screen.
//
// Slice 2 adds: seek-by-time, frame-step forward/backward, scrub
// coalescing (via the decoder's generation counter), and audio playback
// via QAudioSink (owned here, driven by a pull timer on the UI thread).
class GpuPlayer : public QObject {
    Q_OBJECT
public:
    explicit GpuPlayer(QObject *parent = nullptr);
    ~GpuPlayer() override;

    // Open a media file. Returns false on failure.
    bool load(const QString &path);

    // Transport.
    void setLooping(bool looping);
    bool isLooping() const { return m_looping; }
    void play();
    void pause();
    void stop();

    bool isPlaying() const { return m_playing; }

    // Accessors.
    int videoWidth() const;
    int videoHeight() const;
    double duration() const;   // seconds
    double currentPts() const; // seconds, -1 if none
    double fps() const;

    // --- Slice 2: seek by time ---
    // Seek to targetSeconds. Multiple rapid calls coalesce: only the newest
    // target survives (the decoder overwrites its seek target atomically and
    // bumps a generation counter that invalidates stale queued frames).
    void seek(double targetSeconds);

    // --- Slice 2: frame-step ---
    // Advance one displayed frame where codec timing allows. Pauses playback
    // if currently playing (frame-stepping is an inherently paused activity).
    void stepForward();
    // Step backward one frame: seeks to (currentPts - 1/fps) and displays
    // the resulting frame. No continuous reverse playback.
    void stepBackward();

    // The decoder, so the widget can pull frames. Ownership stays here.
    FFmpegGpuDecoder *decoder() const { return m_decoder.get(); }

    // PTS readback hook: the demo wires this to the widget's currentPts() so
    // the overlay reflects the frame actually on screen, not the queue front.
    void setPtsProvider(const std::function<double()> &provider) {
        m_ptsProvider = provider;
    }

    // Slice 2: audio pump hook. The demo wires this so the player can pull
    // decoded audio from the decoder and push it to the QAudioSink on each
    // timer tick without the player owning a Qt Multimedia type directly
    // (keeps the facade light; the demo owns the sink and format).
    void setAudioPump(const std::function<void()> &pump) {
        m_audioPump = pump;
    }

    // Slice 2: step-completed hook. Called after a stepForward/stepBackward
    // has produced a new frame so the viewport can re-arm its clock from the
    // new position and repaint.
    void setStepCallback(const std::function<void()> &cb) {
        m_stepCallback = cb;
    }

signals:
    void looped();
    void overlayChanged(const QString &text);
    void ended();
    // Emitted on play()/pause()/stop() transitions so the viewport can arm or
    // freeze its media-timed presentation clock.
    void playingChanged(bool playing);
    // Emitted by the playback timer so the viewport pumps/uploads video
    // frames independently of overlay text updates.
    void repaintRequested();
    // Emitted during playback ticks (and never when PTS is unknown/negative)
    // so the viewport can keep its timeline/scrubber in sync with the frame
    // actually on screen.
    void positionChanged(double ptsSeconds);

private slots:
    void onTick();

private:
    void updateOverlay();

    std::unique_ptr<FFmpegGpuDecoder> m_decoder;
    QTimer m_timer;
    bool m_playing = false;
    bool m_looping = false;
    // True between requesting a loop-restart seek and the decoder clearing
    // atEnd() (i.e. producing the first post-loop frame). Guards against
    // repeated looped()/seek(0.0) emissions across the EOF boundary while a
    // seek is already in flight or being serviced.
    bool m_loopRestartPending = false;
    std::function<double()> m_ptsProvider;
    std::function<void()> m_audioPump;
    std::function<void()> m_stepCallback;
    // Last timeline position requested by seek/step while paused. UI delivery
    // is asynchronous, so repeated step clicks must advance from the previous
    // requested display frame instead of rereading the not-yet-updated viewport
    // PTS and oscillating around the old paused frame.
    double m_requestedDisplayPts = -1.0;
};

} // namespace player_lab
