#pragma once

#include "operator_api/types.h"
#include <string>
#include <vector>
#include <array>
#include <unordered_map>
#include <memory>

namespace vivid::ui {

// Owned copy of VividParamDescriptor (no C string pointers)
struct ParamInfo {
    std::string name;
    VividParamType type = VIVID_PARAM_FLOAT;
    float default_value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    std::vector<std::string> choice_labels;
    uint32_t choice_count = 0;
};

// Owned copy of port metadata
struct PortInfo {
    std::string name;
    VividPortType type = VIVID_PORT_CONTROL_FLOAT;
    VividPortDirection direction = VIVID_PORT_INPUT;
};

// Owned copy of operator metadata
struct OperatorInfo {
    std::string name;
    VividDomain domain = VIVID_DOMAIN_CONTROL;
    bool has_shader = false;
    bool is_user = false;
    std::vector<ParamInfo> params;
    std::vector<PortInfo> ports;
};

// Per-node snapshot data
struct NodeSnapshot {
    std::string node_id;
    std::string type_name;
    VividDomain domain = VIVID_DOMAIN_CONTROL;

    bool is_gpu = false;
    bool is_audio = false;
    bool is_gpu_sink = false;
    bool is_generator = false;  // GPU node with no texture inputs and not a sink

    std::unordered_map<std::string, uint32_t> input_port_indices;
    std::unordered_map<std::string, uint32_t> output_port_indices;
    std::unordered_map<std::string, uint32_t> param_indices;

    std::vector<float> param_values;
    std::vector<float> output_values;
    std::vector<std::vector<float>> output_spreads;
    std::unordered_map<std::string, std::string> file_param_values;  // param_name -> path

    uint32_t gpu_tex_width = 0;
    uint32_t gpu_tex_height = 0;

    // Error state (from scheduler)
    bool errored = false;
    std::string error_message;

    // Layout position from graph
    float layout_x = 0.0f;
    float layout_y = 0.0f;
    bool has_layout = false;

    // Operator metadata (shared across nodes of same type, cached across frames)
    std::shared_ptr<const OperatorInfo> op_info;
};

// Per-connection snapshot
struct ConnectionSnapshot {
    std::string from_node;
    std::string from_port;
    std::string to_node;
    std::string to_port;
    float scale = 1.0f;
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

// Complete frame snapshot — everything the UI needs to render
struct GraphSnapshot {
    std::vector<NodeSnapshot> nodes;
    std::vector<ConnectionSnapshot> connections;

    // O(1) lookup by node_id -> index in nodes vector
    std::unordered_map<std::string, size_t> node_index;

    // Audio visualization data
    std::unordered_map<std::string, int> audio_index;  // node_id -> audio engine index
    std::vector<AudioNodeAnalysis> audio_analysis;

    // Audio underrun detection
    uint32_t audio_underrun_count = 0;
    bool audio_underrun_active = false;

    // MIDI mapping data
    std::vector<MidiMappingSnapshot> midi_mappings;
    std::unordered_map<std::string, size_t> midi_mapping_index; // "node_id\tparam" -> index
    std::vector<MidiCCEventSnapshot> pending_cc_events;

    // Operator catalog for chooser popup
    std::vector<std::string> operator_types;  // sorted list
    std::unordered_map<std::string, std::shared_ptr<const OperatorInfo>> operator_catalog;

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
};

} // namespace vivid::ui
