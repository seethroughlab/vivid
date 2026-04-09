#pragma once

#include "video_decoder.h"
#include <memory>

#ifdef __APPLE__
#include <CoreVideo/CVPixelBuffer.h>
#endif

struct DecodedFrame;  // forward decl from decoded_frame_queue.h

// Retained CVPixelBuffer acquired from AVFoundation on the main thread.
// Ownership transfers to the caller; release via release() or move into
// AVFDecoder::copy_pixel_buffer().
struct AcquiredPixelBuffer {
#ifdef __APPLE__
    CVPixelBufferRef buffer = nullptr;
#else
    void* buffer = nullptr;
#endif
    double pts = 0.0;
    DecodeStatus status = DecodeStatus::NilFrame;

    AcquiredPixelBuffer() = default;
    ~AcquiredPixelBuffer();
    AcquiredPixelBuffer(const AcquiredPixelBuffer&) = delete;
    AcquiredPixelBuffer& operator=(const AcquiredPixelBuffer&) = delete;
    AcquiredPixelBuffer(AcquiredPixelBuffer&& other) noexcept;
    AcquiredPixelBuffer& operator=(AcquiredPixelBuffer&& other) noexcept;

    bool valid() const { return buffer != nullptr; }
    void release();
};

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
    DecodeStatus decode_frame() override;
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

    // --- Split decode for async pipeline (Stage 4) ---

    // Phase 1 (main thread only, fast): acquire a retained CVPixelBuffer
    // from AVFoundation without doing the CPU copy.
    AcquiredPixelBuffer acquire_pixel_buffer();

    // Phase 2 (any thread): lock, copy, unlock, release the pixel buffer.
    // Returns a DecodedFrame with tightly packed BGRA pixels.
    static DecodedFrame copy_pixel_buffer(AcquiredPixelBuffer&& acquired);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<VideoDecoder> create_avf_decoder();
