#pragma once

#include "operator_api/types.h"
#include "runtime/cadence_types.h"
#include "runtime/operator_loader.h"
#include "runtime/gpu_frame_analysis.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct WGPUTextureImpl;
struct WGPUTextureViewImpl;
typedef WGPUTextureImpl* WGPUTexture;
typedef WGPUTextureViewImpl* WGPUTextureView;

namespace vivid {

// ---------------------------------------------------------------------------
// ParamLockFlags — per-param lock bits (session-only, not serialized).
// ---------------------------------------------------------------------------

enum ParamLockFlags : uint8_t {
    PARAM_LOCK_NONE    = 0,
    PARAM_LOCK_WIRES   = 1,
    PARAM_LOCK_PRESETS = 2,
    PARAM_LOCK_ALL     = 3,
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
// Replaces Wire, AudioWire, AudioFloatPortWire, AudioCustomWire,
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

    // Precomputed SIGNAL ordinals — the index of from_port/to_port among
    // VIVID_PORT_SIGNAL ports only.  Set by GraphCompiler for SIGNAL edges.
    uint32_t from_signal_ordinal = 0;
    uint32_t to_signal_ordinal = 0;

    // Remap transform.
    float from_min = 0.0f, from_max = 1.0f;
    float to_min   = 0.0f, to_max  = 1.0f;
    bool  clamp    = false;

    // Custom port metadata (for CUSTOM_VALUE / CUSTOM_REF edges).
    uint32_t custom_type_id = 0;
    VividPortTransport port_transport = VIVID_PORT_TRANSPORT_SIGNAL;
    uint32_t custom_payload_size = 0;

    // Remap helpers.
    bool has_remap() const {
        return from_min != 0.0f || from_max != 1.0f ||
               to_min  != 0.0f || to_max  != 1.0f || clamp;
    }

    float apply_remap(float val) const {
        float range = from_max - from_min;
        float t = (range != 0.0f) ? (val - from_min) / range : 0.5f;
        float out = to_min + t * (to_max - to_min);
        if (clamp) {
            float lo = std::min(to_min, to_max);
            float hi = std::max(to_min, to_max);
            out = std::max(lo, std::min(hi, out));
        }
        return out;
    }

    // Scale factor for snapshot edges (simplified remap for cross-cadence paths).
    float remap_scale() const {
        float range = from_max - from_min;
        return (range != 0.0f) ? (to_max - to_min) / range : 1.0f;
    }
};

// ---------------------------------------------------------------------------
// AudioNodeState — audio-specific state, allocated only for audio-cadence nodes.
// ---------------------------------------------------------------------------

struct AudioNodeState {
    // Audio buffers [port][sample * channels]
    std::vector<std::vector<float>> buffers_in;
    std::vector<std::vector<float>> buffers_out;
    std::vector<float*> in_ptrs;
    std::vector<float*> out_ptrs;

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

    // Audio spread/string/custom bridging flags.
    bool has_spread_ports = false;
    bool has_string_input_ports = false;
    bool has_custom_input_ports = false;

    // Defensive scratch buffers.
    static constexpr uint32_t kScratchFloats = 8;
    float float_output_scratch[kScratchFloats] = {};
    float float_input_scratch[kScratchFloats] = {};

    // Audio error state (fixed-size, no allocation on audio thread).
    char error_message[256] = {};

    // Audio-cadence custom ports.
    std::vector<void*> custom_output_ptrs;
    uint32_t custom_output_count = 0;
    bool has_custom_output_ports = false;

    // Analysis output port indices (rms, peak, waveform).
    std::unordered_map<std::string, uint32_t> analysis_output_port_indices;
};

// ---------------------------------------------------------------------------
// GpuNodeState — GPU-specific state, allocated only for GPU nodes.
// ---------------------------------------------------------------------------

struct GpuNodeState {
    WGPUTexture      texture      = nullptr;
    WGPUTextureView  texture_view = nullptr;
    uint32_t         tex_width    = 0;
    uint32_t         tex_height   = 0;
    bool             tex_inherited = false;

    std::vector<uint32_t> texture_input_port_indices;
    std::vector<WGPUTextureView> resolved_tex_inputs;
    std::vector<WGPUTexture>     resolved_tex_raw;
    std::vector<uint32_t>        resolved_tex_widths;
    std::vector<uint32_t>        resolved_tex_heights;
    bool is_sink = false;

    std::vector<int32_t>         aux_texture_output_port_indices;
    std::vector<WGPUTexture>     aux_gpu_textures;
    std::vector<WGPUTextureView> aux_gpu_texture_views;

    bool has_texture_output = false;
    bool shader_error = false;
    std::string shader_error_msg;

    // Runtime-injected GPU analysis output port indices.
    // Populated during compilation; values written by the FrameExecutor.
    uint32_t analysis_frame_hash_idx  = UINT32_MAX;
    uint32_t analysis_brightness_idx  = UINT32_MAX;
    uint32_t analysis_contrast_idx    = UINT32_MAX;
    uint32_t analysis_dominant_hue_idx = UINT32_MAX;

    // Per-node frame analysis (lazily initialized on first GPU tick).
    std::unique_ptr<GpuFrameAnalysis> frame_analysis;
};

// ---------------------------------------------------------------------------
// CompiledNode — unified node state.
//
// A single CompiledNode exists per graph node. The active_cadence determines
// which executor processes it. Audio and GPU state are factored into optional
// sub-structs that exist only for the appropriate cadence/env.
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
    CadenceOverride original_cadence_override = CadenceOverride::Auto;

    // ── Port configuration (set once at compile time) ───────────────────────
    uint32_t input_port_count = 0;
    uint32_t output_port_count = 0;
    std::vector<VividPortType> input_port_types;
    std::vector<VividPortType> output_port_types;
    std::unordered_map<std::string, uint32_t> input_port_indices;
    std::unordered_map<std::string, uint32_t> output_port_indices;
    std::unordered_map<std::string, uint32_t> param_indices;

    // ── Scalar state (params, inputs, outputs) ──────────────────────────────
    std::vector<float> param_values;
    std::vector<uint8_t> param_lock_flags;
    std::vector<float> input_values;
    std::vector<float> output_values;

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

    // Pre-allocated staging buffers for VividFrameContext.
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

    // ── File (string) params ────────────────────────────────────────────────
    std::vector<std::string> file_param_storage;
    std::vector<const char*> file_param_ptrs;
    std::unordered_map<std::string, uint32_t> file_param_indices;
    std::vector<uint8_t> file_param_is_path;

    // ── Frame-rate skip logic ────────────────────────────────────────────────
    bool time_dependent = false;
    bool dirty = false;              // set by out-of-band changes (bridge, API, reload)
    bool processed_this_tick = false;
    std::vector<uint32_t> upstream_nodes;  // indices of nodes feeding into this one

    // ── Cadence-specific state (at most one is non-null) ─────────────────────
    std::unique_ptr<AudioNodeState> audio;  // present iff active_cadence == Audio
    std::unique_ptr<GpuNodeState>   gpu;    // present iff node has GPU processing

    // ── Convenience queries ─────────────────────────────────────────────────
    bool is_gpu() const { return gpu != nullptr; }
    bool is_gpu_sink() const { return gpu && gpu->is_sink; }
    bool has_texture_output() const { return gpu && gpu->has_texture_output; }

    // ── Subgraph module membership ─────────────────────────────────────────
    std::string subgraph_owner;  // empty = top-level node
    std::string subgraph_type;   // module type name

    // ── Misc ────────────────────────────────────────────────────────────────
    std::vector<uint32_t> string_input_port_indices;
    std::vector<uint32_t> string_spread_input_port_indices;
    bool has_string_output = false;
    bool has_string_spread_output = false;

    bool errored = false;
    std::string error_message;

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

    // Check if any audio-cadence node instances of a given type exist.
    // Scans only audio_order (typically 2-5 nodes), not all nodes.
    bool has_audio_cadence_instances(const std::string& type_name) const {
        for (uint32_t ni : audio_order) {
            if (nodes[ni].type_name == type_name) return true;
        }
        return false;
    }
};

} // namespace vivid
