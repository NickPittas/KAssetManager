#include "player_lab_player.h"

#include "file_utils.h"

#include "gpu_player.h"
#include "ffmpeg_gpu_decoder.h"
#include "player_types.h"

#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QTimer>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <cstring>

#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}
#endif

bool PlayerLabPlayer::s_initialized = false;

PlayerLabPlayer::PlayerLabPlayer(QObject* parent)
    : QObject(parent)
{
    m_gpuPlayer = std::make_unique<player_lab::GpuPlayer>();

    // Reflect the player_lab transport/position onto the facade signals.
    connect(m_gpuPlayer.get(), &player_lab::GpuPlayer::playingChanged, this,
            [this](bool playing) {
                m_playbackState = playing ? PlaybackState::Playing
                                          : PlaybackState::Paused;
                emit playbackStateChanged(m_playbackState.load());
            });
    connect(m_gpuPlayer.get(), &player_lab::GpuPlayer::positionChanged, this,
            [this](double ptsSeconds) {
                publishPosition(ptsSeconds);
            });
    connect(m_gpuPlayer.get(), &player_lab::GpuPlayer::repaintRequested, this,
            [this]() {
                // Raster presentation is driven by PlayerLabViewport's
                // media-FPS timer. The GpuPlayer transport timer ticks at
                // ~60 Hz for the legacy GL presenter; forwarding every tick
                // here overloads the main thread and starves the real raster
                // viewer down to ~8-14 fps on 4K H.264. Only the external GL
                // presenter needs this repaint signal.
                if (m_externalVideoPresenterActive) {
                    emit videoFramesChanged();
                }
            });
    connect(m_gpuPlayer.get(), &player_lab::GpuPlayer::ended,
            this, &PlayerLabPlayer::endOfStream);
    connect(m_gpuPlayer.get(), &player_lab::GpuPlayer::looped, this,
            [this]() {
                // A loop boundary restarts from head; reset buffered audio and
                // the facade clock so the next presented frame seeds cleanly.
                resetAudio(true);
                restartAudioIfPlaying();
                m_clockArmed = false;
            });
    // PTS provider: report the external presenter's on-screen PTS when it owns
    // the queue; otherwise report the raster-presented frame's PTS.
    m_gpuPlayer->setPtsProvider([this]() {
        if (m_externalVideoPresenterActive && m_externalVideoPresenterPtsProvider) {
            return m_externalVideoPresenterPtsProvider();
        }
        return m_currentPtsSeconds;
    });
    m_gpuPlayer->setAudioPump([this]() { pumpAudio(); });
    m_gpuPlayer->setStepCallback([this]() {
        // StepCallback drives a one-frame pop. When the external presenter
        // owns the queue it must do that pop; we just notify it.
        if (!m_externalVideoPresenterActive) {
            requestFreshFrame();
        }
        emit videoFramesChanged();
    });
}

PlayerLabPlayer::~PlayerLabPlayer()
{
    unloadMedia();
}

void PlayerLabPlayer::initialize()
{
    s_initialized = true;
}

bool PlayerLabPlayer::isInitialized()
{
    return s_initialized;
}

void PlayerLabPlayer::loadMedia(const QString& filePath)
{
    const QString suffix = QFileInfo(filePath).suffix();
    if (!FileUtils::isVideoFile(suffix) || suffix.compare(QStringLiteral("mxf"), Qt::CaseInsensitive) == 0) {
        emit error(tr("Unsupported media type: %1").arg(filePath));
        return;
    }

    unloadMedia();
    m_currentPath = filePath;
    {
        QMutexLocker locker(&m_statsMutex);
        m_frameAcquisitionStats = FrameAcquisitionStats();
    }

    if (!m_gpuPlayer->load(filePath)) {
        const std::string& reason = m_gpuPlayer->decoder()->lastError();
        emit error(tr("Failed to open media: %1%2")
                       .arg(filePath)
                       .arg(reason.empty() ? QString()
                                           : QStringLiteral("\n") +
                                                 QString::fromStdString(reason)));
        m_currentPath.clear();
        return;
    }

    player_lab::FFmpegGpuDecoder* dec = m_gpuPlayer->decoder();

    MediaInfo info;
    info.width = m_gpuPlayer->videoWidth();
    info.height = m_gpuPlayer->videoHeight();
    info.fps = m_gpuPlayer->fps();
    const double durSeconds = m_gpuPlayer->duration();
    info.durationMs = static_cast<qint64>(durSeconds * 1000.0);
    info.totalFrames = (info.fps > 0.0)
                           ? static_cast<qint64>(durSeconds * info.fps + 0.5)
                           : 0;
    info.hasAudio = dec->hasAudio();
    info.audioChannels = dec->audioChannels();
    info.audioSampleRate = dec->audioSampleRate();

    m_mediaInfo = info;
    m_duration = info.durationMs;
    m_totalFrames = info.totalFrames;
    m_currentFrame = 0;
    m_position = 0;

    // Apply facade-side transport settings to the backend for this clip.
    m_gpuPlayer->setLooping(m_loopMode == LoopMode::Loop);

    emit mediaInfoReady(m_mediaInfo);
    emit durationChanged(m_duration.load());
    emit currentFrameChanged(0);

    setupAudio();

    if (!m_externalVideoPresenterActive) {
        m_gpuPlayer->seek(0.0);
        requestFreshFrame();
    }
}

void PlayerLabPlayer::unloadMedia()
{
    if (m_gpuPlayer) {
        m_gpuPlayer->stop();
    }
    resetAudio();
    m_currentFrameImage = QImage();
    m_currentFramePacket.reset();
    m_scaledFrameImage = QImage();
    m_scaledTargetSize = QSize();
    m_currentPtsSeconds = -1.0;
    m_mediaPtsBase = 0.0;
    m_clockArmed = false;
    m_pendingAudio.reset();
    m_pendingAudioOffset = 0;
    // Invalidate any in-flight raster fresh-frame retry (presentFreshFrame
    // ForRequest re-arms via QTimer::singleShot) so a stale callback from
    // the previous file cannot publish frames into the next one. The
    // external GL presenter provider is intentionally left attached: the
    // widget reattaches on the next load and its own serial guards it.
    ++m_freshFrameRequestSerial;

    m_currentPath.clear();
    m_playbackState = PlaybackState::Stopped;
    m_position = 0;
    m_duration = 0;
    m_currentFrame = 0;
    m_totalFrames = 0;
    m_mediaInfo = MediaInfo();
    QMutexLocker locker(&m_statsMutex);
    m_frameAcquisitionStats = FrameAcquisitionStats();
}

bool PlayerLabPlayer::hasMedia() const
{
    return m_gpuPlayer && m_gpuPlayer->decoder() && m_gpuPlayer->decoder()->isOpen();
}

QString PlayerLabPlayer::currentMediaPath() const
{
    return m_currentPath;
}

void PlayerLabPlayer::play()
{
    if (!hasMedia()) {
        return;
    }
    // Negative rates are unsupported by the backend (no reverse playback).
    // Keep the stored rate but play forward only.
    if (m_audioSink) {
        if (m_audioSink->state() == QAudio::StoppedState) {
            m_audioDevice = m_audioSink->start();
        } else {
            m_audioSink->resume();
        }
    } else {
        setupAudio();
    }
    m_gpuPlayer->play();
}

void PlayerLabPlayer::pause()
{
    if (!hasMedia()) {
        return;
    }
    m_gpuPlayer->pause();
    if (m_audioSink) {
        m_audioSink->suspend();
    }
}

void PlayerLabPlayer::stop()
{
    if (m_gpuPlayer) {
        m_gpuPlayer->stop();
    }
    resetAudio();
    m_currentFrameImage = QImage();
    m_currentFramePacket.reset();
    m_scaledFrameImage = QImage();
    m_scaledTargetSize = QSize();
    m_currentPtsSeconds = -1.0;
    m_mediaPtsBase = 0.0;
    m_clockArmed = false;
    m_pendingAudio.reset();
    m_pendingAudioOffset = 0;
    // Invalidate any in-flight raster fresh-frame retry so a stale callback
    // from before the stop cannot publish frames after state was reset.
    ++m_freshFrameRequestSerial;
    m_position = 0;
    m_playbackState = PlaybackState::Stopped;
    emit playbackStateChanged(PlaybackState::Stopped);
    emit positionChanged(0);
}

void PlayerLabPlayer::togglePlayback()
{
    if (m_playbackState == PlaybackState::Playing) {
        pause();
    } else {
        play();
    }
}

void PlayerLabPlayer::seek(qint64 positionMs)
{
    if (!hasMedia()) {
        return;
    }
    // Drop queued audio from the old timeline before the seek so it can't
    // keep playing from the old position while the new one is decoded.
    m_pendingAudio.reset();
    m_pendingAudioOffset = 0;
    if (m_audioSink) {
        m_audioSink->reset();
        m_audioDevice = nullptr;
    }
    m_gpuPlayer->seek(positionMs / 1000.0);
    m_clockArmed = false;
    m_mediaPtsBase = positionMs / 1000.0;
    // When an external GL presenter owns the queue, it must do the post-seek
    // frame pop; we only notify it (and raster listeners) via the signal.
    if (!m_externalVideoPresenterActive) {
        requestFreshFrame();
    } else {
        emit videoFramesChanged();
    }
    restartAudioIfPlaying();
}

void PlayerLabPlayer::seekAsync(qint64 positionMs)
{
    seek(positionMs);
}

void PlayerLabPlayer::seekToFrame(qint64 frameNumber)
{
    if (!hasMedia()) {
        return;
    }
    const double fpsVal = m_gpuPlayer->fps();
    if (fpsVal <= 0.0) {
        return;
    }
    seek(static_cast<qint64>(frameNumber / fpsVal * 1000.0));
}

void PlayerLabPlayer::setPlaybackRate(double rate)
{
    // Store the requested rate. Negative rates are kept as-is so callers can
    // observe what they asked for, but the backend does not attempt reverse
    // playback; play() always advances forward.
    m_playbackRate = qFuzzyIsNull(rate) ? 1.0 : rate;
}

double PlayerLabPlayer::playbackRate() const
{
    return m_playbackRate;
}

void PlayerLabPlayer::setLoopMode(LoopMode mode)
{
    m_loopMode = mode;
    if (m_gpuPlayer) {
        m_gpuPlayer->setLooping(mode == LoopMode::Loop);
    }
}

PlayerLabPlayer::LoopMode PlayerLabPlayer::loopMode() const
{
    return m_loopMode;
}

void PlayerLabPlayer::stepForward()
{
    if (hasMedia()) {
        m_gpuPlayer->stepForward();
    }
}

void PlayerLabPlayer::stepBackward()
{
    if (hasMedia()) {
        m_gpuPlayer->stepBackward();
    }
}

void PlayerLabPlayer::stepForwardBy(int frames)
{
    for (int i = 0; i < frames; ++i) {
        stepForward();
    }
}

void PlayerLabPlayer::stepBackwardBy(int frames)
{
    for (int i = 0; i < frames; ++i) {
        stepBackward();
    }
}

void PlayerLabPlayer::gotoStart()
{
    seek(0);
}

void PlayerLabPlayer::gotoEnd()
{
    if (!hasMedia()) {
        return;
    }
    seek(m_duration.load());
}

void PlayerLabPlayer::setVolume(float volume)
{
    m_volume = qBound(0.0f, volume, 1.0f);
    if (m_audioSink) {
        m_audioSink->setVolume(m_muted.load() ? 0.0f : m_volume.load());
    }
}

float PlayerLabPlayer::volume() const
{
    return m_volume;
}

void PlayerLabPlayer::setMuted(bool muted)
{
    m_muted = muted;
    if (m_audioSink) {
        m_audioSink->setVolume(muted ? 0.0f : m_volume.load());
    }
}

bool PlayerLabPlayer::isMuted() const
{
    return m_muted;
}

PlayerLabPlayer::PlaybackState PlayerLabPlayer::playbackState() const
{
    return m_playbackState;
}

qint64 PlayerLabPlayer::position() const
{
    return m_position;
}

qint64 PlayerLabPlayer::duration() const
{
    return m_duration;
}

qint64 PlayerLabPlayer::currentFrame() const
{
    return m_currentFrame;
}

qint64 PlayerLabPlayer::totalFrames() const
{
    return m_totalFrames;
}

PlayerLabPlayer::MediaInfo PlayerLabPlayer::mediaInfo() const
{
    return m_mediaInfo;
}

FFmpegMovPlayer* PlayerLabPlayer::ffmpegMovPlayer() const
{
    return nullptr;
}

player_lab::GpuPlayer* PlayerLabPlayer::gpuPlayer()
{
    return m_gpuPlayer.get();
}

void PlayerLabPlayer::setExternalVideoPresenterPtsProvider(std::function<double()> ptsProvider)
{
    m_externalVideoPresenterPtsProvider = std::move(ptsProvider);
    m_externalVideoPresenterActive = static_cast<bool>(m_externalVideoPresenterPtsProvider);
}

void PlayerLabPlayer::tick()
{
}

void PlayerLabPlayer::refreshCurrentFrame()
{
    emit videoFramesChanged();
}

QImage PlayerLabPlayer::getCurrentFrame(const QSize& targetSize)
{
    QElapsedTimer timer;
    timer.start();

    presentDueFrames();

    QImage result;
    if (targetSize.isValid() && !m_currentFrameImage.isNull()) {
        if (m_scaledTargetSize != targetSize || m_scaledFrameImage.isNull()) {
            const QSize fittedSize = m_currentFrameImage.size().scaled(
                targetSize, Qt::KeepAspectRatio);
            if (fittedSize == m_currentFrameImage.size()) {
                // The decoded raster already matches the display-fit size
                // (for example 3840x1608 MKV in a 3840-wide viewer). Do not
                // run QImage::scaled() just to produce the same dimensions;
                // make only the ownership copy needed for queued paints.
                m_scaledFrameImage = m_currentFrameImage.copy();
            } else {
                const Qt::TransformationMode transformMode =
                    (m_playbackState.load() == PlaybackState::Playing)
                        ? Qt::FastTransformation
                        : Qt::SmoothTransformation;
                // Return an owned display-sized image. m_currentFrameImage wraps the
                // decoder packet storage; handing that shallow image to the viewport
                // lets later packet pops invalidate pixels still queued for paint.
                m_scaledFrameImage = m_currentFrameImage.scaled(
                    targetSize, Qt::KeepAspectRatio, transformMode);
            }
            m_scaledTargetSize = targetSize;
        }
        result = m_scaledFrameImage;
    } else {
        // Invalidate scaled cache so next sized request re-rasters.
        m_scaledFrameImage = QImage();
        m_scaledTargetSize = QSize();
        result = m_currentFrameImage;
    }


    {
        QMutexLocker locker(&m_statsMutex);
        m_frameAcquisitionStats.lastGetCurrentFrameNs = timer.nsecsElapsed();
    }
    return result;
}

QImage PlayerLabPlayer::extractThumbnail(const QString& filePath, const QSize& targetSize, qint64 positionMs)
{
    if (filePath.isEmpty()) {
        return {};
    }

    const int outputWidth = qMax(1, targetSize.isValid() ? targetSize.width() : 320);
    const int outputHeight = qMax(1, targetSize.isValid() ? targetSize.height() : 180);
    const QString scaleFilter = QStringLiteral(
        "scale=%1:%2:force_original_aspect_ratio=decrease,"
        "pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black,"
        "format=rgba").arg(outputWidth).arg(outputHeight);

    QProcess ffmpeg;
    QStringList args;
    args << QStringLiteral("-v") << QStringLiteral("error");
    if (positionMs > 0) {
        args << QStringLiteral("-ss") << QString::number(positionMs / 1000.0, 'f', 3);
    }
    args << QStringLiteral("-i") << filePath
         << QStringLiteral("-vf") << scaleFilter
         << QStringLiteral("-frames:v") << QStringLiteral("1")
         << QStringLiteral("-f") << QStringLiteral("rawvideo")
         << QStringLiteral("-pix_fmt") << QStringLiteral("rgba")
         << QStringLiteral("pipe:1");

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("LD_LIBRARY_PATH"));
    ffmpeg.setProcessEnvironment(env);
    ffmpeg.start(QStringLiteral("/usr/bin/ffmpeg"), args, QIODevice::ReadOnly);
    if (!ffmpeg.waitForStarted(3000)) {
        qWarning() << "[PlayerLabPlayer] ffmpeg thumbnail process failed to start" << filePath;
        return {};
    }
    if (!ffmpeg.waitForFinished(15000)) {
        ffmpeg.kill();
        ffmpeg.waitForFinished(1000);
        qWarning() << "[PlayerLabPlayer] ffmpeg thumbnail process timed out" << filePath;
        return {};
    }
    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        qWarning() << "[PlayerLabPlayer] ffmpeg thumbnail process failed"
                   << filePath << ffmpeg.readAllStandardError();
        return {};
    }

    const QByteArray frameData = ffmpeg.readAllStandardOutput();
    const qsizetype expectedSize = qsizetype(outputWidth) * qsizetype(outputHeight) * 4;
    if (frameData.size() != expectedSize) {
        qWarning() << "[PlayerLabPlayer] ffmpeg thumbnail produced unexpected byte count" << filePath;
        return {};
    }

    QImage image(outputWidth, outputHeight, QImage::Format_RGBA8888);
    if (image.isNull()) {
        return {};
    }
    std::memcpy(image.bits(), frameData.constData(), static_cast<size_t>(expectedSize));
    return image;
}

qint64 PlayerLabPlayer::queryDuration(const QString& filePath)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (filePath.isEmpty()) {
        return 0;
    }

    AVFormatContext* fmtCtx = nullptr;
    QByteArray localPath = QFile::encodeName(filePath);
    int ret = avformat_open_input(&fmtCtx, localPath.constData(), nullptr, nullptr);
    if (ret < 0 || !fmtCtx) {
        return 0;
    }

    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&fmtCtx);
        return 0;
    }

    qint64 durationMs = 0;
    if (fmtCtx->duration > 0) {
        durationMs = static_cast<qint64>((fmtCtx->duration * 1000.0) / AV_TIME_BASE);
    } else {
        const int vIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (vIdx >= 0) {
            AVStream* vs = fmtCtx->streams[vIdx];
            if (vs && vs->duration > 0) {
                durationMs = static_cast<qint64>(vs->duration * av_q2d(vs->time_base) * 1000.0);
            }
        }
    }

    avformat_close_input(&fmtCtx);
    return durationMs;
#else
    Q_UNUSED(filePath);
    return 0;
#endif
}

PlayerLabPlayer::FrameAcquisitionStats PlayerLabPlayer::frameAcquisitionStatsForTest() const
{
    QMutexLocker locker(&m_statsMutex);
    return m_frameAcquisitionStats;
}

void PlayerLabPlayer::resetFrameAcquisitionStatsForTest()
{
    QMutexLocker locker(&m_statsMutex);
    m_frameAcquisitionStats = FrameAcquisitionStats();
}

// ---------------------------------------------------------------------------
// Private helpers — audio + raster presentation (QImage-backed equivalent of
// player_lab::GpuPlayerWidget's GL presentation clock).
// ---------------------------------------------------------------------------

void PlayerLabPlayer::setupAudio()
{
    resetAudio();
    if (!m_gpuPlayer || !m_gpuPlayer->decoder() || !m_gpuPlayer->decoder()->hasAudio()) {
        return;
    }

    player_lab::FFmpegGpuDecoder* dec = m_gpuPlayer->decoder();
    QAudioFormat fmt;
    fmt.setSampleRate(dec->audioSampleRate());
    fmt.setChannelCount(dec->audioChannels());
    fmt.setSampleFormat(QAudioFormat::Int16);

    auto sink = std::make_unique<QAudioSink>(fmt);
    // Half a second of ring-buffer headroom to tolerate UI timer jitter.
    const qsizetype halfSecondBytes =
        static_cast<qsizetype>(std::max(1, dec->audioSampleRate()) *
                               std::max(1, dec->audioChannels()) *
                               static_cast<int>(sizeof(int16_t)) / 2);
    sink->setBufferSize(halfSecondBytes);
    m_audioDevice = sink->start();
    if (!m_audioDevice) {
        sink->stop();
        return;
    }
    sink->setVolume(m_muted.load() ? 0.0f : m_volume.load());
    m_audioSink = std::move(sink);
}

void PlayerLabPlayer::resetAudio(bool keepSuspended)
{
    m_pendingAudio.reset();
    m_pendingAudioOffset = 0;
    m_audioDevice = nullptr;
    if (m_audioSink) {
        m_audioSink->reset();
        if (keepSuspended) {
            m_audioSink->suspend();
        }
    }
    m_audioSink.reset();
}

void PlayerLabPlayer::restartAudioIfPlaying()
{
    if (m_playbackState != PlaybackState::Playing) {
        return;
    }
    if (!m_audioSink) {
        setupAudio();
        return;
    }
    m_audioDevice = m_audioSink->start();
    if (!m_audioDevice) {
        m_audioSink.reset();
    }
}

double PlayerLabPlayer::currentVideoPtsSeconds() const
{
    // External GL presenter (full viewer) owns the on-screen frame.
    if (m_externalVideoPresenterActive && m_externalVideoPresenterPtsProvider) {
        return m_externalVideoPresenterPtsProvider();
    }
    // Raster path: once a frame has been presented, its PTS is the best
    // on-screen signal. Before the first frame it is -1.
    if (m_currentPtsSeconds >= 0.0) {
        return m_currentPtsSeconds;
    }
    // No frame shown yet: fall back to the backend (decoder front / provider).
    if (m_gpuPlayer) {
        return m_gpuPlayer->currentPts();
    }
    return -1.0;
}

void PlayerLabPlayer::pumpAudio()
{
    if (!m_audioSink || !m_audioDevice || !m_gpuPlayer) {
        return;
    }
    player_lab::FFmpegGpuDecoder* dec = m_gpuPlayer->decoder();
    if (!dec) {
        return;
    }

    // --- A/V pacing threshold (seconds) ---
    // Audio older than this behind the displayed frame is stale (e.g. left
    // over after a seek/scrub). Drop it instead of playing a jump.
    constexpr double kAudioLagDrop = 0.250;

    // Bytes per second of the configured PCM (Int16, interleaved).
    const int sampleRate = std::max(1, dec->audioSampleRate());
    const int channels = std::max(1, dec->audioChannels());
    const double bytesPerSecond =
        static_cast<double>(sampleRate) * static_cast<double>(channels) *
        static_cast<int>(sizeof(int16_t));

    const uint64_t gen = dec->seekGeneration();
    qint64 freeBytes = m_audioSink->bytesFree();
    int popped = 0;
    constexpr int kMaxPacketsPerPump = 64;
    while (freeBytes > 0 && popped < kMaxPacketsPerPump) {
        if (!m_pendingAudio) {
            player_lab::AudioPacketPtr pkt = dec->tryPopAudio();
            if (!pkt) {
                break;
            }
            ++popped;
            if (pkt->seekGeneration != gen) {
                continue; // stale audio from before a seek
            }
            m_pendingAudio = std::move(pkt);
            m_pendingAudioOffset = 0;
        }

        // PTS of the audio we are about to write, accounting for any samples
        // already drained from the front of this packet.
        const double audioPts =
            m_pendingAudio->ptsSeconds +
            (bytesPerSecond > 0.0
                 ? static_cast<double>(m_pendingAudioOffset) / bytesPerSecond
                 : 0.0);
        const double videoPts = currentVideoPtsSeconds();
        if (videoPts >= 0.0) {
            const double lead = audioPts - videoPts;
            if (lead < -kAudioLagDrop) {
                // Stale (e.g. post-seek/scrub): drop the whole packet.
                m_pendingAudio.reset();
                m_pendingAudioOffset = 0;
                continue;
            }
        }

        const size_t remaining =
            m_pendingAudio->samples.size() - m_pendingAudioOffset;
        const qint64 toWrite =
            std::min<qint64>(freeBytes, static_cast<qint64>(remaining));
        const qint64 written = m_audioDevice->write(
            reinterpret_cast<const char*>(m_pendingAudio->samples.data() +
                                          m_pendingAudioOffset),
            toWrite);
        if (written <= 0) {
            break;
        }
        m_pendingAudioOffset += static_cast<size_t>(written);
        freeBytes -= written;
        if (m_pendingAudioOffset >= m_pendingAudio->samples.size()) {
            m_pendingAudio.reset();
            m_pendingAudioOffset = 0;
        }
    }
}

void PlayerLabPlayer::requestFreshFrame()
{
    ++m_freshFrameRequestSerial;
    presentFreshFrameForRequest(m_freshFrameRequestSerial);
}

void PlayerLabPlayer::presentFreshFrameForRequest(quint64 requestSerial)
{
    if (requestSerial != m_freshFrameRequestSerial || !m_gpuPlayer) {
        return;
    }
    player_lab::FFmpegGpuDecoder* dec = m_gpuPlayer->decoder();
    if (!dec) {
        return;
    }

    const uint64_t gen = dec->seekGeneration();
    int drained = 0;
    const int kMaxDrain = 64;
    while (drained < kMaxDrain) {
        player_lab::FramePacketPtr frame = dec->tryPop();
        if (!frame) {
            break;
        }
        if (frame->seekGeneration != gen) {
            ++drained;
            continue; // discard stale frame from before a seek/step
        }
        publishPresentedFrame(frame);
        emit videoFramesChanged();
        return;
    }
    QTimer::singleShot(30, this, [this, requestSerial]() {
        presentFreshFrameForRequest(requestSerial);
    });
}

void PlayerLabPlayer::presentDueFrames()
{
    if (!m_gpuPlayer || m_playbackState.load() != PlaybackState::Playing) {
        return;
    }
    player_lab::FFmpegGpuDecoder* dec = m_gpuPlayer->decoder();
    if (!dec) {
        return;
    }

    // Seed the media clock from the front frame on the first run after play().
    if (!m_clockArmed) {
        if (!dec->frontPtsValid()) {
            return;
        }
        m_mediaPtsBase = dec->frontPts();
        m_wallBase = std::chrono::steady_clock::now();
        m_clockArmed = true;
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsed =
        std::chrono::duration<double>(now - m_wallBase).count();
    const double expectedPts = m_mediaPtsBase + elapsed;

    int drained = 0;
    const int kMaxDrainPerFrame = 64;
    while (drained < kMaxDrainPerFrame) {
        if (!dec->frontPtsValid()) {
            break;
        }
        if (dec->frontPts() > expectedPts) {
            break; // front frame not due yet; hold current image
        }

        player_lab::FramePacketPtr frame = dec->tryPop();
        if (!frame) {
            break;
        }
        if (frame->seekGeneration != dec->seekGeneration()) {
            ++drained;
            continue; // discard stale frame
        }
        publishPresentedFrame(frame);
        ++drained;
    }
}

void PlayerLabPlayer::publishPresentedFrame(const player_lab::FramePacketPtr& frame)
{
    if (!frame || frame->pixels.empty()) {
        return;
    }
    // Keep the popped decoder frame alive and wrap its RGBA buffer directly.
    // Copying every 4K/ProRes frame here costs hundreds of MB/s on the UI
    // thread and was the dominant raster playback bottleneck. QImage does not
    // own this storage, so m_currentFramePacket must outlive both
    // m_currentFrameImage and any scaled cache derived from it.
    m_currentFramePacket = frame;
    m_currentFrameImage = QImage(m_currentFramePacket->pixels.data(),
                                 m_currentFramePacket->width,
                                 m_currentFramePacket->height,
                                 m_currentFramePacket->width * 4,
                                 QImage::Format_RGBA8888);
    m_currentPtsSeconds = frame->ptsSeconds;
    // Invalidate the scaled cache; the source changed.
    m_scaledFrameImage = QImage();
    m_scaledTargetSize = QSize();

    publishPosition(frame->ptsSeconds);
}

void PlayerLabPlayer::publishPosition(double ptsSeconds)
{
    if (ptsSeconds < 0.0) {
        return;
    }
    const qint64 positionMs = static_cast<qint64>(ptsSeconds * 1000.0);
    m_position = positionMs;

    const double fpsVal = m_mediaInfo.fps;
    if (fpsVal > 0.0) {
        const qint64 frameNumber = static_cast<qint64>(ptsSeconds * fpsVal + 0.5);
        if (frameNumber != m_currentFrame.load()) {
            m_currentFrame = frameNumber;
            emit currentFrameChanged(frameNumber);
        }
    }
    emit positionChanged(positionMs);
}
