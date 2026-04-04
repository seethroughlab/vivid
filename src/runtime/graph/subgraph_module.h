#pragma once

#include "runtime/graph/graph.h"
#include "operator_api/types.h"
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace vivid {

// ---------------------------------------------------------------------------
// Subgraph module definition — parsed from .vivid-module.json
// ---------------------------------------------------------------------------

struct SubgraphPortBinding {
    std::string name;           // external port name (e.g. "output")
    VividPortType type;         // VIVID_PORT_SCALAR, _AUDIO, _SPREAD, etc.
    VividPortDirection direction;
    std::string internal_node;  // internal node ID (e.g. "mixer")
    std::string internal_port;  // internal port name (e.g. "output")
};

struct SubgraphParamBinding {
    std::string name;           // external param name (e.g. "cutoff")
    std::string internal_node;  // internal node ID (e.g. "filter")
    std::string internal_param; // internal param name (e.g. "cutoff")

    // Optional metadata overrides (absent = inherit from bound internal node or use defaults)
    std::optional<VividParamType> type;
    std::optional<float> default_value;
    std::optional<float> min_value;
    std::optional<float> max_value;
    std::vector<std::string> choice_labels;
    std::string group;
    std::string description;
    std::optional<VividDisplayHint> display_hint;
    uint8_t layout_columns = 0;
    uint8_t layout_column_index = 0;
    std::string semantic_tag;
    std::string semantic_shape;
    std::string semantic_unit;
    std::string semantic_intent;

    // Performance surface metadata (Step 5)
    std::string performance_page;      // e.g. "Performance" (empty = not on perf page)
    int performance_order = -1;        // sort order within page (-1 = unset)
    std::string performance_role;      // "macro", "mod_wheel", "expression", etc.
};

struct SubgraphPreset {
    std::string name;
    // Keys are "node_id/param_name", values are the override value.
    std::unordered_map<std::string, float> param_overrides;
    std::unordered_map<std::string, std::string> string_param_overrides;
};

// ---------------------------------------------------------------------------
// Modulation source/destination bindings — declared by module authors
// ---------------------------------------------------------------------------

struct ModSourceBinding {
    std::string name;           // stable name, e.g. "lfo1", "velocity"
    std::string description;
    std::string shape;          // "scalar" (default) or "lane_aware"
    std::string polarity;       // "unipolar" (default) or "bipolar"
    std::string internal_node;  // internal node ID (empty when kind == "port")
    std::string internal_port;  // output port name — or exposed port name for kind=="port"
    std::string kind;           // "internal" (default) or "port" (exposed input port)
    std::string group;          // optional display grouping
};

struct ModDestinationBinding {
    std::string name;           // stable name, e.g. "filter_cutoff"
    std::string description;
    std::string shape;          // "scalar" (default) or "lane_aware"
    std::string internal_node;  // internal node ID
    std::string internal_param; // param name (the modulation target)
    std::string group;          // optional display grouping
};

// ---------------------------------------------------------------------------
// Modulation lowering metadata — produced by flatten_subgraphs()
// ---------------------------------------------------------------------------

struct ModulationLoweringRecord {
    std::string instance_id;          // module node ID
    std::string exposed_param;        // exposed param name mapping to this destination (empty if none)
    // The connection whose remap encodes the base value (first source in chain)
    std::string base_conn_from_node;
    std::string base_conn_from_port;
    std::string base_conn_to_node;
    std::string base_conn_to_port;
    float amount = 0.0f;
    bool bipolar = false;
};

struct SubgraphModuleDef {
    std::string name;           // type name used in graphs (e.g. "WavetablePad")
    std::string description;
    std::string category;       // for UI catalog grouping
    std::vector<SubgraphPortBinding> ports;
    std::vector<SubgraphParamBinding> params;
    std::vector<SubgraphPreset> presets;
    std::vector<ModSourceBinding> mod_sources;
    std::vector<ModDestinationBinding> mod_destinations;
    Graph internal_graph;       // the nodes/connections inside this module
    std::string source_path;    // path to the .vivid-module.json file

    // Lookup helpers
    const SubgraphPortBinding* find_port(const std::string& port_name) const;
    const SubgraphParamBinding* find_param(const std::string& param_name) const;
    const SubgraphPreset* find_preset(const std::string& preset_name) const;
    const ModSourceBinding* find_mod_source(const std::string& name) const;
    const ModDestinationBinding* find_mod_destination(const std::string& name) const;
};

// ---------------------------------------------------------------------------
// Registry — discovers and stores module definitions
// ---------------------------------------------------------------------------

class SubgraphModuleRegistry {
public:
    // Load a single module definition from a .vivid-module.json file.
    bool load(const std::string& path);

    // Scan a directory for .vivid-module.json files (non-recursive).
    int scan(const std::string& directory);

    // Register a pre-parsed module definition.
    bool register_module(SubgraphModuleDef def);

    // Lookup by module type name.
    const SubgraphModuleDef* find(const std::string& type_name) const;

    // All registered type names.
    std::vector<std::string> type_names() const;

    bool empty() const { return modules_.empty(); }

private:
    std::unordered_map<std::string, SubgraphModuleDef> modules_;
};

// ---------------------------------------------------------------------------
// Synthetic OperatorInfo for UI catalog
// ---------------------------------------------------------------------------

namespace ui { struct OperatorInfo; }

// Build a synthetic OperatorInfo from a module definition for the UI catalog.
// The returned info lists the module's exposed ports and params.
std::shared_ptr<const ui::OperatorInfo> make_operator_info(const SubgraphModuleDef& def);

// Convert a SubgraphPreset (keyed by internal "node/param" paths) to an
// OperatorPreset (keyed by exposed param names). Overrides that don't match
// any exposed param binding are silently dropped.
OperatorPreset to_operator_preset(const SubgraphPreset& sp, const SubgraphModuleDef& def);

// ---------------------------------------------------------------------------
// Graph flattening — expands module nodes into their internal graphs
// ---------------------------------------------------------------------------

// Result of flattening, including the flattened graph and modulation metadata.
struct FlattenResult {
    Graph graph;
    std::vector<ModulationLoweringRecord> modulation_records;
};

// Returns a new Graph with all module nodes expanded into their internal
// nodes and connections. The input graph is not modified.
// Module nodes are identified by matching their type against the registry.
//
// Modulation assignments on module instances are lowered into ordinary
// connection remaps and Math(add) nodes. The ModulationLoweringRecord
// entries in the result describe base-carrying connections for live
// param updates without recompile.
//
// Limitation: flattening is single-level only. If a module's internal graph
// contains another module node, that nested module is NOT expanded. Support
// for nested modules would require iterative/recursive flattening.
FlattenResult flatten_subgraphs(const Graph& authored, const SubgraphModuleRegistry& registry);

} // namespace vivid
