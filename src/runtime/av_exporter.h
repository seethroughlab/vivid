#pragma once

#include <cstdint>
#include <string>

namespace vivid {

// C++ interface for AVFoundation-based .mov file export.
// Implementation in av_exporter.mm (Objective-C++, macOS only).
class AVExporter {
public:
    AVExporter();
    virtual ~AVExporter();

    // Non-copyable
    AVExporter(const AVExporter&) = delete;
    AVExporter& operator=(const AVExporter&) = delete;

    virtual bool start(const std::string& path, uint32_t width, uint32_t height,
                       double fps, uint32_t sample_rate);
    virtual bool write_video_frame(const uint8_t* rgba, uint32_t width, uint32_t height);
    virtual bool write_audio_samples(const float* pcm_interleaved, uint64_t sample_count,
                                     uint32_t channels);
    virtual bool finish();
    virtual bool is_recording() const;
    virtual const std::string& output_path() const;
    virtual uint64_t frame_count() const;
    virtual double fps() const;
    virtual double elapsed_sec() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace vivid
