#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace player_lab {

// A single CPU-visible decoded video frame handed from the decode thread
// to the UI/GL thread. All bytes are owned by the vector — no FFmpeg
// objects cross the boundary.
struct FramePacket {
    int width = 0;
    int height = 0;
    // Presentation timestamp in seconds (from FFmpeg PTS).
    double ptsSeconds = 0.0;
    // Packed RGBA8, row-major, tightly packed in the first width * height * 4
    // bytes. The vector may include trailing FFmpeg padding bytes.
    std::vector<uint8_t> pixels;
    // Slice 2: seek generation tag. Set by the decode loop to the current
    // m_seekGeneration at decode time. Consumers drop frames whose tag
    // does not match the live generation (stale output from before a seek).
    uint64_t seekGeneration = 0;
};

// Shared-pointer alias so frames can flow through a bounded queue cheaply.
using FramePacketPtr = std::shared_ptr<FramePacket>;

// A chunk of decoded PCM audio handed from the decode thread to the audio
// output (QAudioSink) on the UI thread. Bytes are planar-agnostic interleaved
// PCM in the QAudioFormat the player configured. All bytes are owned by the
// vector — no FFmpeg objects cross the boundary.
struct AudioPacket {
    // Interleaved PCM bytes ready for QAudioSink::write().
    std::vector<uint8_t> samples;
    // Presentation timestamp (seconds) of the first sample in this packet.
    double ptsSeconds = 0.0;
    // Slice 2: seek generation tag (see FramePacket::seekGeneration).
    uint64_t seekGeneration = 0;
};

using AudioPacketPtr = std::shared_ptr<AudioPacket>;

} // namespace player_lab
