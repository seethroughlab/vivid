#include "runtime/control/runtime_api.h"
#include "runtime/graph/graph.h"
#include "runtime/graph/subgraph_module.h"
#include "runtime/core/runtime_core.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/audio/system_midi.h"
#include <sstream>

namespace vivid {

namespace {
const VividOperatorDescriptor* node_descriptor(const CompiledNode& cn) {
    return cn.loader ? cn.loader->descriptor() : nullptr;
}
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

CommandResult RuntimeAPI::save_variation(const std::string& name) {
    VariationDef vd;
    vd.name = name;
    for (const auto& cn : core_.compiled_graph()->nodes) {
        const auto* desc = node_descriptor(cn);
        if (!desc) continue;
        auto& pm = vd.params[cn.node_id];
        for (const auto& [pname, idx] : cn.param_indices) {
            if (idx < desc->param_count &&
                desc->params[idx].type != VIVID_PARAM_FILE &&
                desc->params[idx].type != VIVID_PARAM_TEXT &&
                cn.param_values[idx] != desc->params[idx].default_value) {
                pm[pname] = cn.param_values[idx];
            }
        }
        if (pm.empty()) vd.params.erase(cn.node_id);

        for (const auto& [pname, fidx] : cn.file_param_indices) {
            const char* def_str = nullptr;
            auto pi = cn.param_indices.find(pname);
            if (pi != cn.param_indices.end() && pi->second < desc->param_count)
                def_str = desc->params[pi->second].default_string;
            const auto& val = cn.file_param_storage[fidx];
            if (!val.empty() && (def_str == nullptr || val != def_str)) {
                vd.string_params[cn.node_id][pname] = to_persisted_string_value(cn, pname, val);
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

    auto* cg = core_.compiled_graph();
    for (auto& cn : cg->nodes) {
        const auto* desc = node_descriptor(cn);
        if (!desc) continue;
        NodeDef* ndef = graph_.find_node(cn.node_id);
        for (const auto& [pname, pidx] : cn.param_indices) {
            if (cn.param_lock_flags[pidx] & PARAM_LOCK_PRESETS) continue;
            if (pidx < desc->param_count &&
                desc->params[pidx].type != VIVID_PARAM_FILE &&
                desc->params[pidx].type != VIVID_PARAM_TEXT) {
                cn.param_values[pidx] = desc->params[pidx].default_value;
                if (ndef) ndef->params[pname] = desc->params[pidx].default_value;
            }
        }
        for (const auto& [pname, fidx] : cn.file_param_indices) {
            auto pi = cn.param_indices.find(pname);
            if (pi != cn.param_indices.end() &&
                (cn.param_lock_flags[pi->second] & PARAM_LOCK_PRESETS))
                continue;
            if (pi != cn.param_indices.end() && pi->second < desc->param_count) {
                const char* def_str = desc->params[pi->second].default_string;
                std::string def_val = def_str ? def_str : "";
                cn.file_param_storage[fidx] = def_val;
                cn.file_param_ptrs[fidx] = cn.file_param_storage[fidx].c_str();
                if (ndef) {
                    if (def_val.empty())
                        ndef->string_params.erase(pname);
                    else
                        ndef->string_params[pname] = def_val;
                }
            }
        }
        cn.dirty = true;
    }

    for (const auto& [node_id, pm] : vd.params) {
        auto* cn = cg->find_node(node_id);
        if (!cn) continue;
        for (const auto& [pname, pval] : pm) {
            auto pi = cn->param_indices.find(pname);
            if (pi != cn->param_indices.end()) {
                if (!(cn->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS)) {
                    cn->param_values[pi->second] = pval;
                    NodeDef* ndef = graph_.find_node(node_id);
                    if (ndef) ndef->params[pname] = pval;
                }
            }
        }
        cn->dirty = true;
    }

    for (const auto& [node_id, spm] : vd.string_params) {
        auto* cn = cg->find_node(node_id);
        if (!cn) continue;
        for (const auto& [pname, pval] : spm) {
            auto fi = cn->file_param_indices.find(pname);
            if (fi == cn->file_param_indices.end()) continue;
            auto pi = cn->param_indices.find(pname);
            if (pi != cn->param_indices.end() &&
                (cn->param_lock_flags[pi->second] & PARAM_LOCK_PRESETS))
                continue;
            cn->file_param_storage[fi->second] = to_runtime_string_value(*cn, pname, pval);
            cn->file_param_ptrs[fi->second] = cn->file_param_storage[fi->second].c_str();
            NodeDef* ndef = graph_.find_node(node_id);
            if (ndef) ndef->string_params[pname] = pval;
        }
        cn->dirty = true;
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
    for (const auto& cn : core_.compiled_graph()->nodes) {
        const auto* desc = node_descriptor(cn);
        if (!desc) continue;
        auto& pm = vd->params[cn.node_id];
        for (const auto& [pname, idx] : cn.param_indices) {
            if (idx < desc->param_count &&
                desc->params[idx].type != VIVID_PARAM_FILE &&
                desc->params[idx].type != VIVID_PARAM_TEXT &&
                cn.param_values[idx] != desc->params[idx].default_value) {
                pm[pname] = cn.param_values[idx];
            }
        }
        if (pm.empty()) vd->params.erase(cn.node_id);

        for (const auto& [pname, fidx] : cn.file_param_indices) {
            const char* def_str = nullptr;
            auto pi = cn.param_indices.find(pname);
            if (pi != cn.param_indices.end() && pi->second < desc->param_count)
                def_str = desc->params[pi->second].default_string;
            const auto& val = cn.file_param_storage[fidx];
            if (!val.empty() && (def_str == nullptr || val != def_str)) {
                vd->string_params[cn.node_id][pname] = to_persisted_string_value(cn, pname, val);
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
        apply_variation(pending_variation_.variation_idx);
        return;
    }

    const auto* clock_cn = core_.compiled_graph()->find_node(clock_id);
    if (!clock_cn) {
        apply_variation(pending_variation_.variation_idx);
        return;
    }

    auto bp_it = clock_cn->output_port_indices.find("beat_phase");
    if (bp_it == clock_cn->output_port_indices.end() ||
        bp_it->second >= clock_cn->output_values.size()) {
        apply_variation(pending_variation_.variation_idx);
        return;
    }

    float beat_phase = clock_cn->output_values[bp_it->second];
    bool zero_crossing = (beat_phase < prev_beat_phase_) && (prev_beat_phase_ > 0.5f);
    prev_beat_phase_ = beat_phase;

    if (!zero_crossing) return;

    pending_variation_.beats_remaining--;
    if (pending_variation_.beats_remaining <= 0) {
        apply_variation(pending_variation_.variation_idx);
    }
}

const std::string& RuntimeAPI::active_preset(const std::string& node_id) const {
    static const std::string empty;
    auto it = active_presets_.find(node_id);
    return (it != active_presets_.end()) ? it->second : empty;
}

CommandResult RuntimeAPI::save_preset(const std::string& node_id, const std::string& name) {
    auto* cn = core_.compiled_graph()->find_node(node_id);

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
        graph_.save_preset(node_id, preset);
        active_presets_[node_id] = name;
        mark_graph_dirty();
        return {true, "saved preset '" + name + "' on " + node_id};
    }

    if (!cn) return {false, "unknown node '" + node_id + "'"};

    OperatorPreset preset;
    preset.name = name;
    for (const auto& [pname, idx] : cn->param_indices) {
        preset.params[pname] = cn->param_values[idx];
    }
    for (const auto& [pname, idx] : cn->file_param_indices) {
        preset.string_params[pname] = to_persisted_string_value(*cn, pname, cn->file_param_storage[idx]);
    }
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

    auto* cn = core_.compiled_graph()->find_node(node_id);

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

    auto* cn = core_.compiled_graph()->find_node(node_id);

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
        preset->params[pname] = cn->param_values[idx];
    }
    preset->string_params.clear();
    for (const auto& [pname, idx] : cn->file_param_indices) {
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
        const auto* sm_cn = core_.compiled_graph()->find_node(spm.state_machine_node);
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
                    auto* tcn = core_.compiled_graph()->find_node(target_node);
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
            auto* tcn = core_.compiled_graph()->find_node(target_node);
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
                auto* tcn = core_.compiled_graph()->find_node(target_node);
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
