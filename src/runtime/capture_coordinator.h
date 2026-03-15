#pragma once

#include <webgpu/webgpu.h>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vivid {

class AudioEngine;
class AVExporter;

enum class CaptureType { Frame, Audio, AV, StartRecording, StopRecording, SnapshotToFile };

struct CaptureRequest {
    CaptureType type;
    float audio_duration = 1.0f;
    std::string recording_path;
    double recording_fps = 60.0;
    std::promise<std::string> promise;
};

class CaptureCoordinator {
public:
    CaptureCoordinator();
    explicit CaptureCoordinator(std::unique_ptr<AVExporter> exporter);
    ~CaptureCoordinator();

    void set_audio_engine(AudioEngine* ae) { audio_ = ae; }

    // Called from HTTP thread — pushes a request and returns a future
    std::future<std::string> request_capture(CaptureType type, float audio_duration = 1.0f);
    std::future<std::string> request_start_recording(const std::string& path, double fps);
    std::future<std::string> request_stop_recording();

    // Immediate handlers (called from HTTP thread, don't need main thread)
    std::string handle_start_recording_tap();
    std::string handle_stop_recording_tap();

    // Called from main thread after scheduler.tick()
    bool has_pending() const;
    void process_pending(WGPUDevice device, WGPUQueue queue,
                         WGPUTexture capture_tex,
                         uint32_t tex_width, uint32_t tex_height);

    // Called each frame when recording is active
    void tick_recording(WGPUDevice device, WGPUQueue queue,
                        WGPUTexture capture_tex,
                        uint32_t tex_width, uint32_t tex_height);
    bool is_recording() const;
    uint64_t recording_frame_count() const;
    double recording_duration_sec() const;

    // Snapshot-to-file: writes PNG directly to disk, returns path
    std::future<std::string> request_snapshot_to_file(const std::string& path);

private:
    std::string capture_frame(WGPUDevice device, WGPUQueue queue,
                              WGPUTexture tex,
                              uint32_t w, uint32_t h);
    std::string capture_audio(float duration);

    AudioEngine* audio_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<CaptureRequest> pending_;
    std::unique_ptr<AVExporter> exporter_;
};

} // namespace vivid
