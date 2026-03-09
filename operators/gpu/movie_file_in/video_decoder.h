#pragma once

#include <string>
#include <cstdint>
#include <cstddef>

enum class VideoFrameCompressionMode {
    UncompressedBGRA,
    CompressedBC
};

enum class VideoCompressedFormat {
    None,
    BC1,
    BC3,
    BC4
};

class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    virtual bool decode_frame() = 0;  // returns true if new frame available
    virtual const uint8_t* pixel_data() const = 0;  // BGRA8
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
    virtual float duration() const = 0;
    virtual void set_loop(bool loop) = 0;
    virtual void set_speed(float speed) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual float current_time() const = 0;
    // Optional: seek decoder timeline to local media time (seconds).
    virtual bool seek(double /*time_seconds*/) { return false; }

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
