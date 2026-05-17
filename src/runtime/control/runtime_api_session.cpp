#include "runtime/control/runtime_api.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/core/runtime_core.h"
#include <cstdio>
#include <cstdint>
#include <algorithm>

namespace vivid {

namespace {

constexpr const char* kNoCompiledGraph = "no compiled graph";

// Returns the index of node_id in cg.nodes, or UINT32_MAX if not found.
uint32_t node_index_of(const CompiledGraph& cg, const std::string& node_id) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(cg.nodes.size()); ++i) {
        if (cg.nodes[i].node_id == node_id) return i;
    }
    return UINT32_MAX;
}

// True if float param `param_idx` of node `node_idx` has an active wire driving it.
bool float_param_is_wired(const CompiledGraph& cg, uint32_t node_idx, uint32_t param_idx) {
    for (const auto& e : cg.edges) {
        if (e.to_node == node_idx && e.targets_param && e.to_port == param_idx)
            return true;
    }
    return false;
}

// True if file/string param `file_idx` of node `node_idx` has an active wire.
bool string_param_is_wired(const CompiledGraph& cg, uint32_t node_idx, uint32_t file_idx) {
    for (const auto& e : cg.edges) {
        if (e.to_node == node_idx && e.targets_file_param && e.to_file_param_idx == file_idx)
            return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::pair<
    std::unordered_map<std::string, std::unordered_map<std::string, float>>,
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
>
RuntimeAPI::capture_clip_params(const std::string& track_id) const {
    using P = std::unordered_map<std::string, std::unordered_map<std::string, float>>;
    using S = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;
    P params;
    S string_params;

    const auto* track = graph_.find_track(track_id);
    if (!track) return {};
    const auto* cg = core_.compiled_graph();
    if (!cg) return {};

    for (const auto& node_id : track->owned_node_ids) {
        const uint32_t nidx = node_index_of(*cg, node_id);
        if (nidx == UINT32_MAX) {
            std::fprintf(stderr,
                "[session] capture_clip_params: '%s' not in compiled graph — skipped\n",
                node_id.c_str());
            continue;
        }
        const auto& cn = cg->nodes[nidx];

        for (const auto& [name, idx] : cn.param_indices) {
            if (cn.param_lock_flags[idx] & PARAM_LOCK_WIRES) continue;
            if (float_param_is_wired(*cg, nidx, idx)) continue;
            params[node_id][name] = cn.param_values[idx];
        }

        for (const auto& [name, idx] : cn.file_param_indices) {
            if (string_param_is_wired(*cg, nidx, idx)) continue;
            string_params[node_id][name] =
                to_persisted_string_value(cn, name, cn.file_param_storage[idx]);
        }
    }

    return {std::move(params), std::move(string_params)};
}

void RuntimeAPI::apply_clip_params(const std::string& track_id, const SessionClipDef& clip) {
    const auto* track = graph_.find_track(track_id);
    if (!track) return;
    auto* cg = core_.compiled_graph();
    if (!cg) return;

    for (const auto& node_id : track->owned_node_ids) {
        auto* cn = cg->find_node(node_id);
        if (!cn) {
            std::fprintf(stderr,
                "[session] apply_clip_params: '%s' not in compiled graph — skipped\n",
                node_id.c_str());
            continue;
        }
        auto* ndef = graph_.find_node(node_id);

        auto p_it = clip.params.find(node_id);
        if (p_it != clip.params.end()) {
            for (const auto& [pname, pval] : p_it->second) {
                auto pi = cn->param_indices.find(pname);
                if (pi == cn->param_indices.end()) continue;
                if (cn->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS) continue;
                cn->param_values[pi->second] = pval;
                if (ndef) ndef->params[pname] = pval;
            }
        }

        auto sp_it = clip.string_params.find(node_id);
        if (sp_it != clip.string_params.end()) {
            for (const auto& [pname, pval] : sp_it->second) {
                auto fi = cn->file_param_indices.find(pname);
                if (fi == cn->file_param_indices.end()) continue;
                const std::string runtime_val = to_runtime_string_value(*cn, pname, pval);
                cn->file_param_storage[fi->second] = runtime_val;
                if (fi->second < cn->file_param_ptrs.size())
                    cn->file_param_ptrs[fi->second] = cn->file_param_storage[fi->second].c_str();
                if (ndef) ndef->string_params[pname] = pval;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Track CRUD
// ---------------------------------------------------------------------------

CommandResult RuntimeAPI::create_track(const std::string& name) {
    std::string id = graph_.create_track(name);
    if (id.empty()) return {false, "create_track failed"};
    mark_graph_dirty();
    return {true, id};
}

CommandResult RuntimeAPI::rename_track(const std::string& track_id, const std::string& name) {
    if (!graph_.rename_track(track_id, name))
        return {false, "unknown track '" + track_id + "'"};
    mark_graph_dirty();
    return {true, track_id};
}

CommandResult RuntimeAPI::remove_track(const std::string& track_id) {
    if (!graph_.remove_track(track_id))
        return {false, "unknown track '" + track_id + "'"};
    active_clips_.erase(track_id);
    mark_graph_dirty();
    return {true, track_id};
}

CommandResult RuntimeAPI::move_track(const std::string& track_id, int to_index) {
    if (!graph_.move_track(track_id, to_index))
        return {false, "move_track failed for '" + track_id + "'"};
    mark_graph_dirty();
    return {true, track_id};
}

CommandResult RuntimeAPI::assign_nodes_to_track(const std::string& track_id,
                                                  const std::vector<std::string>& node_ids) {
    if (!graph_.assign_nodes_to_track(track_id, node_ids))
        return {false, "assign_nodes_to_track failed for '" + track_id + "'"};
    mark_graph_dirty();
    return {true, track_id};
}

CommandResult RuntimeAPI::unassign_nodes_from_track(const std::string& track_id,
                                                      const std::vector<std::string>& node_ids) {
    if (!graph_.unassign_nodes_from_track(track_id, node_ids))
        return {false, "unassign_nodes_from_track failed for '" + track_id + "'"};
    mark_graph_dirty();
    return {true, track_id};
}

// ---------------------------------------------------------------------------
// Clip CRUD + launch
// ---------------------------------------------------------------------------

CommandResult RuntimeAPI::save_clip(const std::string& track_id, const std::string& name) {
    if (!core_.compiled_graph()) return {false, kNoCompiledGraph};
    if (!graph_.find_track(track_id))
        return {false, "unknown track '" + track_id + "'"};
    auto [params, string_params] = capture_clip_params(track_id);
    std::string cid = graph_.save_clip(track_id, name, std::move(params), std::move(string_params));
    if (cid.empty()) return {false, "save_clip failed"};
    mark_graph_dirty();
    return {true, cid};
}

CommandResult RuntimeAPI::update_clip(const std::string& track_id, const std::string& clip_id) {
    if (!core_.compiled_graph()) return {false, kNoCompiledGraph};
    if (!graph_.find_track(track_id))
        return {false, "unknown track '" + track_id + "'"};
    if (!graph_.find_clip(track_id, clip_id))
        return {false, "unknown clip '" + clip_id + "'"};
    auto [params, string_params] = capture_clip_params(track_id);
    if (!graph_.update_clip(track_id, clip_id, std::move(params), std::move(string_params)))
        return {false, "update_clip failed"};
    mark_graph_dirty();
    return {true, clip_id};
}

CommandResult RuntimeAPI::rename_clip(const std::string& track_id, const std::string& clip_id,
                                        const std::string& new_name) {
    if (!graph_.rename_clip(track_id, clip_id, new_name))
        return {false, "unknown clip '" + clip_id + "'"};
    mark_graph_dirty();
    return {true, clip_id};
}

CommandResult RuntimeAPI::remove_clip(const std::string& track_id, const std::string& clip_id) {
    if (!graph_.remove_clip(track_id, clip_id))
        return {false, "unknown clip '" + clip_id + "'"};
    auto it = active_clips_.find(track_id);
    if (it != active_clips_.end() && it->second == clip_id)
        active_clips_.erase(it);
    mark_graph_dirty();
    return {true, clip_id};
}

CommandResult RuntimeAPI::move_clip(const std::string& track_id, const std::string& clip_id,
                                      int to_index) {
    if (!graph_.move_clip(track_id, clip_id, to_index))
        return {false, "move_clip failed for '" + clip_id + "'"};
    mark_graph_dirty();
    return {true, clip_id};
}

CommandResult RuntimeAPI::launch_clip(const std::string& track_id, const std::string& clip_id) {
    if (!core_.compiled_graph()) return {false, kNoCompiledGraph};
    if (!graph_.find_track(track_id))
        return {false, "unknown track '" + track_id + "'"};
    const auto* clip = graph_.find_clip(track_id, clip_id);
    if (!clip)
        return {false, "unknown clip '" + clip_id + "'"};
    apply_clip_params(track_id, *clip);
    active_clips_[track_id] = clip_id;
    return {true, clip_id};
}

const std::string& RuntimeAPI::active_clip(const std::string& track_id) const {
    static const std::string empty;
    auto it = active_clips_.find(track_id);
    return (it != active_clips_.end()) ? it->second : empty;
}

} // namespace vivid
