#pragma once

#include <cstdint>
#include <string>

namespace vivid {

// C++ interface for AVFoundation-based .mov file export.
// Implementation in av_exporter.mm (Objective-C++, macOS only).
class AVExporter {
public:
    AVExporter();
    ~AVExporter();

    // Non-copyable
    AVExporter(const AVExporter&) = delete;
    AVExporter& operator=(const AVExporter&) = delete;

    bool start(const std::string& path, uint32_t width, uint32_t height,
               double fps, uint32_t sample_rate);
    bool write_video_frame(const uint8_t* rgba, uint32_t width, uint32_t height);
    bool write_audio_samples(const float* pcm_interleaved, uint64_t sample_count,
                             uint32_t channels);
    bool finish();
    bool is_recording() const;
    const std::string& output_path() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace vivid
