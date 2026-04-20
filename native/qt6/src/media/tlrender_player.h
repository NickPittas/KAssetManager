#ifndef TLRENDER_PLAYER_H
#define TLRENDER_PLAYER_H

#include <QObject>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QMutex>
#include <QTimer>
#include <QSize>
#include <QPointer>
#include <QSharedPointer>
#include <atomic>
#include <memory>

class MpvPlayer;
class FFmpegMovPlayer;

// Forward declarations for tlRender types
namespace ftk {
    class Context;
    class FontSystem;
    class LogSystem;
}

namespace tl {
    class Player;
    class Timeline;
    class System;
    struct VideoFrame;
    namespace qt {
        class ContextObject;
        class PlayerObject;
    }
    namespace gl {
        class Render;
        class TextureCache;
    }
}

/**
 * @brief Professional tlRender-based video player for Qt applications
 *
 * This class provides production-ready video playback using tlRender (mrv2's engine),
 * with tlRender-backed playback support.
 *
 * Key features:
 * - OpenGL 4.1 hardware-accelerated rendering
 * - OpenTimelineIO support (.otio, .otioz files)
 * - Image sequence playback (EXR, PNG, JPEG, TIFF, etc.)
 * - Video playback via FFmpeg (MP4, MOV, MKV, etc.)
 * - Frame-accurate seeking and scrubbing
 * - Audio playback with sync
 * - Playback rate control (including reverse)
 * - Loop modes (Once, Loop, PingPong)
 *
 * This replaces GStreamerPlayer in the current app stack.
 */
class TLRenderPlayer : public QObject
{
    Q_OBJECT

public:
    enum class PlaybackState {
        Stopped,
        Playing,
        Paused
    };
    Q_ENUM(PlaybackState)

    enum class LoopMode {
        Once,
        Loop,
        PingPong
    };
    Q_ENUM(LoopMode)

    struct MediaInfo {
        int width = 0;
        int height = 0;
        double fps = 0.0;
        qint64 durationMs = 0;
        qint64 totalFrames = 0;
        QString codec;
        bool hasAudio = false;
        int audioChannels = 0;
        int audioSampleRate = 0;
    };

    struct FrameAcquisitionStats {
        qint64 cachedFrameConversions = 0;
        qint64 fallbackExtractions = 0;
        qint64 unsupportedCachedFrameTypes = 0;
        int lastCachedImageType = -1;
        qint64 lastCachedConversionNs = 0;
        qint64 lastFallbackExtractionNs = 0;
        qint64 lastGetCurrentFrameNs = 0;
    };

    explicit TLRenderPlayer(QObject* parent = nullptr);
    ~TLRenderPlayer();

    // ========================================================================
    // Media Loading
    // ========================================================================

    /**
     * @brief Load media file (video, image sequence, or timeline)
     * @param filePath Path to video, image sequence pattern, or .otio file
     *
     * Supports:
     * - Video files: .mp4, .mov, .mkv, .avi, .webm, etc.
     * - Image sequences: file.####.exr, file_%04d.png, etc.
     * - Timelines: .otio, .otioz
     */
    void loadMedia(const QString& filePath);

    /**
     * @brief Alias for loadMedia() for API compatibility
     */
    inline void load(const QString& filePath) { loadMedia(filePath); }

    /**
     * @brief Unload current media and release resources
     */
    void unloadMedia();

    /**
     * @brief Check if media is currently loaded
     */
    bool hasMedia() const;

    /**
     * @brief Get the current media file path
     */
    QString currentMediaPath() const;

    // ========================================================================
    // Playback Control
    // ========================================================================

    void play();
    void pause();
    void stop();
    void togglePlayback();

    /**
     * @brief Seek to position in milliseconds
     */
    void seek(qint64 positionMs);

    /**
     * @brief Seek to specific frame number
     */
    void seekToFrame(qint64 frameNumber);

    /**
     * @brief Set playback rate (1.0 = normal, 2.0 = 2x, -1.0 = reverse)
     */
    void setPlaybackRate(double rate);
    double playbackRate() const;

    /**
     * @brief Set loop mode
     */
    void setLoopMode(LoopMode mode);
    LoopMode loopMode() const;

    // Frame stepping
    void stepForward();
    void stepBackward();
    void stepForwardBy(int frames);
    void stepBackwardBy(int frames);

    // Go to start/end
    void gotoStart();
    void gotoEnd();

    // ========================================================================
    // Audio Control
    // ========================================================================

    void setVolume(float volume); // 0.0 to 1.0
    float volume() const;

    void setMuted(bool muted);
    bool isMuted() const;

    // ========================================================================
    // State Queries
    // ========================================================================

    PlaybackState playbackState() const;
    qint64 position() const;      // Current position in milliseconds
    qint64 duration() const;      // Total duration in milliseconds
    qint64 currentFrame() const;  // Current frame number
    qint64 totalFrames() const;   // Total frame count
    MediaInfo mediaInfo() const;

    // ========================================================================
    // Rendering (for TLRenderWidget)
    // ========================================================================

    /**
     * @brief Get the shared tlRender context (static, for viewports)
     */
    static std::shared_ptr<ftk::Context> sharedContext();

    /**
     * @brief Get the shared Qt context object (static, for viewports)
     */
    static tl::qt::ContextObject* sharedContextObject();

    /**
     * @brief Get the tlRender context (for widget rendering)
     */
    std::shared_ptr<ftk::Context> context() const;

    /**
     * @brief Get the tlRender player (for widget rendering)
     */
    std::shared_ptr<tl::Player> player() const;
    MpvPlayer* mpvPlayer() const;
    FFmpegMovPlayer* ffmpegMovPlayer() const;

    /**
     * @brief Get the Qt PlayerObject wrapper (for native viewports)
     */
    QSharedPointer<tl::qt::PlayerObject> playerObject() const;

    /**
     * @brief Get the current video frames for rendering
     */
    std::vector<tl::VideoFrame> currentVideoFrames() const;

    /**
     * @brief Tick the player (call from render loop)
     */
    void tick();
    void refreshCurrentFrame();

    // ========================================================================
    // Frame Extraction (for thumbnails)
    // ========================================================================

    /**
     * @brief Extract a frame as QImage (for thumbnails)
     * @param targetSize Target size for the extracted frame
     * @return QImage of the current frame, or null image on failure
     */
    QImage getCurrentFrame(const QSize& targetSize = QSize());

    /**
     * @brief Static method to extract a thumbnail from a file
     */
    static QImage extractThumbnail(const QString& filePath, const QSize& targetSize, qint64 positionMs = 0);

    /**
     * @brief Static method to query duration of a media file
     */
    static qint64 queryDuration(const QString& filePath);

    FrameAcquisitionStats frameAcquisitionStatsForTest() const;
    void resetFrameAcquisitionStatsForTest();

    // ========================================================================
    // Static Initialization
    // ========================================================================

    /**
     * @brief Initialize tlRender (call once at application startup)
     */
    static void initialize();

    /**
     * @brief Check if tlRender is initialized
     */
    static bool isInitialized();

signals:
    //! Emitted when new video frames are available for rendering.
    void videoFramesChanged();

    void playbackStateChanged(TLRenderPlayer::PlaybackState state);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void currentFrameChanged(qint64 frameNumber);
    void mediaInfoReady(const TLRenderPlayer::MediaInfo& info);
    void error(const QString& errorString);
    void endOfStream();

private slots:
    void onUpdateTimer();

private:
    void ensureTlRenderContext();
    void setupContext();
    void updateMediaInfo();
    bool useMpvBackendForCurrentMedia(const QString& filePath) const;
    void stopManualReversePlayback();

    // tlRender objects
    std::shared_ptr<ftk::Context> m_context;
    std::shared_ptr<tl::System> m_system;
    std::shared_ptr<tl::Timeline> m_timeline;
    std::shared_ptr<tl::Player> m_player;
    MpvPlayer* m_mpvPlayer{nullptr};
    FFmpegMovPlayer* m_ffmpegMovPlayer{nullptr};

    // tlRender Qt integration (observes player + keeps context ticking).
    // Stored as QSharedPointer so it can be shared with native viewport.
    QSharedPointer<tl::qt::PlayerObject> m_playerObject;

    // Cached frames for rendering (to avoid blocking calls on the UI thread).
    std::vector<tl::VideoFrame> m_cachedVideoFrames;
    FrameAcquisitionStats m_frameAcquisitionStats;

    // State
    mutable QMutex m_mutex;
    std::atomic<PlaybackState> m_playbackState{PlaybackState::Stopped};
    std::atomic<qint64> m_position{0};
    std::atomic<qint64> m_duration{0};
    std::atomic<qint64> m_currentFrame{0};
    std::atomic<qint64> m_totalFrames{0};
    std::atomic<float> m_volume{1.0f};
    std::atomic<bool> m_muted{false};
    std::atomic<double> m_playbackRate{1.0};
    LoopMode m_loopMode{LoopMode::Loop};

    MediaInfo m_mediaInfo;
    QString m_currentPath;

    // Flag to defer playback until first frame is cached
    bool m_pendingPlay{false};
    bool m_pendingReverse{false};
    bool m_manualReversePlaybackActive{false};
    double m_manualReverseStepAccumulatorMs{0.0};

    // Update timer
    QTimer* m_updateTimer{nullptr};

    // Initialization flag
    static bool s_initialized;
    static bool s_fontsInitialized;
    static std::shared_ptr<ftk::Context> s_sharedContext;

    // Single context tick object for the shared context (Qt timer driven).
    static QPointer<tl::qt::ContextObject> s_contextObject;
};

#endif // TLRENDER_PLAYER_H
