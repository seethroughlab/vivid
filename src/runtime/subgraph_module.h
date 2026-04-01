#pragma once

#include "runtime/graph.h"
#include "operator_api/types.h"
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
};

struct SubgraphPreset {
    std::string name;
    // Keys are "node_id/param_name", values are the override value.
    std::unordered_map<std::string, float> param_overrides;
    std::unordered_map<std::string, std::string> string_param_overrides;
};

struct SubgraphModuleDef {
    std::string name;           // type name used in graphs (e.g. "WavetablePad")
    std::string description;
    std::string category;       // for UI catalog grouping
    std::vector<SubgraphPortBinding> ports;
    std::vector<SubgraphParamBinding> params;
    std::vector<SubgraphPreset> presets;
    Graph internal_graph;       // the nodes/connections inside this module
    std::string source_path;    // path to the .vivid-module.json file

    // Lookup helpers
    const SubgraphPortBinding* find_port(const std::string& port_name) const;
    const SubgraphParamBinding* find_param(const std::string& param_name) const;
    const SubgraphPreset* find_preset(const std::string& preset_name) const;
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

// ---------------------------------------------------------------------------
// Graph flattening — expands module nodes into their internal graphs
// ---------------------------------------------------------------------------

// Returns a new Graph with all module nodes expanded into their internal
// nodes and connections. The input graph is not modified.
// Module nodes are identified by matching their type against the registry.
//
// Limitation: flattening is single-level only. If a module's internal graph
// contains another module node, that nested module is NOT expanded. Support
// for nested modules would require iterative/recursive flattening.
Graph flatten_subgraphs(const Graph& authored, const SubgraphModuleRegistry& registry);

} // namespace vivid
