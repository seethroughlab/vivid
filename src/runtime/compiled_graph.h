#pragma once

#include "operator_api/types.h"
#include "runtime/operator_loader.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct WGPUTextureImpl;
struct WGPUTextureViewImpl;
typedef WGPUTextureImpl* WGPUTexture;
typedef WGPUTextureViewImpl* WGPUTextureView;

namespace vivid {

// ---------------------------------------------------------------------------
// Cadence — the runtime execution rate of a node.
// ---------------------------------------------------------------------------

enum class Cadence : uint8_t {
    Frame = 0,   // ~60 Hz, main thread (control + GPU)
    Audio = 1,   // ~48 kHz, audio thread
};

// ---------------------------------------------------------------------------
// EdgeTransport — how data crosses an edge.
// ---------------------------------------------------------------------------

enum class EdgeTransport : uint8_t {
    Direct,      // same-cadence: value copy during executor pass
    Snapshot,    // cross-cadence: double-buffered snapshot via CadenceBridge
};

// ---------------------------------------------------------------------------
// CompiledEdge — unified wire representation.
//
// Replaces Wire (scheduler), AudioWire, AudioFloatPortWire, AudioCustomWire,
// AudioSpreadWire, and all five CrossCadence*Wire types.
// ---------------------------------------------------------------------------

struct CompiledEdge {
    uint32_t from_node;       // index into CompiledGraph::nodes
    uint32_t from_port;       // port index (output port ordinal, or param index if targets_param)
    uint32_t to_node;         // index into CompiledGraph::nodes
    uint32_t to_port;         // port index (input port ordinal, or param index if targets_param)

    EdgeTransport transport = EdgeTransport::Direct;

    // Data type carried by this edge.
    VividPortType data_type = VIVID_PORT_SIGNAL;

    // True when the source is a param rather than an output port.
    bool sources_param = false;
    // True when the destination is a param rather than an input port.
    bool targets_param = false;

    // File/string param wiring (indexes into file_param_storage when true).
    bool sources_file_param = false;
    bool targets_file_param = false;
    uint32_t from_file_param_idx = 0;
    uint32_t to_file_param_idx = 0;

    // Audio buffer channel negotiation (only for AUDIO edges).
    uint8_t from_channels = 1;
    uint8_t to_channels = 1;

    // Remap transform.
    float from_min = 0.0f, from_max = 1.0f;
    float to_min   = 0.0f, to_max  = 1.0f;
    bool  clamp    = false;

    // Custom port metadata (for CUSTOM_VALUE / CUSTOM_REF edges).
    uint32_t custom_type_id = 0;
    VividPortTransport port_transport = VIVID_PORT_TRANSPORT_SIGNAL;
    uint32_t custom_payload_size = 0;
};

// ---------------------------------------------------------------------------
// CompiledNode — unified node state.
//
// Replaces both NodeState (scheduler) and AudioNodeState (audio engine).
// A single CompiledNode exists per graph node. The active_cadence determines
// which executor processes it.
// ---------------------------------------------------------------------------

struct CompiledNode {
    // ── Identity & lifecycle ────────────────────────────────────────────────
    std::string node_id;
    std::string type_name;
    OperatorLoader* loader = nullptr;
    void* instance = nullptr;

    // ── Cadence ─────────────────────────────────────────────────────────────
    Cadence active_cadence = Cadence::Frame;
    VividCadenceCapability cadence_capability = VIVID_CADENCE_FRAME_ONLY;

    // ── Port configuration (set once at compile time) ───────────────────────
    uint32_t input_port_count = 0;
    uint32_t output_port_count = 0;
    std::vector<VividPortType> input_port_types;
    std::vector<VividPortType> output_port_types;
    std::unordered_map<std::string, uint32_t> input_port_indices;
    std::unordered_map<std::string, uint32_t> output_port_indices;
    std::unordered_map<std::string, uint32_t> param_indices;

    // Analysis output port indices (audio nodes: rms, peak, waveform).
    std::unordered_map<std::string, uint32_t> analysis_output_port_indices;

    // ── Scalar state (params, inputs, outputs) ──────────────────────────────
    std::vector<float> param_values;
    std::vector<uint8_t> param_lock_flags;
    std::vector<float> input_values;
    std::vector<float> output_values;
    std::vector<float> prev_output_values;

    // ── String state ────────────────────────────────────────────────────────
    std::vector<std::string> input_string_values;
    std::vector<std::string> output_string_values;
    std::vector<const char*> c_input_string_values;
    std::vector<const char*> c_output_string_values;

    // ── Spread state ────────────────────────────────────────────────────────
    std::vector<std::vector<float>> input_spreads;
    std::vector<std::vector<float>> output_spreads;
    std::vector<std::vector<std::string>> input_string_spreads;
    std::vector<std::vector<std::string>> output_string_spreads;

    // Pre-allocated staging buffers for VividProcessContext / VividFrameContext.
    std::vector<VividSpreadPort> c_in_spreads;
    std::vector<VividSpreadPort> c_out_spreads;
    std::vector<std::vector<float>> out_spread_buf;
    std::vector<VividStringSpreadPort> c_in_string_spreads;
    std::vector<VividStringSpreadPort> c_out_string_spreads;
    std::vector<std::vector<const char*>> in_string_spread_ptrs;
    std::vector<std::vector<const char*>> out_string_spread_ptr_buf;

    // ── Custom ports ────────────────────────────────────────────────────────
    std::vector<uint32_t> custom_input_port_indices;
    std::vector<void*> resolved_custom_inputs;
    std::vector<uint32_t> custom_output_port_indices;
    std::vector<void*> custom_outputs;
    std::vector<void*> custom_output_buf;

    // Audio-cadence custom ports.
    std::vector<void*> custom_output_ptrs;
    uint32_t custom_output_count_audio = 0;
    bool has_custom_output_ports_audio = false;

    // ── File (string) params ────────────────────────────────────────────────
    std::vector<std::string> file_param_storage;
    std::vector<const char*> file_param_ptrs;
    std::unordered_map<std::string, uint32_t> file_param_indices;
    std::vector<uint8_t> file_param_is_path;

    // ── Generation-based cooking ────────────────────────────────────────────
    bool time_dependent = false;
    bool is_gpu = false;
    uint64_t generation = 0;
    uint64_t last_processed_gen = 0;
    bool processed_this_tick = false;
    std::vector<uint32_t> upstream_nodes;  // indices of nodes feeding into this one

    // ── Audio-specific state (allocated only when active_cadence == Audio) ──
    std::vector<std::vector<float>> audio_buffers_in;   // [port][sample]
    std::vector<std::vector<float>> audio_buffers_out;  // [port][sample]
    std::vector<float*> audio_in_ptrs;
    std::vector<float*> audio_out_ptrs;

    // Multi-channel negotiation.
    std::vector<uint8_t> input_channel_counts;
    std::vector<uint8_t> output_channel_counts;
    std::vector<uint8_t> descriptor_input_channels;
    std::vector<uint8_t> descriptor_output_channels;
    bool is_mono_autodup = false;

    // Float CV inputs (cross-cadence bridge for audio nodes).
    std::vector<float> float_input_defaults;
    std::vector<float> float_input_values;
    uint32_t float_input_count = 0;

    // Float outputs (audio SIGNAL output ports → scalar values).
    std::vector<float> float_output_values;
    uint32_t float_output_count = 0;

    // SIGNAL output auto-extraction mapping.
    struct SignalOutputMapping { uint32_t port_idx; uint32_t float_ordinal; };
    std::vector<SignalOutputMapping> signal_output_extractions;

    // Audio spread bridging.
    bool has_spread_ports = false;
    bool has_string_input_ports = false;
    bool has_custom_input_ports = false;

    // Defensive scratch buffers.
    static constexpr uint32_t kScratchFloats = 8;
    float float_output_scratch[kScratchFloats] = {};
    float float_input_scratch[kScratchFloats] = {};

    // Audio error state (fixed-size, no allocation on audio thread).
    char audio_error_message[256] = {};

    // ── GPU resources ───────────────────────────────────────────────────────
    WGPUTexture      gpu_texture      = nullptr;
    WGPUTextureView  gpu_texture_view = nullptr;
    uint32_t         gpu_tex_width    = 0;
    uint32_t         gpu_tex_height   = 0;
    bool             gpu_tex_inherited = false;
    std::vector<uint32_t> texture_input_port_indices;
    std::vector<WGPUTextureView> resolved_tex_inputs;
    std::vector<WGPUTexture>     resolved_tex_raw;
    std::vector<uint32_t>        resolved_tex_widths;
    std::vector<uint32_t>        resolved_tex_heights;
    bool is_gpu_sink = false;

    std::vector<int32_t>         aux_texture_output_port_indices;
    std::vector<WGPUTexture>     aux_gpu_textures;
    std::vector<WGPUTextureView> aux_gpu_texture_views;

    // ── Misc ────────────────────────────────────────────────────────────────
    std::vector<uint32_t> string_input_port_indices;
    std::vector<uint32_t> string_spread_input_port_indices;
    bool has_texture_output = false;
    bool has_string_output = false;
    bool has_string_spread_output = false;

    bool errored = false;
    std::string error_message;
    bool gpu_shader_error = false;
    std::string gpu_shader_error_msg;

    // Per-instance loader for WGSLFilter nodes.
    std::unique_ptr<OperatorLoader> owned_loader;

    bool missing_operator = false;
};

// ---------------------------------------------------------------------------
// CompiledGraph — the compiled, ready-to-execute graph.
//
// Built once by GraphCompiler from a Graph + OperatorRegistry.
// Shared (read) by FrameExecutor and AudioExecutor.
// ---------------------------------------------------------------------------

struct CompiledGraph {
    std::vector<CompiledNode> nodes;
    std::vector<CompiledEdge> edges;

    // Execution orders — indices into nodes[], topologically sorted.
    std::vector<uint32_t> frame_order;    // frame-rate + GPU nodes
    std::vector<uint32_t> audio_order;    // audio-rate nodes

    // Snapshot edges — subsets of edges[] with transport == Snapshot.
    std::vector<uint32_t> frame_to_audio_edges;  // indices into edges[]
    std::vector<uint32_t> audio_to_frame_edges;  // indices into edges[]

    // Direct edges partitioned by cadence — subsets of edges[] with transport == Direct.
    std::vector<uint32_t> frame_direct_edges;    // indices into edges[]
    std::vector<uint32_t> audio_direct_edges;    // indices into edges[]

    // Lookup.
    std::unordered_map<std::string, uint32_t> node_id_to_index;

    const CompiledNode* find_node(const std::string& id) const {
        auto it = node_id_to_index.find(id);
        return it != node_id_to_index.end() ? &nodes[it->second] : nullptr;
    }
    CompiledNode* find_node(const std::string& id) {
        auto it = node_id_to_index.find(id);
        return it != node_id_to_index.end() ? &nodes[it->second] : nullptr;
    }
};

} // namespace vivid
