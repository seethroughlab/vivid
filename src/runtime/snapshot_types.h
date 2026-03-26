#pragma once

// Shared snapshot types for cross-cadence communication.
// Used by CadenceBridge (owns the double-buffered snapshots),
// AudioExecutor (reads/writes them on the audio thread), and
// AudioEngine (thin facade exposing analysis reads).

#include "operator_api/types.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace vivid {

struct SpreadSnapshot {
    static constexpr uint32_t kMaxLength = 64;
    float data[kMaxLength] = {};
    uint32_t length = 0;
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
    std::vector<std::vector<float>> float_input_values; // [audio_node_idx][float_input_ordinal]
    std::vector<std::vector<SpreadSnapshot>> spread_inputs; // [audio_node_idx][input_port_idx]
    std::vector<std::vector<std::string>> input_string_values; // [audio_node_idx][input_port_idx]
    std::vector<std::vector<CustomPortSnapshot>> custom_inputs; // [audio_node_idx][input_port_idx]
    std::vector<bool> solo_active_set;  // empty = no solo; [audio_node_idx] = active
};

struct AnalysisSnapshot {
    static constexpr uint32_t kWaveformSamples = 1024;
    std::vector<float> rms;   // [audio_node_idx]
    std::vector<float> peak;  // [audio_node_idx]
    std::vector<std::array<float, kWaveformSamples>> waveform; // [audio_node_idx]
    std::vector<std::vector<SpreadSnapshot>> spread_outputs; // [audio_node_idx][output_port_idx]
    std::vector<std::vector<float>> float_outputs; // [audio_node_idx][float_output_ordinal]

    // Error state propagation (audio thread → main thread)
    // Fixed-size char arrays avoid heap allocation on the audio thread.
    std::vector<bool> errored;                          // [audio_node_idx]
    std::vector<std::array<char, 256>> error_msgs;      // [audio_node_idx]
};

// Lock-free SPSC ring buffer for recording the final stereo mix.
// Audio thread writes, main thread reads. Pre-allocated, zero overhead when inactive.
struct RecordingTap {
    static constexpr uint32_t kRingSize = 960000; // 10 sec @ 48kHz stereo interleaved
    float ring[kRingSize];
    std::atomic<uint64_t> write_pos{0}; // monotonic, audio thread writes
    std::atomic<uint64_t> read_pos{0};  // monotonic, main thread writes
    std::atomic<bool> active{false};    // main thread toggles
};

// Auto-duplication group: runs a mono operator N times for N-channel wires
struct AutoDupGroup {
    uint32_t node_idx;
    uint8_t  channel_count;          // e.g. 2 for stereo
    std::vector<void*> instances;    // [channel] → operator instance (instances[0] = primary)
    // Per-instance mono buffers for process() calls
    std::vector<std::vector<std::vector<float>>> per_ch_inputs;  // [ch][port][sample]
    std::vector<std::vector<std::vector<float>>> per_ch_outputs; // [ch][port][sample]
    std::vector<std::vector<float*>> per_ch_in_ptrs;  // [ch][port]
    std::vector<std::vector<float*>> per_ch_out_ptrs;  // [ch][port]
};

} // namespace vivid
