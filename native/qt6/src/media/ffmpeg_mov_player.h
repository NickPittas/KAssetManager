#ifndef FFMPEG_MOV_PLAYER_H
#define FFMPEG_MOV_PLAYER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QChronoTimer>
#include <QElapsedTimer>
#include <QString>
#include <QtGlobal>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#include "media_player_types.h"

#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
extern "C" {
#include <libavutil/pixfmt.h>
}
#endif

struct AVCodecContext;
struct AVFormatContext;
struct AVBufferRef;
struct AVFrame;
struct AVPacket;
struct SwsContext;
class FFmpegMovPlayer : public QObject
{
    Q_OBJECT

public:
    explicit FFmpegMovPlayer(QObject* parent = nullptr);
    ~FFmpegMovPlayer() override;

    void loadMedia(const QString& filePath);
    void unloadMedia();
    bool hasMedia() const;

    void play();
    void pause();
    void stop();
    void seek(qint64 positionMs);
    void seekToFrame(qint64 frameNumber);
    void stepForward();
    void stepBackward();

    void setPlaybackRate(double rate);
    double playbackRate() const { return m_playbackRate; }
    void setLoopMode(media_player::LoopMode mode);
    media_player::LoopMode loopMode() const { return m_loopMode; }

    void setVolume(float volume);
    float volume() const { return m_volume; }
    void setMuted(bool muted);
    bool isMuted() const { return m_muted; }

    media_player::PlaybackState playbackState() const { return m_playbackState; }
    qint64 position() const { return m_positionMs; }
    qint64 duration() const { return m_durationMs; }
    qint64 currentFrame() const { return m_currentFrame; }
    qint64 totalFrames() const { return m_totalFrames; }
    media_player::MediaInfo mediaInfo() const { return m_mediaInfo; }
    QString currentMediaPath() const { return m_currentPath; }

    QImage currentFrameImage() const;
    QImage currentFrameImage(const QSize& targetSize);
    QSize currentFrameSize() const;

signals:
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void currentFrameChanged(qint64 frameNumber);
    void debugFramePresented(qint64 frameNumber, qint64 positionMs, qint64 pts);
    void mediaInfoReady(const media_player::MediaInfo& info);
    void playbackStateChanged(media_player::PlaybackState state);
    void error(const QString& errorString);
    void endOfStream();
    void frameUpdated();

private slots:
    void onPlaybackTick();
    void syncAudioPosition(qint64 positionMs);

private:
    struct BufferedFrame {
        QImage image;
        QImage scaledImage;
        QSize scaledTargetSize;
        qint64 positionMs{0};
        qint64 frameNumber{0};
        qint64 pts{0};
    };

    bool openMedia(const QString& filePath, QString* errorString, bool allowHardwareDecoding = true);
    void closeMedia();
    void startDecodeThread();
    void stopDecodeThread();
    void decodeThreadMain();
    void updatePlaybackTimer();
    bool decodeNextFrame(quint64 generation);
    bool decodeFrameForTimestamp(int64_t targetTs, bool allowPastTarget, quint64 generation, bool preferPreviousFrame = false);
    bool seekInternal(qint64 positionMs, bool emitSignals, bool clearHistory = true);
    bool seekToTimestampInternal(int64_t targetTs, bool emitSignals, bool preferPreviousFrame = false, bool clearHistory = true);
    bool presentDecodedFrame(AVFrame* frame, BufferedFrame* bufferedFrame);
    bool queueBufferedFrame(BufferedFrame&& frame, quint64 generation, bool satisfySeek);
    bool waitForSeekFrame(quint64 generation, BufferedFrame* frame, QString* errorString = nullptr);
    bool takeBufferedFrameForPlayback(qint64 targetPositionMs, BufferedFrame* frame, bool* reachedEndOfStream);
    void presentBufferedFrame(const BufferedFrame& frame, bool emitSignals, bool pushToBackwardBuffer = true);
    void rememberPresentedFrame(const BufferedFrame& frame);
    bool takePreviousPresentedFrame(BufferedFrame* frame);
    void clearPresentedHistory();
    void pushBackwardFrame(const BufferedFrame& frame);
    bool popBackwardFrame(BufferedFrame* frame);
    void clearBackwardFrames();
    int decodeQueueCapacity() const;
    void resetPlaybackClock();
    bool fallbackToSoftwareDecoding(qint64 restartPositionMs, quint64 generation, bool satisfySeek, QString* errorString = nullptr);
    bool ensureConversionContext(AVFrame* frame);
    qint64 timestampToMs(int64_t pts) const;
    int64_t msToTimestamp(qint64 positionMs) const;
    int64_t frameToTimestamp(qint64 frameNumber) const;
    qint64 timestampToFrame(int64_t pts, qint64 fallbackPositionMs) const;
    qint64 clampFrameNumber(qint64 frameNumber) const;
    void setPlaybackState(media_player::PlaybackState state);
    void clearPresentedFrame();
    static AVPixelFormat selectHardwarePixelFormat(AVCodecContext* codecContext, const AVPixelFormat* pixelFormats);

    mutable QMutex m_frameMutex;
    QImage m_currentFrameImage;
    QImage m_currentFrameScaledImage;
    QSize m_currentFrameScaledTargetSize;
    QSize m_requestedFrameTargetSize;
    QImage m_conversionScratchImage;

    QChronoTimer* m_playbackTimer{nullptr};
    QElapsedTimer m_playbackClock;
    std::chrono::nanoseconds m_nextPlaybackDeadline{std::chrono::nanoseconds::zero()};
    qint64 m_playbackStartPositionMs{0};
    class QMediaPlayer* m_audioPlayer{nullptr};
    class QAudioOutput* m_audioOutput{nullptr};
    media_player::PlaybackState m_playbackState{media_player::PlaybackState::Stopped};
    media_player::LoopMode m_loopMode{media_player::LoopMode::Loop};
    media_player::MediaInfo m_mediaInfo;
    QString m_currentPath;
    qint64 m_positionMs{0};
    qint64 m_durationMs{0};
    qint64 m_currentFrame{0};
    qint64 m_totalFrames{0};
    float m_volume{1.0f};
    bool m_muted{false};
    double m_playbackRate{1.0};

    AVFormatContext* m_formatContext{nullptr};
    AVCodecContext* m_codecContext{nullptr};
    AVBufferRef* m_hwDeviceContext{nullptr};
    SwsContext* m_swsContext{nullptr};
    AVFrame* m_decodeFrame{nullptr};
    AVFrame* m_transferFrame{nullptr};
    AVPacket* m_packet{nullptr};
    int m_videoStreamIndex{-1};
    int m_timeBaseNum{0};
    int m_timeBaseDen{1};
    int m_frameRateNum{0};
    int m_frameRateDen{1};
    int m_hwPixelFormat{-1};
    int m_swsSourceFormat{-1};
    int m_swsWidth{0};
    int m_swsHeight{0};
    int64_t m_lastPresentedPts{-1};
    bool m_hardwareDecodingActive{false};

    std::mutex m_decodeMutex;
    std::condition_variable m_decodeCondition;
    std::deque<BufferedFrame> m_decodedFrames;
    std::deque<BufferedFrame> m_presentedFrames;
    std::deque<BufferedFrame> m_backwardFrames;
    BufferedFrame m_seekResultFrame;
    std::thread m_decodeThread;
    bool m_decodeStopRequested{false};
    bool m_seekPending{false};
    bool m_seekInProgress{false};
    bool m_seekFailed{false};
    bool m_seekResultReady{false};
    bool m_decodeAtEnd{false};
    qint64 m_pendingSeekMs{0};
    int64_t m_pendingSeekTs{0};
    bool m_pendingSeekPreferPreviousFrame{false};
    quint64 m_decodeGeneration{0};
    QString m_pendingDecodeError;
};

#endif
