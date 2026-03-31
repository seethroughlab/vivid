#include "runtime/subgraph_module.h"
#include "ui/graph_snapshot.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace vivid {

// ---------------------------------------------------------------------------
// SubgraphModuleDef helpers
// ---------------------------------------------------------------------------

const SubgraphPortBinding* SubgraphModuleDef::find_port(const std::string& port_name) const {
    for (const auto& p : ports)
        if (p.name == port_name) return &p;
    return nullptr;
}

const SubgraphParamBinding* SubgraphModuleDef::find_param(const std::string& param_name) const {
    for (const auto& p : params)
        if (p.name == param_name) return &p;
    return nullptr;
}

const SubgraphPreset* SubgraphModuleDef::find_preset(const std::string& preset_name) const {
    for (const auto& p : presets)
        if (p.name == preset_name) return &p;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------

static VividPortType parse_port_type(const std::string& s) {
    if (s == "signal")        return VIVID_PORT_SIGNAL;
    if (s == "audio")         return VIVID_PORT_AUDIO;
    if (s == "spread")        return VIVID_PORT_LANE_ARRAY;
    if (s == "string")        return VIVID_PORT_STRING;
    if (s == "string_spread") return VIVID_PORT_STRING_SPREAD;
    if (s == "texture")       return VIVID_PORT_TEXTURE;
    std::fprintf(stderr, "[vivid] SubgraphModule: unknown port type '%s', defaulting to signal\n", s.c_str());
    return VIVID_PORT_SIGNAL;
}

static VividPortDirection parse_port_direction(const std::string& s) {
    if (s == "output") return VIVID_PORT_OUTPUT;
    return VIVID_PORT_INPUT;
}

// Split "node_id/port_or_param" into two parts. Returns false on bad format.
static bool split_bind(const std::string& bind, std::string& node, std::string& name) {
    auto slash = bind.find('/');
    if (slash == std::string::npos || slash == 0 || slash == bind.size() - 1)
        return false;
    node = bind.substr(0, slash);
    name = bind.substr(slash + 1);
    return true;
}

static bool parse_module_def(const nlohmann::json& root, SubgraphModuleDef& def) {
    auto mod_it = root.find("module");
    if (mod_it == root.end() || !mod_it->is_object()) {
        std::fprintf(stderr, "[vivid] SubgraphModule: missing 'module' section\n");
        return false;
    }
    const auto& mod = *mod_it;

    // Name (required)
    auto name_it = mod.find("name");
    if (name_it == mod.end() || !name_it->is_string() || name_it->get<std::string>().empty()) {
        std::fprintf(stderr, "[vivid] SubgraphModule: missing or empty 'name'\n");
        return false;
    }
    def.name = name_it->get<std::string>();

    // Description, category (optional)
    auto desc_it = mod.find("description");
    if (desc_it != mod.end() && desc_it->is_string())
        def.description = desc_it->get<std::string>();

    auto cat_it = mod.find("category");
    if (cat_it != mod.end() && cat_it->is_string())
        def.category = cat_it->get<std::string>();

    // Ports (required, at least one)
    auto ports_it = mod.find("ports");
    if (ports_it == mod.end() || !ports_it->is_array() || ports_it->empty()) {
        std::fprintf(stderr, "[vivid] SubgraphModule '%s': 'ports' must be a non-empty array\n",
                     def.name.c_str());
        return false;
    }
    for (const auto& pval : *ports_it) {
        SubgraphPortBinding pb;
        auto pn = pval.find("name");
        auto pt = pval.find("type");
        auto pd = pval.find("direction");
        auto pbind = pval.find("bind");
        if (pn == pval.end() || !pn->is_string() || pt == pval.end() || !pt->is_string() ||
            pd == pval.end() || !pd->is_string() || pbind == pval.end() || !pbind->is_string()) {
            std::fprintf(stderr, "[vivid] SubgraphModule '%s': port entry missing required fields\n",
                         def.name.c_str());
            return false;
        }
        pb.name = pn->get<std::string>();
        pb.type = parse_port_type(pt->get<std::string>());
        pb.direction = parse_port_direction(pd->get<std::string>());
        if (!split_bind(pbind->get<std::string>(), pb.internal_node, pb.internal_port)) {
            std::fprintf(stderr, "[vivid] SubgraphModule '%s': port '%s' has invalid bind '%s'\n",
                         def.name.c_str(), pb.name.c_str(), pbind->get<std::string>().c_str());
            return false;
        }
        def.ports.push_back(std::move(pb));
    }

    // Params (optional)
    auto params_it = mod.find("params");
    if (params_it != mod.end() && params_it->is_array()) {
        for (const auto& pval : *params_it) {
            SubgraphParamBinding pb;
            auto pn = pval.find("name");
            auto pbind = pval.find("bind");
            if (pn == pval.end() || !pn->is_string() || pbind == pval.end() || !pbind->is_string()) {
                std::fprintf(stderr, "[vivid] SubgraphModule '%s': param entry missing required fields\n",
                             def.name.c_str());
                return false;
            }
            pb.name = pn->get<std::string>();
            if (!split_bind(pbind->get<std::string>(), pb.internal_node, pb.internal_param)) {
                std::fprintf(stderr, "[vivid] SubgraphModule '%s': param '%s' has invalid bind '%s'\n",
                             def.name.c_str(), pb.name.c_str(), pbind->get<std::string>().c_str());
                return false;
            }
            def.params.push_back(std::move(pb));
        }
    }

    // Presets (optional)
    auto presets_it = mod.find("presets");
    if (presets_it != mod.end() && presets_it->is_object()) {
        for (const auto& [pname, pval] : presets_it->items()) {
            if (!pval.is_object()) continue;
            SubgraphPreset sp;
            sp.name = pname;
            for (const auto& [key, val] : pval.items()) {
                if (val.is_number())
                    sp.param_overrides[key] = static_cast<float>(val.get<double>());
                else if (val.is_string())
                    sp.string_param_overrides[key] = val.get<std::string>();
            }
            def.presets.push_back(std::move(sp));
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// SubgraphModuleRegistry
// ---------------------------------------------------------------------------

bool SubgraphModuleRegistry::load(const std::string& path) {
    try {
        std::ifstream ifs(path);
        if (!ifs) {
            std::fprintf(stderr, "[vivid] SubgraphModule: failed to open '%s'\n", path.c_str());
            return false;
        }
        auto root = nlohmann::json::parse(ifs);

        SubgraphModuleDef def;
        def.source_path = path;

        if (!parse_module_def(root, def))
            return false;

        // Parse the internal graph (nodes/connections) using the same JSON.
        // We serialize back the relevant parts and let Graph parse them.
        nlohmann::json graph_json;
        auto sv = root.find("schema_version");
        if (sv != root.end()) graph_json["schema_version"] = *sv;
        auto nodes_it = root.find("nodes");
        if (nodes_it != root.end()) graph_json["nodes"] = *nodes_it;
        auto conn_it = root.find("connections");
        if (conn_it != root.end()) graph_json["connections"] = *conn_it;

        std::string graph_str = graph_json.dump();
        if (!def.internal_graph.load_from_string(graph_str.c_str(), graph_str.size())) {
            std::fprintf(stderr, "[vivid] SubgraphModule '%s': failed to parse internal graph\n",
                         def.name.c_str());
            return false;
        }

        std::fprintf(stderr, "[vivid] SubgraphModule: loaded '%s' (%zu ports, %zu params, %zu presets, %zu internal nodes)\n",
                     def.name.c_str(), def.ports.size(), def.params.size(),
                     def.presets.size(), def.internal_graph.nodes().size());

        std::string name = def.name;
        modules_[name] = std::move(def);
        return true;

    } catch (const nlohmann::json::parse_error& e) {
        std::fprintf(stderr, "[vivid] SubgraphModule: JSON parse error in '%s': %s\n",
                     path.c_str(), e.what());
        return false;
    }
}

int SubgraphModuleRegistry::scan(const std::string& directory) {
    // Use std::filesystem to find .vivid-module.json files
    int count = 0;
    // Iterate directory entries looking for the module extension
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto& p = entry.path();
        // Check for .vivid-module.json suffix
        auto filename = p.filename().string();
        if (filename.size() > 20 && filename.substr(filename.size() - 20) == ".vivid-module.json") {
            if (load(p.string()))
                ++count;
        }
    }
    if (ec) {
        std::fprintf(stderr, "[vivid] SubgraphModule: failed to scan directory '%s': %s\n",
                     directory.c_str(), ec.message().c_str());
    }
    return count;
}

bool SubgraphModuleRegistry::register_module(SubgraphModuleDef def) {
    if (def.name.empty()) return false;
    std::string name = def.name;
    modules_[name] = std::move(def);
    return true;
}

const SubgraphModuleDef* SubgraphModuleRegistry::find(const std::string& type_name) const {
    auto it = modules_.find(type_name);
    if (it == modules_.end()) return nullptr;
    return &it->second;
}

std::vector<std::string> SubgraphModuleRegistry::type_names() const {
    std::vector<std::string> names;
    names.reserve(modules_.size());
    for (const auto& [name, _] : modules_)
        names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

// ---------------------------------------------------------------------------
// Synthetic OperatorInfo for UI catalog
// ---------------------------------------------------------------------------

std::shared_ptr<const ui::OperatorInfo> make_operator_info(const SubgraphModuleDef& def) {
    auto info = std::make_shared<ui::OperatorInfo>();
    info->name = def.name;
    info->is_gpu = false;

    // Infer cadence capability from internal graph: if any internal node has
    // audio-cadence ports, the module is at least audio-capable.
    bool has_audio_port = false;
    for (const auto& pb : def.ports) {
        if (pb.type == VIVID_PORT_AUDIO) { has_audio_port = true; break; }
    }
    info->cadence_capability = has_audio_port ? VIVID_CADENCE_AUDIO_CAPABLE
                                              : VIVID_CADENCE_FRAME_ONLY;

    // Ports from module definition
    info->ports.reserve(def.ports.size());
    for (const auto& pb : def.ports) {
        ui::PortInfo pi;
        pi.name = pb.name;
        pi.type = pb.type;
        pi.direction = pb.direction;
        info->ports.push_back(std::move(pi));
    }

    // Params from module definition.
    // Inherit default value from the bound internal node's graph params.
    info->params.reserve(def.params.size());
    for (const auto& pb : def.params) {
        ui::ParamInfo pi;
        pi.name = pb.name;
        pi.type = VIVID_PARAM_FLOAT;
        pi.default_value = 0.0f;
        pi.min_value = 0.0f;
        pi.max_value = 1.0f;

        // Look up the bound internal node's param default from the internal graph
        const auto* inode = def.internal_graph.find_node(pb.internal_node);
        if (inode) {
            auto it = inode->params.find(pb.internal_param);
            if (it != inode->params.end())
                pi.default_value = it->second;
        }

        info->params.push_back(std::move(pi));
    }

    return info;
}

// ---------------------------------------------------------------------------
// Graph flattening
// ---------------------------------------------------------------------------

// Prefix for internal node IDs to avoid collisions.
// e.g. instance "synth1" + internal node "osc" => "synth1.__osc"
static std::string prefixed_id(const std::string& instance_id, const std::string& internal_id) {
    return instance_id + ".__" + internal_id;
}

Graph flatten_subgraphs(const Graph& authored, const SubgraphModuleRegistry& registry) {
    if (registry.empty()) return authored;

    // Collect which authored nodes are module instances
    struct ModuleInstance {
        size_t node_index;
        const SubgraphModuleDef* def;
    };
    std::vector<ModuleInstance> instances;
    for (size_t i = 0; i < authored.nodes().size(); ++i) {
        const auto* def = registry.find(authored.nodes()[i].type);
        if (def) instances.push_back({i, def});
    }

    if (instances.empty()) return authored;

    // Build a set of module node IDs for quick lookup
    std::unordered_set<std::string> module_node_ids;
    for (const auto& inst : instances)
        module_node_ids.insert(authored.nodes()[inst.node_index].id);

    // Start building the flat graph.
    // Copy schema metadata.
    Graph flat;
    flat.schema_version = authored.schema_version;
    flat.vivid_version = authored.vivid_version;
    if (authored.has_viewport())
        flat.set_viewport(authored.viewport_pan_x, authored.viewport_pan_y, authored.viewport_zoom);

    // Copy non-module nodes as-is
    for (const auto& node : authored.nodes()) {
        if (module_node_ids.count(node.id)) continue;
        flat.add_node(node.id, node.type, node.params, node.string_params);
        auto* added = flat.find_node(node.id);
        if (added) {
            added->layout_x = node.layout_x;
            added->layout_y = node.layout_y;
            added->tex_width = node.tex_width;
            added->tex_height = node.tex_height;
            added->cadence_override = node.cadence_override;
            added->param_lock_flags = node.param_lock_flags;
            added->pkg_name = node.pkg_name;
            added->pkg_version = node.pkg_version;
        }
    }

    // For each module instance, expand internal nodes
    for (const auto& inst : instances) {
        const auto& module_node = authored.nodes()[inst.node_index];
        const auto& def = *inst.def;
        const std::string& instance_id = module_node.id;

        // Build param binding lookup: external_name -> (internal_node, internal_param)
        // so we can apply the module node's param values to internal nodes.
        std::unordered_map<std::string, const SubgraphParamBinding*> param_bindings;
        for (const auto& pb : def.params)
            param_bindings[pb.name] = &pb;

        // Collect param overrides to apply to internal nodes.
        // key = internal_node_id, value = { param_name -> value }
        std::unordered_map<std::string, std::unordered_map<std::string, float>> float_overrides;
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> string_overrides;

        for (const auto& [pname, pval] : module_node.params) {
            auto it = param_bindings.find(pname);
            if (it != param_bindings.end()) {
                float_overrides[it->second->internal_node][it->second->internal_param] = pval;
            }
        }
        for (const auto& [pname, pval] : module_node.string_params) {
            auto it = param_bindings.find(pname);
            if (it != param_bindings.end()) {
                string_overrides[it->second->internal_node][it->second->internal_param] = pval;
            }
        }

        // Copy internal nodes with prefixed IDs and apply overrides
        for (const auto& inode : def.internal_graph.nodes()) {
            std::string flat_id = prefixed_id(instance_id, inode.id);

            auto merged_params = inode.params;
            auto fo_it = float_overrides.find(inode.id);
            if (fo_it != float_overrides.end()) {
                for (const auto& [k, v] : fo_it->second)
                    merged_params[k] = v;
            }

            auto merged_string_params = inode.string_params;
            auto so_it = string_overrides.find(inode.id);
            if (so_it != string_overrides.end()) {
                for (const auto& [k, v] : so_it->second)
                    merged_string_params[k] = v;
            }

            flat.add_node(flat_id, inode.type, merged_params, merged_string_params);
            auto* added = flat.find_node(flat_id);
            if (added) {
                added->layout_x = inode.layout_x;
                added->layout_y = inode.layout_y;
                added->tex_width = inode.tex_width;
                added->tex_height = inode.tex_height;
                added->cadence_override = inode.cadence_override;
                added->param_lock_flags = inode.param_lock_flags;
                added->pkg_name = inode.pkg_name;
                added->pkg_version = inode.pkg_version;
                added->subgraph_owner = instance_id;
                added->subgraph_type = def.name;
            }
        }

        // Copy internal connections with prefixed node IDs
        for (const auto& conn : def.internal_graph.connections()) {
            std::string from = prefixed_id(instance_id, conn.from_node);
            std::string to   = prefixed_id(instance_id, conn.to_node);
            flat.add_connection(from, conn.from_port, to, conn.to_port);
            if (conn.has_remap()) {
                flat.set_connection_remap(from, conn.from_port, to, conn.to_port,
                                          conn.from_min, conn.from_max,
                                          conn.to_min, conn.to_max, conn.clamp);
            }
        }
    }

    // Build port and param binding lookups for rewriting connections, MIDI mappings, and variations.
    // Key format: "instance_id/name" (slash-separated, matching address convention).
    struct ResolvedBinding {
        std::string flat_node_id;
        std::string name;  // internal port or param name
    };
    std::unordered_map<std::string, ResolvedBinding> port_map;   // "instance/ext_port" -> internal
    std::unordered_map<std::string, ResolvedBinding> param_map;  // "instance/ext_param" -> internal

    for (const auto& inst : instances) {
        const auto& module_node = authored.nodes()[inst.node_index];
        for (const auto& pb : inst.def->ports) {
            std::string key = module_node.id + "/" + pb.name;
            port_map[key] = {prefixed_id(module_node.id, pb.internal_node), pb.internal_port};
        }
        for (const auto& pb : inst.def->params) {
            std::string key = module_node.id + "/" + pb.name;
            param_map[key] = {prefixed_id(module_node.id, pb.internal_node), pb.internal_param};
        }
    }

    // Copy connections, rewriting any that reference module nodes through port bindings
    for (const auto& conn : authored.connections()) {
        std::string from_node = conn.from_node;
        std::string from_port = conn.from_port;
        std::string to_node   = conn.to_node;
        std::string to_port   = conn.to_port;

        // If the source is a module node, rewrite through port binding
        if (module_node_ids.count(from_node)) {
            std::string key = from_node + "/" + from_port;
            auto it = port_map.find(key);
            if (it != port_map.end()) {
                from_node = it->second.flat_node_id;
                from_port = it->second.name;
            } else {
                std::fprintf(stderr, "[vivid] SubgraphModule: no port binding for %s — skipping connection\n",
                             key.c_str());
                continue;
            }
        }

        // If the destination is a module node, rewrite through port binding
        if (module_node_ids.count(to_node)) {
            std::string key = to_node + "/" + to_port;
            auto it = port_map.find(key);
            if (it != port_map.end()) {
                to_node = it->second.flat_node_id;
                to_port = it->second.name;
            } else {
                std::fprintf(stderr, "[vivid] SubgraphModule: no port binding for %s — skipping connection\n",
                             key.c_str());
                continue;
            }
        }

        flat.add_connection(from_node, from_port, to_node, to_port);
        if (conn.has_remap()) {
            flat.set_connection_remap(from_node, from_port, to_node, to_port,
                                      conn.from_min, conn.from_max,
                                      conn.to_min, conn.to_max, conn.clamp);
        }
    }

    // Copy non-graph metadata: filters, MIDI mappings, variations, sticky notes.
    // MIDI mappings and variations that target module nodes must be remapped
    // through the param binding table to reach the correct internal node/param.
    for (const auto& f : authored.filters())
        flat.add_filter(f);

    for (const auto& m : authored.midi_mappings()) {
        std::string node_id = m.node_id;
        std::string param_name = m.param_name;
        if (module_node_ids.count(node_id)) {
            std::string key = node_id + "/" + param_name;
            auto it = param_map.find(key);
            if (it != param_map.end()) {
                node_id = it->second.flat_node_id;
                param_name = it->second.name;
            } else {
                std::fprintf(stderr, "[vivid] SubgraphModule: no param binding for MIDI mapping %s — skipping\n",
                             key.c_str());
                continue;
            }
        }
        flat.add_midi_mapping(node_id, param_name, m.cc_number, m.channel,
                              m.range_min, m.range_max);
    }

    for (const auto& v : authored.variations()) {
        VariationDef remapped;
        remapped.name = v.name;
        // Collect remapped entries separately to avoid iterator invalidation
        for (const auto& [node_id, node_params] : v.params) {
            if (module_node_ids.count(node_id)) {
                // Distribute each param to its bound internal node
                for (const auto& [pname, pval] : node_params) {
                    std::string key = node_id + "/" + pname;
                    auto pm_it = param_map.find(key);
                    if (pm_it != param_map.end())
                        remapped.params[pm_it->second.flat_node_id][pm_it->second.name] = pval;
                }
            } else {
                remapped.params[node_id] = node_params;
            }
        }
        for (const auto& [node_id, node_params] : v.string_params) {
            if (module_node_ids.count(node_id)) {
                for (const auto& [pname, pval] : node_params) {
                    std::string key = node_id + "/" + pname;
                    auto pm_it = param_map.find(key);
                    if (pm_it != param_map.end())
                        remapped.string_params[pm_it->second.flat_node_id][pm_it->second.name] = pval;
                }
            } else {
                remapped.string_params[node_id] = node_params;
            }
        }
        flat.add_variation(remapped);
    }

    if (authored.active_variation() >= 0)
        flat.set_active_variation(authored.active_variation());

    for (const auto& sn : authored.sticky_notes())
        flat.add_sticky_note(sn);

    return flat;
}

} // namespace vivid
