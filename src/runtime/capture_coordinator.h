#pragma once

#include "runtime/output_analyzer.h"
#include <webgpu/webgpu.h>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace vivid {

class AudioEngine;
class AVExporter;
class RuntimeAPI;
namespace ui { class NodeGraphUI; }

enum class CaptureType { Frame, Audio, AV, StartRecording, StopRecording, SnapshotToFile };

struct CaptureRequest {
    CaptureType type;
    float audio_duration = 1.0f;
    std::string recording_path;
    double recording_fps = 60.0;
    std::promise<std::string> promise;
};

struct AnalysisRequest {
    AnalysisMode mode = AnalysisMode::Frame;
    float window_seconds = 1.0f;
    bool include_payload = false;
    std::string node_id;  // if set, solo this node during analysis
    std::promise<std::string> promise;
};

struct PendingAnalysis {
    AnalysisMode mode;
    float window_seconds;
    bool include_payload;
    std::vector<uint8_t> frame_a_pixels;
    uint32_t frame_w = 0, frame_h = 0;
    std::chrono::steady_clock::time_point start_time;
    bool frame_a_captured = false;
    bool audio_tap_started = false;
    std::string node_id;
    std::string prev_solo;
    bool solo_set = false;
    std::promise<std::string> promise;
};

struct CompareRequest {
    AnalysisMode mode = AnalysisMode::Frame;
    float window_a = 1.0f;
    float window_b = 1.0f;
    bool include_payload = false;
    std::string node_id;
    std::promise<std::string> promise;
};

struct InterfaceCaptureRequest {
    std::string node_id;
    std::string save_path;
    bool ensure_ui_visible = true;
    std::promise<std::string> promise;
};

class CaptureCoordinator {
public:
    CaptureCoordinator();
    explicit CaptureCoordinator(std::unique_ptr<AVExporter> exporter);
    ~CaptureCoordinator();

    void set_audio_engine(AudioEngine* ae) { audio_ = ae; }
    void set_runtime_api(RuntimeAPI* api) { runtime_api_ = api; }

    // Called from HTTP thread — pushes a request and returns a future
    std::future<std::string> request_capture(CaptureType type, float audio_duration = 1.0f);
    std::future<std::string> request_start_recording(const std::string& path, double fps);
    std::future<std::string> request_stop_recording();

    // Analysis requests (called from HTTP thread)
    std::future<std::string> request_analyze(AnalysisMode mode, float window_seconds,
                                              bool include_payload, const std::string& node_id = "");
    std::future<std::string> request_compare(AnalysisMode mode, float window_a, float window_b,
                                              bool include_payload, const std::string& node_id = "");

    // Immediate handlers (called from HTTP thread, don't need main thread)
    std::string handle_start_recording_tap();
    std::string handle_stop_recording_tap();

    // Called from main thread after runtime.tick()
    bool has_pending() const;
    void process_pending(WGPUDevice device, WGPUQueue queue,
                         WGPUTexture capture_tex,
                         uint32_t tex_width, uint32_t tex_height);

    // Called each frame to advance analysis state machines
    void tick_analysis(WGPUDevice device, WGPUQueue queue,
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

    // Whole-interface capture from the running window after UI overlays are drawn.
    std::future<std::string> request_interface_capture(const std::string& node_id,
                                                       const std::string& save_path,
                                                       bool ensure_ui_visible);
    bool has_pending_interface_capture() const;
    bool has_active_interface_capture() const;
    bool prepare_pending_interface_capture(ui::NodeGraphUI& graph_ui);
    void complete_active_interface_capture(uint32_t width, uint32_t height,
                                           const std::vector<uint8_t>& png_data);
    void fail_active_interface_capture(const std::string& error);
    void fail_pending_interface_captures(const std::string& error);

    // Returns true if there are pending analyses (for main loop check)
    bool has_pending_analyses() const;

private:
    std::string capture_frame(WGPUDevice device, WGPUQueue queue,
                              WGPUTexture tex,
                              uint32_t w, uint32_t h);
    std::string capture_audio(float duration);

    std::string serialize_analysis(const AnalysisResult& result);
    std::string serialize_comparison(const ComparisonResult& result);

    AudioEngine* audio_ = nullptr;
    RuntimeAPI* runtime_api_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<CaptureRequest> pending_;
    std::unique_ptr<AVExporter> exporter_;

    // Analysis state
    std::vector<AnalysisRequest> pending_analysis_requests_;
    std::vector<PendingAnalysis> pending_analyses_;
    std::vector<CompareRequest> pending_compare_requests_;

    std::vector<InterfaceCaptureRequest> pending_interface_capture_requests_;
    std::optional<InterfaceCaptureRequest> active_interface_capture_;
};

} // namespace vivid
