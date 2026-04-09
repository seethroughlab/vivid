#pragma once

#include "../../shared/movie_decode/video_codec_types.h"
#include <cstdint>
#include <mutex>
#include <vector>

// A decoded video frame ready for GPU upload.
// Carries either uncompressed BGRA pixels or compressed BC data,
// so both AVF and HAP paths can use the same queue infrastructure.
struct DecodedFrame {
    std::vector<uint8_t> data;     // BGRA pixels OR compressed BC data
    uint32_t width = 0;
    uint32_t height = 0;
    double pts = 0.0;              // presentation timestamp (seconds)
    float copy_time_us = 0.0f;    // background copy duration for telemetry

    // Format metadata (defaults match AVF uncompressed path)
    bool compressed = false;
    VideoCompressedFormat compressed_format = VideoCompressedFormat::None;
    bool requires_ycocg = false;

    bool empty() const { return data.empty(); }
    void clear() {
        data.clear(); width = height = 0; pts = 0.0; copy_time_us = 0.0f;
        compressed = false; compressed_format = VideoCompressedFormat::None;
        requires_ycocg = false;
    }
};

// Bounded queue of decoded video frames.  Mutex-protected; low contention
// (one producer on the decode worker, one consumer on the frame thread).
class DecodedFrameQueue {
public:
    static constexpr size_t kMaxFrames = 3;

    // Push a decoded frame.  If the queue is full, the oldest frame is dropped.
    bool push(DecodedFrame&& frame);

    // Pop the most recent frame, discarding any older frames in the queue.
    // Returns false if the queue is empty.
    bool pop_latest(DecodedFrame& out);

    // Discard all queued frames.
    void flush();

    size_t size() const;

private:
    mutable std::mutex mu_;
    std::vector<DecodedFrame> frames_;
};
