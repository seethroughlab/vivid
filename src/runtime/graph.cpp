#include "runtime/graph.h"
#include "yyjson.h"
#include <cstdio>
#include <algorithm>

namespace vivid {

static bool split_address(const char* addr, std::string& node, std::string& port) {
    const char* slash = std::strchr(addr, '/');
    if (!slash) return false;
    node.assign(addr, slash);
    port.assign(slash + 1);
    return !node.empty() && !port.empty();
}

bool Graph::load(const char* path) {
    nodes_.clear();
    connections_.clear();
    midi_mappings_.clear();
    filters_.clear();
    variations_.clear();
    node_presets_.clear();
    state_preset_mappings_.clear();
    active_variation_ = -1;
    quantize_clock_node_.clear();
    source_path_ = path;

    yyjson_read_err err;
    yyjson_doc* doc = yyjson_read_file(path, 0, nullptr, &err);
    if (!doc) {
        std::fprintf(stderr, "[vivid] Graph: failed to read %s: %s\n", path, err.msg);
        return false;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);

    // Parse filters (before nodes, since nodes may reference user filter types)
    yyjson_val* filters_obj = yyjson_obj_get(root, "filters");
    if (filters_obj && yyjson_is_obj(filters_obj)) {
        yyjson_obj_iter fiter;
        yyjson_obj_iter_init(filters_obj, &fiter);
        yyjson_val* fkey;
        while ((fkey = yyjson_obj_iter_next(&fiter)) != nullptr) {
            yyjson_val* fval = yyjson_obj_iter_get_val(fkey);
            FilterDef fd;
            fd.name = yyjson_get_str(fkey);

            yyjson_val* src_val = yyjson_obj_get(fval, "source");
            if (src_val && yyjson_is_str(src_val))
                fd.source = yyjson_get_str(src_val);

            yyjson_val* td_val = yyjson_obj_get(fval, "time_dependent");
            if (td_val && yyjson_is_bool(td_val))
                fd.time_dependent = yyjson_get_bool(td_val);

            yyjson_val* shader_val = yyjson_obj_get(fval, "shader");
            if (shader_val && yyjson_is_str(shader_val))
                fd.shader = yyjson_get_str(shader_val);

            yyjson_val* params_arr = yyjson_obj_get(fval, "params");
            if (params_arr && yyjson_is_arr(params_arr)) {
                size_t pidx, pmax;
                yyjson_val* pval;
                yyjson_arr_foreach(params_arr, pidx, pmax, pval) {
                    FilterDef::ParamDef pd;
                    yyjson_val* pn = yyjson_obj_get(pval, "name");
                    if (pn && yyjson_is_str(pn)) pd.name = yyjson_get_str(pn);
                    yyjson_val* pdef = yyjson_obj_get(pval, "default");
                    if (pdef && yyjson_is_num(pdef)) pd.default_value = static_cast<float>(yyjson_get_num(pdef));
                    yyjson_val* pmin = yyjson_obj_get(pval, "min");
                    if (pmin && yyjson_is_num(pmin)) pd.min_value = static_cast<float>(yyjson_get_num(pmin));
                    yyjson_val* pmax_v = yyjson_obj_get(pval, "max");
                    if (pmax_v && yyjson_is_num(pmax_v)) pd.max_value = static_cast<float>(yyjson_get_num(pmax_v));
                    fd.params.push_back(std::move(pd));
                }
            }

            std::fprintf(stderr, "[vivid] Graph: loaded filter '%s' (source=%s, %zu params)\n",
                         fd.name.c_str(), fd.source.c_str(), fd.params.size());
            filters_.push_back(std::move(fd));
        }
    }

    // Parse nodes
    yyjson_val* nodes_obj = yyjson_obj_get(root, "nodes");
    if (nodes_obj && yyjson_is_obj(nodes_obj)) {
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(nodes_obj, &iter);
        yyjson_val* key;
        while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
            yyjson_val* val = yyjson_obj_iter_get_val(key);

            NodeDef node;
            node.id = yyjson_get_str(key);

            yyjson_val* type_val = yyjson_obj_get(val, "type");
            if (!type_val || !yyjson_is_str(type_val)) {
                std::fprintf(stderr, "[vivid] Graph: node '%s' missing type\n", node.id.c_str());
                yyjson_doc_free(doc);
                return false;
            }
            node.type = yyjson_get_str(type_val);

            yyjson_val* params_obj = yyjson_obj_get(val, "params");
            if (params_obj && yyjson_is_obj(params_obj)) {
                yyjson_obj_iter piter;
                yyjson_obj_iter_init(params_obj, &piter);
                yyjson_val* pkey;
                while ((pkey = yyjson_obj_iter_next(&piter)) != nullptr) {
                    yyjson_val* pval = yyjson_obj_iter_get_val(pkey);
                    if (yyjson_is_num(pval)) {
                        node.params[yyjson_get_str(pkey)] = static_cast<float>(yyjson_get_num(pval));
                    } else if (yyjson_is_str(pval)) {
                        node.string_params[yyjson_get_str(pkey)] = yyjson_get_str(pval);
                    }
                }
            }

            // Optional layout position
            yyjson_val* layout_obj = yyjson_obj_get(val, "layout");
            if (layout_obj && yyjson_is_obj(layout_obj)) {
                yyjson_val* lx = yyjson_obj_get(layout_obj, "x");
                yyjson_val* ly = yyjson_obj_get(layout_obj, "y");
                if (lx && yyjson_is_num(lx) && ly && yyjson_is_num(ly)) {
                    node.layout_x = static_cast<float>(yyjson_get_num(lx));
                    node.layout_y = static_cast<float>(yyjson_get_num(ly));
                }
            }

            // Optional per-node GPU texture resolution
            yyjson_val* res_arr = yyjson_obj_get(val, "resolution");
            if (res_arr && yyjson_is_arr(res_arr) && yyjson_arr_size(res_arr) == 2) {
                yyjson_val* rw = yyjson_arr_get(res_arr, 0);
                yyjson_val* rh = yyjson_arr_get(res_arr, 1);
                if (rw && yyjson_is_int(rw) && rh && yyjson_is_int(rh)) {
                    node.tex_width  = static_cast<uint32_t>(yyjson_get_int(rw));
                    node.tex_height = static_cast<uint32_t>(yyjson_get_int(rh));
                }
            }

            // Optional per-parameter lock flags
            yyjson_val* locks_obj = yyjson_obj_get(val, "locks");
            if (locks_obj && yyjson_is_obj(locks_obj)) {
                yyjson_obj_iter liter;
                yyjson_obj_iter_init(locks_obj, &liter);
                yyjson_val* lkey;
                while ((lkey = yyjson_obj_iter_next(&liter)) != nullptr) {
                    yyjson_val* lval = yyjson_obj_iter_get_val(lkey);
                    if (lval && yyjson_is_int(lval)) {
                        node.param_lock_flags[yyjson_get_str(lkey)] =
                            static_cast<uint8_t>(yyjson_get_int(lval));
                    }
                }
            }

            nodes_.push_back(std::move(node));
        }
    }

    // Parse connections
    yyjson_val* conns_arr = yyjson_obj_get(root, "connections");
    if (conns_arr && yyjson_is_arr(conns_arr)) {
        size_t idx, max;
        yyjson_val* val;
        yyjson_arr_foreach(conns_arr, idx, max, val) {
            yyjson_val* from_val = yyjson_obj_get(val, "from");
            yyjson_val* to_val   = yyjson_obj_get(val, "to");
            if (!from_val || !to_val) continue;

            ConnectionDef conn;
            if (!split_address(yyjson_get_str(from_val), conn.from_node, conn.from_port) ||
                !split_address(yyjson_get_str(to_val),   conn.to_node,   conn.to_port)) {
                std::fprintf(stderr, "[vivid] Graph: invalid connection address\n");
                continue;
            }
            yyjson_val* scale_val = yyjson_obj_get(val, "scale");
            if (scale_val && yyjson_is_num(scale_val))
                conn.scale = static_cast<float>(yyjson_get_num(scale_val));
            connections_.push_back(std::move(conn));
        }
    }

    // Parse MIDI mappings
    yyjson_val* midi_arr = yyjson_obj_get(root, "midi_mappings");
    if (midi_arr && yyjson_is_arr(midi_arr)) {
        size_t midx, mmax;
        yyjson_val* mval;
        yyjson_arr_foreach(midi_arr, midx, mmax, mval) {
            MidiMappingDef mm;
            yyjson_val* node_val = yyjson_obj_get(mval, "node");
            yyjson_val* param_val = yyjson_obj_get(mval, "param");
            yyjson_val* cc_val = yyjson_obj_get(mval, "cc");
            if (!node_val || !param_val || !cc_val) continue;
            mm.node_id = yyjson_get_str(node_val);
            mm.param_name = yyjson_get_str(param_val);
            mm.cc_number = static_cast<int>(yyjson_get_int(cc_val));
            yyjson_val* chan_val = yyjson_obj_get(mval, "channel");
            if (chan_val && yyjson_is_int(chan_val))
                mm.channel = static_cast<int>(yyjson_get_int(chan_val));
            yyjson_val* rmin_val = yyjson_obj_get(mval, "range_min");
            if (rmin_val && yyjson_is_num(rmin_val))
                mm.range_min = static_cast<float>(yyjson_get_num(rmin_val));
            yyjson_val* rmax_val = yyjson_obj_get(mval, "range_max");
            if (rmax_val && yyjson_is_num(rmax_val))
                mm.range_max = static_cast<float>(yyjson_get_num(rmax_val));
            midi_mappings_.push_back(std::move(mm));
        }
    }

    // Parse viewport
    yyjson_val* vp_obj = yyjson_obj_get(root, "viewport");
    if (vp_obj && yyjson_is_obj(vp_obj)) {
        yyjson_val* vpx = yyjson_obj_get(vp_obj, "pan_x");
        yyjson_val* vpy = yyjson_obj_get(vp_obj, "pan_y");
        yyjson_val* vpz = yyjson_obj_get(vp_obj, "zoom");
        if (vpx && yyjson_is_num(vpx) && vpy && yyjson_is_num(vpy) && vpz && yyjson_is_num(vpz)) {
            viewport_pan_x = static_cast<float>(yyjson_get_num(vpx));
            viewport_pan_y = static_cast<float>(yyjson_get_num(vpy));
            viewport_zoom  = static_cast<float>(yyjson_get_num(vpz));
        }
    }

    // Parse variations
    yyjson_val* var_arr = yyjson_obj_get(root, "variations");
    if (var_arr && yyjson_is_arr(var_arr)) {
        size_t vidx, vmax;
        yyjson_val* vval;
        yyjson_arr_foreach(var_arr, vidx, vmax, vval) {
            VariationDef vd;
            yyjson_val* vname = yyjson_obj_get(vval, "name");
            if (vname && yyjson_is_str(vname))
                vd.name = yyjson_get_str(vname);
            yyjson_val* vparams = yyjson_obj_get(vval, "params");
            if (vparams && yyjson_is_obj(vparams)) {
                yyjson_obj_iter niter;
                yyjson_obj_iter_init(vparams, &niter);
                yyjson_val* nkey;
                while ((nkey = yyjson_obj_iter_next(&niter)) != nullptr) {
                    yyjson_val* nval = yyjson_obj_iter_get_val(nkey);
                    std::string node_id = yyjson_get_str(nkey);
                    if (nval && yyjson_is_obj(nval)) {
                        auto& pm = vd.params[node_id];
                        yyjson_obj_iter piter;
                        yyjson_obj_iter_init(nval, &piter);
                        yyjson_val* pkey;
                        while ((pkey = yyjson_obj_iter_next(&piter)) != nullptr) {
                            yyjson_val* pv = yyjson_obj_iter_get_val(pkey);
                            if (pv && yyjson_is_num(pv))
                                pm[yyjson_get_str(pkey)] = static_cast<float>(yyjson_get_num(pv));
                        }
                    }
                }
            }
            variations_.push_back(std::move(vd));
        }
    }

    // Parse active_variation
    yyjson_val* av_val = yyjson_obj_get(root, "active_variation");
    if (av_val && yyjson_is_int(av_val))
        active_variation_ = static_cast<int>(yyjson_get_int(av_val));

    // Parse quantize_clock
    yyjson_val* qc_val = yyjson_obj_get(root, "quantize_clock");
    if (qc_val && yyjson_is_str(qc_val))
        quantize_clock_node_ = yyjson_get_str(qc_val);

    // Parse per-operator presets
    yyjson_val* presets_obj = yyjson_obj_get(root, "presets");
    if (presets_obj && yyjson_is_obj(presets_obj)) {
        yyjson_obj_iter priter;
        yyjson_obj_iter_init(presets_obj, &priter);
        yyjson_val* prkey;
        while ((prkey = yyjson_obj_iter_next(&priter)) != nullptr) {
            yyjson_val* prval = yyjson_obj_iter_get_val(prkey);
            std::string node_id = yyjson_get_str(prkey);
            if (prval && yyjson_is_arr(prval)) {
                auto& presets = node_presets_[node_id];
                size_t pidx2, pmax2;
                yyjson_val* pentry;
                yyjson_arr_foreach(prval, pidx2, pmax2, pentry) {
                    OperatorPreset op;
                    yyjson_val* pn = yyjson_obj_get(pentry, "name");
                    if (pn && yyjson_is_str(pn))
                        op.name = yyjson_get_str(pn);
                    yyjson_val* pp = yyjson_obj_get(pentry, "params");
                    if (pp && yyjson_is_obj(pp)) {
                        yyjson_obj_iter ppiter;
                        yyjson_obj_iter_init(pp, &ppiter);
                        yyjson_val* ppkey;
                        while ((ppkey = yyjson_obj_iter_next(&ppiter)) != nullptr) {
                            yyjson_val* ppv = yyjson_obj_iter_get_val(ppkey);
                            if (ppv && yyjson_is_num(ppv))
                                op.params[yyjson_get_str(ppkey)] = static_cast<float>(yyjson_get_num(ppv));
                        }
                    }
                    presets.push_back(std::move(op));
                }
            }
        }
    }

    // Parse state_preset_mappings
    yyjson_val* spm_arr = yyjson_obj_get(root, "state_preset_mappings");
    if (spm_arr && yyjson_is_arr(spm_arr)) {
        size_t sidx2, smax2;
        yyjson_val* sentry;
        yyjson_arr_foreach(spm_arr, sidx2, smax2, sentry) {
            StatePresetMapping spm;
            yyjson_val* sn = yyjson_obj_get(sentry, "node");
            if (sn && yyjson_is_str(sn))
                spm.state_machine_node = yyjson_get_str(sn);
            yyjson_val* states_arr = yyjson_obj_get(sentry, "states");
            if (states_arr && yyjson_is_arr(states_arr)) {
                size_t si, sm;
                yyjson_val* state_obj;
                yyjson_arr_foreach(states_arr, si, sm, state_obj) {
                    std::unordered_map<std::string, std::string> bindings;
                    if (state_obj && yyjson_is_obj(state_obj)) {
                        yyjson_obj_iter biter;
                        yyjson_obj_iter_init(state_obj, &biter);
                        yyjson_val* bkey;
                        while ((bkey = yyjson_obj_iter_next(&biter)) != nullptr) {
                            yyjson_val* bval = yyjson_obj_iter_get_val(bkey);
                            if (bval && yyjson_is_str(bval))
                                bindings[yyjson_get_str(bkey)] = yyjson_get_str(bval);
                        }
                    }
                    spm.state_presets.push_back(std::move(bindings));
                }
            }
            state_preset_mappings_.push_back(std::move(spm));
        }
    }

    yyjson_doc_free(doc);

    std::fprintf(stderr, "[vivid] Loaded graph: %s (%zu nodes, %zu connections)\n",
        path, nodes_.size(), connections_.size());
    return true;
}

// --- Mutation ---

bool Graph::add_node(const std::string& id, const std::string& type,
                     const std::unordered_map<std::string, float>& params,
                     const std::unordered_map<std::string, std::string>& string_params) {
    if (find_node(id)) return false;  // duplicate id
    NodeDef node;
    node.id = id;
    node.type = type;
    node.params = params;
    node.string_params = string_params;
    nodes_.push_back(std::move(node));
    return true;
}

bool Graph::remove_node(const std::string& id) {
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
        [&](const NodeDef& n) { return n.id == id; });
    if (it == nodes_.end()) return false;
    nodes_.erase(it);
    // Remove all connections involving this node
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
            [&](const ConnectionDef& c) {
                return c.from_node == id || c.to_node == id;
            }),
        connections_.end());
    // Remove all MIDI mappings referencing this node
    midi_mappings_.erase(
        std::remove_if(midi_mappings_.begin(), midi_mappings_.end(),
            [&](const MidiMappingDef& m) { return m.node_id == id; }),
        midi_mappings_.end());
    return true;
}

bool Graph::add_connection(const std::string& from_node, const std::string& from_port,
                           const std::string& to_node, const std::string& to_port) {
    // Check for duplicate
    for (const auto& c : connections_) {
        if (c.from_node == from_node && c.from_port == from_port &&
            c.to_node == to_node && c.to_port == to_port)
            return false;
    }
    connections_.push_back({from_node, from_port, to_node, to_port});
    return true;
}

bool Graph::remove_connection(const std::string& from_node, const std::string& from_port,
                              const std::string& to_node, const std::string& to_port) {
    auto it = std::find_if(connections_.begin(), connections_.end(),
        [&](const ConnectionDef& c) {
            return c.from_node == from_node && c.from_port == from_port &&
                   c.to_node == to_node && c.to_port == to_port;
        });
    if (it == connections_.end()) return false;
    connections_.erase(it);
    return true;
}

bool Graph::set_connection_scale(const std::string& from_node, const std::string& from_port,
                                  const std::string& to_node, const std::string& to_port, float scale) {
    for (auto& c : connections_) {
        if (c.from_node == from_node && c.from_port == from_port &&
            c.to_node == to_node && c.to_port == to_port) {
            c.scale = scale;
            return true;
        }
    }
    return false;
}

const NodeDef* Graph::find_node(const std::string& id) const {
    for (const auto& n : nodes_) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

NodeDef* Graph::find_node(const std::string& id) {
    for (auto& n : nodes_) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

// --- Filter Mutation ---

void Graph::add_filter(FilterDef filter) {
    // Replace if already exists
    for (auto& f : filters_) {
        if (f.name == filter.name) {
            f = std::move(filter);
            return;
        }
    }
    filters_.push_back(std::move(filter));
}

const FilterDef* Graph::find_filter(const std::string& name) const {
    for (const auto& f : filters_) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

FilterDef* Graph::find_filter(const std::string& name) {
    for (auto& f : filters_) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

bool Graph::remove_filter(const std::string& name) {
    auto it = std::find_if(filters_.begin(), filters_.end(),
        [&](const FilterDef& f) { return f.name == name; });
    if (it == filters_.end()) return false;
    filters_.erase(it);
    return true;
}

void Graph::update_filter_shader(const std::string& name, const std::string& source) {
    auto* f = find_filter(name);
    if (f) f->shader = source;
}

// --- MIDI Mapping Mutation ---

bool Graph::add_midi_mapping(const std::string& node_id, const std::string& param,
                             int cc, int channel, float range_min, float range_max) {
    // Replace existing mapping for the same node/param
    for (auto& m : midi_mappings_) {
        if (m.node_id == node_id && m.param_name == param) {
            m.cc_number = cc;
            m.channel = channel;
            m.range_min = range_min;
            m.range_max = range_max;
            return true;
        }
    }
    midi_mappings_.push_back({node_id, param, cc, channel, range_min, range_max});
    return true;
}

bool Graph::remove_midi_mapping(const std::string& node_id, const std::string& param) {
    auto it = std::find_if(midi_mappings_.begin(), midi_mappings_.end(),
        [&](const MidiMappingDef& m) {
            return m.node_id == node_id && m.param_name == param;
        });
    if (it == midi_mappings_.end()) return false;
    midi_mappings_.erase(it);
    return true;
}

bool Graph::update_midi_mapping(const std::string& node_id, const std::string& param,
                                float range_min, float range_max) {
    for (auto& m : midi_mappings_) {
        if (m.node_id == node_id && m.param_name == param) {
            m.range_min = range_min;
            m.range_max = range_max;
            return true;
        }
    }
    return false;
}

const MidiMappingDef* Graph::find_midi_mapping(const std::string& node_id,
                                               const std::string& param) const {
    for (const auto& m : midi_mappings_) {
        if (m.node_id == node_id && m.param_name == param) return &m;
    }
    return nullptr;
}

// --- Variation Mutation ---

void Graph::add_variation(VariationDef v) {
    // Replace if already exists
    for (auto& existing : variations_) {
        if (existing.name == v.name) {
            existing = std::move(v);
            return;
        }
    }
    variations_.push_back(std::move(v));
}

bool Graph::remove_variation(const std::string& name) {
    auto it = std::find_if(variations_.begin(), variations_.end(),
        [&](const VariationDef& v) { return v.name == name; });
    if (it == variations_.end()) return false;
    int idx = static_cast<int>(it - variations_.begin());
    variations_.erase(it);
    // Adjust active_variation index
    if (active_variation_ == idx)
        active_variation_ = -1;
    else if (active_variation_ > idx)
        active_variation_--;
    return true;
}

bool Graph::rename_variation(const std::string& old_name, const std::string& new_name) {
    auto* v = find_variation(old_name);
    if (!v) return false;
    if (find_variation(new_name)) return false; // name conflict
    v->name = new_name;
    return true;
}

const VariationDef* Graph::find_variation(const std::string& name) const {
    for (const auto& v : variations_) {
        if (v.name == name) return &v;
    }
    return nullptr;
}

VariationDef* Graph::find_variation(const std::string& name) {
    for (auto& v : variations_) {
        if (v.name == name) return &v;
    }
    return nullptr;
}

int Graph::find_variation_index(const std::string& name) const {
    for (int i = 0; i < static_cast<int>(variations_.size()); ++i) {
        if (variations_[i].name == name) return i;
    }
    return -1;
}

// --- Per-Operator Preset CRUD ---

void Graph::save_preset(const std::string& node_id, const OperatorPreset& preset) {
    auto& presets = node_presets_[node_id];
    for (auto& p : presets) {
        if (p.name == preset.name) {
            p = preset;
            return;
        }
    }
    presets.push_back(preset);
}

bool Graph::remove_preset(const std::string& node_id, const std::string& name) {
    auto it = node_presets_.find(node_id);
    if (it == node_presets_.end()) return false;
    auto& presets = it->second;
    auto pit = std::find_if(presets.begin(), presets.end(),
        [&](const OperatorPreset& p) { return p.name == name; });
    if (pit == presets.end()) return false;
    presets.erase(pit);
    if (presets.empty()) node_presets_.erase(it);
    return true;
}

bool Graph::rename_preset(const std::string& node_id, const std::string& old_name,
                           const std::string& new_name) {
    auto* p = find_preset(node_id, old_name);
    if (!p) return false;
    if (find_preset(node_id, new_name)) return false;  // name conflict
    p->name = new_name;
    return true;
}

const OperatorPreset* Graph::find_preset(const std::string& node_id, const std::string& name) const {
    auto it = node_presets_.find(node_id);
    if (it == node_presets_.end()) return nullptr;
    for (const auto& p : it->second) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

OperatorPreset* Graph::find_preset(const std::string& node_id, const std::string& name) {
    auto it = node_presets_.find(node_id);
    if (it == node_presets_.end()) return nullptr;
    for (auto& p : it->second) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

std::vector<std::string> Graph::list_presets(const std::string& node_id) const {
    std::vector<std::string> names;
    auto it = node_presets_.find(node_id);
    if (it != node_presets_.end()) {
        for (const auto& p : it->second) names.push_back(p.name);
    }
    return names;
}

// --- State-Preset Mapping CRUD ---

void Graph::set_state_preset(const std::string& sm_node, int state_idx,
                              const std::string& target_node, const std::string& preset_name) {
    // Find or create mapping for this state machine
    StatePresetMapping* spm = nullptr;
    for (auto& m : state_preset_mappings_) {
        if (m.state_machine_node == sm_node) { spm = &m; break; }
    }
    if (!spm) {
        state_preset_mappings_.push_back({sm_node, {}});
        spm = &state_preset_mappings_.back();
    }
    // Grow state_presets vector if needed
    if (state_idx >= static_cast<int>(spm->state_presets.size())) {
        spm->state_presets.resize(state_idx + 1);
    }
    spm->state_presets[state_idx][target_node] = preset_name;
}

bool Graph::remove_state_preset(const std::string& sm_node, int state_idx,
                                 const std::string& target_node) {
    for (auto& m : state_preset_mappings_) {
        if (m.state_machine_node != sm_node) continue;
        if (state_idx >= static_cast<int>(m.state_presets.size())) return false;
        auto it = m.state_presets[state_idx].find(target_node);
        if (it == m.state_presets[state_idx].end()) return false;
        m.state_presets[state_idx].erase(it);
        return true;
    }
    return false;
}

void Graph::clear_state_presets(const std::string& sm_node) {
    state_preset_mappings_.erase(
        std::remove_if(state_preset_mappings_.begin(), state_preset_mappings_.end(),
            [&](const StatePresetMapping& m) { return m.state_machine_node == sm_node; }),
        state_preset_mappings_.end());
}

const StatePresetMapping* Graph::find_state_mapping(const std::string& sm_node) const {
    for (const auto& m : state_preset_mappings_) {
        if (m.state_machine_node == sm_node) return &m;
    }
    return nullptr;
}

// --- Serialization ---

bool Graph::save(const char* path) const {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    // Filters
    if (!filters_.empty()) {
        yyjson_mut_val* filters_obj = yyjson_mut_obj(doc);
        for (const auto& fd : filters_) {
            yyjson_mut_val* f_obj = yyjson_mut_obj(doc);
            if (!fd.source.empty())
                yyjson_mut_obj_add_strcpy(doc, f_obj, "source", fd.source.c_str());
            if (fd.time_dependent)
                yyjson_mut_obj_add_bool(doc, f_obj, "time_dependent", true);

            if (!fd.params.empty()) {
                yyjson_mut_val* p_arr = yyjson_mut_arr(doc);
                for (const auto& pd : fd.params) {
                    yyjson_mut_val* p_obj = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_strcpy(doc, p_obj, "name", pd.name.c_str());
                    yyjson_mut_obj_add_real(doc, p_obj, "default", static_cast<double>(pd.default_value));
                    yyjson_mut_obj_add_real(doc, p_obj, "min", static_cast<double>(pd.min_value));
                    yyjson_mut_obj_add_real(doc, p_obj, "max", static_cast<double>(pd.max_value));
                    yyjson_mut_arr_add_val(p_arr, p_obj);
                }
                yyjson_mut_obj_add_val(doc, f_obj, "params", p_arr);
            }

            if (!fd.shader.empty())
                yyjson_mut_obj_add_strcpy(doc, f_obj, "shader", fd.shader.c_str());

            yyjson_mut_obj_add_val(doc, filters_obj, fd.name.c_str(), f_obj);
        }
        yyjson_mut_obj_add_val(doc, root, "filters", filters_obj);
    }

    // Nodes
    yyjson_mut_val* nodes_obj = yyjson_mut_obj(doc);
    for (const auto& node : nodes_) {
        yyjson_mut_val* node_obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, node_obj, "type", node.type.c_str());

        if (!node.params.empty() || !node.string_params.empty()) {
            yyjson_mut_val* params_obj = yyjson_mut_obj(doc);
            for (const auto& [pname, pval] : node.params) {
                yyjson_mut_obj_add_real(doc, params_obj, pname.c_str(), static_cast<double>(pval));
            }
            for (const auto& [pname, pval] : node.string_params) {
                yyjson_mut_obj_add_strcpy(doc, params_obj, pname.c_str(), pval.c_str());
            }
            yyjson_mut_obj_add_val(doc, node_obj, "params", params_obj);
        }

        if (node.has_layout()) {
            yyjson_mut_val* layout_obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_real(doc, layout_obj, "x", static_cast<double>(node.layout_x));
            yyjson_mut_obj_add_real(doc, layout_obj, "y", static_cast<double>(node.layout_y));
            yyjson_mut_obj_add_val(doc, node_obj, "layout", layout_obj);
        }

        if (node.tex_width > 0 && node.tex_height > 0) {
            yyjson_mut_val* res_arr = yyjson_mut_arr(doc);
            yyjson_mut_arr_add_int(doc, res_arr, static_cast<int64_t>(node.tex_width));
            yyjson_mut_arr_add_int(doc, res_arr, static_cast<int64_t>(node.tex_height));
            yyjson_mut_obj_add_val(doc, node_obj, "resolution", res_arr);
        }

        if (!node.param_lock_flags.empty()) {
            yyjson_mut_val* locks_obj = yyjson_mut_obj(doc);
            for (const auto& [pname, flags] : node.param_lock_flags) {
                yyjson_mut_obj_add_int(doc, locks_obj, pname.c_str(),
                                       static_cast<int64_t>(flags));
            }
            yyjson_mut_obj_add_val(doc, node_obj, "locks", locks_obj);
        }

        yyjson_mut_obj_add_val(doc, nodes_obj, node.id.c_str(), node_obj);
    }
    yyjson_mut_obj_add_val(doc, root, "nodes", nodes_obj);

    // Connections
    yyjson_mut_val* conns_arr = yyjson_mut_arr(doc);
    for (const auto& conn : connections_) {
        yyjson_mut_val* conn_obj = yyjson_mut_obj(doc);
        std::string from_addr = conn.from_node + "/" + conn.from_port;
        std::string to_addr = conn.to_node + "/" + conn.to_port;
        yyjson_mut_obj_add_strcpy(doc, conn_obj, "from", from_addr.c_str());
        yyjson_mut_obj_add_strcpy(doc, conn_obj, "to", to_addr.c_str());
        if (conn.scale != 1.0f)
            yyjson_mut_obj_add_real(doc, conn_obj, "scale", static_cast<double>(conn.scale));
        yyjson_mut_arr_add_val(conns_arr, conn_obj);
    }
    yyjson_mut_obj_add_val(doc, root, "connections", conns_arr);

    // MIDI mappings
    if (!midi_mappings_.empty()) {
        yyjson_mut_val* midi_arr = yyjson_mut_arr(doc);
        for (const auto& mm : midi_mappings_) {
            yyjson_mut_val* mm_obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_strcpy(doc, mm_obj, "node", mm.node_id.c_str());
            yyjson_mut_obj_add_strcpy(doc, mm_obj, "param", mm.param_name.c_str());
            yyjson_mut_obj_add_int(doc, mm_obj, "cc", mm.cc_number);
            if (mm.channel != 0)
                yyjson_mut_obj_add_int(doc, mm_obj, "channel", mm.channel);
            yyjson_mut_obj_add_real(doc, mm_obj, "range_min", static_cast<double>(mm.range_min));
            yyjson_mut_obj_add_real(doc, mm_obj, "range_max", static_cast<double>(mm.range_max));
            yyjson_mut_arr_add_val(midi_arr, mm_obj);
        }
        yyjson_mut_obj_add_val(doc, root, "midi_mappings", midi_arr);
    }

    // Variations
    if (!variations_.empty()) {
        yyjson_mut_val* var_arr = yyjson_mut_arr(doc);
        for (const auto& vd : variations_) {
            yyjson_mut_val* v_obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_strcpy(doc, v_obj, "name", vd.name.c_str());
            yyjson_mut_val* params_obj = yyjson_mut_obj(doc);
            for (const auto& [node_id, pm] : vd.params) {
                yyjson_mut_val* node_obj = yyjson_mut_obj(doc);
                for (const auto& [pname, pval] : pm) {
                    yyjson_mut_obj_add_real(doc, node_obj, pname.c_str(),
                                            static_cast<double>(pval));
                }
                yyjson_mut_obj_add_val(doc, params_obj, node_id.c_str(), node_obj);
            }
            yyjson_mut_obj_add_val(doc, v_obj, "params", params_obj);
            yyjson_mut_arr_add_val(var_arr, v_obj);
        }
        yyjson_mut_obj_add_val(doc, root, "variations", var_arr);
    }
    if (active_variation_ >= 0)
        yyjson_mut_obj_add_int(doc, root, "active_variation", active_variation_);
    if (!quantize_clock_node_.empty())
        yyjson_mut_obj_add_strcpy(doc, root, "quantize_clock", quantize_clock_node_.c_str());

    // Per-operator presets
    if (!node_presets_.empty()) {
        yyjson_mut_val* presets_obj = yyjson_mut_obj(doc);
        for (const auto& [node_id, presets] : node_presets_) {
            yyjson_mut_val* pr_arr = yyjson_mut_arr(doc);
            for (const auto& p : presets) {
                yyjson_mut_val* pr_obj = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_strcpy(doc, pr_obj, "name", p.name.c_str());
                yyjson_mut_val* pp_obj = yyjson_mut_obj(doc);
                for (const auto& [pname, pval] : p.params) {
                    yyjson_mut_obj_add_real(doc, pp_obj, pname.c_str(),
                                            static_cast<double>(pval));
                }
                yyjson_mut_obj_add_val(doc, pr_obj, "params", pp_obj);
                yyjson_mut_arr_add_val(pr_arr, pr_obj);
            }
            yyjson_mut_obj_add_val(doc, presets_obj, node_id.c_str(), pr_arr);
        }
        yyjson_mut_obj_add_val(doc, root, "presets", presets_obj);
    }

    // State-preset mappings
    if (!state_preset_mappings_.empty()) {
        yyjson_mut_val* spm_arr = yyjson_mut_arr(doc);
        for (const auto& spm : state_preset_mappings_) {
            yyjson_mut_val* spm_obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_strcpy(doc, spm_obj, "node", spm.state_machine_node.c_str());
            yyjson_mut_val* states_arr = yyjson_mut_arr(doc);
            for (const auto& bindings : spm.state_presets) {
                yyjson_mut_val* b_obj = yyjson_mut_obj(doc);
                for (const auto& [target_node, preset_name] : bindings) {
                    yyjson_mut_obj_add_strcpy(doc, b_obj, target_node.c_str(),
                                              preset_name.c_str());
                }
                yyjson_mut_arr_add_val(states_arr, b_obj);
            }
            yyjson_mut_obj_add_val(doc, spm_obj, "states", states_arr);
            yyjson_mut_arr_add_val(spm_arr, spm_obj);
        }
        yyjson_mut_obj_add_val(doc, root, "state_preset_mappings", spm_arr);
    }

    // Viewport
    if (has_viewport()) {
        yyjson_mut_val* vp_obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_real(doc, vp_obj, "pan_x", static_cast<double>(viewport_pan_x));
        yyjson_mut_obj_add_real(doc, vp_obj, "pan_y", static_cast<double>(viewport_pan_y));
        yyjson_mut_obj_add_real(doc, vp_obj, "zoom",  static_cast<double>(viewport_zoom));
        yyjson_mut_obj_add_val(doc, root, "viewport", vp_obj);
    }

    // Write
    yyjson_write_err werr;
    bool ok = yyjson_mut_write_file(path, doc, YYJSON_WRITE_PRETTY | YYJSON_WRITE_NEWLINE_AT_END,
                                     nullptr, &werr);
    yyjson_mut_doc_free(doc);

    if (!ok) {
        std::fprintf(stderr, "[vivid] Graph: failed to write %s: %s\n", path, werr.msg);
        return false;
    }

    std::fprintf(stderr, "[vivid] Graph saved: %s (%zu nodes, %zu connections)\n",
        path, nodes_.size(), connections_.size());
    return true;
}

} // namespace vivid
