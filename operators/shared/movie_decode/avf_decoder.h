#pragma once

#include "video_decoder.h"
#include <memory>

// AVFoundation-based video decoder for macOS.
// open() and close() are safe from any thread (dispatch to main internally).
// All other methods must be called from the main thread (GPU render loop).
// Implementation in avf_decoder.mm (Objective-C++).

class AVFDecoder : public VideoDecoder {
public:
    AVFDecoder();
    ~AVFDecoder() override;

    bool open(const std::string& path) override;
    void close() override;
    bool is_open() const override;
    bool decode_frame() override;
    const uint8_t* pixel_data() const override;
    uint32_t width() const override;
    uint32_t height() const override;
    float duration() const override;
    void set_loop(bool loop) override;
    void set_speed(float speed) override;
    float current_time() const override;
    bool seek(double time_seconds) override;
    float frame_rate() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<VideoDecoder> create_avf_decoder();
