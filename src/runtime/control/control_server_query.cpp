#include "runtime/control/control_server_internal.h"
#include "runtime/graph/subgraph_module.h"

namespace vivid {

namespace {

struct ResolvedModuleParamValue {
    float numeric_value = 0.0f;
    std::string string_value;
    bool has_string_value = false;
};

struct BuildTaskSummary {
    BuildTaskId task_id = 0;
    BuildTaskKind kind = BuildTaskKind::PackageBuild;
    BuildTaskState state = BuildTaskState::Running;
    std::string label;
    std::string summary;
    uint64_t last_timestamp_ms = 0;
    uint64_t last_sequence = 0;
    std::vector<BuildConsoleLine> lines;
};

static const char* build_task_kind_str(BuildTaskKind kind) {
    switch (kind) {
        case BuildTaskKind::HotReload: return "hot_reload";
        case BuildTaskKind::PackageBuild: return "package_build";
        case BuildTaskKind::PackageConfigure: return "package_configure";
        case BuildTaskKind::PackageInstall: return "package_install";
        case BuildTaskKind::PackageTestCompile: return "package_test_compile";
        case BuildTaskKind::PackageTestRun: return "package_test_run";
        case BuildTaskKind::GitClone: return "git_clone";
        default: return "unknown";
    }
}

static const char* build_stream_kind_str(BuildConsoleStreamKind kind) {
    switch (kind) {
        case BuildConsoleStreamKind::Stdout: return "stdout";
        case BuildConsoleStreamKind::Stderr: return "stderr";
        case BuildConsoleStreamKind::System: return "system";
        default: return "unknown";
    }
}

static const char* build_task_state_str(BuildTaskState state) {
    switch (state) {
        case BuildTaskState::Running: return "running";
        case BuildTaskState::Succeeded: return "succeeded";
        case BuildTaskState::Failed: return "failed";
        case BuildTaskState::Cancelled: return "cancelled";
        default: return "unknown";
    }
}

static const char* build_entry_kind_str(BuildConsoleEntryKind kind) {
    switch (kind) {
        case BuildConsoleEntryKind::TaskStart: return "task_start";
        case BuildConsoleEntryKind::Line: return "line";
        case BuildConsoleEntryKind::TaskFinish: return "task_finish";
        default: return "unknown";
    }
}

static std::vector<std::string> json_string_array(const nlohmann::json& root, const char* key) {
    std::vector<std::string> values;
    if (!root.contains(key) || !root[key].is_array()) return values;
    for (const auto& item : root[key]) {
        if (item.is_string())
            values.push_back(item.get<std::string>());
    }
    return values;
}

static std::vector<BuildTaskSummary> collect_build_task_summaries(BuildConsole* build_console) {
    std::vector<BuildTaskSummary> tasks;
    if (!build_console) return tasks;

    auto snapshot = build_console->snapshot();
    std::unordered_map<BuildTaskId, std::size_t> index_by_id;
    for (const auto& line : snapshot.lines) {
        auto it = index_by_id.find(line.task_id);
        if (it == index_by_id.end()) {
            BuildTaskSummary summary;
            summary.task_id = line.task_id;
            summary.kind = line.task_kind;
            summary.state = line.task_state;
            summary.label = line.task_label;
            summary.summary = line.text;
            summary.last_timestamp_ms = line.timestamp_ms;
            summary.last_sequence = line.sequence;
            summary.lines.push_back(line);
            index_by_id[line.task_id] = tasks.size();
            tasks.push_back(std::move(summary));
        } else {
            auto& summary = tasks[it->second];
            summary.kind = line.task_kind;
            summary.state = line.task_state;
            summary.label = line.task_label;
            summary.summary = line.text;
            summary.last_timestamp_ms = line.timestamp_ms;
            summary.last_sequence = line.sequence;
            summary.lines.push_back(line);
        }
    }

    std::sort(tasks.begin(), tasks.end(),
              [](const BuildTaskSummary& a, const BuildTaskSummary& b) {
                  return a.last_sequence > b.last_sequence;
              });
    return tasks;
}

static nlohmann::json build_console_line_json(const BuildConsoleLine& line) {
    return nlohmann::json{
        {"task_id", line.task_id},
        {"entry_kind", build_entry_kind_str(line.entry_kind)},
        {"task_kind", build_task_kind_str(line.task_kind)},
        {"stream_kind", build_stream_kind_str(line.stream_kind)},
        {"task_state", build_task_state_str(line.task_state)},
        {"timestamp_ms", line.timestamp_ms},
        {"sequence", line.sequence},
        {"task_label", line.task_label},
        {"text", line.text},
    };
}

static nlohmann::json top_error_lines_json(const BuildTaskSummary& task, std::size_t max_lines) {
    nlohmann::json lines = nlohmann::json::array();
    std::unordered_set<std::string> seen;
    for (const auto& line : task.lines) {
        const std::string lower = line.text;
        const std::string lower_copy = [&]() {
            std::string tmp = lower;
            std::transform(tmp.begin(), tmp.end(), tmp.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return tmp;
        }();
        const bool looks_error =
            line.stream_kind == BuildConsoleStreamKind::Stderr ||
            lower_copy.find("error") != std::string::npos ||
            lower_copy.find("failed") != std::string::npos ||
            lower_copy.find("fatal") != std::string::npos;
        if (!looks_error) continue;
        if (!seen.insert(line.text).second) continue;
        lines.push_back(build_console_line_json(line));
        if (lines.size() >= max_lines) break;
    }
    return lines;
}

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

std::string handle_inspect_graph(Graph& graph, RuntimeCore& core, const SubgraphModuleRegistry* modules, const std::string& detail) {
    const bool summary = (detail == "summary");
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

        // Params
        const SubgraphModuleDef* mod_def = (!desc && modules) ? modules->find(ndef.type) : nullptr;
        std::shared_ptr<const ui::OperatorInfo> mod_info;
        if (mod_def) mod_info = make_operator_info(*mod_def);

        if (summary) {
            // Flat {name: value} — compact for LLM consumption
            nlohmann::json params_obj = nlohmann::json::object();
            if (desc) {
                for (uint32_t i = 0; i < desc->param_count; ++i) {
                    const auto& pd = desc->params[i];
                    if ((pd.type == VIVID_PARAM_FILE || pd.type == VIVID_PARAM_TEXT) && ns) {
                        auto fi = ns->file_param_indices.find(pd.name);
                        if (fi != ns->file_param_indices.end()) {
                            params_obj[pd.name] = ns->file_param_storage[fi->second];
                            continue;
                        }
                    }
                    float value = pd.default_value;
                    if (ns) {
                        auto pi = ns->param_indices.find(pd.name);
                        if (pi != ns->param_indices.end())
                            value = ns->param_values[pi->second];
                    }
                    params_obj[pd.name] = static_cast<double>(value);
                }
            } else if (mod_info) {
                for (size_t i = 0; i < mod_info->params.size(); ++i) {
                    const auto& pb = mod_def->params[i];
                    const auto& pi = mod_info->params[i];
                    auto resolved = resolve_module_param_value(ndef, pb, pi, cg, core.modulation_records());
                    if (resolved.has_string_value)
                        params_obj[pb.name] = resolved.string_value;
                    else
                        params_obj[pb.name] = static_cast<double>(resolved.numeric_value);
                }
            }
            node["params"] = std::move(params_obj);
        } else {
            // Full params with schema metadata
            nlohmann::json params_arr = nlohmann::json::array();
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
                    if (pd.asset_kind)
                        p["asset_kind"] = pd.asset_kind;
                    if (pd.widget_id && *pd.widget_id)
                        p["widget_id"] = pd.widget_id;
                    if (pd.widget_span > 0)
                        p["widget_span"] = pd.widget_span;
                    add_param_descriptor_visibility(p, pd);
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
                    if (!pi.asset_kind.empty()) p["asset_kind"] = pi.asset_kind;
                    if (!pi.widget_id.empty()) p["widget_id"] = pi.widget_id;
                    if (pi.widget_span > 0) p["widget_span"] = pi.widget_span;
                    add_param_info_visibility(p, pi);
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
        }

        // Ports split into inputs / outputs
        nlohmann::json inputs_arr = nlohmann::json::array();
        nlohmann::json outputs_arr = nlohmann::json::array();
        if (summary) {
            // Compact: just name and type, no live values or lane arrays
            if (desc) {
                for (uint32_t i = 0; i < desc->port_count; ++i) {
                    const auto& pd = desc->ports[i];
                    nlohmann::json p = nlohmann::json::object();
                    p["name"] = pd.name;
                    p["type"] = port_type_str(pd.type);
                    if (pd.direction == VIVID_PORT_INPUT)
                        inputs_arr.push_back(std::move(p));
                    else
                        outputs_arr.push_back(std::move(p));
                }
            } else if (mod_def) {
                for (const auto& port : mod_def->ports) {
                    nlohmann::json p = nlohmann::json::object();
                    p["name"] = port.name;
                    p["type"] = port_type_str(port.type);
                    if (port.direction == VIVID_PORT_INPUT)
                        inputs_arr.push_back(std::move(p));
                    else
                        outputs_arr.push_back(std::move(p));
                }
            }
        } else {
            // Full: all port metadata, live values, lane arrays
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
                            if (oi->second < icn->output_lane_refs.size() &&
                                icn->output_lane_refs[oi->second]) {
                                const auto& ref = icn->output_lane_refs[oi->second];
                                nlohmann::json lane_arr = nlohmann::json::array();
                                for (uint32_t j = 0; j < ref.length(); ++j)
                                    lane_arr.push_back(static_cast<double>(ref.data()[j]));
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
                            if (ii->second < icn->input_lane_refs.size() &&
                                icn->input_lane_refs[ii->second]) {
                                const auto& ref = icn->input_lane_refs[ii->second];
                                nlohmann::json lane_arr = nlohmann::json::array();
                                for (uint32_t j = 0; j < ref.length(); ++j)
                                    lane_arr.push_back(static_cast<double>(ref.data()[j]));
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
        }

        node["inputs"] = std::move(inputs_arr);
        node["outputs"] = std::move(outputs_arr);

        // Modulation and lane metadata (full mode only)
        if (!summary) {
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
        }

        nodes_arr.push_back(std::move(node));
    }
    result["nodes"] = std::move(nodes_arr);

    // -- Connections --
    nlohmann::json conns_arr = nlohmann::json::array();
    for (const auto& conn : graph.connections()) {
        nlohmann::json c = nlohmann::json::object();
        c["from"] = conn.from_node + "/" + conn.from_port;
        c["to"] = conn.to_node + "/" + conn.to_port;
        if (summary) {
            // Compact: just indicate remap/bridge presence
            if (conn.has_remap()) c["has_remap"] = true;
            if (conn.has_bridge()) c["bridge"] = conn.bridge;
        } else {
            // Full: remap values and compiled edge metadata
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

    {
        const auto& metronome = graph.metronome();
        nlohmann::json metro = nlohmann::json::object();
        metro["bpm"] = metronome.bpm;
        metro["beats_per_bar"] = metronome.beats_per_bar;
        if (!graph.quantize_clock_node().empty())
            metro["legacy_quantize_clock"] = graph.quantize_clock_node();
        result["metronome"] = std::move(metro);
    }

    return json_ok(std::move(result));
}

static const CompiledNode* find_node_state(const RuntimeCore& core,
                                            const std::string& node_id) {
    const auto* cg = core.compiled_graph();
    if (!cg) return nullptr;
    return cg->find_node(node_id);
}

static void append_audio_port_debug_fields(nlohmann::json& port_obj,
                                           const CompiledNode& ns,
                                           bool input,
                                           uint32_t port_idx) {
    if (!ns.audio) return;
    auto snap = read_audio_port_debug(*ns.audio, input, port_idx);
    if (!snap.valid) return;
    port_obj["channel_count"] = static_cast<int64_t>(snap.channel_count);
    port_obj["buffer_size"] = static_cast<int64_t>(snap.buffer_size);
    port_obj["last_block_peak"] = static_cast<double>(snap.last_block_peak);
    port_obj["active"] = snap.active;
}

static nlohmann::json make_audio_node_debug_json(const CompiledNode& ns) {
    nlohmann::json obj = nlohmann::json::object();
    if (!ns.audio) return obj;
    auto snap = read_audio_node_debug(*ns.audio);
    if (!snap.valid) return obj;
    obj["last_block_total_us"] = static_cast<int64_t>(snap.last_block_total_us);
    obj["last_process_us"] = static_cast<int64_t>(snap.last_process_us);
    obj["ema_block_us"] = static_cast<int64_t>(snap.ema_block_us);
    obj["last_block_budget_pct"] = static_cast<double>(snap.last_block_budget_pct);
    obj["last_lane_count"] = static_cast<int64_t>(snap.last_lane_count);
    obj["lane_state_entries"] = static_cast<int64_t>(snap.lane_state_entries);
    return obj;
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
            if (include_lanes && oi < ns.output_lane_refs.size() &&
                ns.output_lane_refs[oi]) {
                const auto& ref = ns.output_lane_refs[oi];
                nlohmann::json lane_arr = nlohmann::json::array();
                for (uint32_t j = 0; j < ref.length(); ++j) {
                    lane_arr.push_back(static_cast<double>(ref.data()[j]));
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
            if (pd.type == VIVID_PORT_AUDIO_BUFFER)
                append_audio_port_debug_fields(out, ns, false, oi);
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
        if (ns->audio)
            sample["audio_debug"] = make_audio_node_debug_json(*ns);
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
        if (ns.audio)
            node["audio_debug"] = make_audio_node_debug_json(ns);

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
                if (pd.asset_kind)
                    pm["asset_kind"] = pd.asset_kind;
                if (pd.widget_id && *pd.widget_id)
                    pm["widget_id"] = pd.widget_id;
                if (pd.widget_span > 0)
                    pm["widget_span"] = pd.widget_span;
                add_param_descriptor_visibility(pm, pd);
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
                    if (ii < ns.input_lane_refs.size()) {
                        in["lane_array"] = nlohmann::json{{"length", static_cast<int64_t>(ns.input_lane_refs[ii].length())}};
                    }
                    if (ii < ns.input_string_lanes.size()) {
                        in["string_lanes"] = nlohmann::json{{"length", static_cast<int64_t>(ns.input_string_lanes[ii].size())}};
                    }
                    if (pd.type == VIVID_PORT_AUDIO_BUFFER)
                        append_audio_port_debug_fields(in, ns, true, ii);
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
                    if (oi < ns.output_lane_refs.size()) {
                        out["lane_array"] = nlohmann::json{{"length", static_cast<int64_t>(ns.output_lane_refs[oi].length())}};
                    }
                    if (oi < ns.output_string_lanes.size()) {
                        out["string_lanes"] = nlohmann::json{{"length", static_cast<int64_t>(ns.output_string_lanes[oi].size())}};
                    }
                    if (pd.type == VIVID_PORT_AUDIO_BUFFER)
                        append_audio_port_debug_fields(out, ns, false, oi);
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
                wave_it->second < ns.output_lane_refs.size() &&
                ns.output_lane_refs[wave_it->second]) {
                const auto& wave = ns.output_lane_refs[wave_it->second];
                audio["waveform_length"] = static_cast<int64_t>(wave.length());
                nlohmann::json preview = nlohmann::json::array();
                uint32_t preview_count = wave.length();
                if (preview_count > 32) preview_count = 32;
                for (uint32_t wi = 0; wi < preview_count; ++wi) {
                    preview.push_back(static_cast<double>(wave.data()[wi]));
                }
                audio["waveform_preview"] = std::move(preview);
            }
            env_metrics["audio"] = std::move(audio);
        } else {
            nlohmann::json control = nlohmann::json::object();
            int64_t lane_out_nonempty = 0;
            int64_t scalar_out_nonzero = 0;
            for (const auto& ref : ns.output_lane_refs)
                if (ref) lane_out_nonempty++;
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
                if (!pi.asset_kind.empty()) pm["asset_kind"] = pi.asset_kind;
                if (!pi.widget_id.empty()) pm["widget_id"] = pi.widget_id;
                if (pi.widget_span > 0) pm["widget_span"] = pi.widget_span;
                add_param_info_visibility(pm, pi);
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
                        if (oi->second < icn->output_lane_refs.size())
                            p["lane_array"] = nlohmann::json{{"length", static_cast<int64_t>(icn->output_lane_refs[oi->second].length())}};
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
                        if (ii->second < icn->input_lane_refs.size())
                            p["lane_array"] = nlohmann::json{{"length", static_cast<int64_t>(icn->input_lane_refs[ii->second].length())}};
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

std::string handle_operator_map(OperatorRegistry& registry) {
    nlohmann::json entries = nlohmann::json::array();
    for (const auto& e : registry.operator_map()) {
        nlohmann::json j;
        j["type"] = e.type_name;
        if (!e.dylib_path.empty()) j["path"] = e.dylib_path;
        if (!e.package_name.empty()) j["package"] = e.package_name;
        j["status"] = e.status;
        if (e.abi_version > 0) j["abi_version"] = e.abi_version;
        entries.push_back(std::move(j));
    }
    return json_ok(std::move(entries));
}

std::string handle_get_discovery_report(PackageManager* package_manager) {
    if (!package_manager)
        return json_err("package manager not available");

    const auto& report = package_manager->last_discovery_report();
    nlohmann::json result = nlohmann::json::object();
    result["workspace_detected"] = report.workspace_detected;

    nlohmann::json scopes = nlohmann::json::array();
    for (const auto& s : report.scopes_searched) {
        scopes.push_back({{"scope", s.scope}, {"root", s.root}, {"exists", s.exists}});
    }
    result["scopes"] = std::move(scopes);

    nlohmann::json loaded = nlohmann::json::array();
    for (const auto& p : report.loaded_packages) {
        loaded.push_back({
            {"name", p.name},
            {"version", p.version},
            {"scope", p.source_scope},
            {"path", p.path},
            {"operators", p.operators.size() + p.gpu_operators.size()}
        });
    }
    result["loaded"] = std::move(loaded);

    nlohmann::json skipped = nlohmann::json::array();
    for (const auto& s : report.skipped_packages) {
        skipped.push_back({
            {"name", s.name},
            {"path", s.path},
            {"scope", s.source_scope},
            {"reason", s.reason},
            {"detail", s.detail}
        });
    }
    result["skipped"] = std::move(skipped);
    return json_ok(std::move(result));
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

std::string handle_list_packages(PackageManager* package_manager) {
    if (!package_manager)
        return json_err("package manager not available");

    auto packages = package_manager->list();
    nlohmann::json res = nlohmann::json::object();
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& pkg : packages) {
        nlohmann::json p = nlohmann::json::object();
        p["name"] = pkg.name;
        p["version"] = pkg.version;
        if (!pkg.vivid_core.empty()) p["vivid_core"] = pkg.vivid_core;
        if (!pkg.source_scope.empty()) p["source_scope"] = pkg.source_scope;
        if (!pkg.path.empty()) p["path"] = pkg.path;
        if (!pkg.build_type.empty()) p["build_type"] = pkg.build_type;
        p["description"] = pkg.description;
        p["author"] = pkg.author;
        nlohmann::json ops = nlohmann::json::array();
        for (const auto& op : pkg.operators) ops.push_back(op);
        for (const auto& op : pkg.gpu_operators) ops.push_back(op);
        p["operators"] = std::move(ops);
        p["linked"] = pkg.linked;
        arr.push_back(std::move(p));
    }
    res["packages"] = std::move(arr);
    return json_ok(std::move(res));
}

std::string handle_read_package_docs(PackageManager* package_manager, const nlohmann::json& root) {
    if (!package_manager)
        return json_err("package manager not available");
    if (!root.contains("name") || !root["name"].is_string())
        return json_err("missing 'name'");

    std::string name = root["name"].get<std::string>();
    if (!is_safe_package_name(name))
        return json_err("invalid package name");
    if (!package_manager->is_installed(name))
        return json_err("package not installed: " + name);

    auto readme_path = std::filesystem::path(PackageManager::packages_dir()) / name / "README.md";
    std::ifstream f(readme_path);
    if (!f.is_open())
        return json_ok_msg("No README.md found for package '" + name + "'");

    std::ostringstream ss;
    ss << f.rdbuf();
    return json_ok(nlohmann::json{{"name", name}, {"content", ss.str()}});
}

std::string handle_list_package_examples(PackageManager* package_manager, const nlohmann::json& root) {
    if (!package_manager)
        return json_err("package manager not available");
    if (!root.contains("name") || !root["name"].is_string())
        return json_err("missing 'name'");

    std::string name = root["name"].get<std::string>();
    if (!is_safe_package_name(name))
        return json_err("invalid package name");
    if (!package_manager->is_installed(name))
        return json_err("package not installed: " + name);

    auto graphs_dir = std::filesystem::path(PackageManager::packages_dir()) / name / "graphs";
    nlohmann::json res = nlohmann::json::object();
    res["name"] = name;
    nlohmann::json arr = nlohmann::json::array();
    std::error_code ec;
    if (std::filesystem::is_directory(graphs_dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(graphs_dir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;
            nlohmann::json ex = nlohmann::json::object();
            ex["filename"] = entry.path().filename().string();
            vivid::ExampleEntry edata;
            if (load_example_entry_from_graph(entry.path(), graphs_dir, edata)) {
                ex["description"] = edata.summary;
                if (!edata.content_kind.empty()) ex["content_kind"] = edata.content_kind;
                if (!edata.category.empty()) ex["category"] = edata.category;
                if (!edata.family.empty()) ex["family"] = edata.family;
                if (!edata.role.empty()) ex["role"] = edata.role;
                if (!edata.playability.empty()) ex["playability"] = edata.playability;
                if (!edata.domains.empty()) {
                    nlohmann::json darr = nlohmann::json::array();
                    for (const auto& d : edata.domains) darr.push_back(d);
                    ex["domains"] = std::move(darr);
                }
            } else {
                ex["description"] = "";
            }
            arr.push_back(std::move(ex));
        }
    }
    res["examples"] = std::move(arr);
    return json_ok(std::move(res));
}

std::string handle_read_package_example(PackageManager* package_manager, const nlohmann::json& root) {
    if (!package_manager)
        return json_err("package manager not available");
    if (!root.contains("name") || !root["name"].is_string() ||
        !root.contains("filename") || !root["filename"].is_string())
        return json_err("missing 'name' or 'filename'");

    std::string name = root["name"].get<std::string>();
    std::string filename = root["filename"].get<std::string>();
    if (!is_safe_package_name(name))
        return json_err("invalid package name");
    if (filename.find('/') != std::string::npos ||
        filename.find('\\') != std::string::npos ||
        filename.find("..") != std::string::npos) {
        return json_err("invalid filename");
    }
    if (!package_manager->is_installed(name))
        return json_err("package not installed: " + name);

    auto file_path = std::filesystem::path(PackageManager::packages_dir()) / name / "graphs" / filename;
    std::ifstream f(file_path);
    if (!f.is_open())
        return json_err("example not found: " + filename);

    std::ostringstream ss;
    ss << f.rdbuf();
    return json_ok(nlohmann::json{{"name", name}, {"filename", filename}, {"content", ss.str()}});
}

std::string handle_package_operator_docs(OperatorRegistry& registry,
                                         PackageManager* package_manager,
                                         OperatorSourceDocs& source_docs,
                                         const nlohmann::json& root) {
    if (!package_manager)
        return json_err("package manager not available");
    if (!root.contains("name") || !root["name"].is_string())
        return json_err("missing 'name'");

    std::string name = root["name"].get<std::string>();
    if (!package_manager->is_installed(name))
        return json_err("package not installed: " + name);

    nlohmann::json res = nlohmann::json::object();
    res["package"] = name;
    nlohmann::json ops_arr = nlohmann::json::array();
    for (const auto& type_name : registry.type_names()) {
        const auto* pkg = registry.package_for_type(type_name);
        if (!pkg || *pkg != name) continue;
        const auto* desc = registry.probe_descriptor(type_name);
        if (!desc) continue;
        nlohmann::json detail = source_docs.resolve_package(
            name,
            package_manager->resolve_package_path(name),
            type_name);
        nlohmann::json op = build_operator_docs_response(*desc,
            detail.is_null() ? nullptr : &detail, name);
        ops_arr.push_back(std::move(op));
    }
    res["operators"] = std::move(ops_arr);
    return json_ok(std::move(res));
}

std::string handle_package_catalog(PackageCatalog* package_catalog) {
    if (!package_catalog)
        return json_err("package catalog not available");

    auto entries = package_catalog->entries();
    nlohmann::json resp = nlohmann::json::object();
    resp["ok"] = true;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : entries) {
        nlohmann::json obj = nlohmann::json::object();
        obj["name"] = e.name;
        obj["description"] = e.description;
        obj["version"] = e.version;
        if (!e.vivid_core.empty()) obj["vivid_core"] = e.vivid_core;
        obj["author"] = e.author;
        obj["url"] = e.url;
        if (!e.category.empty()) obj["category"] = e.category;
        if (!e.description_short.empty()) obj["description_short"] = e.description_short;
        if (!e.status.empty()) obj["status"] = e.status;
        if (!e.status_note.empty()) obj["status_note"] = e.status_note;
        if (!e.preview_image_url.empty()) obj["preview_image_url"] = e.preview_image_url;
        if (!e.repo_url.empty()) obj["repo_url"] = e.repo_url;
        if (!e.homepage_url.empty()) obj["homepage_url"] = e.homepage_url;
        if (!e.install_url.empty()) obj["install_url"] = e.install_url;
        obj["installed"] = e.installed;
        if (e.installed) obj["installed_version"] = e.installed_version;
        arr.push_back(std::move(obj));
    }
    resp["packages"] = std::move(arr);
    return resp.dump();
}

std::string handle_check_package_updates(PackageCatalog* package_catalog,
                                         PackageManager* package_manager,
                                         const nlohmann::json& root) {
    if (!package_catalog || !package_manager)
        return json_err("package catalog/manager not available");

    std::string core_version = root.contains("core_version") && root["core_version"].is_string()
        ? root["core_version"].get<std::string>()
        : "0.1.0";
    const bool include_all_installed = root.contains("include_all_installed") &&
        root["include_all_installed"].is_boolean() &&
        root["include_all_installed"].get<bool>();

    auto entries = package_catalog->entries();
    nlohmann::json resp = nlohmann::json::object();
    resp["ok"] = true;
    resp["core_version"] = core_version;

    nlohmann::json updates = nlohmann::json::array();
    int64_t update_count = 0;
    int64_t incompatible_count = 0;
    for (const auto& e : entries) {
        if (!e.installed) continue;

        PackageInfo installed;
        installed.name = e.name;
        installed.version = e.installed_version;
        auto assessment = PackageManager::assess_update(
            installed, e.version, e.vivid_core, core_version);

        if (!include_all_installed && !assessment.update_available) continue;

        nlohmann::json obj = nlohmann::json::object();
        obj["name"] = assessment.package_name;
        obj["installed_version"] = assessment.installed_version;
        obj["remote_version"] = assessment.remote_version;
        if (!assessment.remote_vivid_core.empty())
            obj["vivid_core"] = assessment.remote_vivid_core;
        obj["update_available"] = assessment.update_available;
        obj["compatible"] = assessment.compatible;
        obj["constraint_valid"] = assessment.constraint_valid;
        obj["classification"] = update_class_str(assessment.classification);
        obj["message"] = assessment.message;

        if (assessment.update_available) update_count++;
        if (assessment.classification == PackageUpdateClass::IncompatibleUpdate)
            incompatible_count++;

        updates.push_back(std::move(obj));
    }
    resp["updates_available"] = update_count;
    resp["incompatible_updates"] = incompatible_count;
    resp["packages"] = std::move(updates);
    return resp.dump();
}

std::string handle_check_core_updates(AppUpdateManager* app_update_manager, const nlohmann::json& root) {
    if (!app_update_manager)
        return json_err("core update manager unavailable");

    const bool force_refresh = root.contains("force_refresh") &&
        root["force_refresh"].is_boolean() &&
        root["force_refresh"].get<bool>();
    if (force_refresh) app_update_manager->refresh();
    if (app_update_manager->fetch_state() == AppUpdateFetchState::Idle)
        app_update_manager->refresh();
    for (int i = 0; i < 200; ++i) {
        auto st = app_update_manager->fetch_state();
        if (st != AppUpdateFetchState::Fetching) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    nlohmann::json resp = nlohmann::json::object();
    resp["ok"] = true;

    const auto st = app_update_manager->fetch_state();
    switch (st) {
        case AppUpdateFetchState::Idle:     resp["state"] = "idle"; break;
        case AppUpdateFetchState::Fetching: resp["state"] = "fetching"; break;
        case AppUpdateFetchState::Ready:    resp["state"] = "ready"; break;
        case AppUpdateFetchState::Error:    resp["state"] = "error"; break;
    }

    auto info = app_update_manager->latest();
    resp["update_available"] = info.update_available;
    resp["current_version"] = info.current_version;
    resp["latest_version"] = info.latest_version;
    resp["download_url"] = info.download_url;
    resp["release_notes_url"] = info.release_notes_url;
    resp["title"] = info.title;
    resp["publication_date"] = info.publication_date;
    resp["minimum_system_version"] = info.minimum_system_version;
    resp["appcast_url"] = AppUpdateManager::appcast_url();
    if (st == AppUpdateFetchState::Error)
        resp["error"] = app_update_manager->fetch_error();
    return resp.dump();
}

std::string handle_list_source_roots(SourceIndex& source_index) {
    return nlohmann::json{{"ok", true}, {"roots", source_index.list_roots()}}.dump();
}

std::string handle_search_source(SourceIndex& source_index, const nlohmann::json& root) {
    if (!root.contains("query") || !root["query"].is_string())
        return json_err("missing 'query'");
    auto result = source_index.search(root["query"].get<std::string>(),
                                      json_string_array(root, "roots"),
                                      root.value("limit", 20),
                                      json_string_array(root, "file_types"),
                                      json_string_array(root, "path_globs"));
    return result.dump();
}

std::string handle_read_source_file(SourceIndex& source_index, const nlohmann::json& root) {
    if (!root.contains("path") || !root["path"].is_string())
        return json_err("missing 'path'");
    auto result = source_index.read_file(root["path"].get<std::string>(),
                                         root.value("max_bytes", 200000));
    return result.dump();
}

std::string handle_read_source_span(SourceIndex& source_index, const nlohmann::json& root) {
    if (!root.contains("path") || !root["path"].is_string())
        return json_err("missing 'path'");
    if (!root.contains("start_line") || !root["start_line"].is_number_integer() ||
        !root.contains("end_line") || !root["end_line"].is_number_integer())
        return json_err("missing 'start_line' or 'end_line'");
    auto result = source_index.read_span(root["path"].get<std::string>(),
                                         root["start_line"].get<int>(),
                                         root["end_line"].get<int>());
    return result.dump();
}

std::string handle_find_symbol(SourceIndex& source_index, const nlohmann::json& root) {
    if (!root.contains("name") || !root["name"].is_string())
        return json_err("missing 'name'");
    auto result = source_index.find_symbol(root["name"].get<std::string>(),
                                           json_string_array(root, "roots"),
                                           root.value("limit", 20));
    return result.dump();
}

std::string handle_find_references(SourceIndex& source_index, const nlohmann::json& root) {
    if (!root.contains("name") || !root["name"].is_string())
        return json_err("missing 'name'");
    auto result = source_index.find_references(root["name"].get<std::string>(),
                                               json_string_array(root, "roots"),
                                               root.value("limit", 50));
    return result.dump();
}

std::string handle_get_build_activity(BuildConsole* build_console, const nlohmann::json& root) {
    if (!build_console)
        return json_err("build console not available");

    auto snapshot = build_console->snapshot();
    auto tasks = collect_build_task_summaries(build_console);
    const std::string scope = root.value("scope", std::string("recent"));
    const std::size_t limit = std::max(1, std::min(root.value("limit", 10), 50));

    nlohmann::json tasks_arr = nlohmann::json::array();
    for (const auto& task : tasks) {
        if (scope == "active" && task.state != BuildTaskState::Running)
            continue;
        nlohmann::json entry = {
            {"task_id", task.task_id},
            {"kind", build_task_kind_str(task.kind)},
            {"label", task.label},
            {"state", build_task_state_str(task.state)},
            {"summary", task.summary},
            {"last_timestamp_ms", task.last_timestamp_ms},
            {"line_count", task.lines.size()},
            {"top_error_lines", top_error_lines_json(task, 3)},
        };
        nlohmann::json recent_lines = nlohmann::json::array();
        const std::size_t start = task.lines.size() > 6 ? task.lines.size() - 6 : 0;
        for (std::size_t i = start; i < task.lines.size(); ++i)
            recent_lines.push_back(build_console_line_json(task.lines[i]));
        entry["recent_lines"] = std::move(recent_lines);
        tasks_arr.push_back(std::move(entry));
        if (tasks_arr.size() >= limit)
            break;
    }

    return nlohmann::json{
        {"ok", true},
        {"scope", scope},
        {"version", snapshot.version},
        {"running_task_count", snapshot.running_task_count},
        {"tasks", std::move(tasks_arr)},
    }.dump();
}

std::string handle_explain_build_failure(BuildConsole* build_console, const nlohmann::json& root) {
    if (!build_console)
        return json_err("build console not available");

    auto tasks = collect_build_task_summaries(build_console);
    if (tasks.empty())
        return json_err("no build activity available");

    BuildTaskId requested_id = 0;
    if (root.contains("task_id")) {
        if (root["task_id"].is_number_unsigned())
            requested_id = root["task_id"].get<BuildTaskId>();
        else if (root["task_id"].is_number_integer())
            requested_id = static_cast<BuildTaskId>(root["task_id"].get<int64_t>());
        else if (root["task_id"].is_string()) {
            const std::string task_id = root["task_id"].get<std::string>();
            if (task_id != "latest")
                return json_err("task_id must be a number or 'latest'");
        }
    }

    const BuildTaskSummary* chosen = nullptr;
    if (requested_id != 0) {
        for (const auto& task : tasks) {
            if (task.task_id == requested_id) {
                chosen = &task;
                break;
            }
        }
        if (!chosen)
            return json_err("build task not found");
    } else {
        for (const auto& task : tasks) {
            if (task.state == BuildTaskState::Failed) {
                chosen = &task;
                break;
            }
        }
        if (!chosen)
            return json_err("no failed build task found");
    }

    const std::size_t max_lines = std::max(5, std::min(root.value("max_lines", 40), 200));
    nlohmann::json excerpt = nlohmann::json::array();
    const std::size_t start = chosen->lines.size() > max_lines ? chosen->lines.size() - max_lines : 0;
    for (std::size_t i = start; i < chosen->lines.size(); ++i)
        excerpt.push_back(build_console_line_json(chosen->lines[i]));

    std::ostringstream joined;
    for (std::size_t i = start; i < chosen->lines.size(); ++i) {
        if (i > start) joined << '\n';
        joined << chosen->lines[i].text;
    }

    return nlohmann::json{
        {"ok", true},
        {"task", {
            {"task_id", chosen->task_id},
            {"kind", build_task_kind_str(chosen->kind)},
            {"label", chosen->label},
            {"state", build_task_state_str(chosen->state)},
            {"summary", chosen->summary},
            {"last_timestamp_ms", chosen->last_timestamp_ms},
        }},
        {"top_error_lines", top_error_lines_json(*chosen, 8)},
        {"lines", std::move(excerpt)},
        {"output_excerpt", joined.str()},
    }.dump();
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
