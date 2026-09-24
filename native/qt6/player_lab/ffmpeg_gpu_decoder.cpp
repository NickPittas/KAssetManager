#include "ffmpeg_gpu_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace player_lab {

// Cap decoded RGBA output so oversized frames still have bounded queue memory,
// but keep 4K-class media near display resolution for the raster UI. A 1920
// cap made the real full-screen viewer upscale every frame in QPainter, which
// is slower than FFmpeg's swscale conversion and produced ~9 fps on MOV/MKV.
// The short edge follows source aspect ratio; frames at/below cap stay native.
constexpr int kMaxDecodedOutputLongEdge = 3840;

// Compute aspect-preserving output dimensions, clamped so the long edge does
// not exceed kMaxDecodedOutputLongEdge. Dimensions are rounded to even,
// positive integers (>= 2) — libswscale prefers even sizes and some codecs
// require them for chroma formats.
static void computeCappedOutputSize(int srcW, int srcH, int &outW, int &outH) {
    if (srcW <= 0 || srcH <= 0) {
        outW = srcW;
        outH = srcH;
        return;
    }
    const int longEdge = (srcW >= srcH) ? srcW : srcH;
    if (longEdge <= kMaxDecodedOutputLongEdge) {
        outW = srcW;
        outH = srcH;
        return;
    }
    const double scale =
        static_cast<double>(kMaxDecodedOutputLongEdge) / longEdge;
    int w = static_cast<int>(srcW * scale + 0.5);
    int h = static_cast<int>(srcH * scale + 0.5);
    // Enforce even, >= 2.
    if (w < 2) w = 2;
    if (h < 2) h = 2;
    if (w & 1) w -= 1;
    if (h & 1) h -= 1;
    outW = w;
    outH = h;
}

namespace {

// Preferred hardware device types for Linux decode, in priority order.
// NVIDIA/CUDA is tried first (this workstation's RTX 4090 exposes the cuda
// hwaccel plus h264_cuvid/hevc_cuvid-style hw configs for many codecs);
// VAAPI is the fallback. The codec must expose a matching AVCodecHWConfig
// with AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX for a given device type for
// decode to actually go hardware — the first device type whose config is
// present AND whose device context can be created wins.
#if defined(__linux__)
constexpr AVHWDeviceType kPreferredHwDeviceTypes[] = {
    AV_HWDEVICE_TYPE_CUDA,
    AV_HWDEVICE_TYPE_VAAPI,
};
#endif

// FFmpeg get_format callback for hardware decode. Selects the pixel format
// advertised by the chosen AVCodecHWConfig (the decoder's m_hwPixelFormat,
// reached through the opaque pointer set in open()) when the decoder offers
// it; otherwise falls back to the first available format. This mirrors
// FFmpegMovPlayer::selectHardwarePixelFormat exactly.
AVPixelFormat selectHardwarePixelFormat(
    AVCodecContext *codecContext, const AVPixelFormat *pixelFormats) {
    const auto *self = static_cast<const FFmpegGpuDecoder *>(
        codecContext ? codecContext->opaque : nullptr);
    const AVPixelFormat preferred = self
        ? static_cast<AVPixelFormat>(self->hwPixelFormatForCallback())
        : AV_PIX_FMT_NONE;

    if (pixelFormats) {
        for (const AVPixelFormat *format = pixelFormats;
             *format != AV_PIX_FMT_NONE; ++format) {
            if (*format == preferred) {
                return *format;
            }
        }
        return pixelFormats[0];
    }
    return AV_PIX_FMT_NONE;
}

// Classify a (container-format-name, video-codec-id) pair into the single
// documented PlayerLab playback path. Container names come from
// AVInputFormat::name (a comma-separated alias list, e.g. "matroska,webm");
// the checks below tolerate either alias appearing. Returns one of the
// FFmpegGpuDecoder::SelectedPlaybackPath values so open() can accept the
// known paths and reject MXF / everything else before decoder allocation.
//
// The lookup of these container/codec pairs is the only accept gate for
// open(); anything not listed below is treated as PathUnsupported (or
// PathMxfUnsupported for MXF), and open() fails with a descriptive
// m_lastError rather than opening a decoder for it.
bool containerNameHasToken(const char *name, const char *token) {
    if (!name || !token) {
        return false;
    }
    const size_t tokenLen = std::strlen(token);
    const char *p = name;
    while (*p) {
        const char *comma = std::strchr(p, ',');
        const size_t len = comma ? static_cast<size_t>(comma - p) : std::strlen(p);
        if (len == tokenLen && std::strncmp(p, token, tokenLen) == 0) {
            return true;
        }
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
    return false;
}


void configureDecoderThreading(AVCodecContext *ctx) {
    if (!ctx) {
        return;
    }
    // Match FFmpeg's documented AVCodecContext threading controls instead of
    // leaving high-resolution software codecs effectively single-threaded in
    // the app path. thread_count=0 asks libavcodec to choose the worker count;
    // enabling both frame and slice threading lets codecs use whichever mode
    // they support.
    ctx->thread_count = 0;
    ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
}
 
 } // namespace


FFmpegGpuDecoder::~FFmpegGpuDecoder() {
    stop();
    cleanup();
}
FFmpegGpuDecoder::FFmpegGpuDecoder() = default;
bool FFmpegGpuDecoder::tryOpenHardware(const AVCodec *codec) {
    // Enumerate the codec's AVCodecHWConfig entries for each preferred
    // device type in priority order (CUDA first, then VAAPI on this Linux
    // workstation). For the first device type that exposes a
    // HW_DEVICE_CTX config AND whose device context can be created, wire
    // it into m_codecCtx. On any per-attempt failure, clean only that
    // attempt and try the next device type; if all fail, revert and return
    // false so the caller falls back to a clean software open.
#if defined(__linux__)
    if (!codec || !m_codecCtx) {
        return false;
    }

    for (const AVHWDeviceType deviceType : kPreferredHwDeviceTypes) {
        // Find a hw config for this codec/device pair using the
        // HW_DEVICE_CTX method (the path that lets us hand a pre-created
        // device to avcodec_open2).
        int hwPixelFormat = AV_PIX_FMT_NONE;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig *hwConfig = avcodec_get_hw_config(codec, i);
            if (!hwConfig) {
                break;
            }
            if ((hwConfig->methods &
                 AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                hwConfig->device_type == deviceType) {
                hwPixelFormat = hwConfig->pix_fmt;
                break;
            }
        }
        if (hwPixelFormat == AV_PIX_FMT_NONE) {
            std::fprintf(stderr,
                         "[FFmpegGpuDecoder] hw config missing: codec=%s device=%s\n",
                         codec->name ? codec->name : "unknown",
                         av_hwdevice_get_type_name(deviceType));
            // No hardware config for this codec/device — try next device.
            continue;
        }

        // Attempt to create the device context for this device type.
        // VAAPI init prints a stderr warning on failure (e.g. "[VAAPI]
        // Failed to initialise VAAPI connection"); CUDA failing on a
        // headless/AMD box is quiet. We try CUDA first so that on this
        // NVIDIA workstation the VAAPI attempt is never reached.
        AVBufferRef *hwDeviceCtx = nullptr;
        const int ret = av_hwdevice_ctx_create(&hwDeviceCtx, deviceType,
                                               nullptr, nullptr, 0);
        if (ret < 0 || !hwDeviceCtx) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::fprintf(stderr,
                         "[FFmpegGpuDecoder] hw device create failed: codec=%s device=%s error=%s\n",
                         codec->name ? codec->name : "unknown",
                         av_hwdevice_get_type_name(deviceType), errbuf);
            // Device init failed for this type — clean and try the next.
            continue;
        }

        // Wire the device into the codec context. av_buffer_ref transfers a
        // new reference to m_codecCtx; the original hwDeviceCtx ref is kept
        // on m_hwDeviceCtx so cleanup() can release it.
        m_codecCtx->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
        if (!m_codecCtx->hw_device_ctx) {
            std::fprintf(stderr,
                         "[FFmpegGpuDecoder] hw device ref failed: codec=%s device=%s\n",
                         codec->name ? codec->name : "unknown",
                         av_hwdevice_get_type_name(deviceType));
            av_buffer_unref(&hwDeviceCtx);
            continue;
        }
        m_codecCtx->opaque = this;
        m_codecCtx->get_format = &selectHardwarePixelFormat;
        m_hwDeviceCtx = hwDeviceCtx;
        m_hwPixelFormat = hwPixelFormat;
        m_hardwareDecodingActive = true;
        m_selectedHwDeviceType = static_cast<int>(deviceType);
        return true;
    }

    // No device type succeeded — not an error, software decode will be used.
    return false;
#else
    (void)codec;
    return false;
#endif
}

void FFmpegGpuDecoder::resetHardwareState() {
    // Drop any reference the codec context still holds, then free the owned
    // device context. m_codecCtx itself is managed separately by the caller
    // (it may be freed/rebuilt around this call).
    if (m_codecCtx && m_codecCtx->hw_device_ctx) {
        AVBufferRef *ref = m_codecCtx->hw_device_ctx;
        m_codecCtx->hw_device_ctx = nullptr;
        av_buffer_unref(&ref);
    }
    m_hwPixelFormat = -1;
    m_hardwareDecodingActive = false;
    m_selectedHwDeviceType = -1;
}

void FFmpegGpuDecoder::setRequireHardware(bool on) {
    m_requireHardware = on;
}

bool FFmpegGpuDecoder::requireHardware() const {
    return m_requireHardware;
}

int FFmpegGpuDecoder::selectedHwDeviceType() const {
    return m_selectedHwDeviceType;
}

const char *FFmpegGpuDecoder::selectedHwDeviceName() const {
    if (m_selectedHwDeviceType >= 0) {
        const char *name = av_hwdevice_get_type_name(
            static_cast<AVHWDeviceType>(m_selectedHwDeviceType));
        if (name) {
            return name;
        }
    }
    return "software";
}

const char *FFmpegGpuDecoder::selectedPlaybackPathName() const {
    switch (m_selectedPath) {
    case PathMp4H264:        return "mp4-h264";
    case PathMkvH264:        return "mkv-h264";
    case PathMovProRes:      return "mov-prores";
    case PathWebmVp8:        return "webm-vp8";
    case PathMxfUnsupported: return "mxf-unsupported";
    case PathUnsupported:    return "unsupported";
    case PathUnknown:
    default:                 break;
    }
    return "unknown";
}

void FFmpegGpuDecoder::cleanup() {
    // Free hardware decode state first: the reference held on
    // m_codecCtx->hw_device_ctx and the owned m_hwDeviceCtx. Must happen
    // before avcodec_free_context() so the owned ref is dropped cleanly.
    resetHardwareState();
    if (m_swrCtx) {
        swr_free(&m_swrCtx);
    }
    if (m_audioCodecCtx) {
        avcodec_free_context(&m_audioCodecCtx);
    }
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
        m_swsSrcW = m_swsSrcH = m_swsOutW = m_swsOutH = 0;
        m_swsSrcFormat = -1;
    }
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
    }
    if (m_fmtCtx) {
        avformat_close_input(&m_fmtCtx);
    }
    m_open = false;
    m_selectedPath = PathUnknown;
}

bool FFmpegGpuDecoder::open(const std::string &path) {
    m_lastError.clear();

    // Every load request must pass the FFmpeg-decoder-availability gate. If
    // a previous file is still open, tear it down first so the validation
    // below is never skipped via the old early-open return. stop() halts
    // the decode thread; cleanup() frees FFmpeg state and clears m_open.
    stop();
    if (m_open) {
        cleanup();
    }
    // Unconditionally clear stale EOF state. stop() above clears it when the
    // decode thread was running, but stop() early-returns (without touching
    // m_eof) if the thread was already stopped — e.g. a second consecutive
    // load(). open() is the canonical "new media session" boundary, so we
    // guarantee m_eof is false before any gating/validation runs. Otherwise a
    // prior clip's EOF would leave atEnd() true and the next play() would
    // refuse to start even though open() succeeded.
    m_eof.store(false, std::memory_order_release);

    if (avformat_open_input(&m_fmtCtx, path.c_str(), nullptr, nullptr) < 0) {
        m_fmtCtx = nullptr;
        m_lastError = "Could not open file (FFmpeg avformat_open_input failed).";
        return false;
    }
    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        m_lastError = "Could not read stream info (invalid or corrupt file).";
        cleanup();
        return false;
    }

    // --- Path classification (before any decoder is opened). ---
    // Use the documented av_find_best_stream() to select the best video and
    // (optionally) audio streams, then classify the file into a single
    // selected playback path from the container format name and the video
    // codec id. Only the documented container/codec combinations are
    // accepted; MXF and anything unrecognized fail here, before decoder
    // allocation, with a descriptive m_lastError.
    m_streamIndex = -1;
    m_audioStreamIndex = -1;
    const int videoIdx = av_find_best_stream(
        m_fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIdx < 0) {
        m_lastError = "No video stream found in file.";
        cleanup();
        return false;
    }
    m_streamIndex = videoIdx;
    // Audio is optional: missing audio is a valid (audio-less) file.
    const int audioIdx = av_find_best_stream(
        m_fmtCtx, AVMEDIA_TYPE_AUDIO, -1, m_streamIndex, nullptr, 0);
    if (audioIdx >= 0) {
        m_audioStreamIndex = audioIdx;
    }

    AVStream *stream = m_fmtCtx->streams[m_streamIndex];
    AVCodecParameters *par = stream->codecpar;
    const char *containerName =
        m_fmtCtx->iformat ? m_fmtCtx->iformat->name : "";
    const AVCodecID videoCodecId = par->codec_id;

    // Classify into exactly one documented path. Container aliases:
    //   mov,mp4,m4a,3gp,3g2,mj2  -> MP4/MOV family
    //   matroska,webm            -> Matroska/WebM family
    //   mxf                      -> MXF (rejected)
    const bool isMp4Mov = containerNameHasToken(containerName, "mov") ||
                          containerNameHasToken(containerName, "mp4") ||
                          containerNameHasToken(containerName, "m4a") ||
                          containerNameHasToken(containerName, "3gp") ||
                          containerNameHasToken(containerName, "3g2") ||
                          containerNameHasToken(containerName, "mj2");
    const bool isMatroskaWebm =
        containerNameHasToken(containerName, "matroska") ||
        containerNameHasToken(containerName, "webm");
    const bool isMxf = containerNameHasToken(containerName, "mxf");

    if (isMxf) {
        // MXF is rejected outright for this slice; no decoder is opened.
        // Re-arm the classification after cleanup() (which resets it to
        // PathUnknown for normal teardown) so the failure is observable via
        // selectedPlaybackPathName() after open() returns false.
        m_lastError = "MXF unsupported";
        cleanup();
        m_selectedPath = PathMxfUnsupported;
        return false;
    }
    if (isMp4Mov && videoCodecId == AV_CODEC_ID_H264) {
        m_selectedPath = PathMp4H264;
    } else if (isMatroskaWebm && videoCodecId == AV_CODEC_ID_H264) {
        m_selectedPath = PathMkvH264;
    } else if (isMp4Mov && videoCodecId == AV_CODEC_ID_PRORES) {
        m_selectedPath = PathMovProRes;
    } else if (isMatroskaWebm && videoCodecId == AV_CODEC_ID_VP8) {
        m_selectedPath = PathWebmVp8;
    } else {
        m_lastError = "Unsupported playback path: container='";
        m_lastError += containerName ? containerName : "";
        m_lastError += "' codec_id=";
        m_lastError += std::to_string(static_cast<int>(videoCodecId));
        cleanup();
        // Re-arm the classification after cleanup() (which resets it to
        // PathUnknown for normal teardown) so the unsupported failure is
        // observable via selectedPlaybackPathName().
        m_selectedPath = PathUnsupported;
        return false;
    }

    // H.264 app playback must use the hardware decoder when available. The
    // generic "h264" decoder on this FFmpeg build exposes no CUDA hw config,
    // so avcodec_find_decoder(AV_CODEC_ID_H264) silently forces the real app
    // into 4K software decode (~8-11 fps). Prefer the CUDA decoder by name for
    // documented H.264 paths; keep the generic decoder as the software fallback.
    const bool h264Path =
        m_selectedPath == PathMp4H264 || m_selectedPath == PathMkvH264;
    const AVCodec *softwareCodec = avcodec_find_decoder(videoCodecId);
    const AVCodec *codec = h264Path
        ? avcodec_find_decoder_by_name("h264_cuvid")
        : softwareCodec;
    if (!codec) {
        codec = softwareCodec;
    }
    if (!codec) {
        m_lastError = "No FFmpeg decoder available for video codec "
            "(got codec_id=";
        m_lastError += std::to_string(static_cast<int>(videoCodecId));
        m_lastError += ").";
        cleanup();
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        m_lastError = "Failed to allocate decoder context.";
        cleanup();
        return false;
    }
    if (avcodec_parameters_to_context(m_codecCtx, par) < 0) {
        m_lastError = "Failed to copy stream parameters to decoder context.";
        cleanup();
        return false;
    }
    m_codecCtx->pkt_timebase = stream->time_base;
    configureDecoderThreading(m_codecCtx);

    // Attempt hardware decode first on Linux (CUDA preferred on this NVIDIA
    // workstation, then VAAPI). If the codec has no matching AVCodecHWConfig,
    // or device creation / avcodec_open2 fails, default behavior tears down
    // ONLY the hardware state and reopens the decoder in pure software mode.
    //
    // Validation mode (m_requireHardware): when the hardware path does not
    // fully succeed, open() must NOT fall back to software. It sets a clear
    // m_lastError, cleans up, and returns false so a test can prove CUDA
    // decode either works or fails visibly — no silent CPU fallback.
    bool opened = false;
    const bool hwTried = h264Path && tryOpenHardware(codec);
    if (hwTried) {
        const int openRet = avcodec_open2(m_codecCtx, codec, nullptr);
        if (openRet == 0) {
            opened = true;
            m_hardwareDecodingActive = true;
            std::fprintf(stderr, "[FFmpegGpuDecoder] open OK: decode=%s hwActive=1\n",
                         selectedHwDeviceName());
        } else if (m_requireHardware) {
            // Hardware device was created but the codec would not open on the
            // hardware-wired context. Fail fast — do not rebuild software.
            m_lastError = "Hardware decode required but unavailable";
            cleanup();
            return false;
        } else {
            char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(openRet, errbuf, sizeof(errbuf));
            std::fprintf(stderr,
                         "[FFmpegGpuDecoder] hw decoder open failed, falling back software: codec=%s device=%s error=%s\n",
                         codec->name ? codec->name : "unknown",
                         selectedHwDeviceName(), errbuf);
            const AVCodec *fallbackCodec = softwareCodec;
            if (!fallbackCodec) {
                m_lastError = "Hardware decode failed and no software decoder is available.";
                cleanup();
                return false;
            }
            resetHardwareState();
            if (m_codecCtx) {
                avcodec_free_context(&m_codecCtx);
            }
            codec = fallbackCodec;
            m_codecCtx = avcodec_alloc_context3(codec);
            if (m_codecCtx &&
                avcodec_parameters_to_context(m_codecCtx, par) >= 0) {
                m_codecCtx->pkt_timebase = stream->time_base;
                configureDecoderThreading(m_codecCtx);
            } else {
                m_lastError = "Failed rebuild software decoder context.";
                if (m_codecCtx) {
                    avcodec_free_context(&m_codecCtx);
                }
                cleanup();
                return false;
            }
        }
    } else if (m_requireHardware) {
        // No hardware device/config could be created for this codec. Fail
        // fast — do not open software.
        m_lastError = "Hardware decode required but unavailable";
        cleanup();
        return false;
    }

    if (!opened) {
        // Software path (also reached when hardware was never available in
        // default mode).
        if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
            m_lastError = "Failed to open the decoder.";
            cleanup();
            return false;
        }
        m_hardwareDecodingActive = false;
        std::fprintf(stderr, "[FFmpegGpuDecoder] open OK: decode=software hwActive=0\n");
    }

    m_width = par->width;
    m_height = par->height;

    // --- Slice 2: open audio stream if present (best-effort — audio is
    // not subject to the video codec gate; failure is non-fatal). ---
    if (m_audioStreamIndex >= 0) {
        AVStream *astream = m_fmtCtx->streams[m_audioStreamIndex];
        AVCodecParameters *apar = astream->codecpar;
        const AVCodec *acodec = avcodec_find_decoder(apar->codec_id);
        if (acodec) {
            m_audioCodecCtx = avcodec_alloc_context3(acodec);
            if (m_audioCodecCtx &&
                avcodec_parameters_to_context(m_audioCodecCtx, apar) >= 0) {
                // pkt_timebase must be set before avcodec_open2() so the
                // audio decoder converts packet PTS into correct stream
                // timebase units (fixes MP4 audio PTS / cutoff).
                m_audioCodecCtx->pkt_timebase = astream->time_base;
                if (avcodec_open2(m_audioCodecCtx, acodec, nullptr) >= 0) {
                    m_audioSampleRate = m_audioCodecCtx->sample_rate;
                    m_audioChannels = m_audioCodecCtx->ch_layout.nb_channels;
                    // Set up swresample to convert to interleaved signed
                    // 16-bit PCM at the source sample rate. QAudioSink
                    // handles S16.
                    AVChannelLayout outLayout;
                    av_channel_layout_default(&outLayout, m_audioChannels);
                    if (swr_alloc_set_opts2(&m_swrCtx,
                            &outLayout, AV_SAMPLE_FMT_S16, m_audioSampleRate,
                            &m_audioCodecCtx->ch_layout,
                            m_audioCodecCtx->sample_fmt,
                            m_audioCodecCtx->sample_rate, 0, nullptr) >= 0) {
                        swr_init(m_swrCtx);
                    } else {
                        swr_free(&m_swrCtx);
                        m_swrCtx = nullptr;
                    }
                } else {
                    avcodec_free_context(&m_audioCodecCtx);
                    m_audioStreamIndex = -1;
                }
            } else {
                if (m_audioCodecCtx) {
                    avcodec_free_context(&m_audioCodecCtx);
                }
                m_audioStreamIndex = -1;
            }
        } else {
            m_audioStreamIndex = -1;
        }
    }

    // Duration in seconds.
    if (m_fmtCtx->duration != AV_NOPTS_VALUE && m_fmtCtx->duration > 0) {
        m_duration = static_cast<double>(m_fmtCtx->duration) / AV_TIME_BASE;
    } else if (stream->duration != AV_NOPTS_VALUE && stream->time_base.den) {
        m_duration = static_cast<double>(stream->duration) *
                     stream->time_base.num / stream->time_base.den;
    }

    // Frame rate.
    AVRational fr = av_guess_frame_rate(m_fmtCtx, stream, nullptr);
    if (fr.den > 0) {
        m_fps = static_cast<double>(fr.num) / fr.den;
    }

    m_open = true;
    return true;
}

bool FFmpegGpuDecoder::decodeOneFrame(
    FramePacketPtr &outVideo, std::vector<AudioPacketPtr> &outAudio) {
    // Reads packets and feeds the decoders until one video RGBA frame is
    // produced, or until EOF / error (returns false). All audio frames
    // decoded along the way are appended to outAudio (zero or more per
    // call). outVideo is set once when a video frame is produced.
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        return false;
    }
    AVFrame *frame = av_frame_alloc();
    AVFrame *rgba = av_frame_alloc();
    AVFrame *audioFrame = av_frame_alloc();
    // Scratch frame for hardware->software transfer (av_hwframe_transfer_data).
    // Only used when m_hardwareDecodingActive; freed below, never leaked.
    AVFrame *swFrame = av_frame_alloc();

    bool gotVideo = false;
    bool draining = false;
    // Snapshot the current seek generation so we can tag every packet
    // produced this call. If a seek lands mid-decode the generation advances;
    // serviceSeek() flushes queues, so the snapshot is consistent with
    // whatever survives into the queue.
    const uint64_t gen = m_seekGeneration.load(std::memory_order_acquire);

    while (!gotVideo) {
        int ret = av_read_frame(m_fmtCtx, pkt);
        if (ret < 0) {
            // EOF or read error: flush both decoders.
            draining = true;
            av_packet_unref(pkt);
            avcodec_send_packet(m_codecCtx, nullptr);
            if (m_audioCodecCtx) {
                avcodec_send_packet(m_audioCodecCtx, nullptr);
            }
        } else {
            if (pkt->stream_index == m_streamIndex) {
                ret = avcodec_send_packet(m_codecCtx, pkt);
                av_packet_unref(pkt);
                if (ret == AVERROR(EAGAIN)) {
                    // Decoder's internal buffer is full; fall through to receive.
                } else if (ret < 0) {
                    break;
                }
            } else if (m_audioCodecCtx &&
                       pkt->stream_index == m_audioStreamIndex) {
                ret = avcodec_send_packet(m_audioCodecCtx, pkt);
                av_packet_unref(pkt);
                if (ret < 0 && ret != AVERROR(EAGAIN)) {
                    // Audio send error — not fatal, continue reading.
                }
                // Drain all available audio frames from this packet.
                while (m_audioCodecCtx && m_swrCtx) {
                    ret = avcodec_receive_frame(m_audioCodecCtx, audioFrame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    if (ret < 0) {
                        break;
                    }
                    // Resample to interleaved S16.
                    const int outSamples = swr_get_out_samples(
                        m_swrCtx, audioFrame->nb_samples);
                    uint8_t *outBuf = nullptr;
                    int outLinesize = 0;
                    if (av_samples_alloc(&outBuf, &outLinesize,
                                         m_audioChannels, outSamples,
                                         AV_SAMPLE_FMT_S16, 0) < 0) {
                        break;
                    }
                    const int converted = swr_convert(m_swrCtx, &outBuf,
                        outSamples,
                        const_cast<const uint8_t **>(audioFrame->data),
                        audioFrame->nb_samples);
                    if (converted > 0) {
                        auto apkt = std::make_shared<AudioPacket>();
                        const size_t byteCount = static_cast<size_t>(converted) *
                            m_audioChannels * sizeof(int16_t);
                        apkt->samples.resize(byteCount);
                        std::memcpy(apkt->samples.data(), outBuf, byteCount);
                        apkt->seekGeneration = gen;
                        apkt->ptsSeconds = (audioFrame->pts != AV_NOPTS_VALUE)
                            ? static_cast<double>(audioFrame->pts) *
                              av_q2d(m_audioCodecCtx->pkt_timebase)
                            : 0.0;
                        outAudio.push_back(std::move(apkt));
                    }
                    av_freep(&outBuf);
                    av_frame_unref(audioFrame);
                    // Drain all immediately available audio frames; do not
                    // break after the first — this keeps audio from gating
                    // video decode.
                }
                // FFmpeg's send/receive API is a state machine: after any
                // packet, drain available decoder output until EAGAIN. Do not
                // skip video receive here; a prior video packet may have just
                // made a frame available.
            } else {
                av_packet_unref(pkt);
                continue;
            }
        }

        while (true) {
            ret = avcodec_receive_frame(m_codecCtx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                break;
            }

            // Hardware decode produces frames whose data lives in GPU memory
            // (frame->format == m_hwPixelFormat, e.g. AV_PIX_FMT_VAAPI).
            // sws_scale cannot read those directly, so transfer to the
            // scratch CPU-visible swFrame first and feed that to the existing
            // RGBA/sws path. pts is preserved from the original hardware frame
            // below. On transfer failure the frame is dropped (unref'd) and we
            // continue to the next receive rather than corrupting sws input.
            AVFrame *processFrame = frame;
            if (m_hardwareDecodingActive &&
                frame->format == m_hwPixelFormat) {
                av_frame_unref(swFrame);
                if (av_hwframe_transfer_data(swFrame, frame, 0) < 0) {
                    av_frame_unref(frame);
                    break;
                }
                processFrame = swFrame;
            }

            // Compute aspect-preserved output dimensions, capped so the long
            // edge does not exceed kMaxDecodedOutputLongEdge. This keeps
            // 4K/2160p frames from allocating/converting/uploading full-res
            // RGBA; <= cap frames keep native size. videoWidth()/Height()
            // (source metadata from open()) is unaffected. Use processFrame
            // (the transferred software frame when hardware decode is active).
            int outW = 0;
            int outH = 0;
            computeCappedOutputSize(processFrame->width, processFrame->height, outW, outH);

            const int srcFormat =
                static_cast<int>(static_cast<AVPixelFormat>(processFrame->format));

            // (Re)create swscale context if any source or output cache key
            // changes. This guarantees the context always matches the actual
            // destination dimensions, so sws_scale never writes with a
            // mismatched line size.
            const bool swsStale = !m_swsCtx ||
                m_swsSrcW != processFrame->width ||
                m_swsSrcH != processFrame->height ||
                m_swsSrcFormat != srcFormat ||
                m_swsOutW != outW ||
                m_swsOutH != outH;
            if (swsStale) {
                if (m_swsCtx) {
                    sws_freeContext(m_swsCtx);
                    m_swsCtx = nullptr;
                }
                m_swsCtx = sws_getContext(
                    processFrame->width, processFrame->height,
                    static_cast<AVPixelFormat>(processFrame->format),
                    outW, outH, AV_PIX_FMT_RGBA,
                    SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!m_swsCtx) {
                    break;
                }
                m_swsSrcW = processFrame->width;
                m_swsSrcH = processFrame->height;
                m_swsSrcFormat = srcFormat;
                m_swsOutW = outW;
                m_swsOutH = outH;
            }

            if (!outVideo) {
                outVideo = std::make_shared<FramePacket>();
            }
            outVideo->width = outW;
            outVideo->height = outH;
            const size_t pixelBytes = static_cast<size_t>(outW) * outH * 4;
            // libswscale's SIMD paths can legally overread/write small tails
            // around image buffers; keep FFmpeg's required padding after the
            // tightly-packed RGBA payload so odd H.264 dimensions (e.g.
            // 3848x2080) do not corrupt the heap when FramePacket is freed.
            outVideo->pixels.resize(pixelBytes + AV_INPUT_BUFFER_PADDING_SIZE);

            av_image_fill_arrays(
                rgba->data, rgba->linesize, outVideo->pixels.data(),
                AV_PIX_FMT_RGBA, outW, outH, 1);

            sws_scale(m_swsCtx, processFrame->data, processFrame->linesize, 0,
                      processFrame->height, rgba->data, rgba->linesize);

            outVideo->seekGeneration = gen;
            outVideo->ptsSeconds = (frame->pts != AV_NOPTS_VALUE)
                ? static_cast<double>(frame->pts) *
                      av_q2d(m_codecCtx->pkt_timebase)
                : 0.0;

            // Unref the decoded AVFrame before it is reused on the next
            // avcodec_receive_frame call. Without this the frame's buffers
            // are never released, and the decoder's refcount pool grows on
            // every receive — observed to crash on large MKVs (e.g. Superman).
            av_frame_unref(frame);
            gotVideo = true;
            break;
        }

        // If we were flushing and the decoder is done, stop.
        if (draining) {
            // Yield any final audio from the flush.
            if (m_audioCodecCtx && m_swrCtx && outAudio.empty()) {
                while (true) {
                    ret = avcodec_receive_frame(m_audioCodecCtx, audioFrame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    if (ret < 0) {
                        break;
                    }
                    const int outSamples = swr_get_out_samples(
                        m_swrCtx, audioFrame->nb_samples);
                    uint8_t *outBuf = nullptr;
                    int outLinesize = 0;
                    if (av_samples_alloc(&outBuf, &outLinesize,
                                         m_audioChannels, outSamples,
                                         AV_SAMPLE_FMT_S16, 0) < 0) {
                        break;
                    }
                    const int converted = swr_convert(m_swrCtx, &outBuf,
                        outSamples,
                        const_cast<const uint8_t **>(audioFrame->data),
                        audioFrame->nb_samples);
                    if (converted > 0) {
                        auto apkt = std::make_shared<AudioPacket>();
                        const size_t byteCount = static_cast<size_t>(converted) *
                            m_audioChannels * sizeof(int16_t);
                        apkt->samples.resize(byteCount);
                        std::memcpy(apkt->samples.data(), outBuf, byteCount);
                        apkt->seekGeneration = gen;
                        apkt->ptsSeconds = (audioFrame->pts != AV_NOPTS_VALUE)
                            ? static_cast<double>(audioFrame->pts) *
                              av_q2d(m_audioCodecCtx->pkt_timebase)
                            : 0.0;
                        outAudio.push_back(std::move(apkt));
                    }
                    av_freep(&outBuf);
                    av_frame_unref(audioFrame);
                    // Drain all remaining audio frames on flush; do not
                    // break after the first.
                }
            }
            break;
        }
    }

    av_frame_free(&frame);
    av_frame_free(&rgba);
    av_frame_free(&audioFrame);
    av_frame_free(&swFrame);
    av_packet_free(&pkt);
    return gotVideo;
}


void FFmpegGpuDecoder::serviceSeek() {
    // Called on the decode thread when m_seekRequested is true.
    const double target = m_seekTarget.load(std::memory_order_acquire);
    // Generation is bumped synchronously in seek(), before UI callbacks can
    // pop frames. Do not bump again here or consumers will wait for a
    // generation that never tags decoded output from this serviced seek.

    // Seek the container to the target time using the documented bounded
    // avformat_seek_file() on the video stream, with min=INT64_MIN,
    // target=seekTargetTs, max=INT64_MAX and AVSEEK_FLAG_BACKWARD. The
    // target is converted to the selected video stream's time_base.
    AVStream *vstream = m_fmtCtx->streams[m_streamIndex];
    const int64_t seekTargetTs = static_cast<int64_t>(
        target / av_q2d(vstream->time_base));
    const int seekRet = avformat_seek_file(
        m_fmtCtx, m_streamIndex, INT64_MIN, seekTargetTs, INT64_MAX,
        AVSEEK_FLAG_BACKWARD);

    // Flush both decoders so stale frames/packets don't leak through.
    if (m_codecCtx) {
        avcodec_flush_buffers(m_codecCtx);
    }
    if (m_audioCodecCtx) {
        avcodec_flush_buffers(m_audioCodecCtx);
    }

    // Drop all queued output so consumers see only post-seek frames.
    flushQueues();

    if (seekRet < 0) {
        // Seek failed: clear the pending flag and notify without crashing.
        // Preroll discard stays disarmed so the loop resumes normally from
        // wherever the demuxer currently sits.
        m_eof.store(false, std::memory_order_release);
        m_seekRequested.store(false, std::memory_order_release);
        m_queueCond.notify_all();
        return;
    }

    // Arm preroll discard: avformat_seek_file(BACKWARD) lands on the nearest
    // keyframe at or before the target, so the first decoded frames may
    // precede it. Drop those until a frame at/after the target is accepted,
    // so the first queued video frame does not start before the seek point.
    m_seekPrerollDiscard = true;
    m_seekPrerollTarget = target;

    // Clear EOF so the loop resumes reading from the new position.
    m_eof.store(false, std::memory_order_release);
    m_seekRequested.store(false, std::memory_order_release);
    m_queueCond.notify_all();
}

void FFmpegGpuDecoder::decodeLoop() {
    while (m_running.load(std::memory_order_acquire)) {
        // Service a pending seek before reading any new packets. This is the
        // coalescing point: multiple seek() calls from the UI only set the
        // newest target; we honor whatever value is current right now, and
        // every stale frame that was queued before this generation is gone.
        if (m_seekRequested.load(std::memory_order_acquire)) {
            serviceSeek();
            // After servicing, loop back to re-check running state; the seek
            // may have landed and we want fresh packets from the new position.
            continue;
        }

 // Backpressure: video queue is the pacing boundary for decode. Audio
 // packets are bounded separately and may be dropped when their queue is
 // full, but an audio queue with free space must not let video-only files
 // bypass kMaxQueuedFrames and decode an entire clip into memory.
 {
 std::unique_lock<std::mutex> lk(m_queueMutex);
 m_queueCond.wait(lk, [this] {
 return m_queue.size() < kMaxQueuedFrames ||
 m_seekRequested.load(std::memory_order_acquire) ||
 !m_running.load(std::memory_order_acquire);
            });
        }
        if (!m_running.load(std::memory_order_acquire)) {
            break;
        }
        // Re-check seek after waking — a seek during backpressure wait must
        // be serviced before consuming more.
        if (m_seekRequested.load(std::memory_order_acquire)) {
            continue;
        }

        FramePacketPtr frame;
        std::vector<AudioPacketPtr> audios;
        bool got = decodeOneFrame(frame, audios);

        // Enqueue every decoded audio packet while respecting the audio
        // queue capacity (Fix A: one call may yield zero or more packets).
        if (!audios.empty()) {
            std::lock_guard<std::mutex> lk(m_queueMutex);
            for (auto &apkt : audios) {
                if (m_audioQueue.size() >= kMaxQueuedAudio) {
                    break; // respect capacity — backpressure will let us resume
                }
                m_audioQueue.push_back(std::move(apkt));
            }
        }

        if (!got) {
            // No video this call. If we also produced no audio, the stream
            // is at EOF. Do NOT exit the decode thread (Fix B): keep the
            // loop alive waiting for a future seek request so a later seek
            // can be serviced without re-spawning the thread. serviceSeek
            // clears m_eof and the backpressure wait wakes on seek.
            if (audios.empty()) {
                // No frame reached the seek target before EOF: abandon the
                // preroll discard so we don't drop forever, then let normal
                // EOF handling proceed.
                m_seekPrerollDiscard = false;
                m_eof.store(true, std::memory_order_release);
                m_queueCond.notify_all();
                // Wait until either a seek is requested or we are stopped.
                std::unique_lock<std::mutex> lk(m_queueMutex);
                m_queueCond.wait(lk, [this] {
                    return m_seekRequested.load(std::memory_order_acquire) ||
                           !m_running.load(std::memory_order_acquire);
                });
                continue; // seek (if requested) is serviced at loop top
            }
            // Got audio but no video yet; continue looping.
            m_queueCond.notify_all();
            continue;
        }

        // Seek preroll discard: after serviceSeek() armed this, drop frames
        // decoded from the keyframe/pre-roll that precede the requested
        // target, so the first queued frame is at or after the seek point.
        // Accept (and clear) once a frame reaches the target. If EOF hits
        // first the loop above clears the discard and proceeds normally.
        if (m_seekPrerollDiscard) {
            constexpr double kSeekPrerollEpsilon = 0.020; // half a 25fps frame
            if (frame->ptsSeconds + kSeekPrerollEpsilon <
                m_seekPrerollTarget) {
                // Pre-roll frame before the target: drop and keep decoding.
                continue;
            }
            m_seekPrerollDiscard = false;
        }

        {
            std::lock_guard<std::mutex> lk(m_queueMutex);
            m_queue.push_back(std::move(frame));
        }
        m_queueCond.notify_all();
    }
}

void FFmpegGpuDecoder::start() {
    if (m_running.load(std::memory_order_acquire)) {
        return;
    }
    m_running.store(true, std::memory_order_release);
    m_eof.store(false, std::memory_order_release);
    m_thread = std::thread([this] { decodeLoop(); });
}

void FFmpegGpuDecoder::stop() {
    if (!m_running.exchange(false)) {
        return;
    }
    m_queueCond.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    {
        std::lock_guard<std::mutex> lk(m_queueMutex);
        m_queue.clear();
        m_audioQueue.clear();
    }
    // Drop any stale end-of-stream state from the previous decode run so a
    // subsequent open()/start() can begin playback cleanly. Without this,
    // a clip that reached EOF leaves m_eof set; the next valid load reopens
    // the decoder but atEnd() still returns true and play() refuses to
    // start (start() also clears it, but play() gates on atEnd() first).
    m_eof.store(false, std::memory_order_release);
    // Clear any pending seek so a restart doesn't service a stale target.
    m_seekRequested.store(false, std::memory_order_release);
}

FramePacketPtr FFmpegGpuDecoder::tryPop() {
    std::lock_guard<std::mutex> lk(m_queueMutex);
    if (m_queue.empty()) {
        return nullptr;
    }
    FramePacketPtr f = std::move(m_queue.front());
    m_queue.pop_front();
    m_queueCond.notify_all();
    return f;
}

AudioPacketPtr FFmpegGpuDecoder::tryPopAudio() {
    std::lock_guard<std::mutex> lk(m_queueMutex);
    if (m_audioQueue.empty()) {
        return nullptr;
    }
    AudioPacketPtr a = std::move(m_audioQueue.front());
    m_audioQueue.pop_front();
    m_queueCond.notify_all();
    return a;
}

double FFmpegGpuDecoder::frontPts() const {
    std::lock_guard<std::mutex> lk(m_queueMutex);
    if (m_queue.empty()) {
        return -1.0;
    }
    return m_queue.front()->ptsSeconds;
}
bool FFmpegGpuDecoder::frontPtsValid() const {
    std::lock_guard<std::mutex> lk(m_queueMutex);
    return !m_queue.empty();
}

bool FFmpegGpuDecoder::atEnd() const {
    return m_eof.load(std::memory_order_acquire) &&
           [&] {
               std::lock_guard<std::mutex> lk(m_queueMutex);
               return m_queue.empty() && m_audioQueue.empty();
           }();
}

void FFmpegGpuDecoder::seek(double targetSeconds) {
    if (!m_open) {
        return;
    }
    // Clamp to valid range.
    if (targetSeconds < 0.0) {
        targetSeconds = 0.0;
    }
    const double dur = m_duration;
    if (dur > 0.0 && targetSeconds > dur) {
        targetSeconds = dur;
    }
    // Bump generation synchronously so UI callbacks triggered immediately
    // after seek() reject any queued frames from the old timeline. The decode
    // thread will service the latest target and tag decoded output with this
    // generation.
    m_seekGeneration.fetch_add(1, std::memory_order_acq_rel);
    flushQueues();
    // Coalescing: always overwrite with the newest target. If a seek is
    // already pending, the decode thread hasn't serviced it yet, so this
    // simply replaces the target and the old one never executes.
    m_seekTarget.store(targetSeconds, std::memory_order_release);
    m_seekRequested.store(true, std::memory_order_release);
    m_queueCond.notify_all();
}

bool FFmpegGpuDecoder::seekPending() const {
    return m_seekRequested.load(std::memory_order_acquire);
}

void FFmpegGpuDecoder::flushQueues() {
    std::lock_guard<std::mutex> lk(m_queueMutex);
    m_queue.clear();
    m_audioQueue.clear();
}

} // namespace player_lab
