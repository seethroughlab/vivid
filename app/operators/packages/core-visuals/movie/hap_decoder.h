#pragma once

#include "video_decoder.h"
#include <memory>

// HAP compressed-texture video decoder. Uses AVAssetReader (not AVPlayer),
// so all methods are safe to call from any single thread.
class HAPDecoder : public VideoDecoder {
public:
    HAPDecoder();
    ~HAPDecoder() override;

    bool open(const std::string& path) override;
    void close() override;
    bool is_open() const override;
    DecodeStatus decode_frame() override;
    DecodeStatus present_at(double t) override;
    const uint8_t* pixel_data() const override;
    uint32_t width() const override;
    uint32_t height() const override;
    float duration() const override;
    void set_loop(bool loop) override;
    void set_speed(float speed) override;
    float current_time() const override;
    bool seek(double time_seconds) override;
    float frame_rate() const override;
    uint64_t nil_frame_count() const override;
    VideoFrameCompressionMode compression_mode() const override;
    VideoCompressedFormat compressed_format() const override;
    bool requires_ycocg_decode() const override;
    const uint8_t* compressed_data() const override;
    size_t compressed_size() const override;

    static bool is_hap_file(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool is_hap_video_file(const std::string& path);
std::unique_ptr<VideoDecoder> create_hap_decoder();
