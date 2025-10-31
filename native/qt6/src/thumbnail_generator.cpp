#include "thumbnail_generator.h"
#include "progress_manager.h"
#include "log_manager.h"
#include "oiio_image_loader.h"
#include "video_metadata.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QFile>

#include <QStandardPaths>
#include <QCoreApplication>
#include <QDateTime>
#include <QPainter>
#include <QColor>
#include <QBrush>
#include <QFont>
#include <QImageReader>
#include <QDebug>
#include <QMutexLocker>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>
#include <QTimer>
#include <QRegularExpression>

#ifdef _MSC_VER
#include <windows.h>
#endif

namespace {
inline bool thumbnailDiagEnabled() {
    static const bool enabled = !qEnvironmentVariableIsEmpty("KASSET_THUMBNAIL_DIAG");
    return enabled;
}
}

#ifdef HAVE_FFMPEG

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
}
#endif

// ThumbnailTask implementation (for images only)
ThumbnailTask::ThumbnailTask(const QString& filePath, ThumbnailGenerator* generator, int sessionId)
    : m_filePath(filePath), m_generator(generator), m_sessionId(sessionId) {
    setAutoDelete(true);
}

void ThumbnailTask::run() {
    // Fast-cancel if session changed
    if (m_generator->m_sessionId.load() != m_sessionId) {
        // Remove from pending and return without work
        QString filePath = m_filePath;
        ThumbnailGenerator* generator = m_generator;
        QMetaObject::invokeMethod(generator, [generator, filePath]() {
            QMutexLocker locker(&generator->m_mutex);
            generator->m_pendingThumbnails.remove(filePath);
            generator->m_pendingImageTasks--;
            generator->updateThreadPoolLimit();
            generator->updateProgress();
        }, Qt::QueuedConnection);
        return;
    }

    QString thumbnailPath;
    bool success = false;

    try {
        thumbnailPath = m_generator->generateImageThumbnail(m_filePath);
        success = !thumbnailPath.isEmpty();
    } catch (...) {
        qCritical() << "[ThumbnailTask] Exception during image thumbnail generation:" << m_filePath;
    }


    // CRITICAL: Don't capture 'this' because ThumbnailTask will be deleted after run() completes
    // Capture only the values we need by value
    QString filePath = m_filePath;
    ThumbnailGenerator* generator = m_generator;

    QMetaObject::invokeMethod(generator, [generator, filePath, thumbnailPath, success]() {
        QMutexLocker locker(&generator->m_mutex);
        generator->m_pendingThumbnails.remove(filePath);
        generator->m_pendingImageTasks--;
        generator->updateThreadPoolLimit();
        // Update progress
        generator->updateProgress();
        if (success) {
            emit generator->thumbnailGenerated(filePath, thumbnailPath);
        } else {
            emit generator->thumbnailFailed(filePath);
        }
    }, Qt::QueuedConnection);

}


// VideoFFmpegTask implementation (fallback path)
VideoFFmpegTask::VideoFFmpegTask(const QString& filePath, const QString& cachePath, ThumbnailGenerator* generator)
    : m_filePath(filePath), m_cachePath(cachePath), m_generator(generator)
{
    setAutoDelete(true);
}

// Helper: decode first frame and save thumbnail using FFmpeg
bool VideoFFmpegTask::decodeAndSave()
{
#ifndef HAVE_FFMPEG
    return false;
#else
    // Reduce FFmpeg logging noise once
    static bool s_logSet = false;
    if (!s_logSet) { av_log_set_level(AV_LOG_ERROR); s_logSet = true; }

    AVFormatContext* fmt = nullptr;
    QByteArray localPath = QFile::encodeName(m_filePath);
    if (avformat_open_input(&fmt, localPath.constData(), nullptr, nullptr) < 0) {
        qWarning() << "[VideoFFmpegTask] avformat_open_input failed for" << m_filePath;
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        qWarning() << "[VideoFFmpegTask] avformat_find_stream_info failed";
        avformat_close_input(&fmt);
        return false;
    }

    int vIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIdx < 0) {
        qWarning() << "[VideoFFmpegTask] No video stream";
        avformat_close_input(&fmt);
        return false;
    }

    AVStream* vs = fmt->streams[vIdx];
    AVCodecParameters* vp = vs->codecpar;
    const AVCodec* dec = avcodec_find_decoder(vp->codec_id);
    if (!dec) {
        const AVCodecDescriptor* desc = avcodec_descriptor_get(vp->codec_id);
        const char* name = desc ? desc->name : "?";
        qWarning() << "[VideoFFmpegTask] Decoder not found for codec" << (int)vp->codec_id << "(" << name << ")";

        // Special-case: MOV with PNG-coded frames. Many stock clips store full PNG images per packet.
        if (vp->codec_id == AV_CODEC_ID_PNG) {
            // Rewind to start and scan packets for a PNG signature
            av_seek_frame(fmt, vIdx, 0, AVSEEK_FLAG_BACKWARD);
            AVPacket pkt;
            av_init_packet(&pkt);
            const uint8_t sig[8] = {0x89, 'P','N','G', 0x0D, 0x0A, 0x1A, 0x0A};
            int scanned = 0;
            while (scanned < 1024 && av_read_frame(fmt, &pkt) >= 0) {
                if (pkt.stream_index == vIdx && pkt.size >= 8 && pkt.data) {
                    int limit = pkt.size - 8;
                    int pos = -1;
                    for (int i = 0; i <= limit; ++i) {
                        if (pkt.data[i] == sig[0]) {
                            bool match = true;
                            for (int j = 1; j < 8; ++j) {
                                if (pkt.data[i + j] != sig[j]) { match = false; break; }
                            }
                            if (match) { pos = i; break; }
                        }
                    }
                    if (pos >= 0) {
                        QByteArray bytes(reinterpret_cast<const char*>(pkt.data + pos), pkt.size - pos);
                        QImage img = QImage::fromData(bytes, "PNG");
                        if (!img.isNull()) {
                            // Scale and save using same policy as QMedia path
                            QImage thumb = img.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                            QString cachePath = m_cachePath;
                            if (thumb.hasAlphaChannel()) {
                                cachePath.replace(QRegularExpression("\\.jpg$", QRegularExpression::CaseInsensitiveOption), ".png");
                            }
                            const char* fmtName = thumb.hasAlphaChannel() ? "PNG" : "JPEG";
                            int quality = thumb.hasAlphaChannel() ? 100 : 85;
                            bool ok = thumb.save(cachePath, fmtName, quality);
                            av_packet_unref(&pkt);
                            avformat_close_input(&fmt);
                            if (!ok) {
                                qWarning() << "[VideoFFmpegTask] Failed to save PNG-extracted thumbnail:" << cachePath;
                                return false;
                            }
                            m_cachePath = cachePath;
                            qDebug() << "[VideoFFmpegTask] Extracted embedded PNG frame successfully";
                            return true;
                        }
                    }
                }
                av_packet_unref(&pkt);
                ++scanned;
            }
            avformat_close_input(&fmt);
            qWarning() << "[VideoFFmpegTask] Embedded PNG scan failed";
            return false;
        }
        avformat_close_input(&fmt);
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    if (!ctx) { avformat_close_input(&fmt); return false; }
    if (avcodec_parameters_to_context(ctx, vp) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return false;
    }
    if (avcodec_open2(ctx, dec, nullptr) < 0) {
        qWarning() << "[VideoFFmpegTask] avcodec_open2 failed";
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return false;
    }
    // Seek near the start (faster). Use min(1s, 10% of duration) if available.
    if (fmt->duration > 0) {
        int64_t target = std::min<int64_t>(AV_TIME_BASE, fmt->duration / 10);
        int64_t ts = av_rescale_q(target, AV_TIME_BASE_Q, vs->time_base);
        if (av_seek_frame(fmt, vIdx, ts, AVSEEK_FLAG_BACKWARD) >= 0) {
            avcodec_flush_buffers(ctx);
            qDebug() << "[VideoFFmpegTask] Sought to timestamp:" << ts;
        } else {
            qWarning() << "[VideoFFmpegTask] av_seek_frame near start failed; decoding from current position";
        }
    }


    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (!pkt || !frame) {
        av_packet_free(&pkt); av_frame_free(&frame);
        avcodec_free_context(&ctx); avformat_close_input(&fmt);
        return false;
    }

    bool gotFrame = false;
    int packetsRead = 0;
    const int maxPackets = 200; // keep fast

    while (packetsRead < maxPackets) {
        int r = av_read_frame(fmt, pkt);
        if (r == AVERROR_EOF) {
            // Flush decoder
            avcodec_send_packet(ctx, nullptr);
            while (true) {
                r = avcodec_receive_frame(ctx, frame);
                if (r == 0) { gotFrame = true; break; }
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
            }
            break;
        } else if (r < 0) {
            break;
        }
        if (pkt->stream_index == vIdx) {
            if (avcodec_send_packet(ctx, pkt) == 0) {
                while (true) {
                    r = avcodec_receive_frame(ctx, frame);
                    if (r == 0) { gotFrame = true; break; }
                    if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) break;
                    if (r < 0) break;
                }
            }
        }
        av_packet_unref(pkt);
        if (gotFrame) break;
        ++packetsRead;
    }

    QImage outImage;
    if (gotFrame) {
        SwsContext* sws = sws_getContext(
            frame->width, frame->height, (AVPixelFormat)frame->format,
            frame->width, frame->height, AV_PIX_FMT_BGRA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) {
            qWarning() << "[VideoFFmpegTask] sws_getContext failed";
        } else {
            uint8_t* dstData[4]; int dstLinesize[4];
            if (av_image_alloc(dstData, dstLinesize, frame->width, frame->height, AV_PIX_FMT_BGRA, 1) >= 0) {
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);
                // Copy to QImage
                QImage img(frame->width, frame->height, QImage::Format_ARGB32);
                for (int y = 0; y < frame->height; ++y) {
                    memcpy(img.scanLine(y), dstData[0] + y * dstLinesize[0], frame->width * 4);
                }
                outImage = img;
                av_freep(&dstData[0]);
            }
            sws_freeContext(sws);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    if (outImage.isNull()) {
        return false;
    }

    // Scale and save using same policy as QMedia path
    QImage thumb = outImage.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QString cachePath = m_cachePath;
    if (thumb.hasAlphaChannel()) {
        cachePath.replace(QRegularExpression("\\.jpg$", QRegularExpression::CaseInsensitiveOption), ".png");
    }
    const char* fmtName = thumb.hasAlphaChannel() ? "PNG" : "JPEG";
    int quality = thumb.hasAlphaChannel() ? 100 : 85;
    if (!thumb.save(cachePath, fmtName, quality)) {
        qWarning() << "[VideoFFmpegTask] Failed to save thumbnail:" << cachePath;
        return false;
    }

    m_cachePath = cachePath; // in case extension changed
    return true;
#endif
}

void VideoFFmpegTask::run()
{
    qDebug() << "[VideoFFmpegTask] Fallback decoding for" << m_filePath;
    bool success = false;
    success = decodeAndSave();
    const QString filePath = m_filePath;
    const QString cachePath = m_cachePath;
    ThumbnailGenerator* generator = m_generator;

    QMetaObject::invokeMethod(generator, [generator, filePath, cachePath, success]() {
        QMutexLocker locker(&generator->m_mutex);
        generator->m_pendingThumbnails.remove(filePath);
        generator->updateProgress();
        if (success) {
            emit generator->thumbnailGenerated(filePath, cachePath);
        } else {
            emit generator->thumbnailFailed(filePath);
        }
    }, Qt::QueuedConnection);
}

// VideoThumbnailGenerator implementation
VideoThumbnailGenerator::VideoThumbnailGenerator(const QString& filePath, const QString& cachePath, ThumbnailGenerator* generator, int sessionId)
    : QObject(nullptr), m_filePath(filePath), m_cachePath(cachePath), m_generator(generator),
      m_frameReceived(false), m_sessionId(sessionId) {

    m_player = new QMediaPlayer(this);
    m_videoSink = new QVideoSink(this);
    m_player->setVideoSink(m_videoSink);

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(2000);

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &VideoThumbnailGenerator::onMediaStatusChanged);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &VideoThumbnailGenerator::onError);
    connect(m_videoSink, &QVideoSink::videoFrameChanged, this, &VideoThumbnailGenerator::onVideoFrameChanged);
    connect(m_timeout, &QTimer::timeout, this, &VideoThumbnailGenerator::onTimeout);
}

VideoThumbnailGenerator::~VideoThumbnailGenerator() {
    if (m_player) {
        m_player->stop();
        m_player->setSource(QUrl());
    }
    // Remove from active set
    if (m_generator) {
        QMutexLocker locker(&m_generator->m_mutex);
        m_generator->m_activeVideoGenerators.remove(this);
    }
}

void VideoThumbnailGenerator::startFfmpegFallback()
{
#ifdef HAVE_FFMPEG
    // Keep pending set entry; FFmpeg task will clear it on completion
    VideoFFmpegTask* task = new VideoFFmpegTask(m_filePath, m_cachePath, m_generator);
    m_generator->m_threadPool->start(task);
    qDebug() << "[VideoThumbnailGenerator] Scheduled FFmpeg fallback for:" << m_filePath;
#endif
}

void VideoThumbnailGenerator::start() {
    // Fast-cancel if session changed
    if (m_generator->m_sessionId.load() != m_sessionId) { deleteLater(); return; }
    
    // Check codec first to avoid unnecessary QMediaPlayer attempts
    if (MediaInfo::shouldUseFFmpegPlayback(m_filePath)) {
        startFfmpegFallback();
        return;
    }
    
    if (!m_player) { qWarning() << "[VideoThumbnailGenerator] Player null"; return; }
    
    // Track as active for cancellation
    {
        QMutexLocker locker(&m_generator->m_mutex);
        m_generator->m_activeVideoGenerators.insert(this);
    }
    m_player->setSource(QUrl::fromLocalFile(m_filePath));
    m_timeout->start();
}


void VideoThumbnailGenerator::onMediaStatusChanged() {
    if (!m_player) { qWarning() << "[VideoThumbnailGenerator] onMediaStatusChanged: Player null"; return; }

    QMediaPlayer::MediaStatus status = m_player->mediaStatus();

    if (status == QMediaPlayer::LoadedMedia) {
        m_player->setPosition(m_seekTime);
        // Don't start playing - just seek; the frame will arrive when ready
    }
}

void VideoThumbnailGenerator::onVideoFrameChanged() {

    if (!m_videoSink) { qWarning() << "[VideoThumbnailGenerator] onVideoFrameChanged: VideoSink null"; return; }

    if (m_frameReceived) return;
    if (m_generator->m_sessionId.load() != m_sessionId) { deleteLater(); return; }

    QVideoFrame frame = m_videoSink->videoFrame();
    if (!frame.isValid()) return;

    // Map and extract frame image with SEH protection on MSVC
    bool ok = false;
    QImage captured;
    if (frame.map(QVideoFrame::ReadOnly)) {
        captured = frame.toImage();
        frame.unmap();
        ok = true;
    }

    if (ok && !captured.isNull()) {
        m_capturedFrame = captured;
        m_frameReceived = true;
        m_timeout->stop();
        m_player->stop();
        m_player->setSource(QUrl());

        // Handle alpha channel premultiplication for videos with transparency
        // QVideoFrame often provides unpremultiplied alpha, but Qt expects premultiplied for proper display
        if (m_capturedFrame.hasAlphaChannel()) {
            QImage::Format format = m_capturedFrame.format();

            // Convert to premultiplied alpha if needed
            if (format == QImage::Format_ARGB32 || format == QImage::Format_RGBA8888) {
                m_capturedFrame = m_capturedFrame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            } else if (format != QImage::Format_ARGB32_Premultiplied &&
                       format != QImage::Format_RGBA8888_Premultiplied) {
                // For other formats with alpha, convert to premultiplied
                m_capturedFrame = m_capturedFrame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            }
        }


        QImage thumbnail = m_capturedFrame.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);

        // For images with alpha, save as PNG to preserve transparency
        // Otherwise use JPEG for smaller file size
        QString cachePath = m_cachePath;
        if (thumbnail.hasAlphaChannel()) {
            // Replace .jpg extension with .png for alpha videos
            cachePath.replace(QRegularExpression("\\.jpg$", QRegularExpression::CaseInsensitiveOption), ".png");
        }

        QString format = thumbnail.hasAlphaChannel() ? "PNG" : "JPEG";
        int quality = thumbnail.hasAlphaChannel() ? 100 : 85;

        if (thumbnail.save(cachePath, format.toUtf8().constData(), quality)) {
            qDebug() << "[VideoThumbnailGenerator] Saved video thumbnail:" << cachePath;

            QMutexLocker locker(&m_generator->m_mutex);
            m_generator->m_pendingThumbnails.remove(m_filePath);

            // Update progress
            m_generator->updateProgress();

            emit m_generator->thumbnailGenerated(m_filePath, cachePath);
        } else {
            qWarning() << "[VideoThumbnailGenerator] Failed to save video thumbnail:" << m_cachePath;

            QMutexLocker locker(&m_generator->m_mutex);
            m_generator->m_pendingThumbnails.remove(m_filePath);

            // Update progress
            m_generator->updateProgress();

            emit m_generator->thumbnailFailed(m_filePath);
        }

        // Start next queued video (if any)
        QMetaObject::invokeMethod(m_generator, [g = m_generator]() {
            g->startNextVideoIfPossible();
        }, Qt::QueuedConnection);

        deleteLater();
    }
}

void VideoThumbnailGenerator::onTimeout() {
    if (m_generator->m_sessionId.load() != m_sessionId) { deleteLater(); return; }
    qDebug() << "[VideoThumbnailGenerator] Timeout waiting for video frame (video may be corrupted or unsupported):" << m_filePath;
    m_player->stop();
    m_player->setSource(QUrl());
#ifdef HAVE_FFMPEG
    // Try FFmpeg fallback asynchronously
    startFfmpegFallback();
    // Allow another video to start
    QMetaObject::invokeMethod(m_generator, [g = m_generator]() {
        g->startNextVideoIfPossible();
    }, Qt::QueuedConnection);
    deleteLater();
    return;
#endif
    QMutexLocker locker(&m_generator->m_mutex);
    m_generator->m_pendingThumbnails.remove(m_filePath);
    m_generator->updateProgress();
    emit m_generator->thumbnailFailed(m_filePath);
    // Allow another video to start
    QMetaObject::invokeMethod(m_generator, [g = m_generator]() {
        g->startNextVideoIfPossible();
    }, Qt::QueuedConnection);
    deleteLater();
}

void VideoThumbnailGenerator::onError(QMediaPlayer::Error error, const QString &errorString) {
    if (m_generator->m_sessionId.load() != m_sessionId) { deleteLater(); return; }
    qWarning() << "[VideoThumbnailGenerator] Unexpected media player error for" << m_filePath << "- Error:" << error << errorString;
    qWarning() << "[VideoThumbnailGenerator] This file should have been routed to FFmpeg if codec was unsupported";

    m_timeout->stop();
    m_player->stop();
    m_player->setSource(QUrl());
    
    // Report failure - no fallback since codec detection should have caught this
    {
        QMutexLocker locker(&m_generator->m_mutex);
        m_generator->m_pendingThumbnails.remove(m_filePath);
        m_generator->m_activeVideoGenerators.remove(this);
    }
    m_generator->updateProgress();
    m_generator->startNextVideoIfPossible();
    emit m_generator->thumbnailFailed(m_filePath);
    deleteLater();
}


// ThumbnailGenerator implementation
ThumbnailGenerator& ThumbnailGenerator::instance() {
    static ThumbnailGenerator s;
    return s;
}

ThumbnailGenerator::ThumbnailGenerator(QObject* parent)
    : QObject(parent), m_totalThumbnails(0), m_completedThumbnails(0) {
    ensureThumbnailDir();

    // Create thread pool for thumbnail generation
    m_threadPool = new QThreadPool(this);

    // PERFORMANCE: Use optimal thread count for thumbnail generation
    // Use half of available CPU cores to avoid overwhelming the system
    // Minimum 2 threads, maximum 8 threads for best balance
    int idealThreads = QThread::idealThreadCount();
    int optimalThreads = qBound(2, idealThreads / 2, 8);
    m_baseThreadCount = optimalThreads;
    m_currentThreadLimit = optimalThreads;
    m_threadPool->setMaxThreadCount(m_currentThreadLimit);

    qDebug() << "[ThumbnailGenerator] Initialized with" << m_threadPool->maxThreadCount()

             << "threads (ideal:" << idealThreads << ")";
}

void ThumbnailGenerator::ensureThumbnailDir() {
    // Store thumbnails in {appDir}/data/thumbnails/
    QString appDir = QCoreApplication::applicationDirPath();
    QString dataDir = appDir + "/data";
    QString thumbDir = dataDir + "/thumbnails";

    QDir dir;
    if (!dir.exists(dataDir)) {
        dir.mkpath(dataDir);
    }
    if (!dir.exists(thumbDir)) {
        dir.mkpath(thumbDir);
    }

    m_thumbnailDir = QDir(thumbDir);
    qDebug() << "[ThumbnailGenerator] Cache directory:" << m_thumbnailDir.absolutePath();
}

QString ThumbnailGenerator::getFileHash(const QString& filePath) {
    // Use MD5 hash of absolute file path as cache key
    QFileInfo fi(filePath);
    QString absPath = fi.absoluteFilePath();
    QByteArray hash = QCryptographicHash::hash(absPath.toUtf8(), QCryptographicHash::Md5);
    return QString(hash.toHex());
}

QString ThumbnailGenerator::getThumbnailCachePath(const QString& filePath) {
    QString hash = getFileHash(filePath);

    // Check if PNG version exists (for videos with alpha)
    QString pngPath = m_thumbnailDir.absoluteFilePath(hash + ".png");
    if (QFileInfo::exists(pngPath)) {
        return pngPath;
    }

    // Default to JPG
    return m_thumbnailDir.absoluteFilePath(hash + ".jpg");
}

QString ThumbnailGenerator::writeThumbnailImage(const QString& sourcePath, const QImage& image) {
    if (image.isNull()) {
        return QString();
    }

    const QString hash = getFileHash(sourcePath);
    const bool hasAlpha = image.hasAlphaChannel();
    const QString targetExt = hasAlpha ? ".png" : ".jpg";
    const QString staleExt = hasAlpha ? ".jpg" : ".png";

    QString targetPath = m_thumbnailDir.absoluteFilePath(hash + targetExt);
    QString stalePath = m_thumbnailDir.absoluteFilePath(hash + staleExt);
    if (stalePath != targetPath && QFile::exists(stalePath)) {
        QFile::remove(stalePath);
    }

    const char* format = hasAlpha ? "PNG" : "JPEG";
    const int quality = hasAlpha ? 100 : 85;
    if (!image.save(targetPath, format, quality)) {
        qWarning() << "[ThumbnailGenerator] Failed to save thumbnail:" << targetPath;
        return QString();
    }

    return targetPath;
}

bool ThumbnailGenerator::isImageFile(const QString& filePath) {

    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    // Qt natively supported formats (via QImageReader)
    QStringList qtSupportedExts = {
        "jpg", "jpeg", "png", "gif", "bmp", "webp",
        "tiff", "tif", "ico", "pbm", "pgm", "ppm", "pnm",
        "svg", "svgz"
    };

    // Formats that need special handling (not natively supported by Qt)
    QStringList specialFormats = {
        // RAW formats
        "raw", "cr2", "cr3", "nef", "arw", "dng", "orf", "rw2", "pef", "srw", "raf",
        // HDR/EXR formats
        "exr", "hdr", "pic",
        // Adobe formats
        "psd", "psb",
        // Other formats
        "heic", "heif", "avif", "jxl", "tga", "pcx"
    };

    return qtSupportedExts.contains(ext) || specialFormats.contains(ext);
}

bool ThumbnailGenerator::isVideoFile(const QString& filePath) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    // Comprehensive list of video formats
    QStringList videoExts = {
        // Common formats
        "mp4", "mov", "avi", "mkv", "webm", "flv", "wmv", "m4v",
        // MPEG variants
        "mpg", "mpeg", "m2v", "m4p", "m2ts", "mts", "ts",
        // Other formats
        "3gp", "3g2", "ogv", "ogg", "vob", "divx", "xvid",
        "asf", "rm", "rmvb", "f4v", "swf", "mxf", "roq", "nsv"
    };
    return videoExts.contains(ext);
}

bool ThumbnailGenerator::isQtSupportedFormat(const QString& filePath) {
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    // Qt natively supported image formats
    QStringList qtSupported = {
        "jpg", "jpeg", "png", "gif", "bmp", "webp",
        "tiff", "tif", "ico", "pbm", "pgm", "ppm", "pnm",
        "svg", "svgz"
    };

    return qtSupported.contains(ext);
}

bool ThumbnailGenerator::isThumbnailCached(const QString& filePath) {
    QString cachePath = getThumbnailCachePath(filePath);
    QFileInfo cacheInfo(cachePath);

    if (!cacheInfo.exists()) {
        return false;
    }

    // Check if cache is stale (source file is newer than thumbnail)
    QFileInfo sourceInfo(filePath);
    if (sourceInfo.lastModified() > cacheInfo.lastModified()) {
        return false;
    }

    return true;
}

QString ThumbnailGenerator::getThumbnailPath(const QString& filePath) {
    if (filePath.isEmpty()) {
        return QString();
    }

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        return QString();
    }

    // Check if thumbnail already exists in cache
    // Return cached path if it exists
    if (isThumbnailCached(filePath)) {
        return getThumbnailCachePath(filePath);
    }

    // No cached thumbnail - return empty string (caller should use requestThumbnail for async generation)
    return QString();
}

void ThumbnailGenerator::requestThumbnail(const QString& filePath) {
    int session = m_sessionId.load();
    if (filePath.isEmpty()) {
        return;
    }

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        return;
    }

    // Check if already cached
    if (isThumbnailCached(filePath)) {
        QString cachePath = getThumbnailCachePath(filePath);
        // Always deliver on UI event loop to avoid re-entrancy from data()/paint()
        QMetaObject::invokeMethod(this, [this, filePath, cachePath]() {
            emit thumbnailGenerated(filePath, cachePath);
        }, Qt::QueuedConnection);
        return;
    }

    // Check if already being processed
    {
        QMutexLocker locker(&m_mutex);
        if (m_pendingThumbnails.contains(filePath)) {
            return; // Already in progress
        }
        m_pendingThumbnails.insert(filePath);
    }


    bool isVideo = isVideoFile(filePath);
    bool isImage = isImageFile(filePath);

    if (!isVideo && !isImage) {
        qWarning() << "[ThumbnailGenerator] Unsupported file type, creating placeholder:" << filePath;
        QString unsupportedThumb = createUnsupportedThumbnail(filePath);
        {
            QMutexLocker locker(&m_mutex);
            m_pendingThumbnails.remove(filePath);
        }
        updateProgress();
        if (!unsupportedThumb.isEmpty()) {
            QMetaObject::invokeMethod(this, [this, filePath, unsupportedThumb]() {
                emit thumbnailGenerated(filePath, unsupportedThumb);
            }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(this, [this, filePath]() {
                emit thumbnailFailed(filePath);
            }, Qt::QueuedConnection);
        }
        return;
    }

    if (isVideo) {
        QString cachePath = getThumbnailCachePath(filePath);
        // Throttle video generation: queue if too many active
        {
            QMutexLocker locker(&m_mutex);
            if (m_activeVideoGenerators.size() >= m_maxActiveVideos) {
                m_videoQueue.enqueue(qMakePair(filePath, cachePath));
                return;
            }
        }
        // Start immediately if slot available
        VideoThumbnailGenerator* videoGen = new VideoThumbnailGenerator(filePath, cachePath, this, session);
        videoGen->start();
    } else {
        {
            QMutexLocker locker(&m_mutex);
            m_pendingImageTasks++;
            updateThreadPoolLimit();
        }
        ThumbnailTask* task = new ThumbnailTask(filePath, this, session);
        m_threadPool->start(task);
    }
}


void ThumbnailGenerator::updateThreadPoolLimit() {
    if (m_pendingImageTasks < 10) {
        m_currentThreadLimit = m_baseThreadCount;
    } else if (m_pendingImageTasks < 50) {
        m_currentThreadLimit = qMin(m_baseThreadCount + 2, 12);
    } else {
        m_currentThreadLimit = qMin(m_baseThreadCount + 4, 16);
    }
    
    if (m_threadPool->maxThreadCount() != m_currentThreadLimit) {
        m_threadPool->setMaxThreadCount(m_currentThreadLimit);
    }
}


void ThumbnailGenerator::requestThumbnailForce(const QString& filePath) {
    if (filePath.isEmpty()) return;
    QFileInfo fi(filePath);
    if (!fi.exists()) return;
    QString cachePath = getThumbnailCachePath(filePath);
    if (!cachePath.isEmpty()) {
        QFile::remove(cachePath);
    }
    {
        QMutexLocker locker(&m_mutex);
        m_pendingThumbnails.remove(filePath);
    }
    requestThumbnail(filePath);
}


QString ThumbnailGenerator::generateImageThumbnail(const QString& filePath) {
#ifdef _MSC_VER
    __try {
#else
    try {
#endif
        // Validate file exists and is readable
        QFileInfo fileInfo(filePath);

        if (!fileInfo.exists() || !fileInfo.isReadable()) {
            qWarning() << "[ThumbnailGenerator] File not accessible:" << filePath;
            return QString();
        }

        // Check if this format should be handled by OpenImageIO
        if (OIIOImageLoader::isOIIOSupported(filePath)) {
            QImage image = OIIOImageLoader::loadImage(filePath, THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT);
            if (!image.isNull()) {
                QString cachePath = writeThumbnailImage(filePath, image);
                if (cachePath.isEmpty()) {
                    qWarning() << "[ThumbnailGenerator] Failed to save OIIO thumbnail";
                }
                return cachePath;
            } else {
                qWarning() << "[ThumbnailGenerator] OIIO failed to load image:" << filePath;
                // Fall through to placeholder generation
            }
        }

        // Check if this is a Qt-supported format
        if (!isQtSupportedFormat(filePath)) {


            // Create a placeholder thumbnail with format info
            QImage placeholder(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, QImage::Format_RGB32);
            placeholder.fill(QColor(50, 50, 50));

            QPainter painter(&placeholder);
            painter.setRenderHint(QPainter::Antialiasing);

            // Draw file icon
            painter.setPen(QPen(QColor(150, 150, 150), 2));
            painter.setBrush(Qt::NoBrush);
            QRect iconRect(THUMBNAIL_WIDTH/2 - 50, 30, 100, 100);
            painter.drawRoundedRect(iconRect, 8, 8);

            // Draw file extension
            QFileInfo fi(filePath);
            QString ext = fi.suffix().toUpper();
            painter.setFont(QFont("Segoe UI", 24, QFont::Bold));
            painter.setPen(QColor(200, 200, 200));
            painter.drawText(iconRect, Qt::AlignCenter, ext);

            // Draw "Preview Not Available" text
            painter.setFont(QFont("Segoe UI", 10));
            painter.setPen(QColor(180, 180, 180));
            QRect textRect(20, 150, THUMBNAIL_WIDTH - 40, 60);
            painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, "Preview Not Available\n(Format not supported)");

            QString cachePath = writeThumbnailImage(filePath, placeholder);
            if (cachePath.isEmpty()) {
                qWarning() << "[ThumbnailGenerator] Failed to save placeholder thumbnail";
            }
            return cachePath;
        }

        QImageReader reader(filePath);
        reader.setAutoTransform(true);
        reader.setDecideFormatFromContent(true);
        reader.setQuality(50);

        QSize originalSize = reader.size();
        if (!originalSize.isValid()) {
            qWarning() << "[ThumbnailGenerator] Failed to read image size:" << filePath << reader.errorString();
            return QString();
        }

        // Validate size is reasonable
        if (originalSize.width() <= 0 || originalSize.height() <= 0 ||
            originalSize.width() > 50000 || originalSize.height() > 50000) {
            qWarning() << "[ThumbnailGenerator] Invalid image dimensions:" << originalSize << "for" << filePath;
            return QString();
        }

        // CRITICAL: For very large images (4K+), use scaled reading to avoid memory issues
        QSize scaledSize = originalSize.scaled(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, Qt::KeepAspectRatio);
        reader.setScaledSize(scaledSize);

        // For very large images, also set a clip rect to limit memory usage
        if (originalSize.width() > 4000 || originalSize.height() > 4000) {
            reader.setScaledClipRect(QRect(0, 0, scaledSize.width(), scaledSize.height()));
        }

        QImage image = reader.read();
        if (image.isNull()) {
            qWarning() << "[ThumbnailGenerator] Failed to read image:" << filePath << reader.errorString();
            return QString();
        }

        QString cachePath = writeThumbnailImage(filePath, image);
        if (cachePath.isEmpty()) {
            qWarning() << "[ThumbnailGenerator] Failed to save thumbnail for:" << filePath;
        }
        return cachePath;

#ifdef _MSC_VER
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        qCritical() << "[ThumbnailGenerator] SEH exception (access violation) for" << filePath 
                    << "code:" << Qt::hex << GetExceptionCode();
        return QString();
    }
#else
    } catch (const std::exception& e) {
        qCritical() << "[ThumbnailGenerator] Exception generating thumbnail for" << filePath << ":" << e.what();
        return QString();
    } catch (...) {
        qCritical() << "[ThumbnailGenerator] Unknown exception generating thumbnail for" << filePath;
        return QString();
    }
#endif
}


QString ThumbnailGenerator::createSampleImage(const QString& directory) {
    QString baseDir = directory;
    if (baseDir.isEmpty()) {
        baseDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        if (baseDir.isEmpty()) {
            baseDir = QDir::tempPath();
        }
        baseDir += "/kasset_autotest";
    }

    QDir dir(baseDir);
    if (!dir.exists()) {
        if (!QDir().mkpath(baseDir)) {
            qWarning() << "[ThumbnailGenerator] Failed to create sample image directory" << baseDir;
            LogManager::instance().addLog(QString("Failed to create sample image directory %1").arg(baseDir));
            return QString();
        }
    }

    QString fileName = QStringLiteral("autotest_%1.png").arg(QDateTime::currentMSecsSinceEpoch());
    QString filePath = dir.filePath(fileName);

    QImage img(256, 256, QImage::Format_ARGB32);
    img.fill(QColor("#1e1e1e"));

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(QBrush(QColor("#4a90e2")));
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(24, 24, img.width() - 48, img.height() - 48), 24, 24);

    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 28, QFont::Bold));
    painter.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("KAsset\nAutotest"));
    painter.end();

    if (img.save(filePath, "PNG", 95)) {
        qDebug() << "[ThumbnailGenerator] Created sample image at" << filePath;
        if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
            LogManager::instance().addLog(QString("Generated sample image %1").arg(fileName), "DEBUG");
        }
        return filePath;
    }

    qWarning() << "[ThumbnailGenerator] Failed to save sample image at" << filePath;
    if (!qEnvironmentVariableIsEmpty("KASSET_VERBOSE")) {
        LogManager::instance().addLog(QString("Failed to create sample image %1").arg(fileName), "WARN");
    }
    return QString();
}

void ThumbnailGenerator::clearCache() {
    qDebug() << "ThumbnailGenerator: clearing cache...";
    QStringList files = m_thumbnailDir.entryList(QDir::Files);
    int count = 0;
    for (const QString& file : files) {
        if (m_thumbnailDir.remove(file)) {
            count++;
        }
    }
    qDebug() << "ThumbnailGenerator: cleared" << count << "cached thumbnails";
}

void ThumbnailGenerator::startProgress(int total) {
    m_totalThumbnails = total;
    m_completedThumbnails = 0;
    m_lastReportedProgress = 0;
    ProgressManager::instance().start("Generating thumbnails", total);
}

void ThumbnailGenerator::updateProgress() {

    m_completedThumbnails++;

    if (m_totalThumbnails > 0) {
        // Batch progress updates: only emit every 5% or on completion to reduce UI flashing
        const int progressPercent = (m_completedThumbnails * 100) / m_totalThumbnails;
        const int lastPercent = (m_lastReportedProgress * 100) / m_totalThumbnails;
        const bool shouldReport = (progressPercent - lastPercent >= 5) || (m_completedThumbnails >= m_totalThumbnails);
        
        if (shouldReport) {
            ProgressManager::instance().update(m_completedThumbnails);
            emit progressChanged(m_completedThumbnails, m_totalThumbnails);
            m_lastReportedProgress = m_completedThumbnails;
        }
        
        if (m_completedThumbnails >= m_totalThumbnails) {
            finishProgress();
        }
    }
}


void ThumbnailGenerator::finishProgress() {
    ProgressManager::instance().finish();
    qDebug() << "[ThumbnailGenerator] Finished progress tracking";
    m_totalThumbnails = 0;
    m_completedThumbnails = 0;
}

void ThumbnailGenerator::beginNewSession() {
    QMutexLocker locker(&m_mutex);
    m_sessionId.fetch_add(1);
    // Clear pending (new requests will repopulate only for current view)
    m_pendingThumbnails.clear();
    // Stop active video generators immediately
    for (auto *v : std::as_const(m_activeVideoGenerators)) {
        QMetaObject::invokeMethod(v, "deleteLater", Qt::QueuedConnection);
    }
    m_activeVideoGenerators.clear();
    // Clear queued videos as they belong to the previous session
    m_videoQueue.clear();
}

void ThumbnailGenerator::startNextVideoIfPossible() {
    QString nextPath;
    QString nextCache;
    {
        QMutexLocker locker(&m_mutex);
        if (m_activeVideoGenerators.size() >= m_maxActiveVideos) return;
        if (m_videoQueue.isEmpty()) return;
        auto pair = m_videoQueue.dequeue();
        nextPath = pair.first;
        nextCache = pair.second;
    }
    int session = m_sessionId.load();
    VideoThumbnailGenerator* videoGen = new VideoThumbnailGenerator(nextPath, nextCache, this, session);
    videoGen->start();
    qDebug() << "[ThumbnailGenerator] Started queued video thumbnail generation for:" << nextPath;
}


QString ThumbnailGenerator::createUnsupportedThumbnail(const QString& filePath) {
    qDebug() << "[ThumbnailGenerator] Creating unsupported format thumbnail for:" << filePath;

    try {
        // Create a simple image with "Format Not Supported" text
        QImage image(THUMBNAIL_WIDTH, THUMBNAIL_HEIGHT, QImage::Format_RGB32);
        image.fill(QColor(40, 40, 40));

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);

        // Draw icon/symbol
        painter.setPen(QPen(QColor(120, 120, 120), 3));
        painter.setBrush(Qt::NoBrush);
        QRect iconRect(THUMBNAIL_WIDTH/2 - 40, 40, 80, 80);
        painter.drawRect(iconRect);
        painter.drawLine(iconRect.topLeft(), iconRect.bottomRight());
        painter.drawLine(iconRect.topRight(), iconRect.bottomLeft());

        // Draw text
        painter.setPen(QColor(180, 180, 180));
        painter.setFont(QFont("Segoe UI", 12, QFont::Bold));
        QRect textRect(20, 140, THUMBNAIL_WIDTH - 40, 60);
        painter.drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, "Format Not\nSupported");

        // Draw file extension
        QFileInfo fi(filePath);
        QString ext = fi.suffix().toUpper();
        if (!ext.isEmpty()) {
            painter.setFont(QFont("Segoe UI", 10));
            painter.setPen(QColor(140, 140, 140));
            QRect extRect(20, 200, THUMBNAIL_WIDTH - 40, 30);
            painter.drawText(extRect, Qt::AlignCenter, QString(".%1").arg(ext));
        }

        painter.end();

        // Save to cache
        QString cachePath = writeThumbnailImage(filePath, image);
        if (!cachePath.isEmpty()) {
            qDebug() << "[ThumbnailGenerator] Created unsupported thumbnail:" << cachePath;
            return cachePath;
        } else {
            qWarning() << "[ThumbnailGenerator] Failed to save unsupported thumbnail";
            return QString();
        }

    } catch (const std::exception& e) {
        qCritical() << "[ThumbnailGenerator] Exception creating unsupported thumbnail:" << e.what();
        return QString();
    } catch (...) {
        qCritical() << "[ThumbnailGenerator] Unknown exception creating unsupported thumbnail";
        return QString();
    }
}


