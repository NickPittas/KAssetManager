#include "ffmpeg_mov_player.h"

#include <QAudioOutput>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMediaPlayer>
#include <QMutexLocker>
#include <QThread>
#include <QUrl>

#include <cmath>
#include <iostream>

#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}
#endif

namespace {

constexpr double kDefaultFps = 24.0;
constexpr int kDecodeQueueFrames = 32;
constexpr int kPresentedHistoryFrames = 64;

#if defined(HAVE_FFMPEG) && HAVE_FFMPEG && defined(Q_OS_LINUX)
constexpr AVHWDeviceType kPreferredHwDeviceType = AV_HWDEVICE_TYPE_VAAPI;
#endif

double rationalToDouble(int num, int den)
{
    return (num > 0 && den > 0) ? (static_cast<double>(num) / static_cast<double>(den)) : 0.0;
}

#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
QString avErrorToQString(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = { 0 };
    av_strerror(errorCode, buffer, sizeof(buffer));
    return QString::fromLocal8Bit(buffer);
}

bool isTargetHardwareCodec(AVCodecID codecId)
{
    switch (codecId) {
    case AV_CODEC_ID_H264:
    case AV_CODEC_ID_HEVC:
        return true;
    default:
        return false;
    }
}
#endif

bool isRecoverableDecodeError(int errorCode)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    return errorCode < 0 && errorCode != AVERROR(EAGAIN) && errorCode != AVERROR_EOF;
#else
    Q_UNUSED(errorCode);
    return false;
#endif
}

} // namespace

FFmpegMovPlayer::FFmpegMovPlayer(QObject* parent)
    : QObject(parent)
{
    m_playbackTimer = new QChronoTimer(this);
    m_playbackTimer->setTimerType(Qt::PreciseTimer);
    m_playbackTimer->setSingleShot(true);
    connect(m_playbackTimer, &QChronoTimer::timeout, this, &FFmpegMovPlayer::onPlaybackTick);

    m_audioOutput = new QAudioOutput(this);
    m_audioPlayer = new QMediaPlayer(this);
    m_audioPlayer->setAudioOutput(m_audioOutput);
}

FFmpegMovPlayer::~FFmpegMovPlayer()
{
    unloadMedia();
}

void FFmpegMovPlayer::loadMedia(const QString& filePath)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    unloadMedia();

    QString errorString;
    if (!openMedia(filePath, &errorString, false)) {
        emit error(errorString.isEmpty() ? tr("Failed to open MOV media") : errorString);
        return;
    }

    m_currentPath = filePath;
    startDecodeThread();

    if (m_audioPlayer && m_mediaInfo.hasAudio) {
        m_audioPlayer->setSource(QUrl::fromLocalFile(filePath));
        m_audioPlayer->setPlaybackRate(std::abs(m_playbackRate));
        m_audioOutput->setVolume(m_volume);
        m_audioOutput->setMuted(m_muted);
    } else if (m_audioPlayer) {
        m_audioPlayer->stop();
        m_audioPlayer->setSource(QUrl());
    }

    emit durationChanged(m_durationMs);
    emit mediaInfoReady(m_mediaInfo);

    if (!seekInternal(0, true, true)) {
        emit error(tr("Failed to decode first frame from %1").arg(filePath));
        unloadMedia();
    }
#else
    Q_UNUSED(filePath);
    emit error(tr("FFmpeg MOV playback is unavailable in this build"));
#endif
}

void FFmpegMovPlayer::unloadMedia()
{
    m_playbackTimer->stop();
    if (m_audioPlayer) {
        m_audioPlayer->stop();
        m_audioPlayer->setSource(QUrl());
    }

    stopDecodeThread();
    closeMedia();

    m_mediaInfo = media_player::MediaInfo();
    m_currentPath.clear();
    m_positionMs = 0;
    m_durationMs = 0;
    m_currentFrame = 0;
    m_totalFrames = 0;
    m_lastPresentedPts = -1;
    clearPresentedHistory();
    clearBackwardFrames();
    m_playbackClock.invalidate();
    m_nextPlaybackDeadline = std::chrono::nanoseconds::zero();
    m_playbackStartPositionMs = 0;
    setPlaybackState(media_player::PlaybackState::Stopped);
    clearPresentedFrame();
}

bool FFmpegMovPlayer::hasMedia() const
{
    return m_formatContext && m_codecContext && m_videoStreamIndex >= 0;
}

void FFmpegMovPlayer::play()
{
    if (!hasMedia()) {
        return;
    }

    setPlaybackState(media_player::PlaybackState::Playing);
    resetPlaybackClock();
    if (m_audioPlayer && m_mediaInfo.hasAudio) {
        if (m_playbackRate < 0.0) {
            m_audioPlayer->pause();
        } else {
            syncAudioPosition(m_positionMs);
            m_audioPlayer->setPlaybackRate(std::abs(m_playbackRate));
            m_audioPlayer->play();
        }
    }
    updatePlaybackTimer();
}

void FFmpegMovPlayer::pause()
{
    if (!hasMedia()) {
        return;
    }

    m_playbackTimer->stop();
    m_playbackClock.invalidate();
    m_nextPlaybackDeadline = std::chrono::nanoseconds::zero();
    if (m_audioPlayer && m_mediaInfo.hasAudio) {
        m_audioPlayer->pause();
    }
    setPlaybackState(media_player::PlaybackState::Paused);
}

void FFmpegMovPlayer::stop()
{
    if (!hasMedia()) {
        return;
    }

    m_playbackTimer->stop();
    m_playbackClock.invalidate();
    m_nextPlaybackDeadline = std::chrono::nanoseconds::zero();
    if (m_audioPlayer && m_mediaInfo.hasAudio) {
        m_audioPlayer->stop();
    }
    setPlaybackState(media_player::PlaybackState::Stopped);
    seekInternal(0, true, true);
}

void FFmpegMovPlayer::seek(qint64 positionMs)
{
    if (!hasMedia()) {
        return;
    }
    seekInternal(positionMs, true, true);
}

void FFmpegMovPlayer::seekToFrame(qint64 frameNumber)
{
    if (!hasMedia()) {
        return;
    }

    seekToTimestampInternal(frameToTimestamp(frameNumber), true, false, true);
}

void FFmpegMovPlayer::stepForward()
{
    seekToFrame(m_currentFrame + 1);
}

void FFmpegMovPlayer::stepBackward()
{
    if (!hasMedia()) {
        return;
    }

    BufferedFrame previousFrame;
    if (popBackwardFrame(&previousFrame)) {
        presentBufferedFrame(previousFrame, true, false);
        return;
    }

    const int64_t targetTs = (m_lastPresentedPts != AV_NOPTS_VALUE && m_lastPresentedPts > 0)
        ? (m_lastPresentedPts - 1)
        : frameToTimestamp(clampFrameNumber(m_currentFrame - 1));
    seekToTimestampInternal(targetTs, true, true, false);
}

void FFmpegMovPlayer::setPlaybackRate(double rate)
{
    m_playbackRate = qFuzzyIsNull(rate) ? 1.0 : rate;
    if (m_audioPlayer && m_mediaInfo.hasAudio) {
        m_audioPlayer->setPlaybackRate(std::abs(m_playbackRate));
    }
    if (m_playbackState == media_player::PlaybackState::Playing) {
        resetPlaybackClock();
    }
    updatePlaybackTimer();
}

void FFmpegMovPlayer::setLoopMode(media_player::LoopMode mode)
{
    m_loopMode = mode;
}

void FFmpegMovPlayer::setVolume(float volume)
{
    m_volume = qBound(0.0f, volume, 1.0f);
    if (m_audioOutput) {
        m_audioOutput->setVolume(m_volume);
    }
}

void FFmpegMovPlayer::setMuted(bool muted)
{
    m_muted = muted;
    if (m_audioOutput) {
        m_audioOutput->setMuted(muted);
    }
}

QImage FFmpegMovPlayer::currentFrameImage() const
{
    QMutexLocker locker(&m_frameMutex);
    return m_currentFrameImage;
}

QImage FFmpegMovPlayer::currentFrameImage(const QSize& targetSize)
{
    QMutexLocker locker(&m_frameMutex);
    if (targetSize.isEmpty()) {
        m_requestedFrameTargetSize = QSize();
        return m_currentFrameImage;
    }
    if (!m_currentFrameScaledImage.isNull() && m_currentFrameScaledTargetSize == targetSize) {
        return m_currentFrameScaledImage;
    }

    m_requestedFrameTargetSize = targetSize;
    if (!m_currentFrameImage.isNull()) {
        m_currentFrameScaledImage = m_currentFrameImage.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        m_currentFrameScaledTargetSize = targetSize;
        return m_currentFrameScaledImage;
    }
    return m_currentFrameImage;
}

QSize FFmpegMovPlayer::currentFrameSize() const
{
    QMutexLocker locker(&m_frameMutex);
    return m_currentFrameImage.size();
}

void FFmpegMovPlayer::startDecodeThread()
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    stopDecodeThread();

    {
        std::lock_guard<std::mutex> lock(m_decodeMutex);
        m_decodedFrames.clear();
        m_seekResultFrame = BufferedFrame();
        m_decodeStopRequested = false;
        m_seekPending = false;
        m_seekInProgress = false;
        m_seekFailed = false;
        m_seekResultReady = false;
        m_decodeAtEnd = false;
        m_pendingSeekMs = 0;
        m_pendingSeekTs = 0;
        m_decodeGeneration = 0;
        m_pendingDecodeError.clear();
    }

    m_decodeThread = std::thread(&FFmpegMovPlayer::decodeThreadMain, this);
#endif
}

void FFmpegMovPlayer::stopDecodeThread()
{
    {
        std::lock_guard<std::mutex> lock(m_decodeMutex);
        m_decodeStopRequested = true;
        m_decodeCondition.notify_all();
    }

    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }

    std::lock_guard<std::mutex> lock(m_decodeMutex);
    m_decodedFrames.clear();
    m_seekResultFrame = BufferedFrame();
    m_decodeStopRequested = false;
    m_seekPending = false;
    m_seekInProgress = false;
    m_seekFailed = false;
    m_seekResultReady = false;
    m_decodeAtEnd = false;
    m_pendingSeekMs = 0;
    m_pendingSeekTs = 0;
    m_pendingDecodeError.clear();
}

void FFmpegMovPlayer::decodeThreadMain()
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    for (;;) {
        quint64 generation = 0;
        qint64 seekPositionMs = 0;
        int64_t seekTimestamp = 0;
        bool preferPreviousFrame = false;
        bool handleSeek = false;

        {
            std::unique_lock<std::mutex> lock(m_decodeMutex);
            m_decodeCondition.wait(lock, [this]() {
                return m_decodeStopRequested
                    || m_seekPending
                    || (!m_seekInProgress && hasMedia() && !m_decodeAtEnd
                        && m_decodedFrames.size() < static_cast<size_t>(decodeQueueCapacity()));
            });

            if (m_decodeStopRequested) {
                break;
            }

            if (m_seekPending) {
                handleSeek = true;
                seekPositionMs = m_pendingSeekMs;
                seekTimestamp = m_pendingSeekTs;
                preferPreviousFrame = m_pendingSeekPreferPreviousFrame;
                generation = m_decodeGeneration;
                m_seekPending = false;
                m_seekInProgress = true;
                m_seekFailed = false;
                m_seekResultReady = false;
                m_decodeAtEnd = false;
                m_pendingDecodeError.clear();
                m_decodedFrames.clear();
            } else {
                generation = m_decodeGeneration;
            }
        }

        if (handleSeek) {
            QString errorString;
            bool ok = false;
            const qint64 clampedMs = qBound<qint64>(0, seekPositionMs, qMax<qint64>(0, m_durationMs));
            const int64_t targetTs = seekTimestamp > 0 ? seekTimestamp : msToTimestamp(clampedMs);
            const int seekResult = av_seek_frame(m_formatContext, m_videoStreamIndex, targetTs, AVSEEK_FLAG_BACKWARD);
            if (seekResult >= 0) {
                avcodec_flush_buffers(m_codecContext);
                ok = decodeFrameForTimestamp(targetTs, clampedMs == 0, generation, preferPreviousFrame);
                if (!ok && errorString.isEmpty()) {
                    errorString = tr("Failed to decode seek target from %1").arg(m_currentPath);
                }
            } else {
                errorString = tr("Failed to seek %1: %2").arg(m_currentPath, avErrorToQString(seekResult));
            }

            std::lock_guard<std::mutex> lock(m_decodeMutex);
            if (generation == m_decodeGeneration) {
                if (!ok) {
                    m_seekFailed = true;
                    m_decodeAtEnd = true;
                    if (!errorString.isEmpty()) {
                        m_pendingDecodeError = errorString;
                    }
                }
                m_seekInProgress = false;
                m_decodeCondition.notify_all();
            }
            continue;
        }

        const bool ok = decodeNextFrame(generation);
        std::lock_guard<std::mutex> lock(m_decodeMutex);
        if (m_decodeStopRequested) {
            break;
        }
        if (generation != m_decodeGeneration || m_seekPending || m_seekInProgress) {
            continue;
        }
        if (!ok) {
            m_decodeAtEnd = true;
            m_decodeCondition.notify_all();
        }
    }
#endif
}

void FFmpegMovPlayer::onPlaybackTick()
{
    if (!hasMedia() || m_playbackState != media_player::PlaybackState::Playing) {
        return;
    }

    if (m_playbackRate < 0.0) {
        // Fast path: consume pre-buffered backward frames.
        BufferedFrame previousFrame;
        if (popBackwardFrame(&previousFrame)) {
            presentBufferedFrame(previousFrame, true, false);
            // If the backward buffer is nearly exhausted, kick off the next
            // backward seek NOW so it overlaps with the current frame's display
            // time instead of adding to it.
            postReverseSeekIfNeeded();
            updatePlaybackTimer();
            return;
        }

        // Slow path: backward buffer empty.  Check for a pending async seek result.
        BufferedFrame seekResult;
        bool hasSeekResult = false;
        bool seekActive = false;
        bool seekFailed = false;
        QString seekError;
        {
            std::lock_guard<std::mutex> lock(m_decodeMutex);
            if (m_seekResultReady) {
                hasSeekResult = true;
                seekResult = std::move(m_seekResultFrame);
                m_seekResultFrame = BufferedFrame();
                m_seekResultReady = false;
            } else if (m_seekFailed) {
                seekFailed = true;
                seekError = m_pendingDecodeError;
                m_pendingDecodeError.clear();
            } else {
                seekActive = m_seekPending || m_seekInProgress;
            }
        }

        if (hasSeekResult) {
            presentBufferedFrame(seekResult, true, false);
            // Immediately post the next reverse seek so it runs in parallel
            // with the upcoming frame display period.
            postReverseSeek();
            updatePlaybackTimer();
            return;
        }

        if (seekFailed) {
            if (!seekError.isEmpty()) {
                emit error(seekError);
            }
            // A failed seek during reverse means we are at/before the start.
            switch (m_loopMode) {
            case media_player::LoopMode::Loop:
                seekInternal(m_durationMs, true, true);
                resetPlaybackClock();
                updatePlaybackTimer();
                break;
            case media_player::LoopMode::PingPong:
                m_playbackRate = -m_playbackRate;
                resetPlaybackClock();
                updatePlaybackTimer();
                break;
            case media_player::LoopMode::Once:
            default:
                pause();
                emit endOfStream();
                break;
            }
            return;
        }

        if (seekActive) {
            // Seek still in progress; check back soon.
            m_playbackTimer->setInterval(std::chrono::milliseconds(1));
            m_playbackTimer->start();
            return;
        }

        // No seek active.  If we are at the very start, emit end-of-stream.
        if (m_currentFrame <= 0 || m_lastPresentedPts <= 0) {
            switch (m_loopMode) {
            case media_player::LoopMode::Loop:
                seekInternal(m_durationMs, true, true);
                resetPlaybackClock();
                updatePlaybackTimer();
                break;
            case media_player::LoopMode::PingPong:
                m_playbackRate = -m_playbackRate;
                resetPlaybackClock();
                updatePlaybackTimer();
                break;
            case media_player::LoopMode::Once:
            default:
                pause();
                emit endOfStream();
                break;
            }
            return;
        }

        // Post an async backward seek; timer will fire again in 1 ms to check it.
        postReverseSeek();
        m_playbackTimer->setInterval(std::chrono::milliseconds(1));
        m_playbackTimer->start();
        return;
    }

    const double elapsedMs = m_playbackClock.isValid()
        ? (static_cast<double>(m_playbackClock.nsecsElapsed()) / 1'000'000.0)
        : 0.0;
    const double fps = m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : kDefaultFps;
    const qint64 targetPositionMs = qBound<qint64>(
        0,
        m_playbackStartPositionMs + qRound64(static_cast<double>(elapsedMs) * m_playbackRate),
        qMax<qint64>(0, m_durationMs));
    BufferedFrame frame;
    bool reachedEndOfStream = false;
    bool presentedFrame = false;
    int catchUpFramesRemaining = decodeQueueCapacity();
    while (catchUpFramesRemaining-- > 0
           && takeBufferedFrameForPlayback(targetPositionMs, &frame, &reachedEndOfStream)) {
        presentBufferedFrame(frame, true);
        presentedFrame = true;
        if (frame.positionMs >= targetPositionMs) {
            break;
        }
    }

    if (presentedFrame) {
        if (m_mediaInfo.hasAudio) {
            syncAudioPosition(m_positionMs);
        }
        updatePlaybackTimer();
        return;
    }

    if (!reachedEndOfStream) {
        m_playbackTimer->setInterval(std::chrono::milliseconds(1));
        m_playbackTimer->start();
        return;
    }

    switch (m_loopMode) {
    case media_player::LoopMode::Loop:
        if (!seekInternal(0, true, true)) {
            pause();
            emit endOfStream();
        }
        break;
    case media_player::LoopMode::PingPong:
        m_playbackRate = -m_playbackRate;
        resetPlaybackClock();
        updatePlaybackTimer();
        break;
    case media_player::LoopMode::Once:
    default:
        pause();
        emit endOfStream();
        break;
    }
}

bool FFmpegMovPlayer::openMedia(const QString& filePath, QString* errorString, bool allowHardwareDecoding)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    static bool logLevelSet = false;
    if (!logLevelSet) {
        av_log_set_level(AV_LOG_ERROR);
        logLevelSet = true;
    }

    AVFormatContext* formatContext = nullptr;
    QByteArray localPath = QFile::encodeName(filePath);
    int ret = avformat_open_input(&formatContext, localPath.constData(), nullptr, nullptr);
    if (ret < 0 || !formatContext) {
        if (errorString) {
            *errorString = tr("Could not open %1").arg(filePath);
        }
        return false;
    }

    ret = avformat_find_stream_info(formatContext, nullptr);
    if (ret < 0) {
        avformat_close_input(&formatContext);
        if (errorString) {
            *errorString = tr("Could not read stream info from %1").arg(filePath);
        }
        return false;
    }

    const int videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        avformat_close_input(&formatContext);
        if (errorString) {
            *errorString = tr("No video stream found in %1").arg(filePath);
        }
        return false;
    }

    AVStream* videoStream = formatContext->streams[videoStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&formatContext);
        if (errorString) {
            *errorString = tr("Unsupported MOV codec in %1").arg(filePath);
        }
        return false;
    }

    AVCodecContext* codecContext = nullptr;
    AVBufferRef* hwDeviceContext = nullptr;
    bool hardwareDecodingActive = false;
    int hwPixelFormat = AV_PIX_FMT_NONE;

    auto freeCodecResources = [&]() {
        if (hwDeviceContext) {
            av_buffer_unref(&hwDeviceContext);
        }
        if (codecContext) {
            avcodec_free_context(&codecContext);
        }
    };

    auto openCodecContext = [&](bool useHardwareDecoding) -> bool {
        codecContext = avcodec_alloc_context3(codec);
        if (!codecContext) {
            if (errorString) {
                *errorString = tr("Could not allocate FFmpeg codec context");
            }
            return false;
        }

        ret = avcodec_parameters_to_context(codecContext, videoStream->codecpar);
        if (ret < 0) {
            if (errorString) {
                *errorString = tr("Could not initialize MOV decoder parameters for %1").arg(filePath);
            }
            freeCodecResources();
            return false;
        }

        if (!useHardwareDecoding) {
            codecContext->thread_count = qBound(1, QThread::idealThreadCount(), 16);
            codecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        }

#if defined(Q_OS_LINUX)
        if (useHardwareDecoding) {
            for (int i = 0;; ++i) {
                const AVCodecHWConfig* hwConfig = avcodec_get_hw_config(codec, i);
                if (!hwConfig) {
                    break;
                }
                if ((hwConfig->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
                    && hwConfig->device_type == kPreferredHwDeviceType) {
                    hwPixelFormat = hwConfig->pix_fmt;
                    break;
                }
            }

            if (hwPixelFormat == AV_PIX_FMT_NONE) {
                freeCodecResources();
                return false;
            }

            ret = av_hwdevice_ctx_create(&hwDeviceContext, kPreferredHwDeviceType, nullptr, nullptr, 0);
            if (ret < 0 || !hwDeviceContext) {
                qWarning() << "MOV hardware decode init failed:" << avErrorToQString(ret);
                freeCodecResources();
                return false;
            }

            codecContext->opaque = this;
            codecContext->get_format = &FFmpegMovPlayer::selectHardwarePixelFormat;
            codecContext->hw_device_ctx = av_buffer_ref(hwDeviceContext);
            if (!codecContext->hw_device_ctx) {
                freeCodecResources();
                return false;
            }
        }
#else
        Q_UNUSED(useHardwareDecoding);
#endif

        ret = avcodec_open2(codecContext, codec, nullptr);
        if (ret < 0) {
            if (useHardwareDecoding) {
                qWarning() << "MOV hardware decode open failed:" << avErrorToQString(ret);
            } else if (errorString) {
                *errorString = tr("Could not initialize MOV decoder for %1").arg(filePath);
            }
            freeCodecResources();
            return false;
        }

        hardwareDecodingActive = useHardwareDecoding;
        return true;
    };

    const bool tryHardwareDecoding = allowHardwareDecoding && isTargetHardwareCodec(videoStream->codecpar->codec_id);
    if (!(tryHardwareDecoding && openCodecContext(true)) && !openCodecContext(false)) {
        avformat_close_input(&formatContext);
        return false;
    }

    AVFrame* decodeFrame = av_frame_alloc();
    AVFrame* transferFrame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    if (!decodeFrame || !transferFrame || !packet) {
        if (decodeFrame) av_frame_free(&decodeFrame);
        if (transferFrame) av_frame_free(&transferFrame);
        if (packet) av_packet_free(&packet);
        freeCodecResources();
        avformat_close_input(&formatContext);
        if (errorString) {
            *errorString = tr("Could not allocate FFmpeg decode buffers");
        }
        return false;
    }

    closeMedia();
    m_formatContext = formatContext;
    m_codecContext = codecContext;
    m_hwDeviceContext = hwDeviceContext;
    m_decodeFrame = decodeFrame;
    m_transferFrame = transferFrame;
    m_packet = packet;
    m_videoStreamIndex = videoStreamIndex;
    const AVRational frameRate = (videoStream->avg_frame_rate.num > 0 && videoStream->avg_frame_rate.den > 0)
        ? videoStream->avg_frame_rate
        : av_guess_frame_rate(formatContext, videoStream, nullptr);
    m_timeBaseNum = videoStream->time_base.num;
    m_timeBaseDen = videoStream->time_base.den;
    m_frameRateNum = frameRate.num;
    m_frameRateDen = frameRate.den;
    m_hwPixelFormat = hwPixelFormat;
    m_swsSourceFormat = AV_PIX_FMT_NONE;
    m_swsWidth = 0;
    m_swsHeight = 0;
    m_hardwareDecodingActive = hardwareDecodingActive;

    const double guessedFps = rationalToDouble(m_frameRateNum, m_frameRateDen);
    const double fps = guessedFps > 0.0 ? guessedFps : kDefaultFps;
    const qint64 durationMs = formatContext->duration > 0
        ? static_cast<qint64>((formatContext->duration * 1000.0) / AV_TIME_BASE)
        : timestampToMs(videoStream->duration);
    const qint64 totalFrames = videoStream->nb_frames > 0
        ? static_cast<qint64>(videoStream->nb_frames)
        : qMax<qint64>(1, qRound64((durationMs / 1000.0) * fps));

    m_mediaInfo.width = codecContext->width;
    m_mediaInfo.height = codecContext->height;
    m_mediaInfo.fps = fps;
    m_mediaInfo.durationMs = durationMs;
    m_mediaInfo.totalFrames = totalFrames;
    m_mediaInfo.codec = QString::fromLatin1(codec->name);
    m_mediaInfo.hasAudio = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;
    m_durationMs = durationMs;
    m_totalFrames = totalFrames;
    return true;
#else
    Q_UNUSED(filePath);
    if (errorString) {
        *errorString = tr("FFmpeg support is unavailable");
    }
    return false;
#endif
}

void FFmpegMovPlayer::closeMedia()
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (m_packet) {
        av_packet_free(&m_packet);
    }
    if (m_decodeFrame) {
        av_frame_free(&m_decodeFrame);
    }
    if (m_transferFrame) {
        av_frame_free(&m_transferFrame);
    }
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }
    if (m_hwDeviceContext) {
        av_buffer_unref(&m_hwDeviceContext);
    }
    if (m_formatContext) {
        avformat_close_input(&m_formatContext);
    }
#endif
    m_videoStreamIndex = -1;
    m_timeBaseNum = 0;
    m_timeBaseDen = 1;
    m_frameRateNum = 0;
    m_frameRateDen = 1;
    m_hwPixelFormat = -1;
    m_swsSourceFormat = -1;
    m_swsWidth = 0;
    m_swsHeight = 0;
    m_hardwareDecodingActive = false;
}

void FFmpegMovPlayer::updatePlaybackTimer()
{
    if (!hasMedia() || m_playbackState != media_player::PlaybackState::Playing) {
        m_playbackTimer->stop();
        return;
    }

    const double fps = m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : kDefaultFps;
    const double rate = qMax(0.001, std::abs(m_playbackRate));
    const auto frameDurationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double, std::milli>(qMax(1.0, 1000.0 / (fps * rate))));
    if (!m_playbackClock.isValid()) {
        m_playbackClock.start();
        m_playbackStartPositionMs = m_positionMs;
    }
    if (m_nextPlaybackDeadline <= std::chrono::nanoseconds::zero()) {
        m_nextPlaybackDeadline = frameDurationNs;
    }

    const auto nowNs = std::chrono::nanoseconds(m_playbackClock.nsecsElapsed());
    const auto delay = std::max(std::chrono::nanoseconds::zero(), m_nextPlaybackDeadline - nowNs);
    m_playbackTimer->setInterval(delay);
    m_playbackTimer->start();
    m_nextPlaybackDeadline = std::max(m_nextPlaybackDeadline, nowNs) + frameDurationNs;
}

bool FFmpegMovPlayer::decodeNextFrame(quint64 generation)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!hasMedia()) {
        return false;
    }

    bool inputEof = false;
    bool packetPending = false;

    while (true) {
        int ret = 0;
        while ((ret = avcodec_receive_frame(m_codecContext, m_decodeFrame)) >= 0) {
            BufferedFrame frame;
            if (!presentDecodedFrame(m_decodeFrame, &frame)) {
                continue;
            }
            if (queueBufferedFrame(std::move(frame), generation, false)) {
                return true;
            }

            std::lock_guard<std::mutex> lock(m_decodeMutex);
            return generation != m_decodeGeneration || m_seekPending || m_decodeStopRequested;
        }

        if (ret == AVERROR_EOF) {
            return false;
        }
        if (ret != AVERROR(EAGAIN) && ret < 0) {
            if (m_hardwareDecodingActive && isRecoverableDecodeError(ret)) {
                return fallbackToSoftwareDecoding(m_positionMs, generation, false);
            }
            return false;
        }

        if (packetPending) {
            ret = avcodec_send_packet(m_codecContext, m_packet);
            if (ret == AVERROR(EAGAIN)) {
                continue;
            }
            av_packet_unref(m_packet);
            packetPending = false;
            if (ret < 0) {
                if (m_hardwareDecodingActive && isRecoverableDecodeError(ret)) {
                    return fallbackToSoftwareDecoding(m_positionMs, generation, false);
                }
                continue;
            }
            continue;
        }

        if (inputEof) {
            ret = avcodec_send_packet(m_codecContext, nullptr);
            if (ret == AVERROR(EAGAIN)) {
                continue;
            }
            if (m_hardwareDecodingActive && isRecoverableDecodeError(ret)) {
                return fallbackToSoftwareDecoding(m_positionMs, generation, false);
            }
            if (ret < 0) {
                return false;
            }
            inputEof = false;
            continue;
        }

        ret = av_read_frame(m_formatContext, m_packet);
        if (ret < 0) {
            inputEof = true;
            continue;
        }
        if (m_packet->stream_index != m_videoStreamIndex) {
            av_packet_unref(m_packet);
            continue;
        }
        packetPending = true;
    }
#else
    Q_UNUSED(generation);
#endif
    return false;
}

bool FFmpegMovPlayer::decodeFrameForTimestamp(int64_t targetTs, bool allowPastTarget, quint64 generation, bool preferPreviousFrame)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!hasMedia()) {
        return false;
    }

    bool inputEof = false;
    bool packetPending = false;
    BufferedFrame previousFrame;
    bool hasPreviousFrame = false;

    while (true) {
        int ret = 0;
        while ((ret = avcodec_receive_frame(m_codecContext, m_decodeFrame)) >= 0) {
            const int64_t pts = (m_decodeFrame->best_effort_timestamp != AV_NOPTS_VALUE)
                ? m_decodeFrame->best_effort_timestamp
                : m_decodeFrame->pts;

            BufferedFrame frame;
            if (!presentDecodedFrame(m_decodeFrame, &frame)) {
                continue;
            }

            if (preferPreviousFrame && pts != AV_NOPTS_VALUE) {
                if (pts <= targetTs) {
                    previousFrame = frame;
                    hasPreviousFrame = true;
                    continue;
                }

                if (hasPreviousFrame) {
                    return queueBufferedFrame(std::move(previousFrame), generation, true);
                }
            }

            if (!allowPastTarget && pts != AV_NOPTS_VALUE && pts < targetTs) {
                continue;
            }
            return queueBufferedFrame(std::move(frame), generation, true);
        }

        if (ret == AVERROR_EOF) {
            if (preferPreviousFrame && hasPreviousFrame) {
                return queueBufferedFrame(std::move(previousFrame), generation, true);
            }
            return false;
        }
        if (ret != AVERROR(EAGAIN) && ret < 0) {
            if (m_hardwareDecodingActive && isRecoverableDecodeError(ret)) {
                return fallbackToSoftwareDecoding(timestampToMs(targetTs), generation, true);
            }
            return false;
        }

        if (packetPending) {
            ret = avcodec_send_packet(m_codecContext, m_packet);
            if (ret == AVERROR(EAGAIN)) {
                continue;
            }
            av_packet_unref(m_packet);
            packetPending = false;
            if (ret < 0) {
                if (m_hardwareDecodingActive && isRecoverableDecodeError(ret)) {
                    return fallbackToSoftwareDecoding(timestampToMs(targetTs), generation, true);
                }
                continue;
            }
            continue;
        }

        if (inputEof) {
            ret = avcodec_send_packet(m_codecContext, nullptr);
            if (ret == AVERROR(EAGAIN)) {
                continue;
            }
            if (m_hardwareDecodingActive && isRecoverableDecodeError(ret)) {
                return fallbackToSoftwareDecoding(timestampToMs(targetTs), generation, true);
            }
            if (ret < 0) {
                return false;
            }
            inputEof = false;
            continue;
        }

        ret = av_read_frame(m_formatContext, m_packet);
        if (ret < 0) {
            inputEof = true;
            continue;
        }
        if (m_packet->stream_index != m_videoStreamIndex) {
            av_packet_unref(m_packet);
            continue;
        }
        packetPending = true;
    }
#else
    Q_UNUSED(targetTs);
    Q_UNUSED(allowPastTarget);
    Q_UNUSED(generation);
#endif
    return false;
}

bool FFmpegMovPlayer::seekInternal(qint64 positionMs, bool emitSignals, bool clearHistory)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!hasMedia()) {
        return false;
    }

    return seekToTimestampInternal(msToTimestamp(positionMs), emitSignals, false, clearHistory);
#else
    Q_UNUSED(positionMs);
    Q_UNUSED(emitSignals);
    return false;
#endif
}

bool FFmpegMovPlayer::seekToTimestampInternal(int64_t targetTs, bool emitSignals, bool preferPreviousFrame, bool clearHistory)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!hasMedia()) {
        return false;
    }

    const qint64 clampedMs = qBound<qint64>(0, timestampToMs(targetTs), qMax<qint64>(0, m_durationMs));
    const int64_t clampedTs = qMax<int64_t>(0, targetTs);
    const bool wasPlaying = m_playbackState == media_player::PlaybackState::Playing;
    m_playbackTimer->stop();
    if (clearHistory) {
        clearPresentedHistory();
        clearBackwardFrames();
    }

    quint64 generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_decodeMutex);
        ++m_decodeGeneration;
        generation = m_decodeGeneration;
        m_pendingSeekMs = clampedMs;
        m_pendingSeekTs = clampedTs;
        m_pendingSeekPreferPreviousFrame = preferPreviousFrame;
        m_seekPending = true;
        m_seekInProgress = false;
        m_seekFailed = false;
        m_seekResultReady = false;
        m_decodeAtEnd = false;
        m_pendingDecodeError.clear();
        m_decodedFrames.clear();
    }
    m_decodeCondition.notify_all();

    BufferedFrame frame;
    QString errorString;
    if (!waitForSeekFrame(generation, &frame, &errorString)) {
        if (!errorString.isEmpty()) {
            emit error(errorString);
        }
        return false;
    }

    presentBufferedFrame(frame, emitSignals, false);
    if (m_mediaInfo.hasAudio) {
        syncAudioPosition(m_positionMs);
    }

    if (wasPlaying) {
        resetPlaybackClock();
        updatePlaybackTimer();
    }
    return true;
#else
    Q_UNUSED(targetTs);
    Q_UNUSED(emitSignals);
    return false;
#endif
}

bool FFmpegMovPlayer::presentDecodedFrame(AVFrame* frame, BufferedFrame* bufferedFrame)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!frame || !bufferedFrame || !m_codecContext) {
        return false;
    }

    AVFrame* readableFrame = frame;
    if (m_hardwareDecodingActive && frame->format == m_hwPixelFormat) {
        if (!m_transferFrame) {
            m_transferFrame = av_frame_alloc();
            if (!m_transferFrame) {
                return false;
            }
        }
        av_frame_unref(m_transferFrame);
        const int ret = av_hwframe_transfer_data(m_transferFrame, frame, 0);
        if (ret < 0) {
            qWarning() << "MOV hardware decode transfer failed:" << avErrorToQString(ret);
            return false;
        }
        readableFrame = m_transferFrame;
    }

    if (!ensureConversionContext(readableFrame)) {
        return false;
    }

    const QSize frameSize(readableFrame->width, readableFrame->height);
    if (m_conversionScratchImage.size() != frameSize
        || m_conversionScratchImage.format() != QImage::Format_ARGB32) {
        m_conversionScratchImage = QImage(frameSize, QImage::Format_ARGB32);
    }
    if (m_conversionScratchImage.isNull()) {
        return false;
    }

    uint8_t* dstData[4] = { m_conversionScratchImage.bits(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { static_cast<int>(m_conversionScratchImage.bytesPerLine()), 0, 0, 0 };
    sws_scale(
        m_swsContext,
        readableFrame->data,
        readableFrame->linesize,
        0,
        readableFrame->height,
        dstData,
        dstLinesize);

    const int64_t pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE)
        ? frame->best_effort_timestamp
        : frame->pts;
    const double fps = m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : kDefaultFps;
    const qint64 fallbackPositionMs = m_lastPresentedPts != AV_NOPTS_VALUE && m_lastPresentedPts >= 0
        ? timestampToMs(m_lastPresentedPts) + qRound64(1000.0 / fps)
        : 0;
    const qint64 positionMs = pts != AV_NOPTS_VALUE ? timestampToMs(pts) : fallbackPositionMs;

    bufferedFrame->image = m_conversionScratchImage.copy();
    if (!bufferedFrame->image.isNull()) {
        QSize targetSize;
        {
            QMutexLocker locker(&m_frameMutex);
            targetSize = m_requestedFrameTargetSize;
        }
        if (targetSize.isValid()) {
            bufferedFrame->scaledImage = bufferedFrame->image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            bufferedFrame->scaledTargetSize = targetSize;
        }
    }
    bufferedFrame->positionMs = positionMs;
    bufferedFrame->frameNumber = timestampToFrame(pts, positionMs);
    bufferedFrame->pts = pts != AV_NOPTS_VALUE ? pts : 0;
    return true;
#else
    Q_UNUSED(frame);
    Q_UNUSED(bufferedFrame);
    return false;
#endif
}

bool FFmpegMovPlayer::queueBufferedFrame(BufferedFrame&& frame, quint64 generation, bool satisfySeek)
{
    std::unique_lock<std::mutex> lock(m_decodeMutex);
    if (m_decodeStopRequested || generation != m_decodeGeneration) {
        return false;
    }

    if (satisfySeek) {
        m_seekResultFrame = std::move(frame);
        m_seekResultReady = true;
        m_seekFailed = false;
        m_seekInProgress = false;
        m_decodeAtEnd = false;
        m_decodeCondition.notify_all();
        return true;
    }

    while (m_decodedFrames.size() >= static_cast<size_t>(decodeQueueCapacity())) {
        m_decodeCondition.wait(lock, [this, generation]() {
            return m_decodeStopRequested
                || generation != m_decodeGeneration
                || m_seekPending
                || m_seekInProgress
                || m_decodedFrames.size() < static_cast<size_t>(decodeQueueCapacity());
        });

        if (m_decodeStopRequested || generation != m_decodeGeneration || m_seekPending || m_seekInProgress) {
            return false;
        }
    }

    m_decodedFrames.emplace_back(std::move(frame));
    m_decodeAtEnd = false;
    m_decodeCondition.notify_all();
    return true;
}

bool FFmpegMovPlayer::waitForSeekFrame(quint64 generation, BufferedFrame* frame, QString* errorString)
{
    std::unique_lock<std::mutex> lock(m_decodeMutex);
    m_decodeCondition.wait(lock, [this, generation]() {
        return m_decodeStopRequested
            || generation != m_decodeGeneration
            || m_seekResultReady
            || m_seekFailed;
    });

    if (m_decodeStopRequested || generation != m_decodeGeneration) {
        if (errorString) {
            *errorString = tr("MOV seek was interrupted");
        }
        return false;
    }

    if (!m_seekResultReady) {
        if (errorString) {
            *errorString = m_pendingDecodeError.isEmpty()
                ? tr("Failed to decode MOV seek target")
                : m_pendingDecodeError;
        }
        return false;
    }

    if (frame) {
        *frame = m_seekResultFrame;
    }
    m_seekResultFrame = BufferedFrame();
    m_seekResultReady = false;
    return true;
}

bool FFmpegMovPlayer::takeBufferedFrameForPlayback(qint64 targetPositionMs, BufferedFrame* frame, bool* reachedEndOfStream)
{
    std::lock_guard<std::mutex> lock(m_decodeMutex);
    if (m_decodedFrames.empty()) {
        if (reachedEndOfStream) {
            *reachedEndOfStream = m_decodeAtEnd && !m_seekPending && !m_seekInProgress;
        }
        return false;
    }

    if (m_decodedFrames.front().positionMs > targetPositionMs) {
        if (reachedEndOfStream) {
            *reachedEndOfStream = false;
        }
        return false;
    }

    size_t selectedIndex = 0;
    while (selectedIndex + 1 < m_decodedFrames.size()
           && m_decodedFrames[selectedIndex + 1].positionMs <= targetPositionMs) {
        ++selectedIndex;
    }

    if (frame) {
        *frame = std::move(m_decodedFrames[selectedIndex]);
    }
    m_decodedFrames.erase(m_decodedFrames.begin(), m_decodedFrames.begin() + static_cast<std::ptrdiff_t>(selectedIndex + 1));
    if (reachedEndOfStream) {
        *reachedEndOfStream = false;
    }
    m_decodeCondition.notify_all();
    return true;
}

void FFmpegMovPlayer::presentBufferedFrame(const BufferedFrame& frame, bool emitSignals, bool pushToBackwardBuffer)
{
    rememberPresentedFrame(frame);
    if (pushToBackwardBuffer) {
        pushBackwardFrame(frame);
    }

    {
        QMutexLocker locker(&m_frameMutex);
        m_currentFrameImage = frame.image;
        m_currentFrameScaledImage = QImage();
        m_currentFrameScaledTargetSize = QSize();
    }

    m_lastPresentedPts = frame.pts;
    m_positionMs = frame.positionMs;
    m_currentFrame = frame.frameNumber;

    const qint64 positionMs = m_positionMs;
    const qint64 frameNumber = m_currentFrame;
    const qint64 pts = frame.pts;
    QMetaObject::invokeMethod(this,
                              [this, emitSignals, positionMs, frameNumber, pts]() {
                                  emit frameUpdated();
                                  emit debugFramePresented(frameNumber, positionMs, pts);
                                  if (emitSignals) {
                                      emit positionChanged(positionMs);
                                      emit currentFrameChanged(frameNumber);
                                  }
                              },
                              Qt::QueuedConnection);
}

int FFmpegMovPlayer::decodeQueueCapacity() const
{
    return kDecodeQueueFrames;
}

void FFmpegMovPlayer::rememberPresentedFrame(const BufferedFrame& frame)
{
    std::lock_guard<std::mutex> lock(m_decodeMutex);
    if (!m_presentedFrames.empty() && m_presentedFrames.back().pts == frame.pts) {
        return;
    }
    m_presentedFrames.push_back(frame);
    while (m_presentedFrames.size() > static_cast<size_t>(kPresentedHistoryFrames)) {
        m_presentedFrames.pop_front();
    }
}

bool FFmpegMovPlayer::takePreviousPresentedFrame(BufferedFrame* frame)
{
    std::lock_guard<std::mutex> lock(m_decodeMutex);
    if (m_presentedFrames.size() < 2) {
        return false;
    }

    m_presentedFrames.pop_back();
    if (frame) {
        *frame = m_presentedFrames.back();
    }
    return true;
}

void FFmpegMovPlayer::clearPresentedHistory()
{
    std::lock_guard<std::mutex> lock(m_decodeMutex);
    m_presentedFrames.clear();
}

void FFmpegMovPlayer::pushBackwardFrame(const BufferedFrame& frame)
{
    std::lock_guard<std::mutex> lock(m_decodeMutex);
    if (!m_backwardFrames.empty() && m_backwardFrames.back().pts == frame.pts) {
        return;
    }
    m_backwardFrames.push_back(frame);
    while (m_backwardFrames.size() > static_cast<size_t>(kPresentedHistoryFrames)) {
        m_backwardFrames.pop_front();
    }
}

bool FFmpegMovPlayer::popBackwardFrame(BufferedFrame* frame)
{
    std::lock_guard<std::mutex> lock(m_decodeMutex);
    if (m_backwardFrames.size() < 2) {
        return false;
    }

    m_backwardFrames.pop_back();
    if (frame) {
        *frame = m_backwardFrames.back();
    }
    return true;
}

void FFmpegMovPlayer::clearBackwardFrames()
{
    std::lock_guard<std::mutex> lock(m_decodeMutex);
    m_backwardFrames.clear();
}

void FFmpegMovPlayer::resetPlaybackClock()
{
    m_playbackClock.restart();
    m_playbackStartPositionMs = m_positionMs;
    const double fps = m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : kDefaultFps;
    const double rate = qMax(0.001, std::abs(m_playbackRate));
    m_nextPlaybackDeadline = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double, std::milli>(qMax(1.0, 1000.0 / (fps * rate))));
}

bool FFmpegMovPlayer::fallbackToSoftwareDecoding(qint64 restartPositionMs, quint64 generation, bool satisfySeek, QString* errorString)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!m_hardwareDecodingActive || m_currentPath.isEmpty()) {
        return false;
    }

    const QString currentPath = m_currentPath;
    closeMedia();

    QString localError;
    if (!openMedia(currentPath, &localError, false)) {
        if (errorString) {
            *errorString = localError;
        }
        return false;
    }

    const qint64 clampedMs = qBound<qint64>(0, restartPositionMs, qMax<qint64>(0, m_durationMs));
    const int64_t targetTs = msToTimestamp(clampedMs);
    const int seekResult = av_seek_frame(m_formatContext, m_videoStreamIndex, targetTs, AVSEEK_FLAG_BACKWARD);
    if (seekResult < 0) {
        localError = tr("Failed to resume MOV playback in software mode for %1").arg(currentPath);
        if (errorString) {
            *errorString = localError;
        }
        return false;
    }

    avcodec_flush_buffers(m_codecContext);
    const bool ok = satisfySeek
        ? decodeFrameForTimestamp(targetTs, clampedMs == 0, generation, clampedMs != 0)
        : decodeFrameForTimestamp(targetTs, true, generation, false);
    if (!ok && errorString) {
        *errorString = tr("Failed to resume MOV playback in software mode for %1").arg(currentPath);
    }

    qWarning() << "MOV hardware decode disabled, using software fallback for" << currentPath;
    return ok;
#else
    Q_UNUSED(restartPositionMs);
    Q_UNUSED(generation);
    Q_UNUSED(satisfySeek);
    Q_UNUSED(errorString);
    return false;
#endif
}

bool FFmpegMovPlayer::ensureConversionContext(AVFrame* frame)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!frame) {
        return false;
    }

    if (m_swsContext && m_swsSourceFormat == frame->format && m_swsWidth == frame->width && m_swsHeight == frame->height) {
        return true;
    }

    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }

    m_swsContext = sws_getContext(frame->width,
                                  frame->height,
                                  static_cast<AVPixelFormat>(frame->format),
                                  frame->width,
                                  frame->height,
                                  AV_PIX_FMT_BGRA,
                                  SWS_BILINEAR,
                                  nullptr,
                                  nullptr,
                                  nullptr);
    if (!m_swsContext) {
        return false;
    }

    m_swsSourceFormat = frame->format;
    m_swsWidth = frame->width;
    m_swsHeight = frame->height;
    return true;
#else
    Q_UNUSED(frame);
    return false;
#endif
}

AVPixelFormat FFmpegMovPlayer::selectHardwarePixelFormat(AVCodecContext* codecContext, const AVPixelFormat* pixelFormats)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    const auto* player = static_cast<const FFmpegMovPlayer*>(codecContext ? codecContext->opaque : nullptr);
    const AVPixelFormat preferredFormat = player ? static_cast<AVPixelFormat>(player->m_hwPixelFormat) : AV_PIX_FMT_NONE;

    if (pixelFormats) {
        for (const auto* format = pixelFormats; *format != AV_PIX_FMT_NONE; ++format) {
            if (*format == preferredFormat) {
                return *format;
            }
        }
        return pixelFormats[0];
    }
#else
    Q_UNUSED(codecContext);
    Q_UNUSED(pixelFormats);
#endif
    return AV_PIX_FMT_NONE;
}

// ---------------------------------------------------------------------------
// Async reverse-playback helpers
// ---------------------------------------------------------------------------

// Post a non-blocking seek to the frame immediately before the current
// presentation position.  The decode thread will produce the result in
// m_seekResultFrame / m_seekResultReady; the caller must NOT block waiting
// for it (use the async path in onPlaybackTick instead).
void FFmpegMovPlayer::postReverseSeek()
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!hasMedia()) {
        return;
    }

    // Already at the very beginning – nothing to seek to.
    if (m_currentFrame <= 0 && m_lastPresentedPts <= 0) {
        return;
    }

    const int64_t targetTs =
        (m_lastPresentedPts != AV_NOPTS_VALUE && m_lastPresentedPts > 0)
            ? (m_lastPresentedPts - 1)
            : frameToTimestamp(clampFrameNumber(m_currentFrame > 0 ? m_currentFrame - 1 : 0));

    std::lock_guard<std::mutex> lock(m_decodeMutex);
    if (m_seekPending || m_seekInProgress) {
        return; // Already seeking – the in-flight seek is good enough.
    }

    ++m_decodeGeneration;
    m_pendingSeekMs = timestampToMs(targetTs);
    m_pendingSeekTs = targetTs;
    m_pendingSeekPreferPreviousFrame = true;
    m_seekPending = true;
    m_seekInProgress = false;
    m_seekFailed = false;
    m_seekResultReady = false;
    m_decodeAtEnd = false;
    m_pendingDecodeError.clear();
    m_decodedFrames.clear();
    m_decodeCondition.notify_all();
#endif
}

// Post an async backward seek only when the backward buffer is nearly empty,
// so the seek overlaps with the current frame's display window.
void FFmpegMovPlayer::postReverseSeekIfNeeded()
{
    {
        std::lock_guard<std::mutex> lock(m_decodeMutex);
        if (m_backwardFrames.size() >= 2) {
            return; // Buffer still has frames – no prefetch needed yet.
        }
        if (m_seekPending || m_seekInProgress) {
            return; // Seek already in flight.
        }
    }
    postReverseSeek();
}

qint64 FFmpegMovPlayer::timestampToMs(int64_t pts) const
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (pts == AV_NOPTS_VALUE || m_timeBaseNum <= 0 || m_timeBaseDen <= 0) {
        return 0;
    }
    return qMax<qint64>(0, qRound64((pts * 1000.0 * m_timeBaseNum) / m_timeBaseDen));
#else
    Q_UNUSED(pts);
    return 0;
#endif
}

int64_t FFmpegMovPlayer::msToTimestamp(qint64 positionMs) const
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (m_timeBaseNum <= 0 || m_timeBaseDen <= 0) {
        return 0;
    }
    return qRound64((positionMs / 1000.0) * m_timeBaseDen / m_timeBaseNum);
#else
    Q_UNUSED(positionMs);
    return 0;
#endif
}

int64_t FFmpegMovPlayer::frameToTimestamp(qint64 frameNumber) const
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    const qint64 clampedFrame = clampFrameNumber(frameNumber);
    if (m_frameRateNum <= 0 || m_frameRateDen <= 0 || m_timeBaseNum <= 0 || m_timeBaseDen <= 0) {
        return msToTimestamp(qRound64((clampedFrame * 1000.0) / (m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : kDefaultFps)));
    }

    const double seconds = (static_cast<double>(clampedFrame) * m_frameRateDen) / m_frameRateNum;
    return qMax<int64_t>(0, qRound64(seconds * m_timeBaseDen / m_timeBaseNum));
#else
    Q_UNUSED(frameNumber);
    return 0;
#endif
}

qint64 FFmpegMovPlayer::timestampToFrame(int64_t pts, qint64 fallbackPositionMs) const
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (pts != AV_NOPTS_VALUE && m_frameRateNum > 0 && m_frameRateDen > 0 && m_timeBaseNum > 0 && m_timeBaseDen > 0) {
        const double seconds = (static_cast<double>(pts) * m_timeBaseNum) / m_timeBaseDen;
        const double frameValue = seconds * m_frameRateNum / m_frameRateDen;
        return clampFrameNumber(qRound64(frameValue));
    }

    const double fps = m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : kDefaultFps;
    return clampFrameNumber(qRound64((fallbackPositionMs / 1000.0) * fps));
#else
    Q_UNUSED(pts);
    Q_UNUSED(fallbackPositionMs);
    return 0;
#endif
}

qint64 FFmpegMovPlayer::clampFrameNumber(qint64 frameNumber) const
{
    const qint64 maxFrame = qMax<qint64>(0, m_totalFrames - 1);
    return qBound<qint64>(0, frameNumber, maxFrame);
}

void FFmpegMovPlayer::setPlaybackState(media_player::PlaybackState state)
{
    if (m_playbackState == state) {
        return;
    }
    m_playbackState = state;
    emit playbackStateChanged(state);
}

void FFmpegMovPlayer::clearPresentedFrame()
{
    QMutexLocker locker(&m_frameMutex);
    m_currentFrameImage = QImage();
    m_currentFrameScaledImage = QImage();
    m_currentFrameScaledTargetSize = QSize();
    m_requestedFrameTargetSize = QSize();
}

void FFmpegMovPlayer::syncAudioPosition(qint64 positionMs)
{
    if (!m_audioPlayer || m_audioPlayer->source().isEmpty()) {
        return;
    }

    const qint64 delta = std::llabs(m_audioPlayer->position() - positionMs);
    if (delta > 80) {
        m_audioPlayer->setPosition(positionMs);
    }
}
