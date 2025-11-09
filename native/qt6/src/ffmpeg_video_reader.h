#pragma once

#include <QObject>
#include <QImage>
#include <atomic>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}
#include <QElapsedTimer>
#endif

// Generic FFmpeg-based video reader that decodes to RGBA QImage and emits frames with PTS (ms)
// Supports ProRes, DNxHD/DNxHR, PNG-in-MOV, Animation (qtrle), MXF, etc.
class FfmpegVideoReader : public QObject {
    Q_OBJECT
public:
    explicit FfmpegVideoReader(const QString& path, bool dropLateFrames = true)
        : m_path(path)
        , m_dropLateFrames(dropLateFrames) {}

public slots:
    void start();
    void stop() { m_stop = true; }

    void setPaused(bool p) { m_paused = p; }
    void stepOnce() { m_singleStep = true; }
    void seekToMs(qint64 ms) { m_seekTargetMs = ms; m_seekRequested = true; }
    void setDropLateFrames(bool on) { m_dropLateFrames = on; }

signals:
    void frameReady(const QImage& image, qint64 ptsMs);
    void finished();

public:
    double fps() const { return m_fps; }
    qint64 durationMs() const { return m_durationMs; }

private:
#ifdef HAVE_FFMPEG
    bool open();
    static void cleanup(AVPacket* pkt, AVFrame* frame, AVFrame* rgba) {
        if (frame) av_frame_free(&frame);
        if (rgba) av_frame_free(&rgba);
        if (pkt) av_packet_free(&pkt);
    }
#endif

private:
    QString m_path;
#ifdef HAVE_FFMPEG
    AVFormatContext* m_fmt = nullptr;
    AVStream* m_stream = nullptr;
    AVCodecContext* m_ctx = nullptr;
    SwsContext* m_sws = nullptr;
    int m_vIdx = -1;
    double m_fps = 24.0;
    qint64 m_durationMs = 0;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_singleStep{false};
    std::atomic<bool> m_seekRequested{false};
    std::atomic<qint64> m_seekTargetMs{0};
    bool m_dropLateFrames = true;
#else
    double m_fps = 24.0;
    qint64 m_durationMs = 0;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_singleStep{false};
    std::atomic<bool> m_seekRequested{false};
    std::atomic<qint64> m_seekTargetMs{0};
    bool m_dropLateFrames = true;
#endif
};

