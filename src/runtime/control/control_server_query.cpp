#include "runtime/control/control_server_internal.h"
#include "runtime/graph/subgraph_module.h"

namespace vivid {

namespace {

struct ResolvedModuleParamValue {
    float numeric_value = 0.0f;
    std::string string_value;
    bool has_string_value = false;
};

static bool is_modulated_module_param(const NodeDef& authored_node,
                                      const std::string& exposed_param,
                                      const std::vector<ModulationLoweringRecord>& modulation_records) {
    for (const auto& rec : modulation_records) {
        if (rec.instance_id == authored_node.id && rec.exposed_param == exposed_param)
            return true;
    }
    return false;
}

static ResolvedModuleParamValue resolve_module_param_value(
        const NodeDef& authored_node,
        const SubgraphParamBinding& binding,
        const ui::ParamInfo& param_info,
        const CompiledGraph* compiled_graph,
        const std::vector<ModulationLoweringRecord>& modulation_records) {
    ResolvedModuleParamValue resolved;
    resolved.numeric_value = param_info.default_value;

    const std::string flat_id = authored_node.id + ".__" + binding.internal_node;
    const CompiledNode* internal_node = compiled_graph ? compiled_graph->find_node(flat_id) : nullptr;

    // File/text params keep their current string lookup behavior.
    if (param_info.type == VIVID_PARAM_FILE || param_info.type == VIVID_PARAM_TEXT) {
        if (internal_node) {
            auto fi = internal_node->file_param_indices.find(binding.internal_param);
            if (fi != internal_node->file_param_indices.end() &&
                fi->second < internal_node->file_param_storage.size()) {
                resolved.string_value = internal_node->file_param_storage[fi->second];
                resolved.has_string_value = true;
                return resolved;
            }
        }
        auto sit = authored_node.string_params.find(binding.name);
        if (sit != authored_node.string_params.end()) {
            resolved.string_value = sit->second;
            resolved.has_string_value = true;
            return resolved;
        }
    }

    if (is_modulated_module_param(authored_node, binding.name, modulation_records)) {
        auto ait = authored_node.params.find(binding.name);
        if (ait != authored_node.params.end())
            resolved.numeric_value = ait->second;
        return resolved;
    }

    if (internal_node) {
        auto iit = internal_node->param_indices.find(binding.internal_param);
        if (iit != internal_node->param_indices.end() &&
            iit->second < internal_node->param_values.size()) {
            resolved.numeric_value = internal_node->param_values[iit->second];
            return resolved;
        }
    }

    auto ait = authored_node.params.find(binding.name);
    if (ait != authored_node.params.end())
        resolved.numeric_value = ait->second;

    return resolved;
}

} // namespace

std::string handle_inspect_graph(Graph& graph, RuntimeCore& core, const SubgraphModuleRegistry* modules) {
    const auto* cg = core.compiled_graph();
    std::unordered_map<std::string, const CompiledNode*> state_map;
    if (cg) {
        for (const auto& cn : cg->nodes)
            state_map[cn.node_id] = &cn;
    }

    nlohmann::json result = nlohmann::json::object();

    // -- Nodes --
    nlohmann::json nodes_arr = nlohmann::json::array();
    for (const auto& ndef : graph.nodes()) {
        nlohmann::json node = nlohmann::json::object();
        node["id"] = ndef.id;
        node["type"] = ndef.type;

        auto sit = state_map.find(ndef.id);
        const CompiledNode* ns = (sit != state_map.end()) ? sit->second : nullptr;
        const VividOperatorDescriptor* desc =
            (ns && ns->loader) ? ns->loader->descriptor() : nullptr;

        // Health — surface errors and missing operators
        if (ns && (ns->errored || ns->missing_operator)) {
            nlohmann::json health = nlohmann::json::object();
            health["errored"] = ns->errored;
            health["message"] = ns->error_message;
            health["missing_operator"] = ns->missing_operator;
            if (!ns->missing_operator_reason.empty())
                health["reason"] = ns->missing_operator_reason;
            if (!ns->missing_operator_detail.empty())
                health["detail"] = ns->missing_operator_detail;
            node["health"] = std::move(health);
        }

        // Params (with live values from runtime)
        nlohmann::json params_arr = nlohmann::json::array();
        const SubgraphModuleDef* mod_def = (!desc && modules) ? modules->find(ndef.type) : nullptr;
        std::shared_ptr<const ui::OperatorInfo> mod_info;
        if (mod_def) mod_info = make_operator_info(*mod_def);

        if (desc) {
            for (uint32_t i = 0; i < desc->param_count; ++i) {
                const auto& pd = desc->params[i];
                nlohmann::json p = nlohmann::json::object();
                p["name"] = pd.name;
                p["type"] = param_type_str(pd.type);
                float value = pd.default_value;
                if (ns) {
                    auto pi = ns->param_indices.find(pd.name);
                    if (pi != ns->param_indices.end())
                        value = ns->param_values[pi->second];
                }
                p["value"] = static_cast<double>(value);
                p["min"] = static_cast<double>(pd.min_value);
                p["max"] = static_cast<double>(pd.max_value);
                p["default"] = static_cast<double>(pd.default_value);
                if (pd.semantic_tag)
                    p["semantic_tag"] = pd.semantic_tag;
                if (pd.semantic_shape)
                    p["semantic_shape"] = pd.semantic_shape;
                if (pd.semantic_unit)
                    p["semantic_unit"] = pd.semantic_unit;
                if (pd.semantic_intent)
                    p["semantic_intent"] = pd.semantic_intent;
                if (pd.description)
                    p["description"] = pd.description;
                if (pd.choice_count > 0 && pd.choice_labels) {
                    nlohmann::json choices = nlohmann::json::array();
                    for (uint32_t c = 0; c < pd.choice_count; ++c)
                        choices.push_back(pd.choice_labels[c]);
                    p["choices"] = std::move(choices);
                }
                if ((pd.type == VIVID_PARAM_FILE || pd.type == VIVID_PARAM_TEXT) && ns) {
                    auto fi = ns->file_param_indices.find(pd.name);
                    if (fi != ns->file_param_indices.end()) {
                        p["string_value"] = ns->file_param_storage[fi->second];
                    }
                }
                params_arr.push_back(std::move(p));
            }
        } else if (mod_info) {
            // Module instance: emit params from module definition with live values
            for (size_t i = 0; i < mod_info->params.size(); ++i) {
                const auto& pi = mod_info->params[i];
                nlohmann::json p = nlohmann::json::object();
                p["name"] = pi.name;
                p["type"] = param_type_str(pi.type);
                const auto& pb = mod_def->params[i];
                auto resolved = resolve_module_param_value(ndef, pb, pi, cg, core.modulation_records());
                p["value"] = static_cast<double>(resolved.numeric_value);
                p["min"] = static_cast<double>(pi.min_value);
                p["max"] = static_cast<double>(pi.max_value);
                p["default"] = static_cast<double>(pi.default_value);
                if (!pi.group.empty()) p["group"] = pi.group;
                if (!pi.description.empty()) p["description"] = pi.description;
                if (!pi.semantic_tag.empty()) p["semantic_tag"] = pi.semantic_tag;
                if (!pi.semantic_shape.empty()) p["semantic_shape"] = pi.semantic_shape;
                if (!pi.semantic_unit.empty()) p["semantic_unit"] = pi.semantic_unit;
                if (!pi.semantic_intent.empty()) p["semantic_intent"] = pi.semantic_intent;
                if (!pi.performance_page.empty()) p["performance_page"] = pi.performance_page;
                if (pi.performance_order >= 0) p["performance_order"] = pi.performance_order;
                if (!pi.performance_role.empty()) p["performance_role"] = pi.performance_role;
                if (!pi.choice_labels.empty()) {
                    nlohmann::json choices = nlohmann::json::array();
                    for (const auto& c : pi.choice_labels) choices.push_back(c);
                    p["choices"] = std::move(choices);
                }
                if (resolved.has_string_value)
                    p["string_value"] = resolved.string_value;
                params_arr.push_back(std::move(p));
            }
        }
        node["params"] = std::move(params_arr);

        // Ports split into inputs / outputs
        nlohmann::json inputs_arr = nlohmann::json::array();
        nlohmann::json outputs_arr = nlohmann::json::array();
        if (desc) {
            for (uint32_t i = 0; i < desc->port_count; ++i) {
                const auto& pd = desc->ports[i];
                nlohmann::json p = nlohmann::json::object();
                p["name"] = pd.name;
                p["type"] = port_type_str(pd.type);
                p["transport"] = transport_str(pd.transport);
                if (pd.type_name)
                    p["type_name"] = pd.type_name;
                if (pd.stable_type_id)
                    p["stable_type_id"] = pd.stable_type_id;
                if (pd.payload_size > 0)
                    p["payload_size"] = pd.payload_size;

                if (pd.direction == VIVID_PORT_OUTPUT && ns) {
                    auto oi = ns->output_port_indices.find(pd.name);
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_values.size()) {
                        p["current_value"] = static_cast<double>(ns->output_values[oi->second]);
                    }
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_string_values.size() &&
                        !ns->output_string_values[oi->second].empty()) {
                        p["current_string"] = ns->output_string_values[oi->second];
                    }
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_lanes.size() &&
                        !ns->output_lanes[oi->second].empty()) {
                        nlohmann::json lane_arr = nlohmann::json::array();
                        for (float sv : ns->output_lanes[oi->second])
                            lane_arr.push_back(static_cast<double>(sv));
                        p["lane_array"] = std::move(lane_arr);
                    }
                    if (oi != ns->output_port_indices.end() &&
                        oi->second < ns->output_string_lanes.size() &&
                        !ns->output_string_lanes[oi->second].empty()) {
                        nlohmann::json lane_arr = nlohmann::json::array();
                        for (const auto& sv : ns->output_string_lanes[oi->second])
                            lane_arr.push_back(sv);
                        p["string_lanes"] = std::move(lane_arr);
                    }
                }

                if (pd.direction == VIVID_PORT_INPUT && ns) {
                    auto ii = ns->input_port_indices.find(pd.name);
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_values.size()) {
                        p["current_value"] = static_cast<double>(ns->input_values[ii->second]);
                    }
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_string_values.size() &&
                        !ns->input_string_values[ii->second].empty()) {
                        p["current_string"] = ns->input_string_values[ii->second];
                    }
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_lanes.size() &&
                        !ns->input_lanes[ii->second].empty()) {
                        nlohmann::json lane_arr = nlohmann::json::array();
                        for (float sv : ns->input_lanes[ii->second])
                            lane_arr.push_back(static_cast<double>(sv));
                        p["lane_array"] = std::move(lane_arr);
                    }
                    if (ii != ns->input_port_indices.end() &&
                        ii->second < ns->input_string_lanes.size() &&
                        !ns->input_string_lanes[ii->second].empty()) {
                        nlohmann::json lane_arr = nlohmann::json::array();
                        for (const auto& sv : ns->input_string_lanes[ii->second])
                            lane_arr.push_back(sv);
                        p["string_lanes"] = std::move(lane_arr);
                    }
                }

                if (pd.direction == VIVID_PORT_INPUT)
                    inputs_arr.push_back(std::move(p));
                else
                    outputs_arr.push_back(std::move(p));
            }
        } else if (mod_def) {
            // Module instance: emit ports with live data from internal nodes
            for (const auto& port : mod_def->ports) {
                nlohmann::json p = nlohmann::json::object();
                p["name"] = port.name;
                p["type"] = port_type_str(port.type);

                // Resolve internal compiled node for live port data
                std::string flat_id = ndef.id + ".__" + port.internal_node;
                const auto* icn = cg ? cg->find_node(flat_id) : nullptr;
                if (icn && port.direction == VIVID_PORT_OUTPUT) {
                    auto oi = icn->output_port_indices.find(port.internal_port);
                    if (oi != icn->output_port_indices.end()) {
                        if (oi->second < icn->output_values.size())
                            p["current_value"] = static_cast<double>(icn->output_values[oi->second]);
                        if (oi->second < icn->output_string_values.size() &&
                            !icn->output_string_values[oi->second].empty())
                            p["current_string"] = icn->output_string_values[oi->second];
                        if (oi->second < icn->output_lanes.size() &&
                            !icn->output_lanes[oi->second].empty()) {
                            nlohmann::json lane_arr = nlohmann::json::array();
                            for (float sv : icn->output_lanes[oi->second])
                                lane_arr.push_back(static_cast<double>(sv));
                            p["lane_array"] = std::move(lane_arr);
                        }
                        if (oi->second < icn->output_string_lanes.size() &&
                            !icn->output_string_lanes[oi->second].empty()) {
                            nlohmann::json lane_arr = nlohmann::json::array();
                            for (const auto& sv : icn->output_string_lanes[oi->second])
                                lane_arr.push_back(sv);
                            p["string_lanes"] = std::move(lane_arr);
                        }
                    }
                } else if (icn && port.direction == VIVID_PORT_INPUT) {
                    auto ii = icn->input_port_indices.find(port.internal_port);
                    if (ii != icn->input_port_indices.end()) {
                        if (ii->second < icn->input_values.size())
                            p["current_value"] = static_cast<double>(icn->input_values[ii->second]);
                        if (ii->second < icn->input_string_values.size() &&
                            !icn->input_string_values[ii->second].empty())
                            p["current_string"] = icn->input_string_values[ii->second];
                        if (ii->second < icn->input_lanes.size() &&
                            !icn->input_lanes[ii->second].empty()) {
                            nlohmann::json lane_arr = nlohmann::json::array();
                            for (float sv : icn->input_lanes[ii->second])
                                lane_arr.push_back(static_cast<double>(sv));
                            p["lane_array"] = std::move(lane_arr);
                        }
                        if (ii->second < icn->input_string_lanes.size() &&
                            !icn->input_string_lanes[ii->second].empty()) {
                            nlohmann::json lane_arr = nlohmann::json::array();
                            for (const auto& sv : icn->input_string_lanes[ii->second])
                                lane_arr.push_back(sv);
                            p["string_lanes"] = std::move(lane_arr);
                        }
                    }
                }

                if (port.direction == VIVID_PORT_INPUT)
                    inputs_arr.push_back(std::move(p));
                else
                    outputs_arr.push_back(std::move(p));
            }
        }
        // Append runtime-injected GPU analysis ports.
        if (ns && ns->gpu) {
            static const char* kGpuAnalysisPorts[] = {
                "frame_hash", "brightness", "contrast", "dominant_hue"
            };
            for (const char* name : kGpuAnalysisPorts) {
                auto oit = ns->output_port_indices.find(name);
                if (oit != ns->output_port_indices.end() &&
                    oit->second < ns->output_values.size()) {
                    nlohmann::json p = nlohmann::json::object();
                    p["name"] = name;
                    p["type"] = "float";
                    p["transport"] = "scalar";
                    p["current_value"] = static_cast<double>(ns->output_values[oit->second]);
                    outputs_arr.push_back(std::move(p));
                }
            }
        }

        node["inputs"] = std::move(inputs_arr);
        node["outputs"] = std::move(outputs_arr);

        // Modulation sources, destinations, and assignments (module instances only)
        if (mod_def) {
            if (!mod_def->mod_sources.empty()) {
                nlohmann::json src_arr = nlohmann::json::array();
                for (const auto& s : mod_def->mod_sources) {
                    nlohmann::json obj;
                    obj["name"] = s.name;
                    if (!s.description.empty()) obj["description"] = s.description;
                    obj["shape"] = s.shape;
                    obj["polarity"] = s.polarity;
                    obj["kind"] = s.kind;
                    if (!s.group.empty()) obj["group"] = s.group;
                    src_arr.push_back(std::move(obj));
                }
                node["mod_sources"] = std::move(src_arr);
            }
            if (!mod_def->mod_destinations.empty()) {
                nlohmann::json dst_arr = nlohmann::json::array();
                for (const auto& d : mod_def->mod_destinations) {
                    nlohmann::json obj;
                    obj["name"] = d.name;
                    if (!d.description.empty()) obj["description"] = d.description;
                    obj["shape"] = d.shape;
                    if (!d.group.empty()) obj["group"] = d.group;
                    dst_arr.push_back(std::move(obj));
                }
                node["mod_destinations"] = std::move(dst_arr);
            }
            const auto* assigns = graph.find_mod_assignments(ndef.id);
            if (assigns && !assigns->empty()) {
                nlohmann::json assign_arr = nlohmann::json::array();
                for (const auto& a : *assigns) {
                    nlohmann::json obj;
                    obj["source"] = a.source;
                    obj["destination"] = a.destination;
                    obj["amount"] = static_cast<double>(a.amount);
                    obj["polarity"] = a.polarity;
                    obj["curve"] = a.curve;
                    assign_arr.push_back(std::move(obj));
                }
                node["mod_assignments"] = std::move(assign_arr);
            }
        }

        // Lane metadata
        if (ns) {
            node["lane_behavior"] = lane_behavior_str(
                static_cast<VividLaneBehavior>(ns->lane_behavior));
        }

        nodes_arr.push_back(std::move(node));
    }
    result["nodes"] = std::move(nodes_arr);

    // -- Connections --
    nlohmann::json conns_arr = nlohmann::json::array();
    for (const auto& conn : graph.connections()) {
        nlohmann::json c = nlohmann::json::object();
        std::string from_addr = conn.from_node + "/" + conn.from_port;
        std::string to_addr = conn.to_node + "/" + conn.to_port;
        c["from"] = from_addr;
        c["to"] = to_addr;
        if (conn.has_remap()) {
            c["from_min"] = conn.from_min;
            c["from_max"] = conn.from_max;
            c["to_min"] = conn.to_min;
            c["to_max"] = conn.to_max;
            if (conn.clamp)
                c["clamp"] = true;
        }
        if (conn.has_bridge())
            c["bridge"] = conn.bridge;
        // Lane metadata from compiled edge (match by node + port)
        if (cg) {
            for (const auto& e : cg->edges) {
                if (e.from_node >= cg->nodes.size() || e.to_node >= cg->nodes.size())
                    continue;
                const auto& fn = cg->nodes[e.from_node];
                const auto& tn = cg->nodes[e.to_node];
                if (fn.node_id != conn.from_node || tn.node_id != conn.to_node)
                    continue;
                // Match port indices to port names
                bool from_match = false, to_match = false;
                for (const auto& [name, idx] : fn.output_port_indices)
                    if (idx == e.from_port && name == conn.from_port) { from_match = true; break; }
                if (!from_match) {
                    for (const auto& [name, idx] : fn.param_indices)
                        if (idx == e.from_port && name == conn.from_port) { from_match = true; break; }
                }
                if (e.targets_param) {
                    for (const auto& [name, idx] : tn.param_indices)
                        if (idx == e.to_port && name == conn.to_port) { to_match = true; break; }
                } else {
                    for (const auto& [name, idx] : tn.input_port_indices)
                        if (idx == e.to_port && name == conn.to_port) { to_match = true; break; }
                }
                if (from_match && to_match) {
                    c["transport"] = (e.transport == vivid::EdgeTransport::Snapshot) ? "snapshot" : "direct";
                    if (e.bridge_kind != vivid::BridgeKind::None) {
                        static const char* bk_names[] = {"none","hold","snapshot","last_sample","rms","peak","waveform"};
                        c["bridge_kind"] = bk_names[static_cast<int>(e.bridge_kind)];
                    }
                    if (e.lane_set_id != 0)
                        c["lane_set_id"] = e.lane_set_id;
                    if (e.lane_count > 1)
                        c["lane_count"] = e.lane_count;
                    break;
                }
            }
        }
        conns_arr.push_back(std::move(c));
    }
    result["connections"] = std::move(conns_arr);

    // -- Graph metadata --
    if (!graph.meta().empty()) {
        const auto& gm = graph.meta();
        nlohmann::json meta = nlohmann::json::object();
        if (!gm.id.empty()) meta["id"] = gm.id;
        if (!gm.title.empty()) meta["title"] = gm.title;
        if (!gm.description.empty()) meta["description"] = gm.description;
        if (!gm.tags.empty()) {
            nlohmann::json tags_arr = nlohmann::json::array();
            for (const auto& t : gm.tags) tags_arr.push_back(t);
            meta["tags"] = std::move(tags_arr);
        }
        if (!gm.difficulty.empty()) meta["difficulty"] = gm.difficulty;
        if (!gm.domains.empty()) {
            nlohmann::json dom_arr = nlohmann::json::array();
            for (const auto& d : gm.domains) dom_arr.push_back(d);
            meta["domains"] = std::move(dom_arr);
        }
        if (!gm.requires_packages.empty()) {
            nlohmann::json req_arr = nlohmann::json::array();
            for (const auto& pkg : gm.requires_packages) req_arr.push_back(pkg);
            meta["requires_packages"] = std::move(req_arr);
        }
        if (gm.featured_rank >= 0) meta["featured_rank"] = gm.featured_rank;
        if (gm.estimated_minutes >= 0) meta["estimated_minutes"] = gm.estimated_minutes;
        if (!gm.content_kind.empty()) meta["content_kind"] = gm.content_kind;
        if (!gm.category.empty()) meta["category"] = gm.category;
        if (!gm.family.empty()) meta["family"] = gm.family;
        if (!gm.role.empty()) meta["role"] = gm.role;
        if (!gm.playability.empty()) meta["playability"] = gm.playability;
        if (!gm.preview_controls.empty()) {
            nlohmann::json preview_arr = nlohmann::json::array();
            for (const auto& ctrl : gm.preview_controls) {
                nlohmann::json item = nlohmann::json::object();
                item["node"] = ctrl.node;
                item["param"] = ctrl.param;
                if (!ctrl.label.empty()) item["label"] = ctrl.label;
                preview_arr.push_back(std::move(item));
            }
            meta["preview_controls"] = std::move(preview_arr);
        }
        result["meta"] = std::move(meta);
    }

    return json_ok(std::move(result));
}

static const CompiledNode* find_node_state(const RuntimeCore& core,
                                            const std::string& node_id) {
    const auto* cg = core.compiled_graph();
    if (!cg) return nullptr;
    return cg->find_node(node_id);
}

nlohmann::json sample_node_outputs_snapshot(const CompiledNode& ns,
                                                    bool include_lanes) {
    nlohmann::json outputs_obj = nlohmann::json::object();
    const VividOperatorDescriptor* desc = ns.loader ? ns.loader->descriptor() : nullptr;
    if (!desc) return outputs_obj;

    for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
        const auto& pd = desc->ports[pi];
        if (pd.direction != VIVID_PORT_OUTPUT) continue;

        nlohmann::json out = nlohmann::json::object();
        out["kind"] = port_type_str(pd.type);
        out["transport"] = transport_str(pd.transport);
        if (pd.type_name)
            out["type_name"] = pd.type_name;
        if (pd.stable_type_id)
            out["stable_type_id"] = pd.stable_type_id;

        auto oit = ns.output_port_indices.find(pd.name);
        if (oit != ns.output_port_indices.end()) {
            const uint32_t oi = oit->second;
            if (oi < ns.output_values.size()) {
                out["scalar"] = static_cast<double>(ns.output_values[oi]);
            }
            if (oi < ns.output_string_values.size() &&
                !ns.output_string_values[oi].empty()) {
                out["string"] = ns.output_string_values[oi];
            }
            if (include_lanes && oi < ns.output_lanes.size() &&
                !ns.output_lanes[oi].empty()) {
                nlohmann::json lane_arr = nlohmann::json::array();
                for (float sv : ns.output_lanes[oi]) {
                    lane_arr.push_back(static_cast<double>(sv));
                }
                out["lane_array"] = std::move(lane_arr);
            }
            if (include_lanes && oi < ns.output_string_lanes.size() &&
                !ns.output_string_lanes[oi].empty()) {
                nlohmann::json lane_arr = nlohmann::json::array();
                for (const auto& sv : ns.output_string_lanes[oi]) {
                    lane_arr.push_back(sv);
                }
                out["string_lanes"] = std::move(lane_arr);
            }
        }

        outputs_obj[pd.name] = std::move(out);
    }

    // Include runtime-injected GPU analysis ports (not in the operator descriptor).
    if (ns.gpu) {
        static const char* kGpuAnalysisPorts[] = {
            "frame_hash", "brightness", "contrast", "dominant_hue"
        };
        for (const char* name : kGpuAnalysisPorts) {
            auto oit = ns.output_port_indices.find(name);
            if (oit != ns.output_port_indices.end() && oit->second < ns.output_values.size()) {
                nlohmann::json out = nlohmann::json::object();
                out["kind"] = "float";
                out["transport"] = "scalar";
                out["scalar"] = static_cast<double>(ns.output_values[oit->second]);
                outputs_obj[name] = std::move(out);
            }
        }
    }

    return outputs_obj;
}

std::string handle_sample_node_outputs(Graph& graph, RuntimeCore& core,
                                              const nlohmann::json& root) {
    if (!root.contains("node_id") || !root["node_id"].is_string()) return json_err("missing 'node_id'");
    std::string node_id = root["node_id"].get<std::string>();

    double duration_seconds = 8.0;
    int interval_ms = 250;
    bool include_lanes = true;

    if (root.contains("duration_seconds") && root["duration_seconds"].is_number())
        duration_seconds = root["duration_seconds"].get<double>();
    if (root.contains("interval_ms") && root["interval_ms"].is_number())
        interval_ms = root["interval_ms"].get<int>();
    if (root.contains("include_lanes") && root["include_lanes"].is_boolean())
        include_lanes = root["include_lanes"].get<bool>();

    duration_seconds = std::clamp(duration_seconds, 0.0, 60.0);
    interval_ms = std::clamp(interval_ms, 10, 5000);

    const CompiledNode* initial = find_node_state(core, node_id);
    if (!initial) return json_err("node not found");
    if (!initial->loader || !initial->loader->descriptor()) {
        return json_err("node has no live descriptor");
    }

    nlohmann::json result = nlohmann::json::object();
    result["node_id"] = node_id;
    result["type"] = initial->type_name;
    result["kind"] = kind_str(initial->operator_kind);
    result["active_cadence"] = (initial->active_cadence == vivid::Cadence::Audio) ? "audio" : "frame";
    result["duration_seconds"] = duration_seconds;
    result["interval_ms"] = interval_ms;
    result["include_lanes"] = include_lanes;

    nlohmann::json samples_arr = nlohmann::json::array();
    const auto start = std::chrono::steady_clock::now();
    const auto end = start + std::chrono::duration<double>(duration_seconds);
    auto next_sample = start;
    int sample_count = 0;

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        const CompiledNode* ns = find_node_state(core, node_id);
        if (!ns) {
            return json_err("node disappeared during sampling");
        }

        nlohmann::json sample = nlohmann::json::object();
        const double t = std::chrono::duration<double>(now - start).count();
        sample["time_seconds"] = t;
        sample["outputs"] = sample_node_outputs_snapshot(*ns, include_lanes);
        samples_arr.push_back(std::move(sample));
        ++sample_count;

        if (now >= end) break;
        next_sample += std::chrono::milliseconds(interval_ms);
        std::this_thread::sleep_until(next_sample);
    }

    result["sample_count"] = sample_count;
    result["samples"] = std::move(samples_arr);
    return json_ok(std::move(result));
}

std::string handle_introspect_nodes(Graph& graph, RuntimeCore& core, const SubgraphModuleRegistry* modules) {
    std::unordered_map<std::string, const NodeDef*> def_map;
    for (const auto& ndef : graph.nodes())
        def_map[ndef.id] = &ndef;
    std::unordered_map<std::string, int> incoming_wires;
    std::unordered_map<std::string, int> outgoing_wires;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> incoming_port_wires;
    std::unordered_map<std::string, std::unordered_map<std::string, int>> outgoing_port_wires;
    for (const auto& conn : graph.connections()) {
        incoming_wires[conn.to_node]++;
        outgoing_wires[conn.from_node]++;
        incoming_port_wires[conn.to_node][conn.to_port]++;
        outgoing_port_wires[conn.from_node][conn.from_port]++;
    }

    nlohmann::json result_obj = nlohmann::json::object();
    nlohmann::json nodes_arr = nlohmann::json::array();

    const auto* cg = core.compiled_graph();
    if (!cg) {
        result_obj["nodes"] = std::move(nodes_arr);
        return nlohmann::json{{"ok", true}, {"schema_version", 1}, {"result", std::move(result_obj)}}.dump();
    }
    const auto& nodes = cg->nodes;
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
        const auto& ns = nodes[ni];
        nlohmann::json node = nlohmann::json::object();
        node["node_id"] = ns.node_id;
        node["node_index"] = static_cast<int64_t>(ni);

        std::string type_name = ns.type_name;
        if (type_name.empty()) {
            auto dit = def_map.find(ns.node_id);
            if (dit != def_map.end() && dit->second)
                type_name = dit->second->type;
        }
        node["type"] = type_name;
        node["kind"] = kind_str(ns.operator_kind);
        node["active_cadence"] = (ns.active_cadence == vivid::Cadence::Audio) ? "audio" : "frame";
        node["incoming_wires"] = static_cast<int64_t>(incoming_wires[ns.node_id]);
        node["outgoing_wires"] = static_cast<int64_t>(outgoing_wires[ns.node_id]);

        // Health
        nlohmann::json health = nlohmann::json::object();
        health["errored"] = (ns.errored || ns.missing_operator);
        health["message"] = ns.error_message;
        health["missing_operator"] = ns.missing_operator;
        if (!ns.missing_operator_reason.empty())
            health["reason"] = ns.missing_operator_reason;
        if (!ns.missing_operator_detail.empty())
            health["detail"] = ns.missing_operator_detail;
        node["health"] = std::move(health);

        const VividOperatorDescriptor* desc = ns.loader ? ns.loader->descriptor() : nullptr;

        // Current params
        nlohmann::json params_obj = nlohmann::json::object();
        if (desc) {
            for (uint32_t pi = 0; pi < desc->param_count; ++pi) {
                const auto& pd = desc->params[pi];
                if (pi < ns.param_values.size())
                    params_obj[pd.name] = static_cast<double>(ns.param_values[pi]);
            }
            for (const auto& [name, idx] : ns.file_param_indices) {
                if (idx < ns.file_param_storage.size())
                    params_obj[name] = ns.file_param_storage[idx];
            }
        } else {
            auto dit = def_map.find(ns.node_id);
            if (dit != def_map.end() && dit->second) {
                for (const auto& [k, v] : dit->second->params)
                    params_obj[k] = static_cast<double>(v);
                for (const auto& [k, v] : dit->second->string_params)
                    params_obj[k] = v;
            }
        }
        node["params"] = std::move(params_obj);

        // Param metadata
        nlohmann::json param_meta_arr = nlohmann::json::array();
        if (desc) {
            for (uint32_t pi = 0; pi < desc->param_count; ++pi) {
                const auto& pd = desc->params[pi];
                nlohmann::json pm = nlohmann::json::object();
                pm["name"] = pd.name;
                pm["kind"] = param_type_str(pd.type);
                pm["default"] = static_cast<double>(pd.default_value);
                pm["min"] = static_cast<double>(pd.min_value);
                pm["max"] = static_cast<double>(pd.max_value);
                if (pd.semantic_tag)
                    pm["semantic_tag"] = pd.semantic_tag;
                if (pd.semantic_shape)
                    pm["semantic_shape"] = pd.semantic_shape;
                if (pd.semantic_unit)
                    pm["semantic_unit"] = pd.semantic_unit;
                if (pd.semantic_intent)
                    pm["semantic_intent"] = pd.semantic_intent;
                if (pd.description)
                    pm["description"] = pd.description;
                param_meta_arr.push_back(std::move(pm));
            }
        }
        node["param_meta"] = std::move(param_meta_arr);

        // Input summary
        nlohmann::json inputs_arr = nlohmann::json::array();
        if (desc) {
            for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
                const auto& pd = desc->ports[pi];
                if (pd.direction != VIVID_PORT_INPUT) continue;

                nlohmann::json in = nlohmann::json::object();
                in["name"] = pd.name;
                in["kind"] = port_type_str(pd.type);
                in["transport"] = transport_str(pd.transport);
                if (pd.type_name)
                    in["type_name"] = pd.type_name;
                if (pd.stable_type_id)
                    in["stable_type_id"] = pd.stable_type_id;
                if (pd.payload_size > 0)
                    in["payload_size"] = static_cast<int64_t>(pd.payload_size);
                in["connected_wires"] = static_cast<int64_t>(incoming_port_wires[ns.node_id][pd.name]);

                auto iit = ns.input_port_indices.find(pd.name);
                if (iit != ns.input_port_indices.end()) {
                    uint32_t ii = iit->second;
                    if (ii < ns.input_values.size()) {
                        in["scalar"] = static_cast<double>(ns.input_values[ii]);
                    }
                    if (ii < ns.input_string_values.size() &&
                        !ns.input_string_values[ii].empty()) {
                        in["string"] = ns.input_string_values[ii];
                    }
                    if (ii < ns.input_lanes.size()) {
                        in["lane_array"] = nlohmann::json{{"length", static_cast<int64_t>(ns.input_lanes[ii].size())}};
                    }
                    if (ii < ns.input_string_lanes.size()) {
                        in["string_lanes"] = nlohmann::json{{"length", static_cast<int64_t>(ns.input_string_lanes[ii].size())}};
                    }
                }
                inputs_arr.push_back(std::move(in));
            }
        }
        node["inputs"] = std::move(inputs_arr);

        // Output summary
        nlohmann::json outputs_arr = nlohmann::json::array();
        if (desc) {
            for (uint32_t pi = 0; pi < desc->port_count; ++pi) {
                const auto& pd = desc->ports[pi];
                if (pd.direction != VIVID_PORT_OUTPUT) continue;

                nlohmann::json out = nlohmann::json::object();
                out["name"] = pd.name;
                out["kind"] = port_type_str(pd.type);
                out["transport"] = transport_str(pd.transport);
                if (pd.type_name)
                    out["type_name"] = pd.type_name;
                if (pd.stable_type_id)
                    out["stable_type_id"] = pd.stable_type_id;
                if (pd.payload_size > 0)
                    out["payload_size"] = static_cast<int64_t>(pd.payload_size);
                out["connected_wires"] = static_cast<int64_t>(outgoing_port_wires[ns.node_id][pd.name]);

                auto oit = ns.output_port_indices.find(pd.name);
                if (oit != ns.output_port_indices.end()) {
                    uint32_t oi = oit->second;
                    if (oi < ns.output_values.size())
                        out["scalar"] = static_cast<double>(ns.output_values[oi]);
                    if (oi < ns.output_string_values.size() &&
                        !ns.output_string_values[oi].empty()) {
                        out["string"] = ns.output_string_values[oi];
                    }
                    if (oi < ns.output_lanes.size()) {
                        out["lane_array"] = nlohmann::json{{"length", static_cast<int64_t>(ns.output_lanes[oi].size())}};
                    }
                    if (oi < ns.output_string_lanes.size()) {
                        out["string_lanes"] = nlohmann::json{{"length", static_cast<int64_t>(ns.output_string_lanes[oi].size())}};
                    }
                }

                if (pd.type == VIVID_PORT_TEXTURE && ns.gpu && ns.gpu->tex_width > 0 && ns.gpu->tex_height > 0) {
                    out["width"] = ns.gpu->tex_width;
                    out["height"] = ns.gpu->tex_height;
                }
                outputs_arr.push_back(std::move(out));
            }
        }
        node["outputs"] = std::move(outputs_arr);

        // Environment metrics (lightweight first pass)
        nlohmann::json env_metrics = nlohmann::json::object();
        if (ns.is_gpu()) {
            nlohmann::json gpu = nlohmann::json::object();
            gpu["width"] = ns.gpu->tex_width;
            gpu["height"] = ns.gpu->tex_height;
            gpu["has_texture"] = (ns.gpu->texture != nullptr);
            gpu["aux_texture_count"] = static_cast<int64_t>(ns.gpu->aux_gpu_texture_views.size());
            env_metrics["gpu"] = std::move(gpu);
        } else if (ns.active_cadence == vivid::Cadence::Audio) {
            nlohmann::json audio = nlohmann::json::object();
            audio["output_port_count"] = ns.output_port_count;
            audio["input_port_count"] = ns.input_port_count;
            auto rms_it = ns.output_port_indices.find("rms");
            if (rms_it != ns.output_port_indices.end() &&
                rms_it->second < ns.output_values.size()) {
                audio["rms"] = static_cast<double>(ns.output_values[rms_it->second]);
            }
            auto peak_it = ns.output_port_indices.find("peak");
            if (peak_it != ns.output_port_indices.end() &&
                peak_it->second < ns.output_values.size()) {
                audio["peak"] = static_cast<double>(ns.output_values[peak_it->second]);
            }
            auto wave_it = ns.output_port_indices.find("waveform");
            if (wave_it != ns.output_port_indices.end() &&
                wave_it->second < ns.output_lanes.size()) {
                const auto& wave = ns.output_lanes[wave_it->second];
                audio["waveform_length"] = static_cast<int64_t>(wave.size());
                nlohmann::json preview = nlohmann::json::array();
                size_t preview_count = wave.size();
                if (preview_count > 32) preview_count = 32;
                for (size_t wi = 0; wi < preview_count; ++wi) {
                    preview.push_back(static_cast<double>(wave[wi]));
                }
                audio["waveform_preview"] = std::move(preview);
            }
            env_metrics["audio"] = std::move(audio);
        } else {
            nlohmann::json control = nlohmann::json::object();
            int64_t lane_out_nonempty = 0;
            int64_t scalar_out_nonzero = 0;
            for (const auto& sp : ns.output_lanes)
                if (!sp.empty()) lane_out_nonempty++;
            for (float v : ns.output_values)
                if (v != 0.0f) scalar_out_nonzero++;
            control["non_empty_lane_outputs"] = lane_out_nonempty;
            control["non_zero_scalar_outputs"] = scalar_out_nonzero;
            env_metrics["control"] = std::move(control);
        }
        node["env_metrics"] = std::move(env_metrics);

        nodes_arr.push_back(std::move(node));
    }

    // Append module instance entries from authored graph
    if (modules) {
        for (const auto& ndef : graph.nodes()) {
            const auto* mod = modules->find(ndef.type);
            if (!mod) continue;
            auto info = make_operator_info(*mod);

            nlohmann::json node = nlohmann::json::object();
            node["node_id"] = ndef.id;
            node["type"] = mod->name;
            node["kind"] = "module";
            node["is_module"] = true;
            node["active_cadence"] = "frame";
            node["incoming_wires"] = static_cast<int64_t>(incoming_wires[ndef.id]);
            node["outgoing_wires"] = static_cast<int64_t>(outgoing_wires[ndef.id]);
            node["health"] = nlohmann::json{{"errored", false}, {"message", ""},
                                             {"missing_operator", false}};

            // Params with live values from internal nodes
            nlohmann::json params_obj = nlohmann::json::object();
            for (size_t i = 0; i < mod->params.size(); ++i) {
                const auto& pb = mod->params[i];
                auto resolved = resolve_module_param_value(
                    ndef, pb, info->params[i], cg, core.modulation_records());
                if (resolved.has_string_value) {
                    params_obj[pb.name] = resolved.string_value;
                    continue;
                }
                params_obj[pb.name] = static_cast<double>(resolved.numeric_value);
            }
            node["params"] = std::move(params_obj);

            // Param metadata
            nlohmann::json param_meta_arr = nlohmann::json::array();
            for (const auto& pi : info->params) {
                nlohmann::json pm = nlohmann::json::object();
                pm["name"] = pi.name;
                pm["kind"] = param_type_str(pi.type);
                pm["default"] = static_cast<double>(pi.default_value);
                pm["min"] = static_cast<double>(pi.min_value);
                pm["max"] = static_cast<double>(pi.max_value);
                if (!pi.group.empty()) pm["group"] = pi.group;
                if (!pi.description.empty()) pm["description"] = pi.description;
                if (!pi.semantic_tag.empty()) pm["semantic_tag"] = pi.semantic_tag;
                if (!pi.semantic_shape.empty()) pm["semantic_shape"] = pi.semantic_shape;
                if (!pi.semantic_unit.empty()) pm["semantic_unit"] = pi.semantic_unit;
                if (!pi.semantic_intent.empty()) pm["semantic_intent"] = pi.semantic_intent;
                if (!pi.performance_page.empty()) pm["performance_page"] = pi.performance_page;
                if (pi.performance_order >= 0) pm["performance_order"] = pi.performance_order;
                if (!pi.performance_role.empty()) pm["performance_role"] = pi.performance_role;
                param_meta_arr.push_back(std::move(pm));
            }
            node["param_meta"] = std::move(param_meta_arr);

            // Ports with live data from internal nodes
            nlohmann::json inputs_arr = nlohmann::json::array();
            nlohmann::json outputs_arr = nlohmann::json::array();
            for (const auto& port : mod->ports) {
                nlohmann::json p = nlohmann::json::object();
                p["name"] = port.name;
                p["kind"] = port_type_str(port.type);
                p["connected_wires"] = (port.direction == VIVID_PORT_INPUT)
                    ? static_cast<int64_t>(incoming_port_wires[ndef.id][port.name])
                    : static_cast<int64_t>(outgoing_port_wires[ndef.id][port.name]);

                std::string flat_id = ndef.id + ".__" + port.internal_node;
                const auto* icn = cg->find_node(flat_id);
                if (icn && port.direction == VIVID_PORT_OUTPUT) {
                    auto oi = icn->output_port_indices.find(port.internal_port);
                    if (oi != icn->output_port_indices.end()) {
                        if (oi->second < icn->output_values.size())
                            p["scalar"] = static_cast<double>(icn->output_values[oi->second]);
                        if (oi->second < icn->output_string_values.size() &&
                            !icn->output_string_values[oi->second].empty())
                            p["string"] = icn->output_string_values[oi->second];
                        if (oi->second < icn->output_lanes.size())
                            p["lane_array"] = nlohmann::json{{"length", static_cast<int64_t>(icn->output_lanes[oi->second].size())}};
                        if (oi->second < icn->output_string_lanes.size())
                            p["string_lanes"] = nlohmann::json{{"length", static_cast<int64_t>(icn->output_string_lanes[oi->second].size())}};
                    }
                } else if (icn && port.direction == VIVID_PORT_INPUT) {
                    auto ii = icn->input_port_indices.find(port.internal_port);
                    if (ii != icn->input_port_indices.end()) {
                        if (ii->second < icn->input_values.size())
                            p["scalar"] = static_cast<double>(icn->input_values[ii->second]);
                        if (ii->second < icn->input_string_values.size() &&
                            !icn->input_string_values[ii->second].empty())
                            p["string"] = icn->input_string_values[ii->second];
                        if (ii->second < icn->input_lanes.size())
                            p["lane_array"] = nlohmann::json{{"length", static_cast<int64_t>(icn->input_lanes[ii->second].size())}};
                        if (ii->second < icn->input_string_lanes.size())
                            p["string_lanes"] = nlohmann::json{{"length", static_cast<int64_t>(icn->input_string_lanes[ii->second].size())}};
                    }
                }

                if (port.direction == VIVID_PORT_INPUT)
                    inputs_arr.push_back(std::move(p));
                else
                    outputs_arr.push_back(std::move(p));
            }
            node["inputs"] = std::move(inputs_arr);
            node["outputs"] = std::move(outputs_arr);
            node["env_metrics"] = nlohmann::json::object();

            nodes_arr.push_back(std::move(node));
        }
    }

    result_obj["nodes"] = std::move(nodes_arr);

    return nlohmann::json{{"ok", true}, {"schema_version", 1}, {"result", std::move(result_obj)}}.dump();
}

std::string handle_get_graph_load_diagnostics(const Graph& graph) {
    nlohmann::json diags_arr = nlohmann::json::array();
    for (const auto& d : graph.load_diagnostics) {
        diags_arr.push_back({
            {"node_id", d.node_id}, {"pkg_name", d.pkg_name},
            {"saved_version", d.saved_version}, {"installed_version", d.installed_version},
            {"classification", d.classification}
        });
    }

    nlohmann::json result_obj = nlohmann::json::object();
    result_obj["graph_load_diagnostics"] = std::move(diags_arr);
    return nlohmann::json{{"ok", true}, {"result", std::move(result_obj)}}.dump();
}

std::string handle_list_types(OperatorRegistry& registry,
                                     PackageManager* package_manager,
                                     OperatorSourceDocs& source_docs,
                                     const nlohmann::json& root,
                                     const SubgraphModuleRegistry* modules) {
    // Optional domain filter: "gpu", "audio", "control"
    std::string domain_filter;
    if (root.contains("domain") && root["domain"].is_string())
        domain_filter = root["domain"].get<std::string>();

    nlohmann::json result = nlohmann::json::object();
    nlohmann::json types_arr = nlohmann::json::array();

    for (const auto& name : registry.type_names()) {
        const auto* desc = registry.probe_descriptor(name);
        if (!desc) continue;

        std::string kind = kind_str(vivid_operator_kind(desc));
        if (!domain_filter.empty() && kind != domain_filter)
            continue;

        nlohmann::json t = nlohmann::json::object();
        t["name"] = desc->name;
        t["kind"] = kind;
        t["lane_behavior"] = lane_behavior_str(desc->lane_behavior);
        t["lane_behavior_help"] = lane_behavior_help_str(desc->lane_behavior);
        nlohmann::json doc_summary = resolve_operator_source_doc(source_docs, registry, package_manager, name);
        if (doc_summary.is_object()) {
            if (doc_summary.contains("brief") && doc_summary["brief"].is_string())
                t["brief"] = doc_summary["brief"];
            if (doc_summary.contains("has_docs") && doc_summary["has_docs"].is_boolean())
                t["has_docs"] = doc_summary["has_docs"];
            if (doc_summary.contains("operator_family") && doc_summary["operator_family"].is_string())
                t["operator_family"] = doc_summary["operator_family"];
        }
        if (!t.contains("has_docs"))
            t["has_docs"] = false;

        types_arr.push_back(std::move(t));
    }

    // Append subgraph module types
    if (modules) {
        for (const auto& mname : modules->type_names()) {
            if (!domain_filter.empty())
                continue;  // modules don't have a single domain; skip if filtering
            const auto* mod = modules->find(mname);
            if (!mod) continue;
            nlohmann::json t = nlohmann::json::object();
            t["name"] = mod->name;
            t["kind"] = "module";
            t["is_module"] = true;
            if (!mod->description.empty()) t["brief"] = mod->description;
            if (!mod->category.empty()) t["category"] = mod->category;
            t["has_docs"] = false;
            types_arr.push_back(std::move(t));
        }
    }

    result["types"] = std::move(types_arr);
    return json_ok(std::move(result));
}

std::string handle_operator_docs(OperatorRegistry& registry,
                                        PackageManager* package_manager,
                                        OperatorSourceDocs& source_docs,
                                        const nlohmann::json& root,
                                        const SubgraphModuleRegistry* modules) {
    if (!root.contains("name") || !root["name"].is_string())
        return json_err("missing 'name'");

    const std::string name = root["name"].get<std::string>();
    const auto* desc = registry.probe_descriptor(name);

    // Fall back to module lookup
    if (!desc && modules) {
        const auto* mod = modules->find(name);
        if (mod) {
            auto info = make_operator_info(*mod);
            return json_ok(build_module_docs_response(*mod, *info));
        }
    }

    if (!desc)
        return json_err("unknown operator: " + name);

    std::string package_name;
    if (root.contains("package") && root["package"].is_string())
        package_name = root["package"].get<std::string>();
    else if (const auto* pkg = registry.package_for_type(name); pkg)
        package_name = *pkg;
    nlohmann::json detail = resolve_operator_source_doc(source_docs, registry,
                                                        package_manager, name, package_name);
    return json_ok(build_operator_docs_response(*desc, detail.is_null() ? nullptr : &detail, package_name));
}

std::string handle_get_registry_diagnostics(OperatorRegistry& registry) {
    nlohmann::json result = nlohmann::json::object();
    result["schema_version"] = 1;

    uint32_t type_count = 0;
    vivid_list_port_types(nullptr, &type_count);
    std::vector<VividPortTypeInfo> port_types(type_count);
    if (type_count > 0)
        vivid_list_port_types(port_types.data(), &type_count);
    port_types.resize(type_count);
    std::sort(port_types.begin(), port_types.end(),
              [](const VividPortTypeInfo& a, const VividPortTypeInfo& b) {
                  const char* a_id = a.stable_type_id ? a.stable_type_id : "";
                  const char* b_id = b.stable_type_id ? b.stable_type_id : "";
                  int cmp = std::strcmp(a_id, b_id);
                  if (cmp != 0) return cmp < 0;
                  return a.type_id < b.type_id;
              });

    nlohmann::json types_arr = nlohmann::json::array();
    for (const auto& info : port_types) {
        nlohmann::json item = nlohmann::json::object();
        item["type_id"] = info.type_id;
        item["transport"] = transport_str(info.transport);
        item["payload_size"] = info.payload_size;
        item["type_name"] = info.type_name;
        item["stable_type_id"] = info.stable_type_id;
        item["audio_safe"] = (info.audio_safe != 0);
        if (info.package_name && *info.package_name)
            item["package_name"] = info.package_name;
        if (info.description && *info.description)
            item["description"] = info.description;
        types_arr.push_back(std::move(item));
    }
    result["custom_port_types"] = std::move(types_arr);

    auto mismatches = registry.abi_mismatch_diagnostics();
    std::sort(mismatches.begin(), mismatches.end(),
              [](const AbiMismatchDiagnostic& a, const AbiMismatchDiagnostic& b) {
                  if (a.package_name != b.package_name) return a.package_name < b.package_name;
                  if (a.plugin_name != b.plugin_name) return a.plugin_name < b.plugin_name;
                  return a.plugin_path < b.plugin_path;
              });
    nlohmann::json mismatches_arr = nlohmann::json::array();
    for (const auto& diag : mismatches) {
        nlohmann::json item = nlohmann::json::object();
        item["plugin_path"] = diag.plugin_path;
        item["plugin_name"] = diag.plugin_name;
        if (!diag.package_name.empty())
            item["package_name"] = diag.package_name;
        item["plugin_abi"] = diag.plugin_abi;
        item["runtime_abi"] = diag.runtime_abi;
        mismatches_arr.push_back(std::move(item));
    }
    result["abi_mismatch_diagnostics"] = std::move(mismatches_arr);

    auto loader_failures = registry.loader_failure_diagnostics();
    std::sort(loader_failures.begin(), loader_failures.end(),
              [](const LoaderFailureDiagnostic& a, const LoaderFailureDiagnostic& b) {
                  if (a.code != b.code) return a.code < b.code;
                  return a.plugin_path < b.plugin_path;
              });
    nlohmann::json failures_arr = nlohmann::json::array();
    for (const auto& diag : loader_failures) {
        nlohmann::json item = nlohmann::json::object();
        item["plugin_path"] = diag.plugin_path;
        item["plugin_name"] = diag.plugin_name;
        if (!diag.package_name.empty())
            item["package_name"] = diag.package_name;
        item["code"] = diag.code;
        item["message"] = diag.message;
        failures_arr.push_back(std::move(item));
    }
    result["loader_failure_diagnostics"] = std::move(failures_arr);

    return json_ok(std::move(result));
}

// ---------------------------------------------------------------------------
// Dispatch — route method name to handler
// ---------------------------------------------------------------------------

} // namespace vivid
