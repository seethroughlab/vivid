#pragma once

#include "runtime/graph/compiled_graph.h"
#include "runtime/graph/snapshot_types.h"
#include <atomic>
#include <array>
#include <vector>

namespace vivid {

// ---------------------------------------------------------------------------
// AudioFrameBridge — double-buffered snapshot bridge between frame and audio
// execution worlds.
//
// Main thread calls push_to_audio() after FrameExecutor::tick() to snapshot
// frame-rate outputs for audio consumption, and pull_from_audio() before tick
// to inject audio analysis results into frame-rate nodes.
//
// Audio thread reads active_params() and writes via analysis_write_buffer()
// + publish_analysis().
// ---------------------------------------------------------------------------

class AudioFrameBridge {
public:
    // Allocate snapshot buffers sized to the audio node count in the compiled graph.
    void build(const CompiledGraph& cg);

    // ── Main thread operations ──────────────────────────────────────────────

    // Update audio nodes' param_values for inspector display (main thread).
    // Propagates control→audio and audio→audio param wires onto CompiledNode
    // so that the inspector shows modulated values. Called before push_to_audio().
    void propagate_audio_display_params(CompiledGraph& cg);

    // Snapshot frame-rate outputs into ParamSnapshot for audio consumption.
    void push_to_audio(const CompiledGraph& cg);

    // Read audio analysis snapshot and inject into frame-rate nodes.
    void pull_from_audio(CompiledGraph& cg);

    // ── Audio thread operations (called by AudioExecutor) ───────────────────

    // Read the active param snapshot (lock-free, acquire semantics).
    const ParamSnapshot& active_params() const {
        return snapshots_[param_active_.load(std::memory_order_acquire)];
    }

    // Read the active analysis snapshot (main thread, acquire semantics).
    const AnalysisSnapshot& active_analysis() const {
        return analysis_snapshots_[analysis_active_.load(std::memory_order_acquire)];
    }

    // Get the inactive analysis snapshot buffer for writing.
    AnalysisSnapshot& analysis_write_buffer() {
        return analysis_snapshots_[1 - analysis_active_.load(std::memory_order_acquire)];
    }

    // Publish the analysis snapshot (release semantics).
    void publish_analysis() {
        int write_idx = 1 - analysis_active_.load(std::memory_order_acquire);
        analysis_active_.store(write_idx, std::memory_order_release);
    }

    // ── Solo state (readable by audio thread via snapshot) ──────────────────

    void set_solo_active_set(const std::vector<bool>& set);

    // ── Diagnostics ─────────────────────────────────────────────────────────

    // Cumulative count of lane-slot overflows since build(). Rate-limited
    // counter incremented on the main thread when a snapshot would exceed
    // the pre-allocated lane storage; surfaced via runtime_health.
    uint32_t lane_overflow_count() const { return lane_overflow_count_; }

private:
    // Double-buffered param bridge (frame → audio)
    ParamSnapshot snapshots_[2];
    std::atomic<int> param_active_{0};

    // Double-buffered analysis bridge (audio → frame)
    AnalysisSnapshot analysis_snapshots_[2];
    std::atomic<int> analysis_active_{0};

    // Pre-allocated flat backing storage for bridge lane slots.
    // Two buffers per direction (one per double-buffer slot).
    std::vector<float> lane_input_storage_[2];   // frame→audio
    std::vector<float> lane_output_storage_[2];  // audio→frame
    uint32_t lane_overflow_count_ = 0;           // rate-limited diagnostic counter

    // Mapping from CompiledGraph node index → snapshot array index.
    // Indexed by graph node index; -1 for non-audio nodes.
    std::vector<int32_t> node_to_snapshot_idx_;

    // Analysis port mappings (audio node → analysis output ports in CompiledNode)
    struct AnalysisMapping {
        uint32_t graph_node_idx;     // index in CompiledGraph::nodes
        uint32_t snapshot_idx;       // index in snapshot arrays
        uint32_t rms_port_idx;       // output port index for rms
        uint32_t peak_port_idx;      // output port index for peak
        uint32_t waveform_port_idx;  // output port index for waveform
    };
    std::vector<AnalysisMapping> analysis_mappings_;
};

} // namespace vivid
