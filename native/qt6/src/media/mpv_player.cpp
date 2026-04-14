#include "mpv_player.h"

#include <QByteArray>
#include <QFile>
#include <QMetaObject>
#include <QTimer>
#include <QDebug>

namespace {
constexpr uint64_t kObserveTimePos = 1;
constexpr uint64_t kObserveDuration = 2;
constexpr uint64_t kObservePause = 3;
constexpr uint64_t kObserveWidth = 4;
constexpr uint64_t kObserveHeight = 5;
constexpr uint64_t kObserveFps = 6;
constexpr uint64_t kObserveEstimatedFrames = 7;
constexpr uint64_t kObserveVolume = 8;
constexpr uint64_t kObserveMute = 9;
constexpr uint64_t kObserveAudioParams = 10;
}

MpvPlayer::MpvPlayer(QObject* parent)
    : QObject(parent)
    , m_runtime(LibMpvRuntime::instance())
{
    if (!m_runtime.isAvailable()) {
        return;
    }

    m_mpv = m_runtime.create();
    if (!m_mpv) {
        return;
    }

    m_runtime.set_option_string(m_mpv, "vo", "libmpv");
    m_runtime.set_option_string(m_mpv, "hwdec", "auto-safe");
    m_runtime.set_option_string(m_mpv, "keep-open", "yes");
    m_runtime.set_option_string(m_mpv, "keepaspect", "yes");
    m_runtime.set_option_string(m_mpv, "osc", "no");
    m_runtime.set_option_string(m_mpv, "input-default-bindings", "no");
    m_runtime.set_option_string(m_mpv, "input-vo-keyboard", "no");
    m_runtime.set_option_string(m_mpv, "config", "no");
    m_runtime.set_option_string(m_mpv, "audio-display", "no");

    if (m_runtime.initialize(m_mpv) < 0) {
        m_runtime.terminate_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    m_runtime.observe_property(m_mpv, kObserveTimePos, "time-pos", MPV_FORMAT_DOUBLE);
    m_runtime.observe_property(m_mpv, kObserveDuration, "duration", MPV_FORMAT_DOUBLE);
    m_runtime.observe_property(m_mpv, kObservePause, "pause", MPV_FORMAT_FLAG);
    m_runtime.observe_property(m_mpv, kObserveWidth, "width", MPV_FORMAT_INT64);
    m_runtime.observe_property(m_mpv, kObserveHeight, "height", MPV_FORMAT_INT64);
    m_runtime.observe_property(m_mpv, kObserveFps, "container-fps", MPV_FORMAT_DOUBLE);
    m_runtime.observe_property(m_mpv, kObserveEstimatedFrames, "estimated-frame-count", MPV_FORMAT_INT64);
    m_runtime.observe_property(m_mpv, kObserveVolume, "volume", MPV_FORMAT_DOUBLE);
    m_runtime.observe_property(m_mpv, kObserveMute, "mute", MPV_FORMAT_FLAG);
    m_runtime.observe_property(m_mpv, kObserveAudioParams, "audio-params", MPV_FORMAT_NODE);
    m_runtime.request_event(m_mpv, MPV_EVENT_FILE_LOADED, 1);
    m_runtime.request_event(m_mpv, MPV_EVENT_END_FILE, 1);
    m_runtime.set_wakeup_callback(m_mpv, &MpvPlayer::wakeup, this);
}

MpvPlayer::~MpvPlayer()
{
    if (m_mpv) {
        m_runtime.set_wakeup_callback(m_mpv, nullptr, nullptr);
        m_runtime.terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

bool MpvPlayer::isAvailable() const
{
    return m_runtime.isAvailable() && m_mpv;
}

QString MpvPlayer::availabilityError() const
{
    return m_runtime.errorString();
}

void MpvPlayer::wakeup(void* ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvPlayer*>(ctx), &MpvPlayer::onMpvEvents, Qt::QueuedConnection);
}

void MpvPlayer::onMpvEvents()
{
    if (!m_mpv) {
        return;
    }
    while (true) {
        mpv_event* event = m_runtime.wait_event(m_mpv, 0.0);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            break;
        }
        processEvent(event);
    }
}

void MpvPlayer::processEvent(mpv_event* event)
{
    qWarning() << "[MpvPlayer] event id=" << event->event_id;
    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto* prop = static_cast<mpv_event_property*>(event->data);
        if (!prop || prop->format == MPV_FORMAT_NONE) {
            break;
        }
        switch (event->reply_userdata) {
        case kObserveTimePos: {
            const double timePos = *static_cast<double*>(prop->data);
            m_positionMs = qMax<qint64>(0, qRound64(timePos * 1000.0));
            const qint64 frame = qRound64(timePos * fpsOrDefault());
            if (frame != m_currentFrame) {
                m_currentFrame = frame;
                emit currentFrameChanged(frame);
            }
            emit positionChanged(m_positionMs);
            break;
        }
        case kObserveDuration: {
            const double duration = *static_cast<double*>(prop->data);
            m_durationMs = qMax<qint64>(0, qRound64(duration * 1000.0));
            m_mediaInfo.durationMs = m_durationMs;
            emit durationChanged(m_durationMs);
            break;
        }
        case kObservePause:
            updatePlaybackStateFromPause(*static_cast<int*>(prop->data) != 0);
            break;
        case kObserveWidth:
            m_mediaInfo.width = static_cast<int>(*static_cast<int64_t*>(prop->data));
            break;
        case kObserveHeight:
            m_mediaInfo.height = static_cast<int>(*static_cast<int64_t*>(prop->data));
            break;
        case kObserveFps:
            m_mediaInfo.fps = *static_cast<double*>(prop->data);
            break;
        case kObserveEstimatedFrames:
            m_totalFrames = *static_cast<int64_t*>(prop->data);
            m_mediaInfo.totalFrames = m_totalFrames;
            break;
        case kObserveVolume:
            m_volume = static_cast<float>(*static_cast<double*>(prop->data) / 100.0);
            break;
        case kObserveMute:
            m_muted = (*static_cast<int*>(prop->data) != 0);
            break;
        case kObserveAudioParams:
            if (auto* node = static_cast<mpv_node*>(prop->data); node && node->format == MPV_FORMAT_NODE_MAP && node->u.list) {
                m_mediaInfo.hasAudio = node->u.list->num > 0;
                for (int i = 0; i < node->u.list->num; ++i) {
                    const QString key = QString::fromUtf8(node->u.list->keys[i]);
                    const mpv_node& value = node->u.list->values[i];
                    if (key == QLatin1String("samplerate") && value.format == MPV_FORMAT_INT64) {
                        m_mediaInfo.audioSampleRate = static_cast<int>(value.u.int64);
                    } else if (key == QLatin1String("channels") && value.format == MPV_FORMAT_STRING) {
                        m_mediaInfo.audioChannels = QString::fromUtf8(value.u.string).count(QLatin1Char(',')) + 1;
                    }
                }
            } else {
                m_mediaInfo.hasAudio = false;
                m_mediaInfo.audioChannels = 0;
                m_mediaInfo.audioSampleRate = 0;
            }
            break;
        default:
            break;
        }
        break;
    }
    case MPV_EVENT_FILE_LOADED:
        qWarning() << "[MpvPlayer] FILE_LOADED";
        updateMediaInfoFromCore();
        emit mediaInfoReady(m_mediaInfo);
        break;
    case MPV_EVENT_END_FILE: {
        auto* endFile = static_cast<mpv_event_end_file*>(event->data);
        qWarning() << "[MpvPlayer] END_FILE reason=" << (endFile ? endFile->reason : -1)
                   << "error=" << (endFile ? endFile->error : 0)
                   << "msg=" << (endFile ? QString::fromUtf8(m_runtime.error_string(endFile->error)) : QString());
        if (endFile && endFile->reason == MPV_END_FILE_REASON_ERROR) {
            emit error(QString::fromUtf8(m_runtime.error_string(endFile->error)));
        } else {
            emit endOfStream();
        }
        break;
    }
    case MPV_EVENT_VIDEO_RECONFIG:
        qWarning() << "[MpvPlayer] VIDEO_RECONFIG";
        updateMediaInfoFromCore();
        emit mediaInfoReady(m_mediaInfo);
        break;
    case MPV_EVENT_PLAYBACK_RESTART:
        qWarning() << "[MpvPlayer] PLAYBACK_RESTART";
        emit frameUpdated();
        break;
    default:
        break;
    }
}

void MpvPlayer::updatePlaybackStateFromPause(bool paused)
{
    const auto newState = paused ? media_player::PlaybackState::Paused : media_player::PlaybackState::Playing;
    if (newState != m_playbackState) {
        m_playbackState = newState;
        emit playbackStateChanged(newState);
    }
}

void MpvPlayer::updateMediaInfoFromCore()
{
    if (!m_mpv) {
        return;
    }
    m_mediaInfo.durationMs = m_durationMs;
    m_mediaInfo.totalFrames = m_totalFrames;
}

double MpvPlayer::fpsOrDefault() const
{
    return m_mediaInfo.fps > 0.0 ? m_mediaInfo.fps : 24.0;
}

void MpvPlayer::loadMedia(const QString& filePath)
{
    if (!m_mpv) {
        emit error(availabilityError());
        return;
    }
    m_currentPath = filePath;
    m_playbackState = media_player::PlaybackState::Stopped;
    m_positionMs = 0;
    m_durationMs = 0;
    m_currentFrame = 0;
    m_totalFrames = 0;
    m_mediaInfo = media_player::MediaInfo();

    QByteArray path = QFile::encodeName(filePath);
    const char* cmd[] = {"loadfile", path.constData(), "replace", nullptr};
    const int rc = m_runtime.command(m_mpv, cmd);
    if (rc < 0) {
        emit error(QString::fromUtf8(m_runtime.error_string(rc)));
    }
}

void MpvPlayer::unloadMedia()
{
    if (!m_mpv) {
        return;
    }
    const char* cmd[] = {"stop", nullptr};
    m_runtime.command(m_mpv, cmd);
    m_currentPath.clear();
}

bool MpvPlayer::hasMedia() const
{
    return !m_currentPath.isEmpty();
}

void MpvPlayer::play()
{
    if (!m_mpv) {
        return;
    }
    int paused = 0;
    m_runtime.set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &paused);
    if (m_playbackRate < 0.0) {
        m_runtime.command(m_mpv, (const char*[]){"seek", "0", "relative", "exact", nullptr});
    }
    m_playbackState = media_player::PlaybackState::Playing;
    emit playbackStateChanged(m_playbackState);
}

void MpvPlayer::pause()
{
    if (!m_mpv) {
        return;
    }
    int paused = 1;
    m_runtime.set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &paused);
    m_playbackState = media_player::PlaybackState::Paused;
    emit playbackStateChanged(m_playbackState);
}

void MpvPlayer::stop()
{
    if (!m_mpv) {
        return;
    }
    const char* cmd[] = {"stop", nullptr};
    m_runtime.command(m_mpv, cmd);
    m_playbackState = media_player::PlaybackState::Stopped;
    emit playbackStateChanged(m_playbackState);
}

void MpvPlayer::seek(qint64 positionMs)
{
    if (!m_mpv) {
        return;
    }
    const double seconds = positionMs / 1000.0;
    QByteArray pos = QByteArray::number(seconds, 'f', 6);
    const char* cmd[] = {"seek", pos.constData(), "absolute", "exact", nullptr};
    m_runtime.command(m_mpv, cmd);
}

void MpvPlayer::seekToFrame(qint64 frameNumber)
{
    seek(qRound64((frameNumber / fpsOrDefault()) * 1000.0));
}

void MpvPlayer::setPlaybackRate(double rate)
{
    if (!m_mpv) {
        return;
    }
    m_playbackRate = rate;
    const double absRate = qAbs(rate);
    m_runtime.set_property(m_mpv, "speed", MPV_FORMAT_DOUBLE, const_cast<double*>(&absRate));
    if (rate < 0.0) {
        int yes = 1;
        m_runtime.set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &yes);
    }
}

void MpvPlayer::setLoopMode(media_player::LoopMode mode)
{
    m_loopMode = mode;
    if (!m_mpv) {
        return;
    }
    const char* value = mode == media_player::LoopMode::Loop ? "inf" : "no";
    m_runtime.set_property_string(m_mpv, "loop-file", value);
}

void MpvPlayer::stepForward()
{
    if (!m_mpv) {
        return;
    }
    const char* cmd[] = {"frame-step", nullptr};
    m_runtime.command(m_mpv, cmd);
}

void MpvPlayer::stepBackward()
{
    if (!m_mpv) {
        return;
    }
    const char* cmd[] = {"frame-back-step", nullptr};
    m_runtime.command(m_mpv, cmd);
}

void MpvPlayer::setVolume(float volume)
{
    m_volume = qBound(0.0f, volume, 1.0f);
    if (!m_mpv) {
        return;
    }
    double percent = m_volume * 100.0;
    m_runtime.set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &percent);
}

void MpvPlayer::setMuted(bool muted)
{
    m_muted = muted;
    if (!m_mpv) {
        return;
    }
    int flag = muted ? 1 : 0;
    m_runtime.set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &flag);
}

QImage MpvPlayer::currentFrameImage() const
{
    QMutexLocker locker(&m_frameMutex);
    if (!m_lastFramebufferImage.isNull()) {
        return m_lastFramebufferImage;
    }
    if (!m_mpv) {
        return {};
    }

    mpv_node result{};
    mpv_node args[4]{};
    args[0].format = MPV_FORMAT_STRING;
    args[0].u.string = const_cast<char*>("screenshot-raw");
    args[1].format = MPV_FORMAT_STRING;
    args[1].u.string = const_cast<char*>("video");
    args[2].format = MPV_FORMAT_STRING;
    args[2].u.string = const_cast<char*>("single");
    mpv_node_list list{3, args, nullptr};
    mpv_node root{};
    root.format = MPV_FORMAT_NODE_ARRAY;
    root.u.list = &list;

    if (m_runtime.command_node(m_mpv, &root, &result) < 0) {
        return {};
    }

    QImage image;
    if (result.format == MPV_FORMAT_NODE_MAP && result.u.list) {
        int w = 0;
        int h = 0;
        int stride = 0;
        QByteArray format;
        const uchar* data = nullptr;
        size_t dataSize = 0;
        for (int i = 0; i < result.u.list->num; ++i) {
            const QString key = QString::fromUtf8(result.u.list->keys[i]);
            const mpv_node& value = result.u.list->values[i];
            if (key == QLatin1String("w") && value.format == MPV_FORMAT_INT64) {
                w = static_cast<int>(value.u.int64);
            } else if (key == QLatin1String("h") && value.format == MPV_FORMAT_INT64) {
                h = static_cast<int>(value.u.int64);
            } else if (key == QLatin1String("stride") && value.format == MPV_FORMAT_INT64) {
                stride = static_cast<int>(value.u.int64);
            } else if (key == QLatin1String("format") && value.format == MPV_FORMAT_STRING) {
                format = value.u.string;
            } else if (key == QLatin1String("data") && value.format == MPV_FORMAT_BYTE_ARRAY && value.u.ba) {
                data = static_cast<const uchar*>(value.u.ba->data);
                dataSize = value.u.ba->size;
            }
        }
        if (w > 0 && h > 0 && stride > 0 && data && dataSize >= static_cast<size_t>(stride * h) && format == QByteArrayLiteral("bgr0")) {
            QImage wrapped(data, stride / 4, h, stride, QImage::Format_ARGB32);
            image = wrapped.copy().rgbSwapped();
        }
    }

    m_runtime.free_node_contents(&result);
    return image;
}

void MpvPlayer::setLastFramebufferImage(const QImage& image)
{
    QMutexLocker locker(&m_frameMutex);
    m_lastFramebufferImage = image;
}
