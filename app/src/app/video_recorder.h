#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct Transport;

namespace vivid {

class AVExporter;

// A snapshot of the export state, returned by status()/stop() and surfaced by the MCP
// video_export_status tool and the GUI toasts.
struct VideoExportStatus {
    bool        recording = false;
    std::string path;
    uint64_t    frames = 0;
    double      elapsed_sec = 0.0;
    uint32_t    width = 0;
    uint32_t    height = 0;
    std::string error;   // set when a start() failed
};

// Realtime AV export coordinator (the trunk analog of vivid-classic's CaptureCoordinator).
// Owns an AVExporter. The caller feeds it one already-read RGBA8 output frame per rendered frame
// (kept free of any GPU dependency so it is unit-testable); the recorder appends it as video and
// drains the Transport's audio tap into the AAC track. Sync is wall-clock (see av_exporter).
// MAIN THREAD ONLY (the exporter finalize pumps the Cocoa run loop; the only cross-thread piece is
// the lock-free tap producer inside the audio callback).
class VideoRecorder {
public:
    VideoRecorder();                                                // real AVExporter
    explicit VideoRecorder(std::unique_ptr<AVExporter> exporter);   // inject a mock (tests)
    ~VideoRecorder();

    VideoRecorder(const VideoRecorder&) = delete;
    VideoRecorder& operator=(const VideoRecorder&) = delete;

    // Begin an export to `path` (.mp4 or .mov). `seconds` <= 0 records until stop() (manual);
    // > 0 sets an auto-stop deadline (tick() stops itself). Video dimensions are locked to
    // (out_w, out_h) floored to even (H.264). Returns false and fills *err on failure (already
    // recording / bad path / zero size / writer failed). Arms the transport's recording tap.
    bool start(const std::string& path, double fps, double seconds,
               uint32_t out_w, uint32_t out_h, uint32_t sample_rate,
               Transport& tr, std::string* err = nullptr);

    // Drive one frame. `rgba` is this frame's Output readback (w×h, tightly packed) or nullptr if
    // the output was blank/unavailable (video skipped, audio still drained). No-op unless recording.
    // Returns true exactly on the tick where a deadline auto-stop fired, so the caller can toast.
    bool tick(const uint8_t* rgba, uint32_t w, uint32_t h, Transport& tr);

    // Finalize the file (blocks briefly pumping the run loop). No-op if not recording.
    VideoExportStatus stop();

    bool is_recording() const;
    VideoExportStatus status() const;

private:
    std::unique_ptr<AVExporter> exporter_;
    Transport*  tr_ = nullptr;         // bound at start (for stop()/auto-stop tap drain)
    std::string path_;
    uint32_t    w_ = 0, h_ = 0;        // locked (even) video dimensions
    double      fps_ = 60.0;
    double      deadline_ = 0.0;       // seconds since start; 0 = manual stop
    bool        stopping_ = false;     // re-entrancy guard: finish() pumps the run loop, which can
                                       // re-enter tick()/stop() before is_recording() flips to false
    std::vector<float>   audio_scratch_;   // reused drain buffer
    std::vector<uint8_t> crop_scratch_;    // reused crop buffer (only if the output size is odd/changed)
    VideoExportStatus    last_;        // status() after a stop
};

}  // namespace vivid
