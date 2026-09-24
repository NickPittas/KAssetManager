#pragma once
#include <QString>

namespace MediaInfo {
struct VideoMetadata {
    QString videoCodec;
    QString videoProfile; // e.g., HIGH, 422 HQ, 4444 XQ, MAIN10
    QString audioCodec;
    int audioChannels = 0;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    qint64 bitrate = 0; // bits per second (container, video, or audio fallback)
    bool hasTimecode = false;
    QString timecodeStart; // e.g., HH:MM:SS:FF or HH:MM:SS;FF

    // Camera / production metadata extracted from container/stream tags
    QString cameraName;
    QString cameraModel;
    QString lens;
    QString reelName;
    QString scene;
    QString take;
};

// Probes a media file for video/audio codec, resolution, fps and bitrate.
// Returns true on success. On failure, returns false and optionally fills errorMessage.
bool probeVideoFile(const QString& filePath, VideoMetadata& out, QString* errorMessage = nullptr);
}

