#include "runtime/control/runtime_api.h"
#include "runtime/graph/graph.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/subgraph_module.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/operators/operator_preparation_service.h"
#include "runtime/audio/system_midi.h"
#include <sstream>
#include <cmath>

namespace vivid {

namespace {
struct ParamSemanticMeta {
    bool found = false;
    std::string tag;
    std::string unit;
    float min_value = 0.0f;
    float max_value = 1.0f;
};

struct SemanticCoercionRule {
    const char* from_tag;
    const char* to_tag;
    float scale;
};

static constexpr SemanticCoercionRule kSemanticCoercionRules[] = {
    {"time_milliseconds", "time_seconds", 1.0f / 1000.0f},
    {"time_seconds", "time_milliseconds", 1000.0f},
    {"rotation_degrees", "rotation_radians", 3.14159265358979323846f / 180.0f},
    {"rotation_radians", "rotation_degrees", 180.0f / 3.14159265358979323846f},
};

bool resolve_param_semantic_meta(const vivid::Graph& graph,
                                 vivid::OperatorRegistry& registry,
                                 const std::string& node_id,
                                 const std::string& param_name,
                                 ParamSemanticMeta& out) {
    const NodeDef* def = graph.find_node(node_id);
    if (!def) return false;
    const VividOperatorDescriptor* desc = registry.probe_descriptor(def->type);
    if (!desc) return false;
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        const auto& pd = desc->params[i];
        if (param_name != pd.name) continue;
        out.found = true;
        out.tag = pd.semantic_tag ? pd.semantic_tag : "";
        out.unit = pd.semantic_unit ? pd.semantic_unit : "";
        out.min_value = pd.min_value;
        out.max_value = pd.max_value;
        return true;
    }
    if (desc->param_count == 1) {
        const auto& pd = desc->params[0];
        out.found = true;
        out.tag = pd.semantic_tag ? pd.semantic_tag : "";
        out.unit = pd.semantic_unit ? pd.semantic_unit : "";
        out.min_value = pd.min_value;
        out.max_value = pd.max_value;
        return true;
    }
    return false;
}

bool is_identity_remap(float from_min, float from_max, float to_min, float to_max) {
    constexpr float kEps = 1e-4f;
    float denom = from_max - from_min;
    if (std::fabs(denom) < kEps) return false;
    float slope = (to_max - to_min) / denom;
    float intercept = to_min - slope * from_min;
    return std::fabs(slope - 1.0f) < kEps && std::fabs(intercept) < kEps;
}

bool converted_range_for_pair(const ParamSemanticMeta& src,
                              const ParamSemanticMeta& dst,
                              float& out_to_min,
                              float& out_to_max) {
    for (const auto& rule : kSemanticCoercionRules) {
        if (src.tag != rule.from_tag || dst.tag != rule.to_tag) continue;
        out_to_min = src.min_value * rule.scale;
        out_to_max = src.max_value * rule.scale;
        return true;
    }
    return false;
}

bool semantic_default_remap(const ParamSemanticMeta& src,
                            const ParamSemanticMeta& dst,
                            float& from_min,
                            float& from_max,
                            float& to_min,
                            float& to_max) {
    if (!src.found || !dst.found || src.tag.empty() || dst.tag.empty()) return false;

    from_min = src.min_value;
    from_max = src.max_value;

    if (src.tag == dst.tag) {
        to_min = dst.min_value;
        to_max = dst.max_value;
        return true;
    }

    return converted_range_for_pair(src, dst, to_min, to_max);
}

const VividOperatorDescriptor* node_descriptor(const CompiledNode& cn) {
    return cn.loader ? cn.loader->descriptor() : nullptr;
}

// Infer a bridge kind string for a cross-cadence connection. Returns "" for
// same-cadence edges (no bridge needed) or when descriptors are unavailable.
// Rules:
//   audio → frame: "rms"/"peak"/"waveform" if the source port name matches
//                  (audio analysis ports from append_analysis_ports),
//                  "waveform" for any other lane-array source,
//                  "last_sample" for plain scalar audio outputs.
//   frame → audio: "snapshot" for lane-array sources, "hold" for scalars.
std::string infer_bridge_kind(const vivid::Graph& graph,
                              vivid::OperatorRegistry& registry,
                              const std::string& from_node,
                              const std::string& from_port,
                              const std::string& to_node) {
    const NodeDef* from_def = graph.find_node(from_node);
    const NodeDef* to_def   = graph.find_node(to_node);
    if (!from_def || !to_def) return "";

    const VividOperatorDescriptor* from_desc = registry.probe_descriptor(from_def->type);
    const VividOperatorDescriptor* to_desc   = registry.probe_descriptor(to_def->type);
    if (!from_desc || !to_desc) return "";

    bool from_is_audio = (vivid_operator_kind(from_desc) == VIVID_OP_AUDIO);
    bool to_is_audio   = (vivid_operator_kind(to_desc)   == VIVID_OP_AUDIO);
    if (from_is_audio == to_is_audio) return "";

    const VividPortDescriptor* src_port = nullptr;
    for (uint32_t i = 0; i < from_desc->port_count; ++i) {
        const auto& pd = from_desc->ports[i];
        if (pd.direction != VIVID_PORT_OUTPUT) continue;
        if (pd.name && from_port == pd.name) { src_port = &pd; break; }
    }
    if (!src_port) return "";

    if (from_is_audio) {
        std::string name = src_port->name ? src_port->name : "";
        if (name == "rms")      return "rms";
        if (name == "peak")     return "peak";
        if (name == "waveform") return "waveform";
        if (src_port->type == VIVID_PORT_LANE_ARRAY) return "waveform";
        return "last_sample";
    }
    if (src_port->type == VIVID_PORT_LANE_ARRAY) return "snapshot";
    return "hold";
}

std::string node_display_name(const CompiledNode& cn,
                              const VividOperatorDescriptor* desc) {
    if (desc && desc->name && desc->name[0] != '\0') return desc->name;
    if (!cn.type_name.empty()) return cn.type_name;
    return "missing_operator";
}

constexpr const char* kNoCompiledGraph = "no compiled graph";
} // namespace

std::optional<RuntimeAPI::ResolvedModuleParam> RuntimeAPI::resolve_module_param(
        const std::string& node_id, const std::string& param) {
    if (!subgraph_modules_) return std::nullopt;
    const auto* ndef = graph_.find_node(node_id);
    if (!ndef) return std::nullopt;
    const auto* mod = subgraph_modules_->find(ndef->type);
    if (!mod) return std::nullopt;
    const auto* pb = mod->find_param(param);
    if (!pb) return std::nullopt;
    std::string flat_id = node_id + ".__" + pb->internal_node;
    auto* cg = core_.compiled_graph();
    if (!cg) return std::nullopt;
    auto* cn = cg->find_node(flat_id);
    if (!cn) return std::nullopt;
    auto pi = cn->param_indices.find(pb->internal_param);
    if (pi == cn->param_indices.end()) return std::nullopt;
    return ResolvedModuleParam{cn, pi->second, flat_id, pb->internal_param};
}

CommandResult RuntimeAPI::set_param(const std::string& node_id, const std::string& param, float value) {
    auto* cg = core_.compiled_graph();
    if (!cg) return {false, kNoCompiledGraph};
    auto* cn = cg->find_node(node_id);

    // Module param proxy: route to internal node
    if (!cn) {
        // Check if this param targets a modulated destination — if so, update
        // the base-carrying connection remap instead of the (wire-driven) param.
        for (const auto& rec : core_.modulation_records()) {
            if (rec.instance_id == node_id && rec.exposed_param == param) {
                // Find and update the base-carrying CompiledEdge
                auto from_it = cg->node_id_to_index.find(rec.base_conn_from_node);
                auto to_it   = cg->node_id_to_index.find(rec.base_conn_to_node);
                if (from_it != cg->node_id_to_index.end() && to_it != cg->node_id_to_index.end()) {
                    auto* to_cn = &cg->nodes[to_it->second];
                    // Determine the target port index (could be an input port or a param)
                    uint32_t to_port_idx = UINT32_MAX;
                    bool targets_param = false;
                    auto ipi = to_cn->input_port_indices.find(rec.base_conn_to_port);
                    if (ipi != to_cn->input_port_indices.end()) {
                        to_port_idx = ipi->second;
                    } else {
                        auto ppi = to_cn->param_indices.find(rec.base_conn_to_port);
                        if (ppi != to_cn->param_indices.end()) {
                            to_port_idx = ppi->second;
                            targets_param = true;
                        }
                    }
                    if (to_port_idx != UINT32_MAX) {
                        for (auto& edge : cg->edges) {
                            if (edge.from_node == from_it->second &&
                                edge.to_node == to_it->second &&
                                edge.to_port == to_port_idx &&
                                edge.targets_param == targets_param) {
                                // Update remap: to_min/to_max encode base ± amount
                                float new_to_min = rec.bipolar ? (value - rec.amount) : value;
                                float new_to_max = value + rec.amount;
                                edge.to_min = new_to_min;
                                edge.to_max = new_to_max;
                                break;
                            }
                        }
                    }
                }
                // Update authored graph (for persistence)
                NodeDef* ndef = graph_.find_node(node_id);
                if (ndef) ndef->params[param] = value;
                mark_graph_dirty();
                std::ostringstream oss;
                oss << node_id << "/" << param << " = " << value << " (modulated base)";
                return {true, oss.str()};
            }
        }

        auto resolved = resolve_module_param(node_id, param);
        if (!resolved) return {false, "unknown node '" + node_id + "'"};
        resolved->cn->param_values[resolved->param_idx] = value;
        resolved->cn->dirty = true;
        // Sync to authored graph's module node (not internal node)
        NodeDef* ndef = graph_.find_node(node_id);
        if (ndef) ndef->params[param] = value;
        mark_graph_dirty();
        std::ostringstream oss;
        oss << node_id << "/" << param << " = " << value;
        return {true, oss.str()};
    }

    auto pi = cn->param_indices.find(param);
    if (pi == cn->param_indices.end()) {
        return {false, "unknown param '" + param + "' on " + node_id};
    }

    cn->param_values[pi->second] = value;
    cn->dirty = true;

    if (param == "server") {
        const auto* desc = cn->loader ? cn->loader->descriptor() : nullptr;
        if (desc) {
            auto si = cn->param_indices.find("server");
            auto ni = cn->file_param_indices.find("server_name");
            if (si != cn->param_indices.end() && ni != cn->file_param_indices.end()) {
                uint32_t pidx = si->second;
                if (pidx < desc->param_count) {
                    const auto& pd = desc->params[pidx];
                    int idx = static_cast<int>(std::round(value));
                    std::string selected;
                    if (idx > 0 && pd.choice_labels && idx < static_cast<int>(pd.choice_count)) {
                        selected = pd.choice_labels[idx] ? pd.choice_labels[idx] : "";
                    }
                    set_file_param_internal(*cn, "server_name", selected);
                }
            }
        }
    }

    NodeDef* ndef = graph_.find_node(node_id);
    if (ndef) {
        ndef->params[param] = value;
    }

    mark_graph_dirty();

    std::ostringstream oss;
    oss << node_id << "/" << param << " = " << value;
    return {true, oss.str()};
}

CommandResult RuntimeAPI::set_string_param(const std::string& node_id, const std::string& param,
                                           const std::string& value) {
    auto* cg = core_.compiled_graph();
    if (!cg) return {false, kNoCompiledGraph};
    auto* cn = cg->find_node(node_id);

    // Module param proxy: route to internal node
    if (!cn) {
        auto resolved = resolve_module_param(node_id, param);
        if (!resolved) return {false, "unknown node '" + node_id + "'"};
        set_file_param_internal(*resolved->cn, resolved->internal_param, value);
        NodeDef* ndef = graph_.find_node(node_id);
        if (ndef) ndef->string_params[param] = value;
        mark_graph_dirty();
        return {true, node_id + "/" + param + " = " + value};
    }

    auto fi = cn->file_param_indices.find(param);
    if (fi == cn->file_param_indices.end()) {
        return {false, "unknown string param '" + param + "' on " + node_id};
    }

    set_file_param_internal(*cn, param, value);
    NodeDef* ndef = graph_.find_node(node_id);
    if (ndef) ndef->string_params[param] = value;

    mark_graph_dirty();

    return {true, node_id + "/" + param + " = " + value};
}

CommandResult RuntimeAPI::get_string_param(const std::string& node_id, const std::string& param) {
    auto* cg = core_.compiled_graph();
    if (!cg) return {false, kNoCompiledGraph};
    auto* cn = cg->find_node(node_id);
    if (!cn) {
        auto resolved = resolve_module_param(node_id, param);
        if (!resolved) return {false, "unknown node '" + node_id + "'"};
        cn = resolved->cn;
        auto fi = cn->file_param_indices.find(resolved->internal_param);
        if (fi == cn->file_param_indices.end() || fi->second >= cn->file_param_storage.size())
            return {false, "unknown string param '" + param + "' on " + node_id};
        return {true, cn->file_param_storage[fi->second]};
    }

    auto fi = cn->file_param_indices.find(param);
    if (fi == cn->file_param_indices.end() || fi->second >= cn->file_param_storage.size())
        return {false, "unknown string param '" + param + "' on " + node_id};
    return {true, cn->file_param_storage[fi->second]};
}

CommandResult RuntimeAPI::get_param(const std::string& node_id, const std::string& param) {
    auto* cg = core_.compiled_graph();
    if (!cg) return {false, kNoCompiledGraph};
    auto* cn = cg->find_node(node_id);
    if (!cn) {
        auto resolved = resolve_module_param(node_id, param);
        if (!resolved) return {false, "unknown node '" + node_id + "'"};
        cn = resolved->cn;
        // Fall through with resolved cn — param name is the internal param
        auto pi = cn->param_indices.find(resolved->internal_param);
        if (pi == cn->param_indices.end())
            return {false, "unknown param '" + param + "' on " + node_id};
        float val = cn->param_values[pi->second];
        std::ostringstream oss;
        oss << val;
        return {true, oss.str()};
    }
    auto pi = cn->param_indices.find(param);
    if (pi == cn->param_indices.end()) {
        return {false, "unknown param '" + param + "' on " + node_id};
    }
    std::ostringstream oss;
    oss << cn->param_values[pi->second];
    return {true, oss.str()};
}

CommandResult RuntimeAPI::set_param_lock(const std::string& node_id, const std::string& param, uint8_t flags) {
    auto* cg = core_.compiled_graph();
    if (!cg) return {false, kNoCompiledGraph};
    auto* cn = cg->find_node(node_id);

    // Module param proxy
    if (!cn) {
        auto resolved = resolve_module_param(node_id, param);
        if (!resolved) return {false, "unknown node '" + node_id + "'"};
        resolved->cn->param_lock_flags[resolved->param_idx] = flags;
        NodeDef* ndef = graph_.find_node(node_id);
        if (ndef) {
            if (flags != PARAM_LOCK_NONE)
                ndef->param_lock_flags[param] = flags;
            else
                ndef->param_lock_flags.erase(param);
        }
        std::ostringstream oss;
        oss << node_id << "/" << param << " lock = " << static_cast<int>(flags);
        mark_graph_dirty();
        return {true, oss.str()};
    }

    auto pi = cn->param_indices.find(param);
    if (pi == cn->param_indices.end())
        return {false, "unknown param '" + param + "' on " + node_id};

    cn->param_lock_flags[pi->second] = flags;

    NodeDef* ndef = graph_.find_node(node_id);
    if (ndef) {
        if (flags != PARAM_LOCK_NONE)
            ndef->param_lock_flags[param] = flags;
        else
            ndef->param_lock_flags.erase(param);
    }

    std::ostringstream oss;
    oss << node_id << "/" << param << " lock = " << static_cast<int>(flags);
    mark_graph_dirty();
    return {true, oss.str()};
}

CommandResult RuntimeAPI::get_param_lock(const std::string& node_id, const std::string& param) {
    auto* cg = core_.compiled_graph();
    if (!cg) return {false, kNoCompiledGraph};
    auto* cn = cg->find_node(node_id);
    if (!cn) return {false, "unknown node '" + node_id + "'"};
    auto pi = cn->param_indices.find(param);
    if (pi == cn->param_indices.end())
        return {false, "unknown param '" + param + "' on " + node_id};
    std::ostringstream oss;
    oss << static_cast<int>(cn->param_lock_flags[pi->second]);
    return {true, oss.str()};
}

CommandResult RuntimeAPI::set_node_layout(const std::string& node_id, float x, float y) {
    NodeDef* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};
    ndef->layout_x = x;
    ndef->layout_y = y;
    mark_graph_dirty();
    return {true, "layout set"};
}

CommandResult RuntimeAPI::set_resolution(const std::string& node_id, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return {false, "resolution must be non-zero"};
    if (width > 8192 || height > 8192)
        return {false, "resolution exceeds 8192 limit"};

    NodeDef* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};
    ndef->tex_width  = width;
    ndef->tex_height = height;

    auto* cg = core_.compiled_graph();
    if (auto* cn = cg ? cg->find_node(node_id) : nullptr) {
        if (cn->gpu) {
            cn->gpu->tex_width  = width;
            cn->gpu->tex_height = height;
        }
        cn->dirty = true;
    }

    needs_gpu_realloc_ = true;
    mark_graph_dirty();

    std::ostringstream oss;
    oss << node_id << " resolution = " << width << "x" << height;
    return {true, oss.str()};
}

CommandResult RuntimeAPI::set_node_bypassed(const std::string& node_id, bool bypassed) {
    NodeDef* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};

    if (bypassed) {
        // Verify bypass-eligibility from the operator's port descriptors.
        // First input port type must equal first output port type. Sources
        // and asymmetric nodes are not bypassable.
        const VividOperatorDescriptor* desc = registry_.probe_descriptor(ndef->type);
        if (!desc) return {false, "unknown operator type '" + ndef->type + "'"};

        const VividPortDescriptor* first_in  = nullptr;
        const VividPortDescriptor* first_out = nullptr;
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            const auto& p = desc->ports[i];
            if (p.direction == VIVID_PORT_INPUT  && !first_in)  first_in  = &p;
            if (p.direction == VIVID_PORT_OUTPUT && !first_out) first_out = &p;
            if (first_in && first_out) break;
        }
        if (!first_in || !first_out)
            return {false, node_id + " is not bypass-eligible (must have at least one input and one output)"};
        if (first_in->type != first_out->type)
            return {false, node_id + " is not bypass-eligible (first input and output port types must match)"};
    }

    if (ndef->bypassed == bypassed) {
        return {true, node_id + (bypassed ? " already bypassed" : " already enabled")};
    }
    ndef->bypassed = bypassed;

    // Live update on the compiled graph — no topology change, so no recompile.
    if (auto* cg = core_.compiled_graph()) {
        if (auto* cn = cg->find_node(node_id)) {
            cn->bypassed = bypassed;
            cn->dirty = true;  // ensure the next tick re-evaluates this node
        }
    }
    mark_graph_dirty();
    return {true, node_id + (bypassed ? " bypassed" : " enabled")};
}

CommandResult RuntimeAPI::add_node(const std::string& type, const std::string& id) {
    const bool is_non_registry_type =
        (core_.subgraph_modules() && core_.subgraph_modules()->find(type));
    if (!is_non_registry_type) {
        auto prepared = prepare_operator_type_sync(registry_, type);
        if (!prepared.success) {
            const std::string msg = prepared.user_message.empty()
                ? "unknown type '" + type + "'"
                : prepared.user_message;
            return {false, msg};
        }
    }
    if (!graph_.add_node(id, type)) {
        return {false, "node '" + id + "' already exists"};
    }
    pending_topology_change_ = true;
    mark_graph_dirty();
    return {true, "added " + type + " as " + id};
}

CommandResult RuntimeAPI::remove_node(const std::string& id) {
    if (!graph_.remove_node(id)) {
        return {false, "unknown node '" + id + "'"};
    }
    pending_topology_change_ = true;
    mark_graph_dirty();
    return {true, "removed " + id};
}

CommandResult RuntimeAPI::connect(const std::string& from_addr, const std::string& to_addr,
                                  bool semantic_defaults, const std::string& bridge) {
    std::string fn, fp, tn, tp;
    if (!split_addr(from_addr, fn, fp)) {
        return {false, "invalid address '" + from_addr + "' (expected node/port)"};
    }
    if (!split_addr(to_addr, tn, tp)) {
        return {false, "invalid address '" + to_addr + "' (expected node/port)"};
    }
    if (!graph_.find_node(fn)) return {false, "unknown node '" + fn + "'"};
    if (!graph_.find_node(tn)) return {false, "unknown node '" + tn + "'"};
    if (!graph_.add_connection(fn, fp, tn, tp)) {
        return {false, "connection already exists"};
    }

    std::string chosen_bridge = bridge;
    bool inferred_bridge = false;
    if (chosen_bridge.empty()) {
        chosen_bridge = infer_bridge_kind(graph_, registry_, fn, fp, tn);
        inferred_bridge = !chosen_bridge.empty();
    }
    if (!chosen_bridge.empty()) {
        graph_.set_connection_bridge(fn, fp, tn, tp, chosen_bridge);
    }

    bool applied_semantic_remap = false;
    if (semantic_defaults) {
        ParamSemanticMeta src_meta;
        ParamSemanticMeta dst_meta;
        if (resolve_param_semantic_meta(graph_, registry_, fn, fp, src_meta) &&
            resolve_param_semantic_meta(graph_, registry_, tn, tp, dst_meta)) {
            float from_min = 0.0f, from_max = 1.0f, to_min = 0.0f, to_max = 1.0f;
            if (semantic_default_remap(src_meta, dst_meta, from_min, from_max, to_min, to_max) &&
                !is_identity_remap(from_min, from_max, to_min, to_max)) {
                if (graph_.set_connection_remap(fn, fp, tn, tp,
                                                from_min, from_max, to_min, to_max, false)) {
                    applied_semantic_remap = true;
                }
            }
        }
    }

    pending_topology_change_ = true;
    mark_graph_dirty();
    std::string msg = "connected " + from_addr + " -> " + to_addr;
    if (inferred_bridge)
        msg += " (bridge: " + chosen_bridge + ")";
    if (applied_semantic_remap)
        msg += " (semantic default remap applied)";
    return {true, msg};
}

CommandResult RuntimeAPI::disconnect(const std::string& from_addr, const std::string& to_addr) {
    std::string fn, fp, tn, tp;
    if (!split_addr(from_addr, fn, fp) || !split_addr(to_addr, tn, tp)) {
        return {false, "invalid address (expected node/port)"};
    }
    if (!graph_.remove_connection(fn, fp, tn, tp)) {
        return {false, "connection not found"};
    }
    pending_topology_change_ = true;
    mark_graph_dirty();
    return {true, "disconnected " + from_addr + " -> " + to_addr};
}

CommandResult RuntimeAPI::set_connection_remap(const std::string& from_addr,
                                                const std::string& to_addr,
                                                float from_min, float from_max,
                                                float to_min, float to_max,
                                                bool clamp, uint8_t curve) {
    std::string fn, fp, tn, tp;
    if (!split_addr(from_addr, fn, fp) || !split_addr(to_addr, tn, tp)) {
        return {false, "invalid address (expected node/port)"};
    }
    if (!graph_.set_connection_remap(fn, fp, tn, tp, from_min, from_max, to_min, to_max, clamp, curve)) {
        return {false, "connection not found"};
    }
    pending_topology_change_ = true;
    mark_graph_dirty();
    return {true, "set remap on " + from_addr + " -> " + to_addr};
}

bool RuntimeAPI::apply_pending(bool& has_gpu_ops, bool& has_audio) {
    if (!pending_topology_change_) return false;
    pending_topology_change_ = false;
    active_crossfades_.clear();

    std::unordered_map<std::string, std::unordered_map<std::string, float>> saved_params;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> saved_string_params;
    std::unordered_map<std::string, std::unordered_map<std::string, uint8_t>> saved_locks;
    if (auto* existing_cg = core_.compiled_graph()) {
        for (const auto& cn : existing_cg->nodes) {
            auto& sp = saved_params[cn.node_id];
            for (const auto& [name, idx] : cn.param_indices) {
                sp[name] = cn.param_values[idx];
                if (cn.param_lock_flags[idx] != PARAM_LOCK_NONE)
                    saved_locks[cn.node_id][name] = cn.param_lock_flags[idx];
            }
            auto& ssp = saved_string_params[cn.node_id];
            for (const auto& [name, idx] : cn.file_param_indices)
                ssp[name] = cn.file_param_storage[idx];
        }
    }

    bool had_audio = has_audio;
    if (had_audio) {
        audio_engine_.shutdown();
        has_audio = false;
    }

    core_.shutdown();

    if (!core_.build(graph_, registry_)) {
        std::fprintf(stderr, "[vivid] RuntimeAPI: rebuild failed\n");
        has_gpu_ops = false;
        has_audio = false;
        return true;
    }

    auto* cg = core_.compiled_graph();
    if (!cg) {
        has_gpu_ops = false;
        has_audio = false;
        return true;
    }
    for (auto& cn : cg->nodes) {
        auto sit = saved_params.find(cn.node_id);
        if (sit != saved_params.end()) {
            for (const auto& [pname, pval] : sit->second) {
                auto pi = cn.param_indices.find(pname);
                if (pi != cn.param_indices.end()) {
                    cn.param_values[pi->second] = pval;
                }
            }
        }
        auto ssit = saved_string_params.find(cn.node_id);
        if (ssit != saved_string_params.end()) {
            for (const auto& [pname, pval] : ssit->second) {
                auto fi = cn.file_param_indices.find(pname);
                if (fi != cn.file_param_indices.end()) {
                    cn.file_param_storage[fi->second] = pval;
                    cn.file_param_ptrs[fi->second] = cn.file_param_storage[fi->second].c_str();
                }
            }
        }
        auto lit = saved_locks.find(cn.node_id);
        if (lit != saved_locks.end()) {
            for (const auto& [pname, flags] : lit->second) {
                auto pi = cn.param_indices.find(pname);
                if (pi != cn.param_indices.end())
                    cn.param_lock_flags[pi->second] = flags;
            }
        }
    }

    has_gpu_ops = core_.has_gpu_operators();

    if (core_.has_audio_operators()) {
        if (audio_engine_.build(core_)) {
            if (audio_engine_.start()) {
                has_audio = true;
            }
        }
    }

    return true;
}

CommandResult RuntimeAPI::inspect(const std::string& node_id) {
    const auto* cg = core_.compiled_graph();
    if (!cg) return {false, kNoCompiledGraph};
    const auto* cn = cg->find_node(node_id);
    if (!cn) return {false, "unknown node '" + node_id + "'"};
    const auto* desc = node_descriptor(*cn);
    auto find_port_desc = [&](const std::string& name, VividPortDirection dir) -> const VividPortDescriptor* {
        if (!desc) return nullptr;
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            const auto& pd = desc->ports[i];
            if (pd.direction == dir && pd.name == name)
                return &pd;
        }
        return nullptr;
    };
    auto append_audio_debug = [&](std::ostringstream& out, bool input, uint32_t port_idx) {
        if (!cn->audio) return false;
        auto snap = read_audio_port_debug(*cn->audio, input, port_idx);
        if (!snap.valid) return false;
        out << "audio[ch=" << static_cast<int>(snap.channel_count)
            << " peak=" << snap.last_block_peak
            << " frames=" << snap.buffer_size
            << (snap.active ? " active]" : " idle]");
        return true;
    };
    std::ostringstream oss;
    oss << node_id << " (" << node_display_name(*cn, desc) << ")\n";
    {
        static const char* lb_names[] = {"pointwise", "structural", "reduction", "kernel"};
        uint8_t lb = static_cast<uint8_t>(cn->lane_behavior);
        if (lb < 4) oss << "  lane_behavior: " << lb_names[lb] << "\n";
    }
    if (cn->missing_operator) {
        oss << "  status: missing operator placeholder for type "
            << cn->type_name << "\n";
    }
    oss << "  params:";
    for (const auto& [name, idx] : cn->param_indices) {
        oss << " " << name << "=" << cn->param_values[idx];
        uint8_t lock = cn->param_lock_flags[idx];
        if (lock & PARAM_LOCK_WIRES) oss << "[W]";
        if (lock & PARAM_LOCK_PRESETS) oss << "[P]";
    }
    oss << "\n  outputs:";
    for (const auto& [name, idx] : cn->output_port_indices) {
        oss << " " << name << "=";
        const auto* pd = find_port_desc(name, VIVID_PORT_OUTPUT);
        if (pd && pd->type == VIVID_PORT_AUDIO_BUFFER && append_audio_debug(oss, false, idx)) {
            continue;
        }
        oss << cn->output_values[idx];
        if (idx < cn->output_string_values.size() && !cn->output_string_values[idx].empty())
            oss << " \"" << cn->output_string_values[idx] << "\"";
        if (idx < cn->output_lane_refs.size() && cn->output_lane_refs[idx]) {
            const auto& ref = cn->output_lane_refs[idx];
            oss << " [";
            for (uint32_t si = 0; si < ref.length(); ++si) {
                if (si > 0) oss << ",";
                oss << ref.data()[si];
            }
            oss << "]";
        }
        if (idx < cn->output_string_lanes.size() && !cn->output_string_lanes[idx].empty()) {
            oss << " [";
            for (size_t si = 0; si < cn->output_string_lanes[idx].size(); ++si) {
                if (si > 0) oss << ",";
                oss << "\"" << cn->output_string_lanes[idx][si] << "\"";
            }
            oss << "]";
        }
    }
    if (!cn->input_port_indices.empty()) {
        oss << "\n  inputs:";
        for (const auto& [name, idx] : cn->input_port_indices) {
            oss << " " << name << "=";
            const auto* pd = find_port_desc(name, VIVID_PORT_INPUT);
            if (pd && pd->type == VIVID_PORT_AUDIO_BUFFER && append_audio_debug(oss, true, idx)) {
                continue;
            }
            oss << cn->input_values[idx];
            if (idx < cn->input_string_values.size() && !cn->input_string_values[idx].empty())
                oss << " \"" << cn->input_string_values[idx] << "\"";
            if (idx < cn->input_lane_refs.size() && cn->input_lane_refs[idx]) {
                const auto& ref = cn->input_lane_refs[idx];
                oss << " [";
                for (uint32_t si = 0; si < ref.length(); ++si) {
                    if (si > 0) oss << ",";
                    oss << ref.data()[si];
                }
                oss << "]";
            }
            if (idx < cn->input_string_lanes.size() && !cn->input_string_lanes[idx].empty()) {
                oss << " [";
                for (size_t si = 0; si < cn->input_string_lanes[idx].size(); ++si) {
                    if (si > 0) oss << ",";
                    oss << "\"" << cn->input_string_lanes[idx][si] << "\"";
                }
                oss << "]";
            }
        }
    }
    if (cn->audio) {
        auto dbg = read_audio_node_debug(*cn->audio);
        if (dbg.valid) {
            oss << "\n  audio_debug: total=" << dbg.last_block_total_us
                << "us process=" << dbg.last_process_us
                << "us ema=" << dbg.ema_block_us
                << "us budget=" << dbg.last_block_budget_pct
                << "% lanes=" << dbg.last_lane_count
                << " state=" << dbg.lane_state_entries;
        }
    }
    return {true, oss.str()};
}

CommandResult RuntimeAPI::list_nodes() {
    const auto* cg = core_.compiled_graph();
    if (!cg) return {true, "(no nodes)"};
    const auto& nodes = cg->nodes;
    if (nodes.empty()) return {true, "(no nodes)"};
    std::ostringstream oss;
    for (const auto& cn : nodes) {
        const auto* desc = node_descriptor(cn);
        oss << cn.node_id << " (" << node_display_name(cn, desc) << ")\n";
    }
    std::string result = oss.str();
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return {true, result};
}

CommandResult RuntimeAPI::list_types() {
    auto names = registry_.type_names();
    if (names.empty()) return {true, "(no types loaded)"};
    std::ostringstream oss;
    for (const auto& name : names) {
        oss << name << "\n";
    }
    std::string result = oss.str();
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return {true, result};
}

CommandResult RuntimeAPI::set_solo(const std::string& node_id) {
    if (node_id.empty()) {
        core_.set_solo(-1);
        return {true, "solo cleared"};
    }
    auto* cg = core_.compiled_graph();
    if (!cg) return {false, kNoCompiledGraph};
    auto it = cg->node_id_to_index.find(node_id);
    if (it != cg->node_id_to_index.end()) {
        core_.set_solo(static_cast<int>(it->second));
        return {true, "soloed " + node_id};
    }
    return {false, "node not found: " + node_id};
}

std::string RuntimeAPI::solo_node_id() const {
    int idx = core_.solo_node_idx();
    const auto* cg = core_.compiled_graph();
    if (!cg) return {};
    const auto& nodes = cg->nodes;
    if (idx < 0 || idx >= static_cast<int>(nodes.size())) return {};
    return nodes[idx].node_id;
}

bool RuntimeAPI::inject_midi_to_node(const std::string& node_id,
                                       const uint8_t* bytes, uint32_t count) {
    if (!bytes || count == 0) return false;
    auto* cg = core_.compiled_graph();
    if (!cg) return false;
    auto it = cg->node_id_to_index.find(node_id);
    if (it == cg->node_id_to_index.end()) return false;
    const auto& cn = cg->nodes[it->second];
    if (!cn.loader || !cn.instance) return false;
    if (!cn.loader->has_inject_midi()) return false;
    cn.loader->inject_midi(cn.instance, bytes, count);
    return true;
}

} // namespace vivid
