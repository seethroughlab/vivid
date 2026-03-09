#pragma once

#include <cstdint>
#include <memory>
#include <string>

// AVFoundation-based audio extractor for macOS.
// Decodes audio from a movie file into a lock-free SPSC ring buffer.
// Main thread: open/close/fill_buffer/resync
// Audio thread: read_samples/read_head_pts/discard_samples

class AVFAudioExtractor {
public:
    AVFAudioExtractor();
    ~AVFAudioExtractor();

    bool open(const std::string& path, uint32_t target_sample_rate = 48000);
    void close();
    bool is_open() const;
    bool has_audio() const;
    float duration() const;

    // Main thread: set playback speed for pitch-preserving time stretch
    void set_speed(float speed);
    // Main thread: control EOF loop behavior
    void set_loop(bool loop);

    // Main thread: decode into ring buffer (call each frame)
    void fill_buffer();

    // Main thread: recreate AVAssetReader at specified time position
    void resync(double time_seconds);

    // Audio thread: read deinterleaved samples from ring buffer
    uint32_t read_samples(float* left, float* right, uint32_t max_frames);

    // Audio thread: drop samples from ring buffer to catch up to external clock
    uint32_t discard_samples(uint32_t max_frames);

    // Audio thread: PTS of current read position
    double read_head_pts() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
