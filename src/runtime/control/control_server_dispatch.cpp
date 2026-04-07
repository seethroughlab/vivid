#include "runtime/control/control_server_internal.h"

namespace vivid {

// dispatch() routes incoming control server requests by method name.
//
// Read-only queries (inspect_graph, list_types, etc.) are checked first and
// can execute without parsing the JSON body. Mutation handlers parse the body
// and delegate to RuntimeAPI — they never modify Graph or RuntimeCore directly.
// Topology-mutating commands (add_node, connect, etc.) set pending_topology_change_
// in RuntimeAPI; the actual recompile happens between frames via apply_pending().
std::string dispatch(const std::string& method, const std::string& body,
                            RuntimeAPI& api, Graph& graph,
                            RuntimeCore& core, OperatorRegistry& registry,
                            bool& has_gpu_ops, bool& has_audio,
                            HotReloader* hot_reloader,
                            const std::string& src_dir,
                            OperatorSourceDocs& source_docs,
                            SourceIndex& source_index,
                            PackageManager* package_manager,
                            PackageCompiler* package_compiler,
                            Settings* settings,
                            AudioEngine* audio_engine,
                            AssetLibrary* asset_library,
                            BuildConsole* build_console) {
    // Read-only queries (no body needed)
    if (method == "inspect_graph") return handle_inspect_graph(graph, core, core.subgraph_modules());
    if (method == "introspect_nodes") return handle_introspect_nodes(graph, core, core.subgraph_modules());
    if (method == "run_diagnostics")
        return control_server_checks::handle_run_diagnostics(graph, core, registry);
    if (method == "get_registry_diagnostics") return handle_get_registry_diagnostics(registry);
    if (method == "get_graph_load_diagnostics") return handle_get_graph_load_diagnostics(graph);
    if (method == "list_source_roots") return handle_list_source_roots(source_index);
    if (method == "operator_map") return handle_operator_map(registry);
    if (method == "get_discovery_report") return handle_get_discovery_report(package_manager);

    // Parse body JSON (may be empty for some commands)
    nlohmann::json root;
    bool root_valid = false;
    try { root = nlohmann::json::parse(body); root_valid = true; }
    catch (...) {}

    std::string result;

    if (method == "list_types") {
        result = handle_list_types(registry, package_manager, source_docs, root_valid ? root : nlohmann::json::object(), core.subgraph_modules());
    } else if (method == "search_source") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_search_source(source_index, root);
    } else if (method == "read_source_file") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_read_source_file(source_index, root);
    } else if (method == "read_source_span") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_read_source_span(source_index, root);
    } else if (method == "find_symbol") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_find_symbol(source_index, root);
    } else if (method == "find_references") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_find_references(source_index, root);
    } else if (method == "get_build_activity") {
        result = handle_get_build_activity(build_console, root_valid ? root : nlohmann::json::object());
    } else if (method == "explain_build_failure") {
        result = handle_explain_build_failure(build_console, root_valid ? root : nlohmann::json::object());
    } else if (method == "validate_checks") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = control_server_checks::handle_validate_checks(root);
    } else if (method == "sample_node_outputs") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_sample_node_outputs(graph, core, root);
    } else if (method == "run_checks") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = control_server_checks::handle_run_checks(graph, core, registry, root);
    } else if (method == "add_node") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("type") || !root["type"].is_string() ||
                !root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'type' or 'node_id'");
            else
                result = command_result_to_json(
                    api.add_node(root["type"].get<std::string>(), root["node_id"].get<std::string>()));
        }
    } else if (method == "remove_node") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.remove_node(root["node_id"].get<std::string>()));
        }
    } else if (method == "connect") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("from_addr") || !root["from_addr"].is_string() ||
                !root.contains("to_addr") || !root["to_addr"].is_string())
                result = json_err("missing 'from_addr' or 'to_addr'");
            else {
                const std::string from_addr = root["from_addr"].get<std::string>();
                const std::string to_addr = root["to_addr"].get<std::string>();
                const bool semantic_defaults = root.contains("semantic_defaults") &&
                    root["semantic_defaults"].is_boolean() && root["semantic_defaults"].get<bool>();
                const std::string bridge = (root.contains("bridge") && root["bridge"].is_string())
                    ? root["bridge"].get<std::string>() : "";
                CommandResult cr = api.connect(from_addr, to_addr, semantic_defaults, bridge);
                if (!cr.ok) {
                    result = json_err(cr.message);
                } else {
                    nlohmann::json resp = nlohmann::json::object();
                    resp["ok"] = true;
                    resp["message"] = cr.message;

                    bool inferred_applied = false;
                    if (semantic_defaults) {
                        const ConnectionDef* conn = find_connection_by_addr(graph, from_addr, to_addr);
                        if (conn && conn->has_remap()) {
                            inferred_applied = true;
                            resp["inferred_remap"] = {
                                {"from_min", conn->from_min}, {"from_max", conn->from_max},
                                {"to_min", conn->to_min}, {"to_max", conn->to_max},
                                {"clamp", conn->clamp}
                            };
                        }
                    }
                    resp["inferred_remap_applied"] = inferred_applied;
                    result = resp.dump();
                }
            }
        }
    } else if (method == "disconnect") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("from_addr") || !root["from_addr"].is_string() ||
                !root.contains("to_addr") || !root["to_addr"].is_string())
                result = json_err("missing 'from_addr' or 'to_addr'");
            else
                result = command_result_to_json(
                    api.disconnect(root["from_addr"].get<std::string>(), root["to_addr"].get<std::string>()));
        }
    } else if (method == "set_connection_remap") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("from_addr") || !root["from_addr"].is_string() ||
                !root.contains("to_addr") || !root["to_addr"].is_string())
                result = json_err("missing 'from_addr' or 'to_addr'");
            else {
                float fmin = root.contains("from_min") && root["from_min"].is_number() ? root["from_min"].get<float>() : 0.0f;
                float fmax = root.contains("from_max") && root["from_max"].is_number() ? root["from_max"].get<float>() : 1.0f;
                float tmin = root.contains("to_min") && root["to_min"].is_number() ? root["to_min"].get<float>() : 0.0f;
                float tmax = root.contains("to_max") && root["to_max"].is_number() ? root["to_max"].get<float>() : 1.0f;
                bool  cval = root.contains("clamp") && root["clamp"].is_boolean() ? root["clamp"].get<bool>() : false;
                result = command_result_to_json(
                    api.set_connection_remap(root["from_addr"].get<std::string>(), root["to_addr"].get<std::string>(),
                                              fmin, fmax, tmin, tmax, cval));
            }
        }
    } else if (method == "set_param") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string() ||
                !root.contains("value") || !root["value"].is_number())
                result = json_err("missing 'node_id', 'param', or 'value'");
            else
                result = command_result_to_json(
                    api.set_param(root["node_id"].get<std::string>(), root["param"].get<std::string>(),
                                  root["value"].get<float>()));
        }
    } else if (method == "set_string_param") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string() ||
                !root.contains("value") || !root["value"].is_string())
                result = json_err("missing 'node_id', 'param', or 'value' (string)");
            else
                result = command_result_to_json(
                    api.set_string_param(root["node_id"].get<std::string>(), root["param"].get<std::string>(),
                                         root["value"].get<std::string>()));
        }
    } else if (method == "get_param") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string())
                result = json_err("missing 'node_id' or 'param'");
            else {
                auto r = api.get_param(root["node_id"].get<std::string>(), root["param"].get<std::string>());
                if (r.ok) {
                    float v = 0;
                    try { v = std::stof(r.message); } catch (...) {}
                    result = nlohmann::json{{"ok", true}, {"value", static_cast<double>(v)}}.dump();
                } else {
                    result = json_err(r.message);
                }
            }
        }
    } else if (method == "save_graph") {
        // Annotate nodes with package provenance before saving
        if (package_manager) {
            auto packages = package_manager->list();
            std::unordered_map<std::string, std::string> pkg_ver_map;
            for (const auto& p : packages) pkg_ver_map[p.name] = p.version;
            for (auto& node : graph.nodes_mut()) {
                const auto* pkg = registry.package_for_type(node.type);
                if (pkg) {
                    node.pkg_name    = *pkg;
                    node.pkg_version = pkg_ver_map.count(*pkg) ? pkg_ver_map[*pkg] : "";
                }
            }
        }
        if (root_valid && root.contains("path") && root["path"].is_string()) {
            result = command_result_to_json(api.save_as(root["path"].get<std::string>()));
        } else {
            result = command_result_to_json(api.save());
        }
    } else if (method == "load_graph") {
        if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("path") || !root["path"].is_string()) {
                result = json_err("load_graph requires 'path' parameter");
            } else {
                result = command_result_to_json(
                    api.load_graph(root["path"].get<std::string>(), has_gpu_ops, has_audio));
            }
        }
    } else if (method == "set_resolution") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("width") || !root["width"].is_number() ||
                !root.contains("height") || !root["height"].is_number())
                result = json_err("missing 'node_id', 'width', or 'height'");
            else
                result = command_result_to_json(
                    api.set_resolution(root["node_id"].get<std::string>(),
                                       root["width"].get<uint32_t>(),
                                       root["height"].get<uint32_t>()));
        }
    } else if (method == "set_node_layout") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("x") || !root["x"].is_number() ||
                !root.contains("y") || !root["y"].is_number())
                result = json_err("missing 'node_id', 'x', or 'y'");
            else
                result = command_result_to_json(
                    api.set_node_layout(root["node_id"].get<std::string>(),
                                        root["x"].get<float>(),
                                        root["y"].get<float>()));
        }
    } else if (method == "inspect") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.inspect(root["node_id"].get<std::string>()));
        }
    } else if (method == "list_nodes") {
        result = command_result_to_json(api.list_nodes());
    } else if (method == "add_midi_mapping") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string() ||
                !root.contains("cc") || !root["cc"].is_number() ||
                !root.contains("channel") || !root["channel"].is_number() ||
                !root.contains("range_min") || !root["range_min"].is_number() ||
                !root.contains("range_max") || !root["range_max"].is_number())
                result = json_err("missing or invalid params for add_midi_mapping");
            else
                result = command_result_to_json(
                    api.add_midi_mapping(root["node_id"].get<std::string>(), root["param"].get<std::string>(),
                                         root["cc"].get<int>(),
                                         root["channel"].get<int>(),
                                         root["range_min"].get<float>(),
                                         root["range_max"].get<float>()));
        }
    } else if (method == "remove_midi_mapping") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string())
                result = json_err("missing 'node_id' or 'param'");
            else
                result = command_result_to_json(
                    api.remove_midi_mapping(root["node_id"].get<std::string>(), root["param"].get<std::string>()));
        }
    } else if (method == "update_midi_mapping") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string() ||
                !root.contains("range_min") || !root["range_min"].is_number() ||
                !root.contains("range_max") || !root["range_max"].is_number())
                result = json_err("missing or invalid params for update_midi_mapping");
            else
                result = command_result_to_json(
                    api.update_midi_mapping(root["node_id"].get<std::string>(), root["param"].get<std::string>(),
                                            root["range_min"].get<float>(),
                                            root["range_max"].get<float>()));
        }
    } else if (method == "add_mod_assignment") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("source") || !root["source"].is_string() ||
                !root.contains("destination") || !root["destination"].is_string() ||
                !root.contains("amount") || !root["amount"].is_number())
                result = json_err("missing or invalid params for add_mod_assignment");
            else {
                std::string polarity = (root.contains("polarity") && root["polarity"].is_string())
                    ? root["polarity"].get<std::string>() : "unipolar";
                std::string curve = (root.contains("curve") && root["curve"].is_string())
                    ? root["curve"].get<std::string>() : "linear";
                result = command_result_to_json(
                    api.add_mod_assignment(root["node_id"].get<std::string>(),
                                           root["source"].get<std::string>(),
                                           root["destination"].get<std::string>(),
                                           root["amount"].get<float>(),
                                           polarity, curve));
            }
        }
    } else if (method == "remove_mod_assignment") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("source") || !root["source"].is_string() ||
                !root.contains("destination") || !root["destination"].is_string())
                result = json_err("missing 'node_id', 'source', or 'destination'");
            else
                result = command_result_to_json(
                    api.remove_mod_assignment(root["node_id"].get<std::string>(),
                                              root["source"].get<std::string>(),
                                              root["destination"].get<std::string>()));
        }
    } else if (method == "update_mod_assignment") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("source") || !root["source"].is_string() ||
                !root.contains("destination") || !root["destination"].is_string() ||
                !root.contains("amount") || !root["amount"].is_number())
                result = json_err("missing or invalid params for update_mod_assignment");
            else {
                std::string polarity = (root.contains("polarity") && root["polarity"].is_string())
                    ? root["polarity"].get<std::string>() : "unipolar";
                std::string curve = (root.contains("curve") && root["curve"].is_string())
                    ? root["curve"].get<std::string>() : "linear";
                result = command_result_to_json(
                    api.update_mod_assignment(root["node_id"].get<std::string>(),
                                              root["source"].get<std::string>(),
                                              root["destination"].get<std::string>(),
                                              root["amount"].get<float>(),
                                              polarity, curve));
            }
        }
    } else if (method == "list_mod_sources") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else {
                auto r = api.list_mod_sources(root["node_id"].get<std::string>());
                result = r.ok ? r.message : json_err(r.message);
            }
        }
    } else if (method == "list_mod_destinations") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else {
                auto r = api.list_mod_destinations(root["node_id"].get<std::string>());
                result = r.ok ? r.message : json_err(r.message);
            }
        }
    } else if (method == "list_mod_assignments") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else {
                auto r = api.list_mod_assignments(root["node_id"].get<std::string>());
                result = r.ok ? r.message : json_err(r.message);
            }
        }
    } else if (method == "get_graph_errors") {
        nlohmann::json res = nlohmann::json::object();
        nlohmann::json errs = nlohmann::json::array();
        if (const auto* cg = core.compiled_graph()) {
            for (const auto& cn : cg->nodes) {
                if (!cn.errored && !cn.missing_operator) continue;
                nlohmann::json err_obj = {
                    {"node_id", cn.node_id},
                    {"error", cn.missing_operator ? "missing operator" : cn.error_message},
                    {"missing_operator", cn.missing_operator}
                };
                if (!cn.missing_operator_reason.empty())
                    err_obj["reason"] = cn.missing_operator_reason;
                if (!cn.missing_operator_detail.empty())
                    err_obj["detail"] = cn.missing_operator_detail;
                errs.push_back(std::move(err_obj));
            }
        }
        nlohmann::json dropped = nlohmann::json::array();
        if (const auto* cg = core.compiled_graph()) {
            for (const auto& dc : cg->dropped_connections) {
                dropped.push_back({
                    {"from", dc.from_node + "/" + dc.from_port},
                    {"to", dc.to_node + "/" + dc.to_port},
                    {"reason", dc.reason}
                });
            }
        }
        res["errors"] = std::move(errs);
        res["dropped_connections"] = std::move(dropped);
        result = json_ok(std::move(res));
    } else if (method == "save_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.save_variation(root["name"].get<std::string>()));
        }
    } else if (method == "recall_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.recall_variation(root["name"].get<std::string>()));
        }
    } else if (method == "remove_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.remove_variation(root["name"].get<std::string>()));
        }
    } else if (method == "rename_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("old_name") || !root["old_name"].is_string() ||
                !root.contains("new_name") || !root["new_name"].is_string())
                result = json_err("missing 'old_name' or 'new_name'");
            else
                result = command_result_to_json(
                    api.rename_variation(root["old_name"].get<std::string>(), root["new_name"].get<std::string>()));
        }
    } else if (method == "duplicate_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string() ||
                !root.contains("new_name") || !root["new_name"].is_string())
                result = json_err("missing 'name' or 'new_name'");
            else
                result = command_result_to_json(
                    api.duplicate_variation(root["name"].get<std::string>(), root["new_name"].get<std::string>()));
        }
    } else if (method == "move_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string() ||
                !root.contains("to_index") || !root["to_index"].is_number_integer())
                result = json_err("missing 'name' or 'to_index'");
            else
                result = command_result_to_json(
                    api.move_variation(root["name"].get<std::string>(), root["to_index"].get<int64_t>()));
        }
    } else if (method == "update_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else
                result = command_result_to_json(api.update_variation(root["name"].get<std::string>()));
        }
    } else if (method == "list_variations") {
        result = command_result_to_json(api.list_variations());
    } else if (method == "queue_variation") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                std::string q = (root.contains("quantize") && root["quantize"].is_string())
                    ? root["quantize"].get<std::string>() : "instant";
                result = command_result_to_json(api.queue_variation(root["name"].get<std::string>(), q));
            }
        }
    } else if (method == "set_quantize_clock") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.set_quantize_clock(root["node_id"].get<std::string>()));
        }
    } else if (method == "set_graph_metronome") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            const float bpm = (root.contains("bpm") && root["bpm"].is_number())
                ? root["bpm"].get<float>() : 120.0f;
            const int beats_per_bar = (root.contains("beats_per_bar") && root["beats_per_bar"].is_number_integer())
                ? static_cast<int>(root["beats_per_bar"].get<int64_t>()) : 4;
            result = command_result_to_json(api.set_graph_metronome(bpm, beats_per_bar));
        }
    } else if (method == "set_analysis") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("enabled") || !root["enabled"].is_boolean())
                result = json_err("missing 'enabled' (boolean)");
            else {
                bool enabled = root["enabled"].get<bool>();
                core.frame_executor().set_analysis_enabled(enabled);
                if (audio_engine) audio_engine->set_analysis_enabled(enabled);
                if (settings) settings->show_analysis = enabled;
                result = json_ok_msg(enabled ? "analysis enabled" : "analysis disabled");
            }
        }
    } else if (method == "save_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.save_preset(root["node_id"].get<std::string>(), root["name"].get<std::string>()));
        }
    } else if (method == "recall_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.recall_preset(root["node_id"].get<std::string>(), root["name"].get<std::string>()));
        }
    } else if (method == "update_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.update_preset(root["node_id"].get<std::string>(), root["name"].get<std::string>()));
        }
    } else if (method == "remove_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'node_id' or 'name'");
            else
                result = command_result_to_json(
                    api.remove_preset(root["node_id"].get<std::string>(), root["name"].get<std::string>()));
        }
    } else if (method == "rename_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("old_name") || !root["old_name"].is_string() ||
                !root.contains("new_name") || !root["new_name"].is_string())
                result = json_err("missing 'node_id', 'old_name', or 'new_name'");
            else
                result = command_result_to_json(
                    api.rename_preset(root["node_id"].get<std::string>(), root["old_name"].get<std::string>(),
                                      root["new_name"].get<std::string>()));
        }
    } else if (method == "list_presets") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.list_presets(root["node_id"].get<std::string>()));
        }
    } else if (method == "list_factory_presets") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string())
                result = json_err("missing 'node_id'");
            else
                result = command_result_to_json(api.list_factory_presets(root["node_id"].get<std::string>()));
        }
    } else if (method == "set_param_lock") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string() ||
                !root.contains("flags") || !root["flags"].is_number())
                result = json_err("missing 'node_id', 'param', or 'flags'");
            else
                result = command_result_to_json(
                    api.set_param_lock(root["node_id"].get<std::string>(), root["param"].get<std::string>(),
                                       static_cast<uint8_t>(root["flags"].get<int64_t>())));
        }
    } else if (method == "get_param_lock") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("node_id") || !root["node_id"].is_string() ||
                !root.contains("param") || !root["param"].is_string())
                result = json_err("missing 'node_id' or 'param'");
            else
                result = command_result_to_json(
                    api.get_param_lock(root["node_id"].get<std::string>(), root["param"].get<std::string>()));
        }
    } else if (method == "set_state_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("sm_node") || !root["sm_node"].is_string() ||
                !root.contains("state_idx") || !root["state_idx"].is_number() ||
                !root.contains("target_node") || !root["target_node"].is_string() ||
                !root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'sm_node', 'state_idx', 'target_node', or 'name'");
            else
                result = command_result_to_json(
                    api.set_state_preset(root["sm_node"].get<std::string>(),
                                         root["state_idx"].get<int>(),
                                         root["target_node"].get<std::string>(), root["name"].get<std::string>()));
        }
    } else if (method == "remove_state_preset") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("sm_node") || !root["sm_node"].is_string() ||
                !root.contains("state_idx") || !root["state_idx"].is_number() ||
                !root.contains("target_node") || !root["target_node"].is_string())
                result = json_err("missing 'sm_node', 'state_idx', or 'target_node'");
            else
                result = command_result_to_json(
                    api.remove_state_preset(root["sm_node"].get<std::string>(),
                                            root["state_idx"].get<int>(),
                                            root["target_node"].get<std::string>()));
        }
    } else if (method == "clear_state_presets") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("sm_node") || !root["sm_node"].is_string())
                result = json_err("missing 'sm_node'");
            else
                result = command_result_to_json(
                    api.clear_state_presets(root["sm_node"].get<std::string>()));
        }
    } else if (method == "inspect_state_presets") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("sm_node") || !root["sm_node"].is_string())
                result = json_err("missing 'sm_node'");
            else
                result = command_result_to_json(
                    api.inspect_state_presets(root["sm_node"].get<std::string>()));
        }
    } else if (method == "scaffold_operator") {
        result = [&]() -> std::string {
            if (src_dir.empty())
                return json_err("scaffold_operator requires --src-dir");
            if (!root_valid)
                return json_err("invalid JSON body");

            if (!root.contains("name") || !root["name"].is_string())
                return json_err("missing 'name'");
            if (!root.contains("kind") || !root["kind"].is_string())
                return json_err("missing 'kind'");

            std::string name = root["name"].get<std::string>();
            std::string kind_str_val = root["kind"].get<std::string>();

            VividOperatorKind kind;
            if (kind_str_val == "control")      kind = VIVID_OP_CONTROL;
            else if (kind_str_val == "audio")   kind = VIVID_OP_AUDIO;
            else if (kind_str_val == "gpu")     kind = VIVID_OP_GPU;
            else return json_err("kind must be 'control', 'audio', or 'gpu'");

            // Optional variant (e.g. "composite")
            std::string variant;
            if (root.contains("variant") && root["variant"].is_string())
                variant = root["variant"].get<std::string>();

            std::string destination = "auto";
            if (root.contains("destination") && root["destination"].is_string())
                destination = root["destination"].get<std::string>();

            std::string err = OperatorCreator::validate_name(name, registry);
            if (!err.empty()) return json_err(err);

            const std::vector<PackageInfo> packages =
                package_manager ? package_manager->list() : std::vector<PackageInfo>{};
            OperatorDestination resolved;
            std::string resolve_error;
            if (!resolve_operator_destination(destination, src_dir, packages, settings,
                                              resolved, resolve_error)) {
                return json_err(resolve_error);
            }
            if (!resolved.warning.empty()) {
                std::fprintf(stderr, "[vivid] %s\n", resolved.warning.c_str());
            }

            VividCreateOperatorRequest req;
            req.name = name;
            req.kind = kind;
            req.variant = variant;
            req.destination = destination;

            auto parse_port_type = [&](const std::string& type_str, VividOperatorKind k, VividPortType& out) -> std::string {
                if (k == VIVID_OP_CONTROL) {
                    if      (type_str == "float")         out = VIVID_PORT_SCALAR;
                    else if (type_str == "int")           out = VIVID_PORT_SCALAR;
                    else if (type_str == "bool")          out = VIVID_PORT_SCALAR;
                    else if (type_str == "lane_array")        out = VIVID_PORT_LANE_ARRAY;
                    else if (type_str == "string")        out = VIVID_PORT_STRING;
                    else if (type_str == "string_lanes") out = VIVID_PORT_STRING_LANES;
                    else return "unknown control port type '" + type_str + "'";
                } else if (k == VIVID_OP_AUDIO) {
                    if (type_str == "float") out = VIVID_PORT_AUDIO_BUFFER;
                    else return "unknown audio port type '" + type_str + "'";
                } else if (k == VIVID_OP_GPU) {
                    if (type_str == "texture") out = VIVID_PORT_TEXTURE;
                    else return "unknown GPU port type '" + type_str + "'";
                }
                return {};
            };

            auto parse_ports = [&](const char* key, VividPortDirection dir) -> std::string {
                if (!root.contains(key)) return {};
                const auto& arr = root[key];
                if (!arr.is_array()) return std::string(key) + " must be an array";
                for (size_t idx = 0; idx < arr.size(); ++idx) {
                    const auto& elem = arr[idx];
                    if (!elem.contains("name") || !elem["name"].is_string())
                        return std::string(key) + "[" + std::to_string(idx) + "] missing 'name'";
                    std::string ptype = "float";
                    if (elem.contains("type") && elem["type"].is_string()) ptype = elem["type"].get<std::string>();
                    VividPortType vt;
                    std::string err = parse_port_type(ptype, kind, vt);
                    if (!err.empty()) return err;
                    req.ports.push_back({elem["name"].get<std::string>(), vt, dir});
                }
                return {};
            };

            std::string port_err = parse_ports("inputs", VIVID_PORT_INPUT);
            if (!port_err.empty()) return json_err(port_err);
            port_err = parse_ports("outputs", VIVID_PORT_OUTPUT);
            if (!port_err.empty()) return json_err(port_err);

            if (root.contains("params")) {
                const auto& params_arr = root["params"];
                if (!params_arr.is_array())
                    return json_err("params must be an array");
                for (size_t pidx = 0; pidx < params_arr.size(); ++pidx) {
                    const auto& pelem = params_arr[pidx];
                    if (!pelem.contains("name") || !pelem["name"].is_string())
                        return json_err("params[" + std::to_string(pidx) + "] missing 'name'");
                    VividParamSpec ps;
                    ps.name = pelem["name"].get<std::string>();
                    if (pelem.contains("type") && pelem["type"].is_string()) {
                        std::string ts = pelem["type"].get<std::string>();
                        if      (ts == "float") ps.type = VIVID_PARAM_FLOAT;
                        else if (ts == "int")   ps.type = VIVID_PARAM_INT;
                        else if (ts == "bool")  ps.type = VIVID_PARAM_BOOL;
                        else if (ts == "file")  ps.type = VIVID_PARAM_FILE;
                        else if (ts == "text")  ps.type = VIVID_PARAM_TEXT;
                        else return json_err("unknown param type '" + ts + "'");
                    }
                    if (pelem.contains("default") && pelem["default"].is_number()) ps.default_value = pelem["default"].get<float>();
                    if (pelem.contains("min") && pelem["min"].is_number()) ps.min_value = pelem["min"].get<float>();
                    if (pelem.contains("max") && pelem["max"].is_number()) ps.max_value = pelem["max"].get<float>();
                    if (pelem.contains("default_string") && pelem["default_string"].is_string()) ps.default_string = pelem["default_string"].get<std::string>();
                    req.params.push_back(ps);
                }
            }

            auto cr = OperatorCreator::create(req, resolved.root, resolved.package_layout);
            if (!cr.success) return json_err(cr.error);

            if (hot_reloader) {
                if (resolved.package_layout && !resolved.package_name.empty())
                    hot_reloader->queue_rebuild("pkg:" + resolved.package_name + ":" + cr.target_name);
                else
                    hot_reloader->queue_rebuild(cr.target_name);
            }

            OperatorCreator::open_in_editor(cr.cpp_path);

            nlohmann::json res = nlohmann::json::object();
            res["cpp_path"] = cr.cpp_path;
            res["target_name"] = cr.target_name;
            res["destination_root"] = resolved.root;
            res["destination_is_package"] = resolved.package_layout;
            if (!resolved.package_name.empty())
                res["destination_package"] = resolved.package_name;
            if (!resolved.warning.empty())
                res["destination_warning"] = resolved.warning;
            return json_ok(std::move(res));
        }();
    } else if (method == "install_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("url") || !root["url"].is_string())
                result = json_err("missing 'url'");
            else {
                auto ir = package_manager->install(root["url"].get<std::string>());
                if (ir.success) {
                    nlohmann::json res = nlohmann::json::object();
                    res["name"] = ir.info.name;
                    res["version"] = ir.info.version;
                    if (!ir.info.vivid_core.empty())
                        res["vivid_core"] = ir.info.vivid_core;
                    res["operator_count"] = static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size());
                    result = json_ok(std::move(res));
                    // Auto-reload graph if it has missing operators
                    if (const auto* cg = core.compiled_graph()) {
                        for (const auto& cn : cg->nodes) {
                            if (cn.missing_operator) {
                                auto rr = api.reload(has_gpu_ops, has_audio);
                                if (!rr.ok) {
                                    result = json_err("package installed but runtime refresh failed: " + rr.message);
                                }
                                break;
                            }
                        }
                    }
                } else {
                    result = json_err(ir.error);
                }
            }
        }
    } else if (method == "uninstall_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                std::string snapshot_json;
                std::string snapshot_error;
                if (!capture_live_graph_snapshot(graph, snapshot_json, snapshot_error)) {
                    result = json_err(snapshot_error);
                } else if (package_manager->uninstall(root["name"].get<std::string>())) {
                    source_docs.invalidate_package(root["name"].get<std::string>());
                    auto rr = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
                    if (!rr.ok)
                        result = json_err("package uninstalled but runtime refresh failed: " + rr.message);
                    else
                        result = json_ok_msg("uninstalled");
                } else {
                    result = json_err("failed to uninstall package");
                }
            }
        }
    } else if (method == "link_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("path") || !root["path"].is_string())
                result = json_err("missing 'path'");
            else {
                auto ir = package_manager->link(root["path"].get<std::string>());
                if (ir.success) {
                    source_docs.invalidate_package(ir.info.name, ir.info.path);
                    nlohmann::json res = nlohmann::json::object();
                    res["name"] = ir.info.name;
                    res["version"] = ir.info.version;
                    if (!ir.info.vivid_core.empty())
                        res["vivid_core"] = ir.info.vivid_core;
                    res["operator_count"] = static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size());
                    res["linked"] = true;
                    result = json_ok(std::move(res));
                    // Auto-reload graph if it has missing operators
                    if (const auto* cg = core.compiled_graph()) {
                        for (const auto& cn : cg->nodes) {
                            if (cn.missing_operator) {
                                auto rr = api.reload(has_gpu_ops, has_audio);
                                if (!rr.ok) {
                                    result = json_err("package linked but runtime refresh failed: " + rr.message);
                                }
                                break;
                            }
                        }
                    }
                } else {
                    result = json_err(ir.error);
                }
            }
        }
    } else if (method == "unlink_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                std::string snapshot_json;
                std::string snapshot_error;
                if (!capture_live_graph_snapshot(graph, snapshot_json, snapshot_error)) {
                    result = json_err(snapshot_error);
                } else if (package_manager->unlink(root["name"].get<std::string>())) {
                    source_docs.invalidate_package(root["name"].get<std::string>());
                    auto rr = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
                    if (!rr.ok)
                        result = json_err("package unlinked but runtime refresh failed: " + rr.message);
                    else
                        result = json_ok_msg("unlinked");
                } else {
                    result = json_err("failed to unlink package");
                }
            }
        }
    } else if (method == "rebuild_package") {
        if (!package_manager) {
            result = json_err("package manager not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                const std::string pkg_name = root["name"].get<std::string>();
                std::string snapshot_json;
                std::string snapshot_error;
                if (!capture_live_graph_snapshot(graph, snapshot_json, snapshot_error)) {
                    result = json_err(snapshot_error);
                } else {
                    auto ir = package_manager->rebuild(pkg_name);
                    if (ir.success) {
                        source_docs.invalidate_package(pkg_name,
                            package_manager->resolve_package_path(pkg_name));
                        std::error_code build_ec;
                        std::string pkg_path = package_manager->resolve_package_path(pkg_name);
                        if (!pkg_path.empty()) {
                            std::string build_root = std::filesystem::canonical(pkg_path, build_ec).string();
                            if (build_ec || build_root.empty())
                                build_root = pkg_path;
                            registry.clear_deferred_probe_handles_for_dir(build_root + "/build");
                        }
                        auto rr = api.apply_snapshot_json(snapshot_json, has_gpu_ops, has_audio);
                        if (!rr.ok) {
                            result = json_err("package rebuilt but runtime refresh failed: " + rr.message);
                        } else {
                            core.tick(0.0, 0.016, 0);
                            nlohmann::json res = {
                                {"name", ir.info.name},
                                {"operator_count", static_cast<int64_t>(ir.info.operators.size() + ir.info.gpu_operators.size())},
                                {"linked", ir.info.linked}
                            };
                            result = json_ok(std::move(res));
                        }
                    } else {
                        result = json_err(ir.error);
                    }
                }
            }
        }
    } else if (method == "list_packages") {
        result = handle_list_packages(package_manager);
    } else if (method == "read_package_docs") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_read_package_docs(package_manager, root);
    } else if (method == "list_package_examples") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_list_package_examples(package_manager, root);
    } else if (method == "read_package_example") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_read_package_example(package_manager, root);
    } else if (method == "operator_docs") {
        if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            result = handle_operator_docs(registry, package_manager, source_docs, root, core.subgraph_modules());
        }
    } else if (method == "package_operator_docs") {
        if (!root_valid) result = json_err("invalid JSON body");
        else result = handle_package_operator_docs(registry, package_manager, source_docs, root);
    } else if (method == "test_package") {
        if (!package_manager || !package_compiler) {
            result = json_err("package manager/compiler not available");
        } else if (!root_valid) {
            result = json_err("invalid JSON body");
        } else {
            if (!root.contains("name") || !root["name"].is_string())
                result = json_err("missing 'name'");
            else {
                std::string name = root["name"].get<std::string>();
                auto tr = run_package_tests(name, *package_manager,
                                             *package_compiler, registry);
                if (!tr.error.empty()) {
                    result = json_err(tr.error);
                } else {
                    nlohmann::json res = nlohmann::json::object();
                    res["package"] = tr.package_name;
                    res["summary"] = {{"total", tr.total}, {"passed", tr.passed}, {"failed", tr.failed}, {"skipped", tr.skipped}};
                    if (!tr.notes.empty()) res["notes"] = tr.notes;
                    nlohmann::json tests_arr = nlohmann::json::array();
                    for (const auto& t : tr.tests) {
                        nlohmann::json obj = nlohmann::json::object();
                        obj["name"] = t.name;
                        obj["type"] = t.type;
                        obj["status"] = t.status;
                        if (!t.code.empty()) obj["code"] = t.code;
                        if (!t.reason.empty()) obj["reason"] = t.reason;
                        if (!t.output.empty()) obj["output"] = t.output;
                        tests_arr.push_back(std::move(obj));
                    }
                    res["tests"] = std::move(tests_arr);
                    result = json_ok(std::move(res));
                }
            }
        }
    } else if (method == "set_solo") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            std::string node_id_str;
            if (root.contains("node_id") && root["node_id"].is_string())
                node_id_str = root["node_id"].get<std::string>();
            result = command_result_to_json(api.set_solo(node_id_str));
        }
    } else if (method == "get_solo") {
        std::string solo_id = api.solo_node_id();
        result = nlohmann::json{{"ok", true}, {"node_id", solo_id}, {"active", !solo_id.empty()}}.dump();
    } else if (method == "add_sticky_note") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("text") || !root["text"].is_string() ||
                !root.contains("x") || !root["x"].is_number() ||
                !root.contains("y") || !root["y"].is_number())
                result = json_err("missing 'text', 'x', or 'y'");
            else {
                StickyNoteDef note;
                if (root.contains("id") && root["id"].is_string())
                    note.id = root["id"].get<std::string>();
                else
                    note.id = "sticky_" + std::to_string(graph.sticky_notes().size() + 1);
                note.text = root["text"].get<std::string>();
                note.x = root["x"].get<float>();
                note.y = root["y"].get<float>();
                if (root.contains("width") && root["width"].is_number()) note.width = root["width"].get<float>();
                if (root.contains("height") && root["height"].is_number()) note.height = root["height"].get<float>();
                if (root.contains("color") && root["color"].is_number_integer()) note.color = root["color"].get<int>();
                std::string assigned_id = note.id;
                graph.add_sticky_note(std::move(note));
                result = json_ok(nlohmann::json{{"id", assigned_id}});
            }
        }
    } else if (method == "list_sticky_notes") {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& sn : graph.sticky_notes()) {
            arr.push_back({
                {"id", sn.id}, {"text", sn.text},
                {"x", static_cast<double>(sn.x)}, {"y", static_cast<double>(sn.y)},
                {"width", static_cast<double>(sn.width)}, {"height", static_cast<double>(sn.height)},
                {"color", sn.color}
            });
        }
        result = json_ok(std::move(arr));
    } else if (method == "update_sticky_note") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("id") || !root["id"].is_string())
                result = json_err("missing 'id'");
            else {
                auto* sn = graph.find_sticky_note(root["id"].get<std::string>());
                if (!sn)
                    result = json_err("sticky note not found");
                else {
                    if (root.contains("text") && root["text"].is_string()) sn->text = root["text"].get<std::string>();
                    if (root.contains("x") && root["x"].is_number()) sn->x = root["x"].get<float>();
                    if (root.contains("y") && root["y"].is_number()) sn->y = root["y"].get<float>();
                    if (root.contains("width") && root["width"].is_number()) sn->width = root["width"].get<float>();
                    if (root.contains("height") && root["height"].is_number()) sn->height = root["height"].get<float>();
                    if (root.contains("color") && root["color"].is_number_integer()) sn->color = root["color"].get<int>();
                    result = json_ok_msg("updated");
                }
            }
        }
    } else if (method == "remove_sticky_note") {
        if (!root_valid) { result = json_err("invalid JSON body"); }
        else {
            if (!root.contains("id") || !root["id"].is_string())
                result = json_err("missing 'id'");
            else {
                bool removed = graph.remove_sticky_note(root["id"].get<std::string>());
                if (removed) result = json_ok_msg("removed");
                else result = json_err("sticky note not found");
            }
        }
    } else if (method == "list_assets") {
        if (!asset_library) return json_err("asset library not available");
        auto root = body.empty() ? nlohmann::json::object() : nlohmann::json::parse(body, nullptr, false);
        if (root.is_discarded()) root = nlohmann::json::object();
        result = handle_list_assets(*asset_library, root);
    } else if (method == "inspect_asset") {
        if (!asset_library) return json_err("asset library not available");
        auto root = nlohmann::json::parse(body, nullptr, false);
        if (root.is_discarded()) return json_err("invalid JSON body");
        result = handle_inspect_asset(*asset_library, root);
    } else if (method == "import_asset") {
        if (!asset_library) return json_err("asset library not available");
        auto root = nlohmann::json::parse(body, nullptr, false);
        if (root.is_discarded()) return json_err("invalid JSON body");
        result = handle_import_asset(*asset_library, root);
    } else if (method == "refresh_assets") {
        if (!asset_library) return json_err("asset library not available");
        result = handle_refresh_assets(*asset_library);
    } else {
        result = json_err("unknown method '" + method + "'");
    }

    return result;
}

} // namespace vivid
