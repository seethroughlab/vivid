#pragma once

#include <cstdint>
#include <memory>
#include <string>

// AVFoundation-based audio extractor for macOS.
// Decodes audio from a movie file.
// All methods are called from the fill thread (or main thread during open/close).

class AVFAudioExtractor {
public:
    AVFAudioExtractor();
    ~AVFAudioExtractor();

    bool open(const std::string& path, uint32_t target_sample_rate = 48000);
    void close();
    bool is_open() const;
    bool has_audio() const;
    float duration() const;

    // Set playback speed for pitch-preserving time stretch (or rate change)
    void set_speed(float speed);
    // Control EOF loop behavior
    void set_loop(bool loop);
    // Toggle pitch-preserving mode (true = TimePitch, false = rate-only)
    void set_pitch_preserve(bool preserve);
    bool pitch_preserve() const;

    // Decode samples directly into caller-provided buffers.
    // Returns the number of frames actually decoded.
    // This replaces the old fill_buffer + read_samples two-stage pipeline.
    uint32_t decode_samples(float* left, float* right, uint32_t max_frames);

    // Recreate AVAssetReader at specified time position
    void resync(double time_seconds);

    // PTS of current write position (latest decoded media time)
    double write_head_pts() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
