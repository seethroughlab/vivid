#pragma once

#include "../../shared/movie_decode/video_codec_types.h"
#include <cstddef>
#include <cstdint>
#include <memory>
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
    double requested_pts = 0.0;    // requested media-local timestamp (seconds)
    uint64_t loop_generation = 0;  // increments once per loop wrap
    uint64_t request_sequence = 0; // monotonic request id for tie-breaking
    float copy_time_us = 0.0f;    // background copy duration for telemetry
    bool cpu_fallback = false;     // true when produced by a CPU copy fallback

    // Format metadata (defaults match AVF uncompressed path)
    bool compressed = false;
    VideoCompressedFormat compressed_format = VideoCompressedFormat::None;
    bool requires_ycocg = false;

    // Optional retained native frame handle. On macOS non-HAP playback this is a
    // CVPixelBufferRef stored as an opaque shared_ptr with a CoreVideo deleter.
    std::shared_ptr<void> native_pixel_buffer;

    bool has_native_pixel_buffer() const { return native_pixel_buffer != nullptr; }
    bool empty() const { return data.empty() && !has_native_pixel_buffer(); }
    void clear() {
        data.clear(); native_pixel_buffer.reset();
        width = height = 0; pts = 0.0; requested_pts = 0.0;
        loop_generation = 0; request_sequence = 0; copy_time_us = 0.0f;
        cpu_fallback = false;
        compressed = false; compressed_format = VideoCompressedFormat::None;
        requires_ycocg = false;
    }
};

// Bounded queue of decoded video frames.  Mutex-protected; low contention
// (one producer on the decode worker, one consumer on the frame thread).
class DecodedFrameQueue {
public:
    static constexpr size_t kMaxFrames = 12;

    // Push a decoded frame.  If the queue is full, the oldest frame is dropped.
    bool push(DecodedFrame&& frame);

    // Pop the most recent frame, discarding any older frames in the queue.
    // Returns false if the queue is empty.
    bool pop_latest(DecodedFrame& out);

    // Pop the best frame for a loop-aware target time.  Future loop generations
    // are retained so early post-wrap frames cannot steal final pre-wrap frames.
    bool pop_best(double target_pts,
                  uint64_t loop_generation,
                  double frame_duration,
                  DecodedFrame& out);

    // Returns true when a frame for the given loop generation is queued.
    bool has_generation(uint64_t loop_generation) const;

    // Discard all queued frames.
    void flush();

    size_t size() const;

private:
    mutable std::mutex mu_;
    std::vector<DecodedFrame> frames_;
};
