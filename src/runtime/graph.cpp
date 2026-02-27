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

    yyjson_doc_free(doc);

    std::fprintf(stderr, "[vivid] Loaded graph: %s (%zu nodes, %zu connections)\n",
        path, nodes_.size(), connections_.size());
    return true;
}

// --- Mutation ---

bool Graph::add_node(const std::string& id, const std::string& type,
                     const std::unordered_map<std::string, float>& params) {
    if (find_node(id)) return false;  // duplicate id
    NodeDef node;
    node.id = id;
    node.type = type;
    node.params = params;
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

        if (!node.params.empty()) {
            yyjson_mut_val* params_obj = yyjson_mut_obj(doc);
            for (const auto& [pname, pval] : node.params) {
                yyjson_mut_obj_add_real(doc, params_obj, pname.c_str(), static_cast<double>(pval));
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
