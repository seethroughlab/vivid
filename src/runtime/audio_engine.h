#pragma once

#include "runtime/graph.h"
#include "runtime/operator_registry.h"
#include "operator_api/audio_operator.h"
#include "operator_api/media_clock.h"
#include "operator_api/media_stream.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <unordered_map>

struct ma_device;

namespace vivid {

class Scheduler;

struct SpreadSnapshot {
    static constexpr uint32_t kMaxLength = 64;
    float data[kMaxLength] = {};
    uint32_t length = 0;
};

struct AudioNodeState {
    // --- Identity ---
    std::string node_id;
    OperatorLoader* loader;
    void* instance;

    // --- Port config ---
    uint32_t input_port_count;
    uint32_t output_port_count;
    uint32_t param_count;
    std::unordered_map<std::string, uint32_t> input_port_indices;
    std::unordered_map<std::string, uint32_t> output_port_indices;
    std::unordered_map<std::string, uint32_t> param_indices;
    std::vector<VividPortType> input_port_types;
    std::vector<VividPortType> output_port_types;
    bool has_spread_ports = false;
    bool has_string_input_ports = false;
    bool has_handle_input_ports = false;

    // Multi-channel negotiation (resolved during build())
    std::vector<uint8_t> input_channel_counts;   // resolved per input port
    std::vector<uint8_t> output_channel_counts;  // resolved per output port
    std::vector<uint8_t> descriptor_input_channels;  // from VividPortDescriptor
    std::vector<uint8_t> descriptor_output_channels; // from VividPortDescriptor
    bool is_mono_autodup = false;  // all declared ports mono, but wired to multi-channel

    // --- Per-tick buffers (pre-allocated, no audio-thread allocation) ---
    std::vector<float> param_values;
    std::vector<std::vector<float>> input_buffers;   // [port][sample]
    std::vector<std::vector<float>> output_buffers;  // [port][sample]
    std::vector<float*> in_ptrs;
    std::vector<float*> out_ptrs;

    // --- Spread / string / data inputs (cross-domain bridge) ---
    std::vector<SpreadSnapshot> spread_inputs;       // [input_port_idx]
    std::vector<SpreadSnapshot> spread_outputs;      // [output_port_idx]
    std::vector<VividSpreadPort> spread_in_ports;    // pre-allocated for process ctx
    std::vector<VividSpreadPort> spread_out_ports;
    std::vector<std::string> input_string_values;    // [input_port_idx]
    std::vector<const char*> c_input_string_values;  // [input_port_idx]
    std::vector<void*> input_handle_values;            // [input_port_idx]

    // --- Error state (audio thread only; propagated via AnalysisSnapshot) ---
    bool errored = false;
    char error_message[256] = {};  // fixed-size, no allocation on audio thread
};

// Wire within the audio subgraph (audio output → audio input)
struct AudioWire {
    uint32_t from_node_idx, from_port_idx;
    uint32_t to_node_idx, to_port_idx;
    float scale = 1.0f;
    uint8_t from_channels = 1;  // resolved source channel count
    uint8_t to_channels = 1;    // resolved destination channel count
};

// Wire for CONTROL_SPREAD data between audio-domain nodes
struct AudioSpreadWire {
    uint32_t from_node_idx, from_port_idx;
    uint32_t to_node_idx, to_port_idx;
    float scale = 1.0f;
};

// Cross-domain wire: control output → audio param
struct CrossDomainWire {
    std::string control_node_id;
    uint32_t control_output_port_idx;
    uint32_t audio_node_idx;
    uint32_t audio_param_idx;
    float scale = 1.0f;
};

// Cross-domain wire: control spread output → audio spread input
struct CrossDomainSpreadWire {
    std::string control_node_id;
    uint32_t control_spread_port_idx;   // scheduler output port index
    uint32_t audio_node_idx;
    uint32_t audio_port_idx;            // unified input port index
    float scale = 1.0f;
    mutable bool truncation_warned = false;
};

// Cross-domain wire: control string output -> audio string input port
struct CrossDomainStringWire {
    std::string control_node_id;
    uint32_t control_output_port_idx;  // scheduler output port index
    uint32_t audio_node_idx;
    uint32_t audio_port_idx;           // unified input port index
};

struct CrossDomainHandleWire {
    std::string source_node_id;
    uint32_t source_output_port_idx;
    uint32_t audio_node_idx;
    uint32_t audio_port_idx;
    uint32_t handle_type_id = 0;  // from VividPortDescriptor, for snapshot sizing
};

struct HandleInputSnapshot {
    bool valid = false;
    uint32_t handle_type_id = 0;
    uint32_t byte_size = 0;
    static constexpr uint32_t kMaxBytes = 256;
    uint8_t bytes[kMaxBytes] = {};

    template <typename T>
    void set(uint32_t type_id, const T& value) {
        valid = true;
        handle_type_id = type_id;
        byte_size = static_cast<uint32_t>(sizeof(T));
        static_assert(sizeof(T) <= kMaxBytes, "HandleInputSnapshot payload too large");
        std::memcpy(bytes, &value, sizeof(T));
    }

    void clear() {
        valid = false;
        handle_type_id = 0;
        byte_size = 0;
    }
};

struct ParamSnapshot {
    std::vector<std::vector<float>> node_params;  // [audio_node_idx][param_idx]
    std::vector<std::vector<SpreadSnapshot>> spread_inputs; // [audio_node_idx][input_port_idx]
    std::vector<std::vector<std::string>> input_string_values; // [audio_node_idx][input_port_idx]
    std::vector<std::vector<HandleInputSnapshot>> handle_inputs; // [audio_node_idx][input_port_idx]
    std::vector<bool> solo_active_set;  // empty = no solo; [audio_node_idx] = active
};

struct AnalysisSnapshot {
    static constexpr uint32_t kWaveformSamples = 1024;
    std::vector<float> rms;   // [audio_node_idx]
    std::vector<float> peak;  // [audio_node_idx]
    std::vector<std::array<float, kWaveformSamples>> waveform; // [audio_node_idx]
    std::vector<std::vector<SpreadSnapshot>> spread_outputs; // [audio_node_idx][output_port_idx]

    // Error state propagation (audio thread → main thread)
    // Fixed-size char arrays avoid heap allocation on the audio thread.
    std::vector<bool> errored;                          // [audio_node_idx]
    std::vector<std::array<char, 256>> error_msgs;      // [audio_node_idx]
};

struct AudioToControlMapping {
    uint32_t audio_engine_idx;    // index in AudioEngine::nodes_
    uint32_t scheduler_node_idx;  // index in Scheduler::nodes_
    uint32_t rms_port_idx;        // "rms" output port index in scheduler
    uint32_t peak_port_idx;       // "peak" output port index in scheduler
    uint32_t waveform_port_idx;   // "waveform" output port index in scheduler

    // Spread output mappings: audio spread output → scheduler spread output
    struct SpreadOutputMapping {
        uint32_t audio_port_idx;      // output port in audio engine node
        uint32_t scheduler_port_idx;  // output port in scheduler node
    };
    std::vector<SpreadOutputMapping> spread_output_mappings;
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

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool build(const Graph& graph, OperatorRegistry& registry, const Scheduler& scheduler);
    bool start(bool use_null_device = false);
    void push_params(const Scheduler& scheduler);
    void update_sources(double time, const Scheduler& scheduler);
    void inject_analysis(Scheduler& scheduler);
    void shutdown();

    // Read active analysis snapshot (call from main thread)
    const AnalysisSnapshot& analysis_read() const;
    // Map node_id to audio engine index (-1 if not found)
    int audio_node_index(const std::string& node_id) const;

    // Hot-reload support
    void pause();
    void resume();
    bool reload_operator(const std::string& type_name, OperatorRegistry& registry);

    // Recording tap — capture the final stereo mix (call from main thread)
    void start_recording_tap();
    void stop_recording_tap();
    uint64_t available_recorded_samples() const;
    uint64_t pop_recorded_samples(float* dst, uint64_t max_samples);

    uint32_t underrun_count() const { return underrun_count_.load(std::memory_order_relaxed); }
    bool last_buffer_underrun() const { return last_buffer_underrun_.load(std::memory_order_relaxed); }

    static constexpr uint32_t kBufferSize = 256;
    static constexpr uint32_t kSampleRate = 48000;

private:
    // Called from the audio thread
    void audio_callback(float* output, uint32_t frame_count);
    static void ma_data_callback(struct ma_device* device, void* output, const void* input, unsigned int frame_count);

    void init_audio_node_state(AudioNodeState& ns, const VividOperatorDescriptor* desc,
                               const std::unordered_map<std::string, float>* param_overrides);

    std::vector<AudioNodeState> nodes_;
    std::vector<AudioWire> wires_;
    std::vector<AudioSpreadWire> audio_spread_wires_;
    std::vector<CrossDomainWire> cross_wires_;
    std::vector<CrossDomainSpreadWire> cross_spread_wires_;
    std::vector<CrossDomainStringWire> cross_string_wires_;
    std::vector<CrossDomainHandleWire> cross_handle_wires_;

    // Double-buffered param bridge (control→audio)
    ParamSnapshot snapshots_[2];
    std::atomic<int> active_{0};

    // Double-buffered analysis bridge (audio→control)
    AnalysisSnapshot analysis_snapshots_[2];
    std::atomic<int> analysis_active_{0};
    std::vector<AudioToControlMapping> analysis_mappings_;

    // Node ID → audio engine index mapping (built during build())
    std::unordered_map<std::string, int> node_id_to_index_;

    // Per-node ring buffer for raw waveform sample accumulation
    std::vector<std::array<float, 1024>> waveform_rings_;
    std::vector<uint32_t> waveform_ring_pos_;

    // Auto-duplication for mono operators in multi-channel chains
    std::vector<AutoDupGroup> auto_dup_groups_;
    std::unordered_map<uint32_t, uint32_t> node_to_dup_group_;

    // Sink node (audio_out or last node with outputs as fallback)
    int sink_node_idx_ = -1;

    // Audio time tracking
    uint64_t audio_frame_ = 0;

    // miniaudio device (opaque pointer to avoid including miniaudio.h in header)
    ma_device* device_ = nullptr;
    bool running_ = false;

    // Underrun detection
    std::atomic<uint32_t> underrun_count_{0};
    std::atomic<bool> last_buffer_underrun_{false};

    // Recording tap (stereo mix capture)
    RecordingTap recording_tap_;
    uint32_t recording_overrun_count_ = 0;  // audio thread only
};

} // namespace vivid
