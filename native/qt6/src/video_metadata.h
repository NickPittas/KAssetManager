#pragma once
#include <QString>

namespace MediaInfo {
struct VideoMetadata {
    QString videoCodec;
    QString videoProfile; // e.g., HIGH, 422 HQ, 4444 XQ, MAIN10
    QString audioCodec;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    qint64 bitrate = 0;
};

// Probes a media file for video/audio codec, resolution, fps and bitrate.
// Returns true on success. On failure, returns false and optionally fills errorMessage.
bool probeVideoFile(const QString& filePath, VideoMetadata& out, QString* errorMessage = nullptr);
}

