#include "ffmpeg_gpu_decoder.h"
#include "gpu_player.h"
#include "gpu_player_widget.h"

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QFileInfo>
#include <QAudioSink>
#include <QAudioFormat>
#include <QSurfaceFormat>
#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace player_lab;

int main(int argc, char *argv[]) {
    // QOpenGLWidget does not initialize on the current Wayland/EGL path on
    // this workstation, while the documented XCB/XWayland path initializes
    // and presents video correctly. Keep the lab overridable for platform
    // experiments, but default it to the working player surface.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("xcb"));
    }

    QSurfaceFormat glFormat;
    glFormat.setVersion(3, 3);
    glFormat.setProfile(QSurfaceFormat::CoreProfile);
    glFormat.setDepthBufferSize(24);
    glFormat.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(glFormat);

    QApplication app(argc, argv);

    // Parse a file path from argv[1], or open a dialog.
    QString path;
    if (argc >= 2) {
        path = QString::fromLocal8Bit(argv[1]);
    } else {
        path = QFileDialog::getOpenFileName(
            nullptr, QStringLiteral("Open MP4 file"), QString(),
            QStringLiteral("MP4 (*.mp4)"));
        if (path.isEmpty()) {
            return 0;
        }
    }

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("GPU Player Lab — Slice 2"));

    auto *central = new QWidget(&window);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *viewport = new GpuPlayerWidget(central);
    layout->addWidget(viewport, 1);

    auto *controls = new QWidget(central);
    auto *ctrlLayout = new QHBoxLayout(controls);
    auto *playBtn = new QPushButton(QStringLiteral("Play"), controls);
    auto *pauseBtn = new QPushButton(QStringLiteral("Pause"), controls);
    auto *stopBtn = new QPushButton(QStringLiteral("Stop"), controls);
    auto *loopCheck = new QCheckBox(QStringLiteral("Loop"), controls);
    // Slice 2: frame-step buttons.
    auto *stepBackBtn = new QPushButton(QStringLiteral("< Frame"), controls);
    auto *stepFwdBtn = new QPushButton(QStringLiteral("Frame >"), controls);
    auto *statusLabel = new QLabel(QStringLiteral("Idle"), controls);
    // Slice 2: seek slider (scrub). Range set after load when duration is known.
    auto *seekSlider = new QSlider(Qt::Horizontal, controls);
    seekSlider->setMinimum(0);
    seekSlider->setMaximum(1000);
    seekSlider->setValue(0);
    ctrlLayout->addWidget(playBtn);
    ctrlLayout->addWidget(pauseBtn);
    ctrlLayout->addWidget(stopBtn);
    ctrlLayout->addWidget(loopCheck);
    ctrlLayout->addWidget(stepBackBtn);
    ctrlLayout->addWidget(stepFwdBtn);
    ctrlLayout->addWidget(seekSlider, 1);
    ctrlLayout->addStretch(1);
    ctrlLayout->addWidget(statusLabel);
    layout->addWidget(controls);

    window.setCentralWidget(central);
    window.resize(960, 540);

    GpuPlayer player;

    viewport->setDecoder(player.decoder());

    // Arm/freeze the viewport's media-timed presentation clock on transport.
    QObject::connect(&player, &GpuPlayer::playingChanged, viewport,
                     [viewport](bool playing) {
                         if (playing) {
                             viewport->play();
                         } else {
                             viewport->pause();
                         }
                     });
    // Overlay PTS reflects the frame actually on screen, not the queue front.
    player.setPtsProvider([viewport]() { return viewport->currentPts(); });
    // Slice 2: after a frame-step, tell the viewport to pop+show one fresh
    // frame (re-arming the media clock from the new PTS).
    player.setStepCallback([viewport]() { viewport->showOneFreshFrame(); });

    QObject::connect(&player, &GpuPlayer::repaintRequested,
                     viewport, qOverload<>(&GpuPlayerWidget::update));

    QObject::connect(&player, &GpuPlayer::overlayChanged,
                     viewport, &GpuPlayerWidget::setOverlayText);
    // Overlay text change should also repaint the widget.
    QObject::connect(&player, &GpuPlayer::overlayChanged,
                     [&statusLabel](const QString &t) {
                         // Show a compact status line under the controls.
                         QStringList lines = t.split('\n');
                         statusLabel->setText(
                             lines.isEmpty() ? QStringLiteral("—") : lines.last());
                     });
    QObject::connect(&player, &GpuPlayer::ended,
                     [&statusLabel]() {
                         statusLabel->setText(QStringLiteral("Ended"));
                     });
    QObject::connect(&player, &GpuPlayer::looped,
                     [&statusLabel]() {
                         statusLabel->setText(QStringLiteral("Loop"));
                     });

    if (!player.load(path)) {
        const std::string &reason = player.decoder()->lastError();
        QString msg = QStringLiteral("Could not open:\n%1").arg(path);
        if (!reason.empty()) {
            msg += QStringLiteral("\n\n%1")
                       .arg(QString::fromStdString(reason));
        }
        std::fprintf(stderr, "Open failed: %s\n  %s\n",
                     path.toLocal8Bit().constData(), reason.c_str());
        QMessageBox::critical(
            nullptr, QStringLiteral("Open failed"), msg);
        return 2;
    }

    // Resize window to fit aspect if reasonable.
    int vw = player.videoWidth();
    int vh = player.videoHeight();
    if (vw > 0 && vh > 0) {
        // Scale to fit within 1280x720, preserving aspect.
        double scale = std::min(1280.0 / vw, 720.0 / vh);
        if (scale > 1.0) {
            scale = 1.0;
        }
        window.resize(static_cast<int>(vw * scale),
                      static_cast<int>(vh * scale) + controls->sizeHint().height());
    }

    // --- Slice 2: audio playback via QAudioSink ---
    // Set up a pull-style audio pump. The player's 16 ms timer calls the
    // pump, which pulls decoded PCM packets from the decoder and writes
    // them to the QAudioSink. On pause/stop the timer stops, so audio
    // naturally pauses; on stop the decoder flushes, so audio is cleared.
    std::unique_ptr<QAudioSink> audioSink;
    QIODevice *audioDevice = nullptr;
    if (player.decoder()->hasAudio()) {
        QAudioFormat fmt;
        fmt.setSampleRate(player.decoder()->audioSampleRate());
        fmt.setChannelCount(player.decoder()->audioChannels());
        fmt.setSampleFormat(QAudioFormat::Int16);
        audioSink = std::make_unique<QAudioSink>(fmt);
        // QAudioSink's QIODevice path is ring-buffered. Keep enough sink-side
        // space to tolerate UI timer jitter; writes below are still bounded by
        // bytesFree().
        const qsizetype halfSecondBytes =
            static_cast<qsizetype>(std::max(1, player.decoder()->audioSampleRate()) *
                                   std::max(1, player.decoder()->audioChannels()) *
                                   static_cast<int>(sizeof(int16_t)) / 2);
        audioSink->setBufferSize(halfSecondBytes);
        audioDevice = audioSink->start();
    }
    // The pump pulls audio packets tagged with the current seek generation
    // and writes only as much as QAudioSink says its ringbuffer can accept.
    // Partial writes are retained and resumed on the next tick; this avoids
    // dropping decoded PCM whenever the sink is briefly full.
    AudioPacketPtr pendingAudio;
    size_t pendingAudioOffset = 0;
    player.setAudioPump([&player, &audioSink, &audioDevice,
                         &pendingAudio, &pendingAudioOffset]() {
        if (!audioSink || !audioDevice) {
            return;
        }
        FFmpegGpuDecoder *dec = player.decoder();
        const uint64_t gen = dec->seekGeneration();
        qint64 freeBytes = audioSink->bytesFree();
        int popped = 0;
        constexpr int kMaxPacketsPerPump = 64;
        while (freeBytes > 0 && popped < kMaxPacketsPerPump) {
            if (!pendingAudio) {
                AudioPacketPtr pkt = dec->tryPopAudio();
                if (!pkt) {
                    break;
                }
                ++popped;
                if (pkt->seekGeneration != gen) {
                    continue; // stale audio from before a seek
                }
                pendingAudio = std::move(pkt);
                pendingAudioOffset = 0;
            }

            const size_t remaining =
                pendingAudio->samples.size() - pendingAudioOffset;
            const qint64 toWrite = std::min<qint64>(
                freeBytes, static_cast<qint64>(remaining));
            const qint64 written = audioDevice->write(
                reinterpret_cast<const char *>(pendingAudio->samples.data() +
                                               pendingAudioOffset),
                toWrite);
            if (written <= 0) {
                break;
            }
            pendingAudioOffset += static_cast<size_t>(written);
            freeBytes -= written;
            if (pendingAudioOffset >= pendingAudio->samples.size()) {
                pendingAudio.reset();
                pendingAudioOffset = 0;
            }
        }
    });

    // --- Slice 2: seek slider (scrub) ---
    // Coalescing is handled by the decoder: rapid slider drags overwrite
    // the seek target atomically; only the newest survives.
    const double dur = player.duration();
    QObject::connect(seekSlider, &QSlider::sliderMoved,
                     [&player, dur, &audioSink,
                      &pendingAudio, &pendingAudioOffset](int value) {
                         if (dur > 0.0) {
                             // Drop queued audio from the old timeline before
                             // the seek so it can't keep playing from the old
                             // position while the new one is being decoded.
                             pendingAudio.reset();
                             pendingAudioOffset = 0;
                             if (audioSink) {
                                 audioSink->reset();
                             }
                             player.seek(
                                 static_cast<double>(value) / 1000.0 * dur);
                         }
                     });

    // --- Timeline follow: reflect playback PTS on the slider while playing. ---
    // Don't fight the user: while the slider is being dragged, leave its value
    // alone. Guard the divide so a zero/unknown duration can't blow up.
    QObject::connect(&player, &GpuPlayer::positionChanged,
                     [seekSlider, dur](double pts) {
                         if (seekSlider->isSliderDown()) {
                             return;
                         }
                         if (!(dur > 0.0)) {
                             return;
                         }
                         double ratio = pts / dur;
                         if (ratio < 0.0) {
                             ratio = 0.0;
                         } else if (ratio > 1.0) {
                             ratio = 1.0;
                         }
                         const int v = static_cast<int>(
                             std::lround(ratio * 1000.0));
                         seekSlider->setValue(v);
                     });
    QObject::connect(loopCheck, &QCheckBox::toggled,
                     [&player](bool checked) {
        player.setLooping(checked);
    });
    QObject::connect(&player, &GpuPlayer::looped,
                     [&audioSink, &pendingAudio, &pendingAudioOffset]() {
        pendingAudio.reset();
        pendingAudioOffset = 0;
        if (audioSink) {
            audioSink->reset();
        }
    });


    QObject::connect(playBtn, &QPushButton::clicked,
                     [&player, &audioSink, &audioDevice,
                      &pendingAudio, &pendingAudioOffset]() {
        // Resume/rearm audio before kicking playback so MP4 sound continues
        // after pause, and so it can be restarted after stop (which reset()
        // leaves in StoppedState). Suspend keeps the device handle valid, so
        // resume() is enough there; a stopped sink needs a fresh start().
        if (audioSink) {
            if (audioSink->state() == QAudio::StoppedState) {
                // Clear stale buffered PCM from the old timeline before
                // reopening the sink so it can't bleed back in.
                pendingAudio.reset();
                pendingAudioOffset = 0;
                audioDevice = audioSink->start();
            } else {
                audioSink->resume();
            }
        }
        player.play();
    });
    QObject::connect(pauseBtn, &QPushButton::clicked,
                     [&player, &audioSink]() {
        // Suspend (not reset) so the QAudioSink and its device handle stay
        // valid and Play can resume() seamlessly. The pump is driven by the
        // player's timer, which pause() stops, so no more PCM is written.
        if (audioSink) {
            audioSink->suspend();
        }
        player.pause();
    });
    QObject::connect(stopBtn, &QPushButton::clicked,
                     [&player, &audioSink,
                      &pendingAudio, &pendingAudioOffset]() {
        // Drop queued audio so it can't keep playing from the old timeline
        // after stop.
        pendingAudio.reset();
        pendingAudioOffset = 0;
        if (audioSink) {
            audioSink->reset();
        }
        player.stop();
    });
    // Slice 2: frame-step buttons.
    QObject::connect(stepBackBtn, &QPushButton::clicked, [&player]() {
        player.stepBackward();
    });
    QObject::connect(stepFwdBtn, &QPushButton::clicked, [&player]() {
        player.stepForward();
    });

    window.show();

    // Auto-play on launch for quick visible-pixel proof.
    player.play();

    return app.exec();
}
