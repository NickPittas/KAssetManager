/**
 * GStreamerPlayer - Professional video playback using GStreamer
 *
 * This implementation uses GStreamer's playbin with direct widget rendering
 * for hardware-accelerated, professional-grade video playback.
 */

#include "gstreamer_player.h"
#include <QDebug>
#include <QFileInfo>
#include <QUrl>
#include <QMutexLocker>
#include <QApplication>
#include <QScreen>

#ifdef HAVE_GSTREAMER
#include <gst/gst.h>
#include <gst/video/videooverlay.h>
#include <gst/app/gstappsink.h>
#include <gst/video/video.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif

bool GStreamerPlayer::s_gstInitialized = false;

GStreamerPlayer::~GStreamerPlayer()
{
#ifdef HAVE_GSTREAMER
    cleanup();
#endif
}

void GStreamerPlayer::initialize()
{
#ifdef HAVE_GSTREAMER
    // Initialize GStreamer once globally
    if (!s_gstInitialized) {
        GError* error = nullptr;
        if (!gst_init_check(nullptr, nullptr, &error)) {
            qCritical() << "[GStreamerPlayer] Failed to initialize GStreamer:"
                        << (error ? error->message : "Unknown error");
            if (error) g_error_free(error);
            return;
        }

        qInfo() << "[GStreamerPlayer] GStreamer initialized successfully";
        qInfo() << "[GStreamerPlayer] GStreamer version:" << gst_version_string();

        // List available video sinks for debugging
        GstElementFactory* d3d11 = gst_element_factory_find("d3d11videosink");
        GstElementFactory* d3d = gst_element_factory_find("d3dvideosink");
        GstElementFactory* gl = gst_element_factory_find("glimagesink");
        qInfo() << "[GStreamerPlayer] Available sinks: d3d11=" << (d3d11 != nullptr)
                << "d3d=" << (d3d != nullptr) << "gl=" << (gl != nullptr);
        if (d3d11) gst_object_unref(d3d11);
        if (d3d) gst_object_unref(d3d);
        if (gl) gst_object_unref(gl);

        s_gstInitialized = true;
    }
#endif
}

GStreamerPlayer::GStreamerPlayer(QObject* parent)
    : QObject(parent)
    , m_pipeline(nullptr)
    , m_videoWidget(nullptr)
    , m_bus(nullptr)
{
#ifdef HAVE_GSTREAMER
    // Initialize atomic members (they're already initialized in the header with default values)
    m_playbackState.store(PlaybackState::Stopped);
    m_position.store(0);
    m_duration.store(0);
    m_volume.store(1.0);
    m_muted.store(false);

    // Create timers
    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(33); // ~30fps for smooth position updates
    connect(m_positionTimer, &QTimer::timeout, this, &GStreamerPlayer::onPositionUpdate);

    m_busTimer = new QTimer(this);
    m_busTimer->setInterval(10); // Check bus messages frequently for responsiveness
    connect(m_busTimer, &QTimer::timeout, this, &GStreamerPlayer::onBusMessage);
#endif
}

void GStreamerPlayer::cleanup()
{
#ifdef HAVE_GSTREAMER
    QMutexLocker locker(&m_mutex);

    if (m_positionTimer) {
        m_positionTimer->stop();
    }

    if (m_busTimer) {
        m_busTimer->stop();
    }

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }

    if (m_bus) {
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }
#endif
}

void GStreamerPlayer::setVideoWidget(QWidget* widget)
{
    m_videoWidget = widget;

    if (m_videoWidget) {
        // CRITICAL: Set widget attributes for native window embedding
        // Without these, GStreamer will create its own window
        m_videoWidget->setAttribute(Qt::WA_NativeWindow);
        m_videoWidget->setAttribute(Qt::WA_PaintOnScreen);
        m_videoWidget->setAttribute(Qt::WA_OpaquePaintEvent);

        // Force creation of native window handle
        m_videoWidget->winId();

        qDebug() << "[GStreamerPlayer] Video widget configured for embedding, WId:" << m_videoWidget->winId();
    }

    // If we already have a pipeline, update the window handle
    if (m_pipeline && m_videoWidget) {
        setWindowHandle();
    }
}

void GStreamerPlayer::loadMedia(const QString& filePath)
{
#ifdef HAVE_GSTREAMER
    QMutexLocker locker(&m_mutex);

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        emit error(QString("File does not exist: %1").arg(filePath));
        return;
    }

    // Convert file path to URI
    QString uri = QUrl::fromLocalFile(filePath).toString();
    m_currentUri = uri;

    qInfo() << "[GStreamerPlayer] Loading media:" << uri;

    // PERFORMANCE FIX: Reuse existing pipeline if available
    // Creating a new pipeline every time is extremely slow (5-10 seconds)
    // Just change the URI on the existing playbin instead
    if (m_pipeline) {
        // Stop current playback
        gst_element_set_state(m_pipeline, GST_STATE_READY);

        // Change URI - playbin will handle codec detection automatically
        g_object_set(m_pipeline, "uri", uri.toUtf8().constData(), nullptr);

        // Set to PAUSED to start prerolling (non-blocking)
        GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            emit error("Failed to set pipeline to PAUSED state");
            return;
        }

        // Update media info asynchronously when preroll completes
        // The ASYNC_DONE message will trigger updateMediaInfo()

        qInfo() << "[GStreamerPlayer] Reusing existing pipeline with new URI (async preroll)";
    } else {
        // First time - create pipeline
        setupPipeline(uri);
    }
#else
    Q_UNUSED(filePath);
    emit error("GStreamer support not available");
#endif
}

void GStreamerPlayer::setupPipeline(const QString& uri)
{
#ifdef HAVE_GSTREAMER
    // Create playbin - THE professional solution
    // playbin automatically handles:
    // - Codec detection and selection
    // - Hardware acceleration
    // - Audio/video sync
    // - Seeking and scrubbing
    // - Frame stepping
    m_pipeline = gst_element_factory_make("playbin", "playbin");
    if (!m_pipeline) {
        emit error("Failed to create playbin element");
        return;
    }

    // Set URI
    g_object_set(m_pipeline, "uri", uri.toUtf8().constData(), nullptr);

    // Set volume and mute state
    g_object_set(m_pipeline, "volume", m_volume.load(), nullptr);
    g_object_set(m_pipeline, "mute", m_muted.load() ? TRUE : FALSE, nullptr);

    // Configure video sink for direct widget rendering
    if (m_videoWidget) {
#ifdef Q_OS_WIN
        // On Windows, try D3D11 first (best performance), then D3D, then fallback
        GstElement* videoSink = gst_element_factory_make("d3d11videosink", "videosink");
        if (!videoSink) {
            qDebug() << "[GStreamerPlayer] d3d11videosink not available, trying d3dvideosink";
            videoSink = gst_element_factory_make("d3dvideosink", "videosink");
        }
        if (!videoSink) {
            qDebug() << "[GStreamerPlayer] d3dvideosink not available, using autovideosink";
            videoSink = gst_element_factory_make("autovideosink", "videosink");
        }
#else
        // On other platforms, use autovideosink
        GstElement* videoSink = gst_element_factory_make("autovideosink", "videosink");
#endif

        if (videoSink) {
            // Enable force-aspect-ratio to scale video to fit widget while maintaining aspect ratio
            // This ensures video fills the available space on all displays including HiDPI
            g_object_set(videoSink, "force-aspect-ratio", TRUE, nullptr);

            g_object_set(m_pipeline, "video-sink", videoSink, nullptr);
        } else {
            qWarning() << "[GStreamerPlayer] Failed to create video sink";
        }
    }

    // Get bus for messages
    m_bus = gst_element_get_bus(m_pipeline);

    // CRITICAL: Set sync handler for prepare-window-handle message
    // This must be done BEFORE setting pipeline state to ensure we catch the message
    gst_bus_set_sync_handler(m_bus,
        [](GstBus* bus, GstMessage* msg, gpointer user_data) -> GstBusSyncReply {
            GStreamerPlayer* player = static_cast<GStreamerPlayer*>(user_data);

            // Handle prepare-window-handle message synchronously
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ELEMENT &&
                gst_is_video_overlay_prepare_window_handle_message(msg)) {

                qInfo() << "[GStreamerPlayer] Sync handler: prepare-window-handle message";

                if (player->m_videoWidget) {
                    GstElement* sink = GST_ELEMENT(GST_MESSAGE_SRC(msg));
                    WId windowId = player->m_videoWidget->winId();

                    if (windowId && GST_IS_VIDEO_OVERLAY(sink)) {
#ifdef Q_OS_WIN
                        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sink), (guintptr)windowId);
#else
                        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sink), windowId);
#endif
                        qInfo() << "[GStreamerPlayer] Sync handler set window handle:" << windowId
                                << "on element:" << GST_ELEMENT_NAME(sink);

                        // Set render rectangle
                        gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(sink),
                                                                0, 0,
                                                                player->m_videoWidget->width(),
                                                                player->m_videoWidget->height());
                    }
                }

                // Drop the message, we've handled it
                gst_message_unref(msg);
                return GST_BUS_DROP;
            }

            // Pass all other messages to the async handler
            return GST_BUS_PASS;
        },
        this,
        nullptr);

    // Set to PAUSED to preroll and get media info (non-blocking)
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        emit error("Failed to set pipeline to PAUSED state");
        cleanup();
        return;
    }

    // Start bus message processing
    m_busTimer->start();

    // PERFORMANCE FIX: Do NOT wait for preroll here - it blocks the UI!
    // Instead, preroll happens asynchronously in the background.
    // When preroll completes, we'll get GST_MESSAGE_ASYNC_DONE on the bus.
    // The bus handler will call updateMediaInfo() when ready.

    // Set window handle for video overlay immediately
    if (m_videoWidget) {
        setWindowHandle();
    }

    m_playbackState = PlaybackState::Paused;
    emit playbackStateChanged(PlaybackState::Paused);

    qInfo() << "[GStreamerPlayer] Pipeline created, prerolling asynchronously...";
#endif
}

void GStreamerPlayer::setWindowHandle()
{
#ifdef HAVE_GSTREAMER
    if (!m_pipeline || !m_videoWidget) return;

    // Ensure widget has a native window handle
    WId windowId = m_videoWidget->winId();
    if (!windowId) {
        qWarning() << "[GStreamerPlayer] Widget does not have a valid window handle";
        return;
    }

    // Get the video sink from playbin
    GstElement* videoSink = nullptr;
    g_object_get(m_pipeline, "video-sink", &videoSink, nullptr);

    if (!videoSink) {
        qWarning() << "[GStreamerPlayer] No video sink found in pipeline";
        return;
    }

    // The video sink might be wrapped in a bin, so we need to find the actual overlay element
    GstElement* actualSink = videoSink;

    // If it's a bin, try to find the actual video sink inside
    if (GST_IS_BIN(videoSink)) {
        GstIterator* it = gst_bin_iterate_sinks(GST_BIN(videoSink));
        GValue item = G_VALUE_INIT;
        bool found = false;

        while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
            GstElement* element = GST_ELEMENT(g_value_get_object(&item));

            if (GST_IS_VIDEO_OVERLAY(element)) {
                actualSink = element;
                gst_object_ref(actualSink);
                found = true;
                g_value_reset(&item);
                break;
            }
            g_value_reset(&item);
        }

        g_value_unset(&item);
        gst_iterator_free(it);

        if (!found) {
            qWarning() << "[GStreamerPlayer] No video overlay element found in bin";
            gst_object_unref(videoSink);
            return;
        }
    }

    // Now set the window handle on the actual video overlay element
    if (GST_IS_VIDEO_OVERLAY(actualSink)) {
#ifdef Q_OS_WIN
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(actualSink), (guintptr)windowId);
#else
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(actualSink), windowId);
#endif

        qInfo() << "[GStreamerPlayer] Set window handle:" << windowId
                << "on element:" << GST_ELEMENT_NAME(actualSink);

        // Also set render rectangle to match widget size
        // CRITICAL: Use physical pixels (device pixel ratio) for HiDPI displays
        qreal dpr = m_videoWidget->devicePixelRatio();
        int physicalWidth = static_cast<int>(m_videoWidget->width() * dpr);
        int physicalHeight = static_cast<int>(m_videoWidget->height() * dpr);

        gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(actualSink),
                                                0, 0,
                                                physicalWidth,
                                                physicalHeight);
    } else {
        qWarning() << "[GStreamerPlayer] Video sink does not support overlay interface";
    }

    // Clean up references
    if (actualSink != videoSink) {
        gst_object_unref(actualSink);
    }
    gst_object_unref(videoSink);
#endif
}

void GStreamerPlayer::updateRenderRectangle()
{
#ifdef HAVE_GSTREAMER
    if (!m_pipeline || !m_videoWidget) return;

    // Get the video sink from playbin
    GstElement* videoSink = nullptr;
    g_object_get(m_pipeline, "video-sink", &videoSink, nullptr);

    if (!videoSink) {
        return;
    }

    // The video sink might be wrapped in a bin, so we need to find the actual overlay element
    GstElement* actualSink = videoSink;

    // If it's a bin, try to find the actual video sink inside
    if (GST_IS_BIN(videoSink)) {
        GstIterator* it = gst_bin_iterate_sinks(GST_BIN(videoSink));
        GValue item = G_VALUE_INIT;
        bool found = false;

        while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
            GstElement* element = GST_ELEMENT(g_value_get_object(&item));
            if (GST_IS_VIDEO_OVERLAY(element)) {
                actualSink = element;
                gst_object_ref(actualSink);
                found = true;
                g_value_reset(&item);
                break;
            }
            g_value_reset(&item);
        }

        g_value_unset(&item);
        gst_iterator_free(it);

        if (!found) {
            gst_object_unref(videoSink);
            return;
        }
    }

    // Update render rectangle to match current widget size
    // CRITICAL: Use physical pixels (device pixel ratio) for HiDPI displays
    if (GST_IS_VIDEO_OVERLAY(actualSink)) {
        qreal dpr = m_videoWidget->devicePixelRatio();
        int physicalWidth = static_cast<int>(m_videoWidget->width() * dpr);
        int physicalHeight = static_cast<int>(m_videoWidget->height() * dpr);

        gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(actualSink),
                                                0, 0,
                                                physicalWidth,
                                                physicalHeight);

        // Force expose to refresh the video rendering
        gst_video_overlay_expose(GST_VIDEO_OVERLAY(actualSink));
    }

    // Clean up references
    if (actualSink != videoSink) {
        gst_object_unref(actualSink);
    }
    gst_object_unref(videoSink);
#endif
}

void GStreamerPlayer::updateMediaInfo()
{
#ifdef HAVE_GSTREAMER
    if (!m_pipeline) return;

    // Query duration
    queryDuration();
    m_mediaInfo.durationMs = m_duration.load();

    // Get video info
    gint nVideo = 0;
    g_object_get(m_pipeline, "n-video", &nVideo, nullptr);

    if (nVideo > 0) {
        // Get video pad
        GstPad* pad = nullptr;
        g_signal_emit_by_name(m_pipeline, "get-video-pad", 0, &pad);

        if (pad) {
            GstCaps* caps = gst_pad_get_current_caps(pad);
            if (caps) {
                GstStructure* structure = gst_caps_get_structure(caps, 0);
                gst_structure_get_int(structure, "width", &m_mediaInfo.width);
                gst_structure_get_int(structure, "height", &m_mediaInfo.height);

                gint fpsNum = 0, fpsDen = 1;
                if (gst_structure_get_fraction(structure, "framerate", &fpsNum, &fpsDen) && fpsDen > 0) {
                    m_mediaInfo.fps = static_cast<double>(fpsNum) / fpsDen;
                }

                gst_caps_unref(caps);
            }
            gst_object_unref(pad);
        }
    }

    // Get audio info
    gint nAudio = 0;
    g_object_get(m_pipeline, "n-audio", &nAudio, nullptr);
    m_mediaInfo.hasAudio = (nAudio > 0);

    emit mediaInfoReady(m_mediaInfo);
    emit durationChanged(m_duration.load());
#endif
}

void GStreamerPlayer::play()
{
#ifdef HAVE_GSTREAMER
    QMutexLocker locker(&m_mutex);

    if (m_playbackState.load() == PlaybackState::Playing) {
        return;
    }

    if (m_pipeline) {
        GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            emit error("Failed to start playback");
            return;
        }

        m_playbackState = PlaybackState::Playing;
        emit playbackStateChanged(PlaybackState::Playing);
        m_positionTimer->start();

        qDebug() << "[GStreamerPlayer] Playback started";
    }
#endif
}

void GStreamerPlayer::pause()
{
#ifdef HAVE_GSTREAMER
    QMutexLocker locker(&m_mutex);

    if (m_playbackState.load() == PlaybackState::Stopped) {
        return;
    }

    if (m_pipeline) {
        GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            emit error("Failed to pause playback");
            return;
        }

        m_playbackState = PlaybackState::Paused;
        emit playbackStateChanged(PlaybackState::Paused);
        m_positionTimer->stop();

        qDebug() << "[GStreamerPlayer] Playback paused";
    }
#endif
}

void GStreamerPlayer::stop()
{
#ifdef HAVE_GSTREAMER
    QMutexLocker locker(&m_mutex);

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        m_playbackState = PlaybackState::Stopped;
        emit playbackStateChanged(PlaybackState::Stopped);
        m_positionTimer->stop();
        m_position = 0;
        emit positionChanged(0);

        qDebug() << "[GStreamerPlayer] Playback stopped";
    }
#endif
}

void GStreamerPlayer::seek(qint64 positionMs)
{
#ifdef HAVE_GSTREAMER
    if (!m_pipeline) return;

    gint64 position = positionMs * GST_MSECOND;

    // Use accurate seeking for smooth scrubbing
    if (gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME,
                               static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                               position)) {
        m_position = positionMs;
        emit positionChanged(positionMs);
        qDebug() << "[GStreamerPlayer] Seeked to:" << positionMs << "ms";
    } else {
        qWarning() << "[GStreamerPlayer] Seek failed to:" << positionMs << "ms";
    }
#else
    Q_UNUSED(positionMs);
#endif
}

void GStreamerPlayer::stepForward()
{
#ifdef HAVE_GSTREAMER
    if (!m_pipeline) return;

    // Use GStreamer's step event for frame-accurate stepping
    // This is THE professional way to do frame stepping
    if (m_mediaInfo.fps > 0) {
        qint64 frameDuration = static_cast<qint64>((1.0 / m_mediaInfo.fps) * GST_SECOND);

        GstEvent* stepEvent = gst_event_new_step(GST_FORMAT_BUFFERS, 1, 1.0, TRUE, FALSE);
        if (gst_element_send_event(m_pipeline, stepEvent)) {
            qDebug() << "[GStreamerPlayer] Step forward";
        } else {
            qWarning() << "[GStreamerPlayer] Step forward failed, using seek fallback";
            // Fallback to seeking
            seek(m_position.load() + static_cast<qint64>(1000.0 / m_mediaInfo.fps));
        }
    }
#endif
}

void GStreamerPlayer::stepBackward()
{
#ifdef HAVE_GSTREAMER
    if (!m_pipeline || m_mediaInfo.fps <= 0) return;

    // GStreamer doesn't have native backward stepping, so we seek backward by one frame
    qint64 frameDuration = static_cast<qint64>(1000.0 / m_mediaInfo.fps);
    qint64 currentPos = m_position.load();
    qint64 newPos = (currentPos > frameDuration) ? (currentPos - frameDuration) : 0;
    seek(newPos);

    qDebug() << "[GStreamerPlayer] Step backward to:" << newPos;
#endif
}

void GStreamerPlayer::setVolume(double volume)
{
#ifdef HAVE_GSTREAMER
    m_volume = std::clamp(volume, 0.0, 1.0);

    if (m_pipeline) {
        g_object_set(m_pipeline, "volume", m_volume.load(), nullptr);
    }
#else
    Q_UNUSED(volume);
#endif
}

void GStreamerPlayer::setMuted(bool muted)
{
#ifdef HAVE_GSTREAMER
    m_muted = muted;

    if (m_pipeline) {
        g_object_set(m_pipeline, "mute", muted ? TRUE : FALSE, nullptr);
    }
#else
    Q_UNUSED(muted);
#endif
}

double GStreamerPlayer::volume() const
{
    return m_volume.load();
}

bool GStreamerPlayer::isMuted() const
{
    return m_muted.load();
}

GStreamerPlayer::PlaybackState GStreamerPlayer::playbackState() const
{
    return m_playbackState.load();
}

GStreamerPlayer::PlaybackState GStreamerPlayer::state() const
{
    return playbackState();
}

qint64 GStreamerPlayer::position() const
{
    return m_position.load();
}

qint64 GStreamerPlayer::duration() const
{
    return m_duration.load();
}

GStreamerPlayer::MediaInfo GStreamerPlayer::mediaInfo() const
{
    QMutexLocker locker(&m_mutex);
    return m_mediaInfo;
}

void GStreamerPlayer::onBusMessage()
{
#ifdef HAVE_GSTREAMER
    if (!m_bus) return;

    GstMessage* msg;
    while ((msg = gst_bus_pop(m_bus)) != nullptr) {
        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR: {
                GError* err;
                gchar* debug_info;
                gst_message_parse_error(msg, &err, &debug_info);
                QString errorMsg = QString("GStreamer error: %1").arg(err->message);
                qWarning() << "[GStreamerPlayer]" << errorMsg;
                if (debug_info) {
                    qDebug() << "[GStreamerPlayer] Debug info:" << debug_info;
                }
                g_clear_error(&err);
                g_free(debug_info);
                emit error(errorMsg);
                break;
            }
            case GST_MESSAGE_EOS:
                qInfo() << "[GStreamerPlayer] End of stream";
                m_playbackState = PlaybackState::Paused;
                emit playbackStateChanged(PlaybackState::Paused);
                emit endOfStream();
                break;
            case GST_MESSAGE_STATE_CHANGED: {
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipeline)) {
                    GstState oldState, newState, pending;
                    gst_message_parse_state_changed(msg, &oldState, &newState, &pending);
                    qDebug() << "[GStreamerPlayer] State changed from"
                             << gst_element_state_get_name(oldState)
                             << "to" << gst_element_state_get_name(newState);
                }
                break;
            }
            case GST_MESSAGE_ASYNC_DONE:
                qDebug() << "[GStreamerPlayer] Async operation done (preroll/seek completed)";
                // Update media info now that preroll is complete
                updateMediaInfo();

                // CRITICAL: Update render rectangle after preroll completes
                // This ensures video is properly sized on HiDPI displays
                updateRenderRectangle();
                break;
            case GST_MESSAGE_BUFFERING: {
                gint percent = 0;
                gst_message_parse_buffering(msg, &percent);
                qDebug() << "[GStreamerPlayer] Buffering:" << percent << "%";
                break;
            }
            case GST_MESSAGE_ELEMENT: {
                // CRITICAL: Handle prepare-window-handle message
                // This is the PROPER way to set window handle in GStreamer
                // The video sink sends this message when it's ready to receive a window handle
                if (gst_is_video_overlay_prepare_window_handle_message(msg)) {
                    qInfo() << "[GStreamerPlayer] Received prepare-window-handle message";

                    if (m_videoWidget) {
                        // Get the video sink element from the message
                        GstElement* sink = GST_ELEMENT(GST_MESSAGE_SRC(msg));

                        // Ensure widget has a valid window handle
                        WId windowId = m_videoWidget->winId();
                        if (windowId && GST_IS_VIDEO_OVERLAY(sink)) {
#ifdef Q_OS_WIN
                            gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sink), (guintptr)windowId);
#else
                            gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sink), windowId);
#endif
                            qInfo() << "[GStreamerPlayer] Set window handle via prepare-window-handle:" << windowId
                                    << "on element:" << GST_ELEMENT_NAME(sink);

                            // Set render rectangle
                            gst_video_overlay_set_render_rectangle(GST_VIDEO_OVERLAY(sink),
                                                                    0, 0,
                                                                    m_videoWidget->width(),
                                                                    m_videoWidget->height());
                        } else {
                            qWarning() << "[GStreamerPlayer] Invalid window handle or sink doesn't support overlay";
                        }
                    }
                }
                break;
            }
            default:
                break;
        }
        gst_message_unref(msg);
    }
#endif
}

void GStreamerPlayer::onPositionUpdate()
{
#ifdef HAVE_GSTREAMER
    if (m_pipeline && m_playbackState.load() == PlaybackState::Playing) {
        queryPosition();
    }
#endif
}

bool GStreamerPlayer::queryPosition()
{
#ifdef HAVE_GSTREAMER
    if (!m_pipeline) return false;

    gint64 pos = 0;
    if (gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &pos)) {
        m_position = pos / GST_MSECOND;
        emit positionChanged(m_position.load());
        return true;
    }
#endif
    return false;
}

bool GStreamerPlayer::queryDuration()
{
#ifdef HAVE_GSTREAMER
    if (!m_pipeline) return false;

    gint64 dur = 0;
    if (gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &dur)) {
        m_duration = dur / GST_MSECOND;
        return true;
    }
#endif
    return false;
}

qint64 GStreamerPlayer::queryDuration(const QString& filePath)
{
#ifdef HAVE_GSTREAMER
    // CRITICAL: Initialize GStreamer if not already done
    initialize();

    if (!s_gstInitialized) {
        qWarning() << "[GStreamerPlayer] queryDuration: GStreamer not initialized";
        return 0;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qWarning() << "[GStreamerPlayer] queryDuration: File does not exist:" << filePath;
        return 0;
    }

    // Create a lightweight headless pipeline for duration query
    // Use uridecodebin + fakesink (no video windows)
    QString uri = QUrl::fromLocalFile(filePath).toString();

    GstElement* pipeline = gst_pipeline_new("duration-query-pipeline");
    GstElement* uridecodebin = gst_element_factory_make("uridecodebin", "uridecodebin");
    GstElement* fakesink = gst_element_factory_make("fakesink", "fakesink");

    if (!pipeline || !uridecodebin || !fakesink) {
        qWarning() << "[GStreamerPlayer] queryDuration: Failed to create pipeline elements";
        if (pipeline) gst_object_unref(pipeline);
        if (uridecodebin) gst_object_unref(uridecodebin);
        if (fakesink) gst_object_unref(fakesink);
        return 0;
    }

    g_object_set(uridecodebin, "uri", uri.toUtf8().constData(), nullptr);

    gst_bin_add_many(GST_BIN(pipeline), uridecodebin, fakesink, nullptr);

    // Connect pad-added signal to link uridecodebin to fakesink dynamically
    auto padAddedCallback = +[](GstElement* /*src*/, GstPad* newPad, gpointer data) {
        GstElement* sink = static_cast<GstElement*>(data);
        GstPad* sinkPad = gst_element_get_static_pad(sink, "sink");

        if (!gst_pad_is_linked(sinkPad)) {
            gst_pad_link(newPad, sinkPad);
        }

        gst_object_unref(sinkPad);
    };

    g_signal_connect(uridecodebin, "pad-added", G_CALLBACK(padAddedCallback), fakesink);

    // Set to PAUSED to preroll (enough to query duration)
    GstStateChangeReturn stateRet = gst_element_set_state(pipeline, GST_STATE_PAUSED);
    if (stateRet == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "[GStreamerPlayer] queryDuration: Failed to set pipeline to PAUSED";
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 0;
    }

    // Wait for ASYNC_DONE message
    GstBus* bus = gst_element_get_bus(pipeline);
    bool prerollComplete = false;
    GstClockTime timeout = 5 * GST_SECOND;
    GstClockTime startTime = gst_util_get_timestamp();

    while (!prerollComplete && (gst_util_get_timestamp() - startTime) < timeout) {
        GstMessage* msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR));

        if (msg) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ASYNC_DONE) {
                prerollComplete = true;
            } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError* err = nullptr;
                gst_message_parse_error(msg, &err, nullptr);
                qWarning() << "[GStreamerPlayer] queryDuration: Error:" << (err ? err->message : "Unknown");
                if (err) g_error_free(err);
            }
            gst_message_unref(msg);
        }
    }

    gst_object_unref(bus);

    if (!prerollComplete) {
        qWarning() << "[GStreamerPlayer] queryDuration: Preroll timeout for" << filePath;
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 0;
    }

    // Query duration
    gint64 duration = 0;
    if (!gst_element_query_duration(pipeline, GST_FORMAT_TIME, &duration)) {
        qWarning() << "[GStreamerPlayer] queryDuration: Failed to query duration for" << filePath;
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return 0;
    }

    // Clean up
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    qint64 durationMs = duration / GST_MSECOND;
    qDebug() << "[GStreamerPlayer] queryDuration: Duration for" << filePath << ":" << durationMs << "ms";

    return durationMs;
#else
    Q_UNUSED(filePath);
    return 0;
#endif
}

QImage GStreamerPlayer::extractThumbnail(const QString& filePath, const QSize& targetSize, qint64 positionMs)
{
#ifdef HAVE_GSTREAMER
    // CRITICAL: Initialize GStreamer if not already done
    // This is required before creating any GStreamer elements
    initialize();

    if (!s_gstInitialized) {
        qWarning() << "[GStreamerPlayer] extractThumbnail: GStreamer not initialized";
        return QImage();
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        qWarning() << "[GStreamerPlayer] extractThumbnail: File does not exist:" << filePath;
        return QImage();
    }

    // Create a temporary pipeline for thumbnail extraction
    QString uri = QUrl::fromLocalFile(filePath).toString();
    GstElement* pipeline = gst_element_factory_make("playbin", "thumb-playbin");
    if (!pipeline) {
        qWarning() << "[GStreamerPlayer] extractThumbnail: Failed to create playbin for" << filePath;
        return QImage();
    }

    g_object_set(pipeline, "uri", uri.toUtf8().constData(), nullptr);

    // Create appsink for video frames
    GstElement* videoSink = gst_element_factory_make("appsink", "thumb-videosink");
    if (!videoSink) {
        qWarning() << "[GStreamerPlayer] extractThumbnail: Failed to create appsink for" << filePath;
        gst_object_unref(pipeline);
        return QImage();
    }

    g_object_set(videoSink,
                 "emit-signals", FALSE,
                 "drop", TRUE,
                 "max-buffers", 1,
                 nullptr);

    // Set caps to receive RGB frames
    GstCaps* caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "RGB",
                                        nullptr);
    gst_app_sink_set_caps(GST_APP_SINK(videoSink), caps);
    gst_caps_unref(caps);

    g_object_set(pipeline, "video-sink", videoSink, nullptr);

    // Set to PAUSED to preroll
    GstStateChangeReturn stateRet = gst_element_set_state(pipeline, GST_STATE_PAUSED);
    if (stateRet == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "[GStreamerPlayer] extractThumbnail: Failed to set pipeline to PAUSED for" << filePath;
        gst_object_unref(pipeline);
        return QImage();
    }

    // CRITICAL: Wait for ASYNC_DONE message to ensure preroll is complete
    // This is required before pulling samples from appsink
    GstBus* bus = gst_element_get_bus(pipeline);
    bool prerollComplete = false;
    GstClockTime timeout = 2 * GST_SECOND;
    GstClockTime startTime = gst_util_get_timestamp();

    while (!prerollComplete && (gst_util_get_timestamp() - startTime) < timeout) {
        GstMessage* msg = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR | GST_MESSAGE_EOS));

        if (msg) {
            switch (GST_MESSAGE_TYPE(msg)) {
                case GST_MESSAGE_ASYNC_DONE:
                    prerollComplete = true;
                    break;
                case GST_MESSAGE_ERROR: {
                    GError* err = nullptr;
                    gchar* debug = nullptr;
                    gst_message_parse_error(msg, &err, &debug);
                    qWarning() << "[GStreamerPlayer] extractThumbnail: Pipeline error for" << filePath
                               << ":" << (err ? err->message : "Unknown");
                    if (err) g_error_free(err);
                    if (debug) g_free(debug);
                    gst_message_unref(msg);
                    gst_object_unref(bus);
                    gst_element_set_state(pipeline, GST_STATE_NULL);
                    gst_object_unref(pipeline);
                    return QImage();
                }
                case GST_MESSAGE_EOS:
                    qWarning() << "[GStreamerPlayer] extractThumbnail: Unexpected EOS for" << filePath;
                    prerollComplete = true;
                    break;
                default:
                    break;
            }
            gst_message_unref(msg);
        }
    }

    gst_object_unref(bus);

    if (!prerollComplete) {
        qWarning() << "[GStreamerPlayer] extractThumbnail: Preroll timeout for" << filePath;
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return QImage();
    }

    QImage thumbnail;

    // Seek to position if specified
    if (positionMs > 0) {
        gint64 position = positionMs * GST_MSECOND;
        // Use ACCURATE seeking for smooth scrubbing (not KEY_UNIT which jumps to keyframes)
        if (!gst_element_seek_simple(pipeline, GST_FORMAT_TIME,
                               static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE),
                               position)) {
            qWarning() << "[GStreamerPlayer] extractThumbnail: Seek failed for" << filePath;
            gst_element_set_state(pipeline, GST_STATE_NULL);
            gst_object_unref(pipeline);
            return QImage();
        }

        // Wait for seek to complete
        GstBus* seekBus = gst_element_get_bus(pipeline);
        GstMessage* msg = gst_bus_timed_pop_filtered(seekBus, 1 * GST_SECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_ERROR));
        if (msg) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                qWarning() << "[GStreamerPlayer] extractThumbnail: Seek error for" << filePath;
                gst_message_unref(msg);
                gst_object_unref(seekBus);
                gst_element_set_state(pipeline, GST_STATE_NULL);
                gst_object_unref(pipeline);
                return QImage();
            }
            gst_message_unref(msg);
        }
        gst_object_unref(seekBus);
    }

    // CRITICAL: After seeking in PAUSED state, we need to briefly set to PLAYING
    // to push the frame through the pipeline to appsink
    stateRet = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (stateRet == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "[GStreamerPlayer] extractThumbnail: Failed to set pipeline to PLAYING for" << filePath;
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return QImage();
    }

    // Wait a bit for the frame to be pushed to appsink
    // This is necessary because PAUSED state only prerolls but doesn't push data through
    g_usleep(50000); // 50ms should be enough

    // Now pull the sample from appsink
    GstSample* sample = gst_app_sink_try_pull_sample(GST_APP_SINK(videoSink), 200 * GST_MSECOND);
    if (sample) {
        GstCaps* sampleCaps = gst_sample_get_caps(sample);
        GstBuffer* buffer = gst_sample_get_buffer(sample);

        if (sampleCaps && buffer) {
            GstStructure* structure = gst_caps_get_structure(sampleCaps, 0);
            int width = 0, height = 0;
            gst_structure_get_int(structure, "width", &width);
            gst_structure_get_int(structure, "height", &height);

            if (width > 0 && height > 0) {
                GstMapInfo map;
                if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                    QImage image(map.data, width, height, width * 3, QImage::Format_RGB888);
                    thumbnail = image.copy();
                    gst_buffer_unmap(buffer, &map);

                    // Scale to target size if needed
                    if (targetSize.isValid()) {
                        thumbnail = thumbnail.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    }

                    qDebug() << "[GStreamerPlayer] extractThumbnail: Success for" << filePath
                             << "size:" << width << "x" << height;
                } else {
                    qWarning() << "[GStreamerPlayer] extractThumbnail: Failed to map buffer for" << filePath;
                }
            } else {
                qWarning() << "[GStreamerPlayer] extractThumbnail: Invalid dimensions for" << filePath;
            }
        } else {
            qWarning() << "[GStreamerPlayer] extractThumbnail: Invalid sample caps or buffer for" << filePath;
        }
        gst_sample_unref(sample);
    } else {
        qWarning() << "[GStreamerPlayer] extractThumbnail: Failed to pull sample for" << filePath;
    }

    // Cleanup
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);

    return thumbnail;
#else
    Q_UNUSED(filePath);
    Q_UNUSED(targetSize);
    Q_UNUSED(positionMs);
    return QImage();
#endif
}


