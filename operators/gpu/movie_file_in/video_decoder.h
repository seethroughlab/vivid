#pragma once

#include <string>
#include <cstdint>

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
};
