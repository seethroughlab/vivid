#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace vivid {

// Abstract video-export sink (H.264 video + AAC audio). The concrete implementation is
// AVFoundation-backed (av_exporter.mm, macOS) or a no-op (av_exporter_stub.cpp, off macOS) — pick
// one with make_platform_av_exporter(). Being a pure interface lets a test substitute a mock
// (app/tests/test_video_recorder.cpp) without linking AVFoundation.
//
// The concrete impl chooses the container from the output path extension: `.mp4` -> MPEG-4
// (default, web-friendly), `.mov` -> QuickTime.
class AVExporter {
public:
    virtual ~AVExporter() = default;

    // ADR-0032 Phase C: put the exporter in DETERMINISTIC OFFLINE mode (call before start()). The two
    // write_* methods then honor an explicit PTS, and the encoder is fed faster-than-realtime (blocking
    // on input readiness rather than dropping). Default no-op → the realtime recorder path is unchanged.
    virtual void begin_offline() {}

    // Begin writing. width/height must be even (H.264). sample_rate>0 adds an AAC audio track.
    virtual bool start(const std::string& path, uint32_t width, uint32_t height,
                       double fps, uint32_t sample_rate) = 0;
    // Append one RGBA8 frame (tightly packed, top-left origin). pts_sec < 0 (default) => wall-clock PTS
    // (realtime capture); pts_sec >= 0 => that explicit presentation time in seconds (offline determinism).
    virtual bool write_video_frame(const uint8_t* rgba, uint32_t width, uint32_t height,
                                   double pts_sec = -1.0) = 0;
    // Append interleaved float32 PCM (sample_count = frames * channels). Same pts_sec convention.
    virtual bool write_audio_samples(const float* pcm_interleaved, uint64_t sample_count,
                                     uint32_t channels, double pts_sec = -1.0) = 0;
    // Finalize the file (blocks up to ~10s pumping the main run loop). Main thread only.
    virtual bool finish() = 0;
    virtual bool is_recording() const = 0;
    virtual const std::string& output_path() const = 0;
    virtual uint64_t frame_count() const = 0;
    virtual double fps() const = 0;
    virtual double elapsed_sec() const = 0;
};

// The platform-appropriate concrete exporter (AVFoundation on macOS, a no-op stub elsewhere).
std::unique_ptr<AVExporter> make_platform_av_exporter();

}  // namespace vivid
