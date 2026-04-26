#pragma once

#include "operator_api/types.h"
#include "runtime/graph/cadence_types.h"
#include "runtime/graph/graph.h"
#include "runtime/operators/operator_loader.h"
#include "runtime/gpu/gpu_frame_analysis.h"
#include "runtime/graph/lane_types.h"
#include "runtime/graph/lane_buffer.h"
#include "runtime/graph/lane_output_adapter.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
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
    Snapshot,    // cross-cadence: double-buffered snapshot via AudioFrameBridge
};

// ---------------------------------------------------------------------------
// BridgeKind — explicit bridge semantics requested by the graph author.
// Stored on CompiledEdge so the runtime can apply the requested audio-frame
// bridge behavior at the cadence boundary.
// ---------------------------------------------------------------------------

enum class BridgeKind : uint8_t {
    None,        // no explicit bridge (default)
    Hold,        // hold last value
    Snapshot,    // snapshot at cadence boundary
    LastSample,  // last audio sample
    Rms,         // RMS reduction
    Peak,        // peak reduction
    Waveform,    // waveform summary
};

// ---------------------------------------------------------------------------
// RemapCurve — easing function applied to wire remap.
// ---------------------------------------------------------------------------

enum class RemapCurve : uint8_t {
    Linear = 0,
    Exponential,    // t²
    Logarithmic,    // √t
    EaseIn,         // t��
    EaseOut,        // 1 - (1-t)³
    EaseInOut,      // smoothstep: t²(3-2t)
    SCurve,         // t² / (t² + (1-t)²)
};

inline constexpr int kRemapCurveCount = 7;

inline const char* remap_curve_label(RemapCurve c) {
    switch (c) {
    case RemapCurve::Linear:      return "Linear";
    case RemapCurve::Exponential: return "Exponential";
    case RemapCurve::Logarithmic: return "Logarithmic";
    case RemapCurve::EaseIn:      return "Ease In";
    case RemapCurve::EaseOut:     return "Ease Out";
    case RemapCurve::EaseInOut:   return "Ease In-Out";
    case RemapCurve::SCurve:      return "S-Curve";
    }
    return "Linear";
}

inline float apply_remap_curve(RemapCurve c, float t) {
    switch (c) {
    case RemapCurve::Linear:      return t;
    case RemapCurve::Exponential: return t * t;
    case RemapCurve::Logarithmic: return std::sqrt(std::max(0.0f, t));
    case RemapCurve::EaseIn:      return t * t * t;
    case RemapCurve::EaseOut:     { float u = 1.0f - t; return 1.0f - u * u * u; }
    case RemapCurve::EaseInOut:   return t * t * (3.0f - 2.0f * t);
    case RemapCurve::SCurve:      {
        float t2 = t * t;
        float u  = 1.0f - t;
        float denom = t2 + u * u;
        return (denom > 1e-7f) ? t2 / denom : 0.5f;
    }
    }
    return t;
}

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
    BridgeKind bridge_kind = BridgeKind::None;

    // Data type carried by this edge.
    VividPortType data_type = VIVID_PORT_SCALAR;

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
    RemapCurve curve = RemapCurve::Linear;

    // Custom port metadata (for CUSTOM_VALUE / CUSTOM_REF edges).
    uint32_t custom_type_id = 0;
    VividPortTransport port_transport = VIVID_PORT_TRANSPORT_SIGNAL;
    uint32_t custom_payload_size = 0;

    // Lane metadata (populated by compiler Pass 2.6).
    uint32_t lane_set_id = 0;    // 0 = scalar
    uint32_t lane_count  = 1;

    // Remap helpers.
    bool has_remap() const {
        return from_min != 0.0f || from_max != 1.0f ||
               to_min  != 0.0f || to_max  != 1.0f || clamp ||
               curve != RemapCurve::Linear;
    }

    float apply_remap(float val) const {
        float range = from_max - from_min;
        float t = (range != 0.0f) ? (val - from_min) / range : 0.5f;
        t = apply_remap_curve(curve, t);
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
    struct AudioPortDebugTelemetry {
        std::atomic<float> last_block_peak{0.0f};
        std::atomic<uint32_t> buffer_size{0};
    };

    struct AudioNodeDebugTelemetry {
        std::atomic<uint32_t> last_block_total_us{0};
        std::atomic<uint32_t> last_process_us{0};
        std::atomic<uint32_t> ema_block_us{0};
        std::atomic<float> last_block_budget_pct{0.0f};
        std::atomic<uint32_t> last_lane_count{0};
        std::atomic<uint32_t> lane_state_entries{0};
    };

    // Audio buffers [port][sample * channels]
    std::vector<std::vector<float>> buffers_in;
    std::vector<std::vector<float>> buffers_out;
    std::vector<float*> in_ptrs;
    std::vector<float*> out_ptrs;

    // Multi-channel negotiation.
    std::vector<uint8_t> input_channel_counts;
    std::vector<uint8_t> output_channel_counts;
    std::vector<uint8_t> debug_input_channel_counts;
    std::vector<uint8_t> debug_output_channel_counts;
    std::vector<uint8_t> descriptor_input_channels;
    std::vector<uint8_t> descriptor_output_channels;
    std::vector<float> input_port_defaults;   // per-input-port default_value from descriptor
    // Lane execution strategy (selected by compiler, not operator author).
    LaneExecutionStrategy execution_strategy = LaneExecutionStrategy::Scalar;
    uint32_t lane_lift_count = 0;   // 0 = no lifting, N = lift to N lanes
    uint32_t lane_lift_set_id = 0;  // provenance of the lane set being lifted over
    int32_t lane_id_port = -1;  // lane-array port carrying identity-bearing lane_ids (-1 = positional)

    // Audio lane/string/custom bridging flags.
    bool has_lane_ports = false;
    bool has_string_input_ports = false;
    bool has_custom_input_ports = false;

    // Audio error state (fixed-size, no allocation on audio thread).
    char error_message[256] = {};

    // Audio-cadence custom ports.
    std::vector<void*> custom_output_ptrs;
    uint32_t custom_output_count = 0;
    bool has_custom_output_ports = false;

    // Analysis output port indices (rms, peak, waveform).
    std::unordered_map<std::string, uint32_t> analysis_output_port_indices;

    // Audio-thread-local copy of param_values.  The audio callback writes
    // snapshot params here instead of cn.param_values, eliminating a race
    // where the audio thread could revert a main-thread set_param write.
    std::vector<float> audio_local_params;

    // RT-safe per-audio-port telemetry for debugging/introspection.
    std::unique_ptr<AudioPortDebugTelemetry[]> input_port_debug;
    std::unique_ptr<AudioPortDebugTelemetry[]> output_port_debug;
    AudioNodeDebugTelemetry node_debug;
};

struct AudioPortDebugSnapshot {
    uint8_t channel_count = 1;
    float last_block_peak = 0.0f;
    uint32_t buffer_size = 0;
    bool active = false;
    bool valid = false;
};

struct AudioNodeDebugSnapshot {
    uint32_t last_block_total_us = 0;
    uint32_t last_process_us = 0;
    uint32_t ema_block_us = 0;
    float last_block_budget_pct = 0.0f;
    uint32_t last_lane_count = 0;
    uint32_t lane_state_entries = 0;
    bool valid = false;
};

inline AudioPortDebugSnapshot read_audio_port_debug(const AudioNodeState& a,
                                                    bool input,
                                                    uint32_t port_idx,
                                                    float active_epsilon = 1e-6f) {
    AudioPortDebugSnapshot snap;
    const auto& channel_counts = input ? a.debug_input_channel_counts : a.debug_output_channel_counts;
    const auto* telemetry = input ? a.input_port_debug.get() : a.output_port_debug.get();
    if (port_idx >= channel_counts.size() || !telemetry) return snap;

    snap.channel_count = channel_counts[port_idx];
    snap.last_block_peak = telemetry[port_idx].last_block_peak.load(std::memory_order_relaxed);
    snap.buffer_size = telemetry[port_idx].buffer_size.load(std::memory_order_relaxed);
    snap.active = snap.last_block_peak > active_epsilon;
    snap.valid = true;
    return snap;
}

inline AudioNodeDebugSnapshot read_audio_node_debug(const AudioNodeState& a) {
    AudioNodeDebugSnapshot snap;
    snap.last_block_total_us = a.node_debug.last_block_total_us.load(std::memory_order_relaxed);
    snap.last_process_us = a.node_debug.last_process_us.load(std::memory_order_relaxed);
    snap.ema_block_us = a.node_debug.ema_block_us.load(std::memory_order_relaxed);
    snap.last_block_budget_pct = a.node_debug.last_block_budget_pct.load(std::memory_order_relaxed);
    snap.last_lane_count = a.node_debug.last_lane_count.load(std::memory_order_relaxed);
    snap.lane_state_entries = a.node_debug.lane_state_entries.load(std::memory_order_relaxed);
    snap.valid = true;
    return snap;
}

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

    // ── Bypass override (GPU) ─────────────────────────────────────────────
    // When the owning node is bypassed+bypassable, the frame executor sets
    // these to the texture/view that should be exposed to downstream readers
    // in lieu of `texture` / `texture_view`. Null means no override active.
    // Downstream texture-input resolution checks these before falling back to
    // the regular `texture_view`.
    WGPUTexture      output_texture_override      = nullptr;
    WGPUTextureView  output_texture_view_override = nullptr;

    // Runtime-injected GPU analysis output port indices.
    // Populated during compilation; values written by the FrameExecutor.
    uint32_t analysis_frame_hash_idx  = UINT32_MAX;
    uint32_t analysis_brightness_idx  = UINT32_MAX;
    uint32_t analysis_contrast_idx    = UINT32_MAX;
    uint32_t analysis_dominant_hue_idx = UINT32_MAX;

    // Per-node frame analysis (lazily initialized on first GPU tick).
    std::unique_ptr<GpuFrameAnalysis> frame_analysis;

    // GPU lane promotion (Phase 4).
    std::vector<bool> lane_input_gpu_promoted;        // [input_port] true if promoted
    std::vector<WGPUBuffer> resolved_lane_gpu_bufs;   // scratch, populated per frame
    std::vector<uint32_t> resolved_lane_gpu_lengths;   // scratch, populated per frame
};

// ---------------------------------------------------------------------------
// CompiledNode — unified node state.
//
// A single CompiledNode exists per graph node. Most nodes execute on exactly
// one cadence. Mixed-domain nodes expose both audio and frame/GPU capability;
// they use separate operator instances for each cadence and share state through
// operator-owned session objects keyed by node_id.
// ---------------------------------------------------------------------------

struct CompiledNode {
    // ── Identity & lifecycle ────────────────────────────────────────────────
    std::string node_id;
    std::string type_name;
    OperatorLoader* loader = nullptr;
    void* instance = nullptr;
    void* audio_instance = nullptr;

    // ── Cadence ─────────────────────────────────────────────────────────────
    Cadence active_cadence = Cadence::Frame;
    VividOperatorKind operator_kind = VIVID_OP_CONTROL;

    // ── Port configuration (set once at compile time) ───────────────────────
    uint32_t input_port_count = 0;
    uint32_t output_port_count = 0;
    std::vector<VividPortType> input_port_types;
    std::vector<VividPortType> output_port_types;
    std::unordered_map<std::string, uint32_t> input_port_indices;
    std::unordered_map<std::string, uint32_t> output_port_indices;
    // Output ports tagged VIVID_PORT_DISPLAY_ADVANCED — inspector hides
    // them on the node body unless connected. Maps name → output_port index.
    std::unordered_map<std::string, uint32_t> advanced_output_port_indices;
    std::unordered_map<std::string, uint32_t> param_indices;

    // ── Scalar state (params, inputs, outputs) ──────────────────────────────
    std::vector<float> param_values;
    std::vector<uint8_t> param_lock_flags;
    std::vector<float> input_values;
    std::vector<float> bridge_input_values; // Audio→frame bridge-injected values (survive per-frame zeroing)
    std::vector<uint8_t> bridge_input_dirty; // 1 = bridge wrote this port since last frame
    std::vector<uint8_t> input_connected;    // 1 = port has an incoming edge (frame-direct or audio-bridge)
    std::vector<float> output_values;

    // ── String state ────────────────────────────────────────────────────────
    std::vector<std::string> input_string_values;
    std::vector<std::string> output_string_values;
    std::vector<const char*> c_input_string_values;
    std::vector<const char*> c_output_string_values;

    // ── Spread state ────────────────────────────────────────────────────────
    // Canonical lane transport (LaneBufferRef-based, zero-copy).
    std::vector<LaneBufferRef> input_lane_refs;
    std::vector<LaneBufferRef> output_lane_refs;

    // Bridge injection scratch — used by pull_from_audio for analysis/waveform
    // data injected from audio→frame. NOT the canonical lane values.
    std::vector<std::vector<float>> input_lanes;
    std::vector<std::vector<float>> output_lanes;

    // String lanes remain vector-based (not yet ref-based).
    std::vector<std::vector<std::string>> input_string_lanes;
    std::vector<std::vector<std::string>> output_string_lanes;

    // Pre-allocated staging for lane views and output builders.
    std::vector<VividLaneView> c_in_lane_views;
    std::vector<VividLaneOutput> c_out_lane_outputs;
    std::vector<LaneBuffer> out_lane_bufs;
    std::vector<VividStringLaneView> c_in_string_lane_views;
    std::vector<VividStringLaneOutput> c_out_string_lane_outputs;
    std::vector<StringLaneBuffer> out_string_lane_bufs;
    std::vector<std::vector<const char*>> in_string_lane_ptrs; // c_str() staging for input views

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
    std::unique_ptr<AudioNodeState> audio;  // present iff node has audio processing
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
    std::vector<uint32_t> string_lane_input_port_indices;
    bool has_string_output = false;
    bool has_string_lane_output = false;

    bool errored = false;
    std::string error_message;

    // ── Bypass ────────────────────────────────────────────────────────────
    // When `bypassed` is true and `bypassable` is true, executors skip
    // process_*() and pass the first input port through to the first output
    // port (same type) each tick. `bypassable` is computed at compile time
    // from the operator's port descriptors: true iff the first input port
    // type equals the first output port type.
    bool bypassed = false;
    bool bypassable = false;

    // Optional per-instance loader override.
    std::unique_ptr<OperatorLoader> owned_loader;

    bool missing_operator = false;
    std::string missing_operator_reason;  // "not_found", "not_built", "abi_mismatch", "load_failed"
    std::string missing_operator_detail;  // human-readable explanation

    // ── Lane metadata (populated by compiler Pass 2.6) ────────────────────
    LaneBehavior lane_behavior = LaneBehavior::Pointwise;
    std::vector<LaneSet> output_lane_sets;
    std::vector<LaneSet> input_lane_sets;

    // ── Frame-domain lane execution (populated by compiler Pass 4c) ──────
    LaneExecutionStrategy frame_execution_strategy = LaneExecutionStrategy::Scalar;
    int32_t frame_lane_id_port = -1;  // lane-array port with lane_ids (-1 = positional)
};

// ---------------------------------------------------------------------------
// CompiledGraph — the compiled, ready-to-execute graph.
//
// Built once by GraphCompiler from a Graph + OperatorRegistry.
// Shared (read) by FrameExecutor and AudioExecutor.
// ---------------------------------------------------------------------------

// Returns true iff a node with these declared port types is bypass-eligible
// (first declared input port type equals first declared output port type).
// Source nodes (no inputs) and asymmetric nodes are not bypassable.
inline bool is_bypass_eligible(const std::vector<VividPortType>& in_types,
                               const std::vector<VividPortType>& out_types) {
    return !in_types.empty() && !out_types.empty() && in_types[0] == out_types[0];
}

struct CompiledGraph {
    // Maximum lane elements per buffer. Runtime allocation guard — no single
    // lane buffer may exceed this count. Default 16,777,216 (16M floats = 64MB).
    uint32_t max_lane_elements = 16'777'216;

    // Persisted graph metadata snapshot kept for rebuild/load bookkeeping.
    // Live execution samples metronome state from RuntimeCore instead.
    GraphMetronomeDef metronome;
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

    // Connections dropped during compilation (port name mismatch, type mismatch, etc.)
    struct DroppedConnection {
        std::string from_node, from_port;
        std::string to_node, to_port;
        std::string reason;
    };
    std::vector<DroppedConnection> dropped_connections;

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
    // Lane-set ID allocator (0 reserved for scalar).
    uint32_t next_lane_set_id = 1;

    // Maximum lane count for LoopBased audio operators (from compiler options).
    uint32_t max_loop_lanes = 16;
    uint32_t audio_buffer_size = 256;
    uint32_t audio_sample_rate = 48000;

    bool has_audio_cadence_instances(const std::string& type_name) const {
        for (uint32_t ni : audio_order) {
            if (nodes[ni].type_name == type_name) return true;
        }
        return false;
    }
};

} // namespace vivid
