#include "runtime/control/runtime_api.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/subgraph_module.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/audio/system_midi.h"
#include <nlohmann/json.hpp>
#include <sstream>

namespace vivid {

namespace {
const VividOperatorDescriptor* node_descriptor(const CompiledNode& cn) {
    return cn.loader ? cn.loader->descriptor() : nullptr;
}

constexpr const char* kNoCompiledGraph = "no compiled graph";
} // namespace

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
        float raw = 0.0f;
        if (mm.channel == 0) {
            for (int ch = 1; ch <= 16; ++ch) {
                float v = system_midi_->cc_value(ch, mm.cc_number);
                if (v > raw) raw = v;
            }
        } else {
            raw = system_midi_->cc_value(mm.channel, mm.cc_number);
        }

        float mapped = mm.range_min + raw * (mm.range_max - mm.range_min);
        set_param(mm.node_id, mm.param_name, mapped);
    }
}

CommandResult RuntimeAPI::set_quantize_clock(const std::string& node_id) {
    graph_.set_quantize_clock_node(node_id);
    mark_graph_dirty();
    return {true, "quantize clock set to '" + node_id + "' (deprecated; graph metronome now drives quantized switching)"};
}

CommandResult RuntimeAPI::set_launch_quantize(const std::string& mode) {
    if (mode != "instant" && mode != "beat" && mode != "bar" && mode != "4bar")
        return {false, "invalid launch quantize mode '" + mode +
                       "' (expected instant/beat/bar/4bar)"};
    graph_.set_launch_quantize(mode);
    mark_graph_dirty();
    return {true, "launch quantize set to '" + mode + "'"};
}

CommandResult RuntimeAPI::set_graph_metronome(float bpm, int beats_per_bar) {
    GraphMetronomeDef metronome = graph_.metronome();
    metronome.bpm = std::max(1.0f, std::min(300.0f, bpm));
    metronome.beats_per_bar = std::max(1, std::min(16, beats_per_bar));
    graph_.set_metronome(metronome);
    core_.update_live_metronome(metronome, core_.last_tick_time());
    mark_graph_dirty();
    return {true, "graph metronome updated"};
}

GraphMetronomeSample RuntimeAPI::current_metronome_sample() const {
    return core_.sample_live_metronome(core_.last_tick_time());
}

CommandResult RuntimeAPI::queue_state_transition(const std::string& sm_node_id,
                                                   int state_idx,
                                                   const std::string& quantize) {
    if (!core_.compiled_graph()) return {false, kNoCompiledGraph};
    if (state_idx < 0 || state_idx > 7) return {false, "state index out of range (0–7)"};

    if (quantize == "instant") {
        return set_param(sm_node_id, "force_state", static_cast<float>(state_idx));
    }

    const auto metronome = current_metronome_sample();
    const int bpb = std::max(1, metronome.beats_per_bar);
    const int64_t current_beat = static_cast<int64_t>(std::floor(metronome.beats_elapsed));

    PendingStateTransition::Quantize q = PendingStateTransition::Beat;
    int64_t target_beat = current_beat + 1;
    if (quantize == "bar") {
        q           = PendingStateTransition::Bar;
        target_beat = ((current_beat / bpb) + 1) * bpb;
    } else if (quantize == "4bar" || quantize == "four_bar") {
        q                 = PendingStateTransition::FourBar;
        const int four_bar = bpb * 4;
        target_beat       = ((current_beat / four_bar) + 1) * four_bar;
    }

    // Replace any existing pending transition for this SM node
    for (auto& p : pending_state_transitions_) {
        if (p.sm_node_id == sm_node_id) { p.armed = false; }
    }
    pending_state_transitions_.push_back({sm_node_id, state_idx, q, target_beat, true});

    return {true, "queued state " + std::to_string(state_idx) + " for '" + sm_node_id
                  + "' (" + quantize + ")"};
}

int RuntimeAPI::queued_state_for(const std::string& sm_node_id) const {
    for (const auto& p : pending_state_transitions_) {
        if (p.armed && p.sm_node_id == sm_node_id) return p.target_state;
    }
    return -1;
}

void RuntimeAPI::tick_quantized_state_transitions() {
    if (pending_state_transitions_.empty()) return;
    if (!core_.compiled_graph()) return;

    const auto metronome = current_metronome_sample();
    const int64_t current_beat = static_cast<int64_t>(std::floor(metronome.beats_elapsed));

    for (auto& p : pending_state_transitions_) {
        if (!p.armed) continue;
        if (current_beat >= p.target_beat_index) {
            set_param(p.sm_node_id, "force_state", static_cast<float>(p.target_state));
            p.armed = false;
        }
    }
    pending_state_transitions_.erase(
        std::remove_if(pending_state_transitions_.begin(), pending_state_transitions_.end(),
                       [](const PendingStateTransition& p) { return !p.armed; }),
        pending_state_transitions_.end());
}

const std::string& RuntimeAPI::active_preset(const std::string& node_id) const {
    static const std::string empty;
    auto it = active_presets_.find(node_id);
    return (it != active_presets_.end()) ? it->second : empty;
}

CommandResult RuntimeAPI::save_preset(const std::string& node_id, const std::string& name,
                                      const std::string& metadata) {
    auto* cg = core_.compiled_graph();
    if (!cg) return {false, kNoCompiledGraph};
    auto* cn = cg->find_node(node_id);

    // Module preset proxy: read live values from internal nodes
    if (!cn && subgraph_modules_) {
        const auto* ndef = graph_.find_node(node_id);
        if (!ndef) return {false, "unknown node '" + node_id + "'"};
        const auto* mod = subgraph_modules_->find(ndef->type);
        if (!mod) return {false, "unknown node '" + node_id + "'"};

        OperatorPreset preset;
        preset.name = name;
        for (const auto& pb : mod->params) {
            auto resolved = resolve_module_param(node_id, pb.name);
            if (resolved) {
                preset.params[pb.name] = resolved->cn->param_values[resolved->param_idx];
                auto fi = resolved->cn->file_param_indices.find(resolved->internal_param);
                if (fi != resolved->cn->file_param_indices.end() &&
                    fi->second < resolved->cn->file_param_storage.size()) {
                    preset.string_params[pb.name] = to_persisted_string_value(
                        *resolved->cn, resolved->internal_param,
                        resolved->cn->file_param_storage[fi->second]);
                }
            }
        }
        preset.metadata = metadata;
        graph_.save_preset(node_id, preset);
        active_presets_[node_id] = name;
        mark_graph_dirty();
        return {true, "saved preset '" + name + "' on " + node_id};
    }

    if (!cn) return {false, "unknown node '" + node_id + "'"};

    OperatorPreset preset;
    preset.name = name;
    for (const auto& [pname, idx] : cn->param_indices) {
        // String/file params have a meaningless numeric shadow (0); they are
        // captured as strings below. Skip so they don't leak as numeric 0.0.
        if (cn->file_param_indices.count(pname)) continue;
        preset.params[pname] = cn->param_values[idx];
    }
    for (const auto& [pname, idx] : cn->file_param_indices) {
        // Skip transient params (runtime-computed catalogs / scratch) — they bloat
        // the preset and are recomputed at runtime (same rule as graph save).
        if (idx < cn->file_param_persist.size() && !cn->file_param_persist[idx]) continue;
        preset.string_params[pname] = to_persisted_string_value(*cn, pname, cn->file_param_storage[idx]);
    }
    preset.metadata = metadata;
    graph_.save_preset(node_id, preset);
    active_presets_[node_id] = name;
    mark_graph_dirty();
    return {true, "saved preset '" + name + "' on " + node_id};
}

CommandResult RuntimeAPI::recall_preset(const std::string& node_id, const std::string& name) {
    const auto* preset = graph_.find_preset(node_id, name);

    const OperatorPreset* factory_hit = nullptr;
    OperatorPreset translated_module_preset;
    if (!preset) {
        const auto* ndef = graph_.find_node(node_id);
        if (ndef) {
            const auto* fps = registry_.factory_presets(ndef->type);
            if (fps) {
                for (const auto& fp : *fps) {
                    if (fp.name == name) { factory_hit = &fp; break; }
                }
            }
            // Fall back to module presets
            if (!factory_hit && subgraph_modules_) {
                const auto* mod = subgraph_modules_->find(ndef->type);
                if (mod) {
                    const auto* sp = mod->find_preset(name);
                    if (sp) {
                        translated_module_preset = to_operator_preset(*sp, *mod);
                        factory_hit = &translated_module_preset;
                    }
                }
            }
        }
        preset = factory_hit;
    }

    if (!preset) return {false, "preset '" + name + "' not found on " + node_id};

    auto* cg = core_.compiled_graph();
    if (!cg) return {false, kNoCompiledGraph};
    auto* cn = cg->find_node(node_id);

    // Module preset proxy: apply preset by routing each param to its internal node
    if (!cn) {
        NodeDef* ndef = graph_.find_node(node_id);
        if (!ndef) return {false, "unknown node '" + node_id + "'"};
        for (const auto& [pname, pval] : preset->params) {
            auto resolved = resolve_module_param(node_id, pname);
            if (resolved) {
                if (!(resolved->cn->param_lock_flags[resolved->param_idx] & PARAM_LOCK_PRESETS)) {
                    resolved->cn->param_values[resolved->param_idx] = pval;
                    resolved->cn->dirty = true;
                    ndef->params[pname] = pval;
                }
            }
        }
        for (const auto& [pname, pval] : preset->string_params) {
            auto resolved = resolve_module_param(node_id, pname);
            if (resolved) {
                auto fi = resolved->cn->file_param_indices.find(resolved->internal_param);
                if (fi != resolved->cn->file_param_indices.end()) {
                    resolved->cn->file_param_storage[fi->second] =
                        to_runtime_string_value(*resolved->cn, resolved->internal_param, pval);
                    resolved->cn->file_param_ptrs[fi->second] =
                        resolved->cn->file_param_storage[fi->second].c_str();
                    resolved->cn->dirty = true;
                    ndef->string_params[pname] = pval;
                }
            }
        }
        active_presets_[node_id] = name;
        mark_graph_dirty();
        return {true, "recalled preset '" + name + "' on " + node_id};
    }

    for (const auto& [pname, pval] : preset->params) {
        auto pi = cn->param_indices.find(pname);
        if (pi != cn->param_indices.end()) {
            if (!(cn->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS)) {
                cn->param_values[pi->second] = pval;
                NodeDef* ndef = graph_.find_node(node_id);
                if (ndef) ndef->params[pname] = pval;
            }
        }
    }
    for (const auto& [pname, pval] : preset->string_params) {
        auto fi = cn->file_param_indices.find(pname);
        if (fi != cn->file_param_indices.end()) {
            // Don't restore transient params (recomputed at runtime); also avoids
            // applying a stale catalog from presets captured before this rule.
            if (fi->second < cn->file_param_persist.size() && !cn->file_param_persist[fi->second])
                continue;
            auto pi = cn->param_indices.find(pname);
            if (pi != cn->param_indices.end() &&
                (cn->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS))
                continue;
            cn->file_param_storage[fi->second] = to_runtime_string_value(*cn, pname, pval);
            cn->file_param_ptrs[fi->second] = cn->file_param_storage[fi->second].c_str();
            NodeDef* ndef = graph_.find_node(node_id);
            if (ndef) ndef->string_params[pname] = pval;
        }
    }
    cn->dirty = true;
    active_presets_[node_id] = name;
    mark_graph_dirty();

    return {true, "recalled preset '" + name + "' on " + node_id};
}

CommandResult RuntimeAPI::update_preset(const std::string& node_id, const std::string& name) {
    auto* preset = graph_.find_preset(node_id, name);
    if (!preset) {
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

    auto* cg = core_.compiled_graph();
    if (!cg) return {false, kNoCompiledGraph};
    auto* cn = cg->find_node(node_id);

    // Module preset proxy: read live values from internal nodes
    if (!cn && subgraph_modules_) {
        const auto* ndef = graph_.find_node(node_id);
        if (!ndef) return {false, "unknown node '" + node_id + "'"};
        const auto* mod = subgraph_modules_->find(ndef->type);
        if (!mod) return {false, "unknown node '" + node_id + "'"};
        preset->params.clear();
        preset->string_params.clear();
        for (const auto& pb : mod->params) {
            auto resolved = resolve_module_param(node_id, pb.name);
            if (resolved) {
                preset->params[pb.name] = resolved->cn->param_values[resolved->param_idx];
                auto fi = resolved->cn->file_param_indices.find(resolved->internal_param);
                if (fi != resolved->cn->file_param_indices.end() &&
                    fi->second < resolved->cn->file_param_storage.size()) {
                    preset->string_params[pb.name] = to_persisted_string_value(
                        *resolved->cn, resolved->internal_param,
                        resolved->cn->file_param_storage[fi->second]);
                }
            }
        }
        mark_graph_dirty();
        return {true, "updated preset '" + name + "' on " + node_id};
    }

    if (!cn) return {false, "unknown node '" + node_id + "'"};

    preset->params.clear();
    for (const auto& [pname, idx] : cn->param_indices) {
        if (cn->file_param_indices.count(pname)) continue;  // string params captured below
        preset->params[pname] = cn->param_values[idx];
    }
    preset->string_params.clear();
    for (const auto& [pname, idx] : cn->file_param_indices) {
        if (idx < cn->file_param_persist.size() && !cn->file_param_persist[idx]) continue;
        preset->string_params[pname] = to_persisted_string_value(*cn, pname, cn->file_param_storage[idx]);
    }
    mark_graph_dirty();
    return {true, "updated preset '" + name + "' on " + node_id};
}

CommandResult RuntimeAPI::remove_preset(const std::string& node_id, const std::string& name) {
    if (!graph_.remove_preset(node_id, name)) {
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

std::string RuntimeAPI::list_presets_json(const std::string& node_id) const {
    nlohmann::json arr = nlohmann::json::array();
    std::string names;
    const auto& all = graph_.node_presets();
    auto it = all.find(node_id);
    if (it != all.end()) {
        for (const auto& p : it->second) {
            nlohmann::json e;
            e["name"] = p.name;
            if (!p.metadata.empty()) {
                auto m = nlohmann::json::parse(p.metadata, nullptr, false);
                if (m.is_object()) e["metadata"] = std::move(m);
            }
            arr.push_back(std::move(e));
            if (!names.empty()) names += ", ";
            names += p.name;
        }
    }
    return nlohmann::json{{"ok", true}, {"presets", arr}, {"msg", names}}.dump();
}

CommandResult RuntimeAPI::list_factory_presets(const std::string& node_id) {
    const auto* ndef = graph_.find_node(node_id);
    if (!ndef) return {false, "unknown node '" + node_id + "'"};
    auto names = registry_.factory_preset_names(ndef->type);
    // Fall back to module presets
    if (names.empty() && subgraph_modules_) {
        const auto* mod = subgraph_modules_->find(ndef->type);
        if (mod) {
            for (const auto& p : mod->presets)
                names.push_back(p.name);
        }
    }
    if (names.empty()) return {true, "(no factory presets for " + ndef->type + ")"};
    std::ostringstream oss;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << names[i];
    }
    return {true, oss.str()};
}

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

CommandResult RuntimeAPI::ensure_state_mapping(const std::string& sm_node) {
    if (!core_.compiled_graph()) return {false, "no graph"};
    graph_.ensure_state_mapping(sm_node);
    mark_graph_dirty();
    return {true, "ok"};
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
    auto* cg = core_.compiled_graph();
    if (!cg) return;

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
        const auto* sm_cn = cg->find_node(spm.state_machine_node);
        if (!sm_cn) continue;

        auto oi = sm_cn->output_port_indices.find("state");
        if (oi == sm_cn->output_port_indices.end()) continue;
        float current_state = sm_cn->output_values[oi->second];

        auto [prev_it, first_seen] = prev_sm_state_.emplace(spm.state_machine_node, -1.0f);
        if (current_state != prev_it->second) {
            prev_it->second = current_state;

            int state_idx = static_cast<int>(current_state);
            if (state_idx < 0 || state_idx >= static_cast<int>(spm.state_presets.size()))
                continue;

            int xf_mode = 0;
            float xf_bars = 0.0f;
            auto xm_it = sm_cn->param_indices.find("xfade_mode");
            if (xm_it != sm_cn->param_indices.end())
                xf_mode = static_cast<int>(sm_cn->param_values[xm_it->second]);
            auto xb_it = sm_cn->param_indices.find("xfade_bars");
            if (xb_it != sm_cn->param_indices.end())
                xf_bars = sm_cn->param_values[xb_it->second];

            if (xf_mode == 0 || xf_bars <= 0.0f) {
                active_crossfades_.erase(spm.state_machine_node);
                for (const auto& [target_node, preset_name] : spm.state_presets[state_idx]) {
                    recall_preset(target_node, preset_name);
                }
            } else {
                ActiveCrossfade ac;
                ac.sm_node_id = spm.state_machine_node;
                for (const auto& [target_node, preset_name] : spm.state_presets[state_idx]) {
                    const auto* preset = graph_.find_preset(target_node, preset_name);
                    if (!preset) continue;
                    auto* tcn = cg->find_node(target_node);
                    if (!tcn) continue;

                    CrossfadeState cs;
                    cs.target_preset_name = preset_name;
                    for (const auto& [pname, pval] : preset->params) {
                        auto pi = tcn->param_indices.find(pname);
                        if (pi == tcn->param_indices.end()) continue;
                        if (tcn->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS) continue;
                        cs.start_params[pname] = tcn->param_values[pi->second];
                        cs.target_params[pname] = pval;
                    }
                    for (const auto& [pname, pval] : preset->string_params) {
                        auto fi = tcn->file_param_indices.find(pname);
                        if (fi == tcn->file_param_indices.end()) continue;
                        auto pi = tcn->param_indices.find(pname);
                        if (pi != tcn->param_indices.end() &&
                            (tcn->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS))
                            continue;
                        tcn->file_param_storage[fi->second] = to_runtime_string_value(*tcn, pname, pval);
                        tcn->file_param_ptrs[fi->second] = tcn->file_param_storage[fi->second].c_str();
                        NodeDef* ndef = graph_.find_node(target_node);
                        if (ndef) ndef->string_params[pname] = pval;
                    }
                    ac.targets[target_node] = std::move(cs);
                }
                active_crossfades_[spm.state_machine_node] = std::move(ac);
            }
        }

        auto acit = active_crossfades_.find(spm.state_machine_node);
        if (acit == active_crossfades_.end()) continue;

        auto xf_oi = sm_cn->output_port_indices.find("xfade");
        if (xf_oi == sm_cn->output_port_indices.end()) continue;
        float xfade_t = sm_cn->output_values[xf_oi->second];

        for (auto& [target_node, cs] : acit->second.targets) {
            auto* tcn = cg->find_node(target_node);
            if (!tcn) continue;
            NodeDef* ndef = graph_.find_node(target_node);
            for (const auto& [pname, start_val] : cs.start_params) {
                auto pi = tcn->param_indices.find(pname);
                if (pi == tcn->param_indices.end()) continue;
                float target_val = cs.target_params[pname];
                float interp = start_val + (target_val - start_val) * xfade_t;
                tcn->param_values[pi->second] = interp;
                if (ndef) ndef->params[pname] = interp;
            }
            tcn->dirty = true;
        }

        if (xfade_t >= 1.0f) {
            for (auto& [target_node, cs] : acit->second.targets) {
                auto* tcn = cg->find_node(target_node);
                if (tcn) {
                    NodeDef* ndef = graph_.find_node(target_node);
                    for (const auto& [pname, target_val] : cs.target_params) {
                        auto pi = tcn->param_indices.find(pname);
                        if (pi != tcn->param_indices.end()) {
                            tcn->param_values[pi->second] = target_val;
                            if (ndef) ndef->params[pname] = target_val;
                        }
                    }
                    tcn->dirty = true;
                }
                active_presets_[target_node] = cs.target_preset_name;
            }
            active_crossfades_.erase(acit);
        }
    }
}

} // namespace vivid
