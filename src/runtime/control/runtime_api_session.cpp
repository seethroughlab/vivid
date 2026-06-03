#include "runtime/control/runtime_api.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/core/runtime_core.h"
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <optional>

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

RuntimeAPI::ClipCapture
RuntimeAPI::capture_clip_params(const std::string& track_id) {
    core_.update_audio_sources(0.0);
    ClipCapture out;

    const auto* track = graph_.find_track(track_id);
    if (!track) return out;
    const auto* cg = core_.compiled_graph();
    if (!cg) return out;

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
            out.params[node_id][name] = cn.param_values[idx];
        }

        for (const auto& [name, idx] : cn.file_param_indices) {
            if (string_param_is_wired(*cg, nidx, idx)) continue;
            out.string_params[node_id][name] =
                to_persisted_string_value(cn, name, cn.file_param_storage[idx]);
        }

        // Per-node bypass state (so a clip can flip an effect in/out per scene).
        if (const auto* ndef = graph_.find_node(node_id))
            out.bypass[node_id] = ndef->bypassed;
    }

    return out;
}

void RuntimeAPI::apply_clip_params(const std::string& track_id, const SessionClipDef& clip,
                                   float fade_bars) {
    const auto* track = graph_.find_track(track_id);
    if (!track) return;
    auto* cg = core_.compiled_graph();
    if (!cg) return;

    // Resolve the effective fade: explicit fade_bars wins, else the clip's own
    // transition override (the modeled-but-formerly-unused SessionTransitionDef).
    float eff_fade = fade_bars;
    if (eff_fade <= 0.0f && clip.transition_override && clip.transition_override->fade)
        eff_fade = clip.transition_override->duration_bars;
    double ramp_start_beat = 0.0, ramp_end_beat = 0.0;
    const bool do_fade = eff_fade > 0.0f;
    if (do_fade) {
        const auto m = current_metronome_sample();
        const int bpb = std::max(1, m.beats_per_bar);
        ramp_start_beat = m.beats_elapsed;
        ramp_end_beat = ramp_start_beat + static_cast<double>(eff_fade) * bpb;
    }

    for (const auto& node_id : track->owned_node_ids) {
        auto* cn = cg->find_node(node_id);
        if (!cn) {
            std::fprintf(stderr,
                "[session] apply_clip_params: '%s' not in compiled graph — skipped\n",
                node_id.c_str());
            continue;
        }
        auto* ndef = graph_.find_node(node_id);

        // Bypass switches at the start of the launch (not ramped).
        auto b_it = clip.bypass.find(node_id);
        if (b_it != clip.bypass.end()) {
            const bool bypassed = b_it->second;
            if (ndef) ndef->bypassed = bypassed;
            cn->bypassed = bypassed;
            cn->dirty = true;
        }

        auto p_it = clip.params.find(node_id);
        if (p_it != clip.params.end()) {
            for (const auto& [pname, pval] : p_it->second) {
                auto pi = cn->param_indices.find(pname);
                if (pi == cn->param_indices.end()) continue;
                if (cn->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS) continue;
                if (do_fade) {
                    // Persist the destination immediately; ramp the live value.
                    const float start = cn->param_values[pi->second];
                    if (std::fabs(start - pval) > 1e-9f) {
                        // Replace any in-flight ramp for this exact param.
                        active_ramps_.erase(std::remove_if(active_ramps_.begin(), active_ramps_.end(),
                            [&](const ParamRamp& r){ return r.node_id == node_id && r.param_name == pname; }),
                            active_ramps_.end());
                        active_ramps_.push_back({node_id, pname, start, pval,
                                                 ramp_start_beat, ramp_end_beat});
                    }
                    if (ndef) ndef->params[pname] = pval;
                    continue;
                }
                cn->param_values[pi->second] = pval;
                cn->dirty = true;
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
                cn->dirty = true;
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
    graph_.clear_active_clip(track_id);
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

CommandResult RuntimeAPI::save_clip(const std::string& track_id, const std::string& name,
                                    bool activate) {
    if (!core_.compiled_graph()) return {false, kNoCompiledGraph};
    if (!graph_.find_track(track_id))
        return {false, "unknown track '" + track_id + "'"};
    auto cap = capture_clip_params(track_id);
    std::string cid = graph_.save_clip(track_id, name, std::move(cap.params),
                                       std::move(cap.string_params), std::move(cap.bypass));
    if (cid.empty()) return {false, "save_clip failed"};
    if (activate) {
        active_clips_[track_id] = cid;
        graph_.set_active_clip(track_id, cid);
    }
    mark_graph_dirty();
    return {true, cid};
}

CommandResult RuntimeAPI::update_clip(const std::string& track_id, const std::string& clip_id) {
    if (!core_.compiled_graph()) return {false, kNoCompiledGraph};
    if (!graph_.find_track(track_id))
        return {false, "unknown track '" + track_id + "'"};
    if (!graph_.find_clip(track_id, clip_id))
        return {false, "unknown clip '" + clip_id + "'"};
    auto cap = capture_clip_params(track_id);
    if (!graph_.update_clip(track_id, clip_id, std::move(cap.params),
                            std::move(cap.string_params), std::move(cap.bypass)))
        return {false, "update_clip failed"};
    mark_graph_dirty();
    return {true, clip_id};
}

CommandResult RuntimeAPI::update_clip_param(const std::string& track_id, const std::string& clip_id,
                                            const std::string& node_id, const std::string& param,
                                            float value) {
    auto* c = graph_.find_clip(track_id, clip_id);
    if (!c) return {false, "unknown clip '" + clip_id + "' on track '" + track_id + "'"};
    c->params[node_id][param] = value;
    mark_graph_dirty();
    return {true, clip_id};
}

CommandResult RuntimeAPI::update_clip_string_param(const std::string& track_id, const std::string& clip_id,
                                                   const std::string& node_id, const std::string& param,
                                                   const std::string& value) {
    auto* c = graph_.find_clip(track_id, clip_id);
    if (!c) return {false, "unknown clip '" + clip_id + "' on track '" + track_id + "'"};
    c->string_params[node_id][param] = value;
    mark_graph_dirty();
    return {true, clip_id};
}

CommandResult RuntimeAPI::update_clip_bypass(const std::string& track_id, const std::string& clip_id,
                                             const std::string& node_id, bool bypassed) {
    auto* c = graph_.find_clip(track_id, clip_id);
    if (!c) return {false, "unknown clip '" + clip_id + "' on track '" + track_id + "'"};
    c->bypass[node_id] = bypassed;
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
    if (it != active_clips_.end() && it->second == clip_id) {
        active_clips_.erase(it);
        graph_.clear_active_clip(track_id);
    }
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
    graph_.set_active_clip(track_id, clip_id);
    mark_graph_dirty();
    return {true, clip_id};
}

const std::string& RuntimeAPI::active_clip(const std::string& track_id) const {
    static const std::string empty;
    auto it = active_clips_.find(track_id);
    return (it != active_clips_.end()) ? it->second : empty;
}

// ---------------------------------------------------------------------------
// Scene CRUD
// ---------------------------------------------------------------------------

CommandResult RuntimeAPI::save_scene(const std::string& name) {
    std::string scene_id = graph_.save_scene(name);
    if (scene_id.empty()) return {false, "save_scene failed"};
    for (const auto& [track_id, clip_id] : active_clips_)
        graph_.set_scene_assignment(scene_id, track_id, clip_id);
    mark_graph_dirty();
    return {true, scene_id};
}

CommandResult RuntimeAPI::save_scene_from_clips(
        const std::string& name,
        const std::vector<std::pair<std::string, std::string>>& assignments) {
    // Validate every (track, clip) up front so a bad arg doesn't leave a half-built scene.
    for (const auto& [track_id, clip_id] : assignments) {
        if (!graph_.find_track(track_id))
            return {false, "unknown track '" + track_id + "'"};
        if (!graph_.find_clip(track_id, clip_id))
            return {false, "unknown clip '" + clip_id + "' on track '" + track_id + "'"};
    }
    std::string scene_id = graph_.save_scene(name);
    if (scene_id.empty()) return {false, "save_scene failed"};
    for (const auto& [track_id, clip_id] : assignments)
        graph_.set_scene_assignment(scene_id, track_id, clip_id);
    mark_graph_dirty();
    return {true, scene_id};
}

CommandResult RuntimeAPI::update_scene(const std::string& scene_id) {
    if (!graph_.find_scene(scene_id))
        return {false, "unknown scene '" + scene_id + "'"};
    graph_.update_scene_assignments(scene_id, active_clips_);
    mark_graph_dirty();
    return {true, scene_id};
}

CommandResult RuntimeAPI::rename_scene(const std::string& scene_id, const std::string& new_name) {
    if (!graph_.rename_scene(scene_id, new_name))
        return {false, "unknown scene '" + scene_id + "'"};
    mark_graph_dirty();
    return {true, scene_id};
}

CommandResult RuntimeAPI::remove_scene(const std::string& scene_id) {
    if (!graph_.remove_scene(scene_id))
        return {false, "unknown scene '" + scene_id + "'"};
    if (pending_scene_launch_ && pending_scene_launch_->scene_id == scene_id)
        pending_scene_launch_.reset();
    if (!active_cue_path_id_.empty()) {
        const auto* path = graph_.find_cue_path(active_cue_path_id_);
        if (!path || !graph_.find_cue_step(active_cue_path_id_, active_cue_step_id_)) {
            active_cue_path_id_.clear();
            active_cue_step_id_.clear();
            cue_follow_target_beat_index_ = -1;
        }
    }
    mark_graph_dirty();
    return {true, scene_id};
}

CommandResult RuntimeAPI::move_scene(const std::string& scene_id, int to_index) {
    if (!graph_.move_scene(scene_id, to_index))
        return {false, "move_scene failed for '" + scene_id + "'"};
    mark_graph_dirty();
    return {true, scene_id};
}

// ---------------------------------------------------------------------------
// Scene assignment
// ---------------------------------------------------------------------------

CommandResult RuntimeAPI::set_scene_assignment(const std::string& scene_id,
                                                 const std::string& track_id,
                                                 const std::string& clip_id) {
    if (!graph_.set_scene_assignment(scene_id, track_id, clip_id))
        return {false, "set_scene_assignment failed — unknown scene, track, or clip"};
    mark_graph_dirty();
    return {true, scene_id};
}

CommandResult RuntimeAPI::set_scene_leave_unchanged(const std::string& scene_id,
                                                      const std::string& track_id) {
    if (!graph_.set_scene_leave_unchanged(scene_id, track_id))
        return {false, "set_scene_leave_unchanged failed — unknown scene or track"};
    mark_graph_dirty();
    return {true, scene_id};
}

CommandResult RuntimeAPI::clear_scene_assignment(const std::string& scene_id,
                                                   const std::string& track_id) {
    if (!graph_.clear_scene_assignment(scene_id, track_id))
        return {false, "unknown scene '" + scene_id + "'"};
    mark_graph_dirty();
    return {true, scene_id};
}

// ---------------------------------------------------------------------------
// Cue path CRUD + launch
// ---------------------------------------------------------------------------

CommandResult RuntimeAPI::create_cue_path(const std::string& name) {
    std::string id = graph_.create_cue_path(name);
    if (id.empty()) return {false, "create_cue_path failed"};
    mark_graph_dirty();
    return {true, id};
}

CommandResult RuntimeAPI::rename_cue_path(const std::string& path_id, const std::string& name) {
    if (!graph_.rename_cue_path(path_id, name))
        return {false, "unknown cue path '" + path_id + "'"};
    mark_graph_dirty();
    return {true, path_id};
}

CommandResult RuntimeAPI::remove_cue_path(const std::string& path_id) {
    if (!graph_.remove_cue_path(path_id))
        return {false, "unknown cue path '" + path_id + "'"};
    if (active_cue_path_id_ == path_id) {
        active_cue_path_id_.clear();
        active_cue_step_id_.clear();
        cue_follow_target_beat_index_ = -1;
    }
    if (queued_cue_path_id_ == path_id) {
        queued_cue_path_id_.clear();
        queued_cue_step_id_.clear();
        if (pending_scene_launch_ && pending_scene_launch_->cue_path_id == path_id)
            pending_scene_launch_.reset();
    }
    mark_graph_dirty();
    return {true, path_id};
}

CommandResult RuntimeAPI::move_cue_path(const std::string& path_id, int to_index) {
    if (!graph_.move_cue_path(path_id, to_index))
        return {false, "move_cue_path failed for '" + path_id + "'"};
    mark_graph_dirty();
    return {true, path_id};
}

CommandResult RuntimeAPI::add_cue_step(const std::string& path_id, const std::string& scene_id,
                                       int index) {
    if (!graph_.find_cue_path(path_id))
        return {false, "unknown cue path '" + path_id + "'"};
    if (!graph_.find_scene(scene_id))
        return {false, "unknown scene '" + scene_id + "'"};
    std::string step_id = graph_.add_cue_step(path_id, scene_id, index);
    if (step_id.empty()) return {false, "add_cue_step failed"};
    mark_graph_dirty();
    return {true, step_id};
}

CommandResult RuntimeAPI::remove_cue_step(const std::string& path_id,
                                          const std::string& step_id) {
    if (!graph_.remove_cue_step(path_id, step_id))
        return {false, "unknown cue step '" + step_id + "'"};
    if (active_cue_path_id_ == path_id && active_cue_step_id_ == step_id) {
        active_cue_path_id_.clear();
        active_cue_step_id_.clear();
        cue_follow_target_beat_index_ = -1;
    }
    if (queued_cue_path_id_ == path_id && queued_cue_step_id_ == step_id) {
        queued_cue_path_id_.clear();
        queued_cue_step_id_.clear();
        if (pending_scene_launch_ && pending_scene_launch_->cue_path_id == path_id &&
            pending_scene_launch_->cue_step_id == step_id)
            pending_scene_launch_.reset();
    }
    mark_graph_dirty();
    return {true, step_id};
}

CommandResult RuntimeAPI::move_cue_step(const std::string& path_id, const std::string& step_id,
                                        int to_index) {
    if (!graph_.move_cue_step(path_id, step_id, to_index))
        return {false, "move_cue_step failed for '" + step_id + "'"};
    mark_graph_dirty();
    return {true, step_id};
}

CommandResult RuntimeAPI::set_cue_step_advance(const std::string& path_id,
                                               const std::string& step_id,
                                               const std::string& advance_mode,
                                               int bars) {
    if (!graph_.set_cue_step_advance(path_id, step_id, advance_mode, bars))
        return {false, "set_cue_step_advance failed"};
    mark_graph_dirty();
    return {true, step_id};
}

CommandResult RuntimeAPI::launch_cue_step(const std::string& path_id,
                                          const std::string& step_id,
                                          const std::string& quantize) {
    if (!core_.compiled_graph()) return {false, kNoCompiledGraph};
    const auto* path = graph_.find_cue_path(path_id);
    if (!path) return {false, "unknown cue path '" + path_id + "'"};
    const auto* step = graph_.find_cue_step(path_id, step_id);
    if (!step) return {false, "unknown cue step '" + step_id + "'"};
    if (!graph_.find_scene(step->scene_id))
        return {false, "cue step references missing scene '" + step->scene_id + "'"};

    active_cue_path_id_.clear();
    active_cue_step_id_.clear();
    cue_follow_target_beat_index_ = -1;

    if (quantize == "instant") {
        if (!fire_scene(step->scene_id))
            return {false, "launch_cue_step failed"};
        mark_cue_step_fired(path_id, step_id);
        return {true, step_id};
    }
    if (quantize != "beat" && quantize != "bar" &&
        quantize != "4bar" && quantize != "four_bar")
        return {false, "unknown quantize mode '" + quantize + "'"};

    const int64_t target = compute_quantize_target_beat(quantize);
    pending_scene_launch_ = {step->scene_id, target, path_id, step_id};
    queued_cue_path_id_ = path_id;
    queued_cue_step_id_ = step_id;
    return {true, step_id};
}

CommandResult RuntimeAPI::advance_cue_path(const std::string& path_id,
                                           const std::string& quantize) {
    const auto* path = graph_.find_cue_path(path_id);
    if (!path) return {false, "unknown cue path '" + path_id + "'"};
    if (path->steps.empty()) return {false, "cue path has no steps"};

    int next = 0;
    if (active_cue_path_id_ == path_id && !active_cue_step_id_.empty()) {
        for (int i = 0; i < static_cast<int>(path->steps.size()); ++i) {
            if (path->steps[i].id == active_cue_step_id_) {
                next = std::min(i + 1, static_cast<int>(path->steps.size()) - 1);
                break;
            }
        }
    }
    return launch_cue_step(path_id, path->steps[next].id, quantize);
}

CommandResult RuntimeAPI::stop_cue_path(const std::string& path_id) {
    if (!path_id.empty() && active_cue_path_id_ != path_id && queued_cue_path_id_ != path_id)
        return {false, "cue path '" + path_id + "' is not active or queued"};
    if (pending_scene_launch_ && (path_id.empty() ||
        pending_scene_launch_->cue_path_id == path_id))
        pending_scene_launch_.reset();
    if (path_id.empty() || active_cue_path_id_ == path_id) {
        active_cue_path_id_.clear();
        active_cue_step_id_.clear();
        cue_follow_target_beat_index_ = -1;
    }
    if (path_id.empty() || queued_cue_path_id_ == path_id) {
        queued_cue_path_id_.clear();
        queued_cue_step_id_.clear();
    }
    return {true, path_id};
}

// ---------------------------------------------------------------------------
// Quantize helpers + fire_scene
// ---------------------------------------------------------------------------

int64_t RuntimeAPI::compute_quantize_target_beat(const std::string& quantize) const {
    const auto metronome = current_metronome_sample();
    const int bpb = std::max(1, metronome.beats_per_bar);
    const int64_t current_beat = static_cast<int64_t>(std::floor(metronome.beats_elapsed));
    if (quantize == "beat") return current_beat + 1;
    if (quantize == "bar")  return ((current_beat / bpb) + 1) * bpb;
    if (quantize == "4bar" || quantize == "four_bar") {
        const int four_bar = bpb * 4;
        return ((current_beat / four_bar) + 1) * four_bar;
    }
    return current_beat + 1; // fallback: next beat
}

bool RuntimeAPI::fire_scene(const std::string& scene_id, float fade_bars) {
    const auto* scene = graph_.find_scene(scene_id);
    if (!scene) {
        std::fprintf(stderr, "[session] fire_scene: scene '%s' not found — skipped\n",
                     scene_id.c_str());
        return false;
    }
    // Copy assignments before applying so nothing held across the apply loop can
    // be invalidated by a concurrent session edit.
    std::vector<std::pair<std::string, std::string>> assignments(
        scene->assignments.begin(), scene->assignments.end());
    for (const auto& [track_id, clip_id] : assignments) {
        const auto* clip = graph_.find_clip(track_id, clip_id);
        if (!clip) {
            std::fprintf(stderr,
                "[session] fire_scene: clip '%s' for track '%s' not found — skipped\n",
                clip_id.c_str(), track_id.c_str());
            continue;
        }
        apply_clip_params(track_id, *clip, fade_bars);
        active_clips_[track_id] = clip_id;
        graph_.set_active_clip(track_id, clip_id);
    }
    mark_graph_dirty();
    // scene-level launch wins over any individually pending clips for assigned tracks
    pending_clip_launches_.erase(
        std::remove_if(pending_clip_launches_.begin(), pending_clip_launches_.end(),
                       [&](const PendingClipLaunch& p) {
                           for (const auto& a : assignments)
                               if (a.first == p.track_id) return true;
                           return false;
                       }),
        pending_clip_launches_.end());
    return true;
}

void RuntimeAPI::mark_cue_step_fired(const std::string& path_id, const std::string& step_id) {
    const auto* step = graph_.find_cue_step(path_id, step_id);
    if (!step) return;
    active_cue_path_id_ = path_id;
    active_cue_step_id_ = step_id;
    queued_cue_path_id_.clear();
    queued_cue_step_id_.clear();
    cue_follow_target_beat_index_ = -1;
    if (step->advance_mode == "after_bars") {
        const auto metronome = current_metronome_sample();
        const int bpb = std::max(1, metronome.beats_per_bar);
        const int bars = std::max(1, step->bars);
        const int64_t current_beat = static_cast<int64_t>(std::floor(metronome.beats_elapsed));
        cue_follow_target_beat_index_ = current_beat + static_cast<int64_t>(bars) * bpb;
    }
}

// ---------------------------------------------------------------------------
// Quantized launch: queue_clip / queue_scene
// ---------------------------------------------------------------------------

CommandResult RuntimeAPI::queue_clip(const std::string& track_id, const std::string& clip_id,
                                      const std::string& quantize, float fade_bars) {
    if (!core_.compiled_graph()) return {false, kNoCompiledGraph};
    if (!graph_.find_track(track_id))
        return {false, "unknown track '" + track_id + "'"};
    const auto* clip = graph_.find_clip(track_id, clip_id);
    if (!clip)
        return {false, "unknown clip '" + clip_id + "'"};

    // "default" (or empty) falls back to the graph's session-wide launch quantize.
    const std::string q = (quantize == "default" || quantize.empty())
                          ? graph_.launch_quantize() : quantize;

    if (q == "instant") {
        apply_clip_params(track_id, *clip, fade_bars);
        active_clips_[track_id] = clip_id;
        graph_.set_active_clip(track_id, clip_id);
        mark_graph_dirty();
        return {true, clip_id};
    }
    if (q != "beat" && q != "bar" &&
        q != "4bar" && q != "four_bar")
        return {false, "unknown quantize mode '" + q + "'"};

    const int64_t target = compute_quantize_target_beat(q);
    // replace any existing pending for this track
    pending_clip_launches_.erase(
        std::remove_if(pending_clip_launches_.begin(), pending_clip_launches_.end(),
                       [&](const PendingClipLaunch& p) { return p.track_id == track_id; }),
        pending_clip_launches_.end());
    pending_clip_launches_.push_back({track_id, clip_id, target, fade_bars});
    return {true, clip_id};
}

CommandResult RuntimeAPI::queue_scene(const std::string& scene_id, const std::string& quantize,
                                      float fade_bars) {
    if (!core_.compiled_graph()) return {false, kNoCompiledGraph};
    if (!graph_.find_scene(scene_id))
        return {false, "unknown scene '" + scene_id + "'"};

    // "default" (or empty) falls back to the graph's session-wide launch quantize.
    const std::string q = (quantize == "default" || quantize.empty())
                          ? graph_.launch_quantize() : quantize;

    if (q == "instant") {
        queued_cue_path_id_.clear();
        queued_cue_step_id_.clear();
        fire_scene(scene_id, fade_bars);
        return {true, scene_id};
    }
    if (q != "beat" && q != "bar" &&
        q != "4bar" && q != "four_bar")
        return {false, "unknown quantize mode '" + q + "'"};

    const int64_t target = compute_quantize_target_beat(q);
    pending_scene_launch_ = {scene_id, target, {}, {}, fade_bars};
    queued_cue_path_id_.clear();
    queued_cue_step_id_.clear();
    return {true, scene_id};
}

// ---------------------------------------------------------------------------
// Tick: fire pending launches at beat boundary
// ---------------------------------------------------------------------------

void RuntimeAPI::tick_quantized_clip_scene_launches() {
    // Fades advance every frame, independent of any pending quantized launch.
    tick_param_ramps();

    if (!pending_scene_launch_ && pending_clip_launches_.empty() &&
        cue_follow_target_beat_index_ < 0)
        return;
    if (!core_.compiled_graph()) return;

    const auto metronome = current_metronome_sample();
    const int64_t current_beat = static_cast<int64_t>(std::floor(metronome.beats_elapsed));

    if (pending_scene_launch_ && current_beat >= pending_scene_launch_->target_beat_index) {
        const auto cue_path_id = pending_scene_launch_->cue_path_id;
        const auto cue_step_id = pending_scene_launch_->cue_step_id;
        if (fire_scene(pending_scene_launch_->scene_id, pending_scene_launch_->fade_bars) &&
            !cue_path_id.empty() && !cue_step_id.empty()) {
            mark_cue_step_fired(cue_path_id, cue_step_id);
        }
        pending_scene_launch_.reset();
    }

    for (auto& p : pending_clip_launches_) {
        if (current_beat >= p.target_beat_index) {
            const auto* clip = graph_.find_clip(p.track_id, p.clip_id);
            if (clip) {
                apply_clip_params(p.track_id, *clip, p.fade_bars);
                active_clips_[p.track_id] = p.clip_id;
                graph_.set_active_clip(p.track_id, p.clip_id);
            } else {
                std::fprintf(stderr,
                    "[session] tick: clip '%s' for track '%s' not found — skipped\n",
                    p.clip_id.c_str(), p.track_id.c_str());
            }
            p.target_beat_index = INT64_MAX; // mark fired
        }
    }
    bool any_fired = false;
    pending_clip_launches_.erase(
        std::remove_if(pending_clip_launches_.begin(), pending_clip_launches_.end(),
                       [&](const PendingClipLaunch& p) {
                           if (p.target_beat_index == INT64_MAX) { any_fired = true; return true; }
                           return false;
                       }),
        pending_clip_launches_.end());
    if (any_fired) mark_graph_dirty();

    if (cue_follow_target_beat_index_ >= 0 &&
        current_beat >= cue_follow_target_beat_index_ &&
        !active_cue_path_id_.empty()) {
        cue_follow_target_beat_index_ = -1;
        const std::string path_id = active_cue_path_id_;
        advance_cue_path(path_id, "instant");
    }
}

void RuntimeAPI::tick_param_ramps() {
    if (active_ramps_.empty()) return;
    auto* cg = core_.compiled_graph();
    if (!cg) { active_ramps_.clear(); return; }
    const double now = current_metronome_sample().beats_elapsed;

    for (auto& r : active_ramps_) {
        auto* cn = cg->find_node(r.node_id);
        if (!cn) { r.end_beat = -1.0; continue; }  // node gone → drop
        auto pi = cn->param_indices.find(r.param_name);
        if (pi == cn->param_indices.end()) { r.end_beat = -1.0; continue; }
        const double span = r.end_beat - r.start_beat;
        float t = (span > 1e-9) ? static_cast<float>((now - r.start_beat) / span) : 1.0f;
        if (t < 0.0f) t = 0.0f;
        if (t >= 1.0f) {
            cn->param_values[pi->second] = r.target;
            r.end_beat = -1.0;  // complete → drop below
        } else {
            cn->param_values[pi->second] = r.start + (r.target - r.start) * t;
        }
        cn->dirty = true;
    }
    // The persisted destination already lives in NodeDef.params (set at launch),
    // so ramps only touch live audio state — no need to mark the graph dirty here.
    active_ramps_.erase(std::remove_if(active_ramps_.begin(), active_ramps_.end(),
        [](const ParamRamp& r){ return r.end_beat < 0.0; }),
        active_ramps_.end());
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

const std::string& RuntimeAPI::queued_scene_id() const {
    static const std::string empty;
    return pending_scene_launch_ ? pending_scene_launch_->scene_id : empty;
}

const std::string& RuntimeAPI::queued_clip_for(const std::string& track_id) const {
    static const std::string empty;
    for (const auto& p : pending_clip_launches_)
        if (p.track_id == track_id) return p.clip_id;
    return empty;
}

int RuntimeAPI::cue_follow_beats_remaining() const {
    if (cue_follow_target_beat_index_ < 0) return -1;
    const auto metronome = current_metronome_sample();
    const int64_t current_beat = static_cast<int64_t>(std::floor(metronome.beats_elapsed));
    return static_cast<int>(std::max<int64_t>(0, cue_follow_target_beat_index_ - current_beat));
}

} // namespace vivid
