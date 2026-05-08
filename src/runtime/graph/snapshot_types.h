#pragma once

// Shared snapshot types for cross-cadence communication.
// Used by AudioFrameBridge (owns the double-buffered snapshots),
// AudioExecutor (reads/writes them on the audio thread), and
// AudioEngine (thin facade exposing analysis reads).

#include "operator_api/types.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace vivid {

// Pre-allocated bridge lane slot — replaces the old fixed 64-element BridgeLaneSlot.
// The `data` pointer is wired to a flat buffer owned by AudioFrameBridge during
// build(). Capacity is set at build time (default kDefaultLaneCapacity = 1024).
// Frame-thread output builders can grow beyond this; bridge slots are fixed.
struct BridgeLaneSlot {
    float* data = nullptr;       // points into pre-allocated bridge storage
    uint32_t length = 0;         // current valid element count
    uint32_t capacity = 0;       // pre-allocated max
    uint32_t lane_set_id = 0;    // provenance metadata across cadence boundary
};

struct CustomPortSnapshot {
    bool valid = false;
    uint32_t type_id = 0;
    VividPortTransport transport = VIVID_PORT_TRANSPORT_CUSTOM_REF;
    uint32_t byte_size = 0;
    static constexpr uint32_t kMaxBytes = 256;
    uint8_t bytes[kMaxBytes] = {};

    void clear() {
        valid = false;
        type_id = 0;
        byte_size = 0;
    }
};

struct ParamSnapshot {
    std::vector<std::vector<float>> node_params;  // [audio_node_idx][param_idx]
    std::vector<std::vector<BridgeLaneSlot>> lane_inputs; // [audio_node_idx][input_port_idx]
    std::vector<std::vector<std::string>> input_string_values; // [audio_node_idx][input_port_idx]
    std::vector<std::vector<CustomPortSnapshot>> custom_inputs; // [audio_node_idx][input_port_idx]
    std::vector<bool> solo_active_set;  // empty = no solo; [audio_node_idx] = active
};

struct AnalysisSnapshot {
    static constexpr uint32_t kWaveformSamples = 1024;
    static constexpr uint32_t kMaxWaveformChannels = 8;
    std::vector<std::array<float, kMaxWaveformChannels>> rms;   // [audio_node_idx][channel]
    std::vector<std::array<float, kMaxWaveformChannels>> peak;  // [audio_node_idx][channel]
    std::vector<std::array<std::array<float, kWaveformSamples>, kMaxWaveformChannels>> waveform; // [audio_node_idx][channel]
    std::vector<uint8_t> channel_counts; // [audio_node_idx] — actual channel count per node
    std::vector<std::vector<BridgeLaneSlot>> lane_outputs; // [audio_node_idx][output_port_idx]
    std::vector<std::vector<float>> scalar_outputs; // [audio_node_idx][output_port_idx]

    // Error state propagation (audio thread → main thread)
    // Fixed-size char arrays avoid heap allocation on the audio thread.
    std::vector<bool> errored;                          // [audio_node_idx]
    std::vector<std::array<char, 256>> error_msgs;      // [audio_node_idx]
};

// Lock-free SPSC ring buffer for recording the final stereo mix.
// Audio thread writes, main thread reads. Pre-allocated, zero overhead when inactive.
struct RecordingTap {
    static constexpr uint32_t kRingSize = 5760000; // 60 sec @ 48kHz stereo interleaved
    float ring[kRingSize];
    std::atomic<uint64_t> write_pos{0}; // monotonic, audio thread writes
    std::atomic<uint64_t> read_pos{0};  // monotonic, main thread writes
    std::atomic<bool> active{false};    // main thread toggles
};

// Lane lift group: runs a pointwise operator once per lane.
// Replaces the old AutoDupGroup (which ran mono operators per audio channel).
struct LaneLiftGroup {
    uint32_t node_idx;
    uint32_t lane_count;             // number of lanes to lift over
    uint32_t lane_set_id;            // provenance of the lane set
    std::vector<void*> instances;    // [lane] → operator instance (instances[0] = primary)
    std::vector<uint32_t> lane_ids;  // [lane] → stable identity (0 = positional)
    // Per-lane mono buffers for process() calls
    std::vector<std::vector<std::vector<float>>> per_lane_inputs;  // [lane][port][sample]
    std::vector<std::vector<std::vector<float>>> per_lane_outputs; // [lane][port][sample]
    std::vector<std::vector<float*>> per_lane_in_ptrs;  // [lane][port]
    std::vector<std::vector<float*>> per_lane_out_ptrs;  // [lane][port]
};

} // namespace vivid
