#pragma once

#include "runtime/debug/output_analyzer.h"
#include <webgpu/webgpu.h>
#include <chrono>
#include <cstdint>
#include <functional>
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
    std::vector<uint8_t> frame_a_pixels;       // first sampled frame
    std::vector<uint8_t> last_frame_pixels;    // for inter-sample motion
    std::vector<VisualSample> visual_samples;  // intra-window time series (AV mode only)
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_sample_time;
    uint32_t frame_w = 0, frame_h = 0;
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


// ---------------------------------------------------------------------------
// P1 (pivot): minimal data-only endpoints that the Python librosa-based MCP
// tools sit on top of. These return raw bytes / arrays; analysis and
// rendering live on the Python side.
// ---------------------------------------------------------------------------

// Per-frame lane sampling for a node's lane-array output port. Optional
// `id_port_name` collects a parallel id stream so callers can color rows by
// stable voice identity. JSON-only response, no PNG.
struct LaneSeriesRequest {
    std::string node_id;
    std::string port_name;
    std::string id_port_name;       // optional
    float duration_ms = 500.0f;
    std::promise<std::string> promise;
};

struct PendingLaneSeries {
    LaneSeriesRequest req;
    std::chrono::steady_clock::time_point start_time;
    bool started = false;
    int audio_node_idx = -1;
    int port_idx = -1;
    int id_port_idx = -1;          // -1 = no id_port_name
    std::vector<std::vector<float>> per_lane_samples;
    std::vector<std::vector<uint32_t>> per_lane_ids;
    uint32_t observed_lane_count = 0;
};

// Unified MIDI inject + audio capture window. Atomically: drain tap →
// fire scheduled events → wait → pop samples → return WAV bytes (and an
// optional lane-data side-channel). Replaces the prior
// {capture_note_response, capture_polyphony_response,
// capture_retrigger_response} as a single data endpoint; Python wrappers
// build the events[] schedule.
struct NoteWindowEvent {
    float t_ms = 0.0f;
    uint8_t bytes[3] = {0, 0, 0};
    uint8_t length = 0;
};

struct NoteWindowRequest {
    std::string midi_node_id;
    std::vector<NoteWindowEvent> events;
    float capture_ms = 1000.0f;
    // Optional per-node tap. When set, the response WAV comes from the
    // named node's 1024-sample waveform ring instead of the final mix.
    std::string audio_node_id;
    // Optional lane-data collection (mirrors LaneSeriesRequest).
    std::string lane_node_id;
    std::string lane_port_name;
    std::string lane_id_port_name;
    std::promise<std::string> promise;
};

struct PendingNoteWindow {
    NoteWindowRequest req;
    std::chrono::steady_clock::time_point start_time;
    bool started = false;
    size_t next_event = 0;
    int lane_audio_node_idx = -1;
    int lane_port_idx = -1;
    int lane_id_port_idx = -1;
    std::vector<std::vector<float>> per_lane_samples;
    std::vector<std::vector<uint32_t>> per_lane_ids;
    uint32_t observed_lane_count = 0;
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


    // -----------------------------------------------------------------
    // P1 (pivot) — minimal data-only endpoints. See header section above.
    // -----------------------------------------------------------------

    // Synchronous: read the named audio node's 1024-sample waveform ring
    // and return as a 32-bit float WAV (base64). Safe to call from the HTTP
    // dispatch thread — only touches atomics. ~21 ms of audio @ 48 kHz.
    std::string handle_capture_node_audio(const std::string& node_id, int channel);

    // Per-frame lane sampling state machine.
    std::future<std::string> request_lane_series(const std::string& node_id,
                                                  const std::string& port_name,
                                                  const std::string& id_port_name,
                                                  float duration_ms);
    void tick_lane_series();

    // Unified inject + capture state machine. Returns raw WAV (and optional
    // lane data); Python wrappers do all analysis and rendering.
    std::future<std::string> request_note_window(NoteWindowRequest req);
    void tick_note_window();

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


    // P1 (pivot) — minimal data-only state machines.
    std::vector<LaneSeriesRequest> pending_lane_series_requests_;
    std::vector<PendingLaneSeries> pending_lane_series_;
    std::vector<NoteWindowRequest> pending_note_window_requests_;
    std::vector<PendingNoteWindow> pending_note_windows_;
};

} // namespace vivid
