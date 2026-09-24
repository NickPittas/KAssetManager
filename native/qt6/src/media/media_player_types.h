#ifndef MEDIA_PLAYER_TYPES_H
#define MEDIA_PLAYER_TYPES_H

#include <QtCore/QString>
#include <QtCore/qglobal.h>

namespace media_player {

enum class PlaybackState {
    Stopped,
    Playing,
    Paused
};

enum class LoopMode {
    Once,
    Loop,
    PingPong
};

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

} // namespace media_player

#endif
