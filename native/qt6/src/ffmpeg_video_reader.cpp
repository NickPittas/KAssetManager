#include "ffmpeg_video_reader.h"
#include <QByteArray>
#include <QThread>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavutil/avutil.h>
}
#endif

void FfmpegVideoReader::start()
{
#ifdef HAVE_FFMPEG
    if (!open()) { emit finished(); return; }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgba = av_frame_alloc();
    if (!pkt || !frame || !rgba) { cleanup(pkt, frame, rgba); emit finished(); return; }

    const int outW = m_ctx->width;
    const int outH = m_ctx->height;
    QByteArray buf;
    buf.resize(av_image_get_buffer_size(AV_PIX_FMT_RGBA, outW, outH, 1));
    av_image_fill_arrays(rgba->data, rgba->linesize, reinterpret_cast<uint8_t*>(buf.data()), AV_PIX_FMT_RGBA, outW, outH, 1);

    qint64 lastPtsMs = 0;
    AVRational ms = {1, 1000};
    const double intervalMs = m_fps > 0.0 ? (1000.0 / m_fps) : (1000.0 / 24.0);
    bool clockStarted = false;
    qint64 basePtsMs = 0;
    QElapsedTimer playbackClock;

    while (!m_stop) {
        if (m_seekRequested.load()) {
            qint64 targetMs = m_seekTargetMs.load();
            int64_t ts = av_rescale_q(targetMs, ms, m_stream->time_base);
            av_seek_frame(m_fmt, m_vIdx, ts, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(m_ctx);
            m_seekRequested = false;
            lastPtsMs = targetMs;
        }

        bool doSingleStep = m_singleStep.exchange(false);

        // Paused behaviour
        if (m_paused) {
            if (doSingleStep) {
                // Decode until we produce one video frame, then remain paused
                bool gotFrame = false;
                int safety = 0;
                while (!m_stop && !gotFrame && safety < 200) {
                    int r = av_read_frame(m_fmt, pkt);
                    if (r >= 0) {
                        if (pkt->stream_index == m_vIdx) {
                            avcodec_send_packet(m_ctx, pkt);
                        }
                    } else {
                        // Flush decoder at EOF
                        avcodec_send_packet(m_ctx, nullptr);
                    }
                    av_packet_unref(pkt);

                    while (!m_stop) {
                        int ret = avcodec_receive_frame(m_ctx, frame);
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                        if (ret < 0) break;
                        if (!m_sws) {
                            m_sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                                                    outW, outH, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                        }
                        sws_scale(m_sws, frame->data, frame->linesize, 0, frame->height, rgba->data, rgba->linesize);
                        QImage img(rgba->data[0], outW, outH, rgba->linesize[0], QImage::Format_RGBA8888);
                        QImage copy = img.copy(); // detach from buffer
                        qint64 ptsMs = (frame->pts != AV_NOPTS_VALUE) ? av_rescale_q(frame->pts, m_stream->time_base, ms)
                                                                      : (lastPtsMs + static_cast<qint64>(intervalMs));
                        lastPtsMs = ptsMs;
                        emit frameReady(copy, ptsMs);
                        gotFrame = true;
                        break; // stop after first frame in step mode
                    }
                    ++safety;
                }
                QThread::msleep(1);
                continue; // stay paused
            } else {
                QThread::msleep(10);
                continue;
            }
        }

        // Normal playback
        int r = av_read_frame(m_fmt, pkt);
        if (r >= 0 && pkt->stream_index == m_vIdx) {
            avcodec_send_packet(m_ctx, pkt);
        } else if (r < 0) {
            // Flush decoder at EOF
            avcodec_send_packet(m_ctx, nullptr);
        }
        av_packet_unref(pkt);

        while (!m_stop) {
            int ret = avcodec_receive_frame(m_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
            if (ret < 0) break;
            // Compute PTS (ms) from stream time_base; fall back to constant-step if missing
            qint64 ptsMs = (frame->pts != AV_NOPTS_VALUE) ? av_rescale_q(frame->pts, m_stream->time_base, ms)
                                                          : (lastPtsMs + static_cast<qint64>(intervalMs));
            lastPtsMs = ptsMs;

            // Start playback clock on first frame, using this frame's PTS as zero
            if (!clockStarted) {
                basePtsMs = ptsMs;
                playbackClock.start();
                clockStarted = true;
            }

            // Exact fps pacing against the file's timestamps (no guessing)
            if (!m_paused) {
                const qint64 targetMs = ptsMs - basePtsMs;
                const qint64 nowMs = playbackClock.elapsed();
                const qint64 waitMs = targetMs - nowMs;

                if (waitMs > 1) {
                    QThread::msleep(static_cast<unsigned long>(waitMs));
                } else if (m_dropLateFrames && waitMs < -static_cast<qint64>(intervalMs / 2)) {
                    // We're late: drop this frame before conversion to preserve realtime
                    av_frame_unref(frame);
                    continue;
                }
            }

            if (!m_sws) {
                m_sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                                        outW, outH, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            }
            sws_scale(m_sws, frame->data, frame->linesize, 0, frame->height, rgba->data, rgba->linesize);
            QImage img(rgba->data[0], outW, outH, rgba->linesize[0], QImage::Format_RGBA8888);
            QImage copy = img.copy(); // detach from buffer
            emit frameReady(copy, ptsMs);
        }
    }

    cleanup(pkt, frame, rgba);
    emit finished();
#else
    emit finished();
#endif
}

#ifdef HAVE_FFMPEG
bool FfmpegVideoReader::open()
{
    if (avformat_open_input(&m_fmt, m_path.toUtf8().constData(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(m_fmt, nullptr) < 0) { avformat_close_input(&m_fmt); return false; }
    int vIdx = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vIdx < 0) { avformat_close_input(&m_fmt); return false; }
    m_vIdx = vIdx; m_stream = m_fmt->streams[m_vIdx];
    const AVCodec* dec = avcodec_find_decoder(m_stream->codecpar->codec_id);
    if (!dec) { avformat_close_input(&m_fmt); return false; }
    m_ctx = avcodec_alloc_context3(dec);
    if (!m_ctx) { avformat_close_input(&m_fmt); return false; }
    if (avcodec_parameters_to_context(m_ctx, m_stream->codecpar) < 0) { avcodec_free_context(&m_ctx); avformat_close_input(&m_fmt); return false; }
    // Enable multi-threaded decoding for performance
    m_ctx->thread_count = 0; // auto
    m_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (avcodec_open2(m_ctx, dec, nullptr) < 0) { avcodec_free_context(&m_ctx); avformat_close_input(&m_fmt); return false; }
    AVRational r = m_stream->avg_frame_rate.num > 0 ? m_stream->avg_frame_rate : m_stream->r_frame_rate;
    m_fps = (r.num > 0 && r.den > 0) ? (static_cast<double>(r.num) / r.den) : 24.0;
    if (m_fmt->duration > 0) m_durationMs = static_cast<qint64>((m_fmt->duration * 1000) / AV_TIME_BASE); else m_durationMs = 0;
    return true;
}
#endif

