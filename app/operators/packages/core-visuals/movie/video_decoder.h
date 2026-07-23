#pragma once

#include "video_codec_types.h"
#include <string>
#include <cstdint>
#include <cstddef>

// Result of a decode_frame() call.
enum class DecodeStatus {
    NewFrame,     // New frame decoded; pixel_data()/compressed_data() updated
    ReusedFrame,  // No new frame at this display time (content unchanged)
    NilFrame      // Decoder returned nil — stall, error, or not ready
};

// Threading contract:
//   open() and close() are safe to call from any thread (implementations
//   dispatch internally when needed, e.g. AVFDecoder dispatches to main).
//   All other methods must be called from the main thread (GPU render loop).
//   Implementations may assert this; see AVFDecoder for an example.
class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    virtual DecodeStatus decode_frame() = 0;
    // Present the frame at media time `t` seconds (audio-master A/V lock): make pixel_data() /
    // compressed_data() hold the frame nearest `t`, correcting drift as needed. Default self-clocks
    // (ignores t) for decoders without a seekable presentation model.
    virtual DecodeStatus present_at(double t) { (void)t; return decode_frame(); }
    virtual const uint8_t* pixel_data() const = 0;  // BGRA8
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
    virtual float duration() const = 0;
    virtual void set_loop(bool loop) = 0;
    virtual void set_speed(float speed) = 0;  // speed > 0 starts playback, 0 pauses
    virtual float current_time() const = 0;
    // Optional: seek decoder timeline to local media time (seconds).
    virtual bool seek(double /*time_seconds*/) { return false; }

    // Cumulative count of nil/missed frame fetches from the underlying decoder.
    virtual uint64_t nil_frame_count() const { return 0; }

    // Nominal frame rate (fps). Used for sync threshold calculation.
    virtual float frame_rate() const { return 30.0f; }

    // Optional compressed-frame path (used by HAP direct BC uploads).
    // Contract: compression mode/format are fixed after successful open();
    // decode_frame() updates only frame payload and playback time.
    virtual VideoFrameCompressionMode compression_mode() const {
        return VideoFrameCompressionMode::UncompressedBGRA;
    }
    virtual VideoCompressedFormat compressed_format() const {
        return VideoCompressedFormat::None;
    }
    // HapQ (HapY) frames are BC3-encoded YCoCg and require shader-space conversion.
    virtual bool requires_ycocg_decode() const { return false; }
    virtual const uint8_t* compressed_data() const { return nullptr; }
    virtual size_t compressed_size() const { return 0; }
};
