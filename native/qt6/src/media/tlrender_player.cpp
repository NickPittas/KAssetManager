/**
 * TLRenderPlayer - Professional video playback using tlRender (mrv2's engine)
 *
 * This implementation provides full ACES/OpenColorIO color management
 * with hardware-accelerated OpenGL rendering.
 */

#include "tlrender_player.h"
#include "ffmpeg_mov_player.h"
#include "mpv_player.h"
#include "file_utils.h"
#include <QDebug>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QMap>
#include <QMutexLocker>
#include <QApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QSize>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#include <stdexcept>
#include "video_metadata.h"
#include "platform_session.h"

#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}
#endif

#ifdef HAVE_TLRENDER
#include <tlRender/Timeline/Init.h>
#include <tlRender/Timeline/Player.h>
#include <tlRender/Timeline/Timeline.h>
#include <tlRender/Timeline/TimelineOptions.h>
#include <tlRender/Timeline/System.h>
#include <tlRender/Timeline/ColorOptions.h>
#include <tlRender/Timeline/DisplayOptions.h>
#include <tlRender/Timeline/Video.h>
#include <tlRender/IO/System.h>

#include <tlRender/QtWidget/Init.h>
#include <tlRender/Qt/ContextObject.h>
#include <tlRender/Qt/PlayerObject.h>

#include <ftk/Core/Context.h>
#include <ftk/Core/Image.h>
#include <ftk/Core/Path.h>
#include <ftk/Core/Memory.h>
#include <ftk/Core/LogSystem.h>
#include <ftk/Core/FontSystem.h>

#include <opentimelineio/timeline.h>

#ifdef HAVE_OCIO
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
#endif

#endif // HAVE_TLRENDER

bool TLRenderPlayer::s_initialized = false;
bool TLRenderPlayer::s_fontsInitialized = false;
#ifdef HAVE_TLRENDER
std::shared_ptr<ftk::Context> TLRenderPlayer::s_sharedContext;
QPointer<tl::qt::ContextObject> TLRenderPlayer::s_contextObject;

namespace {

TLRenderPlayer::PlaybackState toTlPlaybackState(media_player::PlaybackState state)
{
    switch (state) {
    case media_player::PlaybackState::Playing:
        return TLRenderPlayer::PlaybackState::Playing;
    case media_player::PlaybackState::Paused:
        return TLRenderPlayer::PlaybackState::Paused;
    case media_player::PlaybackState::Stopped:
    default:
        return TLRenderPlayer::PlaybackState::Stopped;
    }
}

media_player::LoopMode toMpvLoopMode(TLRenderPlayer::LoopMode mode)
{
    switch (mode) {
    case TLRenderPlayer::LoopMode::Once:
        return media_player::LoopMode::Once;
    case TLRenderPlayer::LoopMode::PingPong:
        return media_player::LoopMode::PingPong;
    case TLRenderPlayer::LoopMode::Loop:
    default:
        return media_player::LoopMode::Loop;
    }
}

TLRenderPlayer::MediaInfo toTlMediaInfo(const media_player::MediaInfo& info)
{
    TLRenderPlayer::MediaInfo out;
    out.width = info.width;
    out.height = info.height;
    out.fps = info.fps;
    out.durationMs = info.durationMs;
    out.totalFrames = info.totalFrames;
    out.codec = info.codec;
    out.hasAudio = info.hasAudio;
    out.audioChannels = info.audioChannels;
    out.audioSampleRate = info.audioSampleRate;
    return out;
}

struct YuvToRgbCoefficients
{
    float kr = 0.2126f;
    float kb = 0.0722f;
};

YuvToRgbCoefficients getYuvToRgbCoefficients(ftk::YUVCoefficients coefficients)
{
    switch (coefficients) {
    case ftk::YUVCoefficients::BT2020:
        return { 0.2627f, 0.0593f };
    case ftk::YUVCoefficients::REC709:
    default:
        return { 0.2126f, 0.0722f };
    }
}

inline float clampUnit(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

QSize fittedTargetSize(const QSize& sourceSize, const QSize& targetSize)
{
    if (sourceSize.isEmpty() || targetSize.isEmpty()) {
        return sourceSize;
    }
    return sourceSize.scaled(targetSize, Qt::KeepAspectRatio);
}

template<typename Sample>
QImage planarYuv422ToQImage(const std::shared_ptr<ftk::Image>& image, const QSize& targetSize = QSize())
{
    if (!image || !image->isValid()) {
        return {};
    }

    const auto& info = image->getInfo();
    const int width = info.size.w;
    const int height = info.size.h;
    if (width <= 0 || height <= 0) {
        return {};
    }

    const uint8_t* base = image->getData();
    if (!base) {
        return {};
    }

    const int chromaWidth = (width + 1) / 2;
    const size_t sampleBytes = sizeof(Sample);
    const size_t yStride = ftk::getAlignedByteCount(static_cast<size_t>(width) * sampleBytes, info.layout.alignment);
    const size_t uvStride = ftk::getAlignedByteCount(static_cast<size_t>(chromaWidth) * sampleBytes, info.layout.alignment);
    const size_t yBytes = yStride * static_cast<size_t>(height);
    const size_t uvBytes = uvStride * static_cast<size_t>(height);
    const size_t totalBytes = yBytes + uvBytes * 2;
    if (image->getByteCount() < totalBytes) {
        return {};
    }

    const uint8_t* uBase = base + yBytes;
    const uint8_t* vBase = uBase + uvBytes;
    const auto coefficients = getYuvToRgbCoefficients(info.yuvCoefficients);
    const float kg = 1.0f - coefficients.kr - coefficients.kb;
    if (kg <= 0.0f) {
        return {};
    }

    const float maxValue = static_cast<float>(std::numeric_limits<Sample>::max());
    const float invMaxValue = 1.0f / maxValue;
    const bool legalRange = info.videoLevels == ftk::VideoLevels::LegalRange;
    const float yOffset = legalRange ? (16.0f / 255.0f) : 0.0f;
    const float yScale = legalRange ? (255.0f / 219.0f) : 1.0f;
    const float uvOffset = 0.5f;
    const float uvScale = legalRange ? (255.0f / 224.0f) * 2.0f : 2.0f;
    const float rFactor = 2.0f * (1.0f - coefficients.kr);
    const float bFactor = 2.0f * (1.0f - coefficients.kb);
    const float gUFactor = 2.0f * coefficients.kb * (1.0f - coefficients.kb) / kg;
    const float gVFactor = 2.0f * coefficients.kr * (1.0f - coefficients.kr) / kg;

    const QSize outputSize = fittedTargetSize(QSize(width, height), targetSize);
    const int outputWidth = outputSize.width();
    const int outputHeight = outputSize.height();
    QImage out(outputWidth, outputHeight, QImage::Format_RGB32);
    if (out.isNull()) {
        return {};
    }

    for (int y = 0; y < outputHeight; ++y) {
        const int srcY = std::min(height - 1, (y * height) / outputHeight);
        const auto* yRow = reinterpret_cast<const Sample*>(base + yStride * static_cast<size_t>(srcY));
        const auto* uRow = reinterpret_cast<const Sample*>(uBase + uvStride * static_cast<size_t>(srcY));
        const auto* vRow = reinterpret_cast<const Sample*>(vBase + uvStride * static_cast<size_t>(srcY));
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));

        for (int x = 0; x < outputWidth; ++x) {
            const int srcX = std::min(width - 1, (x * width) / outputWidth);
            const float ySample = clampUnit((static_cast<float>(yRow[srcX]) * invMaxValue - yOffset) * yScale);
            const float uSample = (static_cast<float>(uRow[srcX / 2]) * invMaxValue - uvOffset) * uvScale;
            const float vSample = (static_cast<float>(vRow[srcX / 2]) * invMaxValue - uvOffset) * uvScale;

            const float r = clampUnit(ySample + rFactor * vSample);
            const float g = clampUnit(ySample - gUFactor * uSample - gVFactor * vSample);
            const float b = clampUnit(ySample + bFactor * uSample);

            dst[x] = qRgb(
                static_cast<int>(std::lround(r * 255.0f)),
                static_cast<int>(std::lround(g * 255.0f)),
                static_cast<int>(std::lround(b * 255.0f)));
        }
    }

    return out;
}

template<typename Sample>
QImage planarYuv420ToQImage(const std::shared_ptr<ftk::Image>& image, const QSize& targetSize = QSize())
{
    if (!image || !image->isValid()) {
        return {};
    }

    const auto& info = image->getInfo();
    const int width = info.size.w;
    const int height = info.size.h;
    if (width <= 0 || height <= 0) {
        return {};
    }

    const uint8_t* base = image->getData();
    if (!base) {
        return {};
    }

    const int chromaWidth = (width + 1) / 2;
    const int chromaHeight = (height + 1) / 2;
    const size_t sampleBytes = sizeof(Sample);
    const size_t yStride = ftk::getAlignedByteCount(static_cast<size_t>(width) * sampleBytes, info.layout.alignment);
    const size_t uvStride = ftk::getAlignedByteCount(static_cast<size_t>(chromaWidth) * sampleBytes, info.layout.alignment);
    const size_t yBytes = yStride * static_cast<size_t>(height);
    const size_t uvBytes = uvStride * static_cast<size_t>(chromaHeight);
    const size_t totalBytes = yBytes + uvBytes * 2;
    if (image->getByteCount() < totalBytes) {
        return {};
    }

    const uint8_t* uBase = base + yBytes;
    const uint8_t* vBase = uBase + uvBytes;
    const auto coefficients = getYuvToRgbCoefficients(info.yuvCoefficients);
    const float kg = 1.0f - coefficients.kr - coefficients.kb;
    if (kg <= 0.0f) {
        return {};
    }

    const float maxValue = static_cast<float>(std::numeric_limits<Sample>::max());
    const float invMaxValue = 1.0f / maxValue;
    const bool legalRange = info.videoLevels == ftk::VideoLevels::LegalRange;
    const float yOffset = legalRange ? (16.0f / 255.0f) : 0.0f;
    const float yScale = legalRange ? (255.0f / 219.0f) : 1.0f;
    const float uvOffset = 0.5f;
    const float uvScale = legalRange ? (255.0f / 224.0f) * 2.0f : 2.0f;
    const float rFactor = 2.0f * (1.0f - coefficients.kr);
    const float bFactor = 2.0f * (1.0f - coefficients.kb);
    const float gUFactor = 2.0f * coefficients.kb * (1.0f - coefficients.kb) / kg;
    const float gVFactor = 2.0f * coefficients.kr * (1.0f - coefficients.kr) / kg;

    const QSize outputSize = fittedTargetSize(QSize(width, height), targetSize);
    const int outputWidth = outputSize.width();
    const int outputHeight = outputSize.height();
    QImage out(outputWidth, outputHeight, QImage::Format_RGB32);
    if (out.isNull()) {
        return {};
    }

    for (int y = 0; y < outputHeight; ++y) {
        const int srcY = std::min(height - 1, (y * height) / outputHeight);
        const auto* yRow = reinterpret_cast<const Sample*>(base + yStride * static_cast<size_t>(srcY));
        const auto* uRow = reinterpret_cast<const Sample*>(uBase + uvStride * static_cast<size_t>(srcY / 2));
        const auto* vRow = reinterpret_cast<const Sample*>(vBase + uvStride * static_cast<size_t>(srcY / 2));
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));

        for (int x = 0; x < outputWidth; ++x) {
            const int srcX = std::min(width - 1, (x * width) / outputWidth);
            const float ySample = clampUnit((static_cast<float>(yRow[srcX]) * invMaxValue - yOffset) * yScale);
            const float uSample = (static_cast<float>(uRow[srcX / 2]) * invMaxValue - uvOffset) * uvScale;
            const float vSample = (static_cast<float>(vRow[srcX / 2]) * invMaxValue - uvOffset) * uvScale;

            const float r = clampUnit(ySample + rFactor * vSample);
            const float g = clampUnit(ySample - gUFactor * uSample - gVFactor * vSample);
            const float b = clampUnit(ySample + bFactor * uSample);

            dst[x] = qRgb(
                static_cast<int>(std::lround(r * 255.0f)),
                static_cast<int>(std::lround(g * 255.0f)),
                static_cast<int>(std::lround(b * 255.0f)));
        }
    }

    return out;
}

QImage imageToQImage(const std::shared_ptr<ftk::Image>& image, const QSize& targetSize = QSize())
{
    if (!image || !image->isValid()) {
        return {};
    }

    const auto& info = image->getInfo();
    const int width = info.size.w;
    const int height = info.size.h;
    if (width <= 0 || height <= 0) {
        return {};
    }

    const uchar* data = image->getData();
    if (!data) {
        return {};
    }

    QImage out;
    switch (info.type) {
    case ftk::ImageType::L_U8: {
        const int bytesPerLine = static_cast<int>(ftk::getAlignedByteCount(static_cast<size_t>(width), info.layout.alignment));
        out = QImage(data, width, height, bytesPerLine, QImage::Format_Grayscale8).copy();
        break;
    }
    case ftk::ImageType::RGB_U8: {
        const int bytesPerLine = static_cast<int>(ftk::getAlignedByteCount(static_cast<size_t>(width) * 3, info.layout.alignment));
        out = QImage(data, width, height, bytesPerLine, QImage::Format_RGB888).copy();
        break;
    }
    case ftk::ImageType::RGBA_U8: {
        const int bytesPerLine = static_cast<int>(ftk::getAlignedByteCount(static_cast<size_t>(width) * 4, info.layout.alignment));
        out = QImage(data, width, height, bytesPerLine, QImage::Format_RGBA8888).copy();
        break;
    }
    case ftk::ImageType::YUV_422P_U8:
        out = planarYuv422ToQImage<uint8_t>(image, targetSize);
        break;
    case ftk::ImageType::YUV_422P_U16:
        out = planarYuv422ToQImage<uint16_t>(image, targetSize);
        break;
    case ftk::ImageType::YUV_420P_U8:
        out = planarYuv420ToQImage<uint8_t>(image, targetSize);
        break;
    case ftk::ImageType::YUV_420P_U16:
        out = planarYuv420ToQImage<uint16_t>(image, targetSize);
        break;
    default:
        return {};
    }

    return out;
}

QImage currentVideoFramesToQImage(const std::vector<tl::VideoFrame>& frames, const QSize& targetSize)
{
    for (const auto& frame : frames) {
        for (const auto& layer : frame.layers) {
            QImage image = imageToQImage(layer.image, targetSize);
            if (image.isNull()) {
                continue;
            }
            if (!targetSize.isEmpty() && image.size() != fittedTargetSize(image.size(), targetSize)) {
                image = image.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            return image;
        }
    }
    return {};
}

} // namespace
#endif

// ============================================================================
// Construction / Destruction
// ============================================================================

TLRenderPlayer::TLRenderPlayer(QObject* parent)
    : QObject(parent)
{
    m_mpvPlayer = nullptr;
    m_ffmpegMovPlayer = new FFmpegMovPlayer(this);
    connect(m_ffmpegMovPlayer, &FFmpegMovPlayer::positionChanged, this, [this](qint64 positionMs) {
        m_position = positionMs;
        emit positionChanged(positionMs);
    });
    connect(m_ffmpegMovPlayer, &FFmpegMovPlayer::durationChanged, this, [this](qint64 durationMs) {
        m_duration = durationMs;
        emit durationChanged(durationMs);
    });
    connect(m_ffmpegMovPlayer, &FFmpegMovPlayer::currentFrameChanged, this, [this](qint64 frameNumber) {
        m_currentFrame = frameNumber;
        emit currentFrameChanged(frameNumber);
    });
    connect(m_ffmpegMovPlayer, &FFmpegMovPlayer::mediaInfoReady, this, [this](const media_player::MediaInfo& info) {
        m_mediaInfo = toTlMediaInfo(info);
        m_duration = m_mediaInfo.durationMs;
        m_totalFrames = m_mediaInfo.totalFrames;
        emit mediaInfoReady(m_mediaInfo);
    });
    connect(m_ffmpegMovPlayer, &FFmpegMovPlayer::playbackStateChanged, this, [this](media_player::PlaybackState state) {
        m_playbackState = toTlPlaybackState(state);
        emit playbackStateChanged(m_playbackState);
    });
    connect(m_ffmpegMovPlayer, &FFmpegMovPlayer::error, this, &TLRenderPlayer::error);
    connect(m_ffmpegMovPlayer, &FFmpegMovPlayer::endOfStream, this, &TLRenderPlayer::endOfStream);
    connect(m_ffmpegMovPlayer, &FFmpegMovPlayer::frameUpdated, this, &TLRenderPlayer::videoFramesChanged);

    // Create update timer for position updates
    m_updateTimer = new QTimer(this);
    m_updateTimer->setTimerType(Qt::PreciseTimer);
    m_updateTimer->setInterval(5); // keep tlRender context ticking consistently
    connect(m_updateTimer, &QTimer::timeout, this, &TLRenderPlayer::onUpdateTimer);

}

TLRenderPlayer::~TLRenderPlayer()
{
    unloadMedia();
}

void TLRenderPlayer::ensureTlRenderContext()
{
#ifdef HAVE_TLRENDER
    if (!m_context) {
        setupContext();
    }
#endif
}

void TLRenderPlayer::initialize()
{
    if (s_initialized) {
#ifdef HAVE_TLRENDER
        // Create the context tick object after QApplication exists.
        if (!s_contextObject && qApp && s_sharedContext) {
            s_contextObject = new tl::qt::ContextObject(s_sharedContext, qApp);
        }
        if (!s_fontsInitialized && qApp && s_sharedContext) {
            tl::qtwidget::initFonts(s_sharedContext);
            s_fontsInitialized = true;
        }
#endif
        return;
    }

#ifdef HAVE_TLRENDER
    try {
        // Create shared context first
        s_sharedContext = ftk::Context::create();

        // Initialize tlRender with Qt Widgets integration.
        // This registers Qt metatypes and sets a sane default OpenGL surface format.
        const bool useDefaultWaylandSurfaceFormat = PlatformSession::isWayland();
        tl::qtwidget::init(
            s_sharedContext,
            useDefaultWaylandSurfaceFormat
                ? tl::qt::DefaultSurfaceFormat::None
                : tl::qt::DefaultSurfaceFormat::OpenGL_4_1_CoreProfile);

        // Keep the shared context ticking via a precise Qt timer.
        // This must be created after QApplication exists.
        if (!s_contextObject && qApp) {
            s_contextObject = new tl::qt::ContextObject(s_sharedContext, qApp);
        }
        if (!s_fontsInitialized && qApp) {
            tl::qtwidget::initFonts(s_sharedContext);
            s_fontsInitialized = true;
        }
        
        s_initialized = true;
        qDebug() << "TLRenderPlayer: tlRender initialized successfully";
    } catch (const std::exception& e) {
        qWarning() << "TLRenderPlayer: Failed to initialize tlRender:" << e.what();
    }
#else
    qWarning() << "TLRenderPlayer: tlRender support not compiled in";
#endif
}

bool TLRenderPlayer::isInitialized()
{
    return s_initialized;
}

void TLRenderPlayer::setupContext()
{
#ifdef HAVE_TLRENDER
    if (!s_initialized) {
        initialize();
    }
    
    if (s_sharedContext) {
        m_context = s_sharedContext;
    } else {
        m_context = ftk::Context::create();
    }
    
    // Get the timeline system
    m_system = m_context->getSystem<tl::System>();
#endif
}

// ============================================================================
// Media Loading
// ============================================================================

void TLRenderPlayer::loadMedia(const QString& filePath)
{
    if (QFileInfo(filePath).suffix().compare(QStringLiteral("mov"), Qt::CaseInsensitive) == 0 && m_ffmpegMovPlayer) {
        if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
            m_mpvPlayer->unloadMedia();
        }
        if (m_player) {
            m_player->stop();
            m_player.reset();
        }
        m_timeline.reset();
        m_playerObject.clear();
        m_updateTimer->stop();

        QMutexLocker locker(&m_mutex);
        m_currentPath = filePath;
        m_frameAcquisitionStats = FrameAcquisitionStats();
        m_cachedVideoFrames.clear();
        locker.unlock();

        m_ffmpegMovPlayer->setLoopMode(toMpvLoopMode(m_loopMode));
        m_ffmpegMovPlayer->setVolume(m_volume);
        m_ffmpegMovPlayer->setMuted(m_muted);
        m_ffmpegMovPlayer->setPlaybackRate(m_playbackRate);
        m_ffmpegMovPlayer->loadMedia(filePath);
        return;
    }
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->unloadMedia();
    }

    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->unloadMedia();
    }
#ifdef HAVE_TLRENDER
    ensureTlRenderContext();
    if (!m_context) {
        emit error(tr("Failed to initialize tlRender context"));
        return;
    }

    QMutexLocker locker(&m_mutex);
    
    // Reset any pending deferred playback from previous media
    m_pendingPlay = false;
    m_pendingReverse = false;
    
    // Clear cached frames from previous media to avoid stale data
    m_cachedVideoFrames.clear();
    m_frameAcquisitionStats = FrameAcquisitionStats();
    
    // Stop any current playback
    if (m_player) {
        m_player->stop();
    }
    
    try {
        // Convert path
        std::string pathStr = filePath.toStdString();
        ftk::Path path(pathStr);
        
        qDebug() << "[TLRenderPlayer] loadMedia: input path=" << filePath;
        qDebug() << "[TLRenderPlayer] loadMedia: ftk::Path:"
                 << "isSeq=" << path.isSeq()
                 << "getNum=" << QString::fromStdString(path.getNum())
                 << "getPad=" << path.getPad();
        
        // Create timeline from path (handles video, sequences, and .otio files)
        tl::Options timelineOptions;
#ifdef HAVE_FFMPEG
        ::MediaInfo::VideoMetadata meta;
        QString metaError;
        if (::MediaInfo::probeVideoFile(filePath, meta, &metaError)) {
            const QString codec = meta.videoCodec.toLower();
            if (codec.contains("png")) {
                // PNG-in-MOV can be unstable with multi-threaded FFmpeg decode.
                timelineOptions.ioOptions["FFmpeg/ThreadCount"] = "1";
                timelineOptions.ioOptions["FFmpeg/VideoBufferSize"] = "1";
                qDebug() << "[TLRenderPlayer] Using single-thread FFmpeg decode for PNG codec";
            }
        }
#endif
        m_timeline = tl::Timeline::create(m_context, path, timelineOptions);
        
        if (!m_timeline) {
            emit error(tr("Failed to create timeline from: %1").arg(filePath));
            return;
        }
        
        // Log what tlRender actually detected/expanded
        const auto& tlPath = m_timeline->getPath();
        qDebug() << "[TLRenderPlayer] loadMedia: tlRender expanded path:"
                 << "path=" << QString::fromStdString(tlPath.get())
                 << "isSeq=" << tlPath.isSeq()
                 << "hasFrames=" << tlPath.getFrames().has_value();
        if (tlPath.getFrames().has_value()) {
            const auto& frames = tlPath.getFrames().value();
            qDebug() << "[TLRenderPlayer] loadMedia: frame range=" << frames.min() << "-" << frames.max();
        }
        
        // Create player
        tl::PlayerOptions playerOptions;
        // PlayerCacheOptions uses videoGB/audioGB (float) and readBehind (float seconds)
        playerOptions.cache.videoGB = 4.0f;
        playerOptions.cache.audioGB = 0.5f;
        playerOptions.cache.readBehind = 0.5f;
        
        m_player = tl::Player::create(m_context, m_timeline, playerOptions);
        
        if (!m_player) {
            emit error(tr("Failed to create player for: %1").arg(filePath));
            return;
        }
        
        m_currentPath = filePath;

        // Observe player state via Qt wrapper so the UI can update without polling.
        // Use QSharedPointer so the viewport can share this object.
        m_playerObject.reset(new tl::qt::PlayerObject(m_context, m_player, this));
        connect(
            m_playerObject.data(),
            &tl::qt::PlayerObject::currentVideoChanged,
            this,
            [this](const std::vector<tl::VideoFrame>& frames)
            {
                bool shouldStartPlayback = false;
                bool reversePlayback = false;

                // Check if we have a frame with valid image data (not just empty structure)
                bool hasValidFrame = false;
                for (const auto& frame : frames) {
                    if (!frame.layers.empty() && frame.layers[0].image) {
                        hasValidFrame = true;
                        break;
                    }
                }

                {
                    QMutexLocker locker(&m_mutex);
                    m_cachedVideoFrames = frames;

                    // If play() was called before frames were ready, start now
                    if (m_pendingPlay && hasValidFrame) {
                        shouldStartPlayback = true;
                        reversePlayback = m_pendingReverse;
                        m_pendingPlay = false;
                        m_pendingReverse = false;
                    }
                }

                qDebug() << "[TLRenderPlayer] currentVideoChanged: got" << frames.size() 
                         << "frames, hasValidFrame:" << hasValidFrame
                         << "pendingPlay:" << m_pendingPlay;

                // Start actual playback now that we have frames
                if (shouldStartPlayback && m_player) {
                    qDebug() << "[TLRenderPlayer] First frame ready, starting deferred playback";
                    if (reversePlayback) {
                        m_player->setSpeedMult(std::abs(m_playbackRate.load()));
                        m_player->reverse();
                    } else {
                        m_player->setSpeedMult(m_playbackRate.load());
                        m_player->forward();
                    }
                }

                emit videoFramesChanged();
            });
        connect(
            m_playerObject.data(),
            &tl::qt::PlayerObject::currentTimeChanged,
            this,
            [this](const OTIO_NS::RationalTime& value)
            {
                const auto timeRange = m_player->getTimeRange();
                const double rate = (value.rate() > 0.0) ? value.rate() :
                                    ((timeRange.duration().rate() > 0.0) ? timeRange.duration().rate() : 24.0);
                const double startValue = timeRange.start_time().rescaled_to(rate).value();
                const double durationValue = timeRange.duration().value();
                double localValue = value.value() - startValue;
                if (localValue < 0.0) {
                    localValue = 0.0;
                }
                const qint64 newFrame = static_cast<qint64>(localValue);
                const qint64 newPositionMs = static_cast<qint64>((localValue / rate) * 1000.0);
                
                qDebug() << "[TLRenderPlayer] currentTimeChanged DEBUG:"
                         << "rawValue=" << value.value()
                         << "startValue=" << startValue
                         << "durationValue=" << durationValue
                         << "localValue=" << localValue
                         << "rate=" << rate
                         << "newFrame=" << newFrame
                         << "newPositionMs=" << newPositionMs
                         << "m_duration=" << m_duration.load();

                // Lazy update media info if we haven't got valid duration yet
                // (common for sequences where timeline loads asynchronously)
                if (m_duration.load() <= 0 && timeRange.duration().value() > 0.0) {
                    qDebug() << "[TLRenderPlayer] Late media info update (sequence async load)";
                    updateMediaInfo();
                }

                if (newPositionMs != m_position) {
                    m_position = newPositionMs;
                    qDebug() << "[TLRenderPlayer] currentTimeChanged: frame" << newFrame << "pos" << newPositionMs << "ms";
                    emit positionChanged(newPositionMs);
                }
                if (newFrame != m_currentFrame) {
                    m_currentFrame = newFrame;
                    emit currentFrameChanged(newFrame);
                }
            });
        connect(
            m_playerObject.data(),
            &tl::qt::PlayerObject::playbackChanged,
            this,
            [this](tl::Playback playback)
            {
                qDebug() << "[TLRenderPlayer] playbackChanged SIGNAL received:" << static_cast<int>(playback)
                         << "(0=Stop, 1=Forward, 2=Reverse)";
                PlaybackState newState;
                switch (playback) {
                case tl::Playback::Stop:
                    newState = m_position > 0 ? PlaybackState::Paused : PlaybackState::Stopped;
                    break;
                case tl::Playback::Forward:
                case tl::Playback::Reverse:
                    newState = PlaybackState::Playing;
                    break;
                default:
                    newState = PlaybackState::Stopped;
                    break;
                }
                if (newState != m_playbackState) {
                    m_playbackState = newState;
                    emit playbackStateChanged(newState);
                }
            });
        
        // Apply current OCIO settings
        applyOCIOOptions();
        
        // Set loop mode
        switch (m_loopMode) {
            case LoopMode::Once:
                m_player->setLoop(tl::Loop::Once);
                break;
            case LoopMode::Loop:
                m_player->setLoop(tl::Loop::Loop);
                break;
            case LoopMode::PingPong:
                m_player->setLoop(tl::Loop::PingPong);
                break;
        }
        
        // Update media info
        locker.unlock();
        updateMediaInfo();

        // Keep a precise tick running to avoid render stalls on some systems.
        m_updateTimer->start();
        
        qDebug() << "TLRenderPlayer: Loaded media:" << filePath;
        
    } catch (const std::exception& e) {
        emit error(tr("Failed to load media: %1 - %2").arg(filePath, e.what()));
    }
#else
    Q_UNUSED(filePath);
    emit error(tr("tlRender support not available"));
#endif
}

void TLRenderPlayer::unloadMedia()
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->unloadMedia();
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->unloadMedia();
    }
#ifdef HAVE_TLRENDER
    QMutexLocker locker(&m_mutex);
    
    m_updateTimer->stop();

    // Clear the shared pointer (will delete when no longer referenced by viewport)
    m_playerObject.clear();
    
    if (m_player) {
        m_player->stop();
        m_player.reset();
    }
    
    if (m_timeline) {
        m_timeline.reset();
    }
    
    m_currentPath.clear();
    m_playbackState = PlaybackState::Stopped;
    m_position = 0;
    m_duration = 0;
    m_currentFrame = 0;
    m_totalFrames = 0;
    m_mediaInfo = MediaInfo();

    m_cachedVideoFrames.clear();
    m_frameAcquisitionStats = FrameAcquisitionStats();
#endif
}

bool TLRenderPlayer::hasMedia() const
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        return true;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        return true;
    }
#ifdef HAVE_TLRENDER
    return m_player != nullptr;
#else
    return false;
#endif
}

QString TLRenderPlayer::currentMediaPath() const
{
    return m_currentPath;
}

void TLRenderPlayer::updateMediaInfo()
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_mediaInfo = toTlMediaInfo(m_ffmpegMovPlayer->mediaInfo());
        m_duration = m_mediaInfo.durationMs;
        m_totalFrames = m_mediaInfo.totalFrames;
        emit durationChanged(m_mediaInfo.durationMs);
        emit mediaInfoReady(m_mediaInfo);
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mediaInfo = toTlMediaInfo(m_mpvPlayer->mediaInfo());
        m_duration = m_mediaInfo.durationMs;
        m_totalFrames = m_mediaInfo.totalFrames;
        emit durationChanged(m_mediaInfo.durationMs);
        emit mediaInfoReady(m_mediaInfo);
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player || !m_timeline) {
        qDebug() << "[TLRenderPlayer] updateMediaInfo: no player or timeline!";
        return;
    }
    
    const auto& ioInfo = m_player->getIOInfo();
    const auto& timeRange = m_player->getTimeRange();
    
    qDebug() << "[TLRenderPlayer] updateMediaInfo:"
             << "timeRange.start=" << timeRange.start_time().value()
             << "timeRange.duration=" << timeRange.duration().value()
             << "timeRange.rate=" << timeRange.duration().rate();
    
    m_mediaInfo.width = 0;
    m_mediaInfo.height = 0;

    double rate = timeRange.duration().rate();
    if (rate <= 0.0) {
        // tlRender can report 0/invalid rates for some stills/sequences depending on source.
        // Keep the app stable and allow seeking/scrubbing.
        rate = 24.0;
    }
    m_mediaInfo.fps = rate;
    m_mediaInfo.totalFrames = static_cast<qint64>(timeRange.duration().value());
    m_mediaInfo.durationMs = static_cast<qint64>((timeRange.duration().value() / rate) * 1000.0);
    
    qDebug() << "[TLRenderPlayer] updateMediaInfo computed:"
             << "fps=" << m_mediaInfo.fps
             << "totalFrames=" << m_mediaInfo.totalFrames
             << "durationMs=" << m_mediaInfo.durationMs;
    
    // Get video info from first video layer
    if (!ioInfo.video.empty()) {
        const auto& videoInfo = ioInfo.video[0];
        m_mediaInfo.width = videoInfo.size.w;
        m_mediaInfo.height = videoInfo.size.h;
    }
    
    // Get audio info - IOInfo.audio is the AudioInfo struct
    m_mediaInfo.hasAudio = ioInfo.audio.isValid();
    if (m_mediaInfo.hasAudio) {
        m_mediaInfo.audioChannels = ioInfo.audio.channelCount;
        m_mediaInfo.audioSampleRate = ioInfo.audio.sampleRate;
    }
    
    m_duration = m_mediaInfo.durationMs;
    m_totalFrames = m_mediaInfo.totalFrames;
    
    emit durationChanged(m_mediaInfo.durationMs);
    emit mediaInfoReady(m_mediaInfo);
#endif
}

// ============================================================================
// Playback Control
// ============================================================================

void TLRenderPlayer::play()
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->play();
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->play();
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) {
        qDebug() << "[TLRenderPlayer::play] No player - cannot play";
        return;
    }

    qDebug() << "[TLRenderPlayer::play] CALLED - rate:" << m_playbackRate
             << "currentPlayback:" << static_cast<int>(m_player->getPlayback());

    // Check if we have frames with valid image data cached yet. If not, defer
    // playback until currentVideoChanged delivers the first valid frame. This
    // prevents the "grey screen" issue where the timeline advances before frames
    // are decoded and ready for display.
    {
        QMutexLocker locker(&m_mutex);
        bool hasValidFrame = false;
        for (const auto& frame : m_cachedVideoFrames) {
            if (!frame.layers.empty() && frame.layers[0].image) {
                hasValidFrame = true;
                break;
            }
        }
        
        if (!hasValidFrame) {
            qDebug() << "[TLRenderPlayer::play] No valid frames cached yet, deferring playback";
            m_pendingPlay = true;
            m_pendingReverse = (m_playbackRate < 0.0);
            m_playbackState = PlaybackState::Playing;  // UI shows "playing" immediately
            locker.unlock();
            emit playbackStateChanged(PlaybackState::Playing);
            return;
        }
    }

    // Honor current playback rate sign (JKL expects reverse playback).
    if (m_playbackRate < 0.0) {
        m_player->setSpeedMult(std::abs(m_playbackRate));
        qDebug() << "[TLRenderPlayer::play] Calling m_player->reverse()";
        m_player->reverse();
        qDebug() << "[TLRenderPlayer::play] After reverse(), playback:" << static_cast<int>(m_player->getPlayback());
    } else {
        m_player->setSpeedMult(m_playbackRate);
        qDebug() << "[TLRenderPlayer::play] Calling m_player->forward()";
        m_player->forward();
        qDebug() << "[TLRenderPlayer::play] After forward(), playback:" << static_cast<int>(m_player->getPlayback());
    }
    m_playbackState = PlaybackState::Playing;
    emit playbackStateChanged(PlaybackState::Playing);
    qDebug() << "[TLRenderPlayer::play] COMPLETED - emitted PlaybackState::Playing";
#endif
}

void TLRenderPlayer::pause()
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->pause();
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->pause();
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) {
        qDebug() << "[TLRenderPlayer::pause] No player - cannot pause";
        return;
    }
    
    qDebug() << "[TLRenderPlayer::pause] Pausing playback";
    
    // Cancel any pending deferred playback
    {
        QMutexLocker locker(&m_mutex);
        m_pendingPlay = false;
        m_pendingReverse = false;
    }
    
    m_player->stop();
    m_playbackState = PlaybackState::Paused;
    emit playbackStateChanged(PlaybackState::Paused);
#endif
}

void TLRenderPlayer::stop()
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->stop();
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->stop();
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) return;
    
    // Cancel any pending deferred playback
    {
        QMutexLocker locker(&m_mutex);
        m_pendingPlay = false;
        m_pendingReverse = false;
    }
    
    m_player->stop();
    m_player->gotoStart();
    m_playbackState = PlaybackState::Stopped;
    emit playbackStateChanged(PlaybackState::Stopped);
#endif
}

void TLRenderPlayer::togglePlayback()
{
#ifdef HAVE_TLRENDER
    if (!m_player) return;
    
    if (m_playbackState == PlaybackState::Playing) {
        pause();
    } else {
        play();
    }
#endif
}

void TLRenderPlayer::seek(qint64 positionMs)
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->seek(positionMs);
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->seek(positionMs);
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) return;
    
    const auto& timeRange = m_player->getTimeRange();
    double rate = timeRange.duration().rate();
    if (rate <= 0.0) {
        rate = 24.0;
    }
    double seconds = positionMs / 1000.0;
    double frameValue = seconds * rate;
    const double startValue = timeRange.start_time().rescaled_to(rate).value();
    
    OTIO_NS::RationalTime seekTime(frameValue + startValue, rate);
    m_player->seek(seekTime);

    // Make scrubbing responsive even when paused: update systems immediately.
    tick();
#endif
}

void TLRenderPlayer::seekToFrame(qint64 frameNumber)
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->seekToFrame(frameNumber);
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->seekToFrame(frameNumber);
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) return;
    
    const auto& timeRange = m_player->getTimeRange();
    double rate = timeRange.duration().rate();
    if (rate <= 0.0) {
        rate = 24.0;
    }
    
    const double startValue = timeRange.start_time().rescaled_to(rate).value();
    OTIO_NS::RationalTime seekTime(static_cast<double>(frameNumber) + startValue, rate);
    m_player->seek(seekTime);

    // Make scrubbing responsive even when paused: update systems immediately.
    tick();
#endif
}

void TLRenderPlayer::setPlaybackRate(double rate)
{
    m_playbackRate = rate;
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->setPlaybackRate(rate);
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->setPlaybackRate(rate);
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) return;

    
    m_player->setSpeedMult(std::abs(rate));
    if (m_playbackState == PlaybackState::Playing) {
        if (rate < 0) {
            m_player->reverse();
        } else {
            m_player->forward();
        }
    }
#endif
}

double TLRenderPlayer::playbackRate() const
{
    return m_playbackRate;
}

void TLRenderPlayer::setLoopMode(LoopMode mode)
{
    m_loopMode = mode;
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->setLoopMode(toMpvLoopMode(mode));
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->setLoopMode(toMpvLoopMode(mode));
        return;
    }
#ifdef HAVE_TLRENDER
    if (m_player) {
        switch (mode) {
            case LoopMode::Once:
                m_player->setLoop(tl::Loop::Once);
                break;
            case LoopMode::Loop:
                m_player->setLoop(tl::Loop::Loop);
                break;
            case LoopMode::PingPong:
                m_player->setLoop(tl::Loop::PingPong);
                break;
        }
    }
#endif
}

TLRenderPlayer::LoopMode TLRenderPlayer::loopMode() const
{
    return m_loopMode;
}

void TLRenderPlayer::stepForward()
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->stepForward();
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->stepForward();
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) return;
    m_player->frameNext();
#endif
}

void TLRenderPlayer::stepBackward()
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->stepBackward();
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->stepBackward();
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) return;
    m_player->framePrev();
#endif
}

void TLRenderPlayer::stepForwardBy(int frames)
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        for (int i = 0; i < frames; ++i) {
            m_ffmpegMovPlayer->stepForward();
        }
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) return;
    for (int i = 0; i < frames; ++i) {
        m_player->frameNext();
    }
#endif
}

void TLRenderPlayer::stepBackwardBy(int frames)
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        for (int i = 0; i < frames; ++i) {
            m_ffmpegMovPlayer->stepBackward();
        }
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) return;
    for (int i = 0; i < frames; ++i) {
        m_player->framePrev();
    }
#endif
}

void TLRenderPlayer::gotoStart()
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->seek(0);
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) return;
    m_player->gotoStart();
#endif
}

void TLRenderPlayer::gotoEnd()
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->seek(m_ffmpegMovPlayer->duration());
        return;
    }
#ifdef HAVE_TLRENDER
    if (!m_player) return;
    m_player->gotoEnd();
#endif
}

// ============================================================================
// Audio Control
// ============================================================================

void TLRenderPlayer::setVolume(float volume)
{
    m_volume = qBound(0.0f, volume, 1.0f);
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->setVolume(m_volume);
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->setVolume(m_volume);
        return;
    }
#ifdef HAVE_TLRENDER
    if (m_player) {
        m_player->setVolume(m_volume);
    }
#endif
}

float TLRenderPlayer::volume() const
{
    return m_volume;
}

void TLRenderPlayer::setMuted(bool muted)
{
    m_muted = muted;
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        m_ffmpegMovPlayer->setMuted(muted);
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        m_mpvPlayer->setMuted(muted);
        return;
    }
#ifdef HAVE_TLRENDER
    if (m_player) {
        m_player->setMute(muted);
    }
#endif
}

bool TLRenderPlayer::isMuted() const
{
    return m_muted;
}

// ============================================================================
// State Queries
// ============================================================================

TLRenderPlayer::PlaybackState TLRenderPlayer::playbackState() const
{
    return m_playbackState;
}

qint64 TLRenderPlayer::position() const
{
    return m_position;
}

qint64 TLRenderPlayer::duration() const
{
    return m_duration;
}

qint64 TLRenderPlayer::currentFrame() const
{
    return m_currentFrame;
}

qint64 TLRenderPlayer::totalFrames() const
{
    return m_totalFrames;
}

TLRenderPlayer::MediaInfo TLRenderPlayer::mediaInfo() const
{
    return m_mediaInfo;
}

MpvPlayer* TLRenderPlayer::mpvPlayer() const
{
    return m_mpvPlayer;
}

FFmpegMovPlayer* TLRenderPlayer::ffmpegMovPlayer() const
{
    return m_ffmpegMovPlayer;
}

// ============================================================================
// OCIO Color Management
// ============================================================================

void TLRenderPlayer::setOCIOEnabled(bool enabled)
{
    m_ocioEnabled = enabled;
    applyOCIOOptions();
    emit ocioOptionsChanged();
}

bool TLRenderPlayer::isOCIOEnabled() const
{
    return m_ocioEnabled;
}

void TLRenderPlayer::setOCIOConfig(const QString& configPath)
{
    m_ocioConfigPath = configPath;
    updateOCIOLists();
    applyOCIOOptions();
    emit ocioConfigChanged(configPath);
    emit ocioOptionsChanged();
}

QString TLRenderPlayer::ocioConfigPath() const
{
    return m_ocioConfigPath;
}

void TLRenderPlayer::setInputColorspace(const QString& colorspace)
{
    m_inputColorspace = colorspace;
    applyOCIOOptions();
    emit ocioOptionsChanged();
}

QString TLRenderPlayer::inputColorspace() const
{
    return m_inputColorspace;
}

void TLRenderPlayer::setDisplay(const QString& display)
{
    m_display = display;
    applyOCIOOptions();
    emit ocioOptionsChanged();
    
    // Update available views for this display
    emit viewsChanged(availableViews(display));
}

QString TLRenderPlayer::display() const
{
    return m_display;
}

void TLRenderPlayer::setView(const QString& view)
{
    m_view = view;
    applyOCIOOptions();
    emit ocioOptionsChanged();
}

QString TLRenderPlayer::view() const
{
    return m_view;
}

void TLRenderPlayer::setLook(const QString& look)
{
    m_look = look;
    applyOCIOOptions();
    emit ocioOptionsChanged();
}

QString TLRenderPlayer::look() const
{
    return m_look;
}

QStringList TLRenderPlayer::availableColorspaces() const
{
    return m_availableColorspaces;
}

QStringList TLRenderPlayer::availableDisplays() const
{
    return m_availableDisplays;
}

QStringList TLRenderPlayer::availableViews(const QString& display) const
{
    if (m_availableViews.contains(display)) {
        return m_availableViews[display];
    }
    
#ifdef HAVE_OCIO
    try {
        OCIO::ConstConfigRcPtr config;
        if (m_ocioConfigPath.isEmpty()) {
            config = OCIO::GetCurrentConfig();
        } else {
            config = OCIO::Config::CreateFromFile(m_ocioConfigPath.toStdString().c_str());
        }
        
        if (config) {
            QStringList views;
            int numViews = config->getNumViews(display.toStdString().c_str());
            for (int i = 0; i < numViews; ++i) {
                views.append(QString::fromStdString(
                    config->getView(display.toStdString().c_str(), i)));
            }
            m_availableViews[display] = views;
            return views;
        }
    } catch (const OCIO::Exception& e) {
        qWarning() << "OCIO: Failed to get views for display" << display << ":" << e.what();
    }
#endif
    
    return QStringList();
}

QStringList TLRenderPlayer::availableLooks() const
{
    return m_availableLooks;
}

void TLRenderPlayer::updateOCIOLists()
{
#ifdef HAVE_OCIO
    try {
        OCIO::ConstConfigRcPtr config;
        if (m_ocioConfigPath.isEmpty()) {
            config = OCIO::GetCurrentConfig();
        } else {
            config = OCIO::Config::CreateFromFile(m_ocioConfigPath.toStdString().c_str());
        }
        
        if (!config) {
            qWarning() << "OCIO: No config available";
            return;
        }
        
        // Get colorspaces
        m_availableColorspaces.clear();
        for (int i = 0; i < config->getNumColorSpaces(); ++i) {
            m_availableColorspaces.append(QString::fromStdString(config->getColorSpaceNameByIndex(i)));
        }
        if (m_inputColorspace.isEmpty() || !m_availableColorspaces.contains(m_inputColorspace)) {
            const char* role = config->getRoleColorSpace(OCIO::ROLE_SCENE_LINEAR);
            if (role && *role) {
                m_inputColorspace = QString::fromStdString(role);
            } else if (!m_availableColorspaces.isEmpty()) {
                m_inputColorspace = m_availableColorspaces.first();
            }
        }
        emit colorspacesChanged(m_availableColorspaces);
        
        // Get displays
        m_availableDisplays.clear();
        for (int i = 0; i < config->getNumDisplays(); ++i) {
            m_availableDisplays.append(QString::fromStdString(config->getDisplay(i)));
        }
        emit displaysChanged(m_availableDisplays);
        
        // Get looks
        m_availableLooks.clear();
        for (int i = 0; i < config->getNumLooks(); ++i) {
            m_availableLooks.append(QString::fromStdString(config->getLookNameByIndex(i)));
        }
        
        // Clear cached views
        m_availableViews.clear();
        
        // Set defaults if not set
        if (!m_availableDisplays.isEmpty()) {
            if (m_display.isEmpty() || !m_availableDisplays.contains(m_display)) {
                m_display = QString::fromStdString(config->getDefaultDisplay());
            }
        }
        if (!m_display.isEmpty()) {
            const QStringList views = availableViews(m_display);
            if (m_view.isEmpty() || !views.contains(m_view)) {
                m_view = QString::fromStdString(config->getDefaultView(m_display.toStdString().c_str()));
            }
        }

        // Make sure view dropdowns can populate immediately after config load.
        if (!m_display.isEmpty()) {
            emit viewsChanged(availableViews(m_display));
        }
        
        qDebug() << "OCIO: Loaded config with" << m_availableColorspaces.size() << "colorspaces,"
                 << m_availableDisplays.size() << "displays";
                 
    } catch (const OCIO::Exception& e) {
        qWarning() << "OCIO: Failed to load config:" << e.what();
    }
#endif
}

void TLRenderPlayer::applyOCIOOptions()
{
#ifdef HAVE_TLRENDER
    // OCIO options will be used during rendering in TLRenderWidget
    // The player itself doesn't directly apply OCIO - that happens in the GL renderer
#endif
}

tl::OCIOOptions TLRenderPlayer::currentOCIOOptions() const
{
    tl::OCIOOptions options;
#ifdef HAVE_TLRENDER
    options.enabled = m_ocioEnabled;
    
    if (!m_ocioConfigPath.isEmpty()) {
        options.config = tl::OCIOConfig::File;
        options.fileName = m_ocioConfigPath.toStdString();
    } else {
        options.config = tl::OCIOConfig::EnvVar;
    }
    
    options.input = m_inputColorspace.toStdString();
    options.display = m_display.toStdString();
    options.view = m_view.toStdString();
    options.look = m_look.toStdString();
#endif
    return options;
}

tl::DisplayOptions TLRenderPlayer::currentDisplayOptions() const
{
    tl::DisplayOptions options;
#ifdef HAVE_TLRENDER
    // EXR Display settings (exposure)
    if (m_exposure != 0.0f) {
        options.exposure.enabled = true;
        options.exposure.exposure = m_exposure;
    }
    
    // Levels settings (gamma)
    if (m_gamma != 1.0f) {
        options.levels.enabled = true;
        options.levels.gamma = m_gamma;
    }
#endif
    return options;
}

void TLRenderPlayer::setExposure(float exposure)
{
    m_exposure = qBound(-10.0f, exposure, 10.0f);
    emit videoFramesChanged(); // Trigger redraw
}

float TLRenderPlayer::exposure() const
{
    return m_exposure;
}

void TLRenderPlayer::setGamma(float gamma)
{
    m_gamma = qBound(0.1f, gamma, 4.0f);
    emit videoFramesChanged(); // Trigger redraw
}

float TLRenderPlayer::gamma() const
{
    return m_gamma;
}

// ============================================================================
// Rendering Interface
// ============================================================================

std::shared_ptr<ftk::Context> TLRenderPlayer::sharedContext()
{
#ifdef HAVE_TLRENDER
    return s_sharedContext;
#else
    return nullptr;
#endif
}

tl::qt::ContextObject* TLRenderPlayer::sharedContextObject()
{
#ifdef HAVE_TLRENDER
    return s_contextObject;
#else
    return nullptr;
#endif
}

std::shared_ptr<ftk::Context> TLRenderPlayer::context() const
{
    return m_context;
}

std::shared_ptr<tl::Player> TLRenderPlayer::player() const
{
#ifdef HAVE_TLRENDER
    return m_player;
#else
    return nullptr;
#endif
}

QSharedPointer<tl::qt::PlayerObject> TLRenderPlayer::playerObject() const
{
#ifdef HAVE_TLRENDER
    return m_playerObject;
#else
    return QSharedPointer<tl::qt::PlayerObject>();
#endif
}

std::vector<tl::VideoFrame> TLRenderPlayer::currentVideoFrames() const
{
#ifdef HAVE_TLRENDER
    QMutexLocker locker(&m_mutex);
    return m_cachedVideoFrames;
#endif
    return {};
}

void TLRenderPlayer::tick()
{
#ifdef HAVE_TLRENDER
    if (m_context) {
        m_context->tick();
    }
#endif
}

void TLRenderPlayer::refreshCurrentFrame()
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        emit videoFramesChanged();
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        emit videoFramesChanged();
        return;
    }
#ifdef HAVE_TLRENDER
    tick();
    emit videoFramesChanged();
#endif
}

// ============================================================================
// Update Timer
// ============================================================================

void TLRenderPlayer::onUpdateTimer()
{
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        return;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        return;
    }
#ifdef HAVE_TLRENDER
    tick();
#endif
}

// ============================================================================
// Frame Extraction (Static)
// ============================================================================

QImage TLRenderPlayer::getCurrentFrame(const QSize& targetSize)
{
    QElapsedTimer timer;
    timer.start();
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        QImage frame = m_ffmpegMovPlayer->currentFrameImage();
        if (!frame.isNull() && !targetSize.isEmpty()) {
            frame = frame.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return frame;
    }
    if (m_mpvPlayer && m_mpvPlayer->hasMedia()) {
        QImage frame = m_mpvPlayer->currentFrameImage();
        if (!frame.isNull() && !targetSize.isEmpty()) {
            frame = frame.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return frame;
    }
#ifdef HAVE_TLRENDER
    {
        QMutexLocker locker(&m_mutex);
        QImage cachedFrame = currentVideoFramesToQImage(m_cachedVideoFrames, targetSize);
        if (!cachedFrame.isNull()) {
            ++m_frameAcquisitionStats.cachedFrameConversions;
            if (!m_cachedVideoFrames.empty() && !m_cachedVideoFrames.front().layers.empty() && m_cachedVideoFrames.front().layers.front().image) {
                m_frameAcquisitionStats.lastCachedImageType = static_cast<int>(m_cachedVideoFrames.front().layers.front().image->getInfo().type);
            }
            m_frameAcquisitionStats.lastCachedConversionNs = timer.nsecsElapsed();
            m_frameAcquisitionStats.lastGetCurrentFrameNs = m_frameAcquisitionStats.lastCachedConversionNs;
            return cachedFrame;
        }

        if (!m_cachedVideoFrames.empty()) {
            for (const auto& frame : m_cachedVideoFrames) {
                for (const auto& layer : frame.layers) {
                    if (layer.image) {
                        ++m_frameAcquisitionStats.unsupportedCachedFrameTypes;
                        m_frameAcquisitionStats.lastCachedImageType = static_cast<int>(layer.image->getInfo().type);
                        break;
                    }
                }
            }
        }
    }
#endif

    // For now, reuse the static thumbnail extraction when possible.
    // This remains the fallback when tlRender has no cached frame yet or the
    // cached frame format is not handled above.
    if (m_currentPath.isEmpty()) {
#ifdef HAVE_TLRENDER
        QMutexLocker locker(&m_mutex);
        m_frameAcquisitionStats.lastGetCurrentFrameNs = timer.nsecsElapsed();
#endif
        return QImage();
    }
    const QImage fallback = extractThumbnail(m_currentPath, targetSize, position());
#ifdef HAVE_TLRENDER
    {
        QMutexLocker locker(&m_mutex);
        ++m_frameAcquisitionStats.fallbackExtractions;
        m_frameAcquisitionStats.lastFallbackExtractionNs = timer.nsecsElapsed();
        m_frameAcquisitionStats.lastGetCurrentFrameNs = m_frameAcquisitionStats.lastFallbackExtractionNs;
    }
#endif
    return fallback;
}

bool TLRenderPlayer::useMpvBackendForCurrentMedia(const QString& filePath) const
{
    const QString suffix = QFileInfo(filePath).suffix();
    return FileUtils::isVideoFile(suffix) && suffix.compare(QStringLiteral("mov"), Qt::CaseInsensitive) != 0;
}

TLRenderPlayer::FrameAcquisitionStats TLRenderPlayer::frameAcquisitionStatsForTest() const
{
#ifdef HAVE_TLRENDER
    QMutexLocker locker(&m_mutex);
    return m_frameAcquisitionStats;
#else
    return TLRenderPlayer::FrameAcquisitionStats();
#endif
}

void TLRenderPlayer::resetFrameAcquisitionStatsForTest()
{
#ifdef HAVE_TLRENDER
    QMutexLocker locker(&m_mutex);
    m_frameAcquisitionStats = TLRenderPlayer::FrameAcquisitionStats();
#endif
}

QImage TLRenderPlayer::extractThumbnail(const QString& filePath, const QSize& targetSize, qint64 positionMs)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    // Reduce FFmpeg logging noise
    static bool logLevelSet = false;
    if (!logLevelSet) {
        av_log_set_level(AV_LOG_ERROR);
        logLevelSet = true;
    }

    if (filePath.isEmpty()) {
        return {};
    }

    AVFormatContext* fmtCtx = nullptr;
    QByteArray localPath = QFile::encodeName(filePath);
    int ret = avformat_open_input(&fmtCtx, localPath.constData(), nullptr, nullptr);
    if (ret < 0 || !fmtCtx) {
        return {};
    }

    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&fmtCtx);
        return {};
    }

    const int vIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIdx < 0) {
        avformat_close_input(&fmtCtx);
        return {};
    }

    AVStream* vs = fmtCtx->streams[vIdx];
    if (!vs || !vs->codecpar) {
        avformat_close_input(&fmtCtx);
        return {};
    }

    const AVCodec* codec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!codec) {
        avformat_close_input(&fmtCtx);
        return {};
    }

    AVCodecContext* decCtx = avcodec_alloc_context3(codec);
    if (!decCtx) {
        avformat_close_input(&fmtCtx);
        return {};
    }

    ret = avcodec_parameters_to_context(decCtx, vs->codecpar);
    if (ret < 0) {
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return {};
    }

    ret = avcodec_open2(decCtx, codec, nullptr);
    if (ret < 0) {
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return {};
    }

    // Seek to requested position.
    // Convert ms -> stream time_base.
    const double targetSeconds = qMax<qint64>(0, positionMs) / 1000.0;
    const double tb = av_q2d(vs->time_base);
    if (tb > 0.0) {
        const int64_t targetTs = static_cast<int64_t>(targetSeconds / tb);
        av_seek_frame(fmtCtx, vIdx, targetTs, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(decCtx);
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgb = av_frame_alloc();
    if (!pkt || !frame || !rgb) {
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        if (rgb) av_frame_free(&rgb);
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return {};
    }

    const int srcW = decCtx->width;
    const int srcH = decCtx->height;
    if (srcW <= 0 || srcH <= 0) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        av_frame_free(&rgb);
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return {};
    }

    SwsContext* sws = sws_getContext(
        srcW, srcH, decCtx->pix_fmt,
        srcW, srcH, AV_PIX_FMT_BGRA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        av_frame_free(&rgb);
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return {};
    }

    const int rgbBufSize = av_image_get_buffer_size(AV_PIX_FMT_BGRA, srcW, srcH, 1);
    uint8_t* rgbBuf = static_cast<uint8_t*>(av_malloc(rgbBufSize));
    if (!rgbBuf) {
        sws_freeContext(sws);
        av_packet_free(&pkt);
        av_frame_free(&frame);
        av_frame_free(&rgb);
        avcodec_free_context(&decCtx);
        avformat_close_input(&fmtCtx);
        return {};
    }
    av_image_fill_arrays(rgb->data, rgb->linesize, rgbBuf, AV_PIX_FMT_BGRA, srcW, srcH, 1);

    QImage out;
    bool gotFrame = false;
    int safetyPackets = 0;

    while (av_read_frame(fmtCtx, pkt) >= 0 && safetyPackets < 5000) {
        ++safetyPackets;
        if (pkt->stream_index != vIdx) {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(decCtx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) {
            continue;
        }

        while ((ret = avcodec_receive_frame(decCtx, frame)) >= 0) {
            // Optional: try to reach/skip to target time, but keep it simple.
            // Many codecs will output the closest prior frame after AVSEEK_FLAG_BACKWARD.
            sws_scale(sws, frame->data, frame->linesize, 0, srcH, rgb->data, rgb->linesize);

            QImage img(rgb->data[0], srcW, srcH, rgb->linesize[0], QImage::Format_ARGB32);
            out = img.copy();
            gotFrame = true;
            break;
        }

        if (gotFrame) {
            break;
        }
    }

    if (gotFrame && !targetSize.isEmpty()) {
        out = out.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    av_free(rgbBuf);
    sws_freeContext(sws);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_frame_free(&rgb);
    avcodec_free_context(&decCtx);
    avformat_close_input(&fmtCtx);

    return out;
#else
    Q_UNUSED(filePath);
    Q_UNUSED(targetSize);
    Q_UNUSED(positionMs);
    return {};
#endif
}

qint64 TLRenderPlayer::queryDuration(const QString& filePath)
{
#if defined(HAVE_FFMPEG) && HAVE_FFMPEG
    if (filePath.isEmpty()) {
        return 0;
    }

    AVFormatContext* fmtCtx = nullptr;
    QByteArray localPath = QFile::encodeName(filePath);
    int ret = avformat_open_input(&fmtCtx, localPath.constData(), nullptr, nullptr);
    if (ret < 0 || !fmtCtx) {
        return 0;
    }

    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&fmtCtx);
        return 0;
    }

    qint64 durationMs = 0;
    if (fmtCtx->duration > 0) {
        durationMs = static_cast<qint64>((fmtCtx->duration * 1000.0) / AV_TIME_BASE);
    } else {
        const int vIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (vIdx >= 0) {
            AVStream* vs = fmtCtx->streams[vIdx];
            if (vs && vs->duration > 0) {
                const double sec = vs->duration * av_q2d(vs->time_base);
                durationMs = static_cast<qint64>(sec * 1000.0);
            }
        }
    }

    avformat_close_input(&fmtCtx);
    return durationMs;
#else
    Q_UNUSED(filePath);
    return 0;
#endif
}
