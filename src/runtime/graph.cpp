#include "runtime/graph.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>

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
    try {
        std::ifstream ifs(path);
        if (!ifs) {
            std::fprintf(stderr, "[vivid] Graph: failed to read %s: could not open file\n", path);
            return false;
        }
        auto root = nlohmann::json::parse(ifs);
        bool ok = parse_doc(root);
        if (ok) {
            std::fprintf(stderr, "[vivid] Loaded graph: %s (%zu nodes, %zu connections)\n",
                path, nodes_.size(), connections_.size());
        }
        return ok;
    } catch (const nlohmann::json::parse_error& e) {
        std::fprintf(stderr, "[vivid] Graph: failed to read %s: %s\n", path, e.what());
        return false;
    }
}

bool Graph::load_from_string(const char* json, size_t len, bool preserve_source_path) {
    if (!preserve_source_path)
        source_path_.clear();

    if (len == 0) len = std::strlen(json);

    try {
        auto root = nlohmann::json::parse(json, json + len);
        bool ok = parse_doc(root);
        if (ok) {
            std::fprintf(stderr, "[vivid] Loaded graph from string (%zu nodes, %zu connections)\n",
                nodes_.size(), connections_.size());
        }
        return ok;
    } catch (const nlohmann::json::parse_error& e) {
        std::fprintf(stderr, "[vivid] Graph: failed to parse JSON string: %s\n", e.what());
        return false;
    }
}

// Parse common NodeDef fields from a JSON object. Does NOT set node.id (caller's job).
static bool parse_node_fields(const nlohmann::json& val, NodeDef& node) {
    // type
    auto type_it = val.find("type");
    if (type_it != val.end() && type_it->is_string())
        node.type = type_it->get<std::string>();

    // pkg
    auto pkg_it = val.find("pkg");
    if (pkg_it != val.end() && pkg_it->is_object()) {
        auto pn = pkg_it->find("name");
        auto pv = pkg_it->find("version");
        node.pkg_name    = (pn != pkg_it->end() && pn->is_string()) ? pn->get<std::string>() : "";
        node.pkg_version = (pv != pkg_it->end() && pv->is_string()) ? pv->get<std::string>() : "";
    }

    // params
    auto params_it = val.find("params");
    if (params_it != val.end() && params_it->is_object()) {
        for (auto& [pkey, pval] : params_it->items()) {
            if (pval.is_number())
                node.params[pkey] = static_cast<float>(pval.get<double>());
            else if (pval.is_string())
                node.string_params[pkey] = pval.get<std::string>();
        }
    }

    // layout
    auto layout_it = val.find("layout");
    if (layout_it != val.end() && layout_it->is_object()) {
        auto lx = layout_it->find("x");
        auto ly = layout_it->find("y");
        if (lx != layout_it->end() && lx->is_number() && ly != layout_it->end() && ly->is_number()) {
            node.layout_x = static_cast<float>(lx->get<double>());
            node.layout_y = static_cast<float>(ly->get<double>());
        }
    }

    // resolution
    auto res_it = val.find("resolution");
    if (res_it != val.end() && res_it->is_array() && res_it->size() == 2) {
        const auto& rw = (*res_it)[0];
        const auto& rh = (*res_it)[1];
        if (rw.is_number_integer() && rh.is_number_integer()) {
            node.tex_width  = static_cast<uint32_t>(rw.get<int64_t>());
            node.tex_height = static_cast<uint32_t>(rh.get<int64_t>());
            if (node.tex_width > 8192 || node.tex_height > 8192) {
                node.tex_width = 0;
                node.tex_height = 0;
            }
        }
    }

    // "cadence" key silently ignored for backward compat with old graphs.

    // locks
    auto locks_it = val.find("locks");
    if (locks_it != val.end() && locks_it->is_object()) {
        for (auto& [lkey, lval] : locks_it->items()) {
            if (lval.is_number_integer())
                node.param_lock_flags[lkey] = static_cast<uint8_t>(lval.get<int64_t>());
        }
    }

    // Subgraph module membership (present in flattened graphs)
    auto sg_owner_it = val.find("subgraph_owner");
    if (sg_owner_it != val.end() && sg_owner_it->is_string())
        node.subgraph_owner = sg_owner_it->get<std::string>();
    auto sg_type_it = val.find("subgraph_type");
    if (sg_type_it != val.end() && sg_type_it->is_string())
        node.subgraph_type = sg_type_it->get<std::string>();

    return true;
}

bool Graph::parse_doc(const nlohmann::json& root) {
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

    // Schema version — hard-reject if from the future
    auto sv_it = root.find("schema_version");
    schema_version = (sv_it != root.end() && sv_it->is_number_integer()) ? static_cast<int>(sv_it->get<int64_t>()) : 1;
    if (schema_version > GRAPH_SCHEMA_VERSION) {
        std::fprintf(stderr, "[vivid] Graph: schema_version %d > %d — refusing to load.\n",
                     schema_version, GRAPH_SCHEMA_VERSION);
        return false;
    }

    auto vv_it = root.find("vivid_version");
    if (vv_it != root.end() && vv_it->is_string())
        vivid_version = vv_it->get<std::string>();
    else
        vivid_version.clear();

    // Parse filters (before nodes, since nodes may reference user filter types)
    auto filters_it = root.find("filters");
    if (filters_it != root.end() && filters_it->is_object()) {
        for (auto& [fkey, fval] : filters_it->items()) {
            FilterDef fd;
            fd.name = fkey;

            auto src_it = fval.find("source");
            if (src_it != fval.end() && src_it->is_string())
                fd.source = src_it->get<std::string>();

            auto td_it = fval.find("time_dependent");
            if (td_it != fval.end() && td_it->is_boolean())
                fd.time_dependent = td_it->get<bool>();

            auto shader_it = fval.find("shader");
            if (shader_it != fval.end() && shader_it->is_string())
                fd.shader = shader_it->get<std::string>();

            auto params_it = fval.find("params");
            if (params_it != fval.end() && params_it->is_array()) {
                for (auto& pval : *params_it) {
                    FilterDef::ParamDef pd;
                    auto pn = pval.find("name");
                    if (pn != pval.end() && pn->is_string()) pd.name = pn->get<std::string>();
                    auto pdef = pval.find("default");
                    if (pdef != pval.end() && pdef->is_number()) pd.default_value = static_cast<float>(pdef->get<double>());
                    auto pmin = pval.find("min");
                    if (pmin != pval.end() && pmin->is_number()) pd.min_value = static_cast<float>(pmin->get<double>());
                    auto pmax_v = pval.find("max");
                    if (pmax_v != pval.end() && pmax_v->is_number()) pd.max_value = static_cast<float>(pmax_v->get<double>());
                    fd.params.push_back(std::move(pd));
                }
            }

            std::fprintf(stderr, "[vivid] Graph: loaded filter '%s' (source=%s, %zu params)\n",
                         fd.name.c_str(), fd.source.c_str(), fd.params.size());
            filters_.push_back(std::move(fd));
        }
    }

    // Parse nodes
    auto nodes_it = root.find("nodes");
    if (nodes_it != root.end() && nodes_it->is_object()) {
        for (auto& [key, val] : nodes_it->items()) {
            NodeDef node;
            node.id = key;

            auto type_it = val.find("type");
            if (type_it == val.end() || !type_it->is_string()) {
                std::fprintf(stderr, "[vivid] Graph: node '%s' missing type\n", node.id.c_str());
                return false;
            }

            parse_node_fields(val, node);

            if (find_node(node.id)) {
                std::fprintf(stderr, "[vivid] Graph: duplicate node id '%s', skipping\n", node.id.c_str());
                continue;
            }
            nodes_.push_back(std::move(node));
        }
    }

    // Parse connections
    auto conns_it = root.find("connections");
    if (conns_it != root.end() && conns_it->is_array()) {
        for (auto& val : *conns_it) {
            auto from_it = val.find("from");
            auto to_it   = val.find("to");
            if (from_it == val.end() || !from_it->is_string() || to_it == val.end() || !to_it->is_string()) {
                std::fprintf(stderr, "[vivid] Graph: connection 'from'/'to' must be strings, skipping\n");
                continue;
            }

            ConnectionDef conn;
            if (!split_address(from_it->get<std::string>().c_str(), conn.from_node, conn.from_port) ||
                !split_address(to_it->get<std::string>().c_str(),   conn.to_node,   conn.to_port)) {
                std::fprintf(stderr, "[vivid] Graph: invalid connection address\n");
                continue;
            }
            // Remap fields (new format)
            auto fmin_it = val.find("from_min");
            auto fmax_it = val.find("from_max");
            auto tmin_it = val.find("to_min");
            auto tmax_it = val.find("to_max");
            auto clamp_it = val.find("clamp");
            if (fmin_it != val.end() || fmax_it != val.end() || tmin_it != val.end() || tmax_it != val.end()) {
                if (fmin_it != val.end() && fmin_it->is_number())
                    conn.from_min = static_cast<float>(fmin_it->get<double>());
                if (fmax_it != val.end() && fmax_it->is_number())
                    conn.from_max = static_cast<float>(fmax_it->get<double>());
                if (tmin_it != val.end() && tmin_it->is_number())
                    conn.to_min = static_cast<float>(tmin_it->get<double>());
                if (tmax_it != val.end() && tmax_it->is_number())
                    conn.to_max = static_cast<float>(tmax_it->get<double>());
                if (clamp_it != val.end() && clamp_it->is_boolean())
                    conn.clamp = clamp_it->get<bool>();
            }
            auto bridge_it = val.find("bridge");
            if (bridge_it != val.end() && bridge_it->is_string())
                conn.bridge = bridge_it->get<std::string>();
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


    // Parse MIDI mappings
    auto midi_it = root.find("midi_mappings");
    if (midi_it != root.end() && midi_it->is_array()) {
        for (auto& mval : *midi_it) {
            MidiMappingDef mm;
            auto node_it = mval.find("node");
            auto param_it = mval.find("param");
            auto cc_it = mval.find("cc");
            if (node_it == mval.end() || param_it == mval.end() || cc_it == mval.end()) continue;
            mm.node_id = node_it->get<std::string>();
            mm.param_name = param_it->get<std::string>();
            mm.cc_number = static_cast<int>(cc_it->get<int64_t>());
            auto chan_it = mval.find("channel");
            if (chan_it != mval.end() && chan_it->is_number_integer())
                mm.channel = static_cast<int>(chan_it->get<int64_t>());
            if (mm.cc_number < 0 || mm.cc_number > 127) {
                std::fprintf(stderr, "[vivid] Graph: MIDI cc %d out of range [0,127], skipping\n", mm.cc_number);
                continue;
            }
            if (mm.channel < 0 || mm.channel > 16) {
                std::fprintf(stderr, "[vivid] Graph: MIDI channel %d out of range [0,16], skipping\n", mm.channel);
                continue;
            }
            auto rmin_it = mval.find("range_min");
            if (rmin_it != mval.end() && rmin_it->is_number())
                mm.range_min = static_cast<float>(rmin_it->get<double>());
            auto rmax_it = mval.find("range_max");
            if (rmax_it != mval.end() && rmax_it->is_number())
                mm.range_max = static_cast<float>(rmax_it->get<double>());
            midi_mappings_.push_back(std::move(mm));
        }
    }

    // Parse viewport
    auto vp_it = root.find("viewport");
    if (vp_it != root.end() && vp_it->is_object()) {
        auto vpx = vp_it->find("pan_x");
        auto vpy = vp_it->find("pan_y");
        auto vpz = vp_it->find("zoom");
        if (vpx != vp_it->end() && vpx->is_number() && vpy != vp_it->end() && vpy->is_number() && vpz != vp_it->end() && vpz->is_number()) {
            viewport_pan_x = static_cast<float>(vpx->get<double>());
            viewport_pan_y = static_cast<float>(vpy->get<double>());
            viewport_zoom  = static_cast<float>(vpz->get<double>());
        }
    }

    // Parse variations
    auto var_it = root.find("variations");
    if (var_it != root.end() && var_it->is_array()) {
        for (auto& vval : *var_it) {
            VariationDef vd;
            auto vname = vval.find("name");
            if (vname != vval.end() && vname->is_string())
                vd.name = vname->get<std::string>();
            auto vparams = vval.find("params");
            if (vparams != vval.end() && vparams->is_object()) {
                for (auto& [node_id, nval] : vparams->items()) {
                    if (nval.is_object()) {
                        for (auto& [pkey, pv] : nval.items()) {
                            if (pv.is_number())
                                vd.params[node_id][pkey] = static_cast<float>(pv.get<double>());
                            else if (pv.is_string())
                                vd.string_params[node_id][pkey] = pv.get<std::string>();
                        }
                    }
                }
            }
            variations_.push_back(std::move(vd));
        }
    }

    // Parse active_variation
    auto av_it = root.find("active_variation");
    if (av_it != root.end() && av_it->is_number_integer()) {
        active_variation_ = static_cast<int>(av_it->get<int64_t>());
        if (active_variation_ >= static_cast<int>(variations_.size())) {
            std::fprintf(stderr, "[vivid] Graph: active_variation %d out of bounds (%zu variations), resetting to -1\n",
                         active_variation_, variations_.size());
            active_variation_ = -1;
        }
    }

    // Parse quantize_clock
    auto qc_it = root.find("quantize_clock");
    if (qc_it != root.end() && qc_it->is_string())
        quantize_clock_node_ = qc_it->get<std::string>();

    // Parse per-operator presets
    auto presets_it = root.find("presets");
    if (presets_it != root.end() && presets_it->is_object()) {
        for (auto& [node_id, prval] : presets_it->items()) {
            if (prval.is_array()) {
                auto& presets = node_presets_[node_id];
                for (auto& pentry : prval) {
                    OperatorPreset op;
                    auto pn = pentry.find("name");
                    if (pn != pentry.end() && pn->is_string())
                        op.name = pn->get<std::string>();
                    auto pp = pentry.find("params");
                    if (pp != pentry.end() && pp->is_object()) {
                        for (auto& [ppkey, ppv] : pp->items()) {
                            if (ppv.is_number())
                                op.params[ppkey] = static_cast<float>(ppv.get<double>());
                            else if (ppv.is_string())
                                op.string_params[ppkey] = ppv.get<std::string>();
                        }
                    }
                    presets.push_back(std::move(op));
                }
            }
        }
    }

    // Parse state_preset_mappings
    auto spm_it = root.find("state_preset_mappings");
    if (spm_it != root.end() && spm_it->is_array()) {
        for (auto& sentry : *spm_it) {
            StatePresetMapping spm;
            auto sn = sentry.find("node");
            if (sn != sentry.end() && sn->is_string())
                spm.state_machine_node = sn->get<std::string>();
            auto states_it = sentry.find("states");
            if (states_it != sentry.end() && states_it->is_array()) {
                for (auto& state_obj : *states_it) {
                    std::unordered_map<std::string, std::string> bindings;
                    if (state_obj.is_object()) {
                        for (auto& [bkey, bval] : state_obj.items()) {
                            if (bval.is_string())
                                bindings[bkey] = bval.get<std::string>();
                        }
                    }
                    spm.state_presets.push_back(std::move(bindings));
                }
            }
            state_preset_mappings_.push_back(std::move(spm));
        }
    }

    // Parse sticky notes
    auto sticky_it = root.find("sticky_notes");
    if (sticky_it != root.end() && sticky_it->is_array()) {
        for (auto& snval : *sticky_it) {
            StickyNoteDef sn;
            auto sn_id = snval.find("id");
            if (sn_id != snval.end() && sn_id->is_string())
                sn.id = sn_id->get<std::string>();
            auto sn_text = snval.find("text");
            if (sn_text != snval.end() && sn_text->is_string())
                sn.text = sn_text->get<std::string>();
            auto sn_x = snval.find("x");
            if (sn_x != snval.end() && sn_x->is_number())
                sn.x = static_cast<float>(sn_x->get<double>());
            auto sn_y = snval.find("y");
            if (sn_y != snval.end() && sn_y->is_number())
                sn.y = static_cast<float>(sn_y->get<double>());
            auto sn_w = snval.find("width");
            if (sn_w != snval.end() && sn_w->is_number())
                sn.width = static_cast<float>(sn_w->get<double>());
            auto sn_h = snval.find("height");
            if (sn_h != snval.end() && sn_h->is_number())
                sn.height = static_cast<float>(sn_h->get<double>());
            auto sn_color = snval.find("color");
            if (sn_color != snval.end() && sn_color->is_number_integer())
                sn.color = static_cast<int>(sn_color->get<int64_t>());
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

bool Graph::set_connection_bridge(const std::string& from_node, const std::string& from_port,
                                   const std::string& to_node, const std::string& to_port,
                                   const std::string& bridge) {
    for (auto& c : connections_) {
        if (c.from_node == from_node && c.from_port == from_port &&
            c.to_node == to_node && c.to_port == to_port) {
            c.bridge = bridge;
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

static void serialize_node_fields(nlohmann::ordered_json& node_obj, const NodeDef& node) {
    node_obj["type"] = node.type;

    if (!node.pkg_name.empty()) {
        node_obj["pkg"] = nlohmann::ordered_json{{"name", node.pkg_name}, {"version", node.pkg_version}};
    }

    if (!node.params.empty() || !node.string_params.empty()) {
        nlohmann::ordered_json params_obj = nlohmann::ordered_json::object();
        for (const auto& [pname, pval] : node.params) {
            params_obj[pname] = static_cast<double>(pval);
        }
        for (const auto& [pname, pval] : node.string_params) {
            params_obj[pname] = pval;
        }
        node_obj["params"] = std::move(params_obj);
    }

    if (node.has_layout()) {
        node_obj["layout"] = nlohmann::ordered_json{{"x", static_cast<double>(node.layout_x)}, {"y", static_cast<double>(node.layout_y)}};
    }

    if (node.tex_width > 0 && node.tex_height > 0) {
        node_obj["resolution"] = nlohmann::ordered_json::array({static_cast<int64_t>(node.tex_width), static_cast<int64_t>(node.tex_height)});
    }

    if (!node.param_lock_flags.empty()) {
        nlohmann::ordered_json locks_obj = nlohmann::ordered_json::object();
        for (const auto& [pname, flags] : node.param_lock_flags)
            locks_obj[pname] = static_cast<int64_t>(flags);
        node_obj["locks"] = std::move(locks_obj);
    }

    // Subgraph module membership (only written for flattened graphs)
    if (!node.subgraph_owner.empty()) {
        node_obj["subgraph_owner"] = node.subgraph_owner;
        node_obj["subgraph_type"]  = node.subgraph_type;
    }
}

static nlohmann::ordered_json build_graph_json_doc(const Graph& graph) {
    nlohmann::ordered_json root;

    // Schema metadata
    root["schema_version"] = GRAPH_SCHEMA_VERSION;
    root["vivid_version"] = VIVID_CORE_VERSION;

    // Filters
    if (!graph.filters().empty()) {
        nlohmann::ordered_json filters_obj = nlohmann::ordered_json::object();
        for (const auto& fd : graph.filters()) {
            nlohmann::ordered_json f_obj = nlohmann::ordered_json::object();
            if (!fd.source.empty())
                f_obj["source"] = fd.source;
            if (fd.time_dependent)
                f_obj["time_dependent"] = true;

            if (!fd.params.empty()) {
                nlohmann::ordered_json p_arr = nlohmann::ordered_json::array();
                for (const auto& pd : fd.params) {
                    nlohmann::ordered_json p_obj = nlohmann::ordered_json::object();
                    p_obj["name"] = pd.name;
                    p_obj["default"] = static_cast<double>(pd.default_value);
                    p_obj["min"] = static_cast<double>(pd.min_value);
                    p_obj["max"] = static_cast<double>(pd.max_value);
                    p_arr.push_back(std::move(p_obj));
                }
                f_obj["params"] = std::move(p_arr);
            }

            if (!fd.shader.empty())
                f_obj["shader"] = fd.shader;

            filters_obj[fd.name] = std::move(f_obj);
        }
        root["filters"] = std::move(filters_obj);
    }

    // Nodes
    nlohmann::ordered_json nodes_obj = nlohmann::ordered_json::object();
    for (const auto& node : graph.nodes()) {
        nlohmann::ordered_json node_obj = nlohmann::ordered_json::object();
        serialize_node_fields(node_obj, node);
        nodes_obj[node.id] = std::move(node_obj);
    }
    root["nodes"] = std::move(nodes_obj);

    // Connections
    nlohmann::ordered_json conns_arr = nlohmann::ordered_json::array();
    for (const auto& conn : graph.connections()) {
        nlohmann::ordered_json conn_obj = nlohmann::ordered_json::object();
        std::string from_addr = conn.from_node + "/" + conn.from_port;
        std::string to_addr = conn.to_node + "/" + conn.to_port;
        conn_obj["from"] = from_addr;
        conn_obj["to"] = to_addr;
        if (conn.has_remap()) {
            conn_obj["from_min"] = static_cast<double>(conn.from_min);
            conn_obj["from_max"] = static_cast<double>(conn.from_max);
            conn_obj["to_min"]   = static_cast<double>(conn.to_min);
            conn_obj["to_max"]   = static_cast<double>(conn.to_max);
            if (conn.clamp)
                conn_obj["clamp"] = true;
        }
        if (conn.has_bridge())
            conn_obj["bridge"] = conn.bridge;
        conns_arr.push_back(std::move(conn_obj));
    }
    root["connections"] = std::move(conns_arr);

    // MIDI mappings
    if (!graph.midi_mappings().empty()) {
        nlohmann::ordered_json midi_arr = nlohmann::ordered_json::array();
        for (const auto& mm : graph.midi_mappings()) {
            nlohmann::ordered_json mm_obj = nlohmann::ordered_json::object();
            mm_obj["node"] = mm.node_id;
            mm_obj["param"] = mm.param_name;
            mm_obj["cc"] = mm.cc_number;
            if (mm.channel != 0)
                mm_obj["channel"] = mm.channel;
            mm_obj["range_min"] = static_cast<double>(mm.range_min);
            mm_obj["range_max"] = static_cast<double>(mm.range_max);
            midi_arr.push_back(std::move(mm_obj));
        }
        root["midi_mappings"] = std::move(midi_arr);
    }

    // Variations
    if (!graph.variations().empty()) {
        nlohmann::ordered_json var_arr = nlohmann::ordered_json::array();
        for (const auto& vd : graph.variations()) {
            nlohmann::ordered_json v_obj = nlohmann::ordered_json::object();
            v_obj["name"] = vd.name;
            nlohmann::ordered_json params_obj = nlohmann::ordered_json::object();
            // Write float params (node_id refs stable in vd.params)
            for (const auto& [node_id, pm] : vd.params) {
                nlohmann::ordered_json node_obj = nlohmann::ordered_json::object();
                for (const auto& [pname, pval] : pm) {
                    node_obj[pname] = static_cast<double>(pval);
                }
                // Also add string params for this node if present
                auto sit = vd.string_params.find(node_id);
                if (sit != vd.string_params.end()) {
                    for (const auto& [pname, pval] : sit->second) {
                        node_obj[pname] = pval;
                    }
                }
                params_obj[node_id] = std::move(node_obj);
            }
            // Write nodes that only have string params (not in vd.params)
            for (const auto& [node_id, spm] : vd.string_params) {
                if (vd.params.count(node_id)) continue;  // already handled above
                nlohmann::ordered_json node_obj = nlohmann::ordered_json::object();
                for (const auto& [pname, pval] : spm) {
                    node_obj[pname] = pval;
                }
                params_obj[node_id] = std::move(node_obj);
            }
            v_obj["params"] = std::move(params_obj);
            var_arr.push_back(std::move(v_obj));
        }
        root["variations"] = std::move(var_arr);
    }
    if (graph.active_variation() >= 0)
        root["active_variation"] = graph.active_variation();
    if (!graph.quantize_clock_node().empty())
        root["quantize_clock"] = graph.quantize_clock_node();

    // Per-operator presets
    if (!graph.node_presets().empty()) {
        nlohmann::ordered_json presets_obj = nlohmann::ordered_json::object();
        for (const auto& [node_id, presets] : graph.node_presets()) {
            nlohmann::ordered_json pr_arr = nlohmann::ordered_json::array();
            for (const auto& p : presets) {
                nlohmann::ordered_json pr_obj = nlohmann::ordered_json::object();
                pr_obj["name"] = p.name;
                nlohmann::ordered_json pp_obj = nlohmann::ordered_json::object();
                for (const auto& [pname, pval] : p.params) {
                    pp_obj[pname] = static_cast<double>(pval);
                }
                for (const auto& [pname, pval] : p.string_params) {
                    pp_obj[pname] = pval;
                }
                pr_obj["params"] = std::move(pp_obj);
                pr_arr.push_back(std::move(pr_obj));
            }
            presets_obj[node_id] = std::move(pr_arr);
        }
        root["presets"] = std::move(presets_obj);
    }

    // State-preset mappings
    if (!graph.state_preset_mappings().empty()) {
        nlohmann::ordered_json spm_arr = nlohmann::ordered_json::array();
        for (const auto& spm : graph.state_preset_mappings()) {
            nlohmann::ordered_json spm_obj = nlohmann::ordered_json::object();
            spm_obj["node"] = spm.state_machine_node;
            nlohmann::ordered_json states_arr = nlohmann::ordered_json::array();
            for (const auto& bindings : spm.state_presets) {
                nlohmann::ordered_json b_obj = nlohmann::ordered_json::object();
                for (const auto& [target_node, preset_name] : bindings) {
                    b_obj[target_node] = preset_name;
                }
                states_arr.push_back(std::move(b_obj));
            }
            spm_obj["states"] = std::move(states_arr);
            spm_arr.push_back(std::move(spm_obj));
        }
        root["state_preset_mappings"] = std::move(spm_arr);
    }

    // Viewport
    if (graph.has_viewport()) {
        nlohmann::ordered_json vp_obj = nlohmann::ordered_json::object();
        vp_obj["pan_x"] = static_cast<double>(graph.viewport_pan_x);
        vp_obj["pan_y"] = static_cast<double>(graph.viewport_pan_y);
        vp_obj["zoom"]  = static_cast<double>(graph.viewport_zoom);
        root["viewport"] = std::move(vp_obj);
    }

    // Sticky notes
    if (!graph.sticky_notes().empty()) {
        nlohmann::ordered_json sn_arr = nlohmann::ordered_json::array();
        for (const auto& sn : graph.sticky_notes()) {
            nlohmann::ordered_json sn_obj = nlohmann::ordered_json::object();
            sn_obj["id"] = sn.id;
            sn_obj["text"] = sn.text;
            sn_obj["x"] = static_cast<double>(sn.x);
            sn_obj["y"] = static_cast<double>(sn.y);
            sn_obj["width"] = static_cast<double>(sn.width);
            sn_obj["height"] = static_cast<double>(sn.height);
            sn_obj["color"] = sn.color;
            sn_arr.push_back(std::move(sn_obj));
        }
        root["sticky_notes"] = std::move(sn_arr);
    }

    return root;
}

bool Graph::save(const char* path) const {
    auto doc = build_graph_json_doc(*this);
    std::ofstream ofs(path);
    if (!ofs) {
        std::fprintf(stderr, "[vivid] Graph: failed to write %s: could not open file\n", path);
        return false;
    }
    ofs << doc.dump(4) << '\n';
    std::fprintf(stderr, "[vivid] Graph saved: %s (%zu nodes, %zu connections)\n",
        path, nodes_.size(), connections_.size());
    return true;
}

bool Graph::save_to_string(std::string& out_json) const {
    auto doc = build_graph_json_doc(*this);
    out_json = doc.dump(4) + "\n";
    return true;
}

} // namespace vivid
