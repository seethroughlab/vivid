#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct CameraInfo {
    std::string device_id;
    std::string name;
    bool is_default = false;
};

// Platform camera enumeration (implemented per-platform alongside the factory)
std::vector<CameraInfo> enumerate_cameras();

class CaptureSource {
public:
    virtual ~CaptureSource() = default;
    virtual bool open(int device_index, int width, int height, float fps) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;
    virtual void stop() = 0;                          // pause capture session
    virtual void start() = 0;                         // resume capture session
    virtual bool update() = 0;                       // true if new frame copied
    virtual const uint8_t* pixel_data() const = 0;   // BGRA8
    virtual uint32_t width() const = 0;
    virtual uint32_t height() const = 0;
    virtual std::string device_name() const = 0;
};
