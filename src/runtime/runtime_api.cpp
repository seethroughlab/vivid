#include "runtime/runtime_api.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "runtime/audio_engine.h"
#include "runtime/operator_registry.h"
#include <cstdio>
#include <sstream>

namespace vivid {

RuntimeAPI::RuntimeAPI(Graph& graph, Scheduler& scheduler, AudioEngine& audio_engine,
                       OperatorRegistry& registry)
    : graph_(graph), scheduler_(scheduler), audio_engine_(audio_engine), registry_(registry) {}

bool RuntimeAPI::split_addr(const std::string& addr, std::string& node, std::string& port) {
    auto slash = addr.find('/');
    if (slash == std::string::npos) return false;
    node = addr.substr(0, slash);
    port = addr.substr(slash + 1);
    return !node.empty() && !port.empty();
}

// --- Immediate param changes ---

CommandResult RuntimeAPI::set_param(const std::string& node_id, const std::string& param, float value) {
    const auto& nodes = scheduler_.nodes();
    for (uint32_t i = 0; i < static_cast<uint32_t>(nodes.size()); ++i) {
        if (nodes[i].node_id != node_id) continue;
        auto pi = nodes[i].param_indices.find(param);
        if (pi == nodes[i].param_indices.end()) {
            return {false, "unknown param '" + param + "' on " + node_id};
        }
        // Write directly to the scheduler's live param_values
        // (const_cast is safe here — scheduler owns the data, we're the control thread)
        const_cast<NodeState&>(nodes[i]).param_values[pi->second] = value;
        const_cast<NodeState&>(nodes[i]).generation++;

        // Also update graph's NodeDef so save reflects the change
        NodeDef* ndef = graph_.find_node(node_id);
        if (ndef) ndef->params[param] = value;

        std::ostringstream oss;
        oss << node_id << "/" << param << " = " << value;
        return {true, oss.str()};
    }
    return {false, "unknown node '" + node_id + "'"};
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

// --- Layout ---

CommandResult RuntimeAPI::set_node_layout(const std::string& node_id, float x, float y) {
    NodeDef* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};
    ndef->layout_x = x;
    ndef->layout_y = y;
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
    const auto& nodes = scheduler_.nodes();
    for (uint32_t i = 0; i < static_cast<uint32_t>(nodes.size()); ++i) {
        if (nodes[i].node_id != node_id) continue;
        auto& ns = const_cast<NodeState&>(nodes[i]);
        ns.gpu_tex_width  = width;
        ns.gpu_tex_height = height;
        ns.generation++;
        break;
    }

    needs_gpu_realloc_ = true;

    std::ostringstream oss;
    oss << node_id << " resolution = " << width << "x" << height;
    return {true, oss.str()};
}

// --- Buffered topology changes ---

CommandResult RuntimeAPI::add_node(const std::string& type, const std::string& id) {
    if (!registry_.find(type)) {
        return {false, "unknown type '" + type + "'"};
    }
    if (!graph_.add_node(id, type)) {
        return {false, "node '" + id + "' already exists"};
    }
    pending_topology_change_ = true;
    return {true, "added " + type + " as " + id};
}

CommandResult RuntimeAPI::remove_node(const std::string& id) {
    if (!graph_.remove_node(id)) {
        return {false, "unknown node '" + id + "'"};
    }
    pending_topology_change_ = true;
    return {true, "removed " + id};
}

CommandResult RuntimeAPI::connect(const std::string& from_addr, const std::string& to_addr) {
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
    pending_topology_change_ = true;
    return {true, "connected " + from_addr + " -> " + to_addr};
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
    return {true, "disconnected " + from_addr + " -> " + to_addr};
}

// --- apply_pending: full rebuild with param preservation ---

bool RuntimeAPI::apply_pending(bool& has_gpu_ops, bool& has_audio) {
    if (!pending_topology_change_) return false;
    pending_topology_change_ = false;

    // 1. Save current param values by node_id + param_name
    std::unordered_map<std::string, std::unordered_map<std::string, float>> saved_params;
    for (const auto& ns : scheduler_.nodes()) {
        auto& sp = saved_params[ns.node_id];
        for (const auto& [name, idx] : ns.param_indices) {
            sp[name] = ns.param_values[idx];
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
    for (auto& ns : const_cast<std::vector<NodeState>&>(scheduler_.nodes())) {
        auto sit = saved_params.find(ns.node_id);
        if (sit == saved_params.end()) continue;
        for (const auto& [pname, pval] : sit->second) {
            auto pi = ns.param_indices.find(pname);
            if (pi != ns.param_indices.end()) {
                ns.param_values[pi->second] = pval;
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
        }
        oss << "\n  outputs:";
        for (const auto& [name, idx] : ns.output_port_indices) {
            oss << " " << name << "=" << ns.output_values[idx];
        }
        if (!ns.input_port_indices.empty()) {
            oss << "\n  inputs:";
            for (const auto& [name, idx] : ns.input_port_indices) {
                oss << " " << name << "=" << ns.input_values[idx];
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

// --- Persistence ---

CommandResult RuntimeAPI::save() {
    const auto& path = graph_.source_path();
    if (path.empty()) return {false, "no source path (use save <path>)"};
    return save_as(path);
}

CommandResult RuntimeAPI::save_as(const std::string& path) {
    if (graph_.save(path.c_str())) {
        return {true, "saved to " + path};
    }
    return {false, "failed to save to " + path};
}

CommandResult RuntimeAPI::reload(bool& has_gpu_ops, bool& has_audio) {
    const auto& path = graph_.source_path();
    if (path.empty()) return {false, "no source path to reload from"};

    // Save params before reload
    std::unordered_map<std::string, std::unordered_map<std::string, float>> saved_params;
    for (const auto& ns : scheduler_.nodes()) {
        auto& sp = saved_params[ns.node_id];
        for (const auto& [name, idx] : ns.param_indices) {
            sp[name] = ns.param_values[idx];
        }
    }

    bool had_audio = has_audio;
    if (had_audio) {
        audio_engine_.shutdown();
        has_audio = false;
    }
    scheduler_.shutdown();

    if (!graph_.load(path.c_str())) {
        has_gpu_ops = false;
        has_audio = false;
        return {false, "failed to reload " + path};
    }

    if (!scheduler_.build(graph_, registry_)) {
        has_gpu_ops = false;
        has_audio = false;
        return {false, "rebuild failed after reload"};
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

    return {true, "reloaded from " + path};
}

} // namespace vivid
