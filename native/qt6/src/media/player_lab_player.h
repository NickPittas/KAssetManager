#ifndef PLAYER_LAB_PLAYER_H
#define PLAYER_LAB_PLAYER_H

#include <QObject>
#include <QImage>
#include <QString>
#include <QMutex>
#include <QSize>
#include <atomic>

#include <chrono>
#include <functional>
 #include <memory>

#include "player_types.h"

class FFmpegMovPlayer;
class QAudioSink;
class QIODevice;

namespace player_lab {
class GpuPlayer;
} // namespace player_lab

/**
 * @brief Application media player facade backed by the player_lab GPU player.
 *
 * The class name is retained for existing UI call sites. Playback is Wayland-first:
 * PlayerLabPlayer delegates video decode/control to player_lab::GpuPlayer and
 * PlayerLabViewport presents frames with QPainter.
 */
class PlayerLabPlayer : public QObject
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

    explicit PlayerLabPlayer(QObject* parent = nullptr);
    ~PlayerLabPlayer() override;

    void loadMedia(const QString& filePath);
    inline void load(const QString& filePath) { loadMedia(filePath); }
    void unloadMedia();
    bool hasMedia() const;
    QString currentMediaPath() const;

    void play();
    void pause();
    void stop();
    void togglePlayback();
    void seek(qint64 positionMs);
    void seekAsync(qint64 positionMs);
    void seekToFrame(qint64 frameNumber);
    void setPlaybackRate(double rate);
    double playbackRate() const;
    void setLoopMode(LoopMode mode);
    LoopMode loopMode() const;
    void stepForward();
    void stepBackward();
    void stepForwardBy(int frames);
    void stepBackwardBy(int frames);
    void gotoStart();
    void gotoEnd();

    void setVolume(float volume);
    float volume() const;
    void setMuted(bool muted);
    bool isMuted() const;

    PlaybackState playbackState() const;
    qint64 position() const;
    qint64 duration() const;
    qint64 currentFrame() const;
    qint64 totalFrames() const;
    MediaInfo mediaInfo() const;

    // Preserved for ABI/source compatibility with existing call sites. The
    // player_lab backend does not expose an FFmpegMovPlayer, so this always
    // returns nullptr.
    FFmpegMovPlayer* ffmpegMovPlayer() const;

    // Expose the player_lab backend so a GL-backed viewer widget (e.g.
    // player_lab::GpuPlayerWidget) can attach the decoder and render via GL
    // instead of the raster getCurrentFrame() path.
    player_lab::GpuPlayer* gpuPlayer();

    // When an external GL presenter (e.g. player_lab::GpuPlayerWidget) is
    // attached, it owns the decoder's video frame queue. PlayerLabPlayer then
    // stops popping frames on its own (raster) presentation path and instead
    // reports the external presenter's on-screen PTS. Pass an empty provider
    // to detach.
    void setExternalVideoPresenterPtsProvider(std::function<double()> ptsProvider);

    void tick();
    void refreshCurrentFrame();
    QImage getCurrentFrame(const QSize& targetSize = QSize());

    static void initialize();
    static bool isInitialized();
    static QImage extractThumbnail(const QString& filePath, const QSize& targetSize, qint64 positionMs = 0);
    static qint64 queryDuration(const QString& filePath);

    FrameAcquisitionStats frameAcquisitionStatsForTest() const;
    void resetFrameAcquisitionStatsForTest();

signals:
    void videoFramesChanged();
    void playbackStateChanged(PlayerLabPlayer::PlaybackState state);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void currentFrameChanged(qint64 frameNumber);
    void mediaInfoReady(const PlayerLabPlayer::MediaInfo& info);
    void error(const QString& errorString);
    void endOfStream();

private:
    // player_lab backend. Owns the decoder and presentation timer.
    std::unique_ptr<player_lab::GpuPlayer> m_gpuPlayer;

    mutable QMutex m_statsMutex;
    FrameAcquisitionStats m_frameAcquisitionStats;

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
    static bool s_initialized;

    // --- Audio (demo-equivalent QAudioSink pump) ---
    std::unique_ptr<QAudioSink> m_audioSink;
    QIODevice* m_audioDevice{nullptr};
    player_lab::AudioPacketPtr m_pendingAudio;
    size_t m_pendingAudioOffset{0};

    // --- Raster presentation state for getCurrentFrame() ---
    // Mirrors player_lab::GpuPlayerWidget's media-timed presentation clock,
    // but stores the RGBA pixels in a QImage instead of a GL texture.
    QImage m_currentFrameImage;
    player_lab::FramePacketPtr m_currentFramePacket;
    QImage m_scaledFrameImage;
    QSize m_scaledTargetSize;
    double m_currentPtsSeconds{-1.0};
    double m_mediaPtsBase{0.0};
    std::chrono::steady_clock::time_point m_wallBase;
    bool m_clockArmed{false};
    quint64 m_freshFrameRequestSerial{0};
    // --- External GL presenter (full-viewer) ---
    // When a player_lab::GpuPlayerWidget owns the decoder queue, this is true
    // and the raster presentation path stays idle. The provider returns the
    // external presenter's on-screen PTS so the facade position stays correct.
    bool m_externalVideoPresenterActive{false};
    std::function<double()> m_externalVideoPresenterPtsProvider;

    // Helpers.
    void setupAudio();
    void resetAudio(bool keepSuspended = false);
    void restartAudioIfPlaying();
    void pumpAudio();
    // Best available on-screen video PTS (seconds), or -1 if none is known.
    // Used to pace audio so it does not surge ahead of, or lag far behind,
    // the displayed frame. Prefers the external GL presenter's PTS, then the
    // raster presentation clock, then the backend currentPts() fallback.
    double currentVideoPtsSeconds() const;
    void requestFreshFrame();
    void presentFreshFrameForRequest(quint64 requestSerial);
    void presentDueFrames();
    void publishPresentedFrame(const player_lab::FramePacketPtr& frame);
    void publishPosition(double ptsSeconds);
};

#endif // PLAYER_LAB_PLAYER_H
