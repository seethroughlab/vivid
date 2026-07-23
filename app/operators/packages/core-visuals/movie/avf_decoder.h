#pragma once
// AVFoundation video decoder implementing the VideoDecoder interface via AVAssetReader — sequential
// frame decode to tightly-packed BGRA8. The standard-codec path (H.264/HEVC/ProRes/…); HAP files
// route to HAPDecoder instead (see decoder_factory). AVAssetReader (not AVPlayer) so present_at(t)
// can hand back the EXACT frame at the audio master clock — frame-accurate A/V lock, no seeking or
// rate control. Single-thread use for the decode methods (called from the op's render thread).
#include "video_decoder.h"
#include <memory>

class AVFDecoder : public VideoDecoder {
public:
    AVFDecoder();
    ~AVFDecoder() override;

    bool open(const std::string& path) override;
    void close() override;
    bool is_open() const override;
    DecodeStatus decode_frame() override;
    DecodeStatus present_at(double t) override;    // audio-master: fetch the frame at media time t
    const uint8_t* pixel_data() const override;   // BGRA8, valid until the next NewFrame
    uint32_t width() const override;
    uint32_t height() const override;
    float duration() const override;
    void set_loop(bool loop) override;
    void set_speed(float speed) override;         // >0 plays at that rate, 0 pauses
    float current_time() const override;
    bool seek(double time_seconds) override;
    float frame_rate() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<VideoDecoder> create_avf_decoder();
