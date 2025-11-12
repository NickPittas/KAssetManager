#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QMutex>
#include <QTimer>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <memory>
#include <atomic>

// FFmpeg includes
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
#include <libavutil/hwcontext.h>
}
#endif

/**
 * @brief Unified FFmpeg Player for video and image sequence playback
 * 
 * This class consolidates all FFmpeg operations into a single, optimized
 * implementation with hardware acceleration support and smart caching.
 * 
 * Key features:
 * - Hardware-accelerated decoding (CUDA, QuickSync, D3D11, VAAPI)
 * - Smart frame compression and caching
 * - Progressive loading (preview -> full quality)
 * - Predictive prefetching
 * - Unified API for videos and image sequences
 * 
 * Thread Safety:
 * - All public methods are thread-safe via internal QMutex
 * - Signals are emitted from appropriate threads
 * - Resource cleanup is handled via RAII patterns
 */
class FFmpegPlayer : public QObject
{
    Q_OBJECT

public:
    struct VideoFrame {
        QImage image;
        qint64 timestampMs = 0;
        double fps = 0.0;
        QString codec;
        int width = 0;
        int height = 0;
        bool isValid() const { return !image.isNull(); }
    };

    struct MediaInfo {
        QString codec;
        int width = 0;
        int height = 0;
        double fps = 0.0;
        qint64 durationMs = 0;
        bool hasAudio = false;
        bool hasHardwareAcceleration = false;
        QStringList supportedAccelerations;
    };

    enum class PlaybackState {
        Stopped,
        Playing,
        Paused,
        Loading,
        Error
    };

    enum class StreamType {
        Video,
        ImageSequence,
        Unsupported // For fallback
    };

    enum class Quality {
        Preview,  // Low resolution for fast loading
        Full      // Full resolution
    };

    explicit FFmpegPlayer(QObject* parent = nullptr);
    ~FFmpegPlayer();

    // Core API
    void loadVideo(const QString& filePath);
    void loadImageSequence(const QStringList& framePaths, qint64 startFrame = 0, qint64 endFrame = -1);
    
    // Playback control
    void play();
    void pause();
    void stop();
    void seek(qint64 positionMs);
    void seekToFrame(int frameIndex);
    
    // Frame stepping (for sequences)
    void nextFrame();
    void previousFrame();
    
    // Direct frame decoding (for external integration)
    VideoFrame decodeVideoFrame(qint64 targetMs, Quality quality = Quality::Full);
    
    // Getters
    PlaybackState playbackState() const { return m_playbackState.load(); }
    MediaInfo mediaInfo() const;
    qint64 currentPosition() const { return m_currentPosition.load(); }
    qint64 duration() const { return m_duration.load(); }
    int currentFrame() const { return m_currentFrame.load(); }
    int totalFrames() const { return m_totalFrames.load(); }
    bool isHardwareAccelerated() const {
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
        return m_hardwareAcceleration && m_hwContext != nullptr;
#else
        return false;
#endif
    }
    
    // Configuration
    void setHardwareAcceleration(bool enabled) { m_enableHardwareAcceleration = enabled; }
    bool hardwareAccelerationEnabled() const { return m_enableHardwareAcceleration; }
    void setMaxCacheSize(int maxFrames) { m_maxCacheSize = std::max(10, maxFrames); }
    int maxCacheSize() const { return m_maxCacheSize; }
    void setQuality(Quality quality) { m_quality = quality; }
    Quality quality() const { return m_quality; }

signals:
    void frameReady(const VideoFrame& frame);
    void playbackStateChanged(PlaybackState state);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void frameIndexChanged(int frameIndex);
    void error(const QString& errorString);
    void mediaInfoReady(const MediaInfo& info);
    void cacheStatus(const QString& status);

private slots:
    void onTimerTick();
    void onPrefetchTimer();

private:
    // Internal methods
    void initialize();
    void cleanup();
    void detectHardwareAcceleration();
    StreamType detectStreamType(const QString& filePath);
    MediaInfo probeMediaInfo(const QString& filePath);
    
    // Decoding methods (public for external integration)
    VideoFrame decodeSequenceFrame(int frameIndex, Quality quality = Quality::Full);
    VideoFrame decodeFrameWithFFmpeg(qint64 targetMs, const QString& filePath);
    
    // Hardware acceleration
    bool initializeHardwareContext();
    void cleanupHardwareContext();
    bool isFrameAccelerated() const;
    
    // Caching
    void prefetchFrames(int currentFrame);
    void prefetchSequenceFrames(int currentFrame);
    void prefetchVideoFrames(int currentFrame);
    VideoFrame getCachedFrame(qint64 timestampMs, int frameIndex);
    void cacheFrame(const VideoFrame& frame, qint64 timestampMs, int frameIndex);
    bool isFrameInCache(qint64 timestampMs, int frameIndex) const;
    void evictLRUEntries();
    void startPrefetch();
    
    // Utility
    QString getFFmpegErrorString(int errorCode) const;
    bool isImageSequence(const QString& filePath) const;
    QString sequenceHead(const QString& filePath) const;

    // RAII wrappers for FFmpeg resources
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    struct AvFormatCtxDeleter {
        void operator()(AVFormatContext* p) const {
            if (p) avformat_close_input(&p);
        }
    };
    
    struct AvCodecCtxDeleter {
        void operator()(AVCodecContext* p) const {
            if (p) avcodec_free_context(&p);
        }
    };
    
    struct AvPacketDeleter {
        void operator()(AVPacket* p) const {
            if (p) av_packet_free(&p);
        }
    };
    
    struct AvFrameDeleter {
        void operator()(AVFrame* p) const {
            if (p) av_frame_free(&p);
        }
    };
    
    struct SwsCtxDeleter {
        void operator()(SwsContext* p) const {
            if (p) sws_freeContext(p);
        }
    };
#endif

    // State
    std::atomic<PlaybackState> m_playbackState{PlaybackState::Stopped};
    std::atomic<qint64> m_currentPosition{0};
    std::atomic<qint64> m_duration{0};
    std::atomic<int> m_currentFrame{0};
    std::atomic<int> m_totalFrames{0};
    
    // Configuration
    bool m_enableHardwareAcceleration = true;
    Quality m_quality = Quality::Preview;
    int m_maxCacheSize = 100;
    
    // Threading
    mutable QMutex m_mutex;
    QTimer* m_playbackTimer;
    QTimer* m_prefetchTimer;
    
    // Media state
    QString m_currentFilePath;
    QStringList m_sequenceFramePaths;
    StreamType m_streamType = StreamType::Unsupported;
    MediaInfo m_mediaInfo;
    
    // FFmpeg context
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    std::unique_ptr<AVFormatContext, AvFormatCtxDeleter> m_formatCtx;
    std::unique_ptr<AVCodecContext, AvCodecCtxDeleter> m_videoCodecCtx;
    std::unique_ptr<AVCodecContext, AvCodecCtxDeleter> m_audioCodecCtx;
    std::unique_ptr<AVPacket, AvPacketDeleter> m_packet;
    std::unique_ptr<AVFrame, AvFrameDeleter> m_frame;
    std::unique_ptr<SwsContext, SwsCtxDeleter> m_swsCtx;
    AVBufferRef* m_hwContext = nullptr;
    AVBufferRef* m_hwDeviceCtx = nullptr;
    
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;
    bool m_hardwareAcceleration = false;
    QString m_hardwareAccelerationType;
#endif
    
    // Caching
    struct CacheEntry {
        VideoFrame frame;
        qint64 timestampMs = 0;
        int frameIndex = -1;
        qint64 lastAccessed = 0;
        int accessCount = 0;
        qint64 compressedSize = 0; // Size in bytes when compressed
    };
    
    QHash<QString, CacheEntry> m_frameCache;
    QSet<int> m_cachedFrameIndices;
    qint64 m_maxMemoryUsage = 512 * 1024 * 1024; // 512MB default
    qint64 m_currentMemoryUsage = 0;
    
    // Performance tracking
    QElapsedTimer m_performanceTimer;
    quint64 m_totalDecodedFrames = 0;
    quint64 m_cacheHits = 0;
    quint64 m_cacheMisses = 0;
    double m_averageDecodeTime = 0.0;
    
    // Cancellation
    std::atomic<bool> m_cancellationRequested{false};
    
    // Constants
    static constexpr qint64 kDefaultFrameIntervalMs = 40; // 25 FPS
    static constexpr int kPrefetchWindowSize = 50;
    static constexpr int kPrefetchConcurrency = 4;
    static constexpr double kCompressionQuality = 0.85; // JPEG quality
};

// Inline utility functions
inline QString FFmpegPlayer::getFFmpegErrorString(int errorCode) const {
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(errorCode, buf, sizeof(buf));
    return QString::fromUtf8(buf);
#else
    Q_UNUSED(errorCode);
    return QString("FFmpeg not available");
#endif
}