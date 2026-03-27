#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include <nlohmann/json.hpp>
#include "runtime/cadence_types.h"

// Bumped when the graph JSON format changes in a backward-incompatible way.
// Graphs saved with schema_version > GRAPH_SCHEMA_VERSION are hard-rejected.
#define GRAPH_SCHEMA_VERSION 2

namespace vivid {

struct NodeDef {
    std::string id;
    std::string type;
    std::unordered_map<std::string, float> params;
    std::unordered_map<std::string, std::string> string_params;
    // Optional layout position (NaN = use auto-layout)
    float layout_x = NAN;
    float layout_y = NAN;
    bool has_layout() const { return !std::isnan(layout_x); }

    // Per-node GPU texture resolution (0 = inherit or default 800x600)
    uint32_t tex_width  = 0;
    uint32_t tex_height = 0;

    CadenceOverride cadence_override = CadenceOverride::Auto;

    // Per-parameter lock flags (sparse — only non-zero entries stored)
    std::unordered_map<std::string, uint8_t> param_lock_flags;

    // Package provenance (empty for core operators)
    std::string pkg_name;
    std::string pkg_version;
};

struct ConnectionDef {
    std::string from_node, from_port;
    std::string to_node, to_port;
    float from_min = 0.0f, from_max = 1.0f;
    float to_min   = 0.0f, to_max  = 1.0f;
    bool  clamp    = false;

    bool has_remap() const {
        return from_min != 0.0f || from_max != 1.0f ||
               to_min  != 0.0f || to_max  != 1.0f || clamp;
    }
};

struct FilterDef {
    std::string name;
    std::string source;         // which built-in it was copied from
    bool time_dependent = false;
    struct ParamDef {
        std::string name;
        float default_value = 0.0f;
        float min_value = 0.0f;
        float max_value = 1.0f;
    };
    std::vector<ParamDef> params;
    std::string shader;         // WGSL source (inline in JSON)
};

struct MidiMappingDef {
    std::string node_id;
    std::string param_name;
    int cc_number = 0;        // 0-127
    int channel = 0;          // 0 = omni, 1-16 = specific
    float range_min = 0.0f;
    float range_max = 1.0f;
};

struct VariationDef {
    std::string name;
    // node_id -> { param_name -> value }
    std::unordered_map<std::string, std::unordered_map<std::string, float>> params;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> string_params;
};

struct OperatorPreset {
    std::string name;
    std::unordered_map<std::string, float> params;  // param_name -> value
    std::unordered_map<std::string, std::string> string_params;  // file/string params
};

struct StatePresetMapping {
    std::string state_machine_node;  // node_id of StateMachine operator
    // Per state index: map of node_id -> preset_name
    std::vector<std::unordered_map<std::string, std::string>> state_presets;
};

struct StickyNoteDef {
    std::string id;
    std::string text;
    float x = 0.0f, y = 0.0f;
    float width = 200.0f, height = 120.0f;
    int color = 0;  // 0=yellow, 1=green, 2=blue, 3=pink, 4=orange
};

class Graph {
public:
    bool load(const char* path);
    bool load_from_string(const char* json, size_t len = 0, bool preserve_source_path = false);
    const std::vector<NodeDef>& nodes() const { return nodes_; }
    const std::vector<ConnectionDef>& connections() const { return connections_; }
    const std::vector<MidiMappingDef>& midi_mappings() const { return midi_mappings_; }
    const std::vector<FilterDef>& filters() const { return filters_; }
    const std::vector<VariationDef>& variations() const { return variations_; }
    const std::string& source_path() const { return source_path_; }
    void set_source_path(std::string path) { source_path_ = std::move(path); }

    // Mutation
    bool add_node(const std::string& id, const std::string& type,
                  const std::unordered_map<std::string, float>& params = {},
                  const std::unordered_map<std::string, std::string>& string_params = {});
    bool remove_node(const std::string& id);
    bool add_connection(const std::string& from_node, const std::string& from_port,
                        const std::string& to_node, const std::string& to_port);
    bool remove_connection(const std::string& from_node, const std::string& from_port,
                           const std::string& to_node, const std::string& to_port);
    bool set_connection_remap(const std::string& from_node, const std::string& from_port,
                              const std::string& to_node, const std::string& to_port,
                              float from_min, float from_max, float to_min, float to_max, bool clamp);

    // Filter mutation
    void add_filter(FilterDef filter);
    const FilterDef* find_filter(const std::string& name) const;
    FilterDef* find_filter(const std::string& name);
    bool remove_filter(const std::string& name);
    void update_filter_shader(const std::string& name, const std::string& source);

    // MIDI mapping mutation
    bool add_midi_mapping(const std::string& node_id, const std::string& param,
                          int cc, int channel, float range_min, float range_max);
    bool remove_midi_mapping(const std::string& node_id, const std::string& param);
    bool update_midi_mapping(const std::string& node_id, const std::string& param,
                             float range_min, float range_max);
    const MidiMappingDef* find_midi_mapping(const std::string& node_id,
                                            const std::string& param) const;

    // Variation mutation
    void add_variation(VariationDef v);
    bool remove_variation(const std::string& name);
    bool rename_variation(const std::string& old_name, const std::string& new_name);
    bool duplicate_variation(const std::string& name, const std::string& new_name);
    bool move_variation(const std::string& name, int to_index);
    const VariationDef* find_variation(const std::string& name) const;
    VariationDef* find_variation(const std::string& name);
    int find_variation_index(const std::string& name) const;

    int active_variation() const { return active_variation_; }
    void set_active_variation(int idx) { active_variation_ = idx; }
    const std::string& quantize_clock_node() const { return quantize_clock_node_; }
    void set_quantize_clock_node(const std::string& node_id) { quantize_clock_node_ = node_id; }

    // Per-operator preset CRUD
    void save_preset(const std::string& node_id, const OperatorPreset& preset);
    bool remove_preset(const std::string& node_id, const std::string& name);
    bool rename_preset(const std::string& node_id, const std::string& old_name,
                       const std::string& new_name);
    const OperatorPreset* find_preset(const std::string& node_id, const std::string& name) const;
    OperatorPreset* find_preset(const std::string& node_id, const std::string& name);
    std::vector<std::string> list_presets(const std::string& node_id) const;
    const std::unordered_map<std::string, std::vector<OperatorPreset>>& node_presets() const {
        return node_presets_;
    }

    // State-preset mapping CRUD
    void set_state_preset(const std::string& sm_node, int state_idx,
                          const std::string& target_node, const std::string& preset_name);
    bool remove_state_preset(const std::string& sm_node, int state_idx,
                             const std::string& target_node);
    void clear_state_presets(const std::string& sm_node);
    const StatePresetMapping* find_state_mapping(const std::string& sm_node) const;
    const std::vector<StatePresetMapping>& state_preset_mappings() const { return state_preset_mappings_; }

    // Sticky note CRUD
    const std::vector<StickyNoteDef>& sticky_notes() const { return sticky_notes_; }
    std::vector<StickyNoteDef>& sticky_notes_mut() { return sticky_notes_; }
    void add_sticky_note(StickyNoteDef note);
    bool remove_sticky_note(const std::string& id);
    const StickyNoteDef* find_sticky_note(const std::string& id) const;
    StickyNoteDef* find_sticky_note(const std::string& id);

    // Lookup
    const NodeDef* find_node(const std::string& id) const;
    NodeDef* find_node(const std::string& id);

    // Viewport (NaN = not set, use default)
    float viewport_pan_x = NAN;
    float viewport_pan_y = NAN;
    float viewport_zoom  = NAN;
    bool has_viewport() const { return !std::isnan(viewport_pan_x); }
    void set_viewport(float px, float py, float z) { viewport_pan_x = px; viewport_pan_y = py; viewport_zoom = z; }

    // Schema metadata (populated on load, written on save)
    int schema_version = 0;      // 0 = absent from file (treated as 1); set by parse_doc
    std::string vivid_version;   // VIVID_CORE_VERSION at save time

    // Package version diagnostics from last load
    struct LoadDiagnostic {
        std::string node_id;
        std::string pkg_name;
        std::string saved_version;
        std::string installed_version;
        std::string classification;  // "compatible_update" | "incompatible_update"
    };
    std::vector<LoadDiagnostic> load_diagnostics;

    // Mutable node access (for pre-save package annotation)
    std::vector<NodeDef>& nodes_mut() { return nodes_; }

    // Serialization
    bool save(const char* path) const;
    bool save_to_string(std::string& out_json) const;

private:
    bool parse_doc(const nlohmann::json& root);

    std::vector<NodeDef> nodes_;
    std::vector<ConnectionDef> connections_;
    std::vector<MidiMappingDef> midi_mappings_;
    std::vector<FilterDef> filters_;
    std::vector<VariationDef> variations_;
    int active_variation_ = -1;
    std::string quantize_clock_node_;
    std::string source_path_;
    std::unordered_map<std::string, std::vector<OperatorPreset>> node_presets_;
    std::vector<StatePresetMapping> state_preset_mappings_;
    std::vector<StickyNoteDef> sticky_notes_;
};

} // namespace vivid
