#include "live_preview_manager.h"

#include "oiio_image_loader.h"

#include <QtConcurrent/QtConcurrentRun>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QMutexLocker>
#include <QDir>
#include <algorithm>
#include <cmath>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
}
#endif

namespace {

constexpr qreal kDefaultPosterPosition = 0.05; // pick early frame for motion clips
constexpr int kSequenceMetaTtlMs = 30000;

QString ffmpegErrorString(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

bool isImageExtension(const QString& suffix)
{
    static const QSet<QString> kImageExt = {
        "png", "jpg", "jpeg", "bmp", "tga", "tiff", "tif",
        "gif", "webp", "ico", "heic", "heif", "avif", "psd"
    };
    return kImageExt.contains(suffix.toLower());
}

bool isHdrExtension(const QString& suffix)
{
    static const QSet<QString> kHdrExt = { "exr", "hdr", "pfm", "dpx" };
    return kHdrExt.contains(suffix.toLower());
}

bool isSequenceFriendlyExtension(const QString& suffix)
{
    static const QSet<QString> kSequenceExt = {
        "exr", "dpx", "png", "jpg", "jpeg", "tga", "tiff", "tif", "bmp"
    };
    return kSequenceExt.contains(suffix.toLower());
}

bool isVideoExtension(const QString& suffix)
{
    static const QSet<QString> kVideoExt = {
        "mov", "qt", "mp4", "m4v", "mxf", "avi", "mkv", "webm",
        "mpg", "mpeg", "m2v", "m2ts", "mts", "wmv", "asf", "flv",
        "f4v", "ts", "ogv", "y4m", "3gp", "3g2"
    };
    return kVideoExt.contains(suffix.toLower());
}

}

LivePreviewManager& LivePreviewManager::instance()
{
    static LivePreviewManager s_instance;
    return s_instance;
}

LivePreviewManager::LivePreviewManager(QObject* parent)
    : QObject(parent)
{
#ifdef HAVE_FFMPEG
    av_log_set_level(AV_LOG_ERROR);
    static bool s_loggedVersion = false;
    if (!s_loggedVersion) {
        const unsigned version = avcodec_version();
        const unsigned major = AV_VERSION_MAJOR(version);
        const unsigned minor = AV_VERSION_MINOR(version);
        const unsigned micro = AV_VERSION_MICRO(version);
        qInfo() << "[LivePreview] FFmpeg version"
                << QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(micro)
                << QString::fromUtf8(avcodec_configuration());
        s_loggedVersion = true;
    }
#endif
}

LivePreviewManager::FrameHandle LivePreviewManager::cachedFrame(const QString& filePath, const QSize& targetSize, qreal position)
{
    const QString key = makeCacheKey(filePath, targetSize, position);
    QMutexLocker locker(&m_mutex);
    auto it = m_cache.find(key);
    if (it == m_cache.end()) {
        return {};
    }
    it->lastAccess.restart();
    return { it->pixmap, it->position, it->size };
}

void LivePreviewManager::requestFrame(const QString& filePath, const QSize& targetSize, qreal position)
{
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return;
    }
    const QString suffix = info.suffix().toLower();
    if (!isImageExtension(suffix) && !isHdrExtension(suffix) &&
        !isSequenceFriendlyExtension(suffix) && !isVideoExtension(suffix)) {
        return;
    }

    const QString key = makeCacheKey(filePath, targetSize, position);
    {
        QMutexLocker locker(&m_mutex);
        auto cached = m_cache.constFind(key);
        if (cached != m_cache.constEnd()) {
            QPixmap pixmap = cached->pixmap;
            QSize cachedSize = cached->size;
            qreal cachedPos = cached->position;
            locker.unlock();
            emit frameReady(filePath, cachedPos, cachedSize, pixmap);
            return;
        }
        if (m_inFlight.contains(key)) {
            return;
        }
        m_inFlight.insert(key);
    }

    Request request { filePath, targetSize, position };
    enqueueDecode(request, key);
}

void LivePreviewManager::invalidate(const QString& filePath)
{
    QMutexLocker locker(&m_mutex);
    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        if (it.key().startsWith(filePath + "|")) {
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_inFlight.begin(); it != m_inFlight.end(); ) {
        if (it->startsWith(filePath + "|")) {
            it = m_inFlight.erase(it);
        } else {
            ++it;
        }
    }
}

void LivePreviewManager::clear()
{
    QMutexLocker locker(&m_mutex);
    m_cache.clear();
    m_inFlight.clear();
}

int LivePreviewManager::cacheEntryCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_cache.size();
}

QString LivePreviewManager::makeCacheKey(const QString& filePath, const QSize& targetSize, qreal position) const
{
    return QStringLiteral("%1|%2x%3|%4")
        .arg(filePath)
        .arg(targetSize.width())
        .arg(targetSize.height())
        .arg(position, 0, 'f', 3);
}

void LivePreviewManager::enqueueDecode(const Request& request, const QString& cacheKey)
{
    if (isImageSequence(request.filePath)) {
        enqueueSequenceDecode(request, cacheKey);
        return;
    }
    startDecodeTask(request, cacheKey, false);
}

void LivePreviewManager::enqueueSequenceDecode(const Request& request, const QString& cacheKey)
{
    SequenceTask task{request, cacheKey, sequenceHead(request.filePath)};

    QMutexLocker locker(&m_mutex);

    if (m_activeSequenceLoads < m_maxSequenceLoads) {
        ++m_activeSequenceLoads;
        locker.unlock();
        startDecodeTask(task.request, task.cacheKey, true);
        return;
    }

    // Replace any queued request for the same sequence head with the most recent.
    for (auto it = m_sequenceQueue.begin(); it != m_sequenceQueue.end();) {
        if (it->head == task.head) {
            m_inFlight.remove(it->cacheKey);
            it = m_sequenceQueue.erase(it);
        } else {
            ++it;
        }
    }

    if (m_sequenceQueue.size() >= m_sequenceQueueLimit) {
        SequenceTask dropped = m_sequenceQueue.takeFirst();
        m_inFlight.remove(dropped.cacheKey);
    }

    m_sequenceQueue.append(task);
}

void LivePreviewManager::startDecodeTask(const Request& request, const QString& cacheKey, bool fromSequenceQueue)
{
    auto future = QtConcurrent::run([this, request, cacheKey, fromSequenceQueue]() {
        QString error;
        QImage image;

        const bool treatAsSequence = fromSequenceQueue || isImageSequence(request.filePath);
        if (treatAsSequence) {
            image = loadSequenceFrame(request, error);
        } else {
            QFileInfo info(request.filePath);
            const QString suffix = info.suffix().toLower();
            if (isImageExtension(suffix) || isHdrExtension(suffix)) {
                image = loadImageFrame(request, error);
            } else {
                image = loadVideoFrame(request, error);
            }
        }

        QMetaObject::invokeMethod(this, [this, request, cacheKey, image, error, fromSequenceQueue]() {
            SequenceTask nextTask;
            bool launchNext = false;

            {
                QMutexLocker locker(&m_mutex);
                m_inFlight.remove(cacheKey);
                if (fromSequenceQueue) {
                    if (m_activeSequenceLoads > 0) {
                        --m_activeSequenceLoads;
                    }
                    if (!m_sequenceQueue.isEmpty()) {
                        nextTask = m_sequenceQueue.takeLast();
                        ++m_activeSequenceLoads;
                        launchNext = true;
                    }
                }
            }

            if (image.isNull()) {
                emit frameFailed(request.filePath, error.isEmpty() ? QStringLiteral("Unable to decode frame") : error);
            } else {
                QPixmap pixmap = QPixmap::fromImage(image);
                if (pixmap.isNull()) {
                    emit frameFailed(request.filePath, QStringLiteral("Failed to convert image to pixmap"));
                } else {
                    storeFrame(cacheKey, pixmap, request.position, request.targetSize);
                    emit frameReady(request.filePath, request.position, request.targetSize, pixmap);
                }
            }

            if (launchNext) {
                startDecodeTask(nextTask.request, nextTask.cacheKey, true);
            }
        }, Qt::QueuedConnection);
    });
    Q_UNUSED(future);
}

void LivePreviewManager::storeFrame(const QString& key, const QPixmap& pixmap, qreal position, const QSize& size)
{
    QMutexLocker locker(&m_mutex);
    if (m_cache.size() >= m_maxCacheEntries) {
        // Find least recently used entry
        QString lruKey;
        qint64 lruElapsed = -1;
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            const qint64 elapsed = it->lastAccess.elapsed();
            if (elapsed > lruElapsed) {
                lruElapsed = elapsed;
                lruKey = it.key();
            }
        }
        if (!lruKey.isEmpty()) {
            m_cache.remove(lruKey);
        }
    }

    CachedEntry entry;
    entry.pixmap = pixmap;
    entry.position = position;
    entry.size = size;
    entry.lastAccess.start();
    m_cache.insert(key, entry);
}

bool LivePreviewManager::isImageSequence(const QString& filePath)
{
    QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    if (!isSequenceFriendlyExtension(suffix)) {
        return false;
    }
    QString base = info.completeBaseName();
    static QRegularExpression seqPattern(R"(.*(?:\d{2,}|%0\d+d|\#\#\#).*)");
    return seqPattern.match(base).hasMatch();
}

QString LivePreviewManager::sequenceHead(const QString& filePath)
{
    QFileInfo info(filePath);
    QString dir = info.absolutePath();
    QString base = info.completeBaseName();
    QRegularExpression digits(R"((\d+)(?!.*\d))");
    auto match = digits.match(base);
    if (!match.hasMatch()) {
        return info.absoluteFilePath();
    }
    QString head = base.left(match.capturedStart(1));
    return dir + "/" + head;
}

QImage LivePreviewManager::loadImageFrame(const Request& request, QString& error)
{
    QFileInfo info(request.filePath);
    if (!info.exists()) {
        error = QStringLiteral("File does not exist");
        return {};
    }

    const QString suffix = info.suffix().toLower();
    QImage image;

    if (isHdrExtension(suffix) && OIIOImageLoader::isOIIOSupported(request.filePath)) {
#ifdef HAVE_OPENIMAGEIO
        image = OIIOImageLoader::loadImage(request.filePath, request.targetSize.width(), request.targetSize.height());
#else
        image = QImage();
#endif
    } else {
        QImageReader reader(request.filePath);
        reader.setAutoTransform(true);
        if (request.targetSize.isValid()) {
            reader.setScaledSize(request.targetSize);
        }
        image = reader.read();
        if (image.isNull()) {
            error = reader.errorString();
        }
    }

    if (!image.isNull() && request.targetSize.isValid()) {
        image = image.scaled(request.targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image;
}

void LivePreviewManager::pruneSequenceMetaCache()
{
    QList<QString> staleKeys;
    for (auto it = m_sequenceMetaCache.begin(); it != m_sequenceMetaCache.end(); ++it) {
        if (it->lastScan.isValid() && it->lastScan.elapsed() > kSequenceMetaTtlMs) {
            staleKeys.append(it.key());
        }
    }
    for (const QString& key : staleKeys) {
        m_sequenceMetaCache.remove(key);
    }

    while (m_sequenceMetaCache.size() > m_sequenceMetaLimit && !m_sequenceMetaCache.isEmpty()) {
        QString lruKey;
        qint64 lruElapsed = -1;
        for (auto it = m_sequenceMetaCache.begin(); it != m_sequenceMetaCache.end(); ++it) {
            const qint64 elapsed = it->lastScan.isValid() ? it->lastScan.elapsed() : 0;
            if (elapsed > lruElapsed) {
                lruElapsed = elapsed;
                lruKey = it.key();
            }
        }
        if (lruKey.isEmpty()) {
            break;
        }
        m_sequenceMetaCache.remove(lruKey);
    }
}

LivePreviewManager::SequenceMeta LivePreviewManager::sequenceMetaFor(const QString& filePath, QString& error)
{
    const QString head = sequenceHead(filePath);
    {
        QMutexLocker locker(&m_mutex);
        auto it = m_sequenceMetaCache.find(head);
        if (it != m_sequenceMetaCache.end()) {
            if (!it->lastScan.isValid() || it->lastScan.elapsed() < kSequenceMetaTtlMs) {
                if (!it->lastScan.isValid()) {
                    it->lastScan.start();
                } else {
                    it->lastScan.restart();
                }
                return *it;
            }
        }
    }

    SequenceMeta meta;
    meta.head = head;

    QFileInfo info(filePath);
    if (!info.exists()) {
        error = QStringLiteral("Sequence member missing");
        return meta;
    }

    const QString fileName = info.fileName();
    QRegularExpression digits(R"((\d+)(?!.*\d))");
    QRegularExpressionMatch match = digits.match(fileName);
    if (!match.hasMatch()) {
        error = QStringLiteral("Sequence pattern not found");
        return meta;
    }

    meta.directory = info.absolutePath();
    meta.prefix = fileName.left(match.capturedStart(1));
    meta.suffix = fileName.mid(match.capturedEnd(1));
    meta.padding = match.capturedLength();

    QDir dir(meta.directory);
    QStringList entries = dir.entryList(QDir::Files, QDir::Name);
    if (entries.isEmpty()) {
        error = QStringLiteral("Sequence directory empty");
        return meta;
    }

    QRegularExpression sequencePattern(
        QStringLiteral("^%1(\\d+)%2$")
            .arg(QRegularExpression::escape(meta.prefix),
                 QRegularExpression::escape(meta.suffix))
    );

    for (const QString& entry : entries) {
        QRegularExpressionMatch m = sequencePattern.match(entry);
        if (!m.hasMatch()) {
            continue;
        }
        const QString digitsPart = m.captured(1);
        if (meta.padding > 0 && digitsPart.length() < meta.padding) {
            continue;
        }
        meta.frames.append(dir.absoluteFilePath(entry));
    }

    if (meta.frames.isEmpty()) {
        error = QStringLiteral("No sequence frames detected");
        return meta;
    }

    meta.lastScan.start();
    {
        QMutexLocker locker(&m_mutex);
        pruneSequenceMetaCache();
        m_sequenceMetaCache.insert(head, meta);
    }

    return meta;
}

QImage LivePreviewManager::loadSequenceFrame(const Request& request, QString& error)
{
    LivePreviewManager& mgr = LivePreviewManager::instance();
    SequenceMeta meta = mgr.sequenceMetaFor(request.filePath, error);
    if (!meta.isValid()) {
        if (error.isEmpty()) {
            return loadImageFrame(request, error);
        }
        return {};
    }

    const int frameCount = meta.frames.size();
    if (frameCount <= 0) {
        error = QStringLiteral("Sequence has no frames");
        return {};
    }

    qreal normalized = request.position;
    normalized = std::clamp(normalized, 0.0, 1.0);

    int frameIndex = 0;
    if (frameCount > 1) {
        frameIndex = static_cast<int>(std::round(normalized * (frameCount - 1)));
        frameIndex = std::clamp(frameIndex, 0, frameCount - 1);
    }

    Request frameRequest = request;
    frameRequest.filePath = meta.frames.at(frameIndex);

    QString frameError;
    QImage image = loadImageFrame(frameRequest, frameError);
    if (image.isNull()) {
        error = frameError;
    }
    return image;
}

QImage LivePreviewManager::loadVideoFrame(const Request& request, QString& error)
{
#ifndef HAVE_FFMPEG
    Q_UNUSED(request);
    error = QStringLiteral("FFmpeg support not available");
    return {};
#else
    static bool ffmpegInit = false;
    if (!ffmpegInit) {
        av_log_set_level(AV_LOG_ERROR);
        ffmpegInit = true;
    }

    AVFormatContext* fmt = nullptr;
    QByteArray localPath = QFile::encodeName(request.filePath);
    int openResult = avformat_open_input(&fmt, localPath.constData(), nullptr, nullptr);
    if (openResult < 0) {
        error = QStringLiteral("avformat_open_input failed: %1").arg(ffmpegErrorString(openResult));
        return {};
    }

    int infoResult = avformat_find_stream_info(fmt, nullptr);
    if (infoResult < 0) {
        error = QStringLiteral("avformat_find_stream_info failed: %1").arg(ffmpegErrorString(infoResult));
        avformat_close_input(&fmt);
        return {};
    }

    int vIdx = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIdx < 0) {
        error = QStringLiteral("No video stream");
        avformat_close_input(&fmt);
        return {};
    }

    AVStream* stream = fmt->streams[vIdx];
    AVCodecParameters* params = stream->codecpar;
    const AVCodec* decoder = avcodec_find_decoder(params->codec_id);
    if (!decoder) {
        const AVCodecDescriptor* desc = avcodec_descriptor_get(params->codec_id);
        QString codecName = desc ? QString::fromUtf8(desc->name) : QStringLiteral("?");
        error = QStringLiteral("Decoder not found (%1). Rebuild FFmpeg with this codec enabled.").arg(codecName);
        qWarning() << "[LivePreview] decoder missing for" << request.filePath << "codec" << codecName;
        avformat_close_input(&fmt);
        return {};
    }

    AVCodecContext* ctx = avcodec_alloc_context3(decoder);
    if (!ctx) {
        error = QStringLiteral("avcodec_alloc_context3 failed");
        avformat_close_input(&fmt);
        return {};
    }

    int paramsResult = avcodec_parameters_to_context(ctx, params);
    if (paramsResult < 0) {
        error = QStringLiteral("avcodec_parameters_to_context failed: %1").arg(ffmpegErrorString(paramsResult));
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return {};
    }

    int openCodecResult = avcodec_open2(ctx, decoder, nullptr);
    if (openCodecResult < 0) {
        error = QStringLiteral("avcodec_open2 failed: %1").arg(ffmpegErrorString(openCodecResult));
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return {};
    }

    qreal pos = request.position;
    if (pos < 0.0) pos = 0.0;
    if (pos > 1.0) pos = 1.0;
    if (pos == 0.0) pos = kDefaultPosterPosition;

    if (stream->duration > 0) {
        const double duration = stream->duration * av_q2d(stream->time_base);
        const double targetSec = duration * pos;
        int64_t ts = static_cast<int64_t>(targetSec / av_q2d(stream->time_base));
        av_seek_frame(fmt, vIdx, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(ctx);
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    QImage result;

    SwsContext* sws = nullptr;
    bool done = false;
    int safety = 0;

    while (!done && av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index != vIdx) {
            av_packet_unref(packet);
            continue;
        }

        int sendResult = avcodec_send_packet(ctx, packet);
        if (sendResult < 0) {
            qWarning() << "[LivePreview] avcodec_send_packet failed for" << request.filePath << ":" << ffmpegErrorString(sendResult);
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        while (!done) {
            int ret = avcodec_receive_frame(ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                qWarning() << "[LivePreview] avcodec_receive_frame failed for" << request.filePath << ":" << ffmpegErrorString(ret);
                done = true;
                break;
            }

            const int width = frame->width;
            const int height = frame->height;
            if (!sws) {
                sws = sws_getContext(width, height, static_cast<AVPixelFormat>(frame->format),
                                     width, height, AV_PIX_FMT_RGBA,
                                     SWS_BICUBIC, nullptr, nullptr, nullptr);
            }
            if (!sws) {
                error = QStringLiteral("Failed to create sws context");
                done = true;
                break;
            }

            QImage image(width, height, QImage::Format_RGBA8888);
            uint8_t* dstData[4] = { image.bits(), nullptr, nullptr, nullptr };
            int dstLinesize[4] = { static_cast<int>(image.bytesPerLine()), 0, 0, 0 };
            sws_scale(sws, frame->data, frame->linesize, 0, height, dstData, dstLinesize);

            if (request.targetSize.isValid()) {
                image = image.scaled(request.targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            result = image;
            done = true;
            break;
        }

        if (++safety > 256) {
            break;
        }
    }

    if (sws) sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    if (result.isNull() && error.isEmpty()) {
        error = QStringLiteral("No frame decoded");
    }
    return result;
#endif
}
