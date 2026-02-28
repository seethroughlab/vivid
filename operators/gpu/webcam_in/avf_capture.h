#pragma once

#include "capture_source.h"
#include <memory>

class AVFCapture : public CaptureSource {
public:
    AVFCapture();
    ~AVFCapture() override;

    bool open(int device_index, int width, int height, float fps) override;
    void close() override;
    bool is_open() const override;
    bool update() override;
    const uint8_t* pixel_data() const override;
    uint32_t width() const override;
    uint32_t height() const override;
    std::string device_name() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<CaptureSource> create_avf_capture();
