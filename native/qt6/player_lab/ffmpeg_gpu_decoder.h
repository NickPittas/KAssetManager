#pragma once

#include "player_types.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct SwsContext;
struct SwrContext;
struct AVBufferRef;
struct AVCodec;

namespace player_lab {

// FFmpeg-backed decoder. Hardware decode (VAAPI on Linux, when FFmpeg
// exposes a matching AVCodecHWConfig for the codec) is attempted first;
// on any hardware failure the decoder reopens in pure software mode.
// Either way the decode loop pushes CPU-visible RGBA FramePacket objects
// into a bounded queue — hardware frames are transferred back to the CPU
// before the existing RGBA/sws path, so no GL objects cross threads.
//
// Thread model: the decode loop runs on its own std::thread. It pushes
// CPU-visible RGBA FramePacket objects into a bounded queue. The
// QOpenGLWidget/UI thread consumes them. The decode thread never touches
// any GL object.
//
// Slice 2 adds: audio stream decode (interleaved PCM into a separate
// queue), a seek-by-time API, and a scrub-coalescing generation counter
// that invalidates stale queued frames.
class FFmpegGpuDecoder {
public:
    FFmpegGpuDecoder();
    ~FFmpegGpuDecoder();

    FFmpegGpuDecoder(const FFmpegGpuDecoder &) = delete;
    FFmpegGpuDecoder &operator=(const FFmpegGpuDecoder &) = delete;

    // Open a file and read stream metadata. Returns false on failure.
    // After success, videoWidth/Height/duration/fps are populated.
    // open() classifies the file into a single selected playback path
    // (see selectedPlaybackPathName()) before any decoder is opened; only
    // the documented container/codec combinations are accepted. MXF and any
    // unrecognized combination fail before decoder allocation.
    bool open(const std::string &path);
    bool isOpen() const { return m_open; }
    // Human-readable reason for the most recent open() failure (empty on
    // success or before open() is called).
    const std::string &lastError() const { return m_lastError; }

    // Start the background decode loop. No-op if already running.
    void start();
    // Stop and join the decode thread, flush the queues.
    void stop();

    // Pull the next ready video frame. Returns nullptr if the queue is empty.
    FramePacketPtr tryPop();

    // Pull the next ready audio packet. Returns nullptr if empty.
    AudioPacketPtr tryPopAudio();

    // Peek the PTS of the front frame without removing it, or -1 if empty.
    double frontPts() const;
    // True if at least one frame is queued (so frontPts() is meaningful).
    // Distinct from frontPts() >= 0: a real frame can legitimately have a
    // zero PTS, so the widget must not use frontPts() as a validity check.
    bool frontPtsValid() const;

    // Media metadata (valid after open()).
    int videoWidth() const { return m_width; }
    int videoHeight() const { return m_height; }
    double durationSeconds() const { return m_duration; }
    double fps() const { return m_fps; }

    // Audio metadata (valid after open() if an audio stream was found).
    bool hasAudio() const { return m_audioStreamIndex >= 0; }
    int audioSampleRate() const { return m_audioSampleRate; }
    int audioChannels() const { return m_audioChannels; }

    // True when the decode loop has hit EOF and both queues are drained.
    bool atEnd() const;

    // --- Slice 2: seek by time ---
    // Request a seek to targetSeconds. Implemented as an avformat_seek_file()
    // followed by a decoder flush; the decode loop picks up from the new
    // position. A monotonically increasing generation counter is bumped so
    // consumers can detect and drop frames/packets that predate the seek.
    // Multiple rapid seek() calls naturally coalesce: only the newest
    // target survives because the loop drains stale requests via the
    // generation check. Safe to call from any thread.
    void seek(double targetSeconds);
    // Returns true if a seek is pending (decode loop hasn't serviced it yet).
    bool seekPending() const;
    // Current seek generation. Bumped on every seek(). Consumers can
    // snapshot this before a pop and reject stale output.
    uint64_t seekGeneration() const { return m_seekGeneration.load(); }
    // The target seconds of the most recent seek request.
    double seekTarget() const { return m_seekTarget.load(); }

    // Clear all queued video/audio output without stopping the decode thread.
    // Used by the player to drop backlog on pause/stop so a resume starts
    // from the freshest decoded position.
    void flushQueues();

    // Exposes the active hardware pixel format (as a raw int — the FFmpeg
    // AVPixelFormat enum is intentionally kept out of this header) so the
    // file-scope get_format callback in the .cpp can read it. Returns -1
    // (AV_PIX_FMT_NONE) when hardware decode is not active.
    int hwPixelFormatForCallback() const { return m_hwPixelFormat; }

    // Configure hardware decode on m_codecCtx (device creation + get_format
    // callback + hw_device_ctx) for the given codec. Returns true if a
    // hardware device was created and wired into m_codecCtx; the caller is
    // then responsible for avcodec_open2(). On any failure this fully
    // reverts its own hardware state and returns false. No-op on non-Linux.
    bool tryOpenHardware(const AVCodec *codec);
    // --- Hardware-decode proof / validation mode ---
    // When true, open() must NOT fall back to software decode if hardware
    // setup or avcodec_open2 on the hardware-wired context fails; instead it
    // sets m_lastError and returns false. Default is false so normal product
    // behavior keeps the software fallback.
    void setRequireHardware(bool on);
    bool requireHardware() const;
    // The AVHWDeviceType that was actually selected and opened (as a raw int
    // to keep the FFmpeg enum out of this header), or -1 when software decode
    // is in use / no hardware device was selected.
    int selectedHwDeviceType() const;
    // Human-readable name of the selected hardware device (via FFmpeg's
    // av_hwdevice_get_type_name), or "software" when none was selected.
    const char *selectedHwDeviceName() const;
    // Stable literal name of the single playback path open() selected for
    // the current file, chosen before any decoder is opened. One of:
    //   "unknown"        - before a successful open() (or after cleanup).
    //   "mp4-h264"       - MP4/MOV-family container + H.264 video.
    //   "mkv-h264"       - Matroska/WebM container + H.264 video.
    //   "mov-prores"     - MOV-family container + ProRes video.
    //   "webm-vp8"       - Matroska/WebM container + VP8 video.
    //   "mxf-unsupported"- MXF container; open() fails before decoder open.
    //   "unsupported"    - any other container/codec combination; open() fails.
    const char *selectedPlaybackPathName() const;
    // Free the owned hardware device context and reset the hardware flags /
    // pixel format. Leaves m_codecCtx alone (owned separately).
    void resetHardwareState();

private:
    void decodeLoop();
    bool decodeOneFrame(FramePacketPtr &outVideo,
                        std::vector<AudioPacketPtr> &outAudio);
    void cleanup();
    // Service a pending seek: avformat_seek_file + flush decoder + drop queues.
    // Called on the decode thread.
    void serviceSeek();


    // --- FFmpeg state (decode thread only except for open/cleanup) ---
    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_codecCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;
    // Cache key for m_swsCtx so it is rebuilt when source or output
    // dimensions/format change (downscale cap may shrink the output size).
    int m_swsSrcW = 0;
    int m_swsSrcH = 0;
    int m_swsSrcFormat = -1; // AV_PIX_FMT_NONE; stored as int to avoid header dep
    int m_swsOutW = 0;
    int m_swsOutH = 0;
    int m_streamIndex = -1;

    // The single playback path open() selects before any decoder is opened.
    // Drives selectedPlaybackPathName(). Reset to Unknown on cleanup().
    enum SelectedPlaybackPath {
        PathUnknown,
        PathMp4H264,
        PathMkvH264,
        PathMovProRes,
        PathWebmVp8,
        PathMxfUnsupported,
        PathUnsupported
    };
    SelectedPlaybackPath m_selectedPath = PathUnknown;

    // When true, open() fails fast instead of falling back to software.
    bool m_requireHardware = false;
    // The AVHWDeviceType (as int) actually selected in tryOpenHardware(), or
    // -1 until a hardware device is successfully created/opened.
    int m_selectedHwDeviceType = -1;

    // --- Hardware decode state (set up in open(), torn down in cleanup()) ---
    // Hardware device context owned by the decoder, created via
    // av_hwdevice_ctx_create on Linux when the codec exposes a matching
    // AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX config (VAAPI preferred). When
    // set, a reference is handed to m_codecCtx->hw_device_ctx for FFmpeg.
    AVBufferRef *m_hwDeviceCtx = nullptr;
    // Pixel format of hardware frames produced by the decoder (e.g.
    // AV_PIX_FMT_VAAPI). Stored as a plain int to avoid pulling the FFmpeg
    // pixel-format enum into the header; AV_PIX_FMT_NONE == -1.
    int m_hwPixelFormat = -1;
    // True once the decoder has been opened against a hardware device and
    // frames must be transferred to CPU before sws_scale.
    bool m_hardwareDecodingActive = false;

    // Audio decoder state.
    AVCodecContext *m_audioCodecCtx = nullptr;
    SwrContext *m_swrCtx = nullptr;
    int m_audioStreamIndex = -1;
    int m_audioSampleRate = 0;
    int m_audioChannels = 0;

    bool m_open = false;
    std::string m_lastError;

    int m_width = 0;
    int m_height = 0;
    double m_duration = 0.0;
    double m_fps = 0.0;

    // --- Thread + queues ---
    std::thread m_thread;
    mutable std::mutex m_queueMutex;
    std::condition_variable m_queueCond;
    std::deque<FramePacketPtr> m_queue;
    std::deque<AudioPacketPtr> m_audioQueue;
    static constexpr size_t kMaxQueuedFrames = 6;
    static constexpr size_t kMaxQueuedAudio = 256;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_eof{false};

    // --- Seek state (set by any thread via seek(), serviced by decode thread) ---
    std::atomic<bool> m_seekRequested{false};
    std::atomic<double> m_seekTarget{0.0};
    std::atomic<uint64_t> m_seekGeneration{0};

    // --- Seek preroll discard (decode thread) ---
    // After serviceSeek(), frames decoded from the pre-roll/keyframe before
    // the target are dropped until one at or past the target is accepted, so
    // the first queued video frame does not start before the requested seek
    // position. Cleared once such a frame is accepted; at EOF the discard is
    // abandoned so normal EOF handling proceeds.
    bool m_seekPrerollDiscard = false;
    double m_seekPrerollTarget = 0.0;
};

} // namespace player_lab
