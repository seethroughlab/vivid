#include "runtime/graph.h"
#include "yyjson.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#ifndef VIVID_CORE_VERSION
#define VIVID_CORE_VERSION "0.1.0"
#endif

namespace vivid {

static bool split_address(const char* addr, std::string& node, std::string& port) {
    const char* slash = std::strchr(addr, '/');
    if (!slash) return false;
    node.assign(addr, slash);
    port.assign(slash + 1);
    return !node.empty() && !port.empty();
}

bool Graph::load(const char* path) {
    source_path_ = path;

    yyjson_read_err err;
    yyjson_doc* doc = yyjson_read_file(path, 0, nullptr, &err);
    if (!doc) {
        std::fprintf(stderr, "[vivid] Graph: failed to read %s: %s\n", path, err.msg);
        return false;
    }

    bool ok = parse_doc(doc);
    yyjson_doc_free(doc);

    if (ok) {
        std::fprintf(stderr, "[vivid] Loaded graph: %s (%zu nodes, %zu connections)\n",
            path, nodes_.size(), connections_.size());
    }
    return ok;
}

bool Graph::load_from_string(const char* json, size_t len, bool preserve_source_path) {
    if (!preserve_source_path)
        source_path_.clear();

    if (len == 0) len = std::strlen(json);

    yyjson_read_err err;
    // yyjson_read_opts takes char* but does not mutate without YYJSON_READ_INSITU
    yyjson_doc* doc = yyjson_read_opts(const_cast<char*>(json), len, 0, nullptr, &err);
    if (!doc) {
        std::fprintf(stderr, "[vivid] Graph: failed to parse JSON string: %s\n", err.msg);
        return false;
    }

    bool ok = parse_doc(doc);
    yyjson_doc_free(doc);

    if (ok) {
        std::fprintf(stderr, "[vivid] Loaded graph from string (%zu nodes, %zu connections)\n",
            nodes_.size(), connections_.size());
    }
    return ok;
}

bool Graph::parse_doc(yyjson_doc* doc) {
    nodes_.clear();
    connections_.clear();
    midi_mappings_.clear();
    filters_.clear();
    variations_.clear();
    node_presets_.clear();
    state_preset_mappings_.clear();
    sticky_notes_.clear();
    load_diagnostics.clear();
    active_variation_ = -1;
    quantize_clock_node_.clear();

    yyjson_val* root = yyjson_doc_get_root(doc);

    // Schema version — hard-reject if from the future
    auto* sv_val = yyjson_obj_get(root, "schema_version");
    schema_version = sv_val ? static_cast<int>(yyjson_get_int(sv_val)) : 1;
    if (schema_version > GRAPH_SCHEMA_VERSION) {
        std::fprintf(stderr, "[vivid] Graph: schema_version %d > %d — refusing to load.\n",
                     schema_version, GRAPH_SCHEMA_VERSION);
        return false;
    }

    auto* vv_val = yyjson_obj_get(root, "vivid_version");
    if (vv_val && yyjson_is_str(vv_val))
        vivid_version = yyjson_get_str(vv_val);
    else
        vivid_version.clear();

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
                return false;
            }
            node.type = yyjson_get_str(type_val);

            // Optional package provenance
            auto* pkg_obj = yyjson_obj_get(val, "pkg");
            if (pkg_obj && yyjson_is_obj(pkg_obj)) {
                auto* pn = yyjson_obj_get(pkg_obj, "name");
                auto* pv = yyjson_obj_get(pkg_obj, "version");
                node.pkg_name    = (pn && yyjson_is_str(pn)) ? yyjson_get_str(pn) : "";
                node.pkg_version = (pv && yyjson_is_str(pv)) ? yyjson_get_str(pv) : "";
            }

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
                    if (node.tex_width > 8192 || node.tex_height > 8192) {
                        std::fprintf(stderr, "[vivid] Graph: node '%s' resolution %ux%u exceeds max (8192), ignoring\n",
                                     node.id.c_str(), node.tex_width, node.tex_height);
                        node.tex_width  = 0;
                        node.tex_height = 0;
                    }
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

            // Reject legacy embedded_ops (pre-role-binding format)
            if (yyjson_obj_get(val, "embedded_ops")) {
                std::fprintf(stderr,
                    "[vivid] Graph: node '%s' uses legacy embedded_ops format — "
                    "please recreate the graph with role bindings.\n", node.id.c_str());
                return false;
            }

            // Optional role bindings
            yyjson_val* rb_obj = yyjson_obj_get(val, "role_bindings");
            if (rb_obj && yyjson_is_obj(rb_obj)) {
                yyjson_obj_iter rbiter;
                yyjson_obj_iter_init(rb_obj, &rbiter);
                yyjson_val* rbkey;
                while ((rbkey = yyjson_obj_iter_next(&rbiter)) != nullptr) {
                    yyjson_val* rbval = yyjson_obj_iter_get_val(rbkey);
                    if (!rbval || !yyjson_is_obj(rbval)) continue;
                    NodeDef::RoleBindingState rbs;
                    yyjson_val* tid = yyjson_obj_get(rbval, "target_node_id");
                    if (tid && yyjson_is_str(tid))
                        rbs.target_node_id = yyjson_get_str(tid);
                    yyjson_val* tname = yyjson_obj_get(rbval, "target_output_name");
                    if (tname && yyjson_is_str(tname))
                        rbs.target_output_name = yyjson_get_str(tname);
                    node.role_bindings[yyjson_get_str(rbkey)] = std::move(rbs);
                }
            }

            if (find_node(node.id)) {
                std::fprintf(stderr, "[vivid] Graph: duplicate node id '%s', skipping\n", node.id.c_str());
                continue;
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
            if (!from_val || !yyjson_is_str(from_val) || !to_val || !yyjson_is_str(to_val)) {
                std::fprintf(stderr, "[vivid] Graph: connection 'from'/'to' must be strings, skipping\n");
                continue;
            }

            ConnectionDef conn;
            if (!split_address(yyjson_get_str(from_val), conn.from_node, conn.from_port) ||
                !split_address(yyjson_get_str(to_val),   conn.to_node,   conn.to_port)) {
                std::fprintf(stderr, "[vivid] Graph: invalid connection address\n");
                continue;
            }
            // Remap fields (new format)
            yyjson_val* fmin_val = yyjson_obj_get(val, "from_min");
            yyjson_val* fmax_val = yyjson_obj_get(val, "from_max");
            yyjson_val* tmin_val = yyjson_obj_get(val, "to_min");
            yyjson_val* tmax_val = yyjson_obj_get(val, "to_max");
            yyjson_val* clamp_val = yyjson_obj_get(val, "clamp");
            if (fmin_val || fmax_val || tmin_val || tmax_val) {
                if (fmin_val && yyjson_is_num(fmin_val))
                    conn.from_min = static_cast<float>(yyjson_get_num(fmin_val));
                if (fmax_val && yyjson_is_num(fmax_val))
                    conn.from_max = static_cast<float>(yyjson_get_num(fmax_val));
                if (tmin_val && yyjson_is_num(tmin_val))
                    conn.to_min = static_cast<float>(yyjson_get_num(tmin_val));
                if (tmax_val && yyjson_is_num(tmax_val))
                    conn.to_max = static_cast<float>(yyjson_get_num(tmax_val));
                if (clamp_val && yyjson_is_bool(clamp_val))
                    conn.clamp = yyjson_get_bool(clamp_val);
            } else {
                // Backward compat: legacy "scale" field -> remap {0, 1, 0, scale}
                yyjson_val* scale_val = yyjson_obj_get(val, "scale");
                if (scale_val && yyjson_is_num(scale_val))
                    conn.to_max = static_cast<float>(yyjson_get_num(scale_val));
            }
            // Deduplicate: skip if an identical connection already exists
            bool dup = false;
            for (const auto& c : connections_) {
                if (c.from_node == conn.from_node && c.from_port == conn.from_port &&
                    c.to_node   == conn.to_node   && c.to_port   == conn.to_port) {
                    dup = true; break;
                }
            }
            if (dup) {
                std::fprintf(stderr, "[vivid] Graph: duplicate connection %s/%s -> %s/%s, skipping\n",
                             conn.from_node.c_str(), conn.from_port.c_str(),
                             conn.to_node.c_str(),   conn.to_port.c_str());
                continue;
            }
            connections_.push_back(std::move(conn));
        }
    }

    // -----------------------------------------------------------------------
    // Port rename migration: "phase" -> "beat_phase" for operators that
    // renamed their input port in the unified triggering convention.
    // -----------------------------------------------------------------------
    {
        static const struct { const char* type; const char* old_port; const char* new_port; } kPortRenames[] = {
            {"Envelope",       "phase", "beat_phase"},
            {"LFO",            "phase", "beat_phase"},
            {"Sequencer",      "phase", "beat_phase"},
            {"DrumSequencer",  "phase", "beat_phase"},
        };

        // Build node-id -> type lookup
        std::unordered_map<std::string, std::string> node_type_map;
        for (const auto& n : nodes_)
            node_type_map[n.id] = n.type;

        for (auto& conn : connections_) {
            // Check to_port (input side)
            auto it = node_type_map.find(conn.to_node);
            if (it != node_type_map.end()) {
                for (const auto& r : kPortRenames) {
                    if (it->second == r.type && conn.to_port == r.old_port) {
                        std::fprintf(stderr, "[vivid] Graph migration: renamed port %s/%s -> %s/%s\n",
                                     conn.to_node.c_str(), r.old_port, conn.to_node.c_str(), r.new_port);
                        conn.to_port = r.new_port;
                        break;
                    }
                }
            }
            // Check from_port (output side)
            auto it2 = node_type_map.find(conn.from_node);
            if (it2 != node_type_map.end()) {
                for (const auto& r : kPortRenames) {
                    if (it2->second == r.type && conn.from_port == r.old_port) {
                        std::fprintf(stderr, "[vivid] Graph migration: renamed port %s/%s -> %s/%s\n",
                                     conn.from_node.c_str(), r.old_port, conn.from_node.c_str(), r.new_port);
                        conn.from_port = r.new_port;
                        break;
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Drum phase param removal migration: connections targeting the removed
    // "phase" param on drum operators are dropped with a warning.
    // Users should wire through PhaseToMidi -> midi_in instead.
    // -----------------------------------------------------------------------
    {
        static const char* kDrumTypes[] = {
            "DrumKick", "DrumSnare", "DrumHiHat", "DrumClap", "DrumCymbal", "DrumTom"
        };

        std::unordered_map<std::string, std::string> node_type_map;
        for (const auto& n : nodes_)
            node_type_map[n.id] = n.type;

        connections_.erase(
            std::remove_if(connections_.begin(), connections_.end(),
                [&](const ConnectionDef& conn) {
                    auto it = node_type_map.find(conn.to_node);
                    if (it == node_type_map.end() || conn.to_port != "phase")
                        return false;
                    for (const char* dt : kDrumTypes) {
                        if (it->second == dt) {
                            std::fprintf(stderr,
                                "[vivid] Graph migration: removed connection to %s/%s "
                                "(phase param removed — use PhaseToMidi -> midi_in)\n",
                                conn.to_node.c_str(), conn.to_port.c_str());
                            return true;
                        }
                    }
                    return false;
                }),
            connections_.end());
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
            if (mm.cc_number < 0 || mm.cc_number > 127) {
                std::fprintf(stderr, "[vivid] Graph: MIDI cc %d out of range [0,127], skipping\n", mm.cc_number);
                continue;
            }
            if (mm.channel < 0 || mm.channel > 16) {
                std::fprintf(stderr, "[vivid] Graph: MIDI channel %d out of range [0,16], skipping\n", mm.channel);
                continue;
            }
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
                        yyjson_obj_iter piter;
                        yyjson_obj_iter_init(nval, &piter);
                        yyjson_val* pkey;
                        while ((pkey = yyjson_obj_iter_next(&piter)) != nullptr) {
                            yyjson_val* pv = yyjson_obj_iter_get_val(pkey);
                            if (pv && yyjson_is_num(pv))
                                vd.params[node_id][yyjson_get_str(pkey)] = static_cast<float>(yyjson_get_num(pv));
                            else if (pv && yyjson_is_str(pv))
                                vd.string_params[node_id][yyjson_get_str(pkey)] = yyjson_get_str(pv);
                        }
                    }
                }
            }
            variations_.push_back(std::move(vd));
        }
    }

    // Parse active_variation
    yyjson_val* av_val = yyjson_obj_get(root, "active_variation");
    if (av_val && yyjson_is_int(av_val)) {
        active_variation_ = static_cast<int>(yyjson_get_int(av_val));
        if (active_variation_ >= static_cast<int>(variations_.size())) {
            std::fprintf(stderr, "[vivid] Graph: active_variation %d out of bounds (%zu variations), resetting to -1\n",
                         active_variation_, variations_.size());
            active_variation_ = -1;
        }
    }

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
                            else if (ppv && yyjson_is_str(ppv))
                                op.string_params[yyjson_get_str(ppkey)] = yyjson_get_str(ppv);
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

    // Parse sticky notes
    yyjson_val* sticky_arr = yyjson_obj_get(root, "sticky_notes");
    if (sticky_arr && yyjson_is_arr(sticky_arr)) {
        size_t snidx, snmax;
        yyjson_val* snval;
        yyjson_arr_foreach(sticky_arr, snidx, snmax, snval) {
            StickyNoteDef sn;
            yyjson_val* sn_id = yyjson_obj_get(snval, "id");
            if (sn_id && yyjson_is_str(sn_id))
                sn.id = yyjson_get_str(sn_id);
            yyjson_val* sn_text = yyjson_obj_get(snval, "text");
            if (sn_text && yyjson_is_str(sn_text))
                sn.text = yyjson_get_str(sn_text);
            yyjson_val* sn_x = yyjson_obj_get(snval, "x");
            if (sn_x && yyjson_is_num(sn_x))
                sn.x = static_cast<float>(yyjson_get_num(sn_x));
            yyjson_val* sn_y = yyjson_obj_get(snval, "y");
            if (sn_y && yyjson_is_num(sn_y))
                sn.y = static_cast<float>(yyjson_get_num(sn_y));
            yyjson_val* sn_w = yyjson_obj_get(snval, "width");
            if (sn_w && yyjson_is_num(sn_w))
                sn.width = static_cast<float>(yyjson_get_num(sn_w));
            yyjson_val* sn_h = yyjson_obj_get(snval, "height");
            if (sn_h && yyjson_is_num(sn_h))
                sn.height = static_cast<float>(yyjson_get_num(sn_h));
            yyjson_val* sn_color = yyjson_obj_get(snval, "color");
            if (sn_color && yyjson_is_int(sn_color))
                sn.color = static_cast<int>(yyjson_get_int(sn_color));
            sticky_notes_.push_back(std::move(sn));
        }
    }

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
    // Remove per-operator presets for this node
    node_presets_.erase(id);
    // Strip this node from each variation's param maps
    for (auto& v : variations_) {
        v.params.erase(id);
        v.string_params.erase(id);
    }
    // Clean up state-preset mappings referencing this node
    // Remove mappings where this node IS the state machine
    state_preset_mappings_.erase(
        std::remove_if(state_preset_mappings_.begin(), state_preset_mappings_.end(),
            [&](const StatePresetMapping& m) { return m.state_machine_node == id; }),
        state_preset_mappings_.end());
    // For remaining mappings, erase this node from each state's target map
    for (auto& m : state_preset_mappings_) {
        for (auto& bindings : m.state_presets) {
            bindings.erase(id);
        }
    }
    // Clear role bindings referencing the removed node
    for (auto& remaining : nodes_) {
        for (auto it = remaining.role_bindings.begin(); it != remaining.role_bindings.end(); ) {
            if (it->second.target_node_id == id)
                it = remaining.role_bindings.erase(it);
            else
                ++it;
        }
    }
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

bool Graph::set_connection_remap(const std::string& from_node, const std::string& from_port,
                                  const std::string& to_node, const std::string& to_port,
                                  float from_min, float from_max, float to_min, float to_max, bool clamp) {
    for (auto& c : connections_) {
        if (c.from_node == from_node && c.from_port == from_port &&
            c.to_node == to_node && c.to_port == to_port) {
            c.from_min = from_min;
            c.from_max = from_max;
            c.to_min   = to_min;
            c.to_max   = to_max;
            c.clamp    = clamp;
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

bool Graph::duplicate_variation(const std::string& name, const std::string& new_name) {
    if (find_variation(new_name)) return false; // name conflict
    int src_idx = find_variation_index(name);
    if (src_idx < 0) return false;
    // Deep copy the source variation
    VariationDef copy = variations_[src_idx];
    copy.name = new_name;
    // Insert after source
    variations_.insert(variations_.begin() + src_idx + 1, std::move(copy));
    // Adjust active_variation_ if it's after the insertion point
    if (active_variation_ > src_idx)
        active_variation_++;
    return true;
}

bool Graph::move_variation(const std::string& name, int to_index) {
    int from_idx = find_variation_index(name);
    if (from_idx < 0) return false;
    int n = static_cast<int>(variations_.size());
    if (to_index < 0 || to_index >= n) return false;
    if (from_idx == to_index) return true; // no-op
    // Extract, erase, re-insert
    VariationDef tmp = std::move(variations_[from_idx]);
    variations_.erase(variations_.begin() + from_idx);
    variations_.insert(variations_.begin() + to_index, std::move(tmp));
    // Adjust active_variation_ to track the same variation
    if (active_variation_ == from_idx) {
        active_variation_ = to_index;
    } else if (from_idx < active_variation_ && to_index >= active_variation_) {
        active_variation_--;
    } else if (from_idx > active_variation_ && to_index <= active_variation_) {
        active_variation_++;
    }
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

// --- Sticky Note CRUD ---

void Graph::add_sticky_note(StickyNoteDef note) {
    // Replace if already exists
    for (auto& sn : sticky_notes_) {
        if (sn.id == note.id) {
            sn = std::move(note);
            return;
        }
    }
    sticky_notes_.push_back(std::move(note));
}

bool Graph::remove_sticky_note(const std::string& id) {
    auto it = std::find_if(sticky_notes_.begin(), sticky_notes_.end(),
        [&](const StickyNoteDef& sn) { return sn.id == id; });
    if (it == sticky_notes_.end()) return false;
    sticky_notes_.erase(it);
    return true;
}

const StickyNoteDef* Graph::find_sticky_note(const std::string& id) const {
    for (const auto& sn : sticky_notes_) {
        if (sn.id == id) return &sn;
    }
    return nullptr;
}

StickyNoteDef* Graph::find_sticky_note(const std::string& id) {
    for (auto& sn : sticky_notes_) {
        if (sn.id == id) return &sn;
    }
    return nullptr;
}

// --- Serialization ---

static yyjson_mut_doc* build_graph_json_doc(const Graph& graph) {
    yyjson_mut_doc* doc = yyjson_mut_doc_new(nullptr);
    yyjson_mut_val* root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);

    // Schema metadata
    yyjson_mut_obj_add_int(doc, root, "schema_version", GRAPH_SCHEMA_VERSION);
    yyjson_mut_obj_add_str(doc, root, "vivid_version", VIVID_CORE_VERSION);

    // Filters
    if (!graph.filters().empty()) {
        yyjson_mut_val* filters_obj = yyjson_mut_obj(doc);
        for (const auto& fd : graph.filters()) {
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
    for (const auto& node : graph.nodes()) {
        yyjson_mut_val* node_obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, node_obj, "type", node.type.c_str());

        if (!node.pkg_name.empty()) {
            auto* pkg_sub = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_strcpy(doc, pkg_sub, "name",    node.pkg_name.c_str());
            yyjson_mut_obj_add_strcpy(doc, pkg_sub, "version", node.pkg_version.c_str());
            yyjson_mut_obj_add_val(doc, node_obj, "pkg", pkg_sub);
        }

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

        if (!node.role_bindings.empty()) {
            yyjson_mut_val* rb_obj = yyjson_mut_obj(doc);
            for (const auto& [role_id, rbs] : node.role_bindings) {
                yyjson_mut_val* rb_entry = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_strcpy(doc, rb_entry, "target_node_id", rbs.target_node_id.c_str());
                yyjson_mut_obj_add_strcpy(doc, rb_entry, "target_output_name", rbs.target_output_name.c_str());
                yyjson_mut_obj_add_val(doc, rb_obj, role_id.c_str(), rb_entry);
            }
            yyjson_mut_obj_add_val(doc, node_obj, "role_bindings", rb_obj);
        }

        yyjson_mut_obj_add_val(doc, nodes_obj, node.id.c_str(), node_obj);
    }
    yyjson_mut_obj_add_val(doc, root, "nodes", nodes_obj);

    // Connections
    yyjson_mut_val* conns_arr = yyjson_mut_arr(doc);
    for (const auto& conn : graph.connections()) {
        yyjson_mut_val* conn_obj = yyjson_mut_obj(doc);
        std::string from_addr = conn.from_node + "/" + conn.from_port;
        std::string to_addr = conn.to_node + "/" + conn.to_port;
        yyjson_mut_obj_add_strcpy(doc, conn_obj, "from", from_addr.c_str());
        yyjson_mut_obj_add_strcpy(doc, conn_obj, "to", to_addr.c_str());
        if (conn.has_remap()) {
            yyjson_mut_obj_add_real(doc, conn_obj, "from_min", static_cast<double>(conn.from_min));
            yyjson_mut_obj_add_real(doc, conn_obj, "from_max", static_cast<double>(conn.from_max));
            yyjson_mut_obj_add_real(doc, conn_obj, "to_min",   static_cast<double>(conn.to_min));
            yyjson_mut_obj_add_real(doc, conn_obj, "to_max",   static_cast<double>(conn.to_max));
            if (conn.clamp)
                yyjson_mut_obj_add_bool(doc, conn_obj, "clamp", true);
        }
        yyjson_mut_arr_add_val(conns_arr, conn_obj);
    }
    yyjson_mut_obj_add_val(doc, root, "connections", conns_arr);

    // MIDI mappings
    if (!graph.midi_mappings().empty()) {
        yyjson_mut_val* midi_arr = yyjson_mut_arr(doc);
        for (const auto& mm : graph.midi_mappings()) {
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
    if (!graph.variations().empty()) {
        yyjson_mut_val* var_arr = yyjson_mut_arr(doc);
        for (const auto& vd : graph.variations()) {
            yyjson_mut_val* v_obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_strcpy(doc, v_obj, "name", vd.name.c_str());
            yyjson_mut_val* params_obj = yyjson_mut_obj(doc);
            // Write float params (node_id refs stable in vd.params)
            for (const auto& [node_id, pm] : vd.params) {
                yyjson_mut_val* node_obj = yyjson_mut_obj(doc);
                for (const auto& [pname, pval] : pm) {
                    yyjson_mut_obj_add_real(doc, node_obj, pname.c_str(),
                                            static_cast<double>(pval));
                }
                // Also add string params for this node if present
                auto sit = vd.string_params.find(node_id);
                if (sit != vd.string_params.end()) {
                    for (const auto& [pname, pval] : sit->second) {
                        yyjson_mut_obj_add_strcpy(doc, node_obj, pname.c_str(), pval.c_str());
                    }
                }
                yyjson_mut_obj_add_val(doc, params_obj, node_id.c_str(), node_obj);
            }
            // Write nodes that only have string params (not in vd.params)
            for (const auto& [node_id, spm] : vd.string_params) {
                if (vd.params.count(node_id)) continue;  // already handled above
                yyjson_mut_val* node_obj = yyjson_mut_obj(doc);
                for (const auto& [pname, pval] : spm) {
                    yyjson_mut_obj_add_strcpy(doc, node_obj, pname.c_str(), pval.c_str());
                }
                yyjson_mut_obj_add_val(doc, params_obj, node_id.c_str(), node_obj);
            }
            yyjson_mut_obj_add_val(doc, v_obj, "params", params_obj);
            yyjson_mut_arr_add_val(var_arr, v_obj);
        }
        yyjson_mut_obj_add_val(doc, root, "variations", var_arr);
    }
    if (graph.active_variation() >= 0)
        yyjson_mut_obj_add_int(doc, root, "active_variation", graph.active_variation());
    if (!graph.quantize_clock_node().empty())
        yyjson_mut_obj_add_strcpy(doc, root, "quantize_clock", graph.quantize_clock_node().c_str());

    // Per-operator presets
    if (!graph.node_presets().empty()) {
        yyjson_mut_val* presets_obj = yyjson_mut_obj(doc);
        for (const auto& [node_id, presets] : graph.node_presets()) {
            yyjson_mut_val* pr_arr = yyjson_mut_arr(doc);
            for (const auto& p : presets) {
                yyjson_mut_val* pr_obj = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_strcpy(doc, pr_obj, "name", p.name.c_str());
                yyjson_mut_val* pp_obj = yyjson_mut_obj(doc);
                for (const auto& [pname, pval] : p.params) {
                    yyjson_mut_obj_add_real(doc, pp_obj, pname.c_str(),
                                            static_cast<double>(pval));
                }
                for (const auto& [pname, pval] : p.string_params) {
                    yyjson_mut_obj_add_strcpy(doc, pp_obj, pname.c_str(), pval.c_str());
                }
                yyjson_mut_obj_add_val(doc, pr_obj, "params", pp_obj);
                yyjson_mut_arr_add_val(pr_arr, pr_obj);
            }
            yyjson_mut_obj_add_val(doc, presets_obj, node_id.c_str(), pr_arr);
        }
        yyjson_mut_obj_add_val(doc, root, "presets", presets_obj);
    }

    // State-preset mappings
    if (!graph.state_preset_mappings().empty()) {
        yyjson_mut_val* spm_arr = yyjson_mut_arr(doc);
        for (const auto& spm : graph.state_preset_mappings()) {
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
    if (graph.has_viewport()) {
        yyjson_mut_val* vp_obj = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_real(doc, vp_obj, "pan_x", static_cast<double>(graph.viewport_pan_x));
        yyjson_mut_obj_add_real(doc, vp_obj, "pan_y", static_cast<double>(graph.viewport_pan_y));
        yyjson_mut_obj_add_real(doc, vp_obj, "zoom",  static_cast<double>(graph.viewport_zoom));
        yyjson_mut_obj_add_val(doc, root, "viewport", vp_obj);
    }

    // Sticky notes
    if (!graph.sticky_notes().empty()) {
        yyjson_mut_val* sn_arr = yyjson_mut_arr(doc);
        for (const auto& sn : graph.sticky_notes()) {
            yyjson_mut_val* sn_obj = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_strcpy(doc, sn_obj, "id", sn.id.c_str());
            yyjson_mut_obj_add_strcpy(doc, sn_obj, "text", sn.text.c_str());
            yyjson_mut_obj_add_real(doc, sn_obj, "x", static_cast<double>(sn.x));
            yyjson_mut_obj_add_real(doc, sn_obj, "y", static_cast<double>(sn.y));
            yyjson_mut_obj_add_real(doc, sn_obj, "width", static_cast<double>(sn.width));
            yyjson_mut_obj_add_real(doc, sn_obj, "height", static_cast<double>(sn.height));
            yyjson_mut_obj_add_int(doc, sn_obj, "color", sn.color);
            yyjson_mut_arr_add_val(sn_arr, sn_obj);
        }
        yyjson_mut_obj_add_val(doc, root, "sticky_notes", sn_arr);
    }

    return doc;
}

bool Graph::save(const char* path) const {
    yyjson_mut_doc* doc = build_graph_json_doc(*this);

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

bool Graph::save_to_string(std::string& out_json) const {
    yyjson_mut_doc* doc = build_graph_json_doc(*this);
    size_t len = 0;
    char* json = yyjson_mut_write(doc, YYJSON_WRITE_PRETTY | YYJSON_WRITE_NEWLINE_AT_END, &len);
    yyjson_mut_doc_free(doc);

    if (!json) {
        std::fprintf(stderr, "[vivid] Graph: failed to write JSON to string\n");
        return false;
    }

    out_json.assign(json, len);
    std::free(json);
    return true;
}

} // namespace vivid
