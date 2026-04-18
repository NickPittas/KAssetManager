#include "ffmpeg_mov_player.h"

#include <QAudioOutput>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QMutexLocker>

#include <cmath>

#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}
#endif

namespace {

constexpr double kDefaultFps = 24.0;

double rationalToDouble(int num, int den)
{
    return (num > 0 && den > 0) ? (static_cast<double>(num) / static_cast<double>(den)) : 0.0;
}

} // namespace

FFmpegMovPlayer::FFmpegMovPlayer(QObject* parent)
    : QObject(parent)
{
    m_playbackTimer = new QTimer(this);
    m_playbackTimer->setTimerType(Qt::PreciseTimer);
    connect(m_playbackTimer, &QTimer::timeout, this, &FFmpegMovPlayer::onPlaybackTick);

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
    if (!openMedia(filePath, &errorString)) {
        emit error(errorString.isEmpty() ? tr("Failed to open MOV media") : errorString);
        return;
    }

    m_currentPath = filePath;
    if (m_audioPlayer) {
        m_audioPlayer->setSource(QUrl::fromLocalFile(filePath));
        m_audioOutput->setVolume(m_volume);
        m_audioOutput->setMuted(m_muted);
    }
    emit durationChanged(m_durationMs);
    emit mediaInfoReady(m_mediaInfo);

    if (!seekInternal(0, true)) {
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
    closeMedia();
    m_mediaInfo = media_player::MediaInfo();
    m_currentPath.clear();
    m_positionMs = 0;
    m_durationMs = 0;
    m_currentFrame = 0;
    m_totalFrames = 0;
    m_stepAccumulator = 0.0;
    m_lastPresentedPts = -1;
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
    if (m_audioPlayer) {
        syncAudioPosition(m_positionMs);
        m_audioPlayer->play();
    }
    updatePlaybackTimer();
}

void FFmpegMovPlayer::pause()
{
    if (!hasMedia()) {
        return;
    }
    m_playbackTimer->stop();
    if (m_audioPlayer) {
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
    if (m_audioPlayer) {
        m_audioPlayer->stop();
    }
    seekInternal(0, true);
    setPlaybackState(media_player::PlaybackState::Stopped);
}

void FFmpegMovPlayer::seek(qint64 positionMs)
{
    if (!hasMedia()) {
        return;
    }
    seekInternal(positionMs, true);
}

void FFmpegMovPlayer::seekToFrame(qint64 frameNumber)
{
    if (!hasMedia()) {
        return;
    }

    const double fps = m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : kDefaultFps;
    const qint64 targetMs = qRound64((clampFrameNumber(frameNumber) * 1000.0) / fps);
    seekInternal(targetMs, true);
}

void FFmpegMovPlayer::stepForward()
{
    seekToFrame(m_currentFrame + 1);
}

void FFmpegMovPlayer::stepBackward()
{
    seekToFrame(m_currentFrame - 1);
}

void FFmpegMovPlayer::setPlaybackRate(double rate)
{
    m_playbackRate = qFuzzyIsNull(rate) ? 1.0 : rate;
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

void FFmpegMovPlayer::onPlaybackTick()
{
    if (!hasMedia() || m_playbackState != media_player::PlaybackState::Playing) {
        return;
    }

    const double rate = std::abs(m_playbackRate);
    m_stepAccumulator += qMax(0.01, rate);
    const int frameSteps = qMax(1, static_cast<int>(m_stepAccumulator));
    m_stepAccumulator -= frameSteps;

    bool ok = true;
    if (m_playbackRate < 0.0) {
        seekToFrame(m_currentFrame - frameSteps);
        ok = true;
    } else {
        for (int i = 0; i < frameSteps; ++i) {
            if (!decodeNextFrame()) {
                ok = false;
                break;
            }
        }
    }

    if (ok) {
        emit positionChanged(m_positionMs);
        emit currentFrameChanged(m_currentFrame);
        syncAudioPosition(m_positionMs);
        return;
    }

    switch (m_loopMode) {
    case media_player::LoopMode::Loop:
        if (!seekInternal(0, true)) {
            pause();
            emit endOfStream();
        }
        break;
    case media_player::LoopMode::PingPong:
        m_playbackRate = -m_playbackRate;
        updatePlaybackTimer();
        break;
    case media_player::LoopMode::Once:
    default:
        pause();
        emit endOfStream();
        break;
    }
}

bool FFmpegMovPlayer::openMedia(const QString& filePath, QString* errorString)
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

    AVCodecContext* codecContext = avcodec_alloc_context3(codec);
    if (!codecContext) {
        avformat_close_input(&formatContext);
        if (errorString) {
            *errorString = tr("Could not allocate FFmpeg codec context");
        }
        return false;
    }

    ret = avcodec_parameters_to_context(codecContext, videoStream->codecpar);
    if (ret < 0 || avcodec_open2(codecContext, codec, nullptr) < 0) {
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        if (errorString) {
            *errorString = tr("Could not initialize MOV decoder for %1").arg(filePath);
        }
        return false;
    }

    AVFrame* decodeFrame = av_frame_alloc();
    AVFrame* rgbFrame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    if (!decodeFrame || !rgbFrame || !packet) {
        if (decodeFrame) av_frame_free(&decodeFrame);
        if (rgbFrame) av_frame_free(&rgbFrame);
        if (packet) av_packet_free(&packet);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        if (errorString) {
            *errorString = tr("Could not allocate FFmpeg decode buffers");
        }
        return false;
    }

    const int width = codecContext->width;
    const int height = codecContext->height;
    SwsContext* swsContext = sws_getContext(
        width, height, codecContext->pix_fmt,
        width, height, AV_PIX_FMT_BGRA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!swsContext) {
        av_packet_free(&packet);
        av_frame_free(&decodeFrame);
        av_frame_free(&rgbFrame);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        if (errorString) {
            *errorString = tr("Could not create FFmpeg color converter");
        }
        return false;
    }

    const int rgbBufferSize = av_image_get_buffer_size(AV_PIX_FMT_BGRA, width, height, 1);
    uint8_t* rgbBuffer = static_cast<uint8_t*>(av_malloc(rgbBufferSize));
    if (!rgbBuffer) {
        sws_freeContext(swsContext);
        av_packet_free(&packet);
        av_frame_free(&decodeFrame);
        av_frame_free(&rgbFrame);
        avcodec_free_context(&codecContext);
        avformat_close_input(&formatContext);
        if (errorString) {
            *errorString = tr("Could not allocate FFmpeg RGB buffer");
        }
        return false;
    }

    av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, rgbBuffer, AV_PIX_FMT_BGRA, width, height, 1);

    closeMedia();
    m_formatContext = formatContext;
    m_codecContext = codecContext;
    m_swsContext = swsContext;
    m_decodeFrame = decodeFrame;
    m_rgbFrame = rgbFrame;
    m_packet = packet;
    m_rgbBuffer = rgbBuffer;
    m_videoStreamIndex = videoStreamIndex;
    const AVRational frameRate = av_guess_frame_rate(formatContext, videoStream, nullptr);
    m_timeBaseNum = videoStream->time_base.num;
    m_timeBaseDen = videoStream->time_base.den;
    m_frameRateNum = frameRate.num;
    m_frameRateDen = frameRate.den;

    const double guessedFps = rationalToDouble(m_frameRateNum, m_frameRateDen);
    const double fps = guessedFps > 0.0 ? guessedFps : kDefaultFps;
    const qint64 durationMs = formatContext->duration > 0
        ? static_cast<qint64>((formatContext->duration * 1000.0) / AV_TIME_BASE)
        : timestampToMs(videoStream->duration);
    const qint64 totalFrames = videoStream->nb_frames > 0
        ? static_cast<qint64>(videoStream->nb_frames)
        : qMax<qint64>(1, qRound64((durationMs / 1000.0) * fps));

    m_mediaInfo.width = width;
    m_mediaInfo.height = height;
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
    if (m_rgbBuffer) {
        av_free(m_rgbBuffer);
        m_rgbBuffer = nullptr;
    }
    if (m_packet) {
        av_packet_free(&m_packet);
    }
    if (m_decodeFrame) {
        av_frame_free(&m_decodeFrame);
    }
    if (m_rgbFrame) {
        av_frame_free(&m_rgbFrame);
    }
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
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
}

void FFmpegMovPlayer::updatePlaybackTimer()
{
    if (!hasMedia() || m_playbackState != media_player::PlaybackState::Playing) {
        m_playbackTimer->stop();
        return;
    }

    const double fps = m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : kDefaultFps;
    const int intervalMs = qMax(1, qRound(1000.0 / fps));
    if (m_playbackTimer->interval() != intervalMs) {
        m_playbackTimer->setInterval(intervalMs);
    }
    if (!m_playbackTimer->isActive()) {
        m_playbackTimer->start();
    }
}

bool FFmpegMovPlayer::decodeNextFrame()
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!hasMedia()) {
        return false;
    }

    int ret = 0;
    while ((ret = av_read_frame(m_formatContext, m_packet)) >= 0) {
        if (m_packet->stream_index != m_videoStreamIndex) {
            av_packet_unref(m_packet);
            continue;
        }

        ret = avcodec_send_packet(m_codecContext, m_packet);
        av_packet_unref(m_packet);
        if (ret < 0) {
            continue;
        }

        while ((ret = avcodec_receive_frame(m_codecContext, m_decodeFrame)) >= 0) {
            if (presentDecodedFrame(m_decodeFrame)) {
                return true;
            }
        }
    }

    avcodec_send_packet(m_codecContext, nullptr);
    while (avcodec_receive_frame(m_codecContext, m_decodeFrame) >= 0) {
        if (presentDecodedFrame(m_decodeFrame)) {
            return true;
        }
    }
#endif
    return false;
}

bool FFmpegMovPlayer::decodeFrameForTimestamp(int64_t targetTs, bool allowPastTarget)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!hasMedia()) {
        return false;
    }

    int ret = 0;
    while ((ret = av_read_frame(m_formatContext, m_packet)) >= 0) {
        if (m_packet->stream_index != m_videoStreamIndex) {
            av_packet_unref(m_packet);
            continue;
        }

        ret = avcodec_send_packet(m_codecContext, m_packet);
        av_packet_unref(m_packet);
        if (ret < 0) {
            continue;
        }

        while ((ret = avcodec_receive_frame(m_codecContext, m_decodeFrame)) >= 0) {
            const int64_t pts = (m_decodeFrame->best_effort_timestamp != AV_NOPTS_VALUE)
                ? m_decodeFrame->best_effort_timestamp
                : m_decodeFrame->pts;
            if (allowPastTarget || pts == AV_NOPTS_VALUE || pts >= targetTs) {
                return presentDecodedFrame(m_decodeFrame);
            }
        }
    }
#else
    Q_UNUSED(targetTs);
    Q_UNUSED(allowPastTarget);
#endif
    return false;
}

bool FFmpegMovPlayer::seekInternal(qint64 positionMs, bool emitSignals)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!hasMedia()) {
        return false;
    }

    const qint64 clampedMs = qBound<qint64>(0, positionMs, qMax<qint64>(0, m_durationMs));
    const int64_t targetTs = msToTimestamp(clampedMs);
    if (av_seek_frame(m_formatContext, m_videoStreamIndex, targetTs, AVSEEK_FLAG_BACKWARD) < 0) {
        return false;
    }
    avcodec_flush_buffers(m_codecContext);
    const bool ok = decodeFrameForTimestamp(targetTs, clampedMs == 0);
    if (ok && emitSignals) {
        emit positionChanged(m_positionMs);
        emit currentFrameChanged(m_currentFrame);
        syncAudioPosition(m_positionMs);
    }
    return ok;
#else
    Q_UNUSED(positionMs);
    Q_UNUSED(emitSignals);
    return false;
#endif
}

bool FFmpegMovPlayer::presentDecodedFrame(AVFrame* frame)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (!frame || !m_codecContext || !m_swsContext || !m_rgbFrame) {
        return false;
    }

    sws_scale(m_swsContext, frame->data, frame->linesize, 0, m_codecContext->height, m_rgbFrame->data, m_rgbFrame->linesize);
    QImage image(m_rgbFrame->data[0], m_codecContext->width, m_codecContext->height, m_rgbFrame->linesize[0], QImage::Format_ARGB32);
    {
        QMutexLocker locker(&m_frameMutex);
        m_currentFrameImage = image.copy();
    }

    const int64_t pts = (frame->best_effort_timestamp != AV_NOPTS_VALUE) ? frame->best_effort_timestamp : frame->pts;
    m_lastPresentedPts = pts;
    m_positionMs = pts != AV_NOPTS_VALUE ? timestampToMs(pts) : qRound64((m_currentFrame * 1000.0) / (m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : kDefaultFps));
    const double fps = m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : kDefaultFps;
    m_currentFrame = clampFrameNumber(qRound64((m_positionMs / 1000.0) * fps));
    emit frameUpdated();
    return true;
#else
    Q_UNUSED(frame);
    return false;
#endif
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
