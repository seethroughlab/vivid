#include "runtime/runtime_api.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/operator_registry.h"
#include "runtime/system_midi.h"
#include "common/path_util.h"
#include "runtime/platform.h"
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <cmath>

namespace vivid {

RuntimeAPI::RuntimeAPI(Graph& graph, Scheduler& scheduler, AudioEngine& audio_engine,
                       OperatorRegistry& registry, SystemMidiListener* system_midi)
    : graph_(graph), scheduler_(scheduler), audio_engine_(audio_engine),
      registry_(registry), system_midi_(system_midi) {
    if (!graph_.source_path().empty()) {
        active_graph_source_path_ =
            std::filesystem::path(graph_.source_path()).lexically_normal().string();
    }
    capture_saved_snapshot();
}

bool RuntimeAPI::split_addr(const std::string& addr, std::string& node, std::string& port) {
    auto slash = addr.find('/');
    if (slash == std::string::npos) return false;
    node = addr.substr(0, slash);
    port = addr.substr(slash + 1);
    return !node.empty() && !port.empty();
}

namespace {
std::string normalized_graph_identity_path(const std::string& path_str) {
    if (path_str.empty()) return {};
    return std::filesystem::path(path_str).lexically_normal().string();
}

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

// Explicit coercion contract for semantic-default remap.
// Keep in sync with docs/SEMANTIC-PARAM-TAGS.md.
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
    // Common control pattern: operator exposes one output port and one parameter with
    // different names (e.g. out/value). For single-param operators, infer semantics
    // from that sole parameter when endpoint-name lookup doesn't match.
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
    return false;  // Different-tag coercion is disabled unless explicitly listed above.
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
} // namespace

// --- Immediate param changes ---

CommandResult RuntimeAPI::set_param(const std::string& node_id, const std::string& param, float value) {
    NodeState* ns = scheduler_.find_node_mut(node_id);
    if (!ns) return {false, "unknown node '" + node_id + "'"};

    auto pi = ns->param_indices.find(param);
    if (pi == ns->param_indices.end()) {
        return {false, "unknown param '" + param + "' on " + node_id};
    }

    ns->param_values[pi->second] = value;
    ns->generation++;

    // Convenience sync for SyphonIn: selecting enum server writes server_name.
    if (param == "server") {
        const auto* desc = ns->loader ? ns->loader->descriptor() : nullptr;
        if (desc) {
            auto si = ns->param_indices.find("server");
            auto ni = ns->file_param_indices.find("server_name");
            if (si != ns->param_indices.end() && ni != ns->file_param_indices.end()) {
                uint32_t pidx = si->second;
                if (pidx < desc->param_count) {
                    const auto& pd = desc->params[pidx];
                    int idx = static_cast<int>(std::round(value));
                    std::string selected;
                    if (idx > 0 && pd.choice_labels && idx < static_cast<int>(pd.choice_count)) {
                        selected = pd.choice_labels[idx] ? pd.choice_labels[idx] : "";
                    }
                    set_file_param_internal(*ns, "server_name", selected);
                }
            }
        }
    }

    // Also update graph's NodeDef so save reflects the change
    NodeDef* ndef = graph_.find_node(node_id);
    if (ndef) ndef->params[param] = value;

    // Mark variation dirty if we have an active variation
    if (graph_.active_variation() >= 0)
        variation_dirty_ = true;
    mark_graph_dirty();

    std::ostringstream oss;
    oss << node_id << "/" << param << " = " << value;
    return {true, oss.str()};
}

CommandResult RuntimeAPI::set_string_param(const std::string& node_id, const std::string& param,
                                           const std::string& value) {
    // WGSLFilter preset selection — triggers a full graph rebuild
    if (param == "filter") {
        NodeDef* ndef = graph_.find_node(node_id);
        if (ndef && (ndef->type == "WGSLFilter" || registry_.is_wgsl_preset(ndef->type))) {
            ndef->string_params["filter"] = value;
            pending_topology_change_ = true;
            mark_graph_dirty();
            return {true, node_id + "/filter = " + value};
        }
    }

    NodeState* ns = scheduler_.find_node_mut(node_id);
    if (!ns) return {false, "unknown node '" + node_id + "'"};

    auto fi = ns->file_param_indices.find(param);
    if (fi == ns->file_param_indices.end()) {
        return {false, "unknown string param '" + param + "' on " + node_id};
    }

    set_file_param_internal(*ns, param, value);
    NodeDef* ndef = graph_.find_node(node_id);
    if (ndef) ndef->string_params[param] = value;

    if (graph_.active_variation() >= 0)
        variation_dirty_ = true;
    mark_graph_dirty();

    return {true, node_id + "/" + param + " = " + value};
}

CommandResult RuntimeAPI::get_param(const std::string& node_id, const std::string& param) {
    const auto& nodes = scheduler_.nodes();
    for (const auto& ns : nodes) {
        if (ns.node_id != node_id) continue;
        auto pi = ns.param_indices.find(param);
        if (pi == ns.param_indices.end()) {
            return {false, "unknown param '" + param + "' on " + node_id};
        }
        std::ostringstream oss;
        oss << ns.param_values[pi->second];
        return {true, oss.str()};
    }
    return {false, "unknown node '" + node_id + "'"};
}

// --- Per-parameter lock flags ---

CommandResult RuntimeAPI::set_param_lock(const std::string& node_id, const std::string& param, uint8_t flags) {
    NodeState* ns = scheduler_.find_node_mut(node_id);
    if (!ns) return {false, "unknown node '" + node_id + "'"};

    auto pi = ns->param_indices.find(param);
    if (pi == ns->param_indices.end())
        return {false, "unknown param '" + param + "' on " + node_id};

    ns->param_lock_flags[pi->second] = flags;

    // Persist in NodeDef
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
    const auto& nodes = scheduler_.nodes();
    for (const auto& ns : nodes) {
        if (ns.node_id != node_id) continue;
        auto pi = ns.param_indices.find(param);
        if (pi == ns.param_indices.end())
            return {false, "unknown param '" + param + "' on " + node_id};
        std::ostringstream oss;
        oss << static_cast<int>(ns.param_lock_flags[pi->second]);
        return {true, oss.str()};
    }
    return {false, "unknown node '" + node_id + "'"};
}

// --- Layout ---

CommandResult RuntimeAPI::set_node_layout(const std::string& node_id, float x, float y) {
    NodeDef* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};
    ndef->layout_x = x;
    ndef->layout_y = y;
    mark_graph_dirty();
    return {true, "layout set"};
}

// --- Resolution ---

CommandResult RuntimeAPI::set_resolution(const std::string& node_id, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return {false, "resolution must be non-zero"};
    if (width > 8192 || height > 8192)
        return {false, "resolution exceeds 8192 limit"};

    // Update graph NodeDef
    NodeDef* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};
    ndef->tex_width  = width;
    ndef->tex_height = height;

    // Update live scheduler NodeState
    NodeState* ns = scheduler_.find_node_mut(node_id);
    if (ns) {
        ns->gpu_tex_width  = width;
        ns->gpu_tex_height = height;
        ns->generation++;
    }

    needs_gpu_realloc_ = true;
    mark_graph_dirty();

    std::ostringstream oss;
    oss << node_id << " resolution = " << width << "x" << height;
    return {true, oss.str()};
}

// --- Buffered topology changes ---

CommandResult RuntimeAPI::add_node(const std::string& type, const std::string& id) {
    if (!registry_.find(type) && !registry_.is_wgsl_preset(type)) {
        return {false, "unknown type '" + type + "'"};
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
                                  bool semantic_defaults) {
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
                                                float to_min, float to_max, bool clamp) {
    std::string fn, fp, tn, tp;
    if (!split_addr(from_addr, fn, fp) || !split_addr(to_addr, tn, tp)) {
        return {false, "invalid address (expected node/port)"};
    }
    if (!graph_.set_connection_remap(fn, fp, tn, tp, from_min, from_max, to_min, to_max, clamp)) {
        return {false, "connection not found"};
    }
    pending_topology_change_ = true;
    mark_graph_dirty();
    return {true, "set remap on " + from_addr + " -> " + to_addr};
}

// --- apply_pending: full rebuild with param preservation ---

bool RuntimeAPI::apply_pending(bool& has_gpu_ops, bool& has_audio) {
    if (!pending_topology_change_) return false;
    pending_topology_change_ = false;
    active_crossfades_.clear();

    // 1. Save current param values and lock flags by node_id + param_name
    std::unordered_map<std::string, std::unordered_map<std::string, float>> saved_params;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> saved_string_params;
    std::unordered_map<std::string, std::unordered_map<std::string, uint8_t>> saved_locks;
    for (const auto& ns : scheduler_.nodes()) {
        auto& sp = saved_params[ns.node_id];
        for (const auto& [name, idx] : ns.param_indices) {
            sp[name] = ns.param_values[idx];
            if (ns.param_lock_flags[idx] != PARAM_LOCK_NONE)
                saved_locks[ns.node_id][name] = ns.param_lock_flags[idx];
        }
        auto& ssp = saved_string_params[ns.node_id];
        for (const auto& [name, idx] : ns.file_param_indices) {
            ssp[name] = ns.file_param_storage[idx];
        }
    }

    // 2. Shutdown audio if running
    bool had_audio = has_audio;
    if (had_audio) {
        audio_engine_.shutdown();
        has_audio = false;
    }

    // 3. Shutdown scheduler
    scheduler_.shutdown();

    // 4. Rebuild scheduler from (mutated) graph
    if (!scheduler_.build(graph_, registry_)) {
        std::fprintf(stderr, "[vivid] RuntimeAPI: rebuild failed\n");
        has_gpu_ops = false;
        has_audio = false;
        return true;
    }

    // 5. Restore saved params
    for (auto& ns : scheduler_.nodes_mut()) {
        auto sit = saved_params.find(ns.node_id);
        if (sit != saved_params.end()) {
            for (const auto& [pname, pval] : sit->second) {
                auto pi = ns.param_indices.find(pname);
                if (pi != ns.param_indices.end()) {
                    ns.param_values[pi->second] = pval;
                }
            }
        }
        auto ssit = saved_string_params.find(ns.node_id);
        if (ssit != saved_string_params.end()) {
            for (const auto& [pname, pval] : ssit->second) {
                auto fi = ns.file_param_indices.find(pname);
                if (fi != ns.file_param_indices.end()) {
                    ns.file_param_storage[fi->second] = pval;
                    ns.file_param_ptrs[fi->second] = ns.file_param_storage[fi->second].c_str();
                }
            }
        }
        // Restore lock flags
        auto lit = saved_locks.find(ns.node_id);
        if (lit != saved_locks.end()) {
            for (const auto& [pname, flags] : lit->second) {
                auto pi = ns.param_indices.find(pname);
                if (pi != ns.param_indices.end())
                    ns.param_lock_flags[pi->second] = flags;
            }
        }
    }

    has_gpu_ops = scheduler_.has_gpu_operators();

    // 6. Rebuild audio if there are audio operators
    if (scheduler_.has_audio_operators()) {
        if (audio_engine_.build(graph_, registry_, scheduler_)) {
            if (audio_engine_.start()) {
                has_audio = true;
            }
        }
    }

    return true;
}

// --- Inspection ---

CommandResult RuntimeAPI::inspect(const std::string& node_id) {
    const auto& nodes = scheduler_.nodes();
    for (const auto& ns : nodes) {
        if (ns.node_id != node_id) continue;
        const auto* desc = ns.loader->descriptor();
        std::ostringstream oss;
        oss << node_id << " (" << desc->name << ")\n";
        oss << "  params:";
        for (const auto& [name, idx] : ns.param_indices) {
            oss << " " << name << "=" << ns.param_values[idx];
            uint8_t lock = ns.param_lock_flags[idx];
            if (lock & PARAM_LOCK_WIRES) oss << "[W]";
            if (lock & PARAM_LOCK_PRESETS) oss << "[P]";
        }
        oss << "\n  outputs:";
        for (const auto& [name, idx] : ns.output_port_indices) {
            oss << " " << name << "=" << ns.output_values[idx];
            if (idx < ns.output_string_values.size() && !ns.output_string_values[idx].empty())
                oss << " \"" << ns.output_string_values[idx] << "\"";
            if (idx < ns.output_spreads.size() && !ns.output_spreads[idx].empty()) {
                oss << " [";
                for (size_t si = 0; si < ns.output_spreads[idx].size(); ++si) {
                    if (si > 0) oss << ",";
                    oss << ns.output_spreads[idx][si];
                }
                oss << "]";
            }
            if (idx < ns.output_string_spreads.size() && !ns.output_string_spreads[idx].empty()) {
                oss << " [";
                for (size_t si = 0; si < ns.output_string_spreads[idx].size(); ++si) {
                    if (si > 0) oss << ",";
                    oss << "\"" << ns.output_string_spreads[idx][si] << "\"";
                }
                oss << "]";
            }
        }
        if (!ns.input_port_indices.empty()) {
            oss << "\n  inputs:";
            for (const auto& [name, idx] : ns.input_port_indices) {
                oss << " " << name << "=" << ns.input_values[idx];
                if (idx < ns.input_string_values.size() && !ns.input_string_values[idx].empty())
                    oss << " \"" << ns.input_string_values[idx] << "\"";
                if (idx < ns.input_spreads.size() && !ns.input_spreads[idx].empty()) {
                    oss << " [";
                    for (size_t si = 0; si < ns.input_spreads[idx].size(); ++si) {
                        if (si > 0) oss << ",";
                        oss << ns.input_spreads[idx][si];
                    }
                    oss << "]";
                }
                if (idx < ns.input_string_spreads.size() && !ns.input_string_spreads[idx].empty()) {
                    oss << " [";
                    for (size_t si = 0; si < ns.input_string_spreads[idx].size(); ++si) {
                        if (si > 0) oss << ",";
                        oss << "\"" << ns.input_string_spreads[idx][si] << "\"";
                    }
                    oss << "]";
                }
            }
        }
        return {true, oss.str()};
    }
    return {false, "unknown node '" + node_id + "'"};
}

CommandResult RuntimeAPI::list_nodes() {
    const auto& nodes = scheduler_.nodes();
    if (nodes.empty()) return {true, "(no nodes)"};
    std::ostringstream oss;
    for (const auto& ns : nodes) {
        const auto* desc = ns.loader->descriptor();
        oss << ns.node_id << " (" << desc->name << ")\n";
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

// --- MIDI Mapping ---

CommandResult RuntimeAPI::add_midi_mapping(const std::string& node_id, const std::string& param,
                                           int cc, int channel, float range_min, float range_max) {
    graph_.add_midi_mapping(node_id, param, cc, channel, range_min, range_max);
    mark_graph_dirty();
    return {true, "mapped CC " + std::to_string(cc) + " -> " + node_id + "/" + param};
}

CommandResult RuntimeAPI::remove_midi_mapping(const std::string& node_id, const std::string& param) {
    if (!graph_.remove_midi_mapping(node_id, param))
        return {false, "no mapping for " + node_id + "/" + param};
    mark_graph_dirty();
    return {true, "unmapped " + node_id + "/" + param};
}

CommandResult RuntimeAPI::update_midi_mapping(const std::string& node_id, const std::string& param,
                                              float range_min, float range_max) {
    if (!graph_.update_midi_mapping(node_id, param, range_min, range_max))
        return {false, "no mapping for " + node_id + "/" + param};
    mark_graph_dirty();
    return {true, "updated range for " + node_id + "/" + param};
}

void RuntimeAPI::apply_midi_mappings() {
    if (!system_midi_ || !system_midi_->is_open()) return;

    system_midi_->drain_cc_events();

    for (const auto& mm : graph_.midi_mappings()) {
        // For omni (channel 0), check all 16 channels and use highest value
        float raw = 0.0f;
        if (mm.channel == 0) {
            for (int ch = 1; ch <= 16; ++ch) {
                float v = system_midi_->cc_value(ch, mm.cc_number);
                if (v > raw) raw = v;
            }
        } else {
            raw = system_midi_->cc_value(mm.channel, mm.cc_number);
        }

        // Remap [0,1] -> [range_min, range_max]
        float mapped = mm.range_min + raw * (mm.range_max - mm.range_min);
        set_param(mm.node_id, mm.param_name, mapped);
    }
}

// --- Variations ---

CommandResult RuntimeAPI::save_variation(const std::string& name) {
    VariationDef vd;
    vd.name = name;
    for (const auto& ns : scheduler_.nodes()) {
        const auto* desc = ns.loader->descriptor();
        auto& pm = vd.params[ns.node_id];
        for (const auto& [pname, idx] : ns.param_indices) {
            // Delta encoding: only store non-default values
            if (idx < desc->param_count &&
                desc->params[idx].type != VIVID_PARAM_FILE &&
                desc->params[idx].type != VIVID_PARAM_TEXT &&
                ns.param_values[idx] != desc->params[idx].default_value) {
                pm[pname] = ns.param_values[idx];
            }
        }
        // Remove empty node entries
        if (pm.empty()) vd.params.erase(ns.node_id);

        // String params: only store non-default, relativized for persistence
        for (const auto& [pname, fidx] : ns.file_param_indices) {
            const char* def_str = nullptr;
            auto pi = ns.param_indices.find(pname);
            if (pi != ns.param_indices.end() && pi->second < desc->param_count)
                def_str = desc->params[pi->second].default_string;
            const auto& val = ns.file_param_storage[fidx];
            if (!val.empty() && (def_str == nullptr || val != def_str)) {
                vd.string_params[ns.node_id][pname] = to_persisted_string_value(ns, pname, val);
            }
        }
    }
    graph_.add_variation(std::move(vd));
    int idx = graph_.find_variation_index(name);
    graph_.set_active_variation(idx);
    variation_dirty_ = false;
    mark_graph_dirty();
    return {true, "saved variation '" + name + "'"};
}

CommandResult RuntimeAPI::recall_variation(const std::string& name) {
    int idx = graph_.find_variation_index(name);
    if (idx < 0) return {false, "unknown variation '" + name + "'"};
    return recall_variation_idx(idx);
}

CommandResult RuntimeAPI::recall_variation_idx(int idx) {
    const auto& vars = graph_.variations();
    if (idx < 0 || idx >= static_cast<int>(vars.size()))
        return {false, "variation index out of range"};
    apply_variation(idx);
    return {true, "recalled variation '" + vars[idx].name + "'"};
}

void RuntimeAPI::apply_variation(int idx) {
    const auto& vd = graph_.variations()[idx];

    // Phase 1: Reset all unlocked params to defaults
    for (auto& ns : scheduler_.nodes_mut()) {
        const auto* desc = ns.loader->descriptor();
        NodeDef* ndef = graph_.find_node(ns.node_id);
        for (const auto& [pname, pidx] : ns.param_indices) {
            if (ns.param_lock_flags[pidx] & PARAM_LOCK_PRESETS) continue;
            if (pidx < desc->param_count &&
                desc->params[pidx].type != VIVID_PARAM_FILE &&
                desc->params[pidx].type != VIVID_PARAM_TEXT) {
                ns.param_values[pidx] = desc->params[pidx].default_value;
                if (ndef) ndef->params[pname] = desc->params[pidx].default_value;
            }
        }
        // Reset string params to defaults
        for (const auto& [pname, fidx] : ns.file_param_indices) {
            auto pi = ns.param_indices.find(pname);
            if (pi != ns.param_indices.end() &&
                (ns.param_lock_flags[pi->second] & PARAM_LOCK_PRESETS))
                continue;
            if (pi != ns.param_indices.end() && pi->second < desc->param_count) {
                const char* def_str = desc->params[pi->second].default_string;
                std::string def_val = def_str ? def_str : "";
                ns.file_param_storage[fidx] = def_val;
                ns.file_param_ptrs[fidx] = ns.file_param_storage[fidx].c_str();
                if (ndef) {
                    if (def_val.empty())
                        ndef->string_params.erase(pname);
                    else
                        ndef->string_params[pname] = def_val;
                }
            }
        }
        ns.generation++;
    }

    // Phase 2: Apply stored delta values
    for (const auto& [node_id, pm] : vd.params) {
        NodeState* ns = scheduler_.find_node_mut(node_id);
        if (!ns) continue;
        for (const auto& [pname, pval] : pm) {
            auto pi = ns->param_indices.find(pname);
            if (pi != ns->param_indices.end()) {
                if (!(ns->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS)) {
                    ns->param_values[pi->second] = pval;
                    NodeDef* ndef = graph_.find_node(node_id);
                    if (ndef) ndef->params[pname] = pval;
                }
            }
        }
        ns->generation++;
    }

    // Apply stored string param deltas (resolve for runtime, keep relative for persistence)
    for (const auto& [node_id, spm] : vd.string_params) {
        NodeState* ns = scheduler_.find_node_mut(node_id);
        if (!ns) continue;
        for (const auto& [pname, pval] : spm) {
            auto fi = ns->file_param_indices.find(pname);
            if (fi == ns->file_param_indices.end()) continue;
            auto pi = ns->param_indices.find(pname);
            if (pi != ns->param_indices.end() &&
                (ns->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS))
                continue;
            ns->file_param_storage[fi->second] = to_runtime_string_value(*ns, pname, pval);
            ns->file_param_ptrs[fi->second] = ns->file_param_storage[fi->second].c_str();
            NodeDef* ndef = graph_.find_node(node_id);
            if (ndef) ndef->string_params[pname] = pval;  // already relative
        }
        ns->generation++;
    }

    graph_.set_active_variation(idx);
    variation_dirty_ = false;
    pending_variation_.armed = false;
    mark_graph_dirty();
}

CommandResult RuntimeAPI::remove_variation(const std::string& name) {
    if (!graph_.remove_variation(name))
        return {false, "unknown variation '" + name + "'"};
    mark_graph_dirty();
    return {true, "removed variation '" + name + "'"};
}

CommandResult RuntimeAPI::rename_variation(const std::string& old_name, const std::string& new_name) {
    if (!graph_.rename_variation(old_name, new_name))
        return {false, "rename failed (not found or name conflict)"};
    mark_graph_dirty();
    return {true, "renamed '" + old_name + "' to '" + new_name + "'"};
}

CommandResult RuntimeAPI::duplicate_variation(const std::string& name, const std::string& new_name) {
    int src_idx = graph_.find_variation_index(name);
    if (!graph_.duplicate_variation(name, new_name))
        return {false, "duplicate failed (not found or name conflict)"};
    // Adjust pending_variation_ index if it's after the insertion point
    if (pending_variation_.armed && pending_variation_.variation_idx > src_idx)
        pending_variation_.variation_idx++;
    mark_graph_dirty();
    return {true, "duplicated '" + name + "' as '" + new_name + "'"};
}

CommandResult RuntimeAPI::move_variation(const std::string& name, int to_index) {
    int from_idx = graph_.find_variation_index(name);
    if (from_idx < 0) return {false, "unknown variation '" + name + "'"};
    if (!graph_.move_variation(name, to_index))
        return {false, "move failed (invalid index)"};
    // Adjust pending_variation_ index to track the same variation
    if (pending_variation_.armed) {
        int pi = pending_variation_.variation_idx;
        if (pi == from_idx) {
            pending_variation_.variation_idx = to_index;
        } else if (from_idx < pi && to_index >= pi) {
            pending_variation_.variation_idx--;
        } else if (from_idx > pi && to_index <= pi) {
            pending_variation_.variation_idx++;
        }
    }
    mark_graph_dirty();
    return {true, "moved '" + name + "' to index " + std::to_string(to_index)};
}

CommandResult RuntimeAPI::update_variation(const std::string& name) {
    auto* vd = graph_.find_variation(name);
    if (!vd) return {false, "unknown variation '" + name + "'"};
    vd->params.clear();
    vd->string_params.clear();
    for (const auto& ns : scheduler_.nodes()) {
        const auto* desc = ns.loader->descriptor();
        auto& pm = vd->params[ns.node_id];
        for (const auto& [pname, idx] : ns.param_indices) {
            if (idx < desc->param_count &&
                desc->params[idx].type != VIVID_PARAM_FILE &&
                desc->params[idx].type != VIVID_PARAM_TEXT &&
                ns.param_values[idx] != desc->params[idx].default_value) {
                pm[pname] = ns.param_values[idx];
            }
        }
        if (pm.empty()) vd->params.erase(ns.node_id);

        for (const auto& [pname, fidx] : ns.file_param_indices) {
            const char* def_str = nullptr;
            auto pi = ns.param_indices.find(pname);
            if (pi != ns.param_indices.end() && pi->second < desc->param_count)
                def_str = desc->params[pi->second].default_string;
            const auto& val = ns.file_param_storage[fidx];
            if (!val.empty() && (def_str == nullptr || val != def_str)) {
                vd->string_params[ns.node_id][pname] = to_persisted_string_value(ns, pname, val);
            }
        }
    }
    variation_dirty_ = false;
    mark_graph_dirty();
    return {true, "updated variation '" + name + "'"};
}

CommandResult RuntimeAPI::list_variations() {
    const auto& vars = graph_.variations();
    if (vars.empty()) return {true, "(no variations)"};
    std::ostringstream oss;
    for (size_t i = 0; i < vars.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << vars[i].name;
        if (static_cast<int>(i) == graph_.active_variation()) oss << " *";
    }
    return {true, oss.str()};
}

CommandResult RuntimeAPI::queue_variation(const std::string& name, const std::string& quantize) {
    int idx = graph_.find_variation_index(name);
    if (idx < 0) return {false, "unknown variation '" + name + "'"};

    PendingVariation::Quantize q = PendingVariation::Instant;
    if (quantize == "beat") q = PendingVariation::Beat;
    else if (quantize == "bar") q = PendingVariation::Bar;
    else if (quantize == "4bar") q = PendingVariation::FourBar;

    if (q == PendingVariation::Instant) {
        apply_variation(idx);
        return {true, "recalled variation '" + name + "' (instant)"};
    }

    pending_variation_.variation_idx = idx;
    pending_variation_.quantize = q;
    pending_variation_.armed = true;
    pending_variation_.beats_remaining =
        (q == PendingVariation::Bar) ? 4 :
        (q == PendingVariation::FourBar) ? 16 : 1;

    return {true, "queued variation '" + name + "' (" + quantize + ")"};
}

CommandResult RuntimeAPI::set_quantize_clock(const std::string& node_id) {
    graph_.set_quantize_clock_node(node_id);
    mark_graph_dirty();
    return {true, "quantize clock set to '" + node_id + "'"};
}

void RuntimeAPI::tick_quantized_switch() {
    if (!pending_variation_.armed) return;

    const auto& clock_id = graph_.quantize_clock_node();
    if (clock_id.empty()) {
        // No clock — instant fallback
        apply_variation(pending_variation_.variation_idx);
        return;
    }

    const NodeState* clock_ns = nullptr;
    for (const auto& ns : scheduler_.nodes()) {
        if (ns.node_id == clock_id) { clock_ns = &ns; break; }
    }
    if (!clock_ns) {
        // Clock node missing — instant fallback
        apply_variation(pending_variation_.variation_idx);
        return;
    }

    // Read beat_phase output
    auto bp_it = clock_ns->output_port_indices.find("beat_phase");
    if (bp_it == clock_ns->output_port_indices.end() ||
        bp_it->second >= clock_ns->output_values.size()) {
        // No beat_phase output — instant fallback
        apply_variation(pending_variation_.variation_idx);
        return;
    }

    float beat_phase = clock_ns->output_values[bp_it->second];
    bool zero_crossing = (beat_phase < prev_beat_phase_) && (prev_beat_phase_ > 0.5f);
    prev_beat_phase_ = beat_phase;

    if (!zero_crossing) return;

    pending_variation_.beats_remaining--;
    if (pending_variation_.beats_remaining <= 0) {
        apply_variation(pending_variation_.variation_idx);
    }
}

// --- Per-Operator Presets ---

const std::string& RuntimeAPI::active_preset(const std::string& node_id) const {
    static const std::string empty;
    auto it = active_presets_.find(node_id);
    return (it != active_presets_.end()) ? it->second : empty;
}

CommandResult RuntimeAPI::save_preset(const std::string& node_id, const std::string& name) {
    NodeState* ns = scheduler_.find_node_mut(node_id);
    if (!ns) return {false, "unknown node '" + node_id + "'"};

    OperatorPreset preset;
    preset.name = name;
    for (const auto& [pname, idx] : ns->param_indices) {
        preset.params[pname] = ns->param_values[idx];
    }
    for (const auto& [pname, idx] : ns->file_param_indices) {
        preset.string_params[pname] = to_persisted_string_value(*ns, pname, ns->file_param_storage[idx]);
    }
    graph_.save_preset(node_id, preset);
    active_presets_[node_id] = name;
    mark_graph_dirty();
    return {true, "saved preset '" + name + "' on " + node_id};
}

CommandResult RuntimeAPI::recall_preset(const std::string& node_id, const std::string& name) {
    // Check user presets first, then fall through to factory presets
    const auto* preset = graph_.find_preset(node_id, name);

    // Factory preset fallback: look up by node type
    const OperatorPreset* factory_hit = nullptr;
    if (!preset) {
        const auto* ndef = graph_.find_node(node_id);
        if (ndef) {
            const auto* fps = registry_.factory_presets(ndef->type);
            if (fps) {
                for (const auto& fp : *fps) {
                    if (fp.name == name) { factory_hit = &fp; break; }
                }
            }
        }
        preset = factory_hit;
    }

    if (!preset) return {false, "preset '" + name + "' not found on " + node_id};

    NodeState* ns = scheduler_.find_node_mut(node_id);
    if (!ns) return {false, "unknown node '" + node_id + "'"};

    for (const auto& [pname, pval] : preset->params) {
        auto pi = ns->param_indices.find(pname);
        if (pi != ns->param_indices.end()) {
            if (!(ns->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS)) {
                ns->param_values[pi->second] = pval;
                NodeDef* ndef = graph_.find_node(node_id);
                if (ndef) ndef->params[pname] = pval;
            }
        }
    }
    {
        for (const auto& [pname, pval] : preset->string_params) {
            auto fi = ns->file_param_indices.find(pname);
            if (fi != ns->file_param_indices.end()) {
                // Check lock via param_indices (file params share the lock namespace)
                auto pi = ns->param_indices.find(pname);
                if (pi != ns->param_indices.end() &&
                    (ns->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS))
                    continue;
                ns->file_param_storage[fi->second] = to_runtime_string_value(*ns, pname, pval);
                ns->file_param_ptrs[fi->second] = ns->file_param_storage[fi->second].c_str();
                NodeDef* ndef = graph_.find_node(node_id);
                if (ndef) ndef->string_params[pname] = pval;  // already relative
            }
        }
    }
    ns->generation++;
    active_presets_[node_id] = name;
    mark_graph_dirty();
    return {true, "recalled preset '" + name + "' on " + node_id};
}

CommandResult RuntimeAPI::update_preset(const std::string& node_id, const std::string& name) {
    auto* preset = graph_.find_preset(node_id, name);
    if (!preset) {
        // Check if it's a factory preset (read-only)
        const auto* ndef = graph_.find_node(node_id);
        if (ndef) {
            const auto* fps = registry_.factory_presets(ndef->type);
            if (fps) {
                for (const auto& fp : *fps) {
                    if (fp.name == name)
                        return {false, "cannot modify factory preset '" + name + "'"};
                }
            }
        }
        return {false, "preset '" + name + "' not found on " + node_id};
    }

    NodeState* ns = scheduler_.find_node_mut(node_id);
    if (!ns) return {false, "unknown node '" + node_id + "'"};

    preset->params.clear();
    for (const auto& [pname, idx] : ns->param_indices) {
        preset->params[pname] = ns->param_values[idx];
    }
    preset->string_params.clear();
    {
        for (const auto& [pname, idx] : ns->file_param_indices) {
            preset->string_params[pname] = to_persisted_string_value(*ns, pname, ns->file_param_storage[idx]);
        }
    }
    mark_graph_dirty();
    return {true, "updated preset '" + name + "' on " + node_id};
}

CommandResult RuntimeAPI::remove_preset(const std::string& node_id, const std::string& name) {
    if (!graph_.remove_preset(node_id, name)) {
        // Check if it's a factory preset (read-only)
        const auto* ndef = graph_.find_node(node_id);
        if (ndef) {
            const auto* fps = registry_.factory_presets(ndef->type);
            if (fps) {
                for (const auto& fp : *fps) {
                    if (fp.name == name)
                        return {false, "cannot modify factory preset '" + name + "'"};
                }
            }
        }
        return {false, "preset '" + name + "' not found on " + node_id};
    }
    mark_graph_dirty();
    return {true, "removed preset '" + name + "' from " + node_id};
}

CommandResult RuntimeAPI::rename_preset(const std::string& node_id, const std::string& old_name,
                                         const std::string& new_name) {
    if (!graph_.rename_preset(node_id, old_name, new_name)) {
        // Check if it's a factory preset (read-only)
        const auto* ndef = graph_.find_node(node_id);
        if (ndef) {
            const auto* fps = registry_.factory_presets(ndef->type);
            if (fps) {
                for (const auto& fp : *fps) {
                    if (fp.name == old_name)
                        return {false, "cannot modify factory preset '" + old_name + "'"};
                }
            }
        }
        return {false, "rename failed (not found or name conflict)"};
    }
    mark_graph_dirty();
    return {true, "renamed preset '" + old_name + "' to '" + new_name + "' on " + node_id};
}

CommandResult RuntimeAPI::list_presets(const std::string& node_id) {
    auto names = graph_.list_presets(node_id);
    if (names.empty()) return {true, "(no presets on " + node_id + ")"};
    std::ostringstream oss;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << names[i];
    }
    return {true, oss.str()};
}

CommandResult RuntimeAPI::list_factory_presets(const std::string& node_id) {
    const auto* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};
    auto names = registry_.factory_preset_names(ndef->type);
    if (names.empty()) return {true, "(no factory presets for " + ndef->type + ")"};
    std::ostringstream oss;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << names[i];
    }
    return {true, oss.str()};
}

// --- State-Preset Mapping ---

CommandResult RuntimeAPI::set_state_preset(const std::string& sm_node, int state_idx,
                                            const std::string& target_node,
                                            const std::string& preset_name) {
    if (state_idx < 0 || state_idx > 7)
        return {false, "state index must be 0-7"};
    graph_.set_state_preset(sm_node, state_idx, target_node, preset_name);
    mark_graph_dirty();
    return {true, "bound " + sm_node + " state " + std::to_string(state_idx)
                  + " -> " + target_node + "/" + preset_name};
}

CommandResult RuntimeAPI::remove_state_preset(const std::string& sm_node, int state_idx,
                                               const std::string& target_node) {
    if (!graph_.remove_state_preset(sm_node, state_idx, target_node))
        return {false, "binding not found"};
    mark_graph_dirty();
    return {true, "removed binding"};
}

CommandResult RuntimeAPI::clear_state_presets(const std::string& sm_node) {
    graph_.clear_state_presets(sm_node);
    prev_sm_state_.erase(sm_node);
    mark_graph_dirty();
    return {true, "cleared all state-preset mappings for " + sm_node};
}

CommandResult RuntimeAPI::inspect_state_presets(const std::string& sm_node) {
    const auto* spm = graph_.find_state_mapping(sm_node);
    if (!spm) return {true, "(no state-preset mappings for " + sm_node + ")"};
    std::ostringstream oss;
    for (size_t i = 0; i < spm->state_presets.size(); ++i) {
        oss << "state " << i << ":";
        if (spm->state_presets[i].empty()) {
            oss << " (none)";
        } else {
            for (const auto& [target, preset] : spm->state_presets[i]) {
                oss << " " << target << "=" << preset;
            }
        }
        oss << "\n";
    }
    std::string result = oss.str();
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return {true, result};
}

void RuntimeAPI::tick_state_presets() {
    // Clean up orphaned crossfades for removed state machines
    for (auto it = active_crossfades_.begin(); it != active_crossfades_.end(); ) {
        bool found = false;
        for (const auto& spm : graph_.state_preset_mappings()) {
            if (spm.state_machine_node == it->first) { found = true; break; }
        }
        if (!found)
            it = active_crossfades_.erase(it);
        else
            ++it;
    }

    for (const auto& spm : graph_.state_preset_mappings()) {
        const NodeState* sm_ns = nullptr;
        for (const auto& ns : scheduler_.nodes()) {
            if (ns.node_id == spm.state_machine_node) { sm_ns = &ns; break; }
        }
        if (!sm_ns) continue;

        // Read the "state" output
        auto oi = sm_ns->output_port_indices.find("state");
        if (oi == sm_ns->output_port_indices.end()) continue;
        float current_state = sm_ns->output_values[oi->second];

        // Phase 1: detect state change (use -1 sentinel so initial state 0 is detected)
        auto [prev_it, first_seen] = prev_sm_state_.emplace(spm.state_machine_node, -1.0f);
        if (current_state != prev_it->second) {
            prev_it->second = current_state;

            int state_idx = static_cast<int>(current_state);
            if (state_idx < 0 || state_idx >= static_cast<int>(spm.state_presets.size()))
                continue;

            // Read xfade_mode and xfade_bars from the SM's params
            int xf_mode = 0;
            float xf_bars = 0.0f;
            auto xm_it = sm_ns->param_indices.find("xfade_mode");
            if (xm_it != sm_ns->param_indices.end())
                xf_mode = static_cast<int>(sm_ns->param_values[xm_it->second]);
            auto xb_it = sm_ns->param_indices.find("xfade_bars");
            if (xb_it != sm_ns->param_indices.end())
                xf_bars = sm_ns->param_values[xb_it->second];

            if (xf_mode == 0 || xf_bars <= 0.0f) {
                // Hard cut — existing behavior
                active_crossfades_.erase(spm.state_machine_node);
                for (const auto& [target_node, preset_name] : spm.state_presets[state_idx]) {
                    recall_preset(target_node, preset_name);
                }
            } else {
                // Initiate crossfade: snapshot current values, look up targets
                ActiveCrossfade ac;
                ac.sm_node_id = spm.state_machine_node;
                for (const auto& [target_node, preset_name] : spm.state_presets[state_idx]) {
                    const auto* preset = graph_.find_preset(target_node, preset_name);
                    if (!preset) continue;
                    NodeState* tns = scheduler_.find_node_mut(target_node);
                    if (!tns) continue;

                    CrossfadeState cs;
                    cs.target_preset_name = preset_name;
                    for (const auto& [pname, pval] : preset->params) {
                        auto pi = tns->param_indices.find(pname);
                        if (pi == tns->param_indices.end()) continue;
                        if (tns->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS) continue;
                        // Snapshot current value (may be mid-lerp if interrupted)
                        cs.start_params[pname] = tns->param_values[pi->second];
                        cs.target_params[pname] = pval;
                    }
                    // String params: switch immediately (not interpolatable)
                    for (const auto& [pname, pval] : preset->string_params) {
                        auto fi = tns->file_param_indices.find(pname);
                        if (fi == tns->file_param_indices.end()) continue;
                        auto pi = tns->param_indices.find(pname);
                        if (pi != tns->param_indices.end() &&
                            (tns->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS))
                            continue;
                        tns->file_param_storage[fi->second] = to_runtime_string_value(*tns, pname, pval);
                        tns->file_param_ptrs[fi->second] = tns->file_param_storage[fi->second].c_str();
                        NodeDef* ndef = graph_.find_node(target_node);
                        if (ndef) ndef->string_params[pname] = pval;  // already relative
                    }
                    ac.targets[target_node] = std::move(cs);
                }
                active_crossfades_[spm.state_machine_node] = std::move(ac);
            }
        }

        // Phase 2: interpolate active crossfades
        auto acit = active_crossfades_.find(spm.state_machine_node);
        if (acit == active_crossfades_.end()) continue;

        // Read SM's xfade output (0->1 progress)
        auto xf_oi = sm_ns->output_port_indices.find("xfade");
        if (xf_oi == sm_ns->output_port_indices.end()) continue;
        float xfade_t = sm_ns->output_values[xf_oi->second];

        for (auto& [target_node, cs] : acit->second.targets) {
            NodeState* tns = scheduler_.find_node_mut(target_node);
            if (!tns) continue;
            NodeDef* ndef = graph_.find_node(target_node);
            for (const auto& [pname, start_val] : cs.start_params) {
                auto pi = tns->param_indices.find(pname);
                if (pi == tns->param_indices.end()) continue;
                float target_val = cs.target_params[pname];
                float interp = start_val + (target_val - start_val) * xfade_t;
                tns->param_values[pi->second] = interp;
                if (ndef) ndef->params[pname] = interp;
            }
            tns->generation++;
        }

        // Finalize when crossfade completes: snap params to exact target values
        if (xfade_t >= 1.0f) {
            for (auto& [target_node, cs] : acit->second.targets) {
                NodeState* tns = scheduler_.find_node_mut(target_node);
                if (tns) {
                    NodeDef* ndef = graph_.find_node(target_node);
                    for (const auto& [pname, target_val] : cs.target_params) {
                        auto pi = tns->param_indices.find(pname);
                        if (pi != tns->param_indices.end()) {
                            tns->param_values[pi->second] = target_val;
                            if (ndef) ndef->params[pname] = target_val;
                        }
                    }
                    tns->generation++;
                }
                active_presets_[target_node] = cs.target_preset_name;
            }
            active_crossfades_.erase(acit);
        }
    }
}

// --- Solo mode ---

// --- Role binding operations ---

CommandResult RuntimeAPI::set_role_binding(const std::string& node_id, const std::string& role_id,
                                            const std::string& target_node_id,
                                            const std::string& target_output_name) {
    auto* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "node not found: " + node_id};

    // Validate host has this role
    const auto* host_desc = registry_.probe_descriptor(ndef->type);
    if (!host_desc) return {false, "host descriptor not found for: " + ndef->type};

    bool role_found = false;
    for (uint32_t i = 0; i < host_desc->role_binding_count; ++i) {
        if (host_desc->role_bindings[i].role_id &&
            role_id == host_desc->role_bindings[i].role_id) {
            role_found = true;
            break;
        }
    }
    if (!role_found) return {false, "role not found: " + role_id};

    // Validate target node exists
    auto* target_ndef = graph_.find_node(target_node_id);
    if (!target_ndef) return {false, "target node not found: " + target_node_id};

    // Validate target is bindable
    auto vr = validate_role_binding(host_desc, role_id, target_ndef->type,
                                    target_output_name, registry_);
    switch (vr) {
        case RoleBindingValidation::kOk: break;
        case RoleBindingValidation::kRoleNotFound:
            return {false, "role not found: " + role_id};
        case RoleBindingValidation::kNotBindable:
            return {false, "target not bindable: " + target_ndef->type};
        case RoleBindingValidation::kTypeNotAllowed:
            return {false, "type not allowed in role: " + target_ndef->type};
        case RoleBindingValidation::kDomainMismatch:
            return {false, "domain mismatch for: " + target_ndef->type};
        case RoleBindingValidation::kOutputNotFound:
            return {false, "output not found on target: " + target_output_name};
    }

    // Store binding
    auto& binding = ndef->role_bindings[role_id];
    binding.target_node_id = target_node_id;
    binding.target_output_name = target_output_name;

    pending_topology_change_ = true;
    mark_graph_dirty();
    return {true, "bound " + target_node_id + " to role " + role_id};
}

CommandResult RuntimeAPI::clear_role_binding(const std::string& node_id, const std::string& role_id) {
    auto* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "node not found: " + node_id};

    if (ndef->role_bindings.erase(role_id) == 0)
        return {false, "no binding for role: " + role_id};

    pending_topology_change_ = true;
    mark_graph_dirty();
    return {true, "cleared role " + role_id};
}

CommandResult RuntimeAPI::set_solo(const std::string& node_id) {
    if (node_id.empty()) {
        scheduler_.set_solo(-1);
        return {true, "solo cleared"};
    }
    const auto& nodes = scheduler_.nodes();
    for (uint32_t i = 0; i < static_cast<uint32_t>(nodes.size()); ++i) {
        if (nodes[i].node_id == node_id) {
            scheduler_.set_solo(static_cast<int>(i));
            return {true, "soloed " + node_id};
        }
    }
    return {false, "node not found: " + node_id};
}

std::string RuntimeAPI::solo_node_id() const {
    int idx = scheduler_.solo_node_idx();
    if (idx < 0 || idx >= static_cast<int>(scheduler_.nodes().size())) return {};
    return scheduler_.nodes()[idx].node_id;
}

// --- Persistence ---

CommandResult RuntimeAPI::save() {
    const auto& path = graph_.source_path();
    if (path.empty()) return {false, "no source path (use save <path>)"};
    return save_as(path);
}

CommandResult RuntimeAPI::save_as(const std::string& path) {
    if (graph_.save(path.c_str())) {
        const std::string normalized = normalized_graph_identity_path(path);
        graph_.set_source_path(path);
        active_graph_source_path_ = normalized;
        capture_saved_snapshot();
        return {true, "saved to " + path};
    }
    return {false, "failed to save to " + path};
}

CommandResult RuntimeAPI::reload(bool& has_gpu_ops, bool& has_audio) {
    const auto& path = graph_.source_path();
    if (path.empty()) return {false, "no source path to reload from"};
    return load_graph(path, has_gpu_ops, has_audio);
}

CommandResult RuntimeAPI::load_graph(const std::string& path,
                                     bool& has_gpu_ops,
                                     bool& has_audio) {
    if (path.empty()) return {false, "missing graph path"};
    const bool preserve_runtime_state =
        normalized_graph_identity_path(path) == active_graph_source_path_;
    std::string previous_graph_json;
    if (!graph_.save_to_string(previous_graph_json)) {
        return {false, "failed to serialize current graph before reload"};
    }
    const std::string previous_source_path = graph_.source_path();
    const std::string previous_active_graph_source_path = active_graph_source_path_;

    // Preserve live state only for same-graph reloads (e.g. hot reload).
    // Graph switches should always start from file-defined values.
    std::unordered_map<std::string, std::unordered_map<std::string, float>> saved_params;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> saved_string_params;
    std::unordered_map<std::string, std::unordered_map<std::string, uint8_t>> saved_locks;
    if (preserve_runtime_state) {
        for (const auto& ns : scheduler_.nodes()) {
            auto& sp = saved_params[ns.node_id];
            for (const auto& [name, idx] : ns.param_indices) {
                sp[name] = ns.param_values[idx];
                if (ns.param_lock_flags[idx] != PARAM_LOCK_NONE)
                    saved_locks[ns.node_id][name] = ns.param_lock_flags[idx];
            }
            auto& ssp = saved_string_params[ns.node_id];
            for (const auto& [name, idx] : ns.file_param_indices) {
                ssp[name] = ns.file_param_storage[idx];
            }
        }
    }

    auto restore_previous_state = [&](const std::string& reason) -> CommandResult {
        if (!graph_.load_from_string(previous_graph_json.c_str(), previous_graph_json.size(), true)) {
            has_gpu_ops = false;
            has_audio = false;
            return {false, reason + " (and failed to restore previous graph)"};
        }
        graph_.set_source_path(previous_source_path);

        if (!scheduler_.build(graph_, registry_)) {
            has_gpu_ops = false;
            has_audio = false;
            return {false, reason + " (and failed to rebuild previous graph)"};
        }

        has_gpu_ops = scheduler_.has_gpu_operators();
        if (has_gpu_ops) needs_gpu_realloc_ = true;

        has_audio = false;
        if (scheduler_.has_audio_operators()) {
            if (audio_engine_.build(graph_, registry_, scheduler_)) {
                if (audio_engine_.start()) {
                    has_audio = true;
                }
            }
        }

        active_graph_source_path_ = previous_active_graph_source_path;
        pending_topology_change_ = false;
        active_crossfades_.clear();
        return {false, reason};
    };

    bool had_audio = has_audio;
    if (had_audio) {
        audio_engine_.shutdown();
        has_audio = false;
    }
    scheduler_.shutdown();
    registry_.clear_retired_package_loaders();

    if (!graph_.load(path.c_str())) {
        return restore_previous_state("failed to reload " + path);
    }

    if (!scheduler_.build(graph_, registry_)) {
        return restore_previous_state("rebuild failed after reload");
    }

    if (preserve_runtime_state) {
        // Restore saved params to rebuilt nodes.
        for (auto& ns : scheduler_.nodes_mut()) {
            auto sit = saved_params.find(ns.node_id);
            if (sit != saved_params.end()) {
                for (const auto& [pname, pval] : sit->second) {
                    auto pi = ns.param_indices.find(pname);
                    if (pi != ns.param_indices.end())
                        ns.param_values[pi->second] = pval;
                }
            }
            auto ssit = saved_string_params.find(ns.node_id);
            if (ssit != saved_string_params.end()) {
                for (const auto& [pname, pval] : ssit->second) {
                    auto fi = ns.file_param_indices.find(pname);
                    if (fi != ns.file_param_indices.end()) {
                        ns.file_param_storage[fi->second] = pval;
                        ns.file_param_ptrs[fi->second] = ns.file_param_storage[fi->second].c_str();
                    }
                }
            }
            auto lit = saved_locks.find(ns.node_id);
            if (lit != saved_locks.end()) {
                for (const auto& [pname, flags] : lit->second) {
                    auto pi = ns.param_indices.find(pname);
                    if (pi != ns.param_indices.end())
                        ns.param_lock_flags[pi->second] = flags;
                }
            }
        }
    }

    has_gpu_ops = scheduler_.has_gpu_operators();
    if (has_gpu_ops) needs_gpu_realloc_ = true;

    if (scheduler_.has_audio_operators()) {
        if (audio_engine_.build(graph_, registry_, scheduler_)) {
            if (audio_engine_.start()) {
                has_audio = true;
            }
        }
    }

    active_graph_source_path_ = normalized_graph_identity_path(path);
    preserve_undo_history_on_reload_ = false;
    reload_serial_++;
    capture_saved_snapshot();
    return {true, "reloaded from " + path};
}

CommandResult RuntimeAPI::new_graph(bool& has_gpu_ops, bool& has_audio) {
    if (has_audio) { audio_engine_.shutdown(); has_audio = false; }
    scheduler_.shutdown();
    registry_.clear_retired_package_loaders();

    auto read_file = [](const std::string& path, std::string& out) -> bool {
        auto f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        auto sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        out.resize(sz);
        std::fread(out.data(), 1, sz, f);
        std::fclose(f);
        return true;
    };

    bool loaded = false;
    std::string buf;

    // 1. Try user template
    std::string user_path = get_config_dir() + "/default_graph.json";
    if (read_file(user_path, buf)) {
        loaded = graph_.load_from_string(buf.c_str(), buf.size(), false);
        if (!loaded)
            std::fprintf(stderr, "[vivid] Warning: custom default_graph.json is malformed, falling back to bundled default\n");
    }

    // 2. Try bundled template
    if (!loaded && !resources_dir_.empty()) {
        std::string bundled_path = resources_dir_ + "/default_graph.json";
        if (read_file(bundled_path, buf)) {
            loaded = graph_.load_from_string(buf.c_str(), buf.size(), false);
            if (!loaded)
                std::fprintf(stderr, "[vivid] Warning: bundled default_graph.json is malformed\n");
        } else {
            std::fprintf(stderr, "[vivid] Warning: bundled default_graph.json not found at %s\n", bundled_path.c_str());
        }
    }

    if (!loaded) {
        has_gpu_ops = false;
        return {false, "failed to load default graph template"};
    }
    if (!scheduler_.build(graph_, registry_)) {
        has_gpu_ops = false;
        return {false, "scheduler build failed for new graph"};
    }

    has_gpu_ops = scheduler_.has_gpu_operators();
    if (has_gpu_ops) needs_gpu_realloc_ = true;

    if (scheduler_.has_audio_operators()) {
        if (audio_engine_.build(graph_, registry_, scheduler_) && audio_engine_.start())
            has_audio = true;
    }

    active_graph_source_path_.clear();
    pending_topology_change_ = false;
    active_crossfades_.clear();
    preserve_undo_history_on_reload_ = false;
    reload_serial_++;
    capture_saved_snapshot();
    return {true, "new graph"};
}

CommandResult RuntimeAPI::new_project(const std::string& dir_path,
                                       bool& has_gpu_ops, bool& has_audio) {
    namespace fs = std::filesystem;
    std::error_code ec;

    // Create directory — fail if it already exists and is non-empty
    if (fs::exists(dir_path, ec)) {
        if (!fs::is_directory(dir_path, ec))
            return {false, "path exists and is not a directory: " + dir_path};
        if (!fs::is_empty(dir_path, ec))
            return {false, "directory already exists and is non-empty: " + dir_path};
    } else {
        if (!fs::create_directories(dir_path, ec))
            return {false, "failed to create directory: " + dir_path + " (" + ec.message() + ")"};
    }

    // Write default graph JSON to dir_path/graph.json
    std::string graph_path = (fs::path(dir_path) / "graph.json").string();

    // Use new_graph internals
    auto result = new_graph(has_gpu_ops, has_audio);
    if (!result.ok) return result;

    // Save the fresh graph to the project path and set source_path
    auto save_result = save_as(graph_path);
    if (!save_result.ok) return save_result;
    return {true, "new project at " + dir_path};
}

CommandResult RuntimeAPI::apply_snapshot_json(const std::string& graph_json,
                                              bool& has_gpu_ops, bool& has_audio) {
    std::string previous_graph_json;
    if (!graph_.save_to_string(previous_graph_json)) {
        return {false, "failed to serialize current graph before applying snapshot"};
    }

    bool had_audio = has_audio;
    auto restore_previous_state = [&](const std::string& reason) -> CommandResult {
        if (!graph_.load_from_string(previous_graph_json.c_str(), previous_graph_json.size(), true)) {
            has_gpu_ops = false;
            has_audio = false;
            return {false, reason + " (and failed to restore previous graph)"};
        }

        if (!scheduler_.build(graph_, registry_)) {
            has_gpu_ops = false;
            has_audio = false;
            return {false, reason + " (and failed to rebuild previous graph)"};
        }

        has_gpu_ops = scheduler_.has_gpu_operators();
        if (has_gpu_ops) needs_gpu_realloc_ = true;

        has_audio = false;
        if (scheduler_.has_audio_operators()) {
            if (audio_engine_.build(graph_, registry_, scheduler_)) {
                if (audio_engine_.start()) {
                    has_audio = true;
                }
            }
        }

        pending_topology_change_ = false;
        active_crossfades_.clear();
        preserve_undo_history_on_reload_ = false;
        return {false, reason};
    };

    if (had_audio) {
        audio_engine_.shutdown();
        has_audio = false;
    }
    scheduler_.shutdown();
    registry_.clear_retired_package_loaders();

    // Preserve source_path so normal save/reload still target the same graph file.
    if (!graph_.load_from_string(graph_json.c_str(), graph_json.size(), true)) {
        return restore_previous_state("failed to load graph snapshot JSON");
    }

    if (!scheduler_.build(graph_, registry_)) {
        return restore_previous_state("rebuild failed after snapshot load");
    }

    has_gpu_ops = scheduler_.has_gpu_operators();
    if (has_gpu_ops) needs_gpu_realloc_ = true;

    if (scheduler_.has_audio_operators()) {
        if (audio_engine_.build(graph_, registry_, scheduler_)) {
            if (audio_engine_.start()) {
                has_audio = true;
            }
        }
    }

    pending_topology_change_ = false;
    active_crossfades_.clear();
    preserve_undo_history_on_reload_ = true;
    reload_serial_++;
    refresh_graph_dirty_from_saved_snapshot();
    return {true, "applied graph snapshot"};
}

std::filesystem::path RuntimeAPI::graph_base_dir() const {
    const auto& sp = graph_.source_path();
    if (sp.empty()) return {};
    return std::filesystem::path(sp).parent_path();
}

bool RuntimeAPI::is_path_string_param(const NodeState& ns, const std::string& param) const {
    auto fi = ns.file_param_indices.find(param);
    if (fi == ns.file_param_indices.end()) return false;
    return fi->second < ns.file_param_is_path.size() && ns.file_param_is_path[fi->second] != 0;
}

std::string RuntimeAPI::to_runtime_string_value(const NodeState& ns, const std::string& param,
                                                const std::string& value) const {
    if (!is_path_string_param(ns, param)) return value;
    return resolve_file_path(value, graph_base_dir());
}

std::string RuntimeAPI::to_persisted_string_value(const NodeState& ns, const std::string& param,
                                                  const std::string& value) const {
    if (!is_path_string_param(ns, param)) return value;
    return make_relative_path(value, graph_base_dir());
}

void RuntimeAPI::set_file_param_internal(NodeState& ns, const std::string& param,
                                          const std::string& value) {
    auto fi = ns.file_param_indices.find(param);
    if (fi == ns.file_param_indices.end()) return;

    // Path params are resolved for runtime and relativized for persistence.
    // Text params keep literal string values.
    ns.file_param_storage[fi->second] = to_runtime_string_value(ns, param, value);
    ns.file_param_ptrs[fi->second] = ns.file_param_storage[fi->second].c_str();
    ns.generation++;

    NodeDef* ndef = graph_.find_node(ns.node_id);
    if (ndef) ndef->string_params[param] = to_persisted_string_value(ns, param, value);
}

void RuntimeAPI::mark_graph_dirty() {
    graph_dirty_ = true;
}

void RuntimeAPI::capture_saved_snapshot() {
    std::string current;
    if (graph_.save_to_string(current)) {
        last_saved_graph_json_ = std::move(current);
        graph_dirty_ = false;
        return;
    }
    last_saved_graph_json_.clear();
    graph_dirty_ = false;
}

void RuntimeAPI::refresh_graph_dirty_from_saved_snapshot() {
    if (last_saved_graph_json_.empty()) {
        graph_dirty_ = true;
        return;
    }
    std::string current;
    if (!graph_.save_to_string(current)) {
        graph_dirty_ = true;
        return;
    }
    graph_dirty_ = (current != last_saved_graph_json_);
}

} // namespace vivid
