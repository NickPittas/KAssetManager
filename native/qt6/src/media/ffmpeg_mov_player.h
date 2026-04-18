#ifndef FFMPEG_MOV_PLAYER_H
#define FFMPEG_MOV_PLAYER_H

#include <QObject>
#include <QImage>
#include <QMutex>
#include <QTimer>
#include <QString>
#include <QtGlobal>

#include "media_player_types.h"

struct AVCodecContext;
struct AVFormatContext;
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

signals:
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void currentFrameChanged(qint64 frameNumber);
    void mediaInfoReady(const media_player::MediaInfo& info);
    void playbackStateChanged(media_player::PlaybackState state);
    void error(const QString& errorString);
    void endOfStream();
    void frameUpdated();

private slots:
    void onPlaybackTick();
    void syncAudioPosition(qint64 positionMs);

private:
    bool openMedia(const QString& filePath, QString* errorString);
    void closeMedia();
    void updatePlaybackTimer();
    bool decodeNextFrame();
    bool decodeFrameForTimestamp(int64_t targetTs, bool allowPastTarget);
    bool seekInternal(qint64 positionMs, bool emitSignals);
    bool presentDecodedFrame(AVFrame* frame);
    qint64 timestampToMs(int64_t pts) const;
    int64_t msToTimestamp(qint64 positionMs) const;
    qint64 clampFrameNumber(qint64 frameNumber) const;
    void setPlaybackState(media_player::PlaybackState state);
    void clearPresentedFrame();

    mutable QMutex m_frameMutex;
    QImage m_currentFrameImage;

    QTimer* m_playbackTimer{nullptr};
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
    double m_stepAccumulator{0.0};

    AVFormatContext* m_formatContext{nullptr};
    AVCodecContext* m_codecContext{nullptr};
    SwsContext* m_swsContext{nullptr};
    AVFrame* m_decodeFrame{nullptr};
    AVFrame* m_rgbFrame{nullptr};
    AVPacket* m_packet{nullptr};
    uint8_t* m_rgbBuffer{nullptr};
    int m_videoStreamIndex{-1};
    int m_timeBaseNum{0};
    int m_timeBaseDen{1};
    int m_frameRateNum{0};
    int m_frameRateDen{1};
    int64_t m_lastPresentedPts{-1};
};

#endif
