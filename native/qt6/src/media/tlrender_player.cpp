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
#include <QProcess>
#include <QProcessEnvironment>
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
QImage planarYuv422ToQImage(const std::shared_ptr<ftk::Image>& image, const QSize& = QSize())
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
    // RGB coefficients below already include the 2x centered-chroma expansion.
    const float uvScale = legalRange ? (255.0f / 224.0f) : 1.0f;
    const float rFactor = 2.0f * (1.0f - coefficients.kr);
    const float bFactor = 2.0f * (1.0f - coefficients.kb);
    const float gUFactor = 2.0f * coefficients.kb * (1.0f - coefficients.kb) / kg;
    const float gVFactor = 2.0f * coefficients.kr * (1.0f - coefficients.kr) / kg;

    QImage out(width, height, QImage::Format_RGB32);
    if (out.isNull()) {
        return {};
    }

    for (int y = 0; y < height; ++y) {
        const auto* yRow = reinterpret_cast<const Sample*>(base + yStride * static_cast<size_t>(y));
        const auto* uRow = reinterpret_cast<const Sample*>(uBase + uvStride * static_cast<size_t>(y));
        const auto* vRow = reinterpret_cast<const Sample*>(vBase + uvStride * static_cast<size_t>(y));
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));

        for (int x = 0; x < width; ++x) {
            const float ySample = clampUnit((static_cast<float>(yRow[x]) * invMaxValue - yOffset) * yScale);
            const float uSample = (static_cast<float>(uRow[x / 2]) * invMaxValue - uvOffset) * uvScale;
            const float vSample = (static_cast<float>(vRow[x / 2]) * invMaxValue - uvOffset) * uvScale;

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
QImage planarYuv420ToQImage(const std::shared_ptr<ftk::Image>& image, const QSize& = QSize())
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
    // RGB coefficients below already include the 2x centered-chroma expansion.
    const float uvScale = legalRange ? (255.0f / 224.0f) : 1.0f;
    const float rFactor = 2.0f * (1.0f - coefficients.kr);
    const float bFactor = 2.0f * (1.0f - coefficients.kb);
    const float gUFactor = 2.0f * coefficients.kb * (1.0f - coefficients.kb) / kg;
    const float gVFactor = 2.0f * coefficients.kr * (1.0f - coefficients.kr) / kg;

    QImage out(width, height, QImage::Format_RGB32);
    if (out.isNull()) {
        return {};
    }

    for (int y = 0; y < height; ++y) {
        const auto* yRow = reinterpret_cast<const Sample*>(base + yStride * static_cast<size_t>(y));
        const auto* uRow = reinterpret_cast<const Sample*>(uBase + uvStride * static_cast<size_t>(y / 2));
        const auto* vRow = reinterpret_cast<const Sample*>(vBase + uvStride * static_cast<size_t>(y / 2));
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));

        for (int x = 0; x < width; ++x) {
            const float ySample = clampUnit((static_cast<float>(yRow[x]) * invMaxValue - yOffset) * yScale);
            const float uSample = (static_cast<float>(uRow[x / 2]) * invMaxValue - uvOffset) * uvScale;
            const float vSample = (static_cast<float>(vRow[x / 2]) * invMaxValue - uvOffset) * uvScale;

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
    connect(m_ffmpegMovPlayer, &FFmpegMovPlayer::frameUpdated, this, [this]() {
        QMutexLocker locker(&m_mutex);
        locker.unlock();
        emit videoFramesChanged();
    });

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

    {
        QMutexLocker locker(&m_mutex);

        // Clear cached frames from previous media before any early return.
        m_pendingPlay = false;
        m_pendingReverse = false;
        m_cachedVideoFrames.clear();
        m_frameAcquisitionStats = FrameAcquisitionStats();
    }
#ifdef HAVE_TLRENDER
    ensureTlRenderContext();
    if (!m_context) {
        emit error(tr("Failed to initialize tlRender context"));
        return;
    }

    QMutexLocker locker(&m_mutex);

    // Stop any current playback
    if (m_player) {
        m_player->stop();
    }
    
    try {
        // Convert path
        std::string pathStr = filePath.toStdString();
        ftk::Path path(pathStr);
        
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
            }
        }
#endif
        m_timeline = tl::Timeline::create(m_context, path, timelineOptions);
        
        if (!m_timeline) {
            emit error(tr("Failed to create timeline from: %1").arg(filePath));
            return;
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

                // Start actual playback now that we have frames
                if (shouldStartPlayback && m_player) {
                    if (reversePlayback) {
                        m_manualReversePlaybackActive = true;
                        m_manualReverseStepAccumulatorMs = 0.0;
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

                // Lazy update media info if we haven't got valid duration yet
                // (common for sequences where timeline loads asynchronously)
                if (m_duration.load() <= 0 && timeRange.duration().value() > 0.0) {
                    updateMediaInfo();
                }

                if (newPositionMs != m_position) {
                    m_position = newPositionMs;
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
        return;
    }
    
    const auto& ioInfo = m_player->getIOInfo();
    const auto& timeRange = m_player->getTimeRange();
    
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
        return;
    }

    stopManualReversePlayback();

    // Honor current playback rate sign (JKL expects reverse playback).
    if (m_playbackRate < 0.0) {
        m_player->stop();
        m_manualReversePlaybackActive = true;
        m_manualReverseStepAccumulatorMs = 0.0;
        m_playbackState = PlaybackState::Playing;
        emit playbackStateChanged(PlaybackState::Playing);
        return;
    }

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
            m_pendingPlay = true;
            m_pendingReverse = false;
            m_playbackState = PlaybackState::Playing;  // UI shows "playing" immediately
            locker.unlock();
            emit playbackStateChanged(PlaybackState::Playing);
            return;
        }
    }

    m_player->setSpeedMult(m_playbackRate);
    m_player->forward();
    m_playbackState = PlaybackState::Playing;
    emit playbackStateChanged(PlaybackState::Playing);
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
        return;
    }

    stopManualReversePlayback();

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

    stopManualReversePlayback();

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
    if (m_ffmpegMovPlayer && m_ffmpegMovPlayer->hasMedia()) {
        if (m_playbackState == PlaybackState::Playing) {
            pause();
        } else {
            play();
        }
        return;
    }
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

    stopManualReversePlayback();

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

    stopManualReversePlayback();

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
            m_player->stop();
            m_manualReversePlaybackActive = true;
            m_manualReverseStepAccumulatorMs = 0.0;
        } else {
            stopManualReversePlayback();
            m_player->forward();
        }
    } else if (rate >= 0.0) {
        stopManualReversePlayback();
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
    stopManualReversePlayback();
    m_player->frameNext();
#endif
}

void TLRenderPlayer::stepBackwardInternal()
{
#ifdef HAVE_TLRENDER
    if (!m_player) return;
    {
        QMutexLocker locker(&m_mutex);
        m_cachedVideoFrames.clear();
    }
    m_player->framePrev();
    tick();
    emit videoFramesChanged();
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
    stopManualReversePlayback();
    stepBackwardInternal();
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
    stopManualReversePlayback();
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
    stopManualReversePlayback();
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
    stopManualReversePlayback();
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
    stopManualReversePlayback();
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

void TLRenderPlayer::stopManualReversePlayback()
{
#ifdef HAVE_TLRENDER
    m_manualReversePlaybackActive = false;
    m_manualReverseStepAccumulatorMs = 0.0;
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
    if (m_manualReversePlaybackActive) {
        const double fps = m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : 24.0;
        const double rate = std::abs(static_cast<double>(m_playbackRate));
        const double frameDurationMs = 1000.0 / qMax(0.001, fps * qMax(0.001, rate));
        m_manualReverseStepAccumulatorMs += m_updateTimer->interval();

        while (m_manualReverseStepAccumulatorMs >= frameDurationMs) {
            m_manualReverseStepAccumulatorMs -= frameDurationMs;
            stepBackwardInternal();
            if (m_currentFrame.load() <= 0) {
                m_playbackState = PlaybackState::Paused;
                emit playbackStateChanged(PlaybackState::Paused);
                stopManualReversePlayback();
                break;
            }
        }
        return;
    }
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
        QImage frame = m_ffmpegMovPlayer->currentFrameImage(targetSize);
#ifdef HAVE_TLRENDER
        QMutexLocker locker(&m_mutex);
        m_frameAcquisitionStats.lastGetCurrentFrameNs = timer.nsecsElapsed();
#endif
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
    std::vector<tl::VideoFrame> cachedVideoFrames;
    QString currentPath;
    PlaybackState playbackState = PlaybackState::Stopped;

    {
        QMutexLocker locker(&m_mutex);
        cachedVideoFrames = m_cachedVideoFrames;
        currentPath = m_currentPath;
        playbackState = m_playbackState;
    }

    QImage cachedFrame = currentVideoFramesToQImage(cachedVideoFrames, targetSize);
    if (!cachedFrame.isNull()) {
        {
            QMutexLocker locker(&m_mutex);
            ++m_frameAcquisitionStats.cachedFrameConversions;
            if (!cachedVideoFrames.empty() && !cachedVideoFrames.front().layers.empty() && cachedVideoFrames.front().layers.front().image) {
                m_frameAcquisitionStats.lastCachedImageType = static_cast<int>(cachedVideoFrames.front().layers.front().image->getInfo().type);
            }
            m_frameAcquisitionStats.lastCachedConversionNs = timer.nsecsElapsed();
            m_frameAcquisitionStats.lastGetCurrentFrameNs = m_frameAcquisitionStats.lastCachedConversionNs;
            return cachedFrame;
        }
    }

    if (!cachedVideoFrames.empty()) {
        for (const auto& frame : cachedVideoFrames) {
            for (const auto& layer : frame.layers) {
                if (layer.image) {
                    QMutexLocker locker(&m_mutex);
                    ++m_frameAcquisitionStats.unsupportedCachedFrameTypes;
                    m_frameAcquisitionStats.lastCachedImageType = static_cast<int>(layer.image->getInfo().type);
                    break;
                }
            }
        }
    }

    // For now, reuse the static thumbnail extraction when possible.
    // This remains the fallback when tlRender has no cached frame yet or the
    // cached frame format is not handled above.
    if (playbackState == PlaybackState::Playing || currentPath.isEmpty() || !m_player) {
        QMutexLocker locker(&m_mutex);
        m_frameAcquisitionStats.lastGetCurrentFrameNs = timer.nsecsElapsed();
        return QImage();
    }
    const QImage fallback = extractThumbnail(currentPath, targetSize, position());
    {
        QMutexLocker locker(&m_mutex);
        ++m_frameAcquisitionStats.fallbackExtractions;
        m_frameAcquisitionStats.lastFallbackExtractionNs = timer.nsecsElapsed();
        m_frameAcquisitionStats.lastGetCurrentFrameNs = m_frameAcquisitionStats.lastFallbackExtractionNs;
    }
    return fallback;
#endif

    return QImage();
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
    if (filePath.isEmpty()) {
        return {};
    }

    // Keep thumbnail generation out of the app process. The in-process FFmpeg
    // decode + swscale + QImage conversion path reproducibly corrupts heap
    // memory for the Robotics MP4s during File Manager grid/side previews.
    // ffmpeg CLI isolates decoder crashes/corruption from the UI process while
    // preserving video thumbnails, side preview posters, and scrubbing requests.
    QProcess ffmpeg;
    QStringList args;
    args << QStringLiteral("-v") << QStringLiteral("error");
    if (positionMs > 0) {
        args << QStringLiteral("-ss") << QString::number(positionMs / 1000.0, 'f', 3);
    }
    args << QStringLiteral("-i") << filePath
         << QStringLiteral("-frames:v") << QStringLiteral("1")
         << QStringLiteral("-f") << QStringLiteral("image2pipe")
         << QStringLiteral("-vcodec") << QStringLiteral("png")
         << QStringLiteral("pipe:1");

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("LD_LIBRARY_PATH"));
    ffmpeg.setProcessEnvironment(env);
    ffmpeg.start(QStringLiteral("/usr/bin/ffmpeg"), args, QIODevice::ReadOnly);
    if (!ffmpeg.waitForStarted(3000)) {
        qWarning() << "[TLRenderPlayer] ffmpeg thumbnail process failed to start" << filePath;
        return {};
    }
    if (!ffmpeg.waitForFinished(15000)) {
        ffmpeg.kill();
        ffmpeg.waitForFinished(1000);
        qWarning() << "[TLRenderPlayer] ffmpeg thumbnail process timed out" << filePath;
        return {};
    }
    if (ffmpeg.exitStatus() != QProcess::NormalExit || ffmpeg.exitCode() != 0) {
        qWarning() << "[TLRenderPlayer] ffmpeg thumbnail process failed"
                   << filePath << ffmpeg.readAllStandardError();
        return {};
    }

    QImage image;
    if (!image.loadFromData(ffmpeg.readAllStandardOutput(), "PNG")) {
        qWarning() << "[TLRenderPlayer] Failed to decode ffmpeg thumbnail PNG" << filePath;
        return {};
    }

    if (!targetSize.isEmpty()) {
        image = image.scaled(targetSize, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    return image;
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
