// gpu_player_autotest.cpp
//
// Bounded, headless console autotest for player_lab::GpuPlayer transport.
//
// This is a standalone Qt Core console executable, separate from
// gpu_player_demo. It exercises the transport surface (open/play/pause/
// resume/stop/step/scrub/loop) against media paths passed as argv and
// prints explicit PASS/FAIL/SKIP lines to stdout so a human or CI can
// observe results without any windowing system.
//
// Scope is intentionally narrow:
//   - Qt Core only (QCoreApplication). No Widgets, no OpenGL, no Multimedia.
//   - It never validates audio or visual presentation. For every path it
//     prints "AUDIO MANUAL" and "VISUAL MANUAL" so the limitation is explicit.
//   - It creates no files.
//
// Exit codes:
//   0  every non-SKIP check passed for every supplied path
//   2  at least one non-SKIP check failed for some path
//  64  no media paths were supplied on the command line
//
// Build: link this source with the existing player_lab sources (gpu_player.cpp,
// ffmpeg_gpu_decoder.cpp, player_types) plus Qt6::Core and the FFmpeg libs the
// decoder already depends on.

#include "ffmpeg_gpu_decoder.h"
#include "gpu_player.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QString>
#include <chrono>
#include <cmath>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

// Bounded event pump that also drains decoded output: process Qt events for
// up to `msec` wall-clock ms, pop whatever video frames the decode thread has
// produced, and drop decoded audio so atEnd() can become true in loop tests.
// The demo consumes both video and audio continuously; this helper is the
// headless stand-in for that consumption. Never sleeps indefinitely; the
// QElapsedTimer caps every spin.
void pumpAndDrain(int msec, player_lab::FFmpegGpuDecoder *dec, double &observedPts) {
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(msec)) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (dec) {
            while (auto frame = dec->tryPop()) {
                observedPts = frame->ptsSeconds;
            }
            while (dec->tryPopAudio()) {
            }
        }
    }
}

// Headless media-clock presentation clock, the analogue of
// GpuPlayerWidget's m_clockArmed/m_mediaPtsBase/m_wallBase triple. The clock
// is armed once from the front frame's PTS the first time it runs after
// play()/resume(); expectedPts = mediaPtsBase + wall-elapsed since then.
// Persisting this state across pumpByMediaClock() calls within one path keeps
// the clock monotonic across PLAY -> RESUME, exactly like the widget which
// re-seeds m_wallBase/m_mediaPtsBase only on play()/seek/scrub, not every
// paint.
struct MediaClock {
    bool armed = false;
    double mediaPtsBase = 0.0;
    std::chrono::steady_clock::time_point wallBase;
};

// Bounded event pump that consumes decoded VIDEO frames the same way the
// widget does: only frames whose PTS is <= the media-clock-expected PTS are
// popped, so a short clip is consumed at real source fps and is NOT drained
// to EOF during a sub-duration wall-clock pump window. This mirrors
// GpuPlayerWidget::pumpDueFrame(): seed the clock from the front frame on the
// first armed tick, then advance while frontPts() <= expectedPts, capping the
// per-tick drain so a stalled clock cannot spin. Decoded AUDIO is always fully
// drained (the widget does the same for audio) so decoder atEnd()/loop can
// still become true. Never sleeps indefinitely; the QElapsedTimer caps the
// outer spin.
void pumpByMediaClock(int msec, player_lab::FFmpegGpuDecoder *dec,
                      double &observedPts, MediaClock &clock) {
    QElapsedTimer t;
    t.start();
    const int kMaxDrainPerTick = 64; // matches GpuPlayerWidget::pumpDueFrame
    while (!t.hasExpired(msec)) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (!dec) {
            continue;
        }
        if (!clock.armed) {
            if (!dec->frontPtsValid()) {
                // Nothing decoded yet; arm on the next tick once a frame lands.
                while (dec->tryPopAudio()) {
                }
                continue;
            }
            clock.mediaPtsBase = dec->frontPts();
            clock.wallBase = std::chrono::steady_clock::now();
            clock.armed = true;
        }
        const double elapsed =
            std::chrono::duration<double>(
                std::chrono::steady_clock::now() - clock.wallBase)
                .count();
        const double expectedPts = clock.mediaPtsBase + elapsed;
        int drained = 0;
        while (drained < kMaxDrainPerTick && dec->frontPtsValid() &&
               dec->frontPts() <= expectedPts) {
            auto frame = dec->tryPop();
            if (!frame) {
                break;
            }
            observedPts = frame->ptsSeconds;
            ++drained;
        }
        while (dec->tryPopAudio()) {
        }
    }
}

// Bounded helper for step/scrub: wait for the first NEW video frame produced
// after an action (step/seek) and record THAT frame's PTS, instead of draining
// for a fixed time and reading the last frame. This is the headless analogue of
// "the frame the widget would actually display right after the action".
//
// `baselinePts` is the PTS captured immediately before the action (the last
// frame already consumed). Frames with a PTS equal to baseline are treated as
// stale leftovers and skipped. The first popped frame whose PTS differs from
// baseline is recorded into `outPts` and, if `observedPts` is non-null, mirrored
// there so the player's PTS provider sees it. Once captured, `outPts` is locked
// for the remainder of the window: subsequent frames keep being drained (and
// audio too, so atEnd() can still become true) but never overwrite the first
// post-action reading. Returns true if a first new frame was observed.
//
// `baselineSeekGen` (when non-null) additionally gates acceptance on the seek
// generation: only frames whose FramePacket::seekGeneration is STRICTLY GREATER
// than the captured baseline are accepted. Every step/seek action bumps the
// decoder's seek generation, so this rejects stale frames queued by a PRIOR
// seek that happen to carry a different PTS (the baseline-PTS check alone
// cannot detect those). Pass nullptr for actions that do not need a generation
// gate. `minSeekGen` is the baseline value to compare against (ignored when
// `baselineSeekGen` is nullptr).
bool waitForFirstNewFrame(int msec, player_lab::FFmpegGpuDecoder *dec,
                          double baselinePts, double &outPts,
                          double *observedPts = nullptr,
                          const uint64_t *baselineSeekGen = nullptr,
                          uint64_t minSeekGen = 0) {
    bool captured = false;
    QElapsedTimer t;
    t.start();
    while (!t.hasExpired(msec)) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (dec) {
            while (auto frame = dec->tryPop()) {
                if (!captured && frame->ptsSeconds != baselinePts &&
                    (!baselineSeekGen ||
                     frame->seekGeneration > minSeekGen)) {
                    outPts = frame->ptsSeconds;
                    if (observedPts) {
                        *observedPts = outPts;
                    }
                    captured = true;
                }
            }
            while (dec->tryPopAudio()) {
            }
        }
    }
    return captured;
}

// Per-path counters that slots mutate. Plain ints are fine: all slot delivery
// happens on the thread running pumpAndDrain (queued into the event loop),
// and we read them only after the bounded pump returns.
struct Counters {
    int playingChangedTrue = 0;
    int repaintRequested = 0;
    int looped = 0;
    int ended = 0;
};

void reset(Counters &c) {
    c.playingChangedTrue = 0;
    c.repaintRequested = 0;
    c.looped = 0;
    c.ended = 0;
}

// Print a result line: "[path] TAG STATUS [detail]".
// `detail` is an optional free-form suffix (may be null/empty).
void result(const QString &path, const char *tag, const char *status,
            const char *detail = nullptr) {
    if (detail && detail[0]) {
        std::printf("[%s] %s %s %s\n", qPrintable(path), tag, status, detail);
    } else {
        std::printf("[%s] %s %s\n", qPrintable(path), tag, status);
    }
    std::fflush(stdout);
}

// printf-style detail formatter into a fixed stack buffer.
// Returns a pointer to a static TLS-free per-call buffer; only call once per
// result() invocation (the result is consumed immediately).
const char *fmt(char *buf, size_t bufsz, const char *fmts, ...) {
    std::va_list ap;
    va_start(ap, fmts);
    std::vsnprintf(buf, bufsz, fmts, ap);
    va_end(ap);
    return buf;
}

// Run the full transport battery against one media path. Returns true only if
// every non-SKIP check passed.
bool testOnePath(const QString &path, bool requireHw) {
    player_lab::GpuPlayer player;
    double observedPts = -1.0; // headless stand-in for the widget's PTS
    MediaClock clock; // headless stand-in for GpuPlayerWidget's media clock
    bool allOk = true;

    Counters c;

    QObject::connect(&player, &player_lab::GpuPlayer::playingChanged,
                     [&c](bool playing) {
                         if (playing) {
                             ++c.playingChangedTrue;
                         }
                     });
    QObject::connect(&player, &player_lab::GpuPlayer::repaintRequested,
                     [&c]() { ++c.repaintRequested; });
    QObject::connect(&player, &player_lab::GpuPlayer::looped,
                     [&c]() { ++c.looped; });
    QObject::connect(&player, &player_lab::GpuPlayer::ended,
                     [&c]() { ++c.ended; });

    char buf[256];

    // --- OPEN ---
    if (requireHw) {
        player.decoder()->setRequireHardware(true);
    }
    const bool opened = player.load(path);
    if (!opened) {
        // Nothing else is meaningful without a successful load. Surface the
        // decoder's reason so a human/CI can tell an unsupported container
        // (e.g. MOV/MKV on an MP4-only build) from a real I/O failure. Under
        // --require-hw this is also how a CPU/software fallback is reported:
        // the decoder refuses to fall back, so OPEN FAIL with the hardware
        // error surfaces here and fails the run.
        result(path, "OPEN", "FAIL",
               fmt(buf, sizeof(buf), "error=%s",
                   player.decoder()->lastError().c_str()));
        return false;
    }
    result(path, "OPEN", "PASS");
 const char *pathName = player.decoder()->selectedPlaybackPathName();
 const bool pathOk = std::strcmp(pathName, "unknown") != 0 &&
 std::strcmp(pathName, "unsupported") != 0 &&
 std::strcmp(pathName, "mxf-unsupported") != 0;
 result(path, "PATH", pathOk ? "PASS" : "FAIL",
 fmt(buf, sizeof(buf), "name=%s", pathName));
 if (!pathOk) {
 return false;
 }


    // --- HW_DEVICE proof ---
    // Report the actually-selected decode device name (cuda/vaapi/software).
    // Under --require-hw a software selection is impossible (open() would
    // have failed above), so this PASSes. In default mode software is a valid
    // result and also PASSes — the line exists to make the device observable.
    const char *hwName = player.decoder()->selectedHwDeviceName();
    const int hwType = player.decoder()->selectedHwDeviceType();
    const bool hwOk = !requireHw || hwType >= 0;
    result(path, "HW_DEVICE", hwOk ? "PASS" : "FAIL",
           fmt(buf, sizeof(buf), "device=%s type=%d requireHw=%d",
               hwName, hwType, requireHw ? 1 : 0));
    if (!hwOk) {
        return false;
    }

    // Stand in for the absent widget: wire observedPts (the last PTS we
    // actually popped from the decoder) as the PTS source so currentPts()
    // reports a decoded frame, not the (widget-less) default of -1.
    player.setPtsProvider([&observedPts] { return observedPts; });

    const double duration = player.duration();
    const double fps = player.fps();
    std::printf("[%s] META duration=%.3f fps=%.3f pts=%.3f\n",
                qPrintable(path), duration, fps, player.currentPts());
    std::fflush(stdout);

    // --- PLAY ---
    reset(c);
    player.play();
    // Media-clock-gated pump: consume frames the way GpuPlayerWidget does
    // (only frames whose PTS is due by wall-clock media time), so a short
    // clip is NOT drained to EOF during this sub-duration window. The clock
    // seeds from the front frame on the first armed tick, exactly like
    // pumpDueFrame(). Give the presentation timer room to fire repaintRequested
    // and to emit the playing signal.
    pumpByMediaClock(400, player.decoder(), observedPts, clock);
    {
        const bool playingSignal = c.playingChangedTrue > 0;
        const bool playingState = player.isPlaying();
        const bool ok = playingSignal && playingState && (c.repaintRequested > 0);
        result(path, "PLAY", ok ? "PASS" : "FAIL",
               fmt(buf, sizeof(buf), "playSignal=%d isPlaying=%d repaints=%d",
                   c.playingChangedTrue, playingState ? 1 : 0, c.repaintRequested));
        if (!ok) {
            allOk = false;
        }
    }

    // --- PAUSE ---
    reset(c);
    player.pause();
    pumpByMediaClock(200, player.decoder(), observedPts, clock);
    {
        const bool ok = !player.isPlaying();
        result(path, "PAUSE", ok ? "PASS" : "FAIL",
               fmt(buf, sizeof(buf), "isPlaying=%d", player.isPlaying() ? 1 : 0));
        if (!ok) {
            allOk = false;
        }
    }

    // --- RESUME ---
    reset(c);
    player.play();
    // Re-arm the media clock from the current front frame (GpuPlayerWidget::
    // play() does the same by clearing m_clockArmed), then media-clock-gated
    // pump so resume does not drain a short clip to EOF.
    clock.armed = false;
    pumpByMediaClock(300, player.decoder(), observedPts, clock);
    {
        const bool ok = player.isPlaying() && (c.playingChangedTrue > 0);
        result(path, "RESUME", ok ? "PASS" : "FAIL",
               fmt(buf, sizeof(buf), "isPlaying=%d playSignal=%d",
                   player.isPlaying() ? 1 : 0, c.playingChangedTrue));
        if (!ok) {
            allOk = false;
        }
    }

    // --- STOP ---
    reset(c);
    player.stop();
    pumpByMediaClock(200, player.decoder(), observedPts, clock);
    {
        const bool ok = !player.isPlaying();
        result(path, "STOP", ok ? "PASS" : "FAIL",
               fmt(buf, sizeof(buf), "isPlaying=%d", player.isPlaying() ? 1 : 0));
        if (!ok) {
            allOk = false;
        }
    }

    // --- STEP FORWARD ---
    // Before stepping, drain any stale frames the decode thread queued ahead
    // of the playback position (e.g. pre-roll/keyframe frames left over from a
    // prior seek) so the baseline PTS reflects the frame actually consumed at
    // the pause position. Then stepForward() and wait for the FIRST new video
    // frame it produces (not the last frame drained over a fixed window), so
    // the observed PTS matches what the widget would display right after the
    // step. A genuine forward step must strictly increase PTS; a wrong-
    // direction or no-movement result is an explicit FAIL.
    {
        player.pause();
        pumpByMediaClock(300, player.decoder(), observedPts, clock);
        const double ptsBefore = player.currentPts();
        // Snapshot the seek generation BEFORE stepping. stepForward() seeks
        // internally, bumping the generation; only frames tagged with a
        // strictly newer generation are post-step output, not stale leftovers
        // from the previous seek that happen to carry a different PTS.
        const uint64_t genBefore = player.decoder()->seekGeneration();
        player.stepForward();
        double ptsAfter = ptsBefore;
        waitForFirstNewFrame(1200, player.decoder(), ptsBefore, ptsAfter,
                             &observedPts, &genBefore, genBefore);
        // currentPts() may be -1 (none) or negative on some sources. Treat an
        // unknown/negative PTS as a soft FAIL of this PTS-based check rather
        // than crashing: the transport call itself still happened.
        const bool ptsKnown = (ptsAfter >= 0.0) && (ptsBefore >= 0.0);
        const double expectedStep = player.fps() > 0.0 ? (1.0 / player.fps())
                                                       : (1.0 / 30.0);
        const double delta = ptsAfter - ptsBefore;
        const bool oneFrame = ptsKnown && delta > 0.0 &&
                              std::fabs(delta - expectedStep) <=
                                  (expectedStep * 0.55);
        result(path, "STEP_FORWARD", oneFrame ? "PASS" : "FAIL",
               fmt(buf, sizeof(buf),
                   "ptsBefore=%.3f ptsAfter=%.3f delta=%.3f expected=%.3f",
                   ptsBefore, ptsAfter, delta, expectedStep));
        if (!oneFrame) {
            allOk = false;
        }
    }
    // --- STEP BACKWARD ---
    // Same stale-frame drain and first-frame capture as STEP FORWARD: drain to
    // a stable baseline PTS, then wait for the FIRST new video frame the
    // backward step produces. A genuine backward step must strictly decrease
    // PTS; a wrong-direction or no-movement result is an explicit FAIL.
    {
        player.pause();
        pumpByMediaClock(300, player.decoder(), observedPts, clock);
        const double ptsBefore = player.currentPts();
        // Same generation gate as STEP FORWARD: stepBackward() seeks
        // internally, so only frames with a strictly newer seek generation are
        // accepted. This is what stops a no-op/forward read (decoder returning
        // the frame at/after a too-close target) from masquerading as the
        // backward step's result.
        const uint64_t genBefore = player.decoder()->seekGeneration();
        player.stepBackward();
        double ptsAfter = ptsBefore;
        waitForFirstNewFrame(1200, player.decoder(), ptsBefore, ptsAfter,
                             &observedPts, &genBefore, genBefore);
        const bool ptsKnown = (ptsAfter >= 0.0) && (ptsBefore >= 0.0);
        const double expectedStep = player.fps() > 0.0 ? (1.0 / player.fps())
                                                       : (1.0 / 30.0);
        const double delta = ptsBefore - ptsAfter;
        const bool oneFrame = ptsKnown && delta > 0.0 &&
                              std::fabs(delta - expectedStep) <=
                                  (expectedStep * 0.55);
        result(path, "STEP_BACKWARD", oneFrame ? "PASS" : "FAIL",
               fmt(buf, sizeof(buf),
                   "ptsBefore=%.3f ptsAfter=%.3f delta=%.3f expected=%.3f",
                   ptsBefore, ptsAfter, delta, expectedStep));
        if (!oneFrame) {
            allOk = false;
        }
    }

    // --- SCRUB / SEEK ---
    if (duration > 0.0) {
        const double t25 = duration * 0.25;
        const double t75 = duration * 0.75;

        // Seek to 25% and capture the FIRST post-seek video frame's PTS (not
        // the last frame drained over the window), so the reading matches what
        // the widget would display right after the scrub. Require it valid.
        const double pts25Baseline = observedPts;
        double pts25 = pts25Baseline;
        // Gate on the seek generation so a stale frame queued by an earlier
        // seek (different PTS, old generation) cannot be misread as the 25%
        // scrub result.
        const uint64_t gen25 = player.decoder()->seekGeneration();
        player.seek(t25);
        waitForFirstNewFrame(500, player.decoder(), pts25Baseline, pts25,
                             &observedPts, &gen25, gen25);
        const bool scrub25Ok = (pts25 >= 0.0);
        result(path, "SCRUB_25pct", scrub25Ok ? "PASS" : "FAIL",
               fmt(buf, sizeof(buf), "seek=%.3f pts=%.3f", t25, pts25));
        if (!scrub25Ok) {
            allOk = false;
        }
        const double pts75Baseline = pts25;
        double pts75 = pts75Baseline;
        // Same generation gate: only a frame tagged newer than the 75% seek's
        // baseline generation is accepted as the scrub result.
        const uint64_t gen75 = player.decoder()->seekGeneration();
        player.seek(t75);
        waitForFirstNewFrame(500, player.decoder(), pts75Baseline, pts75,
                             &observedPts, &gen75, gen75);
        const bool scrub75Ok =
            (pts75 >= 0.0) && (pts25 >= 0.0) && (pts75 > pts25);
        result(path, "SCRUB_75pct", scrub75Ok ? "PASS" : "FAIL",
               fmt(buf, sizeof(buf), "seek=%.3f pts=%.3f advanced=%s", t75,
                   pts75, scrub75Ok ? "yes" : "no"));
        if (!scrub75Ok) {
            allOk = false;
        }
    } else {
        result(path, "SCRUB", "SKIP", "duration<=0");
    }

    // --- LOOP ---
    if (duration > 0.0) {
        // Stop any in-flight playback, enable looping, park near EOF, and play
        // for a bounded window. A correctly looping source must emit looped()
        // and must NOT emit ended().
        player.stop();
        pumpAndDrain(150, player.decoder(), observedPts);

        player.setLooping(true);
        double nearEof = duration - 0.15;
        if (nearEof < 0.0) {
            nearEof = 0.0;
        }
        player.seek(nearEof);
        // Drain once to settle the seek and observe the actual post-seek PTS.
        // Seeking to nearEof can land on the preceding keyframe, so the real
        // position may be noticeably before the target; measuring it lets us
        // size the wait to the genuine remaining duration instead of guessing.
        pumpAndDrain(300, player.decoder(), observedPts);
        const double loopPts = player.currentPts();

        reset(c);
        player.play();
        // Bounded wait for the decode loop to reach EOF, wrap, and emit
        // looped(). Sized from the observed post-seek PTS so we wait for the
        // real remaining duration plus a margin, capped to a finite ceiling so
        // we never block forever. Audio is always drained (so atEnd()/loop can
        // still become true); video is consumed via media-clock gating, the
        // same way GpuPlayerWidget presents, so the wrap reflects real
        // playback pacing and a short clip is not exhausted ahead of the
        // clock.
        const double remaining = (loopPts >= 0.0) ? (duration - loopPts) : 0.15;
        const double margin = 1.5; // seconds of slack for decode/wrap latency
        int loopWaitMsec = static_cast<int>((remaining + margin) * 1000.0);
        // Clamp: at least enough to observe a wrap on a near-EOF seek, at most
        // a sane finite ceiling so pathological durations cannot stall CI.
        // The floor is enlarged (5000 ms) because short clips parked just below
        // EOF still need a real window to reach decoder EOF and wrap after the
        // seek; the ceiling (10000 ms) keeps the wait finite and bounded.
        if (loopWaitMsec < 5000) {
            loopWaitMsec = 5000;
        }
        if (loopWaitMsec > 10000) {
            loopWaitMsec = 10000;
        }
        // Re-arm the media clock from the post-seek front frame (widget play()
        // clears m_clockArmed) so resume paces from the park position.
        clock.armed = false;
        pumpByMediaClock(loopWaitMsec, player.decoder(), observedPts, clock);

        const bool ok = (c.looped > 0) && (c.ended == 0);
        result(path, "LOOP", ok ? "PASS" : "FAIL",
               fmt(buf, sizeof(buf),
                   "looped=%d ended=%d isPlaying=%d looping=%d loopPts=%.3f waitMs=%d",
                   c.looped, c.ended, player.isPlaying() ? 1 : 0,
                   player.isLooping() ? 1 : 0, loopPts, loopWaitMsec));
        if (!ok) {
            allOk = false;
        }

        player.setLooping(false);
    } else {
        result(path, "LOOP", "SKIP", "duration<=0");
    }

    // Final cleanup: ensure the player is stopped before destruction.
    player.stop();
    pumpAndDrain(150, player.decoder(), observedPts);

    // --- EXPLICIT, NON-AUTOMATABLE OBSERVATIONS ---
    // Audio playback and visual presentation are out of scope for this
    // headless transport test. State the limitation explicitly, once per path.
    std::printf("[%s] AUDIO MANUAL\n", qPrintable(path));
    std::printf("[%s] VISUAL MANUAL\n", qPrintable(path));
    std::fflush(stdout);

    return allOk;
}

} // namespace

int main(int argc, char **argv) {
    // Optional --require-hw flag: when present, force hardware decode (no
    // software fallback) for every path so CUDA/vaapi availability is proven
    // — a CPU/software fallback makes the test fail (nonzero).
    bool requireHw = false;
    std::vector<QString> paths;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--require-hw") == 0) {
            requireHw = true;
            continue;
        }
        paths.emplace_back(QString::fromUtf8(argv[i]));
    }

    if (paths.empty()) {
        std::printf("usage: %s [--require-hw] <media-path> [<media-path> ...]\n",
                    argv[0]);
        std::printf("RESULT NO_PATHS FAIL\n");
        return 64;
    }

    QCoreApplication app(argc, argv);

    bool allOk = true;
    for (const QString &p : paths) {
        // testOnePath prints the per-path result lines. OPEN always runs as a
        // non-SKIP check, so at least one non-SKIP check runs per supplied path.
        if (!testOnePath(p, requireHw)) {
            allOk = false;
        }
    }

    std::printf("RESULT %s\n", allOk ? "PASS" : "FAIL");
    std::fflush(stdout);

    // Per contract: exit 0 only if all non-SKIP checks passed; 2 on any FAIL.
    return allOk ? 0 : 2;
}
