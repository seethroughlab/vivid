#pragma once

#include "operator_api/types.h"
#include "runtime/cadence_types.h"
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

// Owned copy of VividParamDescriptor (no C string pointers)
struct ParamInfo {
    std::string name;
    VividParamType type = VIVID_PARAM_FLOAT;
    float default_value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    std::vector<std::string> choice_labels;
    uint32_t choice_count = 0;

    // Inspector layout metadata
    std::string group;
    VividDisplayHint display_hint = VIVID_DISPLAY_DEFAULT;
    uint8_t layout_columns = 0;
    uint8_t layout_column_index = 0;

    // Optional semantic metadata copied from VividParamDescriptor
    std::string semantic_tag;
    std::string semantic_shape;
    std::string semantic_unit;
    std::string semantic_intent;
};

// Owned copy of port metadata
struct PortInfo {
    std::string name;
    VividPortType type = VIVID_PORT_SIGNAL;
    VividPortDirection direction = VIVID_PORT_INPUT;
};

// Owned copy of operator metadata
struct OperatorInfo {
    std::string name;
    bool is_gpu = false;
    bool is_audio_native = false;  // has_process_audio && !has_process_frame
    bool has_shader = false;
    bool is_user = false;
    bool has_custom_inspector = false;
    uint32_t inspector_mode = 0;
    std::vector<ParamInfo> params;
    std::vector<PortInfo> ports;

};

// Per-node snapshot data
struct NodeSnapshot {
    std::string node_id;
    std::string type_name;
    Cadence active_cadence = Cadence::Frame;
    bool is_gpu = false;
    bool is_audio_capable = false;  // true if operator supports both frame and audio cadence
    CadenceOverride cadence_override = CadenceOverride::Auto;
    bool is_gpu_sink = false;
    bool is_generator = false;  // GPU node with no texture inputs and not a sink

    std::unordered_map<std::string, uint32_t> input_port_indices;
    std::unordered_map<std::string, uint32_t> output_port_indices;
    std::unordered_map<std::string, uint32_t> analysis_output_port_indices; // rms/peak/waveform
    std::unordered_map<std::string, uint32_t> param_indices;

    std::vector<float> param_values;
    std::vector<uint8_t> param_lock_flags;  // parallel to param_values
    std::vector<float> output_values;
    std::vector<std::vector<float>> output_spreads;
    std::vector<std::string> output_string_values;
    std::vector<std::vector<std::string>> output_string_spreads;
    std::unordered_map<std::string, std::string> file_param_values;  // param_name -> path

    uint32_t gpu_tex_width = 0;
    uint32_t gpu_tex_height = 0;
    bool gpu_tex_inherited = false;

    // Error state (from runtime)
    bool errored = false;
    std::string error_message;
    bool missing_operator = false;

    // Solo state (session-only)
    bool soloed = false;        // this node is the solo target
    bool solo_dimmed = false;   // solo is active and this node is NOT in the active set

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
    std::string invalid_reason;

    bool has_remap() const {
        return from_min != 0.0f || from_max != 1.0f ||
               to_min  != 0.0f || to_max  != 1.0f || clamp;
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

// Per-audio-node visualization data
struct AudioNodeAnalysis {
    float peak = 0.0f;
    static constexpr uint32_t kWaveformSamples = 1024;
    std::array<float, kWaveformSamples> waveform{};
};

// Variation info for UI
struct VariationInfo {
    std::string name;
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

    // MIDI mapping data
    std::vector<MidiMappingSnapshot> midi_mappings;
    std::unordered_map<std::string, size_t> midi_mapping_index; // "node_id\tparam" -> index
    std::vector<MidiCCEventSnapshot> pending_cc_events;

    // Operator catalog for chooser popup
    std::vector<std::string> operator_types;  // sorted list
    std::unordered_map<std::string, std::shared_ptr<const OperatorInfo>> operator_catalog;

    // WGSL preset names for filter selector UI
    std::vector<std::string> wgsl_preset_names;

    // Variation data
    std::vector<VariationInfo> variations;
    int active_variation = -1;
    bool variation_dirty = false;
    bool graph_dirty = false;
    int queued_variation = -1;
    std::string quantize_clock_node;

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

    const NodeSnapshot* find_node(const std::string& id) const {
        auto it = node_index.find(id);
        if (it == node_index.end()) return nullptr;
        return &nodes[it->second];
    }

    bool has_node(const std::string& id) const {
        return node_index.count(id) > 0;
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
