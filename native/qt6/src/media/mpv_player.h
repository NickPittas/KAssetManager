#ifndef MPV_PLAYER_H
#define MPV_PLAYER_H

#include <QtCore/QObject>
#include <QtGui/QImage>
#include <QtCore/QMutex>
#include <QtCore/QPointer>

#include "media_player_types.h"
#include "libmpv_runtime.h"

class MpvPlayer : public QObject
{
    Q_OBJECT

public:
    explicit MpvPlayer(QObject* parent = nullptr);
    ~MpvPlayer() override;

    bool isAvailable() const;
    QString availabilityError() const;

    void loadMedia(const QString& filePath);
    void unloadMedia();
    bool hasMedia() const;
    bool isViewportReady() const { return m_viewportReady; }

    void play();
    void pause();
    void stop();
    void seek(qint64 positionMs);
    void seekToFrame(qint64 frameNumber);
    void setPlaybackRate(double rate);
    double playbackRate() const { return m_playbackRate; }
    void setLoopMode(media_player::LoopMode mode);
    media_player::LoopMode loopMode() const { return m_loopMode; }
    void stepForward();
    void stepBackward();

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
    void setLastFramebufferImage(const QImage& image);

    mpv_handle* handle() const { return m_mpv; }

signals:
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void currentFrameChanged(qint64 frameNumber);
    void mediaInfoReady(const media_player::MediaInfo& info);
    void playbackStateChanged(media_player::PlaybackState state);
    void error(const QString& errorString);
    void endOfStream();
    void frameUpdated();
    void viewportReadyChanged(bool ready);

private slots:
    void onMpvEvents();

private:
    static void wakeup(void* ctx);
    void processEvent(mpv_event* event);
    void updatePlaybackStateFromPause(bool paused);
    void updateMediaInfoFromCore();
    double fpsOrDefault() const;
    void setViewportReady(bool ready);

    const LibMpvRuntime& m_runtime;
    mpv_handle* m_mpv{nullptr};
    mutable QMutex m_frameMutex;
    QImage m_lastFramebufferImage;

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
    bool m_viewportReady{false};
    double m_playbackRate{1.0};
};

#endif
