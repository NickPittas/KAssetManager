#include "ffmpeg_player.h"

#include <QFileInfo>
#include <QDir>
#include <QImageWriter>
#include <QImageReader>
#include <QByteArray>
#include <QBuffer>
#include <QtConcurrent/QtConcurrent>
#include <QCoreApplication>
#include <QSettings>
#include <QTimer>
#include <QEventLoop>
#include <QStandardPaths>
#include <QMutex>
#include <QMutexLocker>
#include <QRecursiveMutex>
#include <QThreadPool>
#include <QThread>
#include <QObject>
#include <QDateTime>
#include <algorithm>
#include <cmath>

#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
}
#endif

FFmpegPlayer::FFmpegPlayer(QObject* parent)
    : QObject(parent)
    , m_playbackTimer(new QTimer(this))
    , m_prefetchTimer(new QTimer(this))
{
    qInfo() << "[FFmpegPlayer] Initializing unified FFmpeg playback backend";
    
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    av_log_set_level(AV_LOG_ERROR);
    
    // Log FFmpeg version
    static bool s_loggedVersion = false;
    if (!s_loggedVersion) {
        const unsigned version = avcodec_version();
        const unsigned major = AV_VERSION_MAJOR(version);
        const unsigned minor = AV_VERSION_MINOR(version);
        const unsigned micro = AV_VERSION_MICRO(version);
        qInfo() << "[FFmpegPlayer] FFmpeg version"
                << QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(micro)
                << QString::fromUtf8(avcodec_configuration());
        s_loggedVersion = true;
    }
#endif
    
    // Initialize timers
    m_playbackTimer->setInterval(40); // 25 FPS
    connect(m_playbackTimer, &QTimer::timeout, this, &FFmpegPlayer::onTimerTick);
    
    m_prefetchTimer->setInterval(100); // Prefetch every 100ms
    connect(m_prefetchTimer, &QTimer::timeout, this, &FFmpegPlayer::onPrefetchTimer);
    
    // Load configuration
    QSettings settings("AugmentCode", "KAssetManager");
    m_enableHardwareAcceleration = settings.value("FFmpeg/HardwareAcceleration", true).toBool();
    m_maxCacheSize = settings.value("FFmpeg/MaxCacheSize", 100).toInt();
    m_maxMemoryUsage = settings.value("FFmpeg/MaxMemoryUsage", 512).toInt() * 1024 * 1024; // Convert MB to bytes
    
    detectHardwareAcceleration();
    
    qInfo() << "[FFmpegPlayer] Initialization complete:"
            << "Hardware acceleration:" << (m_enableHardwareAcceleration ? "enabled" : "disabled")
            << "Cache size:" << m_maxCacheSize << "frames"
            << "Memory limit:" << (m_maxMemoryUsage / 1024 / 1024) << "MB";
}

FFmpegPlayer::~FFmpegPlayer()
{
    cleanup();
}

void FFmpegPlayer::loadVideo(const QString& filePath)
{
    QMutexLocker locker(&m_mutex);
    
    if (!QFileInfo(filePath).exists()) {
        emit error(QString("File does not exist: %1").arg(filePath));
        return;
    }
    
    m_currentFilePath = filePath;
    m_streamType = detectStreamType(filePath);
    
    if (m_streamType == StreamType::Unsupported) {
        emit error(QString("Unsupported file format: %1").arg(filePath));
        return;
    }
    
    // Probe media info
    MediaInfo info = probeMediaInfo(filePath);
    m_mediaInfo = info;
    emit mediaInfoReady(info);
    
    // Reset state
    m_currentPosition = 0;
    m_currentFrame = 0;
    m_duration = info.durationMs;
    m_totalFrames = info.durationMs > 0 ? static_cast<int>(info.durationMs * info.fps / 1000.0) : 0;
    
    emit playbackStateChanged(PlaybackState::Loading);
    emit durationChanged(m_duration.load());
    
    // Initialize FFmpeg context
    if (!initializeHardwareContext()) {
        qWarning() << "[FFmpegPlayer] Failed to initialize hardware acceleration, falling back to software";
    }
    
    // Start with preview frame
    QtConcurrent::run([this, filePath]() {
        VideoFrame frame = decodeVideoFrame(0, Quality::Preview);
        if (frame.isValid()) {
            QMetaObject::invokeMethod(this, [this, frame]() {
                emit frameReady(frame);
                emit playbackStateChanged(PlaybackState::Stopped);
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(this, [this]() {
                emit playbackStateChanged(PlaybackState::Error);
                emit error("Failed to decode initial frame");
            }, Qt::QueuedConnection);
        }
    });
}

void FFmpegPlayer::loadImageSequence(const QStringList& framePaths, qint64 startFrame, qint64 endFrame)
{
    QMutexLocker locker(&m_mutex);
    
    if (framePaths.isEmpty()) {
        emit error("Empty frame sequence");
        return;
    }
    
    // Validate frame paths
    for (const QString& path : framePaths) {
        if (!QFileInfo(path).exists()) {
            emit error(QString("Frame file does not exist: %1").arg(path));
            return;
        }
    }
    
    m_sequenceFramePaths = framePaths;
    m_streamType = StreamType::ImageSequence;
    
    // Set up sequence state
    m_totalFrames = framePaths.size();
    m_currentFrame = static_cast<int>(std::max(0LL, startFrame));
    m_duration = m_totalFrames * 40; // Assume 25fps
    m_currentPosition = m_currentFrame * 40; // 40ms per frame
    
    // Create media info
    MediaInfo info;
    info.width = 0; // Will be set by first frame decode
    info.height = 0;
    info.fps = 25.0;
    info.durationMs = m_duration;
    info.codec = "ImageSequence";
    m_mediaInfo = info;
    
    emit mediaInfoReady(info);
    emit playbackStateChanged(PlaybackState::Stopped);
    emit durationChanged(m_duration.load());
    
    qInfo() << "[FFmpegPlayer] Loaded image sequence:" << m_totalFrames << "frames";
    
    // Start prefetching
    startPrefetch();
}

void FFmpegPlayer::play()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_playbackState.load() == PlaybackState::Playing) {
        return;
    }
    
    m_playbackState = PlaybackState::Playing;
    emit playbackStateChanged(PlaybackState::Playing);
    
    if (m_streamType == StreamType::Video && m_duration > 0) {
        m_playbackTimer->start();
        m_prefetchTimer->start();
    } else if (m_streamType == StreamType::ImageSequence) {
        startPrefetch();
    }
}

void FFmpegPlayer::pause()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_playbackState.load() != PlaybackState::Playing) {
        return;
    }
    
    m_playbackState = PlaybackState::Paused;
    emit playbackStateChanged(PlaybackState::Paused);
    
    m_playbackTimer->stop();
    m_prefetchTimer->stop();
}

void FFmpegPlayer::stop()
{
    QMutexLocker locker(&m_mutex);
    
    m_playbackState = PlaybackState::Stopped;
    emit playbackStateChanged(PlaybackState::Stopped);
    
    m_playbackTimer->stop();
    m_prefetchTimer->stop();
    
    m_currentPosition = 0;
    m_currentFrame = 0;
    emit positionChanged(0);
    emit frameIndexChanged(0);
}

void FFmpegPlayer::seek(qint64 positionMs)
{
    QMutexLocker locker(&m_mutex);
    
    if (m_duration <= 0 || positionMs < 0 || positionMs > m_duration) {
        return;
    }
    
    m_currentPosition = positionMs;
    
    if (m_streamType == StreamType::Video) {
        // Convert to frame index
        double fps = m_mediaInfo.fps > 0 ? m_mediaInfo.fps : 25.0;
        m_currentFrame = static_cast<int>(positionMs * fps / 1000.0);
    }
    
    emit positionChanged(positionMs);
    
    // Decode target frame
    QtConcurrent::run([this, positionMs]() {
        VideoFrame frame = m_streamType == StreamType::Video ? 
            decodeVideoFrame(positionMs) : 
            decodeSequenceFrame(m_currentFrame);
            
        if (frame.isValid()) {
            QMetaObject::invokeMethod(this, [this, frame]() {
                emit frameReady(frame);
            }, Qt::QueuedConnection);
        }
    });
}

void FFmpegPlayer::seekToFrame(int frameIndex)
{
    QMutexLocker locker(&m_mutex);
    
    if (frameIndex < 0 || frameIndex >= m_totalFrames) {
        return;
    }
    
    m_currentFrame = frameIndex;
    
    if (m_streamType == StreamType::ImageSequence) {
        m_currentPosition = frameIndex * 40; // 25fps = 40ms per frame
    }
    
    emit frameIndexChanged(frameIndex);
    emit positionChanged(m_currentPosition);
    
    // Decode target frame
    QtConcurrent::run([this, frameIndex]() {
        VideoFrame frame = decodeSequenceFrame(frameIndex);
        if (frame.isValid()) {
            QMetaObject::invokeMethod(this, [this, frame]() {
                emit frameReady(frame);
            }, Qt::QueuedConnection);
        }
    });
}

void FFmpegPlayer::nextFrame()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_streamType != StreamType::ImageSequence) {
        // For video, seek to next frame based on FPS
        qint64 nextPos = m_currentPosition + (1000.0 / m_mediaInfo.fps);
        seek(nextPos);
        return;
    }
    
    if (m_currentFrame < m_totalFrames - 1) {
        seekToFrame(m_currentFrame + 1);
    }
}

void FFmpegPlayer::previousFrame()
{
    QMutexLocker locker(&m_mutex);
    
    if (m_streamType != StreamType::ImageSequence) {
        // For video, seek to previous frame based on FPS
        qint64 prevPos = m_currentPosition - (1000.0 / m_mediaInfo.fps);
        seek(std::max(0LL, prevPos));
        return;
    }
    
    if (m_currentFrame > 0) {
        seekToFrame(m_currentFrame - 1);
    }
}

FFmpegPlayer::MediaInfo FFmpegPlayer::mediaInfo() const
{
    return m_mediaInfo;
}

void FFmpegPlayer::onTimerTick()
{
    if (m_playbackState.load() != PlaybackState::Playing) {
        return;
    }
    
    // Update position
    qint64 newPosition = m_currentPosition.load() + kDefaultFrameIntervalMs;
    
    if (newPosition >= m_duration) {
        // End of media
        stop();
        return;
    }
    
    seek(newPosition);
}

void FFmpegPlayer::onPrefetchTimer()
{
    if (m_playbackState.load() != PlaybackState::Playing) {
        return;
    }
    
    prefetchFrames(m_currentFrame);
}

void FFmpegPlayer::initialize()
{
    // Initialize FFmpeg if needed
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    // FFmpeg is already initialized by av_log_set_level call in constructor
#endif
}

void FFmpegPlayer::cleanup()
{
    QMutexLocker locker(&m_mutex);
    
    m_playbackTimer->stop();
    m_prefetchTimer->stop();
    
    cleanupHardwareContext();
    
    // Clear cache
    m_frameCache.clear();
    m_cachedFrameIndices.clear();
    m_currentMemoryUsage = 0;
    
    // Reset state
    m_currentFilePath.clear();
    m_sequenceFramePaths.clear();
    m_streamType = StreamType::Unsupported;
}

void FFmpegPlayer::detectHardwareAcceleration()
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    QStringList supportedTypes;
    
    // Check for CUDA (NVIDIA)
#ifdef AV_HWDEVICE_TYPE_CUDA
    AVHWDeviceType cudaType = av_hwdevice_find_type_by_name("cuda");
    if (cudaType != AV_HWDEVICE_TYPE_NONE) {
        supportedTypes << "CUDA";
    }
#endif
    
    // Check for QuickSync (Intel)
#ifdef AV_HWDEVICE_TYPE_QSV
    AVHWDeviceType qsvType = av_hwdevice_find_type_by_name("qsv");
    if (qsvType != AV_HWDEVICE_TYPE_NONE) {
        supportedTypes << "QuickSync";
    }
#endif
    
    // Check for D3D11VA (Windows)
#ifdef AV_HWDEVICE_TYPE_D3D11VA
    AVHWDeviceType d3d11Type = av_hwdevice_find_type_by_name("d3d11va");
    if (d3d11Type != AV_HWDEVICE_TYPE_NONE) {
        supportedTypes << "D3D11VA";
    }
#endif
    
    // Check for VideoToolbox (macOS)
#ifdef AV_HWDEVICE_TYPE_VIDEOTOOLBOX
    AVHWDeviceType vtType = av_hwdevice_find_type_by_name("videotoolbox");
    if (vtType != AV_HWDEVICE_TYPE_NONE) {
        supportedTypes << "VideoToolbox";
    }
#endif
    
    m_mediaInfo.supportedAccelerations = supportedTypes;
    
    qInfo() << "[FFmpegPlayer] Supported hardware accelerations:" << supportedTypes.join(", ");
#else
    qInfo() << "[FFmpegPlayer] Hardware acceleration not available (FFmpeg disabled)";
#endif
}

FFmpegPlayer::StreamType FFmpegPlayer::detectStreamType(const QString& filePath)
{
    QFileInfo info(filePath);
    QString suffix = info.suffix().toLower();
    
    // Check for video formats
    static const QSet<QString> videoExts = {
        "mp4", "mov", "avi", "mkv", "webm", "wmv", "asf", "flv",
        "m4v", "mxf", "mpg", "mpeg", "m2v", "m2ts", "mts", "ts",
        "ogv", "y4m", "3gp", "3g2", "qt", "f4v"
    };
    
    if (videoExts.contains(suffix)) {
        return StreamType::Video;
    }
    
    // Check for image sequence formats
    static const QSet<QString> sequenceExts = {
        "exr", "dpx", "png", "jpg", "jpeg", "tga", "tiff", "tif", "bmp"
    };
    
    if (sequenceExts.contains(suffix) && isImageSequence(filePath)) {
        return StreamType::ImageSequence;
    }
    
    return StreamType::Unsupported;
}

FFmpegPlayer::MediaInfo FFmpegPlayer::probeMediaInfo(const QString& filePath)
{
    MediaInfo info;
    
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    AVFormatContext* fmtCtx = nullptr;
    QByteArray localPath = QFile::encodeName(filePath);
    
    if (avformat_open_input(&fmtCtx, localPath.constData(), nullptr, nullptr) < 0) {
        qWarning() << "[FFmpegPlayer] Failed to open input for media probing";
        return info;
    }
    
    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        qWarning() << "[FFmpegPlayer] Failed to find stream info";
        avformat_close_input(&fmtCtx);
        return info;
    }
    
    // Find video stream
    int videoStream = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStream >= 0) {
        AVStream* stream = fmtCtx->streams[videoStream];
        AVCodecParameters* params = stream->codecpar;
        
        info.width = params->width;
        info.height = params->height;
        info.codec = QString::fromUtf8(avcodec_get_name(params->codec_id));
        
        // Get FPS
        AVRational fps = stream->avg_frame_rate.num > 0 ? stream->avg_frame_rate : stream->r_frame_rate;
        if (fps.num > 0 && fps.den > 0) {
            info.fps = static_cast<double>(fps.num) / fps.den;
        }
        
        // Get duration
        if (fmtCtx->duration > 0) {
            info.durationMs = (fmtCtx->duration * 1000) / AV_TIME_BASE;
        } else if (stream->duration > 0) {
            info.durationMs = (stream->duration * stream->time_base.num * 1000) / stream->time_base.den;
        }
    }
    
    // Check for audio
    int audioStream = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    info.hasAudio = (audioStream >= 0);
    
    // Check hardware acceleration support
    info.hasHardwareAcceleration = m_enableHardwareAcceleration && !m_mediaInfo.supportedAccelerations.isEmpty();
    
    avformat_close_input(&fmtCtx);
#endif
    
    return info;
}

bool FFmpegPlayer::initializeHardwareContext()
{
#if !defined(HAVE_FFMPEG) || !HAVE_FFMPEG
    return false;
#else
    if (!m_enableHardwareAcceleration || m_mediaInfo.supportedAccelerations.isEmpty()) {
        return false;
    }
    
    // Try to initialize hardware acceleration
    // Priority order: D3D11VA (Windows), CUDA (NVIDIA), QuickSync (Intel), VideoToolbox (macOS)
    QStringList priorityList;
#ifdef Q_OS_WIN
    priorityList << "D3D11VA" << "CUDA" << "QuickSync";
#elif defined(Q_OS_MAC)
    priorityList << "VideoToolbox" << "CUDA";
#else
    priorityList << "CUDA" << "QuickSync";
#endif
    
    for (const QString& accelType : priorityList) {
        if (!m_mediaInfo.supportedAccelerations.contains(accelType)) {
            continue;
        }
        
        qInfo() << "[FFmpegPlayer] Attempting to initialize hardware acceleration:" << accelType;
        
        // Try to create hardware device context
        AVBufferRef* hwDeviceCtx = nullptr;
        AVHWDeviceType deviceType = av_hwdevice_find_type_by_name(accelType.toLower().toUtf8().constData());
        
        if (deviceType != AV_HWDEVICE_TYPE_NONE) {
            int ret = av_hwdevice_ctx_create(&hwDeviceCtx, deviceType, nullptr, nullptr, 0);
            if (ret == 0 && hwDeviceCtx) {
                m_hwDeviceCtx = hwDeviceCtx;
                m_hardwareAccelerationType = accelType;
                m_hardwareAcceleration = true;
                qInfo() << "[FFmpegPlayer] Hardware acceleration initialized:" << accelType;
                return true;
            }
        }
    }
    
    qInfo() << "[FFmpegPlayer] Failed to initialize any hardware acceleration, using software decoding";
    return false;
#endif
}

void FFmpegPlayer::cleanupHardwareContext()
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (m_hwContext) {
        av_buffer_unref(&m_hwContext);
        m_hwContext = nullptr;
    }
    
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwDeviceCtx = nullptr;
    }
    
    m_hardwareAcceleration = false;
    m_hardwareAccelerationType.clear();
#endif
}

FFmpegPlayer::VideoFrame FFmpegPlayer::decodeVideoFrame(qint64 targetMs, Quality quality)
{
    if (m_streamType != StreamType::Video) {
        return VideoFrame();
    }
    
    // Check cache first
    VideoFrame cached = getCachedFrame(targetMs, -1);
    if (cached.isValid()) {
        return cached;
    }
    
    // Decode frame
    VideoFrame frame = decodeFrameWithFFmpeg(targetMs, m_currentFilePath);
    
    if (frame.isValid()) {
        // Apply quality scaling if needed
        if (quality == Quality::Preview && frame.width > 0 && frame.height > 0) {
            int maxDim = 512; // Preview size limit
            if (frame.width > maxDim || frame.height > maxDim) {
                double scale = std::min(static_cast<double>(maxDim) / frame.width,
                                      static_cast<double>(maxDim) / frame.height);
                int newWidth = static_cast<int>(frame.width * scale);
                int newHeight = static_cast<int>(frame.height * scale);
                
                QImage scaled = frame.image.scaled(newWidth, newHeight, 
                                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
                frame.image = scaled;
            }
        }
        
        // Cache the frame
        cacheFrame(frame, targetMs, -1);
    }
    
    return frame;
}

FFmpegPlayer::VideoFrame FFmpegPlayer::decodeSequenceFrame(int frameIndex, Quality quality)
{
    if (m_streamType != StreamType::ImageSequence || 
        frameIndex < 0 || frameIndex >= m_sequenceFramePaths.size()) {
        return VideoFrame();
    }
    
    // Check cache first
    VideoFrame cached = getCachedFrame(-1, frameIndex);
    if (cached.isValid()) {
        return cached;
    }
    
    QString framePath = m_sequenceFramePaths[frameIndex];
    QImage image;
    
    // Load image using Qt (can be enhanced with OIIO)
    QImageReader reader(framePath);
    
    if (quality == Quality::Preview) {
        reader.setScaledSize(QSize(512, 512));
    }
    
    image = reader.read();
    
    if (image.isNull()) {
        qWarning() << "[FFmpegPlayer] Failed to load sequence frame:" << framePath;
        return VideoFrame();
    }
    
    VideoFrame frame;
    frame.image = image;
    frame.timestampMs = frameIndex * 40; // 25fps
    frame.fps = 25.0;
    frame.width = image.width();
    frame.height = image.height();
    frame.codec = "ImageSequence";
    
    // Cache the frame
    cacheFrame(frame, frame.timestampMs, frameIndex);
    
    return frame;
}

FFmpegPlayer::VideoFrame FFmpegPlayer::decodeFrameWithFFmpeg(qint64 targetMs, const QString& filePath)
{
#if !defined(HAVE_FFMPEG) || !HAVE_FFMPEG
    Q_UNUSED(targetMs);
    Q_UNUSED(filePath);
    return VideoFrame();
#else
    m_performanceTimer.start();
    
    // Initialize FFmpeg context if not already done
    if (!m_formatCtx) {
        AVFormatContext* fmtRaw = nullptr;
        QByteArray localPath = QFile::encodeName(filePath);
        
        if (avformat_open_input(&fmtRaw, localPath.constData(), nullptr, nullptr) < 0) {
            emit error(QString("Failed to open input: %1").arg(filePath));
            return VideoFrame();
        }
        m_formatCtx.reset(fmtRaw);
        
        if (avformat_find_stream_info(m_formatCtx.get(), nullptr) < 0) {
            emit error("Failed to find stream info");
            return VideoFrame();
        }
        
        // Find video stream
        m_videoStreamIndex = av_find_best_stream(m_formatCtx.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (m_videoStreamIndex < 0) {
            emit error("No video stream found");
            return VideoFrame();
        }
        
        AVStream* stream = m_formatCtx->streams[m_videoStreamIndex];
        AVCodecParameters* params = stream->codecpar;
        
        const AVCodec* decoder = avcodec_find_decoder(params->codec_id);
        if (!decoder) {
            const AVCodecDescriptor* desc = avcodec_descriptor_get(params->codec_id);
            QString codecName = desc ? QString::fromUtf8(desc->name) : QStringLiteral("?");
            emit error(QString("Decoder not found for codec: %1").arg(codecName));
            return VideoFrame();
        }
        
        // Create codec context
        m_videoCodecCtx.reset(avcodec_alloc_context3(decoder));
        if (!m_videoCodecCtx) {
            emit error("Failed to allocate codec context");
            return VideoFrame();
        }
        
        if (avcodec_parameters_to_context(m_videoCodecCtx.get(), params) < 0) {
            emit error("Failed to copy codec parameters");
            return VideoFrame();
        }
        
        // Set hardware acceleration if available
        if (m_hardwareAcceleration && m_hwDeviceCtx) {
            m_videoCodecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
        }
        
        if (avcodec_open2(m_videoCodecCtx.get(), decoder, nullptr) < 0) {
            emit error("Failed to open codec");
            return VideoFrame();
        }
        
        // Allocate frame and packet
        m_frame.reset(av_frame_alloc());
        m_packet.reset(av_packet_alloc());
    }
    
    // Seek to target timestamp
    if (m_formatCtx->streams[m_videoStreamIndex]->duration > 0) {
        const double duration = m_formatCtx->streams[m_videoStreamIndex]->duration * 
                              av_q2d(m_formatCtx->streams[m_videoStreamIndex]->time_base);
        const double targetSec = duration * (static_cast<double>(targetMs) / m_duration);
        int64_t ts = static_cast<int64_t>(targetSec / av_q2d(m_formatCtx->streams[m_videoStreamIndex]->time_base));
        av_seek_frame(m_formatCtx.get(), m_videoStreamIndex, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(m_videoCodecCtx.get());
    }
    
    // Decode frame
    bool frameFound = false;
    int safety = 0;
    
    while (!frameFound && av_read_frame(m_formatCtx.get(), m_packet.get()) >= 0) {
        if (m_packet->stream_index != m_videoStreamIndex) {
            av_packet_unref(m_packet.get());
            continue;
        }
        
        int ret = avcodec_send_packet(m_videoCodecCtx.get(), m_packet.get());
        if (ret < 0) {
            qWarning() << "[FFmpegPlayer] avcodec_send_packet failed:" << getFFmpegErrorString(ret);
            av_packet_unref(m_packet.get());
            continue;
        }
        
        av_packet_unref(m_packet.get());
        
        while (!frameFound) {
            ret = avcodec_receive_frame(m_videoCodecCtx.get(), m_frame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                qWarning() << "[FFmpegPlayer] avcodec_receive_frame failed:" << getFFmpegErrorString(ret);
                break;
            }
            
            // Convert frame to QImage
            const int width = m_frame->width;
            const int height = m_frame->height;
            AVPixelFormat format = static_cast<AVPixelFormat>(m_frame->format);
            
            // Create scaling context if needed
            if (!m_swsCtx || format != AV_PIX_FMT_RGBA) {
                m_swsCtx.reset(sws_getContext(width, height, format,
                                            width, height, AV_PIX_FMT_RGBA,
                                            SWS_BICUBIC, nullptr, nullptr, nullptr));
                if (!m_swsCtx) {
                    emit error("Failed to create scaling context");
                    return VideoFrame();
                }
            }
            
            QImage image(width, height, QImage::Format_RGBA8888);
            uint8_t* dstData[4] = { image.bits(), nullptr, nullptr, nullptr };
            int dstLinesize[4] = { static_cast<int>(image.bytesPerLine()), 0, 0, 0 };
            
            sws_scale(m_swsCtx.get(), m_frame->data, m_frame->linesize, 0, height, dstData, dstLinesize);
            
            VideoFrame result;
            result.image = image;
            result.timestampMs = targetMs;
            result.fps = m_mediaInfo.fps;
            result.codec = m_mediaInfo.codec;
            result.width = width;
            result.height = height;
            
            frameFound = true;
            break;
        }
        
        if (++safety > 256) {
            break;
        }
    }
    
    // Update performance metrics
    m_totalDecodedFrames++;
    m_averageDecodeTime = (m_averageDecodeTime * (m_totalDecodedFrames - 1) + m_performanceTimer.elapsed()) / m_totalDecodedFrames;
    
    return frameFound ? VideoFrame() : VideoFrame(); // Return empty if not found
#endif
}

void FFmpegPlayer::prefetchFrames(int currentFrame)
{
    if (m_playbackState.load() != PlaybackState::Playing) {
        return;
    }
    
    if (m_streamType == StreamType::ImageSequence) {
        prefetchSequenceFrames(currentFrame);
    } else if (m_streamType == StreamType::Video) {
        prefetchVideoFrames(currentFrame);
    }
}

void FFmpegPlayer::prefetchSequenceFrames(int currentFrame)
{
    const int windowStart = std::max(0, currentFrame - kPrefetchWindowSize / 2);
    const int windowEnd = std::min(m_totalFrames - 1, currentFrame + kPrefetchWindowSize / 2);
    
    // Check memory usage and evict if necessary
    if (m_frameCache.size() >= m_maxCacheSize || m_currentMemoryUsage > m_maxMemoryUsage) {
        evictLRUEntries();
    }
    
    // Prefetch frames in the window that aren't cached
    QVector<int> framesToPrefetch;
    for (int i = windowStart; i <= windowEnd; ++i) {
        if (!m_cachedFrameIndices.contains(i) && !isFrameInCache(-1, i)) {
            framesToPrefetch.append(i);
        }
        
        if (framesToPrefetch.size() >= kPrefetchConcurrency) {
            break;
        }
    }
    
    // Decode frames in background
    for (int frameIndex : framesToPrefetch) {
        QtConcurrent::run([this, frameIndex]() {
            VideoFrame frame = decodeSequenceFrame(frameIndex, Quality::Preview);
            if (frame.isValid()) {
                QMetaObject::invokeMethod(this, [this, frame]() {
                    emit frameReady(frame);
                }, Qt::QueuedConnection);
            }
        });
    }
}

void FFmpegPlayer::prefetchVideoFrames(int currentFrame)
{
    // For video, we mainly rely on FFmpeg's internal buffering
    // This is a simplified implementation - could be enhanced
    Q_UNUSED(currentFrame);
}

FFmpegPlayer::VideoFrame FFmpegPlayer::getCachedFrame(qint64 timestampMs, int frameIndex)
{
    QMutexLocker locker(&m_mutex);
    
    QString key = QString("ts_%1_frame_%2").arg(timestampMs).arg(frameIndex);
    
    if (m_frameCache.contains(key)) {
        CacheEntry& entry = m_frameCache[key];
        entry.lastAccessed = QDateTime::currentMSecsSinceEpoch();
        entry.accessCount++;
        
        m_cacheHits++;
        return entry.frame;
    }
    
    m_cacheMisses++;
    return VideoFrame();
}

void FFmpegPlayer::cacheFrame(const VideoFrame& frame, qint64 timestampMs, int frameIndex)
{
    QMutexLocker locker(&m_mutex);
    
    // Compress frame data to save memory
    QByteArray compressedData;
    QBuffer buffer(&compressedData);
    buffer.open(QIODevice::WriteOnly);
    frame.image.save(&buffer, "JPEG", static_cast<int>(kCompressionQuality * 100));
    
    QString key = QString("ts_%1_frame_%2").arg(timestampMs).arg(frameIndex);
    
    CacheEntry entry;
    entry.frame = frame;
    entry.timestampMs = timestampMs;
    entry.frameIndex = frameIndex;
    entry.lastAccessed = QDateTime::currentMSecsSinceEpoch();
    entry.accessCount = 1;
    entry.compressedSize = compressedData.size();
    
    // Check memory limits
    if (m_frameCache.size() >= m_maxCacheSize || m_currentMemoryUsage + compressedData.size() > m_maxMemoryUsage) {
        evictLRUEntries();
    }
    
    m_frameCache.insert(key, entry);
    m_currentMemoryUsage += compressedData.size();
    
    if (frameIndex >= 0) {
        m_cachedFrameIndices.insert(frameIndex);
    }
    
    emit cacheStatus(QString("Cached frame %1 (%2 bytes)").arg(key).arg(compressedData.size()));
}

bool FFmpegPlayer::isImageSequence(const QString& filePath) const
{
    QFileInfo info(filePath);
    QString suffix = info.suffix().toLower();
    
    static const QSet<QString> sequenceExts = {
        "exr", "dpx", "png", "jpg", "jpeg", "tga", "tiff", "tif", "bmp"
    };
    
    if (!sequenceExts.contains(suffix)) {
        return false;
    }
    
    QString base = info.completeBaseName();
    static QRegularExpression seqPattern(R"(.*(?:\d{2,}|%0\d+d|\#\#\#).*)");
    return seqPattern.match(base).hasMatch();
}

QString FFmpegPlayer::sequenceHead(const QString& filePath) const
{
    QFileInfo info(filePath);
    QString dir = info.absolutePath();
    QString base = info.completeBaseName();
    QRegularExpression digits(R"((\d+)(?!.*\d))");
    auto match = digits.match(base);
    if (!match.hasMatch()) {
        return info.absoluteFilePath();
    }
    QString head = base.left(match.capturedStart(1));
    return dir + "/" + head;
}

bool FFmpegPlayer::isFrameInCache(qint64 timestampMs, int frameIndex) const
{
    QString key = QString("ts_%1_frame_%2").arg(timestampMs).arg(frameIndex);
    return m_frameCache.contains(key);
}

void FFmpegPlayer::evictLRUEntries()
{
    if (m_frameCache.isEmpty()) {
        return;
    }
    
    // Sort by last accessed time (oldest first)
    QList<QString> keys = m_frameCache.keys();
    std::sort(keys.begin(), keys.end(), [this](const QString& a, const QString& b) {
        return m_frameCache[a].lastAccessed < m_frameCache[b].lastAccessed;
    });
    
    // Remove oldest 25% of entries
    int toRemove = std::max(1, static_cast<int>(m_frameCache.size() / 4));
    for (int i = 0; i < toRemove && i < keys.size(); ++i) {
        CacheEntry& entry = m_frameCache[keys[i]];
        m_currentMemoryUsage -= entry.compressedSize;
        if (entry.frameIndex >= 0) {
            m_cachedFrameIndices.remove(entry.frameIndex);
        }
        m_frameCache.remove(keys[i]);
    }
    
    emit cacheStatus(QString("Evicted %1 entries, memory usage: %2 MB").arg(toRemove).arg(m_currentMemoryUsage / 1024 / 1024));
}

void FFmpegPlayer::startPrefetch()
{
    if (m_streamType == StreamType::ImageSequence) {
        prefetchSequenceFrames(0);
    }
}

QImage FFmpegPlayer::extractThumbnail(const QString& filePath, const QSize& targetSize, qint64 positionMs)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    // Fast single-frame extraction optimized for thumbnails
    // No state management, no hardware acceleration overhead

    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    SwsContext* swsCtx = nullptr;
    QImage result;

    // Open video file
    int ret = avformat_open_input(&formatCtx, filePath.toUtf8().constData(), nullptr, nullptr);
    if (ret < 0) {
        qWarning() << "[FFmpegPlayer] extractThumbnail: Failed to open file:" << filePath;
        return QImage();
    }

    // Retrieve stream information
    ret = avformat_find_stream_info(formatCtx, nullptr);
    if (ret < 0) {
        qWarning() << "[FFmpegPlayer] extractThumbnail: Failed to find stream info:" << filePath;
        avformat_close_input(&formatCtx);
        return QImage();
    }

    // Find video stream
    int videoStreamIndex = av_find_best_stream(formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStreamIndex < 0) {
        qWarning() << "[FFmpegPlayer] extractThumbnail: No video stream found:" << filePath;
        avformat_close_input(&formatCtx);
        return QImage();
    }

    AVStream* videoStream = formatCtx->streams[videoStreamIndex];

    // Find decoder
    const AVCodec* codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    if (!codec) {
        qWarning() << "[FFmpegPlayer] extractThumbnail: Codec not found:" << filePath;
        avformat_close_input(&formatCtx);
        return QImage();
    }

    // Allocate codec context
    codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        qWarning() << "[FFmpegPlayer] extractThumbnail: Failed to allocate codec context:" << filePath;
        avformat_close_input(&formatCtx);
        return QImage();
    }

    // Copy codec parameters
    ret = avcodec_parameters_to_context(codecCtx, videoStream->codecpar);
    if (ret < 0) {
        qWarning() << "[FFmpegPlayer] extractThumbnail: Failed to copy codec parameters:" << filePath;
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return QImage();
    }

    // Open codec
    ret = avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        qWarning() << "[FFmpegPlayer] extractThumbnail: Failed to open codec:" << filePath;
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return QImage();
    }

    // Allocate frame and packet
    frame = av_frame_alloc();
    packet = av_packet_alloc();
    if (!frame || !packet) {
        qWarning() << "[FFmpegPlayer] extractThumbnail: Failed to allocate frame/packet:" << filePath;
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return QImage();
    }

    // Seek to position if specified
    if (positionMs > 0) {
        int64_t timestamp = (positionMs * videoStream->time_base.den) / (1000 * videoStream->time_base.num);
        ret = av_seek_frame(formatCtx, videoStreamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            qDebug() << "[FFmpegPlayer] extractThumbnail: Seek failed, using first frame:" << filePath;
        }
        avcodec_flush_buffers(codecCtx);
    }

    // Read and decode one frame
    bool frameDecoded = false;
    while (av_read_frame(formatCtx, packet) >= 0) {
        if (packet->stream_index == videoStreamIndex) {
            ret = avcodec_send_packet(codecCtx, packet);
            if (ret < 0) {
                av_packet_unref(packet);
                continue;
            }

            ret = avcodec_receive_frame(codecCtx, frame);
            if (ret == 0) {
                frameDecoded = true;
                av_packet_unref(packet);
                break;
            }
        }
        av_packet_unref(packet);
    }

    if (!frameDecoded) {
        qWarning() << "[FFmpegPlayer] extractThumbnail: Failed to decode frame:" << filePath;
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return QImage();
    }

    // Convert frame to RGB format
    const int width = frame->width;
    const int height = frame->height;

    swsCtx = sws_getContext(
        width, height, static_cast<AVPixelFormat>(frame->format),
        width, height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsCtx) {
        qWarning() << "[FFmpegPlayer] extractThumbnail: Failed to create scaling context:" << filePath;
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codecCtx);
        avformat_close_input(&formatCtx);
        return QImage();
    }

    // Allocate RGB buffer
    QImage image(width, height, QImage::Format_RGB888);
    uint8_t* dstData[1] = { image.bits() };
    int dstLinesize[1] = { static_cast<int>(image.bytesPerLine()) };

    // Convert frame to RGB
    sws_scale(swsCtx, frame->data, frame->linesize, 0, height, dstData, dstLinesize);

    result = image.copy();

    // Scale to target size if specified
    if (targetSize.isValid() && !result.isNull()) {
        result = result.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // Cleanup
    sws_freeContext(swsCtx);
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&formatCtx);

    qDebug() << "[FFmpegPlayer] extractThumbnail: Success for" << filePath << "size:" << width << "x" << height;
    return result;
#else
    Q_UNUSED(filePath);
    Q_UNUSED(targetSize);
    Q_UNUSED(positionMs);
    return QImage();
#endif
}