#include "runtime/graph/subgraph_module.h"
#include "ui/graph/graph_snapshot.h"
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

const ModSourceBinding* SubgraphModuleDef::find_mod_source(const std::string& name) const {
    for (const auto& s : mod_sources)
        if (s.name == name) return &s;
    return nullptr;
}

const ModDestinationBinding* SubgraphModuleDef::find_mod_destination(const std::string& name) const {
    for (const auto& d : mod_destinations)
        if (d.name == name) return &d;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Parsing helpers
// ---------------------------------------------------------------------------

static VividPortType parse_port_type(const std::string& s) {
    if (s == "signal")        return VIVID_PORT_SCALAR;
    if (s == "audio")         return VIVID_PORT_AUDIO_BUFFER;
    if (s == "lane_array")    return VIVID_PORT_LANE_ARRAY;
    if (s == "string")        return VIVID_PORT_STRING;
    if (s == "string_lanes") return VIVID_PORT_STRING_LANES;
    if (s == "texture")       return VIVID_PORT_TEXTURE;
    // custom_ref / custom_value are opaque-pointer / opaque-value port shapes
    // (e.g. VividNoteBuffer). The actual transport + stable type id is
    // resolved through the bound internal node's port descriptor at compile
    // time, so the SubgraphModule placeholder type isn't load-bearing — we
    // surface as scalar here so the UI just renders a generic port pin.
    if (s == "custom_ref")    return VIVID_PORT_SCALAR;
    if (s == "custom_value")  return VIVID_PORT_SCALAR;
    std::fprintf(stderr, "[vivid] SubgraphModule: unknown port type '%s', defaulting to signal\n", s.c_str());
    return VIVID_PORT_SCALAR;
}

static VividPortDirection parse_port_direction(const std::string& s) {
    if (s == "output") return VIVID_PORT_OUTPUT;
    return VIVID_PORT_INPUT;
}

static VividParamType parse_param_type(const std::string& s) {
    if (s == "float")  return VIVID_PARAM_FLOAT;
    if (s == "int")    return VIVID_PARAM_INT;
    if (s == "bool")   return VIVID_PARAM_BOOL;
    if (s == "file")   return VIVID_PARAM_FILE;
    if (s == "text")   return VIVID_PARAM_TEXT;
    std::fprintf(stderr, "[vivid] SubgraphModule: unknown param type '%s', defaulting to float\n", s.c_str());
    return VIVID_PARAM_FLOAT;
}

static VividDisplayHint parse_display_hint(const std::string& s) {
    if (s == "default") return VIVID_DISPLAY_DEFAULT;
    if (s == "knob")    return VIVID_DISPLAY_KNOB;
    if (s == "xy_pad")  return VIVID_DISPLAY_XY_PAD;
    if (s == "color")   return VIVID_DISPLAY_COLOR;
    if (s == "hidden")  return VIVID_DISPLAY_HIDDEN;
    if (s == "editor")  return VIVID_DISPLAY_EDITOR;
    if (s == "adsr")    return VIVID_DISPLAY_ADSR;
    if (s == "lfo")     return VIVID_DISPLAY_LFO;
    if (s == "step_seq") return VIVID_DISPLAY_STEP_SEQ;
    std::fprintf(stderr, "[vivid] SubgraphModule: unknown display_hint '%s', defaulting to default\n", s.c_str());
    return VIVID_DISPLAY_DEFAULT;
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

            // Optional metadata fields
            if (auto it = pval.find("type"); it != pval.end() && it->is_string())
                pb.type = parse_param_type(it->get<std::string>());
            if (auto it = pval.find("default"); it != pval.end() && it->is_number())
                pb.default_value = static_cast<float>(it->get<double>());
            if (auto it = pval.find("min"); it != pval.end() && it->is_number())
                pb.min_value = static_cast<float>(it->get<double>());
            if (auto it = pval.find("max"); it != pval.end() && it->is_number())
                pb.max_value = static_cast<float>(it->get<double>());
            if (auto it = pval.find("choices"); it != pval.end() && it->is_array()) {
                for (const auto& c : *it) {
                    if (c.is_string()) pb.choice_labels.push_back(c.get<std::string>());
                }
            }
            if (auto it = pval.find("group"); it != pval.end() && it->is_string())
                pb.group = it->get<std::string>();
            if (auto it = pval.find("description"); it != pval.end() && it->is_string())
                pb.description = it->get<std::string>();
            if (auto it = pval.find("display_hint"); it != pval.end() && it->is_string())
                pb.display_hint = parse_display_hint(it->get<std::string>());
            if (auto it = pval.find("layout_columns"); it != pval.end() && it->is_number_unsigned())
                pb.layout_columns = static_cast<uint8_t>(it->get<unsigned>());
            if (auto it = pval.find("layout_column_index"); it != pval.end() && it->is_number_unsigned())
                pb.layout_column_index = static_cast<uint8_t>(it->get<unsigned>());
            if (auto it = pval.find("semantic_tag"); it != pval.end() && it->is_string())
                pb.semantic_tag = it->get<std::string>();
            if (auto it = pval.find("semantic_shape"); it != pval.end() && it->is_string())
                pb.semantic_shape = it->get<std::string>();
            if (auto it = pval.find("semantic_unit"); it != pval.end() && it->is_string())
                pb.semantic_unit = it->get<std::string>();
            if (auto it = pval.find("semantic_intent"); it != pval.end() && it->is_string())
                pb.semantic_intent = it->get<std::string>();
            if (auto it = pval.find("asset_kind"); it != pval.end() && it->is_string())
                pb.asset_kind = it->get<std::string>();
            if (auto it = pval.find("performance_page"); it != pval.end() && it->is_string())
                pb.performance_page = it->get<std::string>();
            if (auto it = pval.find("performance_order"); it != pval.end() && it->is_number_integer())
                pb.performance_order = it->get<int>();
            if (auto it = pval.find("performance_role"); it != pval.end() && it->is_string())
                pb.performance_role = it->get<std::string>();

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

    // Modulation sources (optional)
    auto mod_src_it = mod.find("mod_sources");
    if (mod_src_it != mod.end() && mod_src_it->is_array()) {
        std::unordered_set<std::string> seen_names;
        for (const auto& sval : *mod_src_it) {
            ModSourceBinding msb;
            auto sn = sval.find("name");
            auto sb = sval.find("bind");
            if (sn == sval.end() || !sn->is_string() || sb == sval.end() || !sb->is_string()) {
                std::fprintf(stderr, "[vivid] SubgraphModule '%s': mod_source entry missing 'name' or 'bind'\n",
                             def.name.c_str());
                return false;
            }
            msb.name = sn->get<std::string>();
            if (!seen_names.insert(msb.name).second) {
                std::fprintf(stderr, "[vivid] SubgraphModule '%s': duplicate mod_source name '%s'\n",
                             def.name.c_str(), msb.name.c_str());
                return false;
            }

            // Determine kind
            if (auto it = sval.find("kind"); it != sval.end() && it->is_string())
                msb.kind = it->get<std::string>();
            if (msb.kind.empty()) msb.kind = "internal";

            if (msb.kind == "port") {
                // bind is the exposed port name (no slash)
                msb.internal_port = sb->get<std::string>();
            } else {
                if (!split_bind(sb->get<std::string>(), msb.internal_node, msb.internal_port)) {
                    std::fprintf(stderr, "[vivid] SubgraphModule '%s': mod_source '%s' has invalid bind '%s'\n",
                                 def.name.c_str(), msb.name.c_str(), sb->get<std::string>().c_str());
                    return false;
                }
            }

            if (auto it = sval.find("description"); it != sval.end() && it->is_string())
                msb.description = it->get<std::string>();
            if (auto it = sval.find("shape"); it != sval.end() && it->is_string())
                msb.shape = it->get<std::string>();
            if (msb.shape.empty()) msb.shape = "scalar";
            if (auto it = sval.find("polarity"); it != sval.end() && it->is_string())
                msb.polarity = it->get<std::string>();
            if (msb.polarity.empty()) msb.polarity = "unipolar";
            if (auto it = sval.find("group"); it != sval.end() && it->is_string())
                msb.group = it->get<std::string>();

            def.mod_sources.push_back(std::move(msb));
        }
    }

    // Modulation destinations (optional)
    auto mod_dst_it = mod.find("mod_destinations");
    if (mod_dst_it != mod.end() && mod_dst_it->is_array()) {
        std::unordered_set<std::string> seen_names;
        for (const auto& dval : *mod_dst_it) {
            ModDestinationBinding mdb;
            auto dn = dval.find("name");
            auto db = dval.find("bind");
            if (dn == dval.end() || !dn->is_string() || db == dval.end() || !db->is_string()) {
                std::fprintf(stderr, "[vivid] SubgraphModule '%s': mod_destination entry missing 'name' or 'bind'\n",
                             def.name.c_str());
                return false;
            }
            mdb.name = dn->get<std::string>();
            if (!seen_names.insert(mdb.name).second) {
                std::fprintf(stderr, "[vivid] SubgraphModule '%s': duplicate mod_destination name '%s'\n",
                             def.name.c_str(), mdb.name.c_str());
                return false;
            }
            if (!split_bind(db->get<std::string>(), mdb.internal_node, mdb.internal_param)) {
                std::fprintf(stderr, "[vivid] SubgraphModule '%s': mod_destination '%s' has invalid bind '%s'\n",
                             def.name.c_str(), mdb.name.c_str(), db->get<std::string>().c_str());
                return false;
            }

            if (auto it = dval.find("description"); it != dval.end() && it->is_string())
                mdb.description = it->get<std::string>();
            if (auto it = dval.find("shape"); it != dval.end() && it->is_string())
                mdb.shape = it->get<std::string>();
            if (mdb.shape.empty()) mdb.shape = "scalar";
            if (auto it = dval.find("group"); it != dval.end() && it->is_string())
                mdb.group = it->get<std::string>();

            def.mod_destinations.push_back(std::move(mdb));
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

        // Validate param/port bindings — these are structural and must reference
        // existing internal nodes, otherwise flattening will silently break.
        for (const auto& pb : def.params) {
            if (!def.internal_graph.find_node(pb.internal_node)) {
                std::fprintf(stderr, "[vivid] SubgraphModule '%s': param '%s' binds to "
                             "non-existent internal node '%s'\n",
                             def.name.c_str(), pb.name.c_str(), pb.internal_node.c_str());
                return false;
            }
        }
        for (const auto& pb : def.ports) {
            if (!def.internal_graph.find_node(pb.internal_node)) {
                std::fprintf(stderr, "[vivid] SubgraphModule '%s': port '%s' binds to "
                             "non-existent internal node '%s'\n",
                             def.name.c_str(), pb.name.c_str(), pb.internal_node.c_str());
                return false;
            }
        }
        // Validate mod_source bindings
        for (const auto& msb : def.mod_sources) {
            if (msb.kind == "port") {
                // Must reference an existing exposed input port
                bool found = false;
                for (const auto& pb : def.ports) {
                    if (pb.name == msb.internal_port && pb.direction == VIVID_PORT_INPUT) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::fprintf(stderr, "[vivid] SubgraphModule '%s': mod_source '%s' references "
                                 "non-existent input port '%s'\n",
                                 def.name.c_str(), msb.name.c_str(), msb.internal_port.c_str());
                    return false;
                }
            } else {
                if (!def.internal_graph.find_node(msb.internal_node)) {
                    std::fprintf(stderr, "[vivid] SubgraphModule '%s': mod_source '%s' binds to "
                                 "non-existent internal node '%s'\n",
                                 def.name.c_str(), msb.name.c_str(), msb.internal_node.c_str());
                    return false;
                }
            }
        }
        // Validate mod_destination bindings
        for (const auto& mdb : def.mod_destinations) {
            if (!def.internal_graph.find_node(mdb.internal_node)) {
                std::fprintf(stderr, "[vivid] SubgraphModule '%s': mod_destination '%s' binds to "
                             "non-existent internal node '%s'\n",
                             def.name.c_str(), mdb.name.c_str(), mdb.internal_node.c_str());
                return false;
            }
        }

        // Preset references are non-fatal — bad keys are silently dropped at apply time.
        for (const auto& preset : def.presets) {
            for (const auto& [key, _] : preset.param_overrides) {
                std::string node, param;
                if (split_bind(key, node, param) && !def.internal_graph.find_node(node)) {
                    std::fprintf(stderr, "[vivid] SubgraphModule '%s': preset '%s' references "
                                 "non-existent internal node '%s' (will be ignored)\n",
                                 def.name.c_str(), preset.name.c_str(), node.c_str());
                }
            }
            for (const auto& [key, _] : preset.string_param_overrides) {
                std::string node, param;
                if (split_bind(key, node, param) && !def.internal_graph.find_node(node)) {
                    std::fprintf(stderr, "[vivid] SubgraphModule '%s': preset '%s' references "
                                 "non-existent internal node '%s' (will be ignored)\n",
                                 def.name.c_str(), preset.name.c_str(), node.c_str());
                }
            }
        }

        std::fprintf(stderr, "[vivid] SubgraphModule: loaded '%s' (%zu ports, %zu params, %zu presets, "
                     "%zu mod_sources, %zu mod_destinations, %zu internal nodes)\n",
                     def.name.c_str(), def.ports.size(), def.params.size(),
                     def.presets.size(), def.mod_sources.size(), def.mod_destinations.size(),
                     def.internal_graph.nodes().size());

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
    static constexpr const char* kModuleSuffix = ".vivid-module.json";
    static constexpr size_t kModuleSuffixLen = 18;
    // Iterate directory entries looking for the module extension
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto& p = entry.path();
        // Check for .vivid-module.json suffix
        auto filename = p.filename().string();
        if (filename.size() >= kModuleSuffixLen &&
            filename.compare(filename.size() - kModuleSuffixLen, kModuleSuffixLen, kModuleSuffix) == 0) {
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
    // SubgraphModuleDef doesn't yet expose explicit display_name/keywords/summary.
    // Auto-derive a display name; modules that want to override get a manifest
    // field in a follow-up. Empty keywords/summary are fine — the chooser falls
    // back to the name/display_name tiers.
    info->display_name = vivid::default_display_name(def.name);
    info->is_gpu = false;
    info->is_module = true;

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
    // Use explicit metadata when provided, otherwise fall back to internal node
    // defaults (for default_value) or sensible defaults (0..1 for range).
    info->params.reserve(def.params.size());
    for (const auto& pb : def.params) {
        ui::ParamInfo pi;
        pi.name = pb.name;
        pi.type = pb.type.value_or(VIVID_PARAM_FLOAT);

        // Start with fallback defaults
        pi.default_value = 0.0f;
        pi.min_value = 0.0f;
        pi.max_value = 1.0f;

        // Inherit default from internal node's graph params
        const auto* inode = def.internal_graph.find_node(pb.internal_node);
        if (inode) {
            auto it = inode->params.find(pb.internal_param);
            if (it != inode->params.end())
                pi.default_value = it->second;
        }

        // Override with explicit metadata where specified
        if (pb.default_value.has_value()) pi.default_value = *pb.default_value;
        if (pb.min_value.has_value())     pi.min_value = *pb.min_value;
        if (pb.max_value.has_value())     pi.max_value = *pb.max_value;

        pi.choice_labels = pb.choice_labels;
        pi.choice_count = static_cast<uint32_t>(pb.choice_labels.size());
        pi.group = pb.group;
        pi.description = pb.description;
        pi.display_hint = pb.display_hint.value_or(VIVID_DISPLAY_DEFAULT);
        pi.layout_columns = pb.layout_columns;
        pi.layout_column_index = pb.layout_column_index;
        pi.semantic_tag = pb.semantic_tag;
        pi.semantic_shape = pb.semantic_shape;
        pi.semantic_unit = pb.semantic_unit;
        pi.semantic_intent = pb.semantic_intent;
        pi.asset_kind = pb.asset_kind;
        pi.performance_page = pb.performance_page;
        pi.performance_order = pb.performance_order;
        pi.performance_role = pb.performance_role;

        info->params.push_back(std::move(pi));
    }

    ui::build_search_haystack(*info);
    return info;
}

OperatorPreset to_operator_preset(const SubgraphPreset& sp, const SubgraphModuleDef& def) {
    // Build reverse map: "internal_node/internal_param" -> exposed param name
    std::unordered_map<std::string, std::string> internal_to_exposed;
    for (const auto& pb : def.params)
        internal_to_exposed[pb.internal_node + "/" + pb.internal_param] = pb.name;

    OperatorPreset op;
    op.name = sp.name;
    for (const auto& [key, val] : sp.param_overrides) {
        auto it = internal_to_exposed.find(key);
        if (it != internal_to_exposed.end())
            op.params[it->second] = val;
    }
    for (const auto& [key, val] : sp.string_param_overrides) {
        auto it = internal_to_exposed.find(key);
        if (it != internal_to_exposed.end())
            op.string_params[it->second] = val;
    }
    return op;
}

// ---------------------------------------------------------------------------
// Graph flattening
// ---------------------------------------------------------------------------

// Prefix for internal node IDs to avoid collisions.
// e.g. instance "synth1" + internal node "osc" => "synth1.__osc"
static std::string prefixed_id(const std::string& instance_id, const std::string& internal_id) {
    return instance_id + ".__" + internal_id;
}

FlattenResult flatten_subgraphs(const Graph& authored, const SubgraphModuleRegistry& registry) {
    if (registry.empty()) return {authored, {}};

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

    if (instances.empty()) return {authored, {}};

    // Build a set of module node IDs for quick lookup
    std::unordered_set<std::string> module_node_ids;
    for (const auto& inst : instances)
        module_node_ids.insert(authored.nodes()[inst.node_index].id);

    // Start building the flat graph.
    // Copy schema metadata.
    std::vector<ModulationLoweringRecord> mod_records;
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

        // ---------------------------------------------------------------
        // Lower modulation assignments into connections + Math(add) nodes
        // ---------------------------------------------------------------
        const auto* mod_assigns = authored.mod_assignments().count(instance_id)
            ? &authored.mod_assignments().at(instance_id) : nullptr;
        if (mod_assigns && !mod_assigns->empty()) {
            // Group assignments by destination
            std::unordered_map<std::string, std::vector<const ModAssignmentDef*>> by_dest;
            for (const auto& ma : *mod_assigns)
                by_dest[ma.destination].push_back(&ma);

            for (const auto& [dest_name, assigns] : by_dest) {
                const auto* dest_binding = def.find_mod_destination(dest_name);
                if (!dest_binding) {
                    std::fprintf(stderr, "[vivid] SubgraphModule '%s' instance '%s': "
                                 "unknown mod_destination '%s' — skipping\n",
                                 def.name.c_str(), instance_id.c_str(), dest_name.c_str());
                    continue;
                }

                // Check if destination is already wire-driven in internal graph
                std::string dest_flat_node = prefixed_id(instance_id, dest_binding->internal_node);
                bool dest_already_wired = false;
                for (const auto& conn : def.internal_graph.connections()) {
                    if (conn.to_node == dest_binding->internal_node &&
                        conn.to_port == dest_binding->internal_param) {
                        dest_already_wired = true;
                        break;
                    }
                }
                if (dest_already_wired) {
                    std::fprintf(stderr, "[vivid] SubgraphModule '%s' instance '%s': "
                                 "destination '%s' already has an incoming connection — skipping mod assignments\n",
                                 def.name.c_str(), instance_id.c_str(), dest_name.c_str());
                    continue;
                }

                // Read base value from the merged internal node params
                float base_value = 0.0f;
                const auto* inode = flat.find_node(dest_flat_node);
                if (inode) {
                    auto pit = inode->params.find(dest_binding->internal_param);
                    if (pit != inode->params.end())
                        base_value = pit->second;
                }

                // Find exposed param name that maps to this destination (for live updates)
                std::string exposed_param_name;
                for (const auto& pb : def.params) {
                    if (pb.internal_node == dest_binding->internal_node &&
                        pb.internal_param == dest_binding->internal_param) {
                        exposed_param_name = pb.name;
                        break;
                    }
                }

                // Process assignments for this destination
                // current_output tracks the last node/port in the chain
                std::string current_node;
                std::string current_port;

                for (size_t j = 0; j < assigns.size(); ++j) {
                    const auto& ma = *assigns[j];

                    // Validate lane rules
                    const auto* src_binding = def.find_mod_source(ma.source);
                    if (!src_binding) {
                        std::fprintf(stderr, "[vivid] SubgraphModule '%s' instance '%s': "
                                     "unknown mod_source '%s' — skipping\n",
                                     def.name.c_str(), instance_id.c_str(), ma.source.c_str());
                        continue;
                    }
                    if (src_binding->shape == "lane_aware" && dest_binding->shape != "lane_aware") {
                        std::fprintf(stderr, "[vivid] SubgraphModule '%s' instance '%s': "
                                     "lane_aware source '%s' -> scalar destination '%s' is not allowed — skipping\n",
                                     def.name.c_str(), instance_id.c_str(),
                                     ma.source.c_str(), ma.destination.c_str());
                        continue;
                    }

                    // Resolve source address
                    std::string src_node, src_port;
                    if (src_binding->kind == "port") {
                        // Resolve through port binding
                        const auto* port_bind = def.find_port(src_binding->internal_port);
                        if (port_bind) {
                            src_node = prefixed_id(instance_id, port_bind->internal_node);
                            src_port = port_bind->internal_port;
                        } else {
                            std::fprintf(stderr, "[vivid] SubgraphModule '%s' instance '%s': "
                                         "mod_source '%s' port '%s' not found — skipping\n",
                                         def.name.c_str(), instance_id.c_str(),
                                         ma.source.c_str(), src_binding->internal_port.c_str());
                            continue;
                        }
                    } else {
                        src_node = prefixed_id(instance_id, src_binding->internal_node);
                        src_port = src_binding->internal_port;
                    }

                    bool bipolar = (ma.polarity == "bipolar");

                    if (assigns.size() == 1) {
                        // Single assignment: direct connection with remap, no Math node
                        flat.add_connection(src_node, src_port,
                                            dest_flat_node, dest_binding->internal_param);
                        float to_min = bipolar ? (base_value - ma.amount) : base_value;
                        float to_max = base_value + ma.amount;
                        flat.set_connection_remap(src_node, src_port,
                                                  dest_flat_node, dest_binding->internal_param,
                                                  0.0f, 1.0f, to_min, to_max, false);

                        mod_records.push_back({
                            instance_id, exposed_param_name,
                            src_node, src_port,
                            dest_flat_node, dest_binding->internal_param,
                            ma.amount, bipolar
                        });
                    } else {
                        // Multiple assignments: use Math(add) chain
                        if (j == 0) {
                            // First assignment: create Math(add), wire source to input a
                            std::string math_id = prefixed_id(instance_id,
                                "mod_" + ma.source + "_to_" + dest_name);
                            flat.add_node(math_id, "Math", {{"operation", 0.0f}});
                            auto* math_node = flat.find_node(math_id);
                            if (math_node) {
                                math_node->subgraph_owner = instance_id;
                                math_node->subgraph_type = def.name;
                            }

                            // Wire source -> math/a with remap encoding base + amount
                            flat.add_connection(src_node, src_port, math_id, "a");
                            float to_min = bipolar ? (base_value - ma.amount) : base_value;
                            float to_max = base_value + ma.amount;
                            flat.set_connection_remap(src_node, src_port, math_id, "a",
                                                      0.0f, 1.0f, to_min, to_max, false);

                            mod_records.push_back({
                                instance_id, exposed_param_name,
                                src_node, src_port,
                                math_id, "a",
                                ma.amount, bipolar
                            });

                            current_node = math_id;
                            current_port = "result";
                        } else if (j == 1) {
                            // Second assignment: wire to the first Math(add)'s b input
                            std::string math_id = current_node;  // reuse first Math
                            flat.add_connection(src_node, src_port, math_id, "b");
                            float to_min = bipolar ? -ma.amount : 0.0f;
                            float to_max = ma.amount;
                            flat.set_connection_remap(src_node, src_port, math_id, "b",
                                                      0.0f, 1.0f, to_min, to_max, false);
                        } else {
                            // j >= 2: chain a new Math(add)
                            std::string math_id = prefixed_id(instance_id,
                                "mod_" + ma.source + "_to_" + dest_name);
                            flat.add_node(math_id, "Math", {{"operation", 0.0f}});
                            auto* math_node = flat.find_node(math_id);
                            if (math_node) {
                                math_node->subgraph_owner = instance_id;
                                math_node->subgraph_type = def.name;
                            }

                            // Wire previous chain output -> new math/a
                            flat.add_connection(current_node, current_port, math_id, "a");
                            // Wire source -> new math/b with remap
                            flat.add_connection(src_node, src_port, math_id, "b");
                            float to_min = bipolar ? -ma.amount : 0.0f;
                            float to_max = ma.amount;
                            flat.set_connection_remap(src_node, src_port, math_id, "b",
                                                      0.0f, 1.0f, to_min, to_max, false);

                            current_node = math_id;
                            current_port = "result";
                        }

                        // After last assignment: wire chain output to destination param
                        if (j == assigns.size() - 1) {
                            flat.add_connection(current_node, current_port,
                                                dest_flat_node, dest_binding->internal_param);
                        }
                    }
                }
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

    // Copy non-graph metadata: MIDI mappings, variations, sticky notes.
    // MIDI mappings and variations that target module nodes must be remapped
    // through the param binding table to reach the correct internal node/param.

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

    return {std::move(flat), std::move(mod_records)};
}

} // namespace vivid
