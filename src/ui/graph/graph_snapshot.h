#pragma once

#include "common/operator_label.h"
#include "operator_api/types.h"
#include "runtime/core/runtime_health.h"
#include "runtime/graph/cadence_types.h"
#include "runtime/packages/project_lockfile.h"
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace vivid::ui {

// Per-parameter lock flag constants (mirrored from compiled_graph.h for UI use)
static constexpr uint8_t kParamLockNone    = 0;
static constexpr uint8_t kParamLockWires   = 1;
static constexpr uint8_t kParamLockPresets = 2;

// Precomputed visibility condition resolved from VividParamDescriptor metadata.
// Evaluated per-frame: show param when param_values[param_index] matches values.
struct ParamVisibilityCondition {
    int32_t param_index = -1;    // index of controlling param, -1 = always visible
    VividParamVisibilityOp op = VIVID_PARAM_VIS_ALWAYS;
    std::vector<int32_t> values; // values to match (OR semantics)
};

inline bool param_visibility_matches(const ParamVisibilityCondition& cond,
                                     const std::vector<float>& param_values) {
    if (cond.op == VIVID_PARAM_VIS_ALWAYS || cond.param_index < 0)
        return true;
    if (cond.values.empty())
        return true;
    if (static_cast<size_t>(cond.param_index) >= param_values.size())
        return true;

    int32_t current = static_cast<int32_t>(param_values[cond.param_index]);
    bool match = false;
    for (int32_t v : cond.values) {
        if (current == v) {
            match = true;
            break;
        }
    }

    if (cond.op == VIVID_PARAM_VIS_EQ) return match;
    if (cond.op == VIVID_PARAM_VIS_NE) return !match;
    return true;
}

// Owned copy of VividParamDescriptor (no C string pointers)
struct ParamInfo {
    std::string name;
    VividParamType type = VIVID_PARAM_FLOAT;
    float default_value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    std::string default_string;  // default for FILE/TEXT params
    std::vector<std::string> choice_labels;
    uint32_t choice_count = 0;

    // Inspector layout metadata
    std::string group;
    VividDisplayHint display_hint = VIVID_DISPLAY_DEFAULT;
    uint8_t layout_columns = 0;
    uint8_t layout_column_index = 0;
    std::string widget_id;
    uint32_t widget_span = 0;

    // Optional semantic metadata copied from VividParamDescriptor
    std::string semantic_tag;
    std::string semantic_shape;
    std::string semantic_unit;
    std::string semantic_intent;
    std::string description;
    std::string asset_kind;
    std::string visible_when_param;
    VividParamVisibilityOp visible_when_op = VIVID_PARAM_VIS_ALWAYS;
    std::vector<int32_t> visible_when_values;

    // Repeat-group metadata (for variadic port patterns)
    std::string repeat_group;
    uint16_t    repeat_group_idx = 0;

    // Conditional visibility (resolved from descriptor metadata)
    ParamVisibilityCondition visibility;

    // Performance surface metadata (Step 5)
    std::string performance_page;
    int performance_order = -1;
    std::string performance_role;
};

inline bool param_info_visible(const ParamInfo& pd,
                               const std::vector<float>& param_values) {
    if (pd.display_hint == VIVID_DISPLAY_HIDDEN) return false;
    if (pd.display_hint == VIVID_DISPLAY_EDITOR) return false;
    return param_visibility_matches(pd.visibility, param_values);
}

inline bool param_info_run_visible(const std::vector<ParamInfo>& params,
                                   const std::vector<float>& param_values,
                                   uint32_t start, uint32_t count) {
    if (start > params.size() || count > params.size() - start)
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!param_info_visible(params[start + i], param_values))
            return false;
    }
    return true;
}

// Owned copy of port metadata
struct PortInfo {
    std::string name;
    VividPortType type = VIVID_PORT_SCALAR;
    VividPortDirection direction = VIVID_PORT_INPUT;

    // Repeat-group metadata (for variadic port patterns)
    std::string repeat_group;
    uint16_t    repeat_group_idx = 0;

    std::string description;  // human-readable tooltip shown on port hover
};

// Owned copy of operator metadata
struct OperatorInfo {
    std::string name;             // stable id (matches descriptor->name)
    std::string display_name;     // human-facing label; auto-derived from name when descriptor's is null
    std::vector<std::string> keywords;  // search hints (may be empty)
    std::string summary;          // one-line description (may be empty)
    bool is_gpu = false;
    bool has_shader = false;
    bool is_user = false;
    bool is_module = false;
    bool has_custom_inspector = false;
    uint32_t inspector_mode = 0;
    bool has_editor = false;
    std::vector<ParamInfo> params;
    std::vector<PortInfo> ports;

    // Pre-normalized strings the chooser scorer searches against. Built once
    // by OperatorInfoCache (or make_operator_info for modules); the scorer
    // never recomputes them.
    struct SearchHaystack {
        std::string display_name_norm;        // e.g. "chord progression"
        std::string id_norm;                  // raw id + space-split id, joined
        std::vector<std::string> keyword_norms;
        std::string summary_norm;
    } search;
};

// Populate OperatorInfo::search from name / display_name / keywords / summary.
// Call after those fields are set. The id_norm includes both the raw lowercase
// id ("chordprogression") and the CamelCase-split form ("chord progression"),
// joined by a space, so substring matches on either form score positive.
inline void build_search_haystack(OperatorInfo& info) {
    info.search.display_name_norm = vivid::normalize_for_search(info.display_name);
    std::string id_split = vivid::normalize_for_search(
        vivid::default_display_name(info.name));
    std::string id_raw = vivid::normalize_for_search(info.name);
    if (id_raw == id_split) {
        info.search.id_norm = id_raw;
    } else {
        info.search.id_norm = id_raw;
        info.search.id_norm.push_back(' ');
        info.search.id_norm.append(id_split);
    }
    info.search.keyword_norms.clear();
    info.search.keyword_norms.reserve(info.keywords.size());
    for (const auto& k : info.keywords)
        info.search.keyword_norms.push_back(vivid::normalize_for_search(k));
    info.search.summary_norm = vivid::normalize_for_search(info.summary);
}

// Per-node snapshot data
struct NodeSnapshot {
    std::string node_id;
    std::string type_name;

    // Subgraph module membership (empty for top-level nodes)
    bool is_subgraph_member = false;
    std::string subgraph_owner;  // instance ID of owning module node
    std::string subgraph_type;   // module type name
    bool is_module_instance = false;  // true for synthesized module-instance nodes

    Cadence active_cadence = Cadence::Frame;
    bool is_gpu = false;
    bool is_gpu_sink = false;
    bool is_generator = false;  // GPU node with no texture inputs and not a sink

    // Lane metadata (from compiler)
    uint8_t lane_behavior = 0;  // 0=Pointwise, 1=Structural, 2=Reduction, 3=Kernel

    std::unordered_map<std::string, uint32_t> input_port_indices;
    std::unordered_map<std::string, uint32_t> output_port_indices;
    std::unordered_map<std::string, uint32_t> analysis_output_port_indices; // rms/peak/waveform
    // Outputs tagged VIVID_PORT_DISPLAY_ADVANCED — per-voice synth breakouts,
    // NoteBreakout shared-control lanes, etc. Hidden on the node body unless
    // a connection lands on them (mirrors analysis-port behavior).
    std::unordered_map<std::string, uint32_t> advanced_output_port_indices;
    std::unordered_map<std::string, uint32_t> param_indices;

    std::vector<float> param_values;
    std::vector<uint8_t> param_lock_flags;  // parallel to param_values
    std::vector<float> output_values;
    std::vector<std::vector<float>> output_lanes;
    std::vector<std::string> output_string_values;
    std::vector<std::vector<std::string>> output_string_lanes;
    std::unordered_map<std::string, std::string> file_param_values;  // param_name -> path

    uint32_t gpu_tex_width = 0;
    uint32_t gpu_tex_height = 0;
    bool gpu_tex_inherited = false;

    // Error state (from runtime)
    bool errored = false;
    std::string error_message;
    bool missing_operator = false;
    // Set when missing_operator_reason == "disabled" (safe-mode crash recovery).
    // Implies missing_operator == true; UI renders an amber DISABLED badge
    // instead of the red MISSING badge.
    bool disabled_by_safe_mode = false;
    // Set when missing_operator_reason == "quarantined" (crash-history scan,
    // Phase 4).  Implies missing_operator == true; UI renders an amber
    // QUARANTINED badge — same color as DISABLED but distinct label.
    bool quarantined = false;

    // Solo state (session-only)
    bool soloed = false;        // this node is the solo target
    bool solo_dimmed = false;   // solo is active and this node is NOT in the active set

    // Bypass (persisted on NodeDef, eligibility derived from operator port descriptors)
    bool bypassed = false;
    bool bypassable = false;

    // Layout position from graph
    float layout_x = 0.0f;
    float layout_y = 0.0f;
    bool has_layout = false;

    // Per-operator presets
    std::vector<std::string> preset_names;          // ordered list of user preset names
    std::vector<std::string> factory_preset_names;   // ordered list of factory preset names
    std::string active_preset;              // currently active preset (empty = none)

    // State-preset mappings (populated only for StateMachine nodes)
    // state_index → { target_node_id → preset_name }
    std::vector<std::unordered_map<std::string, std::string>> state_preset_map;

    // Modulation assignment metadata (module instances only)
    struct ModSourceInfo { std::string name, description, shape, polarity, group; };
    struct ModDestInfo   { std::string name, description, shape, group; };
    struct ModAssignInfo { std::string source, destination; float amount; std::string polarity, curve; };
    std::vector<ModSourceInfo> mod_sources;
    std::vector<ModDestInfo> mod_destinations;
    std::vector<ModAssignInfo> mod_assignments;

    // Operator metadata (shared across nodes of same type, cached across frames)
    std::shared_ptr<const OperatorInfo> op_info;

    // O(1) param descriptor lookup by name
    const ParamInfo* find_param(const std::string& name) const {
        if (!op_info) return nullptr;
        auto it = param_indices.find(name);
        if (it == param_indices.end()) return nullptr;
        return &op_info->params[it->second];
    }
};

// Per-connection snapshot
struct ConnectionSnapshot {
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
    float from_min = 0.0f, from_max = 1.0f;
    float to_min   = 0.0f, to_max  = 1.0f;
    bool  clamp    = false;
    bool from_is_param = false;  // true if source is a param (not an output port)
    bool to_is_param   = false;  // true if destination is a param (not an input port)
    bool invalid = false;
    bool from_endpoint_missing = false;
    bool to_endpoint_missing = false;
    bool dropped = false;           // compiler rejected this connection
    std::string invalid_reason;

    // Lane metadata (from compiled edge)
    uint32_t lane_set_id = 0;
    uint32_t lane_count  = 1;
    uint32_t data_type   = VIVID_PORT_SCALAR;
    uint8_t  curve       = 0;   // RemapCurve index

    bool supports_remap() const {
        return data_type == VIVID_PORT_SCALAR ||
               data_type == VIVID_PORT_LANE_ARRAY ||
               data_type == VIVID_PORT_AUDIO_BUFFER;
    }

    bool has_remap() const {
        return from_min != 0.0f || from_max != 1.0f ||
               to_min  != 0.0f || to_max  != 1.0f || clamp || curve != 0;
    }

    // A connection remains part of graph truth even when an endpoint no longer resolves.
    // The UI should surface these as broken connections rather than dropping them.
    bool is_broken() const { return invalid; }
};

// MIDI mapping snapshot for UI
struct MidiMappingSnapshot {
    std::string node_id, param_name;
    int cc_number = 0, channel = 0;
    float range_min = 0.0f, range_max = 1.0f;
};

// MIDI CC event (for UI relay)
struct MidiCCEventSnapshot {
    int channel = 0, cc_number = 0;
    float value = 0.0f;
};

// Per-audio-node visualization data (n-channel)
struct AudioNodeAnalysis {
    static constexpr uint32_t kWaveformSamples = 1024;
    static constexpr uint32_t kMaxChannels = 8;
    uint8_t channel_count = 1;
    std::array<float, kMaxChannels> peak{};
    std::array<std::array<float, kWaveformSamples>, kMaxChannels> waveform{};
};

struct AudioHotNodeSnapshot {
    std::string node_id;
    std::string type_name;
    uint32_t last_block_total_us = 0;
    uint32_t last_process_us = 0;
    uint32_t ema_block_us = 0;
    uint32_t peak_block_us = 0;
    float last_block_budget_pct = 0.0f;
    uint32_t last_lane_count = 0;
    uint32_t lane_state_entries = 0;
};

// Per-track clip launcher: one entry per StateMachine that has state-preset mappings
struct StateMachineClipInfo {
    std::string node_id;
    std::string display_name;   // node id (used as column header)
    int         state_count  = 4;
    int         active_state = 0;
    int         queued_state = -1;  // -1 = none pending
};

// Sticky note snapshot for UI
struct StickyNoteSnapshot {
    std::string id, text;
    float x, y, width, height;
    int color;
};

// Complete frame snapshot — everything the UI needs to render
struct GraphSnapshot {
    std::vector<NodeSnapshot> nodes;
    std::vector<ConnectionSnapshot> connections;

    // O(1) lookup by node_id -> index in nodes vector
    std::unordered_map<std::string, size_t> node_index;

    // Audio visualization data
    std::unordered_map<std::string, int> audio_index;  // node_id -> audio engine index
    std::vector<AudioNodeAnalysis> audio_analysis;

    // Audio engine stats
    uint32_t audio_underrun_count = 0;
    bool audio_underrun_active = false;
    float audio_load = 0.0f;            // 0.0–1.0 callback budget usage
    uint32_t audio_sample_rate = 0;
    uint32_t audio_buffer_size = 0;
    uint32_t audio_node_count = 0;
    std::vector<AudioHotNodeSnapshot> audio_top_nodes;
    std::vector<AudioHotNodeSnapshot> audio_top_lane_state_nodes;

    // Runtime health rolled up from runtime_health::collect_summary().
    // Drives the diagnostics-panel pill and the status-bar dot.
    runtime_health::RuntimeHealthSummary runtime_health;

    // MIDI mapping data
    std::vector<MidiMappingSnapshot> midi_mappings;
    std::unordered_map<std::string, size_t> midi_mapping_index; // "node_id\tparam" -> index
    std::vector<MidiCCEventSnapshot> pending_cc_events;

    // Operator catalog for chooser popup
    std::vector<std::string> operator_types;  // sorted list
    std::unordered_map<std::string, std::shared_ptr<const OperatorInfo>> operator_catalog;

    // Session / performance data
    bool graph_dirty = false;
    std::vector<StateMachineClipInfo> clip_machines;
    std::string quantize_clock_node;
    float metronome_bpm = 120.0f;
    int metronome_beats_per_bar = 4;
    float metronome_beat_phase = 0.0f;
    float metronome_bar_phase = 0.0f;
    float metronome_beat_ms = 500.0f;

    // Sticky notes
    std::vector<StickyNoteSnapshot> sticky_notes;

    // Solo state
    std::string solo_node_id;  // empty = no solo active

    // Recording state (from CaptureCoordinator)
    bool is_recording = false;
    uint64_t recording_frame_count = 0;
    double recording_duration_sec = 0.0;

    // MCP server last-ping timestamps (steady_clock ms; 0 = never pinged)
    uint64_t mcp_main_last_ping_ms  = 0;  // "vivid" graph server
    uint64_t mcp_opdev_last_ping_ms = 0;  // "opdev" operator-dev server

    // Project lockfile status. overall = Match / empty findings when no
    // sibling vivid.lock exists or the runtime has no PackageManager set.
    // Populated by graph_snapshot_builder.cpp from RuntimeCore::lockfile_status().
    LockfileStatus lockfile_status;

    const NodeSnapshot* find_node(const std::string& id) const {
        auto it = node_index.find(id);
        if (it == node_index.end()) return nullptr;
        return &nodes[it->second];
    }

    bool has_node(const std::string& id) const {
        return node_index.count(id) > 0;
    }

    uint8_t audio_channel_count(const std::string& node_id) const {
        auto it = audio_index.find(node_id);
        if (it == audio_index.end() || it->second < 0) return 1;
        if (it->second >= static_cast<int>(audio_analysis.size())) return 1;
        return audio_analysis[it->second].channel_count;
    }

    const MidiMappingSnapshot* find_midi_mapping(const std::string& node_id,
                                                  const std::string& param) const {
        auto it = midi_mapping_index.find(node_id + "\t" + param);
        if (it == midi_mapping_index.end()) return nullptr;
        return &midi_mappings[it->second];
    }

    const ConnectionSnapshot* find_connection(const std::string& from_node,
                                              const std::string& from_port,
                                              const std::string& to_node,
                                              const std::string& to_port) const {
        for (const auto& conn : connections) {
            if (conn.from_node == from_node && conn.from_port == from_port &&
                conn.to_node == to_node && conn.to_port == to_port) {
                return &conn;
            }
        }
        return nullptr;
    }

    size_t broken_connection_count() const {
        size_t count = 0;
        for (const auto& conn : connections) {
            if (conn.is_broken()) ++count;
        }
        return count;
    }

    bool has_broken_connections() const {
        return broken_connection_count() > 0;
    }
};

} // namespace vivid::ui
