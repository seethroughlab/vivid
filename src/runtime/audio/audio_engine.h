#pragma once

#include "runtime/graph/snapshot_types.h"
#include "runtime/operators/operator_registry.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

class RuntimeCore;
struct CompiledGraph;
class AudioFrameBridge;
class AudioExecutor;

// ---------------------------------------------------------------------------
// AudioEngine — thin facade over AudioExecutor, CompiledGraph, and AudioFrameBridge.
//
// Manages audio device lifecycle and exposes analysis/recording/diagnostics
// to the main thread.  All real audio processing happens in AudioExecutor.
// ---------------------------------------------------------------------------

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool build(RuntimeCore& core);
    bool start(bool use_null_device = false);
    void shutdown();

    // Per-frame main-thread housekeeping. Currently: detects audio_out
    // `device` param changes and re-inits the playback device.
    void tick();

    // Returns a non-zero target sample rate when the most recently opened
    // device's actual rate differs from the session rate, signalling that
    // main.cpp should rebuild the audio graph at that rate. Consume-on-read:
    // each call clears the slot. Returns 0 when nothing is pending.
    uint32_t consume_pending_session_sample_rate();

    // Read active analysis snapshot (call from main thread)
    const AnalysisSnapshot& analysis_read() const;
    // Map node_id to audio engine index (-1 if not found)
    int audio_node_index(const std::string& node_id) const;
    // Map (node_id, output port_name) to (port_idx, lane-array? boolean).
    // Returns -1 in `port_idx_out` if not found. Used by debug captures that
    // need to read lane_outputs via the bridge snapshot.
    void audio_node_output_port(const std::string& node_id,
                                const std::string& port_name,
                                int* port_idx_out,
                                bool* is_lane_array_out) const;

    // Hot-reload support
    void pause();
    void resume();
    // Two-phase hot reload: destroy old instances while old dylib is still loaded,
    // then create new instances after the dylib swap.
    // Call sequence: pre_reload → core.reload_operator → post_reload
    void pre_reload_operator(const std::string& type_name);
    bool post_reload_operator(const std::string& type_name, OperatorRegistry& registry);

    // Recording tap — capture the final stereo mix (call from main thread)
    void start_recording_tap();
    void stop_recording_tap();
    uint64_t available_recorded_samples() const;
    uint64_t pop_recorded_samples(float* dst, uint64_t max_samples);

    // Analysis toggle (main thread)
    void set_analysis_enabled(bool enabled);

    uint32_t underrun_count() const;
    bool last_buffer_underrun() const;
    float audio_load() const;
    uint32_t late_delivery_count() const;
    uint32_t max_delivery_gap_us() const;
    uint32_t node_count() const;
    uint32_t buffer_size() const;
    uint32_t sample_rate() const;
    bool running() const;

    // Test-only accessors used by headless audio integration tests.
    void  process_audio_for_test(float* output, uint32_t frame_count);

    static constexpr uint32_t kSampleRate = 48000;

private:
    // Saved state for two-phase reload (between pre_reload and post_reload)
    struct ReloadSavedNode {
        uint32_t node_idx;  // index into compiled_graph_->audio_order
        std::unordered_map<std::string, float> params;
    };
    std::vector<ReloadSavedNode> reload_saved_;

    // Cadence-aware runtime references (not owned)
    RuntimeCore* runtime_core_ = nullptr;
    CompiledGraph* compiled_graph_ = nullptr;
    AudioFrameBridge* audio_frame_bridge_ = nullptr;
    std::unique_ptr<AudioExecutor> audio_executor_;

    // Fallback analysis snapshot (returned when no AudioFrameBridge available)
    AnalysisSnapshot empty_analysis_;

    // Frame counter that throttles the periodic AudioDeviceList::refresh()
    // call inside tick(). At 60 fps and a threshold of 60 frames, this
    // re-enumerates devices once per second.
    uint32_t tick_frame_counter_ = 0;

    // Pending session-rate change. Set when a device opens at a rate that
    // differs from the runtime's compiled-graph rate. main.cpp drains this
    // each frame; non-zero triggers a graph recompile at the new rate.
    uint32_t pending_session_sample_rate_ = 0;
};

} // namespace vivid
